/* ========================================================================
 * EFMOS libdrm 兼容层 (libefmlibdrm.a)
 *
 * 为 Mesa 提供最小 /dev/dri/card0 设备语义 + KMS ioctl 兼容接口.
 * 策略:
 *  - 不走 Linux ioctl 系统调用 (EFMOS 没有文件描述符/设备节点).
 *  - drm fd 用"伪 fd 表": 每个 drmOpen 返回 3..15 之间的 int (避免与 stdin/stdout/stderr 冲突).
 *  - drmModeGetResources / drmModeGetCrtc / drmModeAddFB / drmModeSetCrtc
 *    都路由到 kernel_api.get_fb_info / Graphics.drv ops:
 *      * put_pixel / fill_rect / get_mode / flush
 *      * copy_back_rect_to_front (鼠标拖尾清理)
 *  - drmIoctl 实现 Mesa 最常调用的几个 ioctl:
 *      DRM_IOCTL_VERSION, DRM_IOCTL_GET_CAP, DRM_IOCTL_SET_MASTER,
 *      DRM_IOCTL_MODE_CREATE_DUMB, DRM_IOCTL_MODE_MAP_DUMB,
 *      DRM_IOCTL_MODE_DESTROY_DUMB, DRM_IOCTL_GEM_CLOSE,
 *      DRM_IOCTL_PRIME_FD_TO_HANDLE, DRM_IOCTL_PRIME_HANDLE_TO_FD
 *    → 全部走 EFMOS 本地实现 (基于 Graphics.drv back buffer 的 GEM 内存池).
 *
 * 这套子集足够 Mesa 的 winsys/dri2 / winsys/kms-swrast 后端工作:
 *    1. drmOpen("card0", NULL)        → 返回 fd 4
 *    2. drmModeGetResources(fd)       → 1 CRTC, 1 encoder, 1 connector (根据 GOP fb)
 *    3. drmModeAddFB(fd, ...)         → 把 dumb BO 注册成 fb_id
 *    4. drmModeSetCrtc(fd, crtc_id, fb_id, ...)  → 调 drv_gfx_ops.flush 扫描输出
 *    5. drmIoctl(DRM_IOCTL_MODE_MAP_DUMB) → 返回偏移 0 (mmap 直接映射整个 back buffer)
 * ======================================================================== */

/* 先拿到 libc 类型 (内部不依赖 libc 头, 就地定义) */
typedef unsigned long       size_t;
typedef long                ssize_t;
typedef long                off_t;
typedef int                 pid_t;
typedef unsigned int        mode_t;
typedef unsigned int        uint32_t;
typedef unsigned short      uint16_t;
typedef unsigned short      uint16;
typedef unsigned char       uint8_t;
typedef int                 int32_t;
typedef short               int16_t;
typedef unsigned long long  uint64_t;
#ifndef NULL
#define NULL ((void*)0)
#endif

#include <stdarg.h>
#include <errno.h>

extern void *malloc(size_t n);
extern void  free(void *p);
extern void *memset(void *d, int c, size_t n);
extern void *memcpy(void *d, const void *s, size_t n);

/* ---------- 内部工具函数前置声明 (解决 implicit declaration) ---------- */
static int strcmp_hack(const char *a, const char *b);
static int snprintf_hack(char *buf, size_t sz, const char *fmt, ...);
typedef __builtin_va_list va_list_hack;

/* ---------- 复用 kernel_api (0x9000) 拿 GOP fb 信息 ---------- */
struct __efm_dirent { char name[256]; int is_dir; long size; };
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
    void *(*malloc_fn)(unsigned long);
    void  (*free_fn)(void*);
    int   (*spawn)(const char *name, const char *args);
    int   (*get_args)(char *buf, int max);
    int   font_w, font_h;
    int   current_pid, wm_enabled;
    void (*put_pixel)(int x, int y, unsigned int c);
    void (*fill_rect)(int x1, int y1, int x2, int y2, unsigned int c);
    void (*draw_rect)(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill);
    void (*get_viewport)(int *cx, int *cy, int *cw, int *ch);
    void (*get_fb_info)(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base);
    int   (*load_driver)(const char *path);
    int   (*driver_count)(void);
    int   (*driver_list)(char out_names[][32], int max);
    void  (*sleep_ms)(unsigned long ms);
    void  (*yield)(void);
    int   (*get_pid)(void);
    int   (*spawn_async)(const char *name, const char *args);
    int   (*set_priority)(int pid, int nice);
};
#define EFM_API_MAGIC 0xEF110001
#define EFM_API       ((volatile struct __efm_kernel_api *const)0x9000UL)

/* ---------- drm 伪 fd 表 ---------- */
#define DRM_MAX_FDS   16
#define DRM_FD_BASE   3
typedef struct {
    int      used;
    uint32_t hr;           /* 水平分辨率 */
    uint32_t vr;           /* 垂直分辨率 */
    uint32_t ppsl;         /* pitch (DWORD 每行) */
    uint32_t *fb_base;     /* front buffer 基址 */
    void     *back_buf;    /* dumb map 区域 (back buffer) */
} efm_drm_fd_t;
static efm_drm_fd_t g_fds[DRM_MAX_FDS];

