/* drm/i915_drm.h — i915 DRM ioctl 占位 (Mesa i915 winsys 引用, swrast 不走此路径) */
#ifndef _I915_DRM_H_
#define _I915_DRM_H_
#include <stdint.h>

/* i915 GEM ioctl 号 (与 Linux 一致, 但 EFMOS 走 efm_gem_ioctl 分发) */
#define DRM_I915_GEM_CREATE          0x40
#define DRM_I915_GEM_MMAP            0x41
#define DRM_I915_GEM_PREAD           0x42
#define DRM_I915_GEM_PWRITE          0x43
#define DRM_I915_GEM_SET_DOMAIN      0x44
#define DRM_I915_GEM_SET_TILING      0x45
#define DRM_I915_GEM_GET_TILING      0x46
#define DRM_I915_GEM_BUSY            0x47
#define DRM_I915_GEM_THROTTLE        0x48
#define DRM_I915_GEM_CONTEXT_CREATE  0x4E
#define DRM_I915_GEM_CONTEXT_DESTROY 0x4F
#define DRM_I915_GEM_EXECBUFFER2     0x4A

/* 结构体占位 (字段与 Linux 一致, 让 Mesa 编译通过) */
struct drm_i915_gem_create {
    uint64_t size;
    uint32_t handle;
    uint32_t pad;
};
struct drm_i915_gem_mmap {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
    uint64_t size;
    uint64_t addr_ptr;
};
struct drm_i915_gem_set_domain {
    uint32_t handle;
    uint32_t read_domains;
    uint32_t write_domain;
};
struct drm_i915_gem_busy {
    uint32_t handle;
    uint32_t busy;
};
struct drm_i915_gem_context_create {
    uint32_t ctx_id;
    uint32_t pad;
};

#define I915_GEM_DOMAIN_CPU   0x00000001
#define I915_GEM_DOMAIN_GTT   0x00000002
#define I915_GEM_DOMAIN_RENDER 0x00000004

#endif /* _I915_DRM_H_ */
