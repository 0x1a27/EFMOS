/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

/* libsync.h — minimal stub for Mesa fence sync (swrast doesn't need real sync) */
#ifndef _LIBSYNC_H_
#define _LIBSYNC_H_
#include <stdint.h>
#include <time.h>

struct sync_merge_data {
    char    name[32];
    int32_t fd2;
    int32_t fence;
};

struct sync_fence_info {
    char    name[32];
    int32_t status;
    uint8_t pt_status;
};

static inline int sync_wait(int fd, int timeout) { (void)fd; (void)timeout; return 0; }
static inline int sync_merge(const char *name, int fd1, int fd2) { (void)name; (void)fd1; (void)fd2; return -1; }
static inline struct sync_fence_info *sync_fence_info(int fd) { (void)fd; return 0; }
static inline void sync_fence_info_free(struct sync_fence_info *info) { (void)info; }

#endif /* _LIBSYNC_H_ */
