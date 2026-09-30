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
 * EFMOS POSIX libc (libefmlibc.a)
 *
 * 目标: 为 Mesa 3D / Gallium / 其他大型 C 代码提供最小 POSIX 兼容层.
 * 所有 syscall 最终路由到 0x9000 处的 struct kernel_api (EFMOS 内部 API 表).
 *
 * 覆盖子集:
 *   - errno          : 存放在线程本地 ptr __efm_errno_loc
 *   - unistd.h       : sbrk, read/write/close/lseek, fsync, getpid/usleep/sleep
 *   - stdio.h        : printf/fprintf/sprintf/snprintf/vprintf (tiny vsnprintf),
 *                      fopen/fread/fwrite/fclose/fflush/fgets/puts
 *   - stdlib.h       : malloc/calloc/realloc/free, atexit, exit, qsort, atoi/atol/strtol
 *   - string.h       : memcpy/memset/memmove/memcmp/strlen/strcpy/strcmp/strncmp/strcat/strdup/strchr/strrchr
 *   - strings.h      : bzero/bcopy
 *   - time.h         : time/clock_gettime (TSC 近似)
 *   - sys/types.h    : 基础 typedef (size_t, ssize_t, pid_t, off_t, mode_t)
 *   - sys/mman.h     : mmap/munmap (基于内核 malloc)
 *   - sys/stat.h     : stat/fstat/mkdir (路由到 file_exists + mkdir)
 *   - dirent.h       : opendir/readdir/closedir (路由到 file_list)
 *   - pthread.h      : pthread_create/pthread_join/pthread_self/pthread_mutex_*
 *                      (路由到内核 spawn_async + yield + 原子自旋锁)
 *   - dlfcn.h        : 无 dlopen/dlsym (静态链接, 返回 NULL)
 *
 * 与 Mesa 的最小接触面 (Mesa 对 libc 的主要调用点):
 *   mmap/munmap, malloc/calloc/realloc/free, str* , stdio, pthread, clock_gettime
 *   stat, open/read/close (读 icd 配置 / 驱动), mkdir (缓存目录), errno
 * 所以这套子集足够 Mesa 的 util/u_math, u_process, u_threadpool, u_disk_cache 编译.
 * ======================================================================== */

/* ---------- 基本类型 (避免 include 宿主 Linux 头文件) ----------
 * 如果有 lib/include/sys/types.h (通过 -Ilib/include 引入), 先 include 它,
 * 避免重复 typedef 冲突. */
#if __has_include("sys/types.h")
#  include <sys/types.h>
#  include <stdint.h>
#  include <pthread.h>
#  include <sched.h>
#  include <sys/resource.h>
#  define _EFM_HAVE_SYS_TYPES 1
#endif
#ifndef _EFM_HAVE_SYS_TYPES
typedef unsigned long       size_t;
typedef long                ssize_t;
typedef long                off_t;
typedef long                ptrdiff_t;
typedef int                 pid_t;
typedef unsigned int        mode_t;
typedef unsigned int        uid_t;
typedef unsigned int        gid_t;
typedef unsigned int        dev_t;
typedef unsigned int        ino_t;
typedef unsigned int        nlink_t;
typedef long                time_t;
typedef long                suseconds_t;
typedef long                useconds_t;
typedef int                 clockid_t;
typedef unsigned long       uintptr_t;
typedef unsigned long       pthread_t;
typedef unsigned long       pthread_mutex_t;
typedef unsigned long       pthread_cond_t;
typedef unsigned long       pthread_key_t;
typedef unsigned long       pthread_once_t;
typedef unsigned long       pthread_rwlock_t;
typedef unsigned long       pthread_spinlock_t;
typedef unsigned long       pthread_barrier_t;
typedef unsigned long       pthread_attr_t;
typedef unsigned long       pthread_mutexattr_t;
typedef unsigned long       pthread_condattr_t;
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
#endif

/* ---------- 本地结构体 (避免循环 include sched.h/time.h) ---------- */
#if __has_include("time.h")
#  include <time.h>
#else
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};
#endif
#ifndef _SCHED_PARAM_DEFINED_
#define _SCHED_PARAM_DEFINED_
struct sched_param { int sched_priority; };
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ---------- errno 占位 (Mesa 到处读写 errno) ---------- */
extern int *__efm_errno_loc(void);
#define errno (*__efm_errno_loc())

/* ---------- malloc/free 前向声明 (string.h 中 strdup 先用到) ---------- */
extern void *malloc(size_t n);
extern void  free(void *p);

/* errno 值 (Mesa 用到的子集) */
#define EINVAL      22
#define ENOMEM      12
#define ENOENT      2
#define EACCES      13
#define EEXIST      17
#define ENOTDIR     20
#define EISDIR      21
#define EIO         5
#define EBADF       9
#define ENOSYS      38
#define EAGAIN      11
#define EBUSY       16
#define ETIMEDOUT   110
#define EOVERFLOW   75
#define EFAULT      14
#define EINTR        4
#define EPERM        1
#define ERANGE      34
#define ENFILE      23
#define EMFILE      24
#define ENAMETOOLONG 36
#define ELOOP       40
#define ENOSPC      28
#define EDQUOT     122
#define EROFS       30
#define ECANCELED  125
#define ENOTEMPTY   39
#define EDEADLK     36  /* 这里与 ELOOP 共用, 足够编译 */
#define ECHILD      10

/* ---------- EFMOS 内部 kernel_api (与 kernel.c 保持完全一致) ---------- */
struct __efm_dirent {
    char name[256];
    int  is_dir;
    long size;
};
struct __efm_kernel_api {
    unsigned int magic;
    unsigned int _pad;
    void (*put_char)(char);
    void (*print)(const char*);
    void (*print_utf8)(const char*);
    void (*clear_screen)(void);
    int  (*file_read)(const char*, char*, int);
    int  (*file_write)(const char*, const char*, int);
    int  (*file_exists)(const char*);
    int  (*mkdir)(const char*);
    int  (*readline)(char*, int);
    void (*reboot)(void);
    int  (*get_lang)(void);
    void (*set_lang)(int);
    int  (*save_settings)(void);
    int  (*mouse_poll)(void *out_event);
    void (*mouse_set_cursor)(int show);
    int  (*file_list)(const char *dir, struct __efm_dirent *out, int max);
    int  (*file_delete)(const char *path);
    int  (*key_poll)(void);
    int  (*get_current_user)(char *buf, int bufsz);
    int  (*set_current_user)(const char *username);
    int  (*user_list)(struct __efm_dirent *out, int max);
    int  (*user_create)(const char *username);
    int  (*user_delete)(const char *username);
    void *(*malloc_fn)(unsigned long);   /* 注意: 与 glibc malloc 同名冲突, 改名 */
    void  (*free_fn)(void*);
    int   (*spawn)(const char *name, const char *args);
    int   (*get_args)(char *buf, int max);
    int   font_w;
    int   font_h;
    int   current_pid;
    int   wm_enabled;
    void (*put_pixel)(int x, int y, unsigned int c);
    void (*fill_rect)(int x1, int y1, int x2, int y2, unsigned int c);
    void (*draw_rect)(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill);
    void (*get_viewport)(int *cx, int *cy, int *cw, int *ch);
    void (*get_fb_info)(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base);
    int  (*blit_to_window)(const void *src, int src_w, int src_h, int src_pitch);
    int   (*load_driver)(const char *path);
    int   (*driver_count)(void);
    int   (*driver_list)(char out_names[][32], int max);
    void  (*sleep_ms)(unsigned long ms);
    void  (*yield)(void);
    int   (*get_pid)(void);
    int   (*spawn_async)(const char *name, const char *args);
    int   (*set_priority)(int pid, int nice);
    int   (*get_wm_snapshot)(void *out, int max_bytes);
    void  (*set_compositor_active)(int active);
    void *(*dlsym)(const char *name);
    int   (*get_backbuffer)(void **out_ptr, int *out_pitch, int *out_w, int *out_h);
    void  (*mark_dirty_rect)(int x1, int y1, int x2, int y2);
    void  (*flush_now)(void);
};
#define EFM_API_MAGIC  0xEF110001
#define EFM_API_PTR    ((volatile struct __efm_kernel_api *const)0x9000UL)

static inline int __efm_api_ok(void) {
    return (EFM_API_PTR->magic == EFM_API_MAGIC);
}

/* ---------- 公开 API: 供 efmlibgbm/efmlibgem 获取内核 API ---------- */
struct __efm_kernel_api *efm_get_api(void) {
    if (__efm_api_ok()) return (struct __efm_kernel_api *)EFM_API_PTR;
    return NULL;
}

void efm_get_fb_info(unsigned int *hr, unsigned int *vr,
                     unsigned int *ppsl, unsigned int **fb_base) {
    if (hr) *hr = 0;
    if (vr) *vr = 0;
    if (ppsl) *ppsl = 0;
    if (fb_base) *fb_base = NULL;
    if (__efm_api_ok() && EFM_API_PTR->get_fb_info)
        EFM_API_PTR->get_fb_info(hr, vr, ppsl, fb_base);
}

/* 批量 blit: 把外部像素缓冲区写入当前进程窗口 (Mesa 集成核心) */
int efm_blit_to_window(const void *src, int src_w, int src_h, int src_pitch) {
    if (__efm_api_ok() && EFM_API_PTR->blit_to_window)
        return EFM_API_PTR->blit_to_window(src, src_w, src_h, src_pitch);
    return -1;
}

