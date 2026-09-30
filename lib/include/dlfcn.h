/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_DLFCN_H
#define _EFMLIBC_DLFCN_H
#ifdef __cplusplus
#include_next <dlfcn.h>
#else
#define RTLD_LAZY   0x00001
#define RTLD_NOW    0x00002
#define RTLD_GLOBAL 0x00100
#define RTLD_LOCAL  0x00000
void *dlopen(const char *file, int mode);
void *dlsym(void *handle, const char *name);
int   dlclose(void *handle);
char *dlerror(void);
#endif
#endif
