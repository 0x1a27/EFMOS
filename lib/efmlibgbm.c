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
 * EFMOS GBM (Generic Buffer Management) 兼容层 (libefmgbm.a)
 *
 * 目标: 为 Mesa 的 GBM EGL 平台提供最小 buffer 对象管理.
 *       完全绕过 X11/Wayland — GBM 直接基于 libdrm dumb BO + EFMOS GOP fb.
 *
 * 核心思路:
 *   - gbm_device   绑定一个 DRM fd (来自 libefmlibdrm.a 的 drmOpen)
 *   - gbm_bo       = 一个 dumb BO (DRM_IOCTL_MODE_CREATE_DUMB)
 *                   + 可 mmap 的 CPU 可写虚拟地址 (供 swrast 写像素)
 *   - gbm_surface  = 双缓冲 BO 队列 (front/back), SwapBuffers 时交换
 *   - gbm_bo_map   → mmap dumb BO (drmIoctl MAP_DUMB + efmdrm_mmap)
 *   - gbm_bo_unmap → munmap
 *
 * Mesa 的 GBM 平台调用链:
 *   gbm_create_device(fd)           → 返回 device
 *   gbm_bo_create(dev,w,h,fmt, ...) → 返回 bo
 *   gbm_bo_map(bo)                  → 返回 CPU 可写地址, swrast 画到这里
 *   gbm_surface_create / lock_front_buffer → 用于 SwapBuffers
 *   gbm_bo_get_handle(bo)           → 传给 drmModeAddFB2 扫描输出
 *
 * 这套子集足够 Mesa 的 loader/platform_gbm.c + state_tracker/drm 工作.
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
#include <drm/drm_fourcc.h>

/* GBM 公开类型 (Mesa 头 gbm.h 中定义; 这里是内部实现) */
#define GBM_BO_USE_SCANOUT     (1 << 0)
#define GBM_BO_USE_CURSOR      (1 << 1)
#define GBM_BO_USE_CURSOR_64X64 (1 << 2)
#define GBM_BO_USE_RENDERING   (1 << 3)
#define GBM_BO_USE_LINEAR      (1 << 4)
#define GBM_BO_USE_PROTECTED   (1 << 5)
#define GBM_BO_USE_WRITE       (1 << 6)

#define GBM_BO_TRANSFER_READ       (1 << 0)
#define GBM_BO_TRANSFER_WRITE      (1 << 1)
#define GBM_BO_TRANSFER_READ_WRITE (GBM_BO_TRANSFER_READ | GBM_BO_TRANSFER_WRITE)

enum gbm_bo_format {
    GBM_BO_FORMAT_XRGB8888 = 0,
    GBM_BO_FORMAT_ARGB8888 = 1,
};

struct gbm_device {
    int fd;                  /* DRM fd */
    uint32_t width;          /* 屏幕宽 (scanout 能力) */
    uint32_t height;
    uint32_t format;         /* 默认 fourcc, 通常 DRM_FORMAT_XRGB8888 */
};

struct gbm_bo {
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t handle;         /* GEM dumb BO handle */
    uint32_t fb_id;          /* drmModeAddFB2 后的 framebuffer id */
    uint32_t format;         /* fourcc */
    uint64_t modifier;       /* DRM_FORMAT_MOD_LINEAR */
    uint32_t flags;
    void *map_ptr;           /* mmap 得到的 CPU 地址 */
    size_t map_size;
    int     map_count;       /* 引用计数 */
    void *user_data;         /* Mesa state tracker 附带的私有数据 */
    int     map_ptr_direct;  /* [优化] 1=map_ptr 直接指向 Graphics.drv back buffer,
                              *      swrast 已直接写入 back buffer, swap 时跳过 blit.
                              *      0=独立 BO 内存, swap 时需要拷贝. */
};

struct gbm_surface {
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    struct gbm_bo *bos[4];   /* 最多 4 个 BO 的交换链 (简化: 只用 2) */
    int count;               /* 当前 BO 数量 */
    int current;             /* 当前 back buffer 索引 */
    struct gbm_bo *front;    /* 已 lock 的 front buffer */
};

struct gbm_format_desc {
    uint32_t fourcc;
    int      bpp;
    int      depth;
};