/* 查询 WM viewport (窗口内容区尺寸) */
void efm_get_viewport(int *cx, int *cy, int *cw, int *ch) {
    if (cx) *cx = 0;
    if (cy) *cy = 0;
    if (cw) *cw = 0;
    if (ch) *ch = 0;
    if (__efm_api_ok() && EFM_API_PTR->get_viewport)
        EFM_API_PTR->get_viewport(cx, cy, cw, ch);
}

/* [优化] 获取 Graphics.drv 的 back buffer 指针和 pitch,
 * 让 Mesa swrast 直接渲染到 back buffer, 消除 BO→back buffer 拷贝.
 * 返回 0=成功, -1=不支持. */
int efm_get_backbuffer(void **out_ptr, int *out_pitch, int *out_w, int *out_h) {
    if (out_ptr)   *out_ptr   = NULL;
    if (out_pitch) *out_pitch = 0;
    if (out_w)     *out_w     = 0;
    if (out_h)     *out_h     = 0;
    if (__efm_api_ok() && EFM_API_PTR->get_backbuffer)
        return EFM_API_PTR->get_backbuffer(out_ptr, out_pitch, out_w, out_h);
    return -1;
}

/* [优化] 直接映射模式下的 swap: swrast 已直接写入 back buffer,
 * 只需标记脏矩形 + 触发 flush (back→front), 无需自拷贝.
 * 通过 mark_dirty_rect 标记全屏脏区 + flush_now 立即搬到 front,
 * 消除旧的 8MB 自拷贝, 彻底避免闪屏. */
void efm_mark_dirty_and_flush(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (__efm_api_ok() && EFM_API_PTR->mark_dirty_rect && EFM_API_PTR->flush_now) {
        EFM_API_PTR->mark_dirty_rect(0, 0, w - 1, h - 1);
        EFM_API_PTR->flush_now();
    }
}

/* [优化] 标记 back buffer 脏矩形 (inclusive) */
void efm_mark_dirty(int x1, int y1, int x2, int y2) {
    if (__efm_api_ok() && EFM_API_PTR->mark_dirty_rect)
        EFM_API_PTR->mark_dirty_rect(x1, y1, x2, y2);
}

/* [优化] 立即 flush back buffer → front buffer */
void efm_flush_now(void) {
    if (__efm_api_ok() && EFM_API_PTR->flush_now)
        EFM_API_PTR->flush_now();
}

/* ---------- errno 实现 ---------- */
static __thread int g_efm_tls_errno = 0;
int *__efm_errno_loc(void) {
    return &g_efm_tls_errno;
}

/* ---------- string.h ---------- */
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    unsigned char v = (unsigned char)c;
    while (n--) *d++ = v;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    if (d == s) return dst;
    if (d < s || d >= s + n) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char*)a;
    const unsigned char *pb = (const unsigned char*)b;
    while (n--) {
        if (*pa != *pb) return *pa < *pb ? -1 : 1;
        pa++; pb++;
    }
    return 0;
}
size_t strlen(const char *s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}
char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) ;
    return dst;
}
char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n--) {
        if (*a != *b) return (int)(unsigned char)*a - (int)(unsigned char)*b;
        if (!*a) return 0;
        a++; b++;
    }
    return 0;
}
char *strcat(char *dst, const char *src) {
    char *d = dst + strlen(dst);
    while ((*d++ = *src++)) ;
    return dst;
}
char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    while (n-- > 0 && *src) { *d++ = *src++; }
    *d = 0;
    return dst;
}
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = (char*)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}
char *strchr(const char *s, int c) {
    char cc = (char)c;
    while (*s) { if (*s == cc) return (char*)s; s++; }
    return (cc == 0) ? (char*)s : NULL;
}
char *strrchr(const char *s, int c) {
    char cc = (char)c;
    const char *last = NULL;
    while (*s) { if (*s == cc) last = s; s++; }
    if (cc == 0) return (char*)s;
    return (char*)last;
}
char *strchrnul(const char *s, int c) {
    char cc = (char)c;
    while (*s) { if (*s == cc) return (char*)s; s++; }
    return (char*)s;
}
size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (s[n]) {
        const char *r = reject;
        while (*r) { if (*r == s[n]) return n; r++; }
        n++;
    }
    return n;
}
size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (s[n]) {
        const char *a = accept; int ok = 0;
        while (*a) { if (*a == s[n]) { ok = 1; break; } a++; }
        if (!ok) return n;
        n++;
    }
    return n;
}
char *strstr(const char *hay, const char *needle) {
    if (!*needle) return (char*)hay;
    for (; *hay; hay++) {
        const char *a = hay, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char*)hay;
    }
    return NULL;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char*)s;
    unsigned char v = (unsigned char)c;
    while (n--) { if (*p == v) return (void*)p; p++; }
    return NULL;
}
void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char*)s + n;
    unsigned char v = (unsigned char)c;
    while (n--) { p--; if (*p == v) return (void*)p; }
    return NULL;
}
void *rawmemchr(const void *s, int c) {
    const unsigned char *p = (const unsigned char*)s;
    unsigned char v = (unsigned char)c;
    while (*p != v) p++;
    return (void*)p;
}
void *memmem(const void *h, size_t hl, const void *n, size_t nl) {
    const unsigned char *hay = (const unsigned char*)h;
    const unsigned char *ndl = (const unsigned char*)n;
    if (nl == 0) return (void*)hay;
    if (nl > hl) return NULL;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t j;
        for (j = 0; j < nl && hay[i+j] == ndl[j]; j++) ;
        if (j == nl) return (void*)(hay + i);
    }
    return NULL;
}
void *mempcpy(void *d, const void *s, size_t n) { memcpy(d,s,n); return (char*)d + n; }
void  explicit_bzero(void *d, size_t n) {
    volatile unsigned char *p = (volatile unsigned char*)d;
    while (n--) *p++ = 0;
}
void *explicit_memset(void *s, int c, size_t n) {
    volatile unsigned char *p = (volatile unsigned char*)s;
    unsigned char v = (unsigned char)c;
    while (n--) *p++ = v;
    return s;
}
int ffs(int i) {
    if (!i) return 0;
    int n = 1;
    while (!(i & 1)) { i >>= 1; n++; }
    return n;
}
int ffsl(long i) {
    if (!i) return 0;
    int n = 1;
    while (!(i & 1)) { i >>= 1; n++; }
    return n;
}
int ffsll(long long i) {
    if (!i) return 0;
    int n = 1;
    while (!(i & 1)) { i >>= 1; n++; }
    return n;
}
void  bzero(void *d, size_t n) { memset(d, 0, n); }
void  bcopy(const void *s, void *d, size_t n) { memmove(d, s, n); }
int   bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        const char *a = accept;
        while (*a) { if (*a == *s) return (char*)s; a++; }
        s++;
    }
    return NULL;
}
char *strtok_r(char *s, const char *delim, char **saveptr) {
    if (!s) s = *saveptr;
    while (*s && strchr(delim, *s)) s++;
    if (!*s) { *saveptr = s; return NULL; }
    char *tok = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s) { *s = 0; s++; }
    *saveptr = s;
    return tok;
}
char *strtok(char *s, const char *delim) { static char *save = NULL; return strtok_r(s, delim, &save); }
size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}
char *strndup(const char *s, size_t n) {
    size_t l = strnlen(s, n);
    char *r = (char*)malloc(l + 1);
    if (!r) return NULL;
    memcpy(r, s, l); r[l] = 0;
    return r;
}
static int __to_lower(int c) { return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c; }
int strcasecmp(const char *a, const char *b) {
    while (*a && __to_lower(*a) == __to_lower(*b)) { a++; b++; }
    return __to_lower(*a) - __to_lower(*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
    while (n--) {
        if (__to_lower(*a) != __to_lower(*b)) return __to_lower(*a) - __to_lower(*b);
        if (!*a) return 0;
        a++; b++;
    }
    return 0;
}
int strcoll(const char *a, const char *b) { return strcmp(a, b); }
size_t strxfrm(char *d, const char *s, size_t n) {
    size_t l = strlen(s);
    if (n) {
        size_t c = (l < n-1) ? l : n-1;
        memcpy(d, s, c); d[c] = 0;
    }
    return l;
}
char *strsignal(int sig) { (void)sig; return "unknown signal"; }
long strtol(const char *s, char **endp, int base) {
    long v = 0, sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++) {
        int d;
        if      (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
    }
    if (endp) *endp = (char*)s;
    return sign * v;
}
unsigned long strtoul(const char *s, char **endp, int base) {
    return (unsigned long)strtol(s, endp, base);
}
long long strtoll(const char *s, char **endp, int base) {
    return (long long)strtol(s, endp, base);
}
unsigned long long strtoull(const char *s, char **endp, int base) {
    return (unsigned long long)strtol(s, endp, base);
}
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

/* strings.h: impls moved above to string section */

/* ---------- tiny vsnprintf (覆盖 Mesa %s/%d/%u/%x/%lu/%zu/%p/%llu/%c 大部分) ---------- */
#include <stdarg.h>  /* 提前包含, 避免与 GCC 15 C23 stdarg.h 的 va_start 重定义冲突 */

static int __pr_num(char *out, int outsz, unsigned long long v, int base, int caps, int w) {
    char tmp[32]; int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) {
        int d = v % base; v /= base;
        char c = (d < 10) ? ('0' + d) : ((caps ? 'A' : 'a') + (d - 10));
        tmp[i++] = c;
    }
    int pad = 0;
    if (w > i) pad = w - i;
    int n = 0;
    while (pad-- && n < outsz) out[n++] = ' ';
    while (i-- && n < outsz) out[n++] = tmp[i];
    return n;
}
int vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap) {
    int n = 0;
    if (sz == 0) return 0;
    sz--;
    while (*fmt && n < (int)sz) {
        if (*fmt != '%') { buf[n++] = *fmt++; continue; }
        fmt++;
        int width = 0, have_w = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); have_w = 1; fmt++; }
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        if (*fmt == 'z') { lng = (sizeof(long) == 8) ? 2 : 1; fmt++; }
        if (*fmt == 't') { lng = (sizeof(long) == 8) ? 2 : 1; fmt++; }
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s && n < (int)sz) buf[n++] = *s++;
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                buf[n++] = c;
                break;
            }
            case 'd': case 'i': {
                long long v;
                if (lng >= 2) v = va_arg(ap, long long);
                else if (lng == 1) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                if (v < 0) { if (n < (int)sz) buf[n++] = '-'; v = -v; }
                n += __pr_num(buf + n, (n < (int)sz) ? (int)sz - n : 0, (unsigned long long)v, 10, 0, width);
                break;
            }
            case 'u': {
                unsigned long long v;
                if (lng >= 2) v = va_arg(ap, unsigned long long);
                else if (lng == 1) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                n += __pr_num(buf + n, (n < (int)sz) ? (int)sz - n : 0, v, 10, 0, width);
                break;
            }
            case 'x': case 'X': {
                unsigned long long v;
                if (lng >= 2) v = va_arg(ap, unsigned long long);
                else if (lng == 1) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                n += __pr_num(buf + n, (n < (int)sz) ? (int)sz - n : 0, v, 16, (*fmt == 'X'), width);
                break;
            }
            case 'p': {
                unsigned long v = (unsigned long)va_arg(ap, void*);
                if (n + 2 <= (int)sz) { buf[n++] = '0'; buf[n++] = 'x'; }
                n += __pr_num(buf + n, (n < (int)sz) ? (int)sz - n : 0, v, 16, 0, 0);
                break;
            }
            case '%': buf[n++] = '%'; break;
            default:  buf[n++] = *fmt; break;
        }
        fmt++;
    }
    buf[n] = 0;
    return n;
}
int snprintf(char *buf, size_t sz, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, sz, fmt, ap);
    va_end(ap); return r;
}
int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap); return r;
}

