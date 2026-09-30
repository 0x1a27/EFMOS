/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_ASSERT_H
#define _EFMLIBC_ASSERT_H
#ifdef __cplusplus
#include_next <assert.h>
#else
#include <stdio.h>
#include <stdlib.h>
/* Minimal assert: print file:line and abort (via exit).
 * NDEBUG disables assertions. */
#ifdef NDEBUG
#  define assert(expr)  ((void)0)
#else
#  define assert(expr)  ((void)((expr) ? 0 : (__assert_fail(#expr, __FILE__, __LINE__, __func__), exit(1))))
void __assert_fail(const char *expr, const char *file, int line, const char *func);
#endif
#undef static_assert
#define static_assert _Static_assert
#endif
#endif
