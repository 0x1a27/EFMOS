/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SYS_IOCTL_H
#define _EFMLIBC_SYS_IOCTL_H
#ifdef __cplusplus
#include_next <sys/ioctl.h>
#else
#include <asm/ioctl.h>

/* ioctl() — EFMOS stub: DRM ioctls 走 drmIoctl(), 不走系统调用 */
int ioctl(int fd, unsigned long request, ...);

#endif
#endif
