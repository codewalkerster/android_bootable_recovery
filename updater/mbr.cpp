/*
 * (C) Copyright 2020 Hardkernel Co,. Ltd
 * Luke.go, <luke.go@hardkernel.com>
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <stdio.h>

#include "mbr.h"

#define MBR_NODE "/dev/block/@MBR"

#define PART_TYPE_LINUX_NATIVE_FS 0x83

#define DOS_PART_TBL_OFFSET 0x1be

typedef struct dos_partition {
    unsigned char boot_ind;
    unsigned char head;
    unsigned char sector;
    unsigned char cyl;
    unsigned char sys_ind;
    unsigned char end_head;
    unsigned char end_sector;
    unsigned char end_cyl;
    unsigned char start4[4];
    unsigned char size4[4];
} dos_partition_t;

static void dos_partition_entry(dos_partition_t *part,
        uint32_t start, uint32_t size, unsigned char type) {
    part->boot_ind = 0x00;
    part->head = 0;
    part->sector = 1;
    part->cyl = 1;
    part->sys_ind = type;
    part->end_head = 0;
    part->end_sector = 0;
    part->end_cyl = 0;

    uint32_t *p = (uint32_t *)part->start4;
    *p = start;
    p = (uint32_t *)part->size4;
    *p = size;
}

bool resize_mbr(uint64_t data_size, uint64_t data_offset) {
    FILE *mbr = fopen(MBR_NODE, "wb");
    if (mbr == NULL) {
        printf("open %s failed.\n", MBR_NODE);
        return false;
    }

    dos_partition_t mbr_data;

    dos_partition_entry(&mbr_data,
            data_offset, data_size, PART_TYPE_LINUX_NATIVE_FS);

    fseek(mbr, DOS_PART_TBL_OFFSET + sizeof(dos_partition_t), SEEK_SET); //change 0x1da work?
    int result = fwrite(&(mbr_data), sizeof(dos_partition_t), 1, mbr);
    fclose(mbr);

    if (result < 0) {
        printf ("result - %d\n", result);
        return false;
    }

    return true;
}
