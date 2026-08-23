/* drm/amdgpu_drm.h — amdgpu DRM ioctl 占位 (Mesa amdgpu winsys 引用, swrast 不走此路径) */
#ifndef _AMDGPU_DRM_H_
#define _AMDGPU_DRM_H_
#include <stdint.h>

#define DRM_AMDGPU_GEM_CREATE    0x40
#define DRM_AMDGPU_GEM_MMAP      0x41
#define DRM_AMDGPU_CTX           0x42
#define DRM_AMDGPU_BO_LIST       0x43
#define DRM_AMDGPU_CS            0x44
#define DRM_AMDGPU_INFO          0x45
#define DRM_AMDGPU_GEM_METADATA  0x46
#define DRM_AMDGPU_GEM_WAIT_IDLE 0x47
#define DRM_AMDGPU_GEM_VA        0x48

struct drm_amdgpu_gem_create_in {
    uint64_t bo_size;
    uint64_t alignment;
    uint32_t domains;
    uint32_t domain_flags;
};
struct drm_amdgpu_gem_create_out {
    uint32_t handle;
    uint32_t _pad;
};

#define AMDGPU_GEM_DOMAIN_CPU    0x1
#define AMDGPU_GEM_DOMAIN_GTT    0x2
#define AMDGPU_GEM_DOMAIN_VRAM   0x4

#endif /* _AMDGPU_DRM_H_ */
