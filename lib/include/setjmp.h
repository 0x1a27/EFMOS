/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SETJMP_H
#define _EFMLIBC_SETJMP_H
#ifdef __cplusplus
#include_next <setjmp.h>
#else
/* setjmp.h — C99 非局部跳转.
 * EFMOS: 使用 GCC 内建 __builtin_setjmp / __builtin_longjmp.
 * 这是编译器内建, 直接操作栈帧, 无需 libc 支持.
 * Mesa SPIR-V vtn (vtn_private.h) 用 setjmp/longjmp 做错误恢复. */
#include <sys/types.h>

/* GCC 内建 jmp_buf: 由 __builtin_setjmp 使用, 大小由编译器决定.
 * 用一个足够大的对齐数组承载. */
typedef long __efm_jmp_buf[32];
typedef __efm_jmp_buf jmp_buf[1];

#ifndef __GNUC__
/* 非 GCC 回退: 声明为普通函数 (无实际可用实现) */
int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#else
/* GCC: __builtin_setjmp 返回 int, __builtin_longjmp 不返回.
 * 注意 __builtin_setjmp 的参数是 jmp_buf (数组), 取首元素地址. */
#define setjmp(env)        __builtin_setjmp((void **)(env))
#define longjmp(env, val)  __builtin_longjmp((void **)(env), (val))
#endif

#endif
#endif /* _EFMLIBC_SETJMP_H */
