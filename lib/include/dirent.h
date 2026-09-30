/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_DIRENT_H
#define _EFMLIBC_DIRENT_H
#ifdef __cplusplus
#include_next <dirent.h>
#else
#include <sys/types.h>
struct dirent {
    ino_t  d_ino;
    off_t  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char   d_name[256];
};
#define DT_REG     8
#define DT_DIR     4
#define DT_UNKNOWN 0
typedef struct { long opaque[8]; } DIR;   /* 不透明, 仅用指针 */
DIR           *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int            closedir(DIR *dir);
#endif
#endif