static const struct gbm_format_desc g_formats[] = {
    { DRM_FORMAT_XRGB8888, 32, 24 },
    { DRM_FORMAT_ARGB8888, 32, 32 },
    { DRM_FORMAT_RGB565,   16, 16 },
    { DRM_FORMAT_RGB888,   24, 24 },
    { DRM_FORMAT_BGR888,   24, 24 },
    { DRM_FORMAT_XBGR8888, 32, 24 },
    { DRM_FORMAT_ABGR8888, 32, 32 },
    { 0, 0, 0 }
};

static int __fmt_to_bpp(uint32_t format) {
    for (int i = 0; g_formats[i].fourcc; i++)
        if (g_formats[i].fourcc == format) return g_formats[i].bpp;
    return 32;  /* 默认 */
}

/* ---------- gbm_device ---------- */
struct gbm_device *gbm_create_device(int fd) {
    struct gbm_device *dev = (struct gbm_device*)calloc(1, sizeof(*dev));
    if (!dev) return NULL;
    dev->fd = fd;
    /* 从 DRM 获取屏幕分辨率 (用 drmModeGetResources) */
    drmModeResPtr res = drmModeGetResources(fd);
    if (res) {
        dev->width  = res->width  ? res->width  : 1024;
        dev->height = res->height ? res->height : 768;
        drmModeFreeResources(res);
    } else {
        dev->width = 1024; dev->height = 768;
    }
    dev->format = DRM_FORMAT_XRGB8888;
    return dev;
}

void gbm_device_destroy(struct gbm_device *gbm) {
    if (!gbm) return;
    /* 不关闭 fd, 由调用者负责 (Mesa loader 管 fd 生命周期) */
    free(gbm);
}

int gbm_device_get_fd(struct gbm_device *gbm) {
    return gbm ? gbm->fd : -1;
}

int gbm_device_is_format_supported(struct gbm_device *gbm,
                                   uint32_t format, uint32_t usage) {
    (void)usage;
    if (!gbm) return 0;
    /* swrast 软件后端: 支持所有常见 32/16-bit 格式 */
    int bpp = __fmt_to_bpp(format);
    return (bpp == 32 || bpp == 24 || bpp == 16) ? 1 : 0;
}

int gbm_device_get_format_modifier_plane_count(struct gbm_device *gbm,
                                                uint32_t format,
                                                uint64_t modifier) {
    (void)gbm; (void)format;
    if (modifier == DRM_FORMAT_MOD_LINEAR) return 1;
    return 0;
}

/* ---------- gbm_bo ---------- */
struct gbm_bo *gbm_bo_create(struct gbm_device *gbm,
                             uint32_t width, uint32_t height,
                             uint32_t format, uint32_t flags) {
    if (!gbm || !width || !height) return NULL;
    int bpp = __fmt_to_bpp(format);
    if (!bpp) return NULL;

    struct gbm_bo *bo = (struct gbm_bo*)calloc(1, sizeof(*bo));
    if (!bo) return NULL;
    bo->gbm     = gbm;
    bo->width   = width;
    bo->height  = height;
    bo->bpp     = (uint32_t)bpp;
    bo->format  = format;
    bo->modifier = DRM_FORMAT_MOD_LINEAR;
    bo->flags   = flags;

    /* 通过 DRM dumb BO 分配显存 */
    struct drm_mode_create_dumb create;
    memset(&create, 0, sizeof(create));
    create.width  = width;
    create.height = height;
    create.bpp    = (uint32_t)bpp;
    if (drmIoctl(gbm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        free(bo);
        return NULL;
    }
    bo->handle = create.handle;
    bo->pitch  = create.pitch;
    bo->map_size = (size_t)create.size;

    /* 注册成 KMS framebuffer (扫描输出用) */
    if (flags & GBM_BO_USE_SCANOUT) {
        uint32_t handles[4] = { bo->handle, 0, 0, 0 };
        uint32_t pitches[4] = { bo->pitch, 0, 0, 0 };
        uint32_t offsets[4] = { 0, 0, 0, 0 };
        uint64_t mods[4]    = { DRM_FORMAT_MOD_LINEAR, 0, 0, 0 };
        if (drmModeAddFB2WithModifiers(gbm->fd, width, height, format,
                                       handles, pitches, offsets, mods,
                                       &bo->fb_id, 0) != 0) {
            /* 回退到 AddFB (不支持 modifier 时) */
            drmModeAddFB(gbm->fd, width, height,
                         (bpp == 32) ? 24 : (uint8_t)bpp, (uint8_t)bpp,
                         bo->pitch, bo->handle, &bo->fb_id);
        }
    }
    return bo;
}

struct gbm_bo *gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                                             uint32_t width, uint32_t height,
                                             uint32_t format,
                                             const uint64_t *modifiers,
                                             const unsigned int count) {
    (void)modifiers; (void)count;
    /* 简化: 忽略 modifier 选择, 退回到线性 */
    return gbm_bo_create(gbm, width, height, format, GBM_BO_USE_RENDERING);
}

struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *gbm,
                                              uint32_t width, uint32_t height,
                                              uint32_t format,
                                              uint32_t flags,
                                              const uint64_t *modifiers,
                                              const unsigned int count) {
    (void)modifiers; (void)count;
    return gbm_bo_create(gbm, width, height, format, flags);
}

void gbm_bo_destroy(struct gbm_bo *bo) {
    if (!bo) return;
    /* 直接映射模式的 back buffer 由 Graphics.drv 管理, 不能 munmap */
    if (bo->map_ptr && bo->map_count > 0 && !bo->map_ptr_direct) {
        munmap(bo->map_ptr, bo->map_size);
    }
    if (bo->fb_id) {
        drmModeRmFB(bo->gbm->fd, bo->fb_id);
    }
    struct drm_mode_destroy_dumb d;
    memset(&d, 0, sizeof(d));
    d.handle = bo->handle;
    drmIoctl(bo->gbm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    free(bo);
}

uint32_t gbm_bo_get_width(struct gbm_bo *bo)      { return bo ? bo->width : 0; }
uint32_t gbm_bo_get_height(struct gbm_bo *bo)     { return bo ? bo->height : 0; }
uint32_t gbm_bo_get_stride(struct gbm_bo *bo)     { return bo ? bo->pitch : 0; }
uint32_t gbm_bo_get_stride_or_tiled(struct gbm_bo *bo) { return bo ? bo->pitch : 0; }
uint32_t gbm_bo_get_format(struct gbm_bo *bo)     { return bo ? bo->format : 0; }
uint64_t gbm_bo_get_modifier(struct gbm_bo *bo)   { return bo ? bo->modifier : DRM_FORMAT_MOD_INVALID; }
uint32_t gbm_bo_get_bpp(struct gbm_bo *bo)        { return bo ? bo->bpp : 0; }
int      gbm_bo_get_plane_count(struct gbm_bo *bo){ return bo ? 1 : 0; }
uint32_t gbm_bo_get_handle(struct gbm_bo *bo)     { return bo ? bo->handle : 0; }
uint32_t gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane) {
    (void)plane; return bo ? bo->handle : 0;
}
int      gbm_bo_get_fd(struct gbm_bo *bo) {
    if (!bo) return -1;
    int fd = -1;
    drmPrimeHandleToFD(bo->gbm->fd, bo->handle, 0, &fd);
    return fd;
}
uint32_t gbm_bo_get_fb_id(struct gbm_bo *bo)      { return bo ? bo->fb_id : 0; }

void gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                          void (*destroy)(struct gbm_bo *, void *)) {
    if (!bo) return;
    (void)destroy;  /* 简化: 不真正注册 destroy 回调 */
    bo->user_data = data;
}
void *gbm_bo_get_user_data(struct gbm_bo *bo) {
    return bo ? bo->user_data : NULL;
}

/* [优化] 查询 BO 是否直接映射到 back buffer (swrast 已直接写入, swap 时无需 blit) */
int gbm_bo_is_direct_mapped(struct gbm_bo *bo) {
    return bo ? bo->map_ptr_direct : 0;
}

