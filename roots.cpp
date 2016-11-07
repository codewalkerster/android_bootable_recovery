/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>

#include <fs_mgr.h>
#include "mtdutils/mtdutils.h"
#include "mtdutils/mounts.h"
#include "roots.h"
#include "common.h"
#include "make_ext4fs.h"
extern "C" {
#include "wipe.h"
#include "cryptfs.h"
}

static struct fstab *fstab = NULL;

extern struct selabel_handle *sehandle;


#define NUM_OF_BLKDEVICE_TO_ENUM    3
#define NUM_OF_PARTITION_TO_ENUM    6

void load_volume_table()
{
    int i;
    int ret;

    fstab = fs_mgr_read_fstab("/etc/recovery.fstab");
    if (!fstab) {
        LOGE("failed to read /etc/recovery.fstab\n");
        return;
    }

    ret = fs_mgr_add_entry(fstab, "/tmp", "ramdisk", "ramdisk");
    if (ret < 0 ) {
        LOGE("failed to add /tmp entry to fstab\n");
        fs_mgr_free_fstab(fstab);
        fstab = NULL;
        return;
    }

    printf("recovery filesystem table\n");
    printf("=========================\n");
    for (i = 0; i < fstab->num_entries; ++i) {
        Volume* v = &fstab->recs[i];
        printf("  %d %s %s %s %lld\n", i, v->mount_point, v->fs_type,
               v->blk_device, v->length);
    }
    printf("\n");
}

Volume* volume_for_path(const char* path) {
    return fs_mgr_get_entry_for_mount_point(fstab, path);
}

static int mount_fs_rdonly(char *device_name, Volume *vol, const char *fs_type) {
    if (!mount(device_name, vol->mount_point, fs_type,
        MS_NOATIME | MS_NODEV | MS_NODIRATIME | MS_RDONLY, 0)) {
        LOGW("successful to mount %s on %s by read-only\n",
            device_name, vol->mount_point);
        return 0;
    } else {
        LOGE("failed to mount %s on %s by read-only (%s)\n",
            device_name, vol->mount_point, strerror(errno));
    }

    return -1;
}

int auto_mount_fs(char *device_name, Volume *vol) {
    if (access(device_name, F_OK)) {
        return -1;
    }

    if (!strcmp(vol->fs_type, "auto")) {
        if (!mount(device_name, vol->mount_point, "vfat",
            MS_NOATIME | MS_NODEV | MS_NODIRATIME, "")) {
            goto auto_mounted;
        } else {
            if (strstr(vol->mount_point, "sdcard")) {
                LOGW("failed to mount %s on %s (%s).try read-only ...\n",
                    device_name, vol->mount_point, strerror(errno));
                if (!mount_fs_rdonly(device_name, vol, "vfat")) {
                    goto auto_mounted;
                }
            }
        }

        if (!mount(device_name, vol->mount_point, "ntfs",
            MS_NOATIME | MS_NODEV | MS_NODIRATIME, "")) {
            goto auto_mounted;
        } else {
            if (strstr(vol->mount_point, "sdcard")) {
                LOGW("failed to mount %s on %s (%s).try read-only ...\n",
                    device_name, vol->mount_point, strerror(errno));
                if (!mount_fs_rdonly(device_name, vol, "ntfs")) {
                    goto auto_mounted;
                }
            }
        }

        if (!mount(device_name, vol->mount_point, "exfat",
            MS_NOATIME | MS_NODEV | MS_NODIRATIME, "")) {
            goto auto_mounted;
        } else {
            if (strstr(vol->mount_point, "sdcard")) {
                LOGW("failed to mount %s on %s (%s).try read-only ...\n",
                    device_name, vol->mount_point, strerror(errno));
                if (!mount_fs_rdonly(device_name, vol, "exfat")) {
                    goto auto_mounted;
                }
            }
        }
    } else {
        if(!mount(device_name, vol->mount_point, vol->fs_type,
            MS_NOATIME | MS_NODEV | MS_NODIRATIME, "")) {
            goto auto_mounted;
        } else {
            if (strstr(vol->mount_point, "sdcard")) {
                LOGW("failed to mount %s on %s (%s).try read-only ...\n",
                    device_name, vol->mount_point, strerror(errno));
                if (!mount_fs_rdonly(device_name, vol, vol->fs_type)) {
                    goto auto_mounted;
                }
            }
        }
    }

    return -1;

auto_mounted:
    return 0;
}