/* ---------- stdout/stderr 输出 ---------- */
int vprintf(const char *fmt, va_list ap) {
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (__efm_api_ok() && EFM_API_PTR->print) EFM_API_PTR->print(buf);
    return n;
}
int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap); return r;
}
int fprintf(void *f /*unused*/, const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (__efm_api_ok() && EFM_API_PTR->print) EFM_API_PTR->print(buf);
    return n;
}
int vfprintf(void *f, const char *fmt, va_list ap) {
    (void)f;
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (__efm_api_ok() && EFM_API_PTR->print) EFM_API_PTR->print(buf);
    return n;
}
int fputs(const char *s, void *f) { (void)f; if (EFM_API_PTR->print) EFM_API_PTR->print(s); return 0; }
int puts(const char *s) { if (EFM_API_PTR->print) { EFM_API_PTR->print(s); EFM_API_PTR->print("\n"); } return 0; }
void perror(const char *s) {
    if (s && *s) { fprintf(NULL, "%s: ", s); }
    fprintf(NULL, "errno=%d\n", errno);
}

/* ---------- stdlib: malloc 族 ---------- */
void *malloc(size_t n) {
    if (!__efm_api_ok()) { errno = ENOSYS; return NULL; }
    void *p = EFM_API_PTR->malloc_fn((unsigned long)n);
    if (!p) errno = ENOMEM;
    return p;
}
void free(void *p) {
    if (!p) return;
    if (__efm_api_ok()) EFM_API_PTR->free_fn(p);
}
void *calloc(size_t cnt, size_t sz) {
    size_t n = cnt * sz;
    void *p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}
void *realloc(void *old, size_t n) {
    /* 简化实现: malloc + memcpy + free (不追踪老 size, 对 Mesa 够用) */
    if (!old) return malloc(n);
    void *p = malloc(n);
    if (p && old) memcpy(p, old, n); /* 可能 over-copy, 但 Mesa 不会在 realloc 前依赖尾部 */
    free(old);
    return p;
}
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (alignment < sizeof(void*) || (alignment & (alignment - 1))) return EINVAL;
    /* 简单方案: malloc + 尾部对齐指针 (简化: malloc 已足够对齐). */
    void *p = malloc(size + alignment);
    if (!p) return ENOMEM;
    unsigned long addr = (unsigned long)p + alignment - 1;
    addr &= ~(unsigned long)(alignment - 1);
    *memptr = (void*)addr;
    return 0;
}
void *aligned_alloc(size_t alignment, size_t size) {
    void *p = NULL;
    int r = posix_memalign(&p, alignment, size);
    if (r) { errno = r; return NULL; }
    return p;
}

/* ---------- stdlib: qsort (非递归, 小栈) ---------- */
typedef int (*__cmp_fn_t)(const void*, const void*);
static void swap_i(unsigned char *a, unsigned char *b, size_t w) {
    while (w--) { unsigned char t = *a; *a++ = *b; *b++ = t; }
}
void qsort(void *base, size_t nmemb, size_t size, __cmp_fn_t cmp) {
    if (!nmemb || !size) return;
    long stk[32*2]; int sp = 0;
    unsigned char *b = (unsigned char*)base;
    stk[sp++] = 0; stk[sp++] = (long)nmemb - 1;
    while (sp) {
        long hi = stk[--sp], lo = stk[--sp];
        long pivot = lo, i = lo + 1, j = hi;
        while (i <= j) {
            while (i <= j && cmp(&b[i*size], &b[pivot*size]) <= 0) i++;
            while (i <= j && cmp(&b[j*size], &b[pivot*size]) >  0) j--;
            if (i < j) swap_i(&b[i*size], &b[j*size], size);
        }
        swap_i(&b[pivot*size], &b[j*size], size);
        if (lo < j - 1) { stk[sp++] = lo; stk[sp++] = j - 1; }
        if (j + 1 < hi)   { stk[sp++] = j + 1; stk[sp++] = hi; }
    }
}

/* bsearch: 二分查找 (要求 base 已按 cmp 升序排序) */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *)) {
    if (!nmemb || !size || !cmp) return NULL;
    const unsigned char *b = (const unsigned char*)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp(key, &b[mid * size]);
        if (c == 0) return (void*)&b[mid * size];
        else if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}

/* qsort_r: GNU 签名 (compar 第三参数为 arg) */
typedef int (*__cmp_r_fn_t)(const void*, const void*, void*);
void qsort_r(void *base, size_t nmemb, size_t size, __cmp_r_fn_t cmp, void *arg) {
    if (!nmemb || !size) return;
    long stk[32*2]; int sp = 0;
    unsigned char *b = (unsigned char*)base;
    stk[sp++] = 0; stk[sp++] = (long)nmemb - 1;
    while (sp) {
        long hi = stk[--sp], lo = stk[--sp];
        long pivot = lo, i = lo + 1, j = hi;
        while (i <= j) {
            while (i <= j && cmp(&b[i*size], &b[pivot*size], arg) <= 0) i++;
            while (i <= j && cmp(&b[j*size], &b[pivot*size], arg) >  0) j--;
            if (i < j) swap_i(&b[i*size], &b[j*size], size);
        }
        swap_i(&b[pivot*size], &b[j*size], size);
        if (lo < j - 1) { stk[sp++] = lo; stk[sp++] = j - 1; }
        if (j + 1 < hi)   { stk[sp++] = j + 1; stk[sp++] = hi; }
    }
}

/* ---------- unistd: sbrk/mmap (基于 malloc 的简化实现) ---------- */
void *sbrk(ptrdiff_t incr) {
    /* 简化: 每次 incr bytes 用 malloc 记录尾部偏移. Mesa 早期初始化仅
     * 用 sbrk(0) 探测堆边界, 返回非 NULL 指针即可. */
    static unsigned long g_brk = 0;
    if (g_brk == 0) g_brk = (unsigned long)malloc(65536); /* 起始 64KB 堆 */
    if (incr == 0) return (void*)g_brk;
    /* 不支持正 incr (动态扩堆用 mmap 就好); 返回错误, Mesa 会走 mmap 分支 */
    errno = ENOMEM;
    return (void*)-1L;
}
#define PROT_NONE    0x0
#define PROT_READ    0x1
#define PROT_WRITE   0x2
#define PROT_EXEC    0x4
#define MAP_SHARED   0x01
#define MAP_PRIVATE  0x02
#define MAP_FIXED    0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON     MAP_ANONYMOUS
#define MAP_FAILED   ((void*)-1)
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    if (!length) { errno = EINVAL; return MAP_FAILED; }
    void *p = malloc(length);
    if (!p) return MAP_FAILED;
    if (prot & PROT_WRITE || flags == 0) {
        /* 匿名映射: 零初始化 (Mesa 期望 MAP_ANONYMOUS 为 0) */
        memset(p, 0, length);
    }
    return p;
}
int munmap(void *addr, size_t length) { (void)length; free(addr); return 0; }
int mprotect(void *addr, size_t len, int prot) { (void)addr;(void)len;(void)prot; return 0; }