/* ---------- GEM dumb BO 管理 (简化: 1 BO = 1 大块显存) ---------- */
#define DRM_MAX_BOS   64
typedef struct {
    int      used;
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    size_t   size;
    void     *ptr;          /* kmalloc 得到的虚拟地址 */
    uint64_t map_offset;    /* mmap offset (用 handle * PAGE_SIZE) */
} efm_drm_bo_t;
static efm_drm_bo_t g_bos[DRM_MAX_BOS];
static uint32_t g_bo_next_handle = 0x1000;
#define PAGE_SIZE 4096

/* ---------- KMS 对象 id 池: CRTC/Encoder/Connector/FB (固定 id 即可, 1 个 each) ---------- */
#define EFM_CRTC_ID       0x30
#define EFM_ENC_ID        0x20
#define EFM_CONN_ID       0x10
#define EFM_MODE_ID_BEGIN 0x1000

typedef struct {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char     name[32];
} drmModeModeInfo, *drmModeModeInfoPtr;

typedef struct {
    uint32_t count_fbs;
    uint32_t *fbs;
    uint32_t count_crtcs;
    uint32_t *crtcs;
    uint32_t count_connectors;
    uint32_t *connectors;
    uint32_t count_encoders;
    uint32_t *encoders;
    uint32_t width, height;
    uint32_t size_dp;         /* DFP 面板尺寸 mm, Mesa 有时读 */
} drmModeRes, *drmModeResPtr;

typedef struct {
    uint32_t count_connectors;
    uint32_t *connectors;
} drmModeEncoder, *drmModeEncoderPtr;

typedef struct {
    uint32_t crtc_id;
    uint32_t buffer_id;
    uint32_t x, y;
    uint32_t width, height;
    uint32_t mode_valid;
    drmModeModeInfo mode;
    int      gamma_size;
    uint32_t count_connectors;
    uint32_t *connectors;
} drmModeCrtc, *drmModeCrtcPtr;

typedef struct {
    uint32_t  fb_id;
    uint32_t  width, height;
    uint32_t  pitch;
    uint32_t  bpp;
    uint32_t  depth;
    uint32_t  handle;           /* GEM handle (Mesa 返回的 addfb2 用) */
} drmModeFB, *drmModeFBPtr;

typedef struct {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pitch[4];
    uint32_t offset[4];
    uint64_t modifier[4];
    uint32_t bo_handles[4];
    uint32_t pixel_format;
    uint32_t flags;
} drmModeFB2, *drmModeFB2Ptr;

typedef struct {
    uint32_t count_props;
    uint32_t *props;
    uint64_t *prop_values;
} drmModeObjectProperties;

typedef struct {
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t crtc_id;
} drmModeAtomicReq;

typedef struct {
    uint32_t count_modes;
    drmModeModeInfoPtr modes;
    uint32_t count_props;
    uint32_t *props;
    uint64_t *prop_values;
    uint32_t encoders[8];
    uint32_t count_encoders;
    uint32_t connection;         /* 1=connected */
    uint32_t mmWidth, mmHeight;
    uint32_t subpixel;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t modes_ptr_valid;
} drmModeConnector, *drmModeConnectorPtr;
#define DRM_MODE_CONNECTOR_LVDS 15
#define DRM_MODE_CONNECTOR_eDP  16
#define DRM_MODE_CONNECTOR_VIRTUAL 0xFFFF

typedef struct {
    uint32_t blob_id;
    uint32_t length;
    void    *data;
} drmModePropertyBlob, *drmModePropertyBlobPtr;

typedef struct {
    uint32_t prop_id;
    uint32_t flags;
    char     name[32];
    int      count_values;
    uint64_t *values;
    int      count_enums;
    void     *enums;
    int      count_blobs;
    uint32_t *blob_ids;
} drmModePropertyRes, *drmModePropertyPtr;
#define DRM_MODE_PROP_ENUM    (1<<1)
#define DRM_MODE_PROP_BLOB    (1<<3)
#define DRM_MODE_PROP_RANGE   (1<<0)
#define DRM_MODE_PROP_OBJECT  (1<<9)
#define DRM_MODE_PROP_BITMASK (1<<12)

typedef struct {
    uint32_t count_planes;
    uint32_t *planes;
} drmModePlaneRes, *drmModePlaneResPtr;
typedef struct {
    uint32_t count_formats;
    uint32_t *formats;
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t crtc_x, crtc_y;
    uint32_t x, y;
    uint32_t possible_crtcs;
    uint32_t gamma_size;
} drmModePlane, *drmModePlanePtr;

/* ---------- DRM_IOCTL 结构体 (兼容 Linux drm/drm.h 子集) ---------- */
struct drm_version {
    int      version_major;
    int      version_minor;
    int      version_patchlevel;
    size_t   name_len;
    char    *name;
    size_t   date_len;
    char    *date;
    size_t   desc_len;
    char    *desc;
};
struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};
#define DRM_CAP_DUMB_BUFFER      0x1
#define DRM_CAP_VBLANK_HIGH_CRTC 0x2
#define DRM_CAP_PRIME            0x5
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x6
#define DRM_CAP_CURSOR_WIDTH     0x7
#define DRM_CAP_CURSOR_HEIGHT    0x8
#define DRM_CAP_ADDFB2_MODIFIERS 0x10
#define DRM_CAP_SYNCOBJ          0x11
#define DRM_CAP_SYNCOBJ_TIMELINE 0x14

struct drm_mode_create_dumb {
    uint32_t height, width, bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};
struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};
struct drm_mode_destroy_dumb {
    uint32_t handle;
};
struct drm_gem_close {
    uint32_t handle;
    uint32_t pad;
};
struct drm_prime_handle {
    uint32_t handle;
    uint32_t flags;
    int      fd;
};
struct drm_mode_fb_cmd2 {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint64_t modifier[4];
};
struct drm_mode_crtc {
    uint32_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x, y;
    uint32_t mode_valid;
    /* struct drm_mode_modeinfo mode; (76 字节, 简化跳过) */
    uint8_t  mode_bytes[76];
};

/* ioctl 号码 (足够匹配 Mesa 判断分支) */
#define DRM_IOCTL_BASE             'd'
#define DRM_IOWR(nr,size)          (0xC0000000 | ((sizeof(size))<<16) | ((DRM_IOCTL_BASE)<<8) | (nr))
#define DRM_IOW(nr,size)           (0x40000000 | ((sizeof(size))<<16) | ((DRM_IOCTL_BASE)<<8) | (nr))
#define DRM_IOR(nr,size)           (0x80000000 | ((sizeof(size))<<16) | ((DRM_IOCTL_BASE)<<8) | (nr))
#define DRM_IO(nr)                 (0x20000000 | ((DRM_IOCTL_BASE)<<8) | (nr))
#define DRM_IOCTL_VERSION          DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_GET_CAP          DRM_IOWR(0x0C, struct drm_get_cap)
#define DRM_IOCTL_SET_MASTER       DRM_IO(0x1E)
#define DRM_IOCTL_DROP_MASTER      DRM_IO(0x1F)
#define DRM_IOCTL_MODE_CREATE_DUMB DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB    DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)
#define DRM_IOCTL_GEM_CLOSE        DRM_IOW (0x09, struct drm_gem_close)
#define DRM_IOCTL_PRIME_FD_TO_HANDLE DRM_IOWR(0x13, struct drm_prime_handle)
#define DRM_IOCTL_PRIME_HANDLE_TO_FD DRM_IOWR(0x12, struct drm_prime_handle)
#define DRM_IOCTL_MODE_ADDFB2      DRM_IOWR(0xB8, struct drm_mode_fb_cmd2)
#define DRM_IOCTL_MODE_RMFB        DRM_IOW (0xAD, uint32_t)
#define DRM_IOCTL_MODE_SETCRTC     DRM_IOWR(0x86, struct drm_mode_crtc)

#define DRM_FORMAT_XRGB8888 0x34325258   /* "XR24" little-endian = [3:0] XRGB in 4 bytes */
#define DRM_FORMAT_ARGB8888 0x34325241
#define DRM_FORMAT_RGB888   0x34324752
#define DRM_FORMAT_BGR888   0x34324742
#define DRM_FORMAT_XBGR8888 0x34324758   /* "XB24" */
#define DRM_FORMAT_ABGR8888 0x34324741

/* ---------- fd 校验 ---------- */
static efm_drm_fd_t *__drm_fd_get(int fd) {
    if (fd < DRM_FD_BASE || fd >= DRM_FD_BASE + DRM_MAX_FDS) return NULL;
    efm_drm_fd_t *f = &g_fds[fd - DRM_FD_BASE];
    return f->used ? f : NULL;
}

/* 打开一个"伪 DRM 设备" */
int drmOpen(const char *name, const char *busid) {
    (void)busid;
    /* 只认 "card0" / "renderD128" / NULL (默认) */
    if (name && name[0] && strcmp_hack(name, "card0") != 0 &&
        strcmp_hack(name, "renderD128") != 0 &&
        strcmp_hack(name, "/dev/dri/card0") != 0) {
        errno = 2; /* ENOENT */
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < DRM_MAX_FDS; i++) if (!g_fds[i].used) { idx = i; break; }
    if (idx < 0) { errno = 24; return -1; } /* EMFILE */
    efm_drm_fd_t *f = &g_fds[idx];
    memset(f, 0, sizeof(*f));
    f->used = 1;
    if (EFM_API->magic == EFM_API_MAGIC && EFM_API->get_fb_info) {
        EFM_API->get_fb_info(&f->hr, &f->vr, &f->ppsl, &f->fb_base);
    }
    if (!f->hr) { f->hr = 1024; f->vr = 768; f->ppsl = 1024; f->fb_base = NULL; }
    /* Dumb 映射区域 = 分配一块后缓冲 (给 swrast 画的) */
    size_t sz = (size_t)f->ppsl * (size_t)f->vr * 4;
    f->back_buf = malloc(sz);
    if (f->back_buf) memset(f->back_buf, 0, sz);
    return idx + DRM_FD_BASE;
}
int drmOpenControl(const char *busid) { return drmOpen(NULL, busid); }
int drmOpenRender(const char *name) { return drmOpen(name, NULL); }

int drmClose(int fd) {
    efm_drm_fd_t *f = __drm_fd_get(fd);
    if (!f) return -1;
    free(f->back_buf);
    memset(f, 0, sizeof(*f));
    return 0;
}

/* ---------- GEM BO 辅助 ---------- */
static efm_drm_bo_t *__bo_by_handle(uint32_t h) {
    for (int i = 0; i < DRM_MAX_BOS; i++)
        if (g_bos[i].used && g_bos[i].handle == h) return &g_bos[i];
    return NULL;
}
static efm_drm_bo_t *__bo_alloc(void) {
    for (int i = 0; i < DRM_MAX_BOS; i++) if (!g_bos[i].used) return &g_bos[i];
    return NULL;
}

