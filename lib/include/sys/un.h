/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SYS_UN_H
#define _EFMLIBC_SYS_UN_H
#ifdef __cplusplus
#include_next <sys/un.h>
#else
#include <sys/socket.h>

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[108];
};
#define UNIX_PATH_MAX 108
#endif
#endif