/* ---------- unistd: process ---------- */
pid_t getpid(void) { return (__efm_api_ok()) ? EFM_API_PTR->get_pid() : 0; }
pid_t gettid(void) { return getpid(); }
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }
int usleep(useconds_t us) {
    if (__efm_api_ok()) EFM_API_PTR->sleep_ms((unsigned long)(us / 1000 + ((us%1000)?1:0)));
    return 0;
}
unsigned int sleep(unsigned int s) {
    if (__efm_api_ok()) EFM_API_PTR->sleep_ms((unsigned long)s * 1000);
    return 0;
}
__attribute__((noreturn)) void _exit(int s) { (void)s; while(1) if (__efm_api_ok()) EFM_API_PTR->yield(); }
__attribute__((noreturn)) void exit(int s) { _exit(s); }
__attribute__((noreturn)) void abort(void) { _exit(1); }
int atexit(void (*fn)(void)) { (void)fn; return 0; }  /* 简化: EFMOS 不回调 exit */

/* ---------- assert support (include/assert.h 引用) ---------- */
void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    /* 避免 fprintf 依赖, 直接组装一条短消息用 print/put_char */
    char buf[512];
    snprintf(buf, sizeof(buf),
                   "Assertion failed: %s, file %s, line %d, function %s\n",
                   expr, file, line, func ? func : "?");
    if (__efm_api_ok()) {
        EFM_API_PTR->print(buf);
    }
    exit(1);
}

/* ---------- stdio: fopen/fread/fwrite/fclose (简易 FILE* 基于 file_read/write) ---------- */
#define EFM_FILE_RD    1
#define EFM_FILE_WR    2
typedef struct {
    int  mode;
    char path[256];
    char *buf;       /* 读取整个文件内容的缓存 (简化: 一次读入) */
    int  size;
    int  pos;
} EFM_FILE;
void *fopen(const char *path, const char *mode) {
    if (!path) return NULL;
    EFM_FILE *f = (EFM_FILE*)malloc(sizeof(*f));
    if (!f) return NULL;
    memset(f, 0, sizeof(*f));
    strncpy(f->path, path, sizeof(f->path)-1);
    if (mode[0] == 'r') {
        f->mode = EFM_FILE_RD;
        int sz = 1024*1024; /* 预估 1MB 缓存 */
        f->buf = (char*)malloc(sz);
        if (!f->buf) { free(f); return NULL; }
        if (!__efm_api_ok()) { free(f->buf); free(f); return NULL; }
        int rd = EFM_API_PTR->file_read(path, f->buf, sz);
        if (rd < 0) { free(f->buf); free(f); return NULL; }
        f->size = rd; f->pos = 0;
    } else if (mode[0] == 'w') {
        f->mode = EFM_FILE_WR;
        f->size = 0; f->pos = 0;
    } else {
        free(f); return NULL;
    }
    return f;
}
size_t fread(void *ptr, size_t size, size_t nmemb, void *stream) {
    EFM_FILE *f = (EFM_FILE*)stream;
    if (!f || f->mode != EFM_FILE_RD) return 0;
    size_t want = size * nmemb;
    if (f->pos + (int)want > f->size) want = (size_t)(f->size - f->pos);
    memcpy(ptr, f->buf + f->pos, want);
    f->pos += (int)want;
    return want / (size ? size : 1);
}
size_t fwrite(const void *ptr, size_t size, size_t nmemb, void *stream) {
    EFM_FILE *f = (EFM_FILE*)stream;
    if (!f || f->mode != EFM_FILE_WR) return 0;
    /* 简化: 每次追加写到 buffer, close 时 flush 到磁盘 */
    size_t n = size * nmemb;
    if (f->buf == NULL) {
        f->buf = (char*)malloc(n + 64);
        f->size = 0;
    } else {
        char *nb = (char*)realloc(f->buf, f->size + n + 64);
        if (!nb) return 0;
        f->buf = nb;
    }
    if (!f->buf) return 0;
    memcpy(f->buf + f->size, ptr, n);
    f->size += (int)n;
    return nmemb;
}
int fflush(void *stream) {
    EFM_FILE *f = (EFM_FILE*)stream;
    if (!f) return 0;
    if (f->mode == EFM_FILE_WR && f->buf && __efm_api_ok()) {
        EFM_API_PTR->file_write(f->path, f->buf, f->size);
    }
    return 0;
}
int fclose(void *stream) {
    EFM_FILE *f = (EFM_FILE*)stream;
    if (!f) return -1;
    fflush(f);
    free(f->buf);
    free(f);
    return 0;
}
char *fgets(char *s, int size, void *stream) {
    if (__efm_api_ok()) {
        /* 简化: fgets(stdin) 直接走 readline */
        int r = EFM_API_PTR->readline(s, size);
        if (r < 0) return NULL;
        /* readline 可能带/不带换行, 保证末尾有 '\n'? Mesa 不依赖换行, 直接返回 s */
        return s;
    }
    return NULL;
}
#define stdin   ((void*)0x1)
#define stdout  ((void*)0x2)
#define stderr  ((void*)0x3)
#define EOF     (-1)

/* ---------- sys/stat: stat/mkdir ---------- */
struct stat {
    dev_t     st_dev;
    ino_t     st_ino;
    mode_t    st_mode;
    nlink_t   st_nlink;
    uid_t     st_uid;
    gid_t     st_gid;
    dev_t     st_rdev;
    off_t     st_size;
    long      st_atim_tv_sec;
    long      st_mtim_tv_sec;
    long      st_ctim_tv_sec;
    long      st_blksize;
    long      st_blocks;
};
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m)  (((m) & S_IFDIR) != 0)
#define S_ISREG(m)  (((m) & S_IFREG) != 0)
#define S_IRWXU  00700
#define S_IRUSR  00400
#define S_IWUSR  00200
#define S_IXUSR  00100
#define S_IRWXG  00070
#define S_IRWXO  00007
#define S_IROTH  00004
#define S_IXOTH  00001
#define S_IRGRP  00040
#define S_IXGRP  00010

int stat(const char *path, struct stat *st) {
    memset(st, 0, sizeof(*st));
    if (!__efm_api_ok()) { errno = ENOSYS; return -1; }
    /* 先用 file_exists 判文件; 再尝试 list 父目录看 is_dir */
    if (EFM_API_PTR->file_exists(path)) {
        st->st_mode = S_IFREG | S_IRUSR | S_IWUSR;
        st->st_size = 0; /* 未知, 但够 Mesa 用 */
        st->st_nlink = 1;
        return 0;
    }
    /* 尝试用 file_list(path, ..., 1) 判断目录 (目录存在时会有子项, 空目录也 OK) */
    struct __efm_dirent d[2];
    int n = EFM_API_PTR->file_list(path, d, 2);
    if (n >= 0) {
        st->st_mode = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP;
        st->st_nlink = 2;
        return 0;
    }
    errno = ENOENT;
    return -1;
}
int fstat(int fd, struct stat *st) {
    (void)fd; memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | S_IRUSR | S_IWUSR;
    return 0;
}
int mkdir(const char *path, mode_t mode) {
    (void)mode;
    if (!__efm_api_ok()) { errno = ENOSYS; return -1; }
    int r = EFM_API_PTR->mkdir(path);
    if (r != 0) { errno = EIO; return -1; }
    return 0;
}
int unlink(const char *path) {
    if (!__efm_api_ok()) { errno = ENOSYS; return -1; }
    int r = EFM_API_PTR->file_delete(path);
    if (r != 0) { errno = ENOENT; return -1; }
    return 0;
}
int rmdir(const char *path) { return unlink(path); }
int access(const char *path, int mode) {
    (void)mode;
    if (__efm_api_ok() && EFM_API_PTR->file_exists(path)) return 0;
    errno = ENOENT; return -1;
}

/* ---------- dirent.h ---------- */
struct dirent {
    ino_t  d_ino;
    off_t  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char   d_name[256];
};
#define DT_REG 8
#define DT_DIR 4
#define DT_UNKNOWN 0
typedef struct {
    struct __efm_dirent *ents;
    int  count;
    int  pos;
} EFM_DIR;
void *opendir(const char *path) {
    if (!__efm_api_ok()) return NULL;
    EFM_DIR *d = (EFM_DIR*)malloc(sizeof(*d));
    if (!d) return NULL;
    memset(d, 0, sizeof(*d));
    int max = 256;
    d->ents = (struct __efm_dirent*)malloc(sizeof(struct __efm_dirent) * max);
    if (!d->ents) { free(d); return NULL; }
    int n = EFM_API_PTR->file_list(path, d->ents, max);
    if (n < 0) { free(d->ents); free(d); return NULL; }
    d->count = n; d->pos = 0;
    return d;
}
struct dirent *readdir(void *dirp) {
    static struct dirent g_r;
    EFM_DIR *d = (EFM_DIR*)dirp;
    if (!d || d->pos >= d->count) return NULL;
    struct __efm_dirent *e = &d->ents[d->pos++];
    memset(&g_r, 0, sizeof(g_r));
    g_r.d_ino  = (ino_t)(uintptr_t)e;
    g_r.d_type = e->is_dir ? DT_DIR : DT_REG;
    g_r.d_reclen = sizeof(g_r);
    for (int i = 0; i < 255 && e->name[i]; i++) g_r.d_name[i] = e->name[i];
    return &g_r;
}
int closedir(void *dirp) {
    EFM_DIR *d = (EFM_DIR*)dirp;
    if (!d) return -1;
    free(d->ents); free(d);
    return 0;
}