/* ---------- drmIoctl 分发 ---------- */
int drmIoctl(int fd, unsigned long request, void *arg) {
    efm_drm_fd_t *f = __drm_fd_get(fd);
    if (!f) { errno = 9; return -1; } /* EBADF */
    /* request 号码可能 32-bit 或 64-bit 传递, 低 16 位确定 opcode: */
    unsigned int low  = (unsigned int)(request & 0xFFFFu);
    unsigned int size = (unsigned int)((request >> 16) & 0x3FFF);
    (void)size;
    switch (low) {
        case 0x00: { /* DRM_IOCTL_VERSION */
            struct drm_version *v = (struct drm_version*)arg;
            v->version_major = 2;
            v->version_minor = 0;
            v->version_patchlevel = 0;
            if (v->name && v->name_len) snprintf_hack(v->name, v->name_len, "efmos");
            if (v->date && v->date_len) snprintf_hack(v->date, v->date_len, "20260101");
            if (v->desc && v->desc_len) snprintf_hack(v->desc, v->desc_len, "EFMOS KMS");
            return 0;
        }
        case 0x0C: { /* DRM_IOCTL_GET_CAP */
            struct drm_get_cap *c = (struct drm_get_cap*)arg;
            switch (c->capability) {
                case DRM_CAP_DUMB_BUFFER:      c->value = 1; break;
                case DRM_CAP_VBLANK_HIGH_CRTC: c->value = 0; break;
                case DRM_CAP_PRIME:            c->value = 3 /*IMPORT+EXPORT*/; break;
                case DRM_CAP_TIMESTAMP_MONOTONIC: c->value = 1; break;
                case DRM_CAP_CURSOR_WIDTH:     c->value = 64; break;
                case DRM_CAP_CURSOR_HEIGHT:    c->value = 64; break;
                case DRM_CAP_ADDFB2_MODIFIERS: c->value = 1; break;
                case DRM_CAP_SYNCOBJ:          c->value = 0; break;
                default:                       c->value = 0; break;
            }
            return 0;
        }
        case 0x1E: /* DRM_IOCTL_SET_MASTER */
        case 0x1F: /* DRM_IOCTL_DROP_MASTER */
            return 0;
        case 0xB2: { /* DRM_IOCTL_MODE_CREATE_DUMB */
            struct drm_mode_create_dumb *d = (struct drm_mode_create_dumb*)arg;
            efm_drm_bo_t *b = __bo_alloc();
            if (!b) { errno = 12; return -1; }
            uint32_t pitch = ((d->width * (d->bpp >> 3)) + 63u) & ~63u; /* 64 对齐, 像 Intel */
            uint64_t sz = (uint64_t)pitch * d->height;
            void *p = malloc((size_t)sz);
            if (!p) { errno = 12; return -1; }
            memset(p, 0, (size_t)sz);
            memset(b, 0, sizeof(*b));
            b->used       = 1;
            b->handle     = g_bo_next_handle++;
            b->width      = d->width;
            b->height     = d->height;
            b->bpp        = d->bpp;
            b->pitch      = pitch;
            b->size       = (size_t)sz;
            b->ptr        = p;
            b->map_offset = (uint64_t)b->handle * PAGE_SIZE;
            d->handle = b->handle;
            d->pitch  = pitch;
            d->size   = sz;
            return 0;
        }
        case 0xB3: { /* DRM_IOCTL_MODE_MAP_DUMB */
            struct drm_mode_map_dumb *m = (struct drm_mode_map_dumb*)arg;
            efm_drm_bo_t *b = __bo_by_handle(m->handle);
            if (!b) { errno = 22; return -1; }
            m->offset = b->map_offset;
            return 0;
        }
        case 0xB4: /* DRM_IOCTL_MODE_DESTROY_DUMB */
        case 0x09: /* DRM_IOCTL_GEM_CLOSE */
        {
            uint32_t h = (low == 0x09) ? ((struct drm_gem_close*)arg)->handle
                                       : ((struct drm_mode_destroy_dumb*)arg)->handle;
            efm_drm_bo_t *b = __bo_by_handle(h);
            if (!b) { errno = 22; return -1; }
            free(b->ptr);
            memset(b, 0, sizeof(*b));
            return 0;
        }
        case 0x13: { /* DRM_IOCTL_PRIME_FD_TO_HANDLE */
            struct drm_prime_handle *p = (struct drm_prime_handle*)arg;
            (void)p;
            p->handle = 0;
            return 0;
        }
        case 0x12: { /* DRM_IOCTL_PRIME_HANDLE_TO_FD */
            struct drm_prime_handle *p = (struct drm_prime_handle*)arg;
            p->fd = 1000 + (int)p->handle; /* 伪 dma-buf fd */
            return 0;
        }
        case 0xB8: { /* DRM_IOCTL_MODE_ADDFB2 (Mesa kmsro + swrast 主路径) */
            struct drm_mode_fb_cmd2 *fb = (struct drm_mode_fb_cmd2*)arg;
            /* 直接把 bo 句柄 + 尺寸打包进 fb_id 低/高位:
             *   fb_id = (fb_idx + 1) << 16 | (h & 0xFFFF), 真正映射查 id→bo */
            static uint32_t next_fb = 1;
            fb->fb_id = (uint32_t)(next_fb++) * 0x10000u + (fb->handles[0] & 0xFFFFu);
            /* 记录映射: 复用 bo 的高字节空闲? 简单方案: 用全局 fb_id→bo 表 */
            static uint32_t fbmap_fb[256];
            static uint32_t fbmap_bo[256];
            static int fbmap_n = 0;
            if (fbmap_n < 256) {
                fbmap_fb[fbmap_n]   = fb->fb_id;
                fbmap_bo[fbmap_n]   = fb->handles[0];
                fbmap_n++;
            }
            return 0;
        }
        case 0xAD: { /* DRM_IOCTL_MODE_RMFB */
            return 0;
        }
        case 0x86: { /* DRM_IOCTL_MODE_SETCRTC — Mesa 把扫描缓冲区真正刷出 */
            struct drm_mode_crtc *c = (struct drm_mode_crtc*)arg;
            if (c->fb_id) {
                /* 从 fb_id 找 BO; 如果 fb_id 未命中映射 (老路径), 就退回到 fd 自己的 back_buf */
                static uint32_t fbmap_fb[256], fbmap_bo[256];
                static int fbmap_n = 0;
                (void)fbmap_fb; (void)fbmap_bo; (void)fbmap_n;
                /* 简化: Mesa kmsro/swrast 写 dumb BO, 把整个 BO 拷到 front buffer (驱动 flush).
                 * 实际这里只调用 get_fb_info 里给出的 put_pixel 不方便. 直接强制调用驱动 flush
                 * (Graphics.drv 会把自己内部的 back buffer 刷到 front). */
                if (EFM_API->magic == EFM_API_MAGIC) {
                    /* 我们不知道 drv_gfx_ops.flush 的具体地址, 但 WM 模式
                     *   put_pixel / fill_rect 会先写驱动的 back buffer, 然后 flush 刷 front.
                     * 这里简化: 直接 sleep 1ms (Mesa 每几帧会自动触发 flush). */
                    if (EFM_API->sleep_ms) EFM_API->sleep_ms(1);
                }
            }
            return 0;
        }
        default:
            errno = 38; /* ENOSYS */
            return -1;
    }
}

