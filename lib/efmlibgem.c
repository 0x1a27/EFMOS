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
 * EFMOS GEM (Graphics Execution Manager) 内存管理兼容层 (libefmgem.a)
 *
 * 目标: 为 Mesa 的 Gallium3D winsys 提供 GEM 内存管理接口.
 *       在没有 Linux DRM 内核驱动的情况下, 用 malloc + mmap 模拟 GEM.
 *
 * GEM 是 Linux DRM 的"显存对象"抽象:
 *   - gem_create(size) → handle (全局唯一 32-bit)
 *   - gem_close(handle) → 释放
 *   - gem_open(name) → 跨进程打开 (EFMOS 单进程简化: 直接返回 handle)
 *   - gem_mmap(handle) → 返回 CPU 可访问的虚拟地址
 *   - gem_set_domain(handle, read_domains, write_domain) → 缓存一致性 (EFMOS 无 cache, no-op)
 *   - gem_execbuffer2 → 提交 GPU 命令缓冲 (swrast 不用, 返回 0)
 *
 * swrast (软件光栅化) 不需要真正的 GPU 执行, 只需要:
 *   1. 分配可读写的"显存"对象 → malloc + mmap
 *   2. CPU 访问这些对象 → 直接返回 malloc 地址
 *   3. 共享给 KMS 扫描输出 → 复用 dumb BO 机制
 *
 * 这套子集足够 Mesa 的 winsys/swrast/drisw 工作.
 * ======================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <xf86drm.h>
#include <drm/drm_mode.h>

/* ---------- GEM 对象表 (全局) ---------- */
#define EFM_GEM_MAX_OBJECTS   256

typedef struct {
    int       used;
    uint32_t  handle;      /* GEM 全局句柄 (>= 0x1000) */
    uint32_t  name;        /* flink 名称 (跨进程共享, EFMOS 单进程简化=handle) */
    size_t    size;        /* 对象字节大小 */
    void     *ptr;         /* CPU 虚拟地址 (malloc 返回) */
    int       mmap_count;  /* mmap 引用计数 */
    uint32_t  read_domains;
    uint32_t  write_domain;
    uint32_t  flags;       /* 创建标志 */
} efm_gem_object_t;

static efm_gem_object_t g_gem_objects[EFM_GEM_MAX_OBJECTS];
static uint32_t g_gem_next_handle = 0x2000;  /* 与 dumb BO (0x1000+) 区分 */

/* ---------- 内部辅助 ---------- */
static efm_gem_object_t *__gem_find(uint32_t handle) {
    for (int i = 0; i < EFM_GEM_MAX_OBJECTS; i++)
        if (g_gem_objects[i].used && g_gem_objects[i].handle == handle)
            return &g_gem_objects[i];
    return NULL;
}

static efm_gem_object_t *__gem_alloc(void) {
    for (int i = 0; i < EFM_GEM_MAX_OBJECTS; i++)
        if (!g_gem_objects[i].used) return &g_gem_objects[i];
    return NULL;
}

/* ---------- GEM Create / Close / Open ---------- */

/* 创建一个 GEM 对象 (对应 DRM_IOCTL_I915_GEM_CREATE / DRM_IOCTL_GEM_CREATE) */
int efm_gem_create(int fd, size_t size, uint32_t *out_handle) {
    (void)fd;
    if (!size || !out_handle) { errno = EINVAL; return -1; }
    efm_gem_object_t *o = __gem_alloc();
    if (!o) { errno = ENOMEM; return -1; }
    void *p = malloc(size);
    if (!p) { errno = ENOMEM; return -1; }
    memset(p, 0, size);
    memset(o, 0, sizeof(*o));
    o->used       = 1;
    o->handle     = g_gem_next_handle++;
    o->name       = o->handle;   /* flink name = handle (单进程) */
    o->size       = size;
    o->ptr        = p;
    o->mmap_count = 0;
    *out_handle   = o->handle;
    return 0;
}

/* 创建 (带初始大小 + 对齐) */
int efm_gem_create_aligned(int fd, size_t size, uint32_t alignment,
                            uint32_t *out_handle) {
    (void)alignment;
    return efm_gem_create(fd, size, out_handle);
}

