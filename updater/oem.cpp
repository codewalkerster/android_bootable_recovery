/*
 * (C) Copyright 2020 Hardkernel Co,. Ltd
 * Luke.go, <luke.go@hardkernel.com>
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <dirent.h>
#include <unistd.h>

#include <string>

#include "mbr.h"

#define MPT_NODE "/dev/block/ptable"

#define MAX_MPT_PART_NAME_LEN   16
#define MAX_MPT_PART_NUM        32

struct mpt_partition {
    char name[MAX_MPT_PART_NAME_LEN];
    uint64_t size;
    uint64_t offset;
    unsigned int mask_flags;
};

struct ptable_t {
    char magic[4];
    char version[12];
    int nr_parts;
    int checksum;
    struct mpt_partition partitions[MAX_MPT_PART_NUM];
};

#define MPT_MAGIC "MPT"
static inline int mpt_magic_check(const char *p) {
    return strncmp(p, MPT_MAGIC, strlen(MPT_MAGIC));
}

#define MPT_VERSION "01.00.00"
static inline int mpt_version_check(const char *p) {
    return strncmp(p, MPT_VERSION, strlen(MPT_VERSION));
}

#define SYS_BLK "/sys/block"

static uint64_t get_block_device_size() {
    DIR *dir;
    struct dirent *entry;

    if (!(dir = opendir(SYS_BLK)))
        return -1;

    while ((entry = readdir(dir)) != NULL) {
        if ((entry->d_type == DT_DIR)||(entry->d_type == DT_LNK)) {
            char path[1024];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
            snprintf(path, sizeof(path), "%s/%s/odm", SYS_BLK, entry->d_name);

            if (access(path, F_OK) != -1) {
                snprintf(path, sizeof(path), "%s/%s/size", SYS_BLK, entry->d_name);
                FILE *blk_size = fopen(path, "r");

                char blk[1024];
                fgets(blk, 1024, blk_size);

                fclose(blk_size);
                closedir(dir);

                return atoi(blk);
            }
        } else {
            printf("no directory - %s\n", entry->d_name);
        }
    }

    closedir(dir);

    return -1;
}

static inline int get_idx(struct ptable_t* mpt, const char* name) {
    int i=0;
    for (; i < mpt->nr_parts; i++) {
        if (!strncmp(mpt->partitions[i].name, name, sizeof(&name)))
            break;
    }
    return i;
}

bool fdisk() {
    // resize mpt's data partition size
    struct ptable_t *mpt;
    FILE *pf = fopen(MPT_NODE, "r");
    if (pf == NULL) {
        printf("fopen %s failed.\n", MPT_NODE);
        return false;
    }

    mpt = (struct ptable_t *) malloc (sizeof(struct ptable_t));

    size_t result = fread(mpt, sizeof(struct ptable_t), 1, pf);
    fclose(pf);
    if (result != 1) {
        printf("reading a mpt is failed.\n");
        return false;
    }

    if (mpt_magic_check(mpt->magic) || mpt_version_check(mpt->version)) {
        printf("wrong mpt\n");
        return false;
    }

	std::string target_part = "data";
    int i = get_idx(mpt, target_part.c_str());

    if (i == mpt->nr_parts) {
        printf("data partition is not exist.\n");
        return false;
    }

    uint64_t blk_size = get_block_device_size();
    mpt->partitions[i].size = blk_size - mpt->partitions[i].offset;

    uint64_t data_size = mpt->partitions[i].size;
    uint64_t data_offset = mpt->partitions[i].offset;

    pf = fopen(MPT_NODE, "wb");
    long int part_offset = offsetof(struct ptable_t, partitions);
    long int offset = part_offset + (sizeof(struct mpt_partition) * i);
    fseek(pf, offset, SEEK_SET);

    result = fwrite(&(mpt->partitions[i]), sizeof(struct mpt_partition), 1, pf);
    free(mpt);
    fclose(pf);

    if (result != 1) {
        printf("Failed to wraite data to mpt\n");
        return false;
    }

    // resize mbr's data partition size
    return resize_mbr(data_size, data_offset);
}
