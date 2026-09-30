/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_STDLIB_H
#define _EFMLIBC_STDLIB_H
#ifdef __cplusplus
#include <limits.h>
#include_next <stdlib.h>
#else
#include <sys/types.h>
#ifndef NULL
#define NULL ((void*)0)
#endif
/* alloca: 栈上分配 (GCC 内建), glibc 在 <alloca.h> 中提供, Mesa c99_alloca.h
 * 通过 <stdlib.h> 引入. 必须用 __builtin_alloca 而非 malloc (栈语义). */
#define alloca(size) __builtin_alloca(size)
void *malloc(size_t n);
void  free(void *p);
void *calloc(size_t cnt, size_t sz);
void *realloc(void *old, size_t n);
void *aligned_alloc(size_t alignment, size_t size);
int   posix_memalign(void **memptr, size_t alignment, size_t size);
void  qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *));
/* GNU qsort_r: compar 接受 arg 作为第三参数 (Mesa u_qsort.h 用此签名) */
void  qsort_r(void *base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*, void*), void *arg);
int   atexit(void (*fn)(void));
void  exit(int s) __attribute__((noreturn));
void  _exit(int s) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));
int   atoi(const char *s);
long  atol(const char *s);
long  strtol(const char *s, char **endp, int base);
unsigned long  strtoul(const char *s, char **endp, int base);
long long      strtoll(const char *s, char **endp, int base);
unsigned long long strtoull(const char *s, char **endp, int base);
char *getenv(const char *n);
int   setenv(const char *name, const char *v, int over);
int   unsetenv(const char *n);
int   system(const char *cmd);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
int   abs(int x);
long  labs(long x);
long long llabs(long long x);
char *secure_getenv(const char *name);
int   mkstemp(char *tmpl);
char *mkdtemp(char *tmpl);
char *mktemp(char *tmpl);
int   mkostemp(char *tmpl, int flags);
int   mkostemps(char *tmpl, int suffixlen, int flags);
int   mkstemps(char *tmpl, int suffixlen);
long  random(void);
void  srandom(unsigned int seed);
int   rand(void);
void  srand(unsigned int seed);
int   posix_openpt(int flags);
int   grantpt(int fd);
int   unlockpt(int fd);
char *ptsname(int fd);
int   ptsname_r(int fd, char *buf, size_t buflen);
double atof(const char *s);
long double strtold(const char *s, char **endp);
double strtod(const char *s, char **endp);
float strtof(const char *s, char **endp);
#endif
#endif