void *gbm_bo_map(struct gbm_bo *bo,
                 uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                 uint32_t flags, uint32_t *stride, void **map_data) {
    (void)x; (void)y; (void)width; (void)height; (void)flags;
    if (!bo) return NULL;
    if (!bo->map_ptr) {
        /* [优化] 优先: 直接映射到 Graphics.drv 的 back buffer.
         * 让 Mesa swrast 直接渲染到 back buffer, 消除 BO→back buffer 的
         * 全屏拷贝 (节省 8MB/帧 @1080p). 只在 BO 尺寸与屏幕匹配时启用. */
        extern int efm_get_backbuffer(void **out_ptr, int *out_pitch,
                                      int *out_w, int *out_h);
        void *bb_ptr = NULL;
        int bb_pitch = 0, bb_w = 0, bb_h = 0;
        if (efm_get_backbuffer(&bb_ptr, &bb_pitch, &bb_w, &bb_h) == 0 && bb_ptr) {
            /* 尺寸匹配 (BO 与屏幕同尺寸) 才直接映射, 避免越界写 */
            if ((int)bo->width == bb_w && (int)bo->height == bb_h) {
                bo->map_ptr        = bb_ptr;
                bo->pitch          = (uint32_t)bb_pitch;
                bo->map_size       = (size_t)bb_pitch * (size_t)bb_h;
                bo->map_ptr_direct = 1;
                bo->map_count++;
                if (stride) *stride = bo->pitch;
                if (map_data) *map_data = bo;
                return bo->map_ptr;
            }
        }
        /* 回退: 分配独立 BO 内存 (原路径) */
        struct drm_mode_map_dumb m;
        memset(&m, 0, sizeof(m));
        m.handle = bo->handle;
        if (drmIoctl(bo->gbm->fd, DRM_IOCTL_MODE_MAP_DUMB, &m) != 0) return NULL;
        /* 通过 efmdrm_mmap 映射 (会识别 DRM fd + offset → 返回 BO 虚拟地址) */
        extern void *efmdrm_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
        bo->map_ptr = efmdrm_mmap(NULL, bo->map_size, PROT_READ|PROT_WRITE,
                                  MAP_SHARED, bo->gbm->fd, (long)m.offset);
        if (bo->map_ptr == MAP_FAILED) { bo->map_ptr = NULL; return NULL; }
        bo->map_ptr_direct = 0;
    }
    bo->map_count++;
    if (stride) *stride = bo->pitch;
    if (map_data) *map_data = bo;
    return bo->map_ptr;
}

void gbm_bo_unmap(struct gbm_bo *bo, void *map_data) {
    (void)map_data;
    if (!bo || bo->map_count <= 0) return;
    bo->map_count--;
    /* 真正 munmap 在 destroy 时做, 这里只减引用计数 */
}

int gbm_bo_write(struct gbm_bo *bo, const void *buf, size_t count) {
    if (!bo || !bo->map_ptr) {
        void *m = gbm_bo_map(bo, 0, 0, bo->width, bo->height,
                             GBM_BO_TRANSFER_WRITE, NULL, NULL);
        if (!m) return -1;
        size_t n = count < bo->map_size ? count : bo->map_size;
        memcpy(m, buf, n);
        gbm_bo_unmap(bo, NULL);
        return 0;
    }
    size_t n = count < bo->map_size ? count : bo->map_size;
    memcpy(bo->map_ptr, buf, n);
    return 0;
}

/* ---------- gbm_surface ---------- */
struct gbm_surface *gbm_surface_create(struct gbm_device *gbm,
                                       uint32_t width, uint32_t height,
                                       uint32_t format, uint32_t flags) {
    if (!gbm) return NULL;
    struct gbm_surface *s = (struct gbm_surface*)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->gbm    = gbm;
    s->width  = width;
    s->height = height;
    s->format = format;
    s->flags  = flags;
    s->count  = 0;
    s->current = 0;
    s->front  = NULL;
    return s;
}

struct gbm_surface *gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                                       uint32_t width, uint32_t height,
                                                       uint32_t format,
                                                       const uint64_t *modifiers,
                                                       const unsigned int count) {
    (void)modifiers; (void)count;
    return gbm_surface_create(gbm, width, height, format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
}

void gbm_surface_destroy(struct gbm_surface *surf) {
    if (!surf) return;
    for (int i = 0; i < surf->count; i++) {
        if (surf->bos[i]) gbm_bo_destroy(surf->bos[i]);
    }
    free(surf);
}

struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surf) {
    if (!surf || surf->count == 0) return NULL;
    /* 返回当前 front (已 SwapBuffers 后的那一个) */
    int idx = (surf->current + surf->count - 1) % surf->count;
    surf->front = surf->bos[idx];
    return surf->front;
}

void gbm_surface_release_buffer(struct gbm_surface *surf, struct gbm_bo *bo) {
    (void)surf; (void)bo;
    /* 简化: BO 生命周期跟 surface 走, 不真正释放 */
}

int gbm_surface_has_free_buffers(struct gbm_surface *surf) {
    if (!surf) return 0;
    return surf->count < 4 ? 1 : 0;
}