/* ---------- time.h: clock_gettime (TSC 近似) ---------- */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME           0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC          1
#endif
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW      4
#endif
#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 2
#endif
#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID  3
#endif
#ifndef _EFM_TIMESPEC_DEFINED
#define _EFM_TIMESPEC_DEFINED
struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
#endif

static inline unsigned long long __efm_rdtsc(void) {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return (unsigned long long)hi << 32 | lo;
}
int clock_gettime(clockid_t cid, struct timespec *tp) {
    (void)cid;
    static unsigned long long tsc0 = 0;
    if (!tsc0) tsc0 = __efm_rdtsc();
    unsigned long long diff = __efm_rdtsc() - tsc0;
    /* 假设 3GHz (QEMU): 近似 1/3,000,000,000 s per tick */
    unsigned long long sec  = diff / 3000000000ULL;
    unsigned long long frac = diff % 3000000000ULL;
    unsigned long long nsec = (frac * 1000000000ULL) / 3000000000ULL;
    tp->tv_sec  = (time_t)sec;
    tp->tv_nsec = (long)nsec;
    return 0;
}
time_t time(time_t *t) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    if (t) *t = ts.tv_sec;
    return ts.tv_sec;
}
int clock_getres(clockid_t cid, struct timespec *r) { (void)cid; r->tv_sec=0; r->tv_nsec=1; return 0; }
int gettimeofday(struct timeval *tv, void *tz) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    if (tv) { tv->tv_sec = ts.tv_sec; tv->tv_usec = ts.tv_nsec / 1000; }
    (void)tz;
    return 0;
}

/* ---------- pthread 简化 (基于 spawn_async + 原子锁) ---------- */
int pthread_create(pthread_t *t, const pthread_attr_t *a,
                   void *(*fn)(void*), void *arg) {
    (void)a;
    /* Mesa 的 threadpool 创建的是内核线程. 这里直接 yield 简化:
     *   EFMOS 没有直接"函数指针"线程接口, 但提供 spawn_async("shell","...") .efs 子进程.
     * 简化实现: 不实际并发, 立即同步调用 fn(arg) (足够通过编译 + 简单单线程 Mesa 程序).
     *   (真正线程版需 kernel 新增 spawn_fn 系统调用.) */
    *t = 1;
    void *(*volatile f)(void*) = fn;
    (*f)(arg);
    return 0;
}
int pthread_join(pthread_t t, void **retval) { (void)t; if (retval) *retval = NULL; return 0; }
pthread_t pthread_self(void) { return 1; }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    (void)a; *m = 0; return 0;
}
int pthread_mutex_lock(pthread_mutex_t *m) {
    /* 单核自旋: while (!CAS(m,0,1)) yield */
    while (__atomic_test_and_set((volatile void*)m, __ATOMIC_ACQUIRE)) {
        if (__efm_api_ok()) EFM_API_PTR->yield();
    }
    return 0;
}
int pthread_mutex_trylock(pthread_mutex_t *m) {
    if (__atomic_test_and_set((volatile void*)m, __ATOMIC_ACQUIRE)) return EBUSY;
    return 0;
}
int pthread_mutex_unlock(pthread_mutex_t *m) {
    __atomic_clear((volatile void*)m, __ATOMIC_RELEASE);
    return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *m) { *m = 0; return 0; }

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a;*c=0;return 0; }
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    (void)c;
    pthread_mutex_unlock(m);
    if (__efm_api_ok()) EFM_API_PTR->sleep_ms(10); /* 简化: 睡 10ms 当等待 */
    pthread_mutex_lock(m);
    return 0;
}
int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) {
    (void)t; return pthread_cond_wait(c, m);
}

int pthread_rwlock_init(pthread_rwlock_t *r, void *a) { (void)a; *r = 0; return 0; }
int pthread_rwlock_rdlock(pthread_rwlock_t *r) { return pthread_mutex_lock((pthread_mutex_t*)r); }
int pthread_rwlock_wrlock(pthread_rwlock_t *r) { return pthread_mutex_lock((pthread_mutex_t*)r); }
int pthread_rwlock_unlock(pthread_rwlock_t *r) { return pthread_mutex_unlock((pthread_mutex_t*)r); }
int pthread_rwlock_destroy(pthread_rwlock_t *r) { *r = 0; return 0; }

int pthread_barrier_init(pthread_barrier_t *b, void *a, unsigned c) { (void)a; (void)c; *b=0; return 0; }
int pthread_barrier_wait(pthread_barrier_t *b) { (void)b; return 0; }
int pthread_barrier_destroy(pthread_barrier_t *b) { *b = 0; return 0; }

int pthread_once(pthread_once_t *o, void (*fn)(void)) {
    if (__atomic_test_and_set((volatile void*)o, __ATOMIC_ACQUIRE) == 0) fn();
    return 0;
}
int pthread_key_create(pthread_key_t *k, void (*dst)(void*)) { (void)dst; static pthread_key_t next = 1; *k = next++; return 0; }
int pthread_key_delete(pthread_key_t k) { (void)k; return 0; }
void *pthread_getspecific(pthread_key_t k) { (void)k; return NULL; }
int   pthread_setspecific(pthread_key_t k, const void *v) { (void)k;(void)v; return 0; }

