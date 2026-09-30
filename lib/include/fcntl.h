/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_FCNTL_H
#define _EFMLIBC_FCNTL_H
#ifdef __cplusplus
#include_next <fcntl.h>
#else
#include <sys/types.h>
/* fcntl.h commands — minimal set Mesa uses for open() flags */
#define O_RDONLY   00
#define O_WRONLY   01
#define O_RDWR     02
#define O_CREAT    0100
#define O_EXCL     0200
#define O_NOCTTY   0400
#define O_TRUNC    01000
#define O_APPEND   02000
#define O_NONBLOCK 04000
#define O_DSYNC    010000
#define O_SYNC     04010000
#define O_DIRECT   040000
#define O_LARGEFILE 0100000
#define O_CLOEXEC  02000000
#define O_NOFOLLOW 0400000
#define O_TMPFILE  020200000
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#define FD_CLOEXEC 1
#define AT_FDCWD (-100)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags, ...);
int fcntl(int fd, int cmd, ...);
int creat(const char *path, mode_t mode);
#endif
#endif
