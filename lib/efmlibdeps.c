/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS - a 64-bit x86_64 UEFI operating system written in C.
 *
 * Copyright (C) 2026 0x1a27
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* ========================================================================
 * EFMOS Mesa 额外依赖兼容层 (libefmdeps.a)
 *
 * Mesa 除了 libc/libdrm/EGL/GBM/GEM 外, 还依赖以下库:
 *   - expat:   XML 解析 (Mesa util/xmlconfig.c, 调用 XML_Parser 等)
 *   - zlib:    压缩 (Mesa disk cache, shader cache)
 *   - libelf:  ELF 解析 (Mesa的spir-v/ELF处理, 简化)
 *   - sha1:    磁盘缓存哈希
 *   - dlfcn:   已在 libefmlibc.a 提供
 *
 * 本文件提供这些库的最小 stub 实现, 让 Mesa 编译和链接通过.
 * 功能上是"返回空结果/简化处理", 不影响 swrast 路径.
 * ======================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ========== 1. expat (XML 解析) ==========
 * Mesa util/xmlconfig.c 和 driver配置 用 expat 解析 XML.
 * stub: 返回一个假 parser, 所有 parse 操作返回成功 (不解析内容). */

typedef struct {
    int dummy;
} XML_ParserStruct;
typedef XML_ParserStruct *XML_Parser;
typedef void (*XML_StartElementHandler)(void *data, const char *el, const char **attr);
typedef void (*XML_EndElementHandler)(void *data, const char *el);
typedef void (*XML_CharacterDataHandler)(void *data, const char *s, int len);

#define XML_STATUS_OK    1
#define XML_STATUS_ERROR 0
#define XML_ERROR_NO_MEMORY 1
#define XML_ERROR_SYNTAX    2

XML_Parser XML_ParserCreate(const char *encoding) {
    (void)encoding;
    XML_Parser p = (XML_Parser)calloc(1, sizeof(XML_ParserStruct));
    return p;
}
XML_Parser XML_ParserCreateNS(const char *encoding, char sep) {
    (void)sep;
    return XML_ParserCreate(encoding);
}
XML_Parser XML_ExternalEntityParserCreate(XML_Parser p, const void *context,
                                           const char *encoding) {
    (void)p; (void)context;
    return XML_ParserCreate(encoding);
}
void XML_ParserFree(XML_Parser p) { free(p); }

void XML_SetElementHandler(XML_Parser p, XML_StartElementHandler start,
                           XML_EndElementHandler end) {
    (void)p; (void)start; (void)end;
}
void XML_SetCharacterDataHandler(XML_Parser p, XML_CharacterDataHandler h) {
    (void)p; (void)h;
}
void XML_SetUserData(XML_Parser p, void *data) { (void)p; (void)data; }
void XML_SetBase(XML_Parser p, const char *base) { (void)p; (void)base; }

int XML_Parse(XML_Parser p, const char *s, int len, int isFinal) {
    (void)p; (void)s; (void)len; (void)isFinal;
    return XML_STATUS_OK;
}
int XML_ParseBuffer(XML_Parser p, int len, int isFinal) {
    (void)p; (void)len; (void)isFinal;
    return XML_STATUS_OK;
}
void *XML_GetBuffer(XML_Parser p, int len) {
    (void)p; (void)len;
    static char dummy_buf[4096];
    return dummy_buf;
}
void XML_StopParser(XML_Parser p, int resumable) { (void)p; (void)resumable; }
void XML_ParserReset(XML_Parser p, const char *encoding) { (void)p; (void)encoding; }

int XML_GetErrorCode(XML_Parser p) { (void)p; return XML_ERROR_SYNTAX; }
const char *XML_ErrorString(int code) {
    switch (code) {
        case XML_ERROR_NO_MEMORY: return "out of memory";
        case XML_ERROR_SYNTAX:    return "syntax error";
        default: return "unknown error";
    }
}
long XML_GetCurrentByteIndex(XML_Parser p) { (void)p; return 0; }
int  XML_GetCurrentLineNumber(XML_Parser p) { (void)p; return 1; }
int  XML_GetCurrentColumnNumber(XML_Parser p) { (void)p; return 0; }

enum XML_Content_Type {
    XML_CTYPE_EMPTY, XML_CTYPE_ANY, XML_CTYPE_MIXED, XML_CTYPE_NAME, XML_CTYPE_CHOICE, XML_CTYPE_SEQ
};
enum XML_Content_Quant {
    XML_CQUANT_NONE, XML_CQUANT_OPT, XML_CQUANT_REP, XML_CQUANT_PLUS
};
typedef struct { enum XML_Content_Type type; enum XML_Content_Quant quant; void *children; int numchildren; } XML_Content;
int XML_GetElementType(XML_Parser p, const char *name, XML_Content **model) {
    (void)p; (void)name;
    *model = NULL;
    return 0;
}

