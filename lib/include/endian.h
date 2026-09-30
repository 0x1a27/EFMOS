/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_ENDIAN_H
#define _EFMLIBC_ENDIAN_H
#ifdef __cplusplus
#include_next <endian.h>
#else
#include <stdint.h>
#define __BYTE_ORDER 1234
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __FLOAT_WORD_ORDER 1234
#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN 4321
#define BYTE_ORDER 1234
/* bswap helpers (Mesa uses be32toh/htobe32/le32toh/htole32 etc.) */
#define bswap_16(x) ((uint16_t)((((x)&0xFF)<<8)|(((x)>>8)&0xFF)))
#define bswap_32(x) ((uint32_t)((bswap_16((uint16_t)(x))<<16)|bswap_16((uint16_t)((x)>>16))))
#define bswap_64(x) ((uint64_t)((bswap_32((uint32_t)(x))<<32ULL)|bswap_32((uint32_t)((x)>>32))))
#if BYTE_ORDER == LITTLE_ENDIAN
#  define htobe16(x)  bswap_16(x)
#  define htobe32(x)  bswap_32(x)
#  define htobe64(x)  bswap_64(x)
#  define be16toh(x)  bswap_16(x)
#  define be32toh(x)  bswap_32(x)
#  define be64toh(x)  bswap_64(x)
#  define htole16(x)  ((uint16_t)(x))
#  define htole32(x)  ((uint32_t)(x))
#  define htole64(x)  ((uint64_t)(x))
#  define le16toh(x)  ((uint16_t)(x))
#  define le32toh(x)  ((uint32_t)(x))
#  define le64toh(x)  ((uint64_t)(x))
#else
#  define htobe16(x)  ((uint16_t)(x))
#  define htobe32(x)  ((uint32_t)(x))
#  define htobe64(x)  ((uint64_t)(x))
#  define be16toh(x)  ((uint16_t)(x))
#  define be32toh(x)  ((uint32_t)(x))
#  define be64toh(x)  ((uint64_t)(x))
#  define htole16(x)  bswap_16(x)
#  define htole32(x)  bswap_32(x)
#  define htole64(x)  bswap_64(x)
#  define le16toh(x)  bswap_16(x)
#  define le32toh(x)  bswap_32(x)
#  define le64toh(x)  bswap_64(x)
#endif
#endif
#endif
