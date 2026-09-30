/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_STDARG_H

/* __need___va_list 模式: 系统头 (如 glibc <stdio.h>) 以此模式包含 <stdarg.h>
 * 时, 只需要 __gnuc_va_list, 不需要 va_start/va_end 等宏.
 * 不能在此模式下设置我们的 guard, 否则后续正常包含 <stdarg.h> 会被跳过,
 * 导致 va_start 等宏永远不定义. */
#ifdef __need___va_list
#include_next <stdarg.h>
#else
#define _EFMLIBC_STDARG_H
#include_next <stdarg.h>
#endif

#endif