int customize_smart_device_mounted(
    Volume *vol) {
    int i = 0, j = 0;
    int first_position = 0;
    int second_position = 0;
    char * tmp = NULL;
    char *mounted_device = NULL;
    char device_name[256] = {0};
    const char *usb_device = "/dev/block/sd";
    const char *sdcard_device = "/dev/block/mmcblk";

    if (vol->blk_device != NULL) {
        int num = 0;
        const char *blk_device = vol->blk_device;
        for (; *blk_device != '\0'; blk_device ++) {
            if (*blk_device == '#') {
                num ++;
            }
        }

        /*
        * Contain two '#' for blk_device name in recovery.fstab
        * such as /dev/block/sd## (udisk)
        * such as /dev/block/mmcblk#p# (sdcard)
        */
        if (num != 2) {
            return 1;   // Don't contain two '#'
        }

        if (access(vol->mount_point, F_OK)) {
            mkdir(vol->mount_point, 0755);
        }

        // find '#' position
        if (strchr(vol->blk_device, '#')) {
            tmp = strchr(vol->blk_device, '#');
            first_position = tmp - vol->blk_device;
            if (strlen(tmp+1) > 0 && strchr(tmp+1, '#')) {
                tmp = strchr(tmp+1, '#');
                second_position = tmp - vol->blk_device;
            }
        }

        if (!first_position || !second_position) {
            LOGW("decompose blk_device error(%s) in recovery.fstab\n",
                vol->blk_device);
            return -1;
        }

        int copy_len = (strlen(vol->blk_device) < sizeof(device_name)) ?
            strlen(vol->blk_device) : sizeof(device_name);

        for (i = 0; i < NUM_OF_BLKDEVICE_TO_ENUM; i ++) {
            memset(device_name, '\0', sizeof(device_name));
            strncpy(device_name, vol->blk_device, copy_len);

            if (!strncmp(device_name, sdcard_device, strlen(sdcard_device))) {
                // start from '0' for mmcblk0p#
                device_name[first_position] = '0' + i;
            } else if (!strncmp(device_name, usb_device, strlen(usb_device))) {
                // start from 'a' for sda#
                device_name[first_position] = 'a' + i;
            }

            for (j = 1; j <= NUM_OF_PARTITION_TO_ENUM; j ++) {
                device_name[second_position] = '0' + j;
                if (!access(device_name, F_OK)) {
                    LOGW("try mount %s ...\n", device_name);
                    if (!auto_mount_fs(device_name, vol)) {
                        mounted_device = device_name;
                        LOGW("successful to mount %s\n", device_name);
                        goto mounted;
                    }
                }
            }

            if (!strncmp(device_name, sdcard_device, strlen(sdcard_device))) {
                // mmcblk0p1->mmcblk0
                device_name[strlen(device_name) - 2] = '\0';
                // TODO: Here,need to distinguish between cards and flash at best
            } else if (!strncmp(device_name, usb_device, strlen(usb_device))) {
                // sda1->sda
                device_name[strlen(device_name) - 1] = '\0';
            }

            if (!access(device_name, F_OK)) {
                LOGW("try mount %s ...\n", device_name);
                if (!auto_mount_fs(device_name, vol)) {
                    mounted_device = device_name;
                    LOGW("successful to mount %s\n", device_name);
                    goto mounted;
                }
            }
        }
    } else {
        LOGE("Can't get blk_device\n");
    }

    return -1;

mounted:
    return 0;
}

int smart_device_mounted(Volume *vol) {
    int i = 0, len = 0;
    char * tmp = NULL;
    char device_name[256] = {0};
    char *mounted_device = NULL;

    mkdir(vol->mount_point, 0755);

    if (vol->blk_device != NULL) {
        int ret = customize_smart_device_mounted(vol);
        if (ret <= 0) {
            return ret;
        }
    }

    if (vol->blk_device != NULL) {
        tmp = strchr(vol->blk_device, '#');
        len = tmp - vol->blk_device;
        if (tmp && len < 255) {
            strncpy(device_name, vol->blk_device, len);
            for (i = 1; i <= NUM_OF_PARTITION_TO_ENUM; i++) {
                device_name[len] = '0' + i;
                device_name[len + 1] = '\0';
                LOGW("try mount %s ...\n", device_name);
                if (!access(device_name, F_OK)) {
                    if (!auto_mount_fs(device_name, vol)) {
                        mounted_device = device_name;
                        LOGW("successful to mount %s\n", device_name);
                        goto mounted;
                    }
                }
            }

            const char *mmcblk = "/dev/block/mmcblk";
            if (!strncmp(device_name, mmcblk, strlen(mmcblk))) {
                device_name[len - 1] = '\0';
            } else {
                device_name[len] = '\0';
            }

            LOGW("try mount %s ...\n", device_name);
            if (!access(device_name, F_OK)) {
                if (!auto_mount_fs(device_name, vol)) {
                    mounted_device = device_name;
                    LOGW("successful to mount %s\n", device_name);
                    goto mounted;
                }
            }
        } else {
            LOGW("try mount %s ...\n", vol->blk_device);
            strncpy(device_name, vol->blk_device, sizeof(device_name));
            if (!access(device_name, F_OK)) {
                if (!auto_mount_fs(device_name, vol)) {
                    mounted_device = device_name;
                    LOGW("successful to mount %s\n", device_name);
                    goto mounted;
                }
            }
        }
    }

    return -1;

mounted:
    return 0;
}

