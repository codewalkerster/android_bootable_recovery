/*
 * (C) Copyright 2008 - 2009
 * Windriver, <www.windriver.com>
 * Tom Rix <Tom.Rix@windriver.com>
 *
 * Copyright 2011 Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * Copyright 2014 Linaro, Ltd.
 * Rob Herring <robh@kernel.org>
 *
 * Copyright 2014 Hardkernel Co,.Ltd
 * Dongjin Kim <tobetter@gmail.com>
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <ctype.h>
#include <stdio.h>
#include <sys/mount.h>
#include <fcntl.h>

#include "edify/expr.h"

#ifdef DEBUG
#define FBTDBG(fmt, args...)\
        printf("DEBUG: [%s]: %d:\n"fmt, __func__, __LINE__, ##args)
#else
#define FBTDBG(fmt, args...) do {} while (0)
#endif

#ifdef INFO
#define FBTINFO(fmt, args...)\
        printf("INFO: [%s]: "fmt, __func__, ##args)
#else
#define FBTINFO(fmt, args...) do {} while (0)
#endif

#ifdef WARN
#define FBTWARN(fmt, args...)\
        printf("WARNING: [%s]: "fmt, __func__, ##args)
#else
#define FBTWARN(fmt, args...) do {} while (0)
#endif

#ifdef ERR
#define FBTERR(fmt, args...)\
        printf("ERROR: [%s]: "fmt, __func__, ##args)
#else
#define FBTERR(fmt, args...) do {} while (0)
#endif

typedef unsigned long lbaint_t;
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct sparse_header {
    __le32        magic;          /* 0xed26ff3a */
    __le16        major_version;  /* (0x1) - reject images with higher major versions */
    __le16        minor_version;  /* (0x0) - allow images with higer minor versions */
    __le16        file_hdr_sz;    /* 28 bytes for first revision of the file format */
    __le16        chunk_hdr_sz;   /* 12 bytes for first revision of the file format */
    __le32        blk_sz;         /* block size in bytes, must be a multiple of 4 (4096) */
    __le32        total_blks;     /* total blocks in the non-sparse output image */
    __le32        total_chunks;   /* total chunks in the sparse input image */
    __le32        image_checksum; /* CRC32 checksum of the original data, counting "don't care" */
    /* as 0. Standard 802.3 polynomial, use a Public Domain */
    /* table implementation */
} sparse_header_t;

#define SPARSE_HEADER_MAGIC     0xed26ff3a

#define CHUNK_TYPE_RAW          0xCAC1
#define CHUNK_TYPE_FILL         0xCAC2
#define CHUNK_TYPE_DONT_CARE    0xCAC3

typedef struct chunk_header {
    __le16        chunk_type;     /* 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care */
    __le16        reserved1;
    __le32        chunk_sz;       /* in blocks in output image */
    __le32        total_sz;       /* in bytes of chunk input file including chunk header and data */
} chunk_header_t;