/* ========== 2. zlib (压缩) ==========
 * Mesa disk_cache / shader cache 用 zlib 做 CRC32 + deflate/inflate.
 * stub: CRC32 用简单实现, deflate/inflate 返回"未压缩" (copy). */

typedef unsigned long uLong;
typedef unsigned int  uInt;

unsigned long crc32(unsigned long crc, const unsigned char *buf, uInt len) {
    /* 简化 CRC32 (与标准 zlib crc32 兼容) */
    crc = ~crc;
    for (uInt i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (unsigned int)(-(long)(crc & 1)));
        }
    }
    return ~crc;
}

/* zlib stream 类型 */
typedef struct z_stream_s {
    const unsigned char *next_in;
    unsigned int avail_in;
    unsigned long total_in;
    unsigned char *next_out;
    unsigned int avail_out;
    unsigned long total_out;
    char *msg;
    void *state;
    void *zalloc;
    void *zfree;
    void *opaque;
    int data_type;
    unsigned long adler;
    unsigned long reserved;
} z_stream;
typedef z_stream *z_streamp;

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5

#define Z_DEFAULT_COMPRESSION (-1)
#define Z_NO_COMPRESSION       0
#define Z_BEST_SPEED           1
#define Z_BEST_COMPRESSION     9

#define Z_DEFLATED   8
#define Z_DEFAULT_STRATEGY 0

int deflateInit_(z_streamp strm, int level, const char *version, int stream_size) {
    (void)strm; (void)level; (void)version; (void)stream_size;
    return Z_OK;
}
int deflateInit2_(z_streamp strm, int level, int method, int windowBits,
                  int memLevel, int strategy, const char *version, int stream_size) {
    (void)strm; (void)level; (void)method; (void)windowBits;
    (void)memLevel; (void)strategy; (void)version; (void)stream_size;
    return Z_OK;
}
int deflate(z_streamp strm, int flush) {
    /* 简化: 直接 copy (不压缩) */
    if (!strm) return Z_STREAM_ERROR;
    unsigned int n = strm->avail_in < strm->avail_out ? strm->avail_in : strm->avail_out;
    if (n > 0 && strm->next_in && strm->next_out) {
        memcpy(strm->next_out, strm->next_in, n);
        strm->next_in  += n;
        strm->avail_in -= n;
        strm->total_in += n;
        strm->next_out  += n;
        strm->avail_out -= n;
        strm->total_out += n;
    }
    if (flush == Z_FINISH && strm->avail_in == 0) return Z_STREAM_END;
    return Z_OK;
}
int deflateEnd(z_streamp strm) { (void)strm; return Z_OK; }
int deflateReset(z_streamp strm) { (void)strm; return Z_OK; }

int inflateInit_(z_streamp strm, const char *version, int stream_size) {
    (void)strm; (void)version; (void)stream_size;
    return Z_OK;
}
int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size) {
    (void)strm; (void)windowBits; (void)version; (void)stream_size;
    return Z_OK;
}
int inflate(z_streamp strm, int flush) {
    if (!strm) return Z_STREAM_ERROR;
    unsigned int n = strm->avail_in < strm->avail_out ? strm->avail_in : strm->avail_out;
    if (n > 0 && strm->next_in && strm->next_out) {
        memcpy(strm->next_out, strm->next_in, n);
        strm->next_in  += n;
        strm->avail_in -= n;
        strm->total_in += n;
        strm->next_out  += n;
        strm->avail_out -= n;
        strm->total_out += n;
    }
    if (flush == Z_FINISH && strm->avail_in == 0) return Z_STREAM_END;
    return Z_OK;
}
int inflateEnd(z_streamp strm) { (void)strm; return Z_OK; }
int inflateReset(z_streamp strm) { (void)strm; return Z_OK; }

unsigned long compressBound(unsigned long sourceLen) {
    return sourceLen + (sourceLen >> 12) + (sourceLen >> 14) + 11;
}
int compress(unsigned char *dest, unsigned long *destLen,
             const unsigned char *source, unsigned long sourceLen) {
    if (*destLen < sourceLen) return Z_BUF_ERROR;
    memcpy(dest, source, sourceLen);
    *destLen = sourceLen;
    return Z_OK;
}
int uncompress(unsigned char *dest, unsigned long *destLen,
               const unsigned char *source, unsigned long sourceLen) {
    if (*destLen < sourceLen) return Z_BUF_ERROR;
    memcpy(dest, source, sourceLen);
    *destLen = sourceLen;
    return Z_OK;
}