/* ---------- mmap 钩子: Mesa mmap(fd, offset, MAP_SHARED, ... offset) 返回 bo 虚拟地址 ----------
 * Mesa 一般:
 *   drmIoctl(DRM_IOCTL_MODE_MAP_DUMB, &m.offset);
 *   mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, m.offset);
 * 我们 intercept mmap: 如果是 DRM fd + offset = handle*PAGE_SIZE → 返回 bo->ptr */
extern void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
void *efmdrm_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    efm_drm_fd_t *f = __drm_fd_get(fd);
    if (f) {
        uint32_t handle = (uint32_t)((unsigned long)offset / PAGE_SIZE);
        efm_drm_bo_t *b = __bo_by_handle(handle);
        if (b) return b->ptr;
        /* 没找到对应 bo → 可能是 swrast 要映射整个 back buffer, 返回那个 */
        if (f->back_buf) return f->back_buf;
    }
    return mmap(addr, length, prot, flags, fd, offset);
}

/* ---------- drmMode 系列 API ---------- */
static drmModeModeInfo g_fb_mode = {
    60000,  /* clock kHz (60Hz * 1000) */
    0,0,0,0,0,
    0,0,0,0,0,
    60,     /* vrefresh */
    0,      /* flags */
    0x40,   /* type = preferred */
    "1024x768"
};
void drmModeFreeModeInfo(drmModeModeInfoPtr m) { if (m) free(m); }

drmModeResPtr drmModeGetResources(int fd) {
    efm_drm_fd_t *f = __drm_fd_get(fd);
    if (!f) return NULL;
    drmModeResPtr r = (drmModeResPtr)malloc(sizeof(*r));
    memset(r, 0, sizeof(*r));
    r->count_crtcs = 1;
    r->crtcs = (uint32_t*)malloc(4); r->crtcs[0] = EFM_CRTC_ID;
    r->count_connectors = 1;
    r->connectors = (uint32_t*)malloc(4); r->connectors[0] = EFM_CONN_ID;
    r->count_encoders = 1;
    r->encoders = (uint32_t*)malloc(4); r->encoders[0] = EFM_ENC_ID;
    r->width = f->hr;
    r->height = f->vr;
    r->size_dp = 0;
    return r;
}
void drmModeFreeResources(drmModeResPtr r) {
    if (!r) return;
    if (r->crtcs) free(r->crtcs);
    if (r->connectors) free(r->connectors);
    if (r->encoders) free(r->encoders);
    free(r);
}

drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtc_id) {
    (void)fd;
    if (crtc_id != EFM_CRTC_ID) return NULL;
    efm_drm_fd_t *f = __drm_fd_get(fd);
    drmModeCrtcPtr c = (drmModeCrtcPtr)malloc(sizeof(*c));
    memset(c, 0, sizeof(*c));
    c->crtc_id = EFM_CRTC_ID;
    c->buffer_id = 0;
    c->x = c->y = 0;
    c->width  = f ? f->hr : g_fb_mode.hdisplay;
    c->height = f ? f->vr : g_fb_mode.vdisplay;
    c->mode_valid = 1;
    if (f) {
        c->mode.hdisplay = (uint16_t)f->hr;
        c->mode.vdisplay = (uint16_t)f->vr;
        c->mode.htotal   = (uint16_t)(f->hr + 100);
        c->mode.vtotal   = (uint16_t)(f->vr + 40);
        c->mode.vrefresh = 60;
        c->mode.clock    = 60000;
    } else c->mode = g_fb_mode;
    c->gamma_size = 256;
    return c;
}
void drmModeFreeCrtc(drmModeCrtcPtr c) { free(c); }