int pthread_setname_np(pthread_t t, const char *n) { (void)t; (void)n; return 0; }
int pthread_getname_np(pthread_t t, char *n, size_t l) { (void)t; (void)l; if (l>0) *n=0; return 0; }
int pthread_attr_init(pthread_attr_t *a) { *a = 0; return 0; }
int pthread_attr_destroy(pthread_attr_t *a) { *a = 0; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *a, size_t s) { (void)a;(void)s;return 0; }
int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *s) { (void)a;*s=0;return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int d) { (void)a;(void)d;return 0; }
int pthread_attr_getdetachstate(const pthread_attr_t *a, int *d) { (void)a; if(d)*d=0;return 0; }
int pthread_attr_setinheritsched(pthread_attr_t *a, int i) { (void)a;(void)i;return 0; }
int pthread_attr_getinheritsched(const pthread_attr_t *a, int *i) { (void)a;if(i)*i=0;return 0; }
int pthread_attr_setschedpolicy(pthread_attr_t *a, int p) { (void)a;(void)p;return 0; }
int pthread_attr_getschedpolicy(const pthread_attr_t *a, int *p) { (void)a;if(p)*p=0;return 0; }
int pthread_attr_setschedparam(pthread_attr_t *a, const void *p) { (void)a;(void)p;return 0; }
int pthread_attr_getschedparam(const pthread_attr_t *a, void *p) { (void)a;if(p)*(int*)p=0;return 0; }
int pthread_attr_setscope(pthread_attr_t *a, int s) { (void)a;(void)s;return 0; }
int pthread_attr_getscope(const pthread_attr_t *a, int *s) { (void)a;if(s)*s=0;return 0; }
int pthread_mutexattr_init(pthread_mutexattr_t *a) { *a=0;return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { *a=0;return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) { (void)a;(void)t;return 0; }
int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *t) { (void)a;if(t)*t=0;return 0; }
int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int p) { (void)a;(void)p;return 0; }
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *p) { (void)a;if(p)*p=0;return 0; }
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int p) { (void)a;(void)p;return 0; }
int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *a, int *p) { (void)a;if(p)*p=0;return 0; }
int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int p) { (void)a;(void)p;return 0; }
int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *a, int *p) { (void)a;if(p)*p=0;return 0; }
int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *abstime) { (void)abstime; return pthread_mutex_lock(m); }
int pthread_condattr_init(pthread_condattr_t *a) { *a=0;return 0; }
int pthread_condattr_destroy(pthread_condattr_t *a) { *a=0;return 0; }
int pthread_condattr_setclock(pthread_condattr_t *a, int c) { (void)a;(void)c;return 0; }
int pthread_condattr_getclock(const pthread_condattr_t *a, int *c) { (void)a;if(c)*c=0;return 0; }
int pthread_condattr_setpshared(pthread_condattr_t *a, int p) { (void)a;(void)p;return 0; }
int pthread_condattr_getpshared(const pthread_condattr_t *a, int *p) { (void)a;if(p)*p=0;return 0; }
typedef unsigned long pthread_rwlockattr_t;
int pthread_rwlockattr_init(pthread_rwlockattr_t *a) { *a=0;return 0; }
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *a) { *a=0;return 0; }
int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int p) { (void)a;(void)p;return 0; }
int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *a, int *p) { (void)a;if(p)*p=0;return 0; }
int pthread_spin_init(pthread_spinlock_t *l, int pshared) { (void)pshared; *l=0;return 0; }
int pthread_spin_destroy(pthread_spinlock_t *l) { *l=0;return 0; }
int pthread_spin_lock(pthread_spinlock_t *l) { return pthread_mutex_lock((pthread_mutex_t*)l); }
int pthread_spin_trylock(pthread_spinlock_t *l) { return pthread_mutex_trylock((pthread_mutex_t*)l); }
int pthread_spin_unlock(pthread_spinlock_t *l) { return pthread_mutex_unlock((pthread_mutex_t*)l); }
int pthread_detach(pthread_t t) { (void)t; return 0; }
__attribute__((noreturn))
void pthread_exit(void *retval) { (void)retval; exit(0); }
int pthread_cancel(pthread_t t) { (void)t; return 0; }
int pthread_setcancelstate(int state, int *oldstate) { if(oldstate)*oldstate=state; return 0; }
int pthread_setcanceltype(int type, int *oldtype) { if(oldtype)*oldtype=type; return 0; }
void pthread_testcancel(void) {}
int pthread_setschedparam(pthread_t t, int policy, const void *param) { (void)t;(void)policy;(void)param;return 0; }
int pthread_getschedparam(pthread_t t, int *policy, void *param) { (void)t;if(policy)*policy=0;if(param)*(int*)param=0;return 0; }
int pthread_setschedprio(pthread_t t, int prio) { (void)t;(void)prio;return 0; }
int pthread_sigmask(int how, const void *set, void *oset) { (void)how;(void)set;(void)oset;return 0; }
/* CPU 亲和性 / 线程 CPU 时钟: EFMOS 单核, 全部成功但只设 CPU 0 */
int pthread_setaffinity_np(pthread_t t, size_t cpusetsize, const cpu_set_t *cpuset) {
    (void)t; (void)cpusetsize; (void)cpuset; return 0;
}
int pthread_getaffinity_np(pthread_t t, size_t cpusetsize, cpu_set_t *cpuset) {
    (void)t; (void)cpusetsize;
    if (cpuset) { CPU_ZERO(cpuset); CPU_SET(0, cpuset); }
    return 0;
}
int pthread_getcpuclockid(pthread_t t, clockid_t *cid) { (void)t; if(cid)*cid=CLOCK_THREAD_CPUTIME_ID; return 0; }
/* ---------- sys/resource.h 实现 ---------- */
int getrlimit(int resource, struct rlimit *rlim) { (void)resource; if(rlim){rlim->rlim_cur=RLIM_INFINITY;rlim->rlim_max=RLIM_INFINITY;} return 0; }
int setrlimit(int resource, const struct rlimit *rlim) { (void)resource;(void)rlim; return 0; }
int getrusage(int who, struct rusage *usage) { (void)who; if(usage) memset(usage,0,sizeof(*usage)); return 0; }
int getpriority(int which, int who) { (void)which; (void)who; return 0; }
int setpriority(int which, int who, int prio) { (void)which; (void)who; (void)prio; return 0; }
/* ---------- time extensions ---------- */
int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (req && __efm_api_ok()) {
        unsigned long ms = (unsigned long)req->tv_sec * 1000UL + (unsigned long)req->tv_nsec / 1000000UL;
        if (ms == 0 && req->tv_nsec > 0) ms = 1;
        EFM_API_PTR->sleep_ms(ms);
    }
    return 0;
}
int clock_nanosleep(clockid_t id, int flags, const struct timespec *req, struct timespec *rem) {
    (void)id; (void)flags;
    return nanosleep(req, rem);
}
struct tm *gmtime_r(const time_t *t, struct tm *out) {
    if (!t || !out) return 0;
    /* simplified: year = 1970 offset, no proper leap calc */
    unsigned long secs = (unsigned long)*t;
    out->tm_sec  = (int)(secs % 60); secs /= 60;
    out->tm_min  = (int)(secs % 60); secs /= 60;
    out->tm_hour = (int)(secs % 24);
    unsigned long days = secs / 24;
    out->tm_wday = (int)((days + 4) % 7);
    unsigned int y = 1970;
    while (1) {
        unsigned long ydays = ((y%4==0 && (y%100!=0 || y%400==0)) ? 366 : 365);
        if (days < ydays) break;
        days -= ydays; y++;
    }
    out->tm_year = (int)y - 1900;
    static const char mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = ((y%4==0 && (y%100!=0 || y%400==0)) ? 1 : 0);
    int mon;
    for (mon = 0; mon < 12; mon++) {
        int m = mdays[mon] + (mon == 1 ? leap : 0);
        if (days < (unsigned long)m) break;
        days -= m;
    }
    out->tm_mon = mon;
    out->tm_mday = (int)days + 1;
    out->tm_yday = 0; /* unused */
    out->tm_isdst = 0;
    out->tm_gmtoff = 0;
    out->tm_zone = "UTC";
    return out;
}
struct tm *localtime_r(const time_t *t, struct tm *out) { return gmtime_r(t, out); }
/* 非可重入版本: 用静态缓冲区 (C++ <ctime> 和 Mesa 偶尔调用) */
static struct tm __efm_tm_buf;
static char __efm_asc_buf[32];
struct tm *gmtime(const time_t *t)   { return gmtime_r(t, &__efm_tm_buf); }
struct tm *localtime(const time_t *t){ return localtime_r(t, &__efm_tm_buf); }
char *asctime(const struct tm *tm)   { return asctime_r(tm, __efm_asc_buf); }
char *ctime(const time_t *t)         { return ctime_r(t, __efm_asc_buf); }
clock_t clock(void) { return (clock_t)0; }
double difftime(time_t t1, time_t t0) { return (double)(t1 - t0); }
time_t timegm(struct tm *tm) { return mktime(tm); }
int timespec_get(struct timespec *ts, int base) {
    (void)base;
    return clock_gettime(CLOCK_REALTIME, ts) == 0 ? base : 0;
}
time_t mktime(struct tm *tm) { (void)tm; return 0; }
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    (void)fmt; (void)tm;
    if (!s || max == 0) return 0;
    s[0] = 0;
    return 0;
}
char *asctime_r(const struct tm *tm, char *buf) { (void)tm; if (buf) buf[0] = 0; return buf; }
char *ctime_r(const time_t *t, char *buf) { (void)t; if (buf) buf[0] = 0; return buf; }
/* ---------- sched ---------- */
int sched_getcpu(void) { return 0; }
int sched_yield(void) { if (__efm_api_ok()) EFM_API_PTR->yield(); return 0; }
int sched_get_priority_min(int policy) { (void)policy; return 1; }
int sched_get_priority_max(int policy) { (void)policy; return 99; }
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param) { (void)pid;(void)policy;(void)param;return 0; }
int sched_getscheduler(pid_t pid) { (void)pid; return SCHED_OTHER; }
int sched_setparam(pid_t pid, const struct sched_param *param) { (void)pid;(void)param;return 0; }
int sched_getparam(pid_t pid, struct sched_param *param) { (void)pid; if(param) param->sched_priority = 0; return 0; }
int sched_rr_get_interval(pid_t pid, struct timespec *tp) { (void)pid; if(tp){tp->tv_sec=0;tp->tv_nsec=0;}return 0; }
int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask) { (void)pid;(void)cpusetsize;(void)mask;return 0; }
int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
    (void)pid; (void)cpusetsize;
    if (mask) { CPU_ZERO(mask); CPU_SET(0, mask); }
    return 0;
}

/* ---------- sys/sysmacros.h: major/minor/makedev ----------
 * Linux glibc 约定: dev_t 为 64-bit, major 在高位, minor 在低位.
 * EFMOS dev_t 是 unsigned (32-bit), 用经典的 12+20 拆分即可. */
unsigned int __efm_major(dev_t dev) { return (unsigned int)((dev >> 8) & 0xfff); }
unsigned int __efm_minor(dev_t dev) { return (unsigned int)(dev & 0xff) | (unsigned int)((dev >> 12) & 0xfff00); }
dev_t        __efm_makedev(unsigned int maj, unsigned int min) { return (dev_t)(((maj & 0xfff) << 8) | (min & 0xff) | ((min & 0xfff00) << 12)); }
/* 提供 major()/minor()/makedev() 函数符号 (loader.c 等直接调用) */
int major(dev_t dev) { return (int)__efm_major(dev); }
int minor(dev_t dev) { return (int)__efm_minor(dev); }
dev_t makedev(unsigned int maj, unsigned int min) { return __efm_makedev(maj, min); }

/* ---------- signal.h stub ---------- */
typedef void (*__efm_sighandler_t)(int);
__efm_sighandler_t signal(int sig, __efm_sighandler_t handler) { (void)sig;(void)handler; return 0; }
int raise(int sig) { (void)sig; return 0; }
int kill(pid_t pid, int sig) { (void)pid;(void)sig; errno=ENOSYS; return -1; }
int sigemptyset(void *set) { if (set) ((unsigned long*)set)[0] = 0; return 0; }
int sigfillset(void *set) { if (set) ((unsigned long*)set)[0] = ~0UL; return 0; }
int sigaddset(void *set, int signum) { (void)signum; if (set) ((unsigned long*)set)[0] = 1; return 0; }
int sigdelset(void *set, int signum) { (void)signum; return sigemptyset(set); }
int sigismember(const void *set, int signum) { (void)set;(void)signum; return 0; }
int sigprocmask(int how, const void *set, void *oset) { (void)how;(void)set;(void)oset; return 0; }
int sigaction(int sig, const void *act, void *oact) { (void)sig;(void)act;(void)oact; return 0; }
int sigwait(const void *set, int *sig) { (void)set; if (__efm_api_ok()) EFM_API_PTR->sleep_ms(10); if (sig) *sig = 0; return 0; }
int sigpending(void *set) { if (set) ((unsigned long*)set)[0] = 0; return 0; }
int pause(void) { if (__efm_api_ok()) EFM_API_PTR->sleep_ms(1000); return 0; }

