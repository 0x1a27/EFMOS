/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_INTTYPES_H
#define _EFMLIBC_INTTYPES_H
#ifdef __cplusplus
#include_next <inttypes.h>
#else
#include <stdint.h>
#include <stddef.h>

/* printf/scanf format macros for C99 inttypes */

/* decimal */
#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "ld"

#define PRIi8  "d"
#define PRIi16 "d"
#define PRIi32 "d"
#define PRIi64 "ld"

#define PRIdLEAST8  "d"
#define PRIdLEAST16 "d"
#define PRIdLEAST32 "d"
#define PRIdLEAST64 "ld"

#define PRIdFAST8  "d"
#define PRIdFAST16 "d"
#define PRIdFAST32 "d"
#define PRIdFAST64 "ld"

#define PRIdMAX "ld"
#define PRIdPTR "ld"
#define PRIiMAX "ld"
#define PRIiPTR "ld"

/* unsigned decimal */
#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "lu"

#define PRIuLEAST8  "u"
#define PRIuLEAST16 "u"
#define PRIuLEAST32 "u"
#define PRIuLEAST64 "lu"

#define PRIuFAST8  "u"
#define PRIuFAST16 "u"
#define PRIuFAST32 "u"
#define PRIuFAST64 "lu"

#define PRIuMAX "lu"
#define PRIuPTR "lu"

/* unsigned octal */
#define PRIo8  "o"
#define PRIo16 "o"
#define PRIo32 "o"
#define PRIo64 "lo"

#define PRIoLEAST8  "o"
#define PRIoLEAST16 "o"
#define PRIoLEAST32 "o"
#define PRIoLEAST64 "lo"

#define PRIoFAST8  "o"
#define PRIoFAST16 "o"
#define PRIoFAST32 "o"
#define PRIoFAST64 "lo"

#define PRIoMAX "lo"
#define PRIoPTR "lo"

/* unsigned hex lowercase */
#define PRIx8  "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 "lx"

#define PRIxLEAST8  "x"
#define PRIxLEAST16 "x"
#define PRIxLEAST32 "x"
#define PRIxLEAST64 "lx"

#define PRIxFAST8  "x"
#define PRIxFAST16 "x"
#define PRIxFAST32 "x"
#define PRIxFAST64 "lx"

#define PRIxMAX "lx"
#define PRIxPTR "lx"

/* unsigned hex uppercase */
#define PRIX8  "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIX64 "lX"

#define PRIXLEAST8  "X"
#define PRIXLEAST16 "X"
#define PRIXLEAST32 "X"
#define PRIXLEAST64 "lX"

#define PRIXFAST8  "X"
#define PRIXFAST16 "X"
#define PRIXFAST32 "X"
#define PRIXFAST64 "lX"

#define PRIXMAX "lX"
#define PRIXPTR "lX"

/* scanf length modifiers */
#define SCNd8   "hhd"
#define SCNd16  "hd"
#define SCNd32  "d"
#define SCNd64  "ld"
#define SCNi8   "hhd"
#define SCNi16  "hd"
#define SCNi32  "d"
#define SCNi64  "ld"
#define SCNu8   "hhu"
#define SCNu16  "hu"
#define SCNu32  "u"
#define SCNu64  "lu"
#define SCNo8   "hho"
#define SCNo16  "ho"
#define SCNo32  "o"
#define SCNo64  "lo"
#define SCNx8   "hhx"
#define SCNx16  "hx"
#define SCNx32  "x"
#define SCNx64  "lx"
#define SCNdLEAST8  "hhd"
#define SCNdLEAST16 "hd"
#define SCNdLEAST32 "d"
#define SCNdLEAST64 "ld"
#define SCNuLEAST8  "hhu"
#define SCNuLEAST16 "hu"
#define SCNuLEAST32 "u"
#define SCNuLEAST64 "lu"
#define SCNdFAST8  "hhd"
#define SCNdFAST16 "hd"
#define SCNdFAST32 "d"
#define SCNdFAST64 "ld"
#define SCNuFAST8  "hhu"
#define SCNuFAST16 "hu"
#define SCNuFAST32 "u"
#define SCNuFAST64 "lu"
#define SCNdMAX "ld"
#define SCNuMAX "lu"
#define SCNdPTR "ld"
#define SCNuPTR "lu"
#define SCNxPTR "lx"

typedef struct {
    intmax_t quot; /* quotient */
    intmax_t rem;  /* remainder */
} imaxdiv_t;

intmax_t  imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t  strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);
#endif
#endif
