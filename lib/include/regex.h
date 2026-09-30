/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_REGEX_H
#define _EFMLIBC_REGEX_H
#ifdef __cplusplus
#include_next <regex.h>
#else
/* EFMOS regex stub: 让 Mesa xmlconfig.c 编译通过.
 * 所有正则操作返回"不匹配", 功能上不影响 swrast 路径. */
#include <sys/types.h>

typedef struct { int _unused; } regex_t;
typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

#define REG_EXTENDED  1
#define REG_ICASE     2
#define REG_NOSUB     4
#define REG_NEWLINE   8
#define REG_NOTBOL    1
#define REG_NOTEOL    2
#define REG_NOMATCH   1
#define REG_BADBR     2
#define REG_BADPAT    3
#define REG_BADRPT    4
#define REG_EBRACE    5
#define REG_EBRACK    6
#define REG_ECOLLATE  7
#define REG_ECTYPE    8
#define REG_EESCAPE   9
#define REG_ESUBREG  10
#define REG_EEND     11
#define REG_EESCAPE  12
#define REG_ESPACE  12
#define REG_ESIZE   13
#define REG_ERPAREN  14
#define REG_ASSERT   1
#define REG_INVARG   2
#define REG_ATOI   255
#define REG_ATOI_NAME  256

static inline int regcomp(regex_t *r, const char *pattern, int flags) {
    (void)r; (void)pattern; (void)flags; return 0;
}
static inline int regexec(const regex_t *r, const char *str, size_t nmatch,
                          regmatch_t pmatch[], int eflags) {
    (void)r; (void)str; (void)nmatch; (void)pmatch; (void)eflags;
    return REG_NOMATCH;
}
static inline void regfree(regex_t *r) { (void)r; }
static inline size_t regerror(int errcode, const regex_t *r, char *errbuf, size_t errbuf_size) {
    (void)errcode; (void)r;
    if (errbuf && errbuf_size > 0) errbuf[0] = 0;
    return 0;
}

#endif
#endif
