/* drm/drm_mode.h — EFMOS 内核 uapi DRM mode 结构体 (供 efmlib 独立构建使用)
 *
 * 此文件 ONLY 包含内核 uapi 结构体和宏 (与 Linux drm-uapi/drm_mode.h 对应).
 * libdrm API 类型 (drmMode*) 已移至 xf86drm.h, 避免与 Mesa 的 drm-uapi 头冲突.
 *
 * 使用场景:
 *  - efmlib*.c 独立构建 (Makefile, -nostdinc -Ilib/include): 被 xf86drm.h 通过
 *    __has_include 回退路径包含, 或被 efmlib 文件直接 #include.
 *  - Mesa 构建: 不会被包含 (xf86drm.h 优先使用 Mesa 自带的 drm-uapi/ 头).
 */
#ifndef DRM_MODE_H
#define DRM_MODE_H
#include <stdint.h>

/* ======================================================================
 * 来自 Linux 内核 drm-uapi/drm.h 的结构体 (EFMOS 本地定义, 不依赖 linux/types.h)
 * ====================================================================== */

#ifndef __u32
#define __u32 uint32_t
#endif
#ifndef __u64
#define __u64 uint64_t
#endif
#ifndef __u16
#define __u16 uint16_t
#endif
#ifndef __u8
#define __u8 uint8_t
#endif
#ifndef __kernel_size_t
#define __kernel_size_t size_t
#endif

/* DRM_IOCTL_VERSION ioctl argument type */
struct drm_version {
    int      version_major;
    int      version_minor;
    int      version_patchlevel;
    __kernel_size_t name_len;
    char    *name;
    __kernel_size_t date_len;
    char    *date;
    __kernel_size_t desc_len;
    char    *desc;
};

/* DRM_IOCTL_GET_CAP */
struct drm_get_cap {
    __u64 capability;
    __u64 value;
};

/* GEM close */
struct drm_gem_close {
    __u32 handle;
    __u32 pad;
};

/* PRIME handle/fd */
struct drm_prime_handle {
    __u32 handle;
    __u32 flags;
    int   fd;
};

/* ======================================================================
 * 来自 Linux 内核 drm-uapi/drm_mode.h 的结构体
 * ====================================================================== */

/* Dumb Buffer ioctl */
struct drm_mode_create_dumb {
    __u32 height;
    __u32 width;
    __u32 bpp;
    __u32 flags;
    __u32 handle;
    __u32 pitch;
    __u64 size;
};

struct drm_mode_map_dumb {
    __u32 handle;
    __u32 pad;
    __u64 offset;
};

struct drm_mode_destroy_dumb {
    __u32 handle;
};

/* ADDFB2 */
struct drm_mode_fb_cmd2 {
    __u32 fb_id;
    __u32 width, height;
    __u32 pixel_format;
    __u32 flags;
    __u32 handles[4];
    __u32 pitches[4];
    __u32 offsets[4];
    __u64 modifier[4];
};

/* SET CRTC */
struct drm_mode_crtc {
    __u32 set_connectors_ptr;
    __u32 count_connectors;
    __u32 crtc_id;
    __u32 fb_id;
    __u32 x, y;
    __u32 mode_valid;
    __u8  mode_bytes[76];
};

/* ======================================================================
 * ioctl 号 (与 Linux DRM 一致)
 * ====================================================================== */
#define DRM_IOCTL_BASE             'd'
#define DRM_IO(nr)                 (0x20000000u | ((uint32_t)(DRM_IOCTL_BASE)<<8) | (uint32_t)(nr))
#define DRM_IOR(nr,size)           (0x80000000u | ((uint32_t)(sizeof(size))<<16) | ((uint32_t)(DRM_IOCTL_BASE)<<8) | (uint32_t)(nr))
#define DRM_IOW(nr,size)           (0x40000000u | ((uint32_t)(sizeof(size))<<16) | ((uint32_t)(DRM_IOCTL_BASE)<<8) | (uint32_t)(nr))
#define DRM_IOWR(nr,size)          (0xC0000000u | ((uint32_t)(sizeof(size))<<16) | ((uint32_t)(DRM_IOCTL_BASE)<<8) | (uint32_t)(nr))

#define DRM_IOCTL_VERSION              DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_GET_CAP              DRM_IOWR(0x0C, struct drm_get_cap)
#define DRM_IOCTL_SET_MASTER           DRM_IO(0x1E)
#define DRM_IOCTL_DROP_MASTER          DRM_IO(0x1F)
#define DRM_IOCTL_MODE_CREATE_DUMB     DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB        DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB    DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)
#define DRM_IOCTL_GEM_CLOSE            DRM_IOW (0x09, struct drm_gem_close)
#define DRM_IOCTL_PRIME_FD_TO_HANDLE   DRM_IOWR(0x2e, struct drm_prime_handle)
#define DRM_IOCTL_PRIME_HANDLE_TO_FD   DRM_IOWR(0x2d, struct drm_prime_handle)
#define DRM_IOCTL_MODE_ADDFB2          DRM_IOWR(0xB8, struct drm_mode_fb_cmd2)
#define DRM_IOCTL_MODE_RMFB            DRM_IOW (0xAF, uint32_t)
#define DRM_IOCTL_MODE_SETCRTC         DRM_IOWR(0xA2, struct drm_mode_crtc)