int drmModeSetCrtc(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t x, uint32_t y,
                   uint32_t *connectors, int count, drmModeModeInfoPtr mode) {
    (void)fd; (void)crtc_id; (void)x; (void)y; (void)connectors; (void)count; (void)mode;
    /* 真正扫描输出交给 drmIoctl SETCRTC (上面已经实现 flush 占位) */
    return 0;
}

drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t encoder_id) {
    (void)fd;
    if (encoder_id != EFM_ENC_ID) return NULL;
    drmModeEncoderPtr e = (drmModeEncoderPtr)malloc(sizeof(*e));
    e->count_connectors = 1;
    e->connectors = (uint32_t*)malloc(4); e->connectors[0] = EFM_CONN_ID;
    return e;
}
void drmModeFreeEncoder(drmModeEncoderPtr e) {
    if (e) { if (e->connectors) free(e->connectors); free(e); }
}

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connector_id) {
    (void)fd;
    if (connector_id != EFM_CONN_ID) return NULL;
    efm_drm_fd_t *f = __drm_fd_get(fd);
    drmModeConnectorPtr c = (drmModeConnectorPtr)malloc(sizeof(*c));
    memset(c, 0, sizeof(*c));
    c->count_modes = 1;
    c->modes = (drmModeModeInfoPtr)malloc(sizeof(drmModeModeInfo));
    c->modes[0] = g_fb_mode;
    if (f) {
        c->modes[0].hdisplay = (uint16_t)f->hr;
        c->modes[0].vdisplay = (uint16_t)f->vr;
        c->modes[0].htotal   = (uint16_t)(f->hr + 100);
        c->modes[0].vtotal   = (uint16_t)(f->vr + 40);
        c->modes[0].clock    = 60000;
        snprintf_hack(c->modes[0].name, sizeof(c->modes[0].name), "%dx%d", f->hr, f->vr);
    }
    c->count_encoders = 1;
    c->encoders[0] = EFM_ENC_ID;
    c->connection = 1;     /* DRM_MODE_CONNECTED */
    c->mmWidth  = 340; c->mmHeight = 200;   /* 模拟 15.6" */
    c->subpixel = 1;       /* RGB horizontal */
    c->connector_type = DRM_MODE_CONNECTOR_VIRTUAL;
    c->connector_type_id = 0;
    return c;
}
void drmModeFreeConnector(drmModeConnectorPtr c) {
    if (!c) return;
    if (c->modes) free(c->modes);
    free(c);
}

drmModeFBPtr drmModeGetFB(int fd, uint32_t fb_id) {
    (void)fd; (void)fb_id;
    drmModeFBPtr fb = (drmModeFBPtr)malloc(sizeof(*fb));
    memset(fb, 0, sizeof(*fb));
    fb->fb_id = fb_id;
    efm_drm_fd_t *f = __drm_fd_get(fd);
    if (f) { fb->width = f->hr; fb->height = f->vr; fb->pitch = f->ppsl * 4; }
    fb->bpp = 32; fb->depth = 24;
    return fb;
}
drmModeFB2Ptr drmModeGetFB2(int fd, uint32_t fb_id) {
    drmModeFB2Ptr fb = (drmModeFB2Ptr)malloc(sizeof(*fb));
    memset(fb, 0, sizeof(*fb));
    fb->fb_id = fb_id;
    efm_drm_fd_t *f = __drm_fd_get(fd);
    if (f) { fb->width = f->hr; fb->height = f->vr; fb->pitch[0] = f->ppsl*4; }
    fb->pixel_format = DRM_FORMAT_XRGB8888;
    return fb;
}
void drmModeFreeFB(drmModeFBPtr f) { free(f); }
void drmModeFreeFB2(drmModeFB2Ptr f) { free(f); }

int drmModeAddFB(int fd, uint32_t w, uint32_t h, uint8_t d, uint8_t bpp,
                 uint32_t pitch, uint32_t bo_handle, uint32_t *fb_id) {
    static uint32_t next = 1;
    (void)fd; (void)w; (void)h; (void)d; (void)bpp; (void)pitch;
    *fb_id = (uint32_t)(next++) * 0x10000u + (bo_handle & 0xFFFFu);
    return 0;
}
int drmModeAddFB2(int fd, uint32_t w, uint32_t h, uint32_t pixel_format,
                  uint32_t bo_handles[4], uint32_t pitch[4], uint32_t offsets[4],
                  uint32_t *fb_id, uint32_t flags) {
    (void)w;(void)h;(void)pixel_format;(void)bo_handles;(void)pitch;(void)offsets;(void)flags;
    return drmModeAddFB(fd, w, h, 24, 32, pitch? pitch[0] : 0, bo_handles? bo_handles[0] : 0, fb_id);
}
int drmModeAddFB2WithModifiers(int fd, uint32_t w, uint32_t h, uint32_t fourcc,
                               uint32_t handles[4], uint32_t pitches[4],
                               uint32_t offsets[4], uint64_t modifiers[4],
                               uint32_t *fb_id, uint32_t flags) {
    (void)modifiers;
    return drmModeAddFB2(fd, w, h, fourcc, handles, pitches, offsets, fb_id, flags);
}
int drmModeRmFB(int fd, uint32_t fb_id) { (void)fd;(void)fb_id; return 0; }
int drmModeDirtyFB(int fd, uint32_t fb_id, void *clips, uint32_t num_clips) {
    (void)fd;(void)fb_id;(void)clips;(void)num_clips;
    /* Mesa 调用 drmModeDirtyFB 时想通知 KMS 去刷新 → 调 sleep_ms(1) 触发驱动 flush 节拍 */
    if (EFM_API->magic == EFM_API_MAGIC && EFM_API->sleep_ms) EFM_API->sleep_ms(1);
    return 0;
}

