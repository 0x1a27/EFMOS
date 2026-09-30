/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Mirrors the Linux kernel uapi type aliases (GPL-2.0-or-later / BSD-2-Clause).
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_LINUX_TYPES_H
#define _EFMLIBC_LINUX_TYPES_H
/* linux/types.h 兼容: Mesa drm-uapi/drm.h 需要 __u32/__u16/__u64 等 */
#include <sys/types.h>

typedef unsigned char   __u8;
typedef unsigned short  __u16;
typedef unsigned int    __u32;
typedef unsigned long long __u64;
typedef signed char     __s8;
typedef short           __s16;
typedef int             __s32;
typedef long long       __s64;

typedef unsigned short  __le16;
typedef unsigned int    __le32;
typedef unsigned long long __le64;
typedef unsigned short  __be16;
typedef unsigned int    __be32;
typedef unsigned long long __be64;

typedef unsigned long   __kernel_size_t;
typedef long            __kernel_ssize_t;
typedef long            __kernel_long_t;
typedef int             __kernel_pid_t;
typedef unsigned int    __kernel_uid32_t;
typedef unsigned int    __kernel_gid32_t;

#endif