/* ---------- dlfcn.h ---------- */
#define RTLD_NOW   2
#define RTLD_GLOBAL 0x100
void *dlopen(const char *f, int fl) { (void)f;(void)fl; errno=ENOSYS; return NULL; }
void *dlsym(void *h, const char *n) { (void)h;(void)n; return NULL; }
int   dlclose(void *h) { (void)h; return 0; }
char *dlerror(void) { return "no dynamic loader"; }

/* ---------- misc libc wrappers Mesa 会引用 ---------- */
int strerror_r(int err, char *buf, size_t sz) {
    const char *m = "unknown error";
    switch (err) {
        case ENOENT: m = "no such file"; break;
        case ENOMEM: m = "out of memory"; break;
        case EINVAL: m = "invalid argument"; break;
        case ENOSYS: m = "not implemented"; break;
        case EACCES: m = "permission denied"; break;
    }
    size_t i = 0; while (i + 1 < sz && m[i]) { buf[i] = m[i]; i++; }
    if (sz > 0) buf[i] = 0;
    return 0;
}
char *strerror(int err) {
    static __thread char s[64];
    strerror_r(err, s, sizeof(s));
    return s;
}
int setenv(const char *name, const char *v, int over) { (void)name;(void)v;(void)over; return 0; }
int unsetenv(const char *n) { (void)n; return 0; }
char *getenv(const char *n) {
    /* Mesa 会查 MESA_GLSL_CACHE_DIR / LIBGL_DRIVERS_PATH 等, 返回 NULL 走默认路径即可 */
    (void)n; return NULL;
}
char *secure_getenv(const char *name) { return getenv(name); }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isprint(int c) { return c >= 0x20 && c <= 0x7E; }
int toupper(int c) { if (c >= 'a' && c <= 'z') return c - 'a' + 'A'; return c; }
int tolower(int c) { if (c >= 'A' && c <= 'Z') return c - 'A' + 'a'; return c; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isascii(int c) { return (c & ~0x7F) == 0; }
int iscntrl(int c) { return c < 0x20 || c == 0x7F; }
int ispunct(int c) { return isprint(c) && !isspace(c) && !isalnum(c); }
int isgraph(int c) { return isprint(c) && c != ' '; }

unsigned long sysconf_DELETEME_stub(int name) { (void)name; return 1; }
int getpagesize(void) { return 4096; }

int pipe(int fds[2]) { (void)fds; errno = ENOSYS; return -1; }
int dup2(int a, int b) { (void)a;(void)b; errno = ENOSYS; return -1; }
pid_t fork(void) { errno = ENOSYS; return -1; }
int execve(const char *p, char *const a[], char *const e[]) { (void)p;(void)a;(void)e; errno=ENOSYS; return -1; }
int waitpid(pid_t p, int *s, int o) { (void)p;(void)s;(void)o; errno=ECHILD; return -1; }
#define ECHILD 10
pid_t wait(int *s) { return waitpid(-1, s, 0); }

/* ---------- unistd/stdlib/stdio extras Mesa uses ---------- */
/* forward decl for existing efmlibc impls used below */
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
int ftruncate(int fd, off_t length) { (void)fd;(void)length; errno=ENOSYS; return -1; }
int truncate(const char *path, off_t length) { (void)path;(void)length; errno=ENOSYS; return -1; }
int fsync(int fd) { (void)fd; return 0; }
int fdatasync(int fd) { (void)fd; return 0; }
int flock(int fd, int op) { (void)fd;(void)op; return 0; }
int link(const char *a, const char *b) { (void)a;(void)b; errno=ENOSYS; return -1; }
int symlink(const char *t, const char *l) { (void)t;(void)l; errno=ENOSYS; return -1; }
ssize_t readlink(const char *p, char *b, size_t s) { (void)p;(void)b;(void)s; errno=ENOENT; return -1; }
int chmod(const char *p, mode_t m) { (void)p;(void)m; return 0; }
int fchmod(int fd, mode_t m) { (void)fd;(void)m; return 0; }
int chown(const char *p, uid_t o, gid_t g) { (void)p;(void)o;(void)g; return 0; }
int fchown(int fd, uid_t o, gid_t g) { (void)fd;(void)o;(void)g; return 0; }
int access__(const char *p, int m) { (void)p;(void)m; errno=ENOENT; return -1; }
int faccessat(int d, const char *p, int m, int f) { (void)d;(void)m;(void)f; return access__(p,m); }
int gethostname(char *n, size_t l) { (void)l; if (n && l>0) *n=0; return 0; }
int sethostname(const char *n, size_t l) { (void)n;(void)l; return 0; }
int getdomainname(char *n, size_t l) { (void)l; if (n && l>0) *n=0; return 0; }
int setdomainname(const char *n, size_t l) { (void)n;(void)l; return 0; }
int chdir(const char *p) { (void)p; return 0; }
int fchdir(int fd) { (void)fd; return 0; }
char *getcwd(char *b, size_t s) { (void)s; if (b && s>2) { b[0]='/'; b[1]=0; } return b; }
int isatty(int fd) { (void)fd; return 0; }
ssize_t writev(int fd, const void *iov, int iovcnt) { (void)fd;(void)iov;(void)iovcnt; errno=ENOSYS; return -1; }
ssize_t pread(int fd, void *b, size_t c, off_t o) { (void)o; return read(fd,b,c); }
ssize_t pwrite(int fd, const void *b, size_t c, off_t o) { (void)o; return write(fd,b,c); }
int dup(int oldfd) { (void)oldfd; errno=ENOSYS; return -1; }
int close_range(unsigned int a, unsigned int b, int fl) { (void)a;(void)b;(void)fl; return 0; }
int memfd_create(const char *n, unsigned int f) { (void)n;(void)f; errno=ENOSYS; return -1; }
int eventfd(unsigned int i, int f) { (void)i;(void)f; errno=ENOSYS; return -1; }
int eventfd_read(int fd, uint64_t *v) { (void)fd;(void)v; errno=ENOSYS; return -1; }
int eventfd_write(int fd, uint64_t v) { (void)fd;(void)v; errno=ENOSYS; return -1; }
int getdtablesize(void) { return 256; }
int nice(int inc) { (void)inc; return 0; }
int setuid(uid_t u) { (void)u; return 0; }
int setgid(gid_t g) { (void)g; return 0; }
pid_t setsid(void) { return 1; }
pid_t getsid(pid_t p) { (void)p; return 1; }
pid_t getpgrp(void) { return 1; }
int setpgid(pid_t p, pid_t pg) { (void)p;(void)pg; return 0; }
int setpgrp(void) { return 0; }
int tcsetpgrp(int fd, pid_t pg) { (void)fd;(void)pg; return 0; }
pid_t tcgetpgrp(int fd) { (void)fd; return 1; }

/* ---------- sysconf ---------- */
#include <unistd.h>
long sysconf(int name) {
    switch (name) {
        case _SC_NPROCESSORS_ONLN: return 1;
        case _SC_NPROCESSORS_CONF: return 1;
        case _SC_PAGESIZE: return PAGE_SIZE;
        case _SC_PHYS_PAGES: return (1024*1024*256)/PAGE_SIZE;   /* 256MB fake */
        case _SC_AVPHYS_PAGES: return (1024*1024*128)/PAGE_SIZE;
        case _SC_ARG_MAX:   return 8192;
        case _SC_CHILD_MAX: return 64;
        case _SC_CLK_TCK:   return 100;
        case _SC_OPEN_MAX:  return 1024;
        case _SC_STREAM_MAX:return 256;
        case _SC_TZNAME_MAX:return 64;
        case _SC_VERSION:   return 200809L;
        case _SC_XOPEN_VERSION: return 700;
        case _SC_MONOTONIC_CLOCK: return 1;
        case _SC_GETPW_R_SIZE_MAX: return 1024;
        case _SC_GETGR_R_SIZE_MAX: return 1024;
        case _SC_HOST_NAME_MAX: return 64;
        case _SC_TTY_NAME_MAX: return 32;
        case _SC_LINE_MAX:  return 2048;
        case _SC_RTSIG_MAX: return 32;
        case _SC_SEM_NSEMS_MAX: return 256;
        case _SC_SEM_VALUE_MAX: return 0x7fffffff;
        case _SC_SIGQUEUE_MAX: return 32;
        case _SC_TIMER_MAX: return 32;
        case _SC_2_VERSION: return 500;
        case _SC_2_C_BIND:  return 1;
        case _SC_2_C_DEV:   return 1;
        case _SC_SYSMALLOC: return 1;
        default:
            errno = EINVAL; return -1;
    }
}
long sysconf_stub_placeholder;

/* ---------- scanf stubs (return EOF for simplicity) ---------- */
int vscanf(const char *fmt, va_list ap) { (void)fmt;(void)ap; errno = EIO; return -1; }
int vfscanf(void *s, const char *fmt, va_list ap) { (void)s;(void)fmt;(void)ap; errno = EIO; return -1; }
int vsscanf(const char *s, const char *fmt, va_list ap) { (void)s;(void)fmt;(void)ap; return 0; }
int scanf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r=vscanf(fmt,ap); va_end(ap); return r; }
int fscanf(void *s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r=vfscanf(s,fmt,ap); va_end(ap); return r; }
int sscanf(const char *s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r=vsscanf(s,fmt,ap); va_end(ap); return r; }