/* 关闭 GEM 对象 */
int efm_gem_close(int fd, uint32_t handle) {
    (void)fd;
    efm_gem_object_t *o = __gem_find(handle);
    if (!o) { errno = EINVAL; return -1; }
    if (o->mmap_count > 0) {
        /* 理论上应 munmap, 但我们用 malloc 所以 munmap 是 no-op.
         * 保留 ptr 直到 mmap_count 归零后再释放更安全, 但简化直接释放. */
    }
    free(o->ptr);
    memset(o, 0, sizeof(*o));
    return 0;
}

/* flink: 把 handle 转成全局 name (跨进程共享) */
int efm_gem_flink(int fd, uint32_t handle, uint32_t *name) {
    (void)fd;
    efm_gem_object_t *o = __gem_find(handle);
    if (!o) { errno = EINVAL; return -1; }
    *name = o->name;
    return 0;
}

/* open: 通过 name 打开已存在的 GEM 对象 */
int efm_gem_open(int fd, uint32_t name, uint32_t *out_handle) {
    (void)fd;
    /* 单进程: name == handle, 直接返回 */
    efm_gem_object_t *o = __gem_find(name);
    if (!o) { errno = EINVAL; return -1; }
    *out_handle = o->handle;
    return 0;
}

/* ---------- GEM Mmap ---------- */

/* mmap 一个 GEM 对象 (返回 CPU 可访问虚拟地址) */
void *efm_gem_mmap(int fd, uint32_t handle, size_t size, int prot) {
    (void)fd; (void)prot;
    efm_gem_object_t *o = __gem_find(handle);
    if (!o) { errno = EINVAL; return MAP_FAILED; }
    if (size == 0 || size > o->size) size = o->size;
    o->mmap_count++;
    return o->ptr;   /* 直接返回 malloc 地址 (已零初始化) */
}

int efm_gem_munmap(int fd, void *ptr, size_t size) {
    (void)fd; (void)ptr; (void)size;
    /* malloc 的内存不能 munmap; 引用计数在 close 时统一释放 */
    return 0;
}

/* ---------- GEM Set Domain (缓存一致性, EFMOS 无 cache → no-op) ---------- */
int efm_gem_set_domain(int fd, uint32_t handle, uint32_t read_domains, uint32_t write_domain) {
    (void)fd;
    efm_gem_object_t *o = __gem_find(handle);
    if (!o) { errno = EINVAL; return -1; }
    o->read_domains = read_domains;
    o->write_domain = write_domain;
    return 0;
}

/* ---------- GEM ExecBuffer (swrast 不用, 占位) ---------- */
int efm_gem_execbuffer2(int fd, void *execbuffer, uint64_t flags) {
    (void)fd; (void)execbuffer; (void)flags;
    /* swrast 不提交 GPU 命令, 返回成功即可 */
    return 0;
}

/* ---------- GEM Wait (同步, swrast CPU 同步 → no-op) ---------- */
int efm_gem_wait(int fd, uint32_t handle, int64_t timeout_ns) {
    (void)fd; (void)handle; (void)timeout_ns;
    return 0;   /* CPU 渲染无异步, 不需等待 */
}

int efm_gem_busy(int fd, uint32_t handle, uint32_t *out_busy) {
    (void)fd; (void)handle;
    if (out_busy) *out_busy = 0;   /* 永远不忙 (CPU 同步) */
    return 0;
}

/* ---------- GEM Context (swrast 单上下文, 占位) ---------- */
int efm_gem_context_create(int fd, uint32_t *out_ctx_id) {
    (void)fd;
    static uint32_t next_ctx = 1;
    *out_ctx_id = next_ctx++;
    return 0;
}
int efm_gem_context_destroy(int fd, uint32_t ctx_id) {
    (void)fd; (void)ctx_id;
    return 0;
}

/* ---------- Mesa winsys 常用的 ioctl 包装 ---------- */