/* CAP constants (如果 drm-uapi 未定义) */
#ifndef DRM_CAP_DUMB_BUFFER
#define DRM_CAP_DUMB_BUFFER      0x1
#endif
#ifndef DRM_CAP_PRIME
#define DRM_CAP_PRIME            0x5
#endif
#ifndef DRM_CAP_TIMESTAMP_MONOTONIC
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x6
#endif
#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH     0x7
#endif
#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT    0x8
#endif
#ifndef DRM_CAP_ADDFB2_MODIFIERS
#define DRM_CAP_ADDFB2_MODIFIERS 0x10
#endif

/* Mode flags (kernel uapi) */
#ifndef DRM_MODE_FLAG_PHSYNC
#define DRM_MODE_FLAG_PHSYNC            (1 << 0)
#define DRM_MODE_FLAG_NHSYNC            (1 << 1)
#define DRM_MODE_FLAG_PVSYNC            (1 << 2)
#define DRM_MODE_FLAG_NVSYNC            (1 << 3)
#define DRM_MODE_FLAG_INTERLACE         (1 << 4)
#define DRM_MODE_FLAG_DBLSCAN           (1 << 5)
#endif

#ifndef DRM_MODE_PAGE_FLIP_EVENT
#define DRM_MODE_PAGE_FLIP_EVENT 0x01
#define DRM_MODE_PAGE_FLIP_ASYNC 0x02
#endif

#ifndef DRM_MODE_ATOMIC_TEST_ONLY
#define DRM_MODE_ATOMIC_TEST_ONLY    0x0100
#define DRM_MODE_ATOMIC_NONBLOCK     0x0200
#define DRM_MODE_ATOMIC_ALLOW_MODESET 0x0400
#endif

#ifndef DRM_MODE_PROP_PENDING
#define DRM_MODE_PROP_PENDING  (1<<0)
#define DRM_MODE_PROP_RANGE    (1<<1)
#define DRM_MODE_PROP_IMMUTABLE (1<<2)
#define DRM_MODE_PROP_ENUM     (1<<3)
#define DRM_MODE_PROP_BLOB     (1<<4)
#define DRM_MODE_PROP_BITMASK  (1<<5)
#define DRM_MODE_PROP_OBJECT   (1<<6)
#define DRM_MODE_PROP_SIGNED_RANGE (1<<7)
#endif

/* Connector / subconnector / subpixel constants */
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED         1
#define DRM_MODE_DISCONNECTED      2
#define DRM_MODE_UNKNOWNCONNECTION 3
#endif
#ifndef DRM_MODE_CONNECTOR_Unknown
#define DRM_MODE_CONNECTOR_Unknown     0
#define DRM_MODE_CONNECTOR_VGA         1
#define DRM_MODE_CONNECTOR_DVII        2
#define DRM_MODE_CONNECTOR_DVID        3
#define DRM_MODE_CONNECTOR_DVIA        4
#define DRM_MODE_CONNECTOR_Composite   5
#define DRM_MODE_CONNECTOR_SVIDEO      6
#define DRM_MODE_CONNECTOR_LVDS        7
#define DRM_MODE_CONNECTOR_Component   8
#define DRM_MODE_CONNECTOR_9PinDIN     9
#define DRM_MODE_CONNECTOR_DisplayPort 10
#define DRM_MODE_CONNECTOR_HDMIA       11
#define DRM_MODE_CONNECTOR_HDMIB       12
#define DRM_MODE_CONNECTOR_TV          13
#define DRM_MODE_CONNECTOR_eDP         14
#define DRM_MODE_CONNECTOR_VIRTUAL     0xFFFF
#define DRM_MODE_CONNECTOR_SPI         17
#endif
#ifndef DRM_MODE_SUBPIXEL_UNKNOWN
#define DRM_MODE_SUBPIXEL_UNKNOWN      1
#define DRM_MODE_SUBPIXEL_HORIZONTAL_RGB 2
#define DRM_MODE_SUBPIXEL_HORIZONTAL_BGR 3
#define DRM_MODE_SUBPIXEL_VERTICAL_RGB   4
#define DRM_MODE_SUBPIXEL_VERTICAL_BGR   5
#define DRM_MODE_SUBPIXEL_NONE           6
#endif
#ifndef DRM_MODE_ENCODER_NONE
#define DRM_MODE_ENCODER_NONE    0
#define DRM_MODE_ENCODER_DAC     1
#define DRM_MODE_ENCODER_TMDS    2
#define DRM_MODE_ENCODER_LVDS    3
#define DRM_MODE_ENCODER_TVDAC   4
#define DRM_MODE_ENCODER_VIRTUAL 5
#define DRM_MODE_ENCODER_DSI     6
#define DRM_MODE_ENCODER_DPMST   7
#define DRM_MODE_ENCODER_DPI     8
#endif

#endif /* DRM_MODE_H */
