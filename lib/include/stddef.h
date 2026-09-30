/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

/* stddef.h — 基本类型定义 (替代 GCC 内置 stddef.h)
 *
 * 关键: GCC 内置 stddef.h 支持 __need_size_t 等"部分包含"模式.
 * 当 <string.h> 等系统头 #define __need_size_t 后 #include <stddef.h> 时,
 * GCC 内置只定义 size_t, 不定义 _STDDEF_H, 也不定义 max_align_t.
 *
 * 如果我们的 include guard 在 __need_* 模式下也被设置, 后续 <cstddef> 的
 * 正常 #include <stddef.h> 会被跳过, 导致 max_align_t 永远不会定义.
 *
 * 修复: 仅在非 __need_* 模式下设置我们的 guard. */
#ifndef _EFMLIBC_STDDEF_H

/* __need_* 模式: 直接透传到 GCC 内置, 不设置 guard */
#if defined(__need_size_t) || defined(__need_wchar_t) || defined(__need_wint_t) || \
    defined(__need_ptrdiff_t) || defined(__need_NULL) || defined(__need_offsetof) || \
    defined(__need_wchar_max) || defined(__need_wint_max)

#include_next <stddef.h>

#else /* 完整 <stddef.h> 包含 */

#define _EFMLIBC_STDDEF_H
#include_next <stddef.h>

/* ssize_t: POSIX type not provided by GCC builtin stddef.h */
#ifndef __ssize_t_defined
#define __ssize_t_defined
typedef long ssize_t;
#endif
/* intptr_t/uintptr_t: provided by GCC builtin <stdint.h>, but
 * historically also defined here for code that includes only <stddef.h>. */
#ifndef __intptr_t_defined
#define __intptr_t_defined
typedef long intptr_t;
#endif
#ifndef __uintptr_t_defined
#define __uintptr_t_defined
typedef unsigned long uintptr_t;
#endif

#endif /* __need_* check */

#endif /* _EFMLIBC_STDDEF_H */