/* 内部: 为 surface 分配下一个 back buffer BO */
struct gbm_bo *gbm_surface_get_back_bo(struct gbm_surface *surf) {
    if (!surf) return NULL;
    if (surf->count < 2) {
        /* 首次或第二次: 创建新 BO */
        struct gbm_bo *bo = gbm_bo_create(surf->gbm, surf->width, surf->height,
                                          surf->format,
                                          surf->flags | GBM_BO_USE_SCANOUT);
        if (!bo) return NULL;
        surf->bos[surf->count++] = bo;
        return bo;
    }
    /* 已有 2 个 BO: 轮转 */
    surf->current = (surf->current + 1) % surf->count;
    return surf->bos[surf->current];
}

/* Mesa loader 内部用 (非公开 API, 但 Mesa EGL platform_gbm.c 引用) */
struct gbm_bo *gbm_surface_get_current_front_bo(struct gbm_surface *surf) {
    if (!surf || surf->count == 0) return NULL;
    int idx = (surf->current + surf->count - 1) % surf->count;
    return surf->bos[idx];
}

/* ---------- GBM format 工具 (Mesa state tracker 引用) ---------- */
int gbm_format_get_name(uint32_t gbm_format, char *out_name, size_t name_size) {
    if (name_size < 5) return -1;
    out_name[0] = (gbm_format      ) & 0xFF;
    out_name[1] = (gbm_format >> 8 ) & 0xFF;
    out_name[2] = (gbm_format >> 16) & 0xFF;
    out_name[3] = (gbm_format >> 24) & 0xFF;
    out_name[4] = 0;
    return 0;
}

uint32_t gbm_bo_get_bpp_for_format(uint32_t gbm_format) {
    return (uint32_t)__fmt_to_bpp(gbm_format);
}

/* ---------- gbm_bo_get_offset_for_plane ---------- */
uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane) {
    (void)bo; (void)plane;
    return 0;   /* 线性格式单平面, offset=0 */
}

/* ---------- gbm_surface_set_orientation (某些 Android fork 引用, 占位) ---------- */
void gbm_surface_set_orientation(struct gbm_surface *surf, int orientation) {
    (void)surf; (void)orientation;
}

/* ---------- EFMOS 扩展: 把 BO 内容 blit 到当前进程窗口 (Mesa 集成核心) ---------- */
/* 这是 Mesa swrast → EFMOS 渲染管线的"最后一公里":
 *   swrast 渲染完一帧到 gbm_bo 的 mmap 区域 → eglSwapBuffers → 本函数
 *   → 调用 efm_blit_to_window 把 BO 像素写入当前进程窗口的内容区
 *   → kernel 内部通过 Graphics.drv 的 blit_buffer 写入 back buffer + 脏矩形
 *   → 下一次 flush 把后缓冲扫描输出到屏幕
 *
 * 关键改进 (vs 旧版直接写 front buffer):
 *   1. WM 感知: 自动重定向到当前进程窗口的内容区, 与其他窗口共存
 *   2. 双缓冲一致: 写 back buffer 而非 front, 不会与 Graphics.drv flush 冲突
 *   3. 脏矩形优化: 只标记被写区域, 下一次 flush 只搬脏矩形 */
extern int efm_blit_to_window(const void *src, int src_w, int src_h, int src_pitch);
extern void efm_get_fb_info(unsigned int*, unsigned int*, unsigned int*, unsigned int**);
void efm_gbm_blit_to_screen(struct gbm_bo *bo) {
    if (!bo || !bo->map_ptr) return;

    /* 优先使用新的 blit_to_window API (WM 感知 + 写 back buffer) */
    if (efm_blit_to_window(bo->map_ptr, (int)bo->width, (int)bo->height, (int)bo->pitch) == 0)
        return;

    /* 回退: 旧路径 (无 blit_to_window 的老内核) — 直接写 front buffer */
    uint32_t *src = (uint32_t*)bo->map_ptr;
    uint32_t pitch_px = bo->pitch / 4;
    unsigned int hr=0, vr=0, ppsl=0;
    unsigned int *fb_base = NULL;
    efm_get_fb_info(&hr, &vr, &ppsl, &fb_base);
    if (fb_base && hr) {
        uint32_t copy_w = bo->width  < hr ? bo->width  : hr;
        uint32_t copy_h = bo->height < vr ? bo->height : vr;
        for (uint32_t y = 0; y < copy_h; y++) {
            uint32_t *dst = fb_base + (uint64_t)y * ppsl;
            uint32_t *s   = src + (uint64_t)y * pitch_px;
            for (uint32_t x = 0; x < copy_w; x++) dst[x] = s[x];
        }
    }
}
