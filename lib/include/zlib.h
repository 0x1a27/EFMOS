/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Mirrors the zlib (zlib license) public API.
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_ZLIB_H
#define _EFMLIBC_ZLIB_H
#ifdef __cplusplus
#include_next <zlib.h>
#else
#include <stddef.h>

#define ZLIB_VERSION "1.2.efm"
#define ZLIB_VER_MAJOR 1
#define ZLIB_VER_MINOR 2
#define ZLIB_VER_REVISION 11
#define Z_VER 0x12B0

/* flush values */
#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5
#define Z_TREES         6

/* return codes */
#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

/* compression levels */
#define Z_NO_COMPRESSION       0
#define Z_BEST_SPEED           1
#define Z_BEST_COMPRESSION     9
#define Z_DEFAULT_COMPRESSION (-1)

/* strategy */
#define Z_FILTERED            1
#define Z_HUFFMAN_ONLY        2
#define Z_RLE                 3
#define Z_FIXED               4
#define Z_DEFAULT_STRATEGY    0

/* data types */
#define Z_BINARY   0
#define Z_TEXT     1
#define Z_ASCII    Z_TEXT
#define Z_UNKNOWN  2

/* deflate compression method */
#define Z_DEFLATED   8

typedef void *(*alloc_func)(void *opaque, unsigned int items, unsigned int size);
typedef void  (*free_func)(void *opaque, void *address);

typedef unsigned char Byte;
typedef unsigned int  uInt;
typedef unsigned long uLong;
typedef Byte Bytef;
typedef char charf;
typedef int intf;
typedef uInt uIntf;
typedef uLong uLongf;
typedef void *voidpf;
typedef void  *voidp;

#define Z_NULL  0

typedef struct z_stream_s {
    Bytef    *next_in;
    uInt     avail_in;
    uLong    total_in;
    Bytef    *next_out;
    uInt     avail_out;
    uLong    total_out;
    char     *msg;
    void     *state;
    alloc_func zalloc;
    free_func  zfree;
    void     *opaque;
    int      data_type;
    uLong    adler;
    uLong    reserved;
} z_stream;
typedef z_stream *z_streamp;

uLong adler32(uLong adler, const Byte *buf, uInt len);
uLong crc32(uLong crc, const Byte *buf, uInt len);

int compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level);
uLong compressBound(uLong sourceLen);
int uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
int uncompress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong *sourceLen);

/* Gzip */
typedef struct gzFile_s *gzFile;
gzFile gzopen(const char *path, const char *mode);
gzFile gzdopen(int fd, const char *mode);
int    gzbuffer(gzFile file, unsigned size);
int    gzsetparams(gzFile file, int level, int strategy);
int    gzread(gzFile file, void *buf, unsigned len);
int    gzwrite(gzFile file, const void *buf, unsigned len);
int    gzprintf(gzFile file, const char *format, ...);
int    gzputs(gzFile file, const char *s);
char  *gzgets(gzFile file, char *buf, int len);
int    gzputc(gzFile file, int c);
int    gzgetc(gzFile file);
int    gzungetc(int c, gzFile file);
int    gzflush(gzFile file, int flush);
int    gzseek(gzFile file, long offset, int whence);
long   gztell(gzFile file);
void   gzclearerr(gzFile file);
int    gzeof(gzFile file);
int    gzdirect(gzFile file);
int    gzclose(gzFile file);
int    gzerror(gzFile file, int *errnum);
const char *gzerror_file(gzFile file, int *errnum);

/* streaming */
int deflateInit_(z_streamp strm, int level, const char *version, int stream_size);
int inflateInit_(z_streamp strm, const char *version, int stream_size);
#define deflateInit(strm, level) deflateInit_((strm), (level), ZLIB_VERSION, (int)sizeof(z_stream))
#define inflateInit(strm) inflateInit_((strm), ZLIB_VERSION, (int)sizeof(z_stream))
int deflateInit2_(z_streamp strm, int level, int method, int windowBits, int memLevel,
                  int strategy, const char *version, int stream_size);
int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size);
#define deflateInit2(strm,level,method,windowBits,memLevel,strategy) \
    deflateInit2_((strm),(level),(method),(windowBits),(memLevel),(strategy),ZLIB_VERSION,(int)sizeof(z_stream))
#define inflateInit2(strm,windowBits) inflateInit2_((strm),(windowBits),ZLIB_VERSION,(int)sizeof(z_stream))
int deflate(z_streamp strm, int flush);
int inflate(z_streamp strm, int flush);
int deflateEnd(z_streamp strm);
int inflateEnd(z_streamp strm);
int inflateReset(z_streamp strm);
int deflateReset(z_streamp strm);
int inflateReset2(z_streamp strm, int windowBits);
int deflateCopy(z_streamp dest, z_streamp source);
int inflateCopy(z_streamp dest, z_streamp source);
int deflateParams(z_streamp strm, int level, int strategy);
int deflateTune(z_streamp strm, int good_length, int max_lazy, int nice_length, int max_chain);
uLong deflateBound(z_streamp strm, uLong sourceLen);
int   deflatePending(z_streamp strm, unsigned *pending, int *bits);
int   deflatePrime(z_streamp strm, int bits, int value);
const char *zlibVersion(void);
const char *zError(int);
int inflateSyncPoint(z_streamp z);
int inflateGetDictionary(z_streamp strm, Bytef *dictionary, uInt *dictLength);
int inflateSetDictionary(z_streamp strm, const Bytef *dictionary, uInt dictLength);
int inflateSync(z_streamp strm);
int inflateValidate(z_streamp strm, int check);
int inflateMark(z_streamp strm);
int deflateGetDictionary(z_streamp strm, Bytef *dictionary, uInt *dictLength);
int deflateSetDictionary(z_streamp strm, const Bytef *dictionary, uInt dictLength);
int deflateSetHeader(z_streamp strm, void *head);
int deflateBound_copy(z_streamp strm, uLong sourceLen);
#endif
#endif