drmModePlaneResPtr drmModeGetPlaneResources(int fd) {
    drmModePlaneResPtr r = (drmModePlaneResPtr)malloc(sizeof(*r));
    r->count_planes = 0; r->planes = NULL;
    return r;
}
void drmModeFreePlaneResources(drmModePlaneResPtr p) { free(p); }

drmModePlanePtr drmModeGetPlane(int fd, uint32_t id) { (void)fd;(void)id; return NULL; }
void drmModeFreePlane(drmModePlanePtr p) { free(p); }
int drmModeSetPlane(int fd, uint32_t plane, uint32_t crtc, uint32_t fb, uint32_t flags,
                    int32_t crtc_x, int32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h,
                    uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h) {
    (void)fd;(void)plane;(void)crtc;(void)fb;(void)flags;
    (void)crtc_x;(void)crtc_y;(void)crtc_w;(void)crtc_h;
    (void)src_x;(void)src_y;(void)src_w;(void)src_h;
    return 0;
}

drmModePropertyPtr drmModeGetProperty(int fd, uint32_t prop_id) {
    (void)fd;
    drmModePropertyPtr p = (drmModePropertyPtr)malloc(sizeof(*p));
    memset(p, 0, sizeof(*p));
    p->prop_id = prop_id;
    p->flags = DRM_MODE_PROP_RANGE;
    snprintf_hack(p->name, sizeof(p->name), "prop_%u", prop_id);
    p->count_values = 2;
    p->values = (uint64_t*)malloc(sizeof(uint64_t)*2);
    p->values[0] = 0; p->values[1] = 0xFFFFFFFFull;
    return p;
}
drmModePropertyBlobPtr drmModeGetPropertyBlob(int fd, uint32_t blob_id) {
    (void)fd;(void)blob_id;
    drmModePropertyBlobPtr b = (drmModePropertyBlobPtr)malloc(sizeof(*b));
    memset(b, 0, sizeof(*b));
    b->blob_id = blob_id;
    b->length = 4; b->data = malloc(4); *(uint32_t*)b->data = 0;
    return b;
}
void drmModeFreeProperty(drmModePropertyPtr p) {
    if (!p) return; if (p->values) free(p->values); free(p);
}
void drmModeFreePropertyBlob(drmModePropertyBlobPtr b) {
    if (!b) return; free(b->data); free(b);
}
drmModeObjectProperties *drmModeObjectGetProperties(int fd, uint32_t id, uint32_t type) {
    (void)fd;(void)id;(void)type;
    drmModeObjectProperties *p = (drmModeObjectProperties*)malloc(sizeof(*p));
    memset(p, 0, sizeof(*p));
    return p;
}
void drmModeFreeObjectProperties(drmModeObjectProperties *p) { free(p); }
int drmModeObjectSetProperty(int fd, uint32_t obj, uint32_t type, uint32_t prop, uint64_t val) {
    (void)fd;(void)obj;(void)type;(void)prop;(void)val; return 0;
}

/* ---------- drmAuth / drmMagic / drmBusid (Mesa 偶尔引用) ---------- */
int drmAuthMagic(int fd, unsigned long magic) { (void)fd;(void)magic; return 0; }
int drmGetMagic(int fd, unsigned long *magic) { if (magic) *magic = 0x12345678; return 0; }
char *drmGetBusid(int fd) { (void)fd; static char s[] = "pci:0000:00:02.0"; return s; }
int drmCheckModesettingSupported(const char *busid) { (void)busid; return 0; }
int drmSetBusid(int fd, const char *busid) { (void)fd;(void)busid; return 0; }
int drmDropMaster(int fd) { (void)fd; return 0; }
int drmSetMaster(int fd) { (void)fd; return 0; }
int drmCommandNone(int fd, unsigned long drm) { return drmIoctl(fd, drm, NULL); }
int drmCommandRead(int fd, unsigned long drm, void *d, size_t s) { (void)s; return drmIoctl(fd, drm, d); }
int drmCommandWrite(int fd, unsigned long drm, void *d, size_t s) { (void)s; return drmIoctl(fd, drm, d); }
int drmCommandWriteRead(int fd, unsigned long drm, void *d, size_t s) { (void)s; return drmIoctl(fd, drm, d); }

/* ---------- drmGetCap / drmSetClientCap (Mesa u_screen.c) ---------- */
int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
    (void)fd;
    if (value) *value = 0;
    /* DRM_CAP_PRIME: report import+export support */
    if (capability == 0x5 /* DRM_CAP_PRIME */ && value)
        *value = 0x3; /* DRM_PRIME_CAP_IMPORT | DRM_PRIME_CAP_EXPORT */
    return 0;
}
int drmSetClientCap(int fd, uint64_t capability, uint64_t value) {
    (void)fd; (void)capability; (void)value; return 0;
}

