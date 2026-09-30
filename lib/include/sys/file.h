/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SYS_FILE_H
#define _EFMLIBC_SYS_FILE_H
#ifdef __cplusplus
#include_next <sys/file.h>
#else
#include <sys/types.h>

/* flock 操作类型 */
#define LOCK_SH  1   /* 共享锁 */
#define LOCK_EX  2   /* 排他锁 */
#define LOCK_UN  8   /* 解锁 */
#define LOCK_NB  4   /* 非阻塞 */

/* EFMOS freestanding: 文件锁无意义, flock 返回成功 */
int flock(int fd, int operation);

#endif
#endif
