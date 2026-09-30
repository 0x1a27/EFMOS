/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_LIMITS_H
#define _EFMLIBC_LIMITS_H
#ifdef __cplusplus
/* Define MB_LEN_MAX before #include_next: the GCC builtin defines it as 1,
 * but glibc's <bits/stdlib.h> and <bits/wchar2.h> require it to be 16.
 * The #include_next chain (our limits.h -> GCC builtin -> syslimits.h ->
 * circular back to us) never reaches glibc's /usr/include/limits.h. */
#ifndef MB_LEN_MAX
#define MB_LEN_MAX 16
#endif
#include_next <limits.h>
/* GCC builtin <limits.h> only defines LLONG_MAX for C99
 * (__STDC_VERSION__ >= 199901L), not for C++ (__STDC_VERSION__ unset).
 * Mesa C++ code (e.g. glsl_lexer.ll) uses LLONG_MAX, so define it here. */
#ifndef LLONG_MAX
#define LLONG_MAX 9223372036854775807LL
#endif
#ifndef LLONG_MIN
#define LLONG_MIN (-9223372036854775807LL - 1LL)
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX 18446744073709551615ULL
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#else
#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN 0
#define CHAR_MAX 255
#define SHRT_MIN  (-32768)
#define SHRT_MAX  32767
#define USHRT_MAX 65535
#define INT_MIN   (-2147483647 - 1)
#define INT_MAX   2147483647
#define UINT_MAX  4294967295U
#define LONG_MIN  (-9223372036854775807L - 1L)
#define LONG_MAX  9223372036854775807L
#define ULONG_MAX 18446744073709551615UL
#define LLONG_MIN (-9223372036854775807LL - 1LL)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL
#define MB_LEN_MAX 16
/* POSIX */
#define PATH_MAX 4096
#define NAME_MAX 255
#define PIPE_BUF 4096
#define HOST_NAME_MAX 64
#define LINE_MAX 2048
#define NGROUPS_MAX 65536
#endif
#endif
