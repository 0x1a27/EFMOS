/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_POLL_H
#define _EFMLIBC_POLL_H
#ifdef __cplusplus
#include_next <poll.h>
#else
#include <sys/types.h>
#include <time.h>

struct pollfd {
    int fd;
    short events;
    short revents;
};
typedef unsigned long nfds_t;

#define POLLIN     0x001
#define POLLPRI    0x002
#define POLLOUT    0x004
#define POLLERR    0x008
#define POLLHUP    0x010
#define POLLNVAL   0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#define POLLWRNORM 0x100
#define POLLWRBAND 0x200
#define POLLMSG    0x400
#define POLLREMOVE 0x1000
#define POLLRDHUP  0x2000

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo, const void *sigmask);
#endif
#endif