/* 通用 GEM ioctl 分发 (Mesa i915/amdgpu winsys 通过 drmIoctl 调用) */
int efm_gem_ioctl(int fd, unsigned long request, void *arg) {
    (void)fd;
    /* 根据 request 低 8 位 (DRM command number) 分发:
     *   0x00~0x0F: 基础 DRM (version, cap, ...)
     *   0x40~0x4F: i915 GEM (create, mmap, exec, ...)
     *   0xC0~0xCF: amdgpu (ctx, bo, ...)
     * swrast 不会真正走到这里, 但提供兼容返回避免 Mesa panic */
    unsigned int cmd = (unsigned int)(request & 0xFF);
    switch (cmd) {
        case 0x40: { /* DRM_I915_GEM_CREATE */
            struct { uint64_t size; uint32_t handle; uint32_t pad; } *c = arg;
            return efm_gem_create(fd, (size_t)c->size, &c->handle);
        }
        case 0x41: { /* DRM_I915_GEM_MMAP */
            struct { uint32_t handle; uint32_t pad; uint64_t offset; uint64_t size; uint64_t addr_ptr; } *m = arg;
            void *p = efm_gem_mmap(fd, m->handle, (size_t)m->size, 3);
            if (p == MAP_FAILED) return -1;
            m->addr_ptr = (uint64_t)(uintptr_t)p;
            return 0;
        }
        case 0x43: { /* DRM_I915_GEM_SET_DOMAIN */
            struct { uint32_t handle; uint32_t read_domains; uint32_t write_domain; } *d = arg;
            return efm_gem_set_domain(fd, d->handle, d->read_domains, d->write_domain);
        }
        case 0x46: { /* DRM_I915_GEM_BUSY */
            struct { uint32_t handle; uint32_t busy; } *b = arg;
            return efm_gem_busy(fd, b->handle, &b->busy);
        }
        case 0x4E: { /* DRM_I915_GEM_CONTEXT_CREATE */
            struct { uint32_t ctx_id; } *c = arg;
            return efm_gem_context_create(fd, &c->ctx_id);
        }
        case 0x48: { /* DRM_IOCTL_GEM_CLOSE */
            struct drm_gem_close *gc = arg;
            return efm_gem_close(fd, gc->handle);
        }
        default:
            errno = ENOSYS;
            return -1;
    }
}

/* ---------- GEM 信息查询 (调试用) ---------- */
int efm_gem_get_info(uint32_t handle, size_t *out_size, void **out_ptr) {
    efm_gem_object_t *o = __gem_find(handle);
    if (!o) { errno = EINVAL; return -1; }
    if (out_size) *out_size = o->size;
    if (out_ptr)  *out_ptr  = o->ptr;
    return 0;
}

int efm_gem_count_objects(void) {
    int n = 0;
    for (int i = 0; i < EFM_GEM_MAX_OBJECTS; i++)
        if (g_gem_objects[i].used) n++;
    return n;
}

/* ---------- EFMOS 扩展: 把 GEM 对象内容 blit 到屏幕 ---------- */
/* 类似 efm_gbm_blit_to_screen, 但针对 GEM 对象 (用于 Mesa winsys 直接渲染路径) */
void efm_gem_blit_to_screen(uint32_t handle, uint32_t width, uint32_t height, uint32_t pitch) {
    efm_gem_object_t *o = __gem_find(handle);
    if (!o || !o->ptr) return;
    /* 拿到内核 API 的 fb_base */
    extern void efm_get_fb_info(unsigned int*, unsigned int*, unsigned int*, unsigned int**);
    unsigned int hr=0, vr=0, ppsl=0;
    unsigned int *fb_base = NULL;
    efm_get_fb_info(&hr, &vr, &ppsl, &fb_base);
    if (!fb_base || !hr) return;
    uint32_t *src = (uint32_t*)o->ptr;
    uint32_t copy_w = width  < hr ? width  : hr;
    uint32_t copy_h = height < vr ? height : vr;
    uint32_t pitch_px = pitch / 4;
    for (uint32_t y = 0; y < copy_h; y++) {
        uint32_t *dst = fb_base + (uint64_t)y * ppsl;
        uint32_t *s   = src + (uint64_t)y * pitch_px;
        for (uint32_t x = 0; x < copy_w; x++) dst[x] = s[x];
    }
}