const char *zlibVersion(void) { return "1.2.13-efmos"; }
int compress2(unsigned char *dest, unsigned long *destLen,
              const unsigned char *source, unsigned long sourceLen, int level) {
    (void)level;
    return compress(dest, destLen, source, sourceLen);
}

/* gzFile (Mesa disk_cache 偶尔引用) */
typedef void *gzFile;
gzFile gzopen(const char *path, const char *mode) { (void)path; (void)mode; return NULL; }
int gzread(gzFile f, void *buf, unsigned int len) { (void)f; (void)buf; (void)len; return 0; }
int gzwrite(gzFile f, const void *buf, unsigned int len) { (void)f; (void)buf; (void)len; return 0; }
int gzclose(gzFile f) { (void)f; return 0; }
int gzeof(gzFile f) { (void)f; return 1; }

/* ========== 3. SHA1 ==========
 * Mesa 自带 src/util/sha1/sha1.c 实现 SHA1Init/Update/Final,
 * 我们不再提供, 避免链接时 multiple definition 冲突. */

/* ========== 4. libelf (ELF 解析, Mesa spir-v 偶尔引用) ==========
 * stub: 返回空 ELF, 不解析. */

typedef struct { int dummy; } Elf;
typedef Elf *Elf_Cmd;
typedef struct { int dummy; } Elf_Data;
typedef struct { int dummy; } Elf_Scn;

#define ELF_C_READ    0
#define ELF_C_WRITE   1
#define ELF_C_RDWR    2
#define ELF_C_NULL    3
#define ELF_K_NONE    0
#define ELF_K_ELF     1
#define ELF_K_AR      2
#define ELF_TYPE_ELF  1

Elf *elf_begin(int fd, int cmd, Elf *ref) {
    (void)fd; (void)cmd; (void)ref;
    return NULL;
}
int elf_end(Elf *elf) { (void)elf; return 0; }
int elf_kind(Elf *elf) { (void)elf; return ELF_K_NONE; }
Elf_Data *elf_getdata(Elf_Scn *scn, Elf_Data *data) { (void)scn; (void)data; return NULL; }
Elf_Scn *elf_nextscn(Elf *elf, Elf_Scn *scn) { (void)elf; (void)scn; return NULL; }
void *elf_getscn(Elf *elf, size_t index) { (void)elf; (void)index; return NULL; }
int elf_getshdrnum(Elf *elf, size_t *dst) { (void)elf; *dst = 0; return 0; }
char *elf_strptr(Elf *elf, size_t section, size_t offset) {
    (void)elf; (void)section; (void)offset; return NULL;
}
unsigned int elf_version(unsigned int v) { (void)v; return 1; }
int elf_getshdrstrndx(Elf *elf, size_t *dst) { (void)elf; *dst = 0; return 0; }

/* ========== 5. Mesa util/detect.h 引用的系统检测 ========== */
/* Mesa 的 util/u_cpu_detect.c 检测 CPU 特性. 我们提供简化版本. */

int efm_cpu_has_sse(void)        { return 0; }  /* EFMOS 不用 SSE (freestanding) */
int efm_cpu_has_sse2(void)       { return 0; }
int efm_cpu_has_sse3(void)       { return 0; }
int efm_cpu_has_sse41(void)      { return 0; }
int efm_cpu_has_sse42(void)      { return 0; }
int efm_cpu_has_avx(void)       { return 0; }
int efm_cpu_has_avx2(void)      { return 0; }
int efm_cpu_has_f16c(void)      { return 0; }
int efm_cpu_has_fma(void)       { return 0; }
int efm_cpu_has_neon(void)      { return 0; }
int efm_cpu_cores(void)         { return 1; }    /* 单核 */
int efm_cpu_l1_cache_bytes(void) { return 32 * 1024; }
int efm_cpu_l2_cache_bytes(void) { return 256 * 1024; }
int efm_cpu_l3_cache_bytes(void) { return 0; }   /* 无 L3 */

/* ========== 6. Mesa util/u_thread (线程名/优先级, 已在 libefmlibc 覆盖) ========== */
/* 占位: libefmlibc.a 已实现 pthread_setname_np 等 */

/* ========== 7. Mesa util/u_memory 对齐分配 (已覆盖) ========== */
/* libefmlibc.a 已实现 posix_memalign / aligned_alloc */

/* ========== 8. Mesa 对 Valgrind 的引用 (编译时检测, 返回 0) ========== */
unsigned long RUNNING_ON_VALGRIND(void) { return 0; }
int valgrind_running(void) { return 0; }