int ensure_path_mounted(const char* path) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOGE("unknown volume for path [%s]\n", path);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 0;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return -1;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv) {
        // volume is already mounted
        return 0;
    }

    mkdir(v->mount_point, 0755);  // in case it doesn't already exist

    if (strcmp(v->fs_type, "yaffs2") == 0) {
        // mount an MTD partition as a YAFFS2 filesystem.
        mtd_scan_partitions();
        const MtdPartition* partition;
        partition = mtd_find_partition_by_name(v->blk_device);
        if (partition == NULL) {
            LOGE("failed to find \"%s\" partition to mount at \"%s\"\n",
                 v->blk_device, v->mount_point);
            return -1;
        }
        return mtd_mount_partition(partition, v->mount_point, v->fs_type, 0);
    } else if (strcmp(v->fs_type, "ext4") == 0) {
        if (strstr(v->mount_point, "system")) {
            if (!mount(v->blk_device, v->mount_point, v->fs_type,
                 MS_NOATIME | MS_NODEV | MS_NODIRATIME | MS_RDONLY, "")) {
                 return 0;
            }
        } else {
            if (!mount(v->blk_device, v->mount_point, v->fs_type,
                 MS_NOATIME | MS_NODEV | MS_NODIRATIME, "discard")) {
                 return 0;
            }
        }
        LOGE("failed to mount %s (%s)\n", v->mount_point, strerror(errno));
        return -1;
    } else if (strcmp(v->fs_type, "vfat") == 0 ||
               strcmp(v->fs_type, "auto") == 0 ) {
        if (strstr(v->mount_point, "sdcard") || strstr(v->mount_point, "udisk")){
            int time_out = 2000000;
            while (time_out) {
                if (!smart_device_mounted(v)) {
                    return 0;
                }
                usleep(100000);
                time_out -= 100000;
            }
        } else {
            if (!mount(v->blk_device, v->mount_point, v->fs_type,
                MS_NOATIME | MS_NODEV | MS_NODIRATIME | MS_RDONLY, "")) {
                return 0;
            }
        }
        LOGE("failed to mount %s (%s)\n", v->mount_point, strerror(errno));
        return -1;
    } else if (strcmp(v->fs_type, "squashfs") == 0 ||
               strcmp(v->fs_type, "f2fs") == 0) {
        result = mount(v->blk_device, v->mount_point, v->fs_type,
                       v->flags, v->fs_options);
        if (result == 0) return 0;
        LOGE("failed to mount %s (%s)\n", v->mount_point, strerror(errno));
        return -1;
    }

    LOGE("unknown fs_type \"%s\" for %s\n", v->fs_type, v->mount_point);
    return -1;
}

int ensure_path_unmounted(const char* path) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOGE("unknown volume for path [%s]\n", path);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted; you can't unmount it.
        return -1;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return -1;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        // volume is already unmounted
        return 0;
    }

    return unmount_mounted_volume(mv);
}

static int exec_cmd(const char* path, char* const argv[]) {
    int status;
    pid_t child;
    if ((child = vfork()) == 0) {
        execv(path, argv);
        _exit(-1);
    }
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("%s failed with status %d\n", path, WEXITSTATUS(status));
    }
    return WEXITSTATUS(status);
}