/* ---------- stdlib extras ---------- */
int mkstemp(char *t) { (void)t; errno=ENOENT; return -1; }
char *mkdtemp(char *t) { (void)t; errno=ENOENT; return NULL; }
char *mktemp(char *t) { (void)t; if (t && *t) *t=0; return t; }
int mkostemp(char *t, int f) { (void)f; return mkstemp(t); }
int mkostemps(char *t, int s, int f) { (void)s;(void)f; return mkstemp(t); }
int mkstemps(char *t, int s) { (void)s; return mkstemp(t); }
long random(void) { static unsigned long x = 1; x = x*6364136223846793005UL + 1442695040888963407UL; return (long)(x>>32); }
void srandom(unsigned int seed) { static unsigned long *x = 0; (void)seed; (void)x; }
int rand(void) { return (int)random(); }
void srand(unsigned int seed) { srandom(seed); }
int posix_openpt(int f) { (void)f; errno=ENOSYS; return -1; }
int grantpt(int fd) { (void)fd; errno=ENOSYS; return -1; }
int unlockpt(int fd) { (void)fd; return 0; }
char *ptsname(int fd) { (void)fd; return NULL; }
int ptsname_r(int fd, char *b, size_t s) { (void)fd;(void)b;(void)s; errno=ENOSYS; return -1; }
double atof(const char *s) { return (double)atol(s); }
long double strtold(const char *s, char **e) { (void)e; return (long double)atof(s); }
double strtod(const char *s, char **e) { (void)e; return atof(s); }
float strtof(const char *s, char **e) { (void)e; return (float)atof(s); }

/* ---------- stdio extras ---------- */
#include <stdarg.h>
extern int vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap);
int asprintf(char **sp, const char *fmt, ...) {
    va_list ap1, ap2;
    va_start(ap1, fmt);
    char stack[512];
    __builtin_va_copy(ap2, ap1);
    int needed = vsnprintf(stack, sizeof(stack), fmt, ap1);
    va_end(ap1);
    if (needed < 0) { __builtin_va_end(ap2); *sp = NULL; return -1; }
    size_t sz = (size_t)needed + 1;
    char *buf = (char*)malloc(sz);
    if (!buf) { __builtin_va_end(ap2); *sp = NULL; return -1; }
    int n = vsnprintf(buf, sz, fmt, ap2);
    __builtin_va_end(ap2);
    *sp = buf;
    return n;
}
int vasprintf(char **sp, const char *fmt, va_list ap) {
    char stack[512];
    va_list ap2;
    __builtin_va_copy(ap2, ap);
    int needed = vsnprintf(stack, sizeof(stack), fmt, ap);
    if (needed < 0) { __builtin_va_end(ap2); *sp = NULL; return -1; }
    size_t sz = (size_t)needed + 1;
    char *buf = (char*)malloc(sz);
    if (!buf) { __builtin_va_end(ap2); *sp = NULL; return -1; }
    int n = vsnprintf(buf, sz, fmt, ap2);
    __builtin_va_end(ap2);
    *sp = buf;
    return n;
}
ssize_t getline(char **lp, size_t *n, void *s) { (void)lp;(void)n;(void)s; errno=ENOSYS; return -1; }
ssize_t getdelim(char **lp, size_t *n, int d, void *s) { (void)d; return getline(lp,n,s); }
char *fgetln(void *s, size_t *l) { (void)s; if (l) *l=0; return NULL; }
int rename(const char *a, const char *b) { (void)a;(void)b; errno=ENOSYS; return -1; }
int renameat(int od, const char *a, int nd, const char *b) { (void)od;(void)nd; return rename(a,b); }
int fileno(void *s) { (void)s; return -1; }
void *fdopen(int fd, const char *m) { (void)fd;(void)m; return NULL; }
void *freopen(const char *p, const char *m, void *s) { (void)p;(void)m;(void)s; return NULL; }
void *tmpfile(void) { return NULL; }
char *tmpnam(char *s) { (void)s; return NULL; }
int setvbuf(void *s, char *b, int m, size_t sz) { (void)s;(void)b;(void)m;(void)sz; return 0; }
void setbuf(void *s, char *b) { (void)s;(void)b; }
int fseek(void *s, long o, int w) { (void)s;(void)o;(void)w; return -1; }
long ftell(void *s) { (void)s; return -1; }
void rewind(void *s) { (void)s; }
int fgetpos(void *s, void *p) { (void)s;(void)p; return -1; }
int fsetpos(void *s, const void *p) { (void)s;(void)p; return -1; }
int feof(void *s) { (void)s; return 0; }
int ferror(void *s) { (void)s; return 0; }
void clearerr(void *s) { (void)s; }
int ungetc(int c, void *s) { (void)s; return c; }
int fgetc(void *s) { (void)s; return EOF; }
int getc(void *s) __asm__("getc") __attribute__((alias("getc_impl_alias")));
int getc_impl_alias(void *s) { return fgetc(s); }
int getchar(void) __asm__("getchar") __attribute__((alias("getchar_impl_alias")));
int getchar_impl_alias(void) { return fgetc((void*)stdin); }
int fputc(int c, void *s) { (void)s; return c; }
int putc_impl_alias(int c, void *s) { return fputc(c,s); }
int putc(int c, void *s) __asm__("putc") __attribute__((alias("putc_impl_alias")));
int putchar_impl_alias(int c) { return fputc(c, (void*)stdout); }
int putchar(int c) __asm__("putchar") __attribute__((alias("putchar_impl_alias")));
extern size_t fread(void *ptr, size_t size, size_t nmemb, void *stream);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, void *stream);
extern int fflush(void *stream);
size_t fread_unlocked(void *p, size_t s, size_t n, void *st) { (void)p; return fread(p,s,n,st); }
size_t fwrite_unlocked(const void *p, size_t s, size_t n, void *st) { (void)p; return fwrite(p,s,n,st); }
int fflush_unlocked(void *s) { return fflush(s); }
int feof_unlocked(void *s) { return feof(s); }
int ferror_unlocked(void *s) { return ferror(s); }
int fgetc_unlocked(void *s) { return fgetc(s); }
int fputc_unlocked(int c, void *s) { return fputc(c,s); }
void flockfile(void *s) { (void)s; }
void funlockfile(void *s) { (void)s; }
int ftrylockfile(void *s) { (void)s; return 0; }
void *popen(const char *c, const char *m) { (void)c;(void)m; return NULL; }
int pclose(void *s) { (void)s; return -1; }
/* simple growable buffer for open_memstream */
struct __efm_memstream {
    char **ptr;
    size_t *sizeloc;
    char *buf;
    size_t len;
    size_t cap;
};
static void __efm_mem_flush(struct __efm_memstream *m) {
    if (m->buf && m->ptr) {
        m->buf[m->len] = 0;
        *m->ptr = m->buf;
        *m->sizeloc = m->len;
    }
}
void *open_memstream(char **ptr, size_t *sizeloc) {
    if (!ptr || !sizeloc) return NULL;
    struct __efm_memstream *m = (struct __efm_memstream*)malloc(sizeof(*m));
    if (!m) return NULL;
    m->ptr = ptr; m->sizeloc = sizeloc;
    m->cap = 256; m->len = 0;
    m->buf = (char*)malloc(m->cap);
    if (!m->buf) { free(m); return NULL; }
    m->buf[0] = 0; *ptr = m->buf; *sizeloc = 0;
    return (void*)m;
}
static size_t __efm_mem_write(void *cookie, const char *data, size_t len) {
    struct __efm_memstream *m = (struct __efm_memstream*)cookie;
    if (m->len + len + 1 > m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 256;
        while (nc < m->len + len + 1) nc *= 2;
        char *nb = (char*)realloc(m->buf, nc);
        if (!nb) return 0;
        m->buf = nb; m->cap = nc;
    }
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    m->buf[m->len] = 0;
    if (m->ptr) { *m->ptr = m->buf; *m->sizeloc = m->len; }
    return len;
}
void *fmemopen(void *buf, size_t size, const char *mode) { (void)buf;(void)size;(void)mode; return NULL; }
void *open_wmemstream(void **ptr, size_t *sizeloc) { (void)ptr;(void)sizeloc; return NULL; }
/* dprintf -> write to fd (use our vsnprintf + stack buf + write()) */
extern int vsnprintf(char *, size_t, const char *, va_list);
extern ssize_t write(int fd, const void *, size_t);
ssize_t vdprintf(int fd, const char *fmt, va_list ap) {
    char stack[512];
    va_list ap2; __builtin_va_copy(ap2, ap);
    int n = vsnprintf(stack, sizeof(stack), fmt, ap);
    if (n < 0) { __builtin_va_end(ap2); return -1; }
    if ((size_t)n < sizeof(stack)) {
        __builtin_va_end(ap2);
        return write(fd, stack, (size_t)n);
    }
    char *big = (char*)malloc((size_t)n + 1);
    if (!big) { __builtin_va_end(ap2); return -1; }
    vsnprintf(big, (size_t)n + 1, fmt, ap2);
    __builtin_va_end(ap2);
    ssize_t w = write(fd, big, (size_t)n);
    free(big);
    return w;
}
int dprintf(int fd, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    ssize_t r = vdprintf(fd, fmt, ap);
    va_end(ap);
    return (int)r;
}

/* ---------- math.h 近似软实现 (Mesa util/u_math 引用) ---------- */
#include "efm_math_stubs.c"   /* 小函数放另一文件避免此处爆炸 */
