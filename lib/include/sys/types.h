/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SYS_TYPES_H
#define _EFMLIBC_SYS_TYPES_H
#ifdef __cplusplus
#include_next <sys/types.h>
#else
typedef unsigned long       size_t;
typedef long                ssize_t;
typedef long                off_t;
typedef long                ptrdiff_t;
typedef unsigned long       uintptr_t;
typedef int                 pid_t;
typedef long                time_t;
typedef long                suseconds_t;
typedef long                useconds_t;
typedef int                 clockid_t;
typedef unsigned            mode_t;
typedef unsigned            uid_t;
typedef unsigned            gid_t;
typedef unsigned            dev_t;
typedef unsigned            ino_t;
typedef unsigned            nlink_t;
typedef unsigned long       pthread_t;

/* sys/sysmacros.h: major/minor/makedev (供 loader.c 等 Linux 代码直接调用) */
int       major(dev_t dev);
int       minor(dev_t dev);
dev_t     makedev(unsigned int maj, unsigned int min);
#endif
#endif