int format_volume(const char* volume) {
    Volume* v = volume_for_path(volume);
    if (v == NULL) {
        LOGE("unknown volume \"%s\"\n", volume);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", volume);
        return -1;
    }
    if (strcmp(v->mount_point, volume) != 0) {
        LOGE("can't give path \"%s\" to format_volume\n", volume);
        return -1;
    }

    if (ensure_path_unmounted(volume) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }

    if (strcmp(v->fs_type, "yaffs2") == 0 || strcmp(v->fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(v->blk_device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", v->blk_device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", v->blk_device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", v->blk_device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n", v->blk_device);
            return -1;
        }
        return 0;
    }

    if (strcmp(v->fs_type, "ext4") == 0 || strcmp(v->fs_type, "f2fs") == 0) {
        // if there's a key_loc that looks like a path, it should be a
        // block device for storing encryption metadata.  wipe it too.
        if (v->key_loc != NULL && v->key_loc[0] == '/') {
            LOGI("wiping %s\n", v->key_loc);
            int fd = open(v->key_loc, O_WRONLY | O_CREAT, 0644);
            if (fd < 0) {
                LOGE("format_volume: failed to open %s\n", v->key_loc);
                return -1;
            }
            wipe_block_device(fd, get_file_size(fd));
            close(fd);
        }

        ssize_t length = 0;
        if (v->length != 0) {
            length = v->length;
        } else if (v->key_loc != NULL && strcmp(v->key_loc, "footer") == 0) {
            length = -CRYPT_FOOTER_OFFSET;
        }
        int result;
        if (strcmp(v->fs_type, "ext4") == 0) {
            if (ext4_erase_volum(v->blk_device)) {
                LOGE("format_volume: ext4_erase_volum failed on %s\n", v->blk_device);
                return -1;
            }
            result = make_ext4fs(v->blk_device, length, volume, sehandle);
        } else {   /* Has to be f2fs because we checked earlier. */
            if (v->key_loc != NULL && strcmp(v->key_loc, "footer") == 0 && length < 0) {
                LOGE("format_volume: crypt footer + negative length (%zd) not supported on %s\n", length, v->fs_type);
                return -1;
            }
            if (length < 0) {
                LOGE("format_volume: negative length (%zd) not supported on %s\n", length, v->fs_type);
                return -1;
            }
            char *num_sectors;
            if (asprintf(&num_sectors, "%zd", length / 512) <= 0) {
                LOGE("format_volume: failed to create %s command for %s\n", v->fs_type, v->blk_device);
                return -1;
            }
            const char *f2fs_path = "/sbin/mkfs.f2fs";
            const char* const f2fs_argv[] = {"mkfs.f2fs", "-t", "-d1", v->blk_device, num_sectors, NULL};

            result = exec_cmd(f2fs_path, (char* const*)f2fs_argv);
            free(num_sectors);
        }
        if (result != 0) {
            LOGE("format_volume: make %s failed on %s with %d(%s)\n", v->fs_type, v->blk_device, result, strerror(errno));
            return -1;
        }
        return 0;
    }

    LOGE("format_volume: fs_type \"%s\" unsupported\n", v->fs_type);
    return -1;
}

int setup_install_mounts() {
    if (fstab == NULL) {
        LOGE("can't set up install mounts: no fstab loaded\n");
        return -1;
    }

    for (int i = 0; i < fstab->num_entries; ++i) {
        Volume* v = fstab->recs + i;

        if (strcmp(v->mount_point, "/tmp") == 0 ) {
            if (ensure_path_mounted(v->mount_point) != 0) {
                LOGE("failed to mount %s\n", v->mount_point);
                return -1;
            }
        } else if (strcmp(v->mount_point, "/cache") == 0){
            if (ensure_path_mounted(v->mount_point) != 0) {
                format_volume("/cache");
                if (ensure_path_mounted(v->mount_point) != 0) {
                    LOGE("failed to mount %s\n", v->mount_point);
                    return -1;
                }
            }
        } else {
            if (ensure_path_unmounted(v->mount_point) != 0) {
                LOGE("failed to unmount %s\n", v->mount_point);
                return -1;
            }
        }
    }
    return 0;
}

int instaboot_clear() {
    static bool done = false;
    const char* swap_dev = "/dev/block/instaboot";

    if (done)
        return -2;

    int fd = open(swap_dev, O_RDWR);

    if (fd > 0) {
        char context[2048] = {0};
        for (int i = 0; i < 8; i++) {
            if (write(fd, context, sizeof(context)) != sizeof(context)) {
                LOGE("instaboot: write %s fail!", swap_dev);
            }
        }
        fsync(fd);
        close(fd);
        usleep(10000);
        done = true;
    } else {
        LOGW("instaboot: cannot open device");
    }

    return 0;
}

int instaboot_disable(){
    static bool done = false;

    if (done)
        return -2;

    ensure_path_mounted("/data");
    unlink("/data/property/persist.sys.instaboot.enable");
    ensure_path_unmounted("/data");

    done = true;

    return 0;
}