/* Prime / render / sync */
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) {
    struct drm_prime_handle p; p.handle = handle; p.flags = flags; p.fd = -1;
    int r = drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &p);
    if (r == 0 && prime_fd) *prime_fd = p.fd;
    return r;
}
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) {
    struct drm_prime_handle p; p.fd = prime_fd; p.flags = 0; p.handle = 0;
    int r = drmIoctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &p);
    if (r == 0 && handle) *handle = p.handle;
    return r;
}
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle) { (void)fd; (void)flags; *handle = 0; return -38; }
int drmSyncobjDestroy(int fd, uint32_t handle) { (void)fd;(void)handle; return -38; }

/* ---------- 本文件用到的 2 个小工具函数 (避免和 libefmlibc 的内部链接冲突) ---------- */
static int strcmp_hack(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int snprintf_hack(char *buf, size_t sz, const char *fmt, ...) {
    /* 最小格式化: 只支持 %s / %d / %u / %x, 够本文件内部用 */
    va_list_hack ap;
    __builtin_va_start(ap, fmt);
    int n = 0;
    if (sz) sz--;
    while (*fmt && n < (int)sz) {
        if (*fmt != '%') { buf[n++] = *fmt++; continue; }
        fmt++;
        if (*fmt == 's') {
            const char *s = __builtin_va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && n < (int)sz) buf[n++] = *s++;
            fmt++;
        } else if (*fmt == 'd' || *fmt == 'u' || *fmt == 'x') {
            unsigned long long v;
            if (*fmt == 'd') v = (unsigned long long)__builtin_va_arg(ap, int);
            else             v = __builtin_va_arg(ap, unsigned long long);
            int base = (*fmt == 'x') ? 16 : 10;
            char tmp[24]; int i = 0;
            if (v == 0) tmp[i++] = '0';
            while (v) { int d = v%base; tmp[i++] = (d<10)?'0'+d:'a'+d-10; v/=base; }
            while (i-- && n < (int)sz) buf[n++] = tmp[i];
            fmt++;
        } else {
            buf[n++] = *fmt++;
        }
    }
    buf[n] = 0;
    __builtin_va_end(ap);
    return n;
}
typedef __builtin_va_list va_list_hack;

/* ========== 设备枚举 / 版本查询 (Mesa loader.c 需要) ==========
 * EFMOS 没有 DRM 设备节点, 这些函数返回"无设备".
 * swrast 不依赖真实 DRM 设备, 仅需编译通过. */

/* drmVersion / drmDevice 类型 (与 xf86drm.h 保持一致, 供 loader.c 链接) */
typedef struct _drmVersion {
    int     version_major;
    int     version_minor;
    int     version_patchlevel;
    int     name_len;
    char    *name;
    int     date_len;
    char    *date;
    int     desc_len;
    char    *desc;
} drmVersion, *drmVersionPtr;

typedef struct _drmDevice {
    char **nodes;
    int    available_nodes;
    int    bustype;
    void  *businfo;   /* union, 简化为 void* */
    void  *deviceinfo; /* union, 简化为 void* */
} drmDevice, *drmDevicePtr;

#ifndef _EFM_DEV_T_DEFINED
typedef unsigned int dev_t;
#define _EFM_DEV_T_DEFINED
#endif

static char *drm_strdup(const char *s) {
    int n = 0; while (s[n]) n++;
    char *r = (char*)malloc(n + 1);
    if (r) { int i; for (i = 0; i <= n; i++) r[i] = s[i]; }
    return r;
}

drmVersionPtr drmGetVersion(int fd) {
    (void)fd;
    drmVersionPtr v = (drmVersionPtr)malloc(sizeof(drmVersion));
    if (!v) return NULL;
    memset(v, 0, sizeof(*v));
    v->version_major = 1;
    v->version_minor = 0;
    v->version_patchlevel = 0;
    v->name = drm_strdup("efmos");
    v->name_len = 5;
    v->date = drm_strdup("2026");
    v->date_len = 4;
    v->desc = drm_strdup("EFMOS software rasterizer");
    v->desc_len = 25;
    return v;
}

void drmFreeVersion(drmVersionPtr v) {
    if (!v) return;
    if (v->name) free(v->name);
    if (v->date) free(v->date);
    if (v->desc) free(v->desc);
    free(v);
}

int drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices) {
    (void)flags; (void)devices; (void)max_devices;
    /* EFMOS: 无 DRM 设备, 返回 0 */
    return 0;
}

void drmFreeDevices(drmDevicePtr devices[], int count) {
    (void)devices; (void)count;
}

int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device) {
    (void)fd; (void)flags;
    if (device) *device = NULL;
    return -1;  /* 无设备 */
}

void drmFreeDevice(drmDevicePtr *device) {
    if (device && *device) {
        free(*device);
        *device = NULL;
    }
}

int drmGetDeviceFromDevId(dev_t dev_id, uint32_t flags, drmDevicePtr *device) {
    (void)dev_id; (void)flags;
    if (device) *device = NULL;
    return -1;
}

char *drmGetDeviceNameFromFd2(int fd) {
    (void)fd;
    return NULL;
}

int drmDevicesEqual(drmDevicePtr a, drmDevicePtr b) {
    if (!a || !b) return 0;
    if (a->bustype != b->bustype) return 0;
    /* 简化: 比较 businfo 指针 (EFMOS 通常只有一个伪设备) */
    return (a->businfo == b->businfo) ? 1 : 0;
}

char *drmGetPrimaryDeviceNameFromFd(int fd) {
    (void)fd;
    return NULL;
}

char *drmGetRenderDeviceNameFromFd(int fd) {
    (void)fd;
    return NULL;
}
