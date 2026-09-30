/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Mirrors the libgbm (MIT license) public API.
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

/* gbm.h — Mesa GBM (Generic Buffer Management) 公共头 (最小子集) */
#ifndef _GBM_H_
#define _GBM_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 枚举 / 宏 ---------- */
#define GBM_BO_USE_SCANOUT       (1 << 0)
#define GBM_BO_USE_CURSOR        (1 << 1)
#define GBM_BO_USE_CURSOR_64X64  (1 << 2)
#define GBM_BO_USE_RENDERING     (1 << 3)
#define GBM_BO_USE_LINEAR        (1 << 4)
#define GBM_BO_USE_PROTECTED     (1 << 5)
#define GBM_BO_USE_WRITE         (1 << 6)

#define GBM_BO_TRANSFER_READ       (1 << 0)
#define GBM_BO_TRANSFER_WRITE      (1 << 1)
#define GBM_BO_TRANSFER_READ_WRITE (GBM_BO_TRANSFER_READ | GBM_BO_TRANSFER_WRITE)

enum gbm_bo_format {
    GBM_BO_FORMAT_XRGB8888 = 0,
    GBM_BO_FORMAT_ARGB8888 = 1,
};

/* ---------- 不透明类型 ---------- */
struct gbm_device;
struct gbm_bo;
struct gbm_surface;
struct gbm_dri_surface;   /* Mesa DRI 内部用 */

/* ---------- 内部类型 (EFMOS 扩展: 字段对 GBM/EGL 实现可见) ---------- */
struct gbm_device {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t format;
};

struct gbm_bo {
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t handle;
    uint32_t fb_id;
    uint32_t format;
    uint64_t modifier;
    uint32_t flags;
    void *map_ptr;
    size_t map_size;
    int     map_count;
    void *user_data;
    int     map_ptr_direct;  /* 1=map_ptr 直接指向 back buffer (无需 blit) */
};

struct gbm_surface {
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    struct gbm_bo *bos[4];
    int count;
    int current;
    struct gbm_bo *front;
};

/* ---------- gbm_device ---------- */
struct gbm_device *gbm_create_device(int fd);
void gbm_device_destroy(struct gbm_device *gbm);
int  gbm_device_get_fd(struct gbm_device *gbm);
int  gbm_device_is_format_supported(struct gbm_device *gbm, uint32_t format, uint32_t usage);
int  gbm_device_get_format_modifier_plane_count(struct gbm_device *gbm, uint32_t format, uint64_t modifier);

/* ---------- gbm_bo ---------- */
struct gbm_bo *gbm_bo_create(struct gbm_device *gbm,
                             uint32_t width, uint32_t height,
                             uint32_t format, uint32_t flags);
struct gbm_bo *gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                                             uint32_t width, uint32_t height,
                                             uint32_t format,
                                             const uint64_t *modifiers,
                                             const unsigned int count);
struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *gbm,
                                              uint32_t width, uint32_t height,
                                              uint32_t format,
                                              uint32_t flags,
                                              const uint64_t *modifiers,
                                              const unsigned int count);
void gbm_bo_destroy(struct gbm_bo *bo);

uint32_t gbm_bo_get_width(struct gbm_bo *bo);
uint32_t gbm_bo_get_height(struct gbm_bo *bo);
uint32_t gbm_bo_get_stride(struct gbm_bo *bo);
uint32_t gbm_bo_get_stride_or_tiled(struct gbm_bo *bo);
uint32_t gbm_bo_get_format(struct gbm_bo *bo);
uint64_t gbm_bo_get_modifier(struct gbm_bo *bo);
uint32_t gbm_bo_get_bpp(struct gbm_bo *bo);
int      gbm_bo_get_plane_count(struct gbm_bo *bo);
uint32_t gbm_bo_get_handle(struct gbm_bo *bo);
uint32_t gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane);
int      gbm_bo_get_fd(struct gbm_bo *bo);
uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane);

void  gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                           void (*destroy)(struct gbm_bo *, void *));
void *gbm_bo_get_user_data(struct gbm_bo *bo);

void *gbm_bo_map(struct gbm_bo *bo,
                 uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                 uint32_t flags, uint32_t *stride, void **map_data);
void  gbm_bo_unmap(struct gbm_bo *bo, void *map_data);
int   gbm_bo_write(struct gbm_bo *bo, const void *buf, size_t count);

/* ---------- gbm_surface ---------- */
struct gbm_surface *gbm_surface_create(struct gbm_device *gbm,
                                       uint32_t width, uint32_t height,
                                       uint32_t format, uint32_t flags);
struct gbm_surface *gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                                       uint32_t width, uint32_t height,
                                                       uint32_t format,
                                                       const uint64_t *modifiers,
                                                       const unsigned int count);
void gbm_surface_destroy(struct gbm_surface *surf);
struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surf);
void gbm_surface_release_buffer(struct gbm_surface *surf, struct gbm_bo *bo);
int  gbm_surface_has_free_buffers(struct gbm_surface *surf);

/* Mesa 内部扩展 */
struct gbm_bo *gbm_surface_get_back_bo(struct gbm_surface *surf);
struct gbm_bo *gbm_surface_get_current_front_bo(struct gbm_surface *surf);

/* EFMOS 扩展 */
void efm_gbm_blit_to_screen(struct gbm_bo *bo);
int  gbm_bo_is_direct_mapped(struct gbm_bo *bo);  /* 1=map_ptr 直接指向 back buffer */

/* 工具 */
int gbm_format_get_name(uint32_t gbm_format, char *out_name, size_t name_size);
uint32_t gbm_bo_get_bpp_for_format(uint32_t gbm_format);

#ifdef __cplusplus
}
#endif
#endif /* _GBM_H_ */