#define SPARSE_HEADER_MAJOR_VER 1
#include <sys/statvfs.h>
static int do_unsparse(int fd, unsigned char *source,
        unsigned int sector)
{
    sparse_header_t *header = (void *) source;
    u32 i;
    unsigned long section_size;
    u64 outlen = 0;

    unsigned long blocks;
    unsigned long blk_sz;

    ioctl(fd, BLKGETSIZE, &blocks);
    ioctl(fd, BLKSSZGET, &blk_sz);

    section_size = blocks * blk_sz;

    FBTINFO("sparse_header:\n");
    FBTINFO("\t         magic=0x%08X\n", header->magic);
    FBTINFO("\t       version=%u.%u\n", header->major_version,
            header->minor_version);
    FBTINFO("\t file_hdr_size=%u\n", header->file_hdr_sz);
    FBTINFO("\tchunk_hdr_size=%u\n", header->chunk_hdr_sz);
    FBTINFO("\t        blk_sz=%u\n", header->blk_sz);
    FBTINFO("\t    total_blks=%u\n", header->total_blks);
    FBTINFO("\t  total_chunks=%u\n", header->total_chunks);
    FBTINFO("\timage_checksum=%u\n", header->image_checksum);

    if (header->magic != SPARSE_HEADER_MAGIC) {
        printf("sparse: bad magic\n");
        return 1;
    }

    if (((u64)header->total_blks * header->blk_sz) > section_size) {
        printf("sparse: section size %lu MB limit: exceeded\n",
                section_size / (1024 * 1024));
        return 1;
    }

    if ((header->major_version != SPARSE_HEADER_MAJOR_VER) ||
            (header->file_hdr_sz != sizeof(sparse_header_t)) ||
            (header->chunk_hdr_sz != sizeof(chunk_header_t))) {
        printf("sparse: incompatible format\n");
        return 1;
    }

    /* Skip the header now */
    source += header->file_hdr_sz;

    for (i = 0; i < header->total_chunks; i++) {
        u64 clen = 0;
        u64 blkcnt;
        chunk_header_t *chunk = (void *) source;

        FBTINFO("chunk_header:\n");
        FBTINFO("\t    chunk_type=%u\n", chunk->chunk_type);
        FBTINFO("\t      chunk_sz=%u\n", chunk->chunk_sz);
        FBTINFO("\t      total_sz=%u\n", chunk->total_sz);

        /* move to next chunk */
        source += sizeof(chunk_header_t);

        switch (chunk->chunk_type) {
            case CHUNK_TYPE_RAW:
                clen = (u64)chunk->chunk_sz * header->blk_sz;
                FBTINFO("sparse: RAW blk=%d bsz=%d:"
                        " write(sector=%d,clen=%d)\n",
                        chunk->chunk_sz, header->blk_sz, sector, clen);

                if (chunk->total_sz != (clen + sizeof(chunk_header_t))) {
                    printf("sparse: bad chunk size for"
                            " chunk %d, type Raw\n", i);
                    return 1;
                }

                outlen += clen;
                if (outlen > section_size) {
                    printf("sparse: section size %lu MB limit:"
                            " exceeded\n", section_size / (1024 * 1024));
                    return 1;
                }
                blkcnt = clen / blk_sz;
                FBTDBG("sparse: RAW blk=%d bsz=%d:"
                        " write(sector=%d,clen=%llu)\n",
                        chunk->chunk_sz, header->blk_sz, sector, clen);

                lseek64(fd, (off64_t)sector * blk_sz, SEEK_SET);
                if (write(fd, source, (size_t)clen) != clen) {
                    printf("sparse: block write to sector %d"
                            " of %llu bytes (%llu blkcnt) failed\n",
                            sector, clen, blkcnt);
                    return 1;
                }

                sector += (clen / blk_sz);
                source += clen;
                break;

            case CHUNK_TYPE_DONT_CARE:
                if (chunk->total_sz != sizeof(chunk_header_t)) {
                    printf("sparse: bogus DONT CARE chunk\n");
                    return 1;
                }
                clen = (u64)chunk->chunk_sz * header->blk_sz;
                FBTDBG("sparse: DONT_CARE blk=%d bsz=%d:"
                        " skip(sector=%d,clen=%llu)\n",
                        chunk->chunk_sz, header->blk_sz, sector, clen);

                outlen += clen;
                if (outlen > section_size) {
                    printf("sparse: section size %lu MB limit:"
                            " exceeded\n", section_size / (1024 * 1024));
                    return 1;
                }
                sector += (clen / blk_sz);
                break;

            default:
                printf("sparse: unknown chunk ID %04x\n",
                        chunk->chunk_type);
                return 1;
        }
    }

    printf("sparse: out-length %llu MB\n", outlen / (1024 * 1024));

    return 0;
}

Value* ExtractSparseToFile(State *state, void *image_start_ptr, char *name)
{
    /* Check if we have sparse compressed image */
    if (((sparse_header_t *)image_start_ptr)->magic
            == SPARSE_HEADER_MAGIC) {
        printf("fastboot: %s is in sparse format\n", name);

        int fd = open(name, O_WRONLY);

        if (!do_unsparse(fd, image_start_ptr, 0)) {
            printf("Writing sparsed: '%s' DONE!\n", name);
            close(fd);
            return 1;
        }

        close(fd);

        printf("Writing sparsed '%s' FAILED!\n", name);
    }

    return 0;
}
