#ifndef _EFMLIBC_ZSTD_H
#define _EFMLIBC_ZSTD_H
#ifdef __cplusplus
#include_next <zstd.h>
#else
/* EFMOS zstd stub: 让 Mesa util/compress.c 编译通过.
 * 压缩/解压直接拷贝数据 (无压缩), 与 zlib stub 行为一致. */
#include <stddef.h>

typedef size_t ZSTD_ErrorCode;

/* stub: 压缩后大小 = 源大小 (不压缩) */
static inline size_t ZSTD_compressBound(size_t srcSize) {
    return srcSize + 16;
}

/* stub: 直接拷贝, 返回写入字节数 */
static inline size_t ZSTD_compress(void *dst, size_t dstCapacity,
                                    const void *src, size_t srcSize,
                                    int compressionLevel) {
    (void)compressionLevel;
    if (dstCapacity < srcSize) return (size_t)-1;
    if (srcSize > 0 && dst && src) {
        char *d = (char*)dst; const char *s = (const char*)src;
        size_t i;
        for (i = 0; i < srcSize; i++) d[i] = s[i];
    }
    return srcSize;
}

/* stub: 直接拷贝, 返回解压字节数 */
static inline size_t ZSTD_decompress(void *dst, size_t dstCapacity,
                                      const void *src, size_t srcSize) {
    if (dstCapacity < srcSize) return (size_t)-1;
    if (srcSize > 0 && dst && src) {
        char *d = (char*)dst; const char *s = (const char*)src;
        size_t i;
        for (i = 0; i < srcSize; i++) d[i] = s[i];
    }
    return srcSize;
}

/* stub: 无错误 */
static inline unsigned ZSTD_isError(size_t code) {
    return code == (size_t)-1 ? 1 : 0;
}

static inline const char *ZSTD_getErrorName(size_t code) {
    (void)code; return "EFMOS zstd stub: no real compression";
}

static inline unsigned long long ZSTD_getFrameContentSize(const void *src, size_t srcSize) {
    (void)src; return srcSize;
}

static inline unsigned ZSTD_maxCLevel(void) { return 1; }

#endif
#endif
