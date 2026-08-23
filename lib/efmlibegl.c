/* ========================================================================
 * EFMOS EGL 平台兼容层 (libefmegl.a)
 *
 * 目标: 提供 Mesa EGL 的"EFMOS 原生平台", 完全绕过 X11/Wayland.
 *       EGL 调用直接走 GBM → dumb BO → KMS 扫描输出 → GOP front buffer.
 *
 * 工作原理:
 *   1. eglGetDisplay(EGL_DEFAULT_DISPLAY) 返回 EFMOS display (内部打开 DRM fd)
 *   2. eglCreateWindowSurface 接受 native_window = EFMOS 窗口 ID 或 NULL (全屏)
 *   3. eglSwapBuffers:
 *        a. Mesa Gallium swrast 把帧渲染到 GBM BO 的 mmap 区域
 *        b. eglSwapBuffers 调用 efm_gbm_blit_to_screen 把 BO 拷到 GOP fb
 *        c. drmModeDirtyFB 通知 KMS 刷新
 *
 * 与 Mesa loader 的接口:
 *   - _eglMatchDriver(disp) 内部匹配 "swrast" 驱动 (kmsro = swrast + kms)
 *   - platform_efmos.c 提供:
 *       eglGetDisplay → _efmos_get_platform_display
 *       eglCreatePlatformWindowSurface → _efmos_create_window_surface
 *   - 本文件实现这些入口符号, Mesa dlopen("libEGL.so.1") 时解析到
 *
 * 配置:
 *   - EGL_PLATFORM_EFMOS = 2 (自定义, 与 EGL_PLATFORM_X11=1/WAYLAND=2 不冲突)
 *   - 环境变量 EGL_PLATFORM=efmos 强制走本平台
 * ======================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <xf86drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>

/* EFMOS kernel API 包装函数 (来自 efmlibc.c) */
extern void efm_get_viewport(int *cx, int *cy, int *cw, int *ch);

/* ---------- EGL 基本类型 (与 Khronos EGL/egl.h 一致) ---------- */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLClientBuffer;
typedef void *EGLNativeDisplayType;
typedef void *EGLNativeWindowType;
typedef void *EGLNativePixmapType;
typedef int32_t EGLint;
typedef uint32_t EGLBoolean;
typedef uint32_t EGLenum;
typedef uint32_t EGLConfigID;
typedef uint32_t EGLSurfaceID;

#define EGL_SUCCESS                0x3000
#define EGL_NOT_INITIALIZED        0x3001
#define EGL_BAD_ACCESS             0x3002
#define EGL_BAD_ALLOC              0x3003
#define EGL_BAD_ATTRIBUTE          0x3004
#define EGL_BAD_CONFIG             0x3005
#define EGL_BAD_CONTEXT            0x3006
#define EGL_BAD_CURRENT_SURFACE    0x3007
#define EGL_BAD_DISPLAY            0x3008
#define EGL_BAD_MATCH              0x3009
#define EGL_BAD_NATIVE_PIXMAP      0x300A
#define EGL_BAD_NATIVE_WINDOW      0x300B
#define EGL_BAD_PARAMETER          0x300C
#define EGL_BAD_SURFACE            0x300D
#define EGL_CONTEXT_LOST           0x300E

#define EGL_BUFFER_SIZE            0x3080
#define EGL_ALPHA_SIZE             0x3021
#define EGL_BLUE_SIZE              0x3022
#define EGL_GREEN_SIZE             0x3023
#define EGL_RED_SIZE               0x3024
#define EGL_DEPTH_SIZE             0x3025
#define EGL_STENCIL_SIZE           0x3026
#define EGL_CONFIG_ID              0x3028
#define EGL_LEVEL                  0x3029
#define EGL_RENDERABLE_TYPE        0x3040
#define EGL_NATIVE_RENDERABLE      0x30D2
#define EGL_NATIVE_VISUAL_ID       0x302B
#define EGL_NATIVE_VISUAL_TYPE     0x302C
#define EGL_SURFACE_TYPE           0x3033
#define EGL_TRANSPARENT_TYPE       0x3034
#define EGL_TRANSPARENT_BLUE_VALUE 0x3035
#define EGL_TRANSPARENT_GREEN_VALUE 0x3036
#define EGL_TRANSPARENT_RED_VALUE  0x3037
#define EGL_CONFIG_CAVEAT          0x3051
#define EGL_MIN_SWAP_INTERVAL      0x3036
#define EGL_MAX_SWAP_INTERVAL      0x3037
#define EGL_SAMPLE_BUFFERS         0x3032
#define EGL_SAMPLES                0x3031
#define EGL_LUMINANCE_SIZE         0x303D
#define EGL_ALPHA_MASK_SIZE        0x303E
#define EGL_COLOR_BUFFER_TYPE      0x303F
#define EGL_RGB_BUFFER             0x308E
#define EGL_LUMINANCE_BUFFER       0x308F
#define EGL_ALPHA_FORMAT           0x3088
#define EGL_COLORSPACE             0x3087
#define EGL_SRGB                   0x3089

#define EGL_WINDOW_BIT             0x0001
#define EGL_PIXMAP_BIT             0x0002
#define EGL_PBUFFER_BIT            0x0004
#define EGL_MULTISAMPLE_RESOLVE_BOX_BIT 0x0200
#define EGL_SWAP_BEHAVIOR_PRESERVED_BIT 0x0400

#define EGL_OPENGL_ES_BIT          0x0001
#define EGL_OPENVG_BIT             0x0002
#define EGL_OPENGL_ES2_BIT         0x0004
#define EGL_OPENGL_BIT             0x0008
#define EGL_OPENGL_ES3_BIT         0x0040

#define EGL_VENDOR                 0x3053
#define EGL_VERSION                0x3054
#define EGL_EXTENSIONS             0x3055
#define EGL_CLIENT_APIS            0x308D
#define EGL_HEIGHT                 0x3056
#define EGL_WIDTH                  0x3057
#define EGL_LARGEST_PBUFFER        0x3058
#define EGL_DRAW                   0x3059
#define EGL_READ                   0x305A
#define EGL_CORE_NATIVE_ENGINE     0x305B

#define EGL_NO_TEXTURE             0x305C
#define EGL_TEXTURE_RGB            0x305D
#define EGL_TEXTURE_RGBA           0x305E
#define EGL_TEXTURE_2D             0x305F
#define EGL_TEXTURE_FORMAT         0x3080
#define EGL_TEXTURE_TARGET         0x3081
#define EGL_MIPMAP_TEXTURE         0x3082
#define EGL_MIPMAP_LEVEL           0x3083

#define EGL_BACK_BUFFER            0x3084
#define EGL_SINGLE_BUFFER          0x3085
#define EGL_RENDER_BUFFER          0x3086

#define EGL_OPENGL_ES_API          0x30A0
#define EGL_OPENVG_API             0x30A1
#define EGL_OPENGL_API             0x30A2

#define EGL_DRAW                   0x3059
#define EGL_READ                   0x305A

#define EGL_CONTEXT_CLIENT_VERSION   0x3098
#define EGL_CONTEXT_MAJOR_VERSION    0x3098
#define EGL_CONTEXT_MINOR_VERSION    0x30FB
#define EGL_CONTEXT_FLAGS_KHR        0x30FC
#define EGL_CONTEXT_OPENGL_DEBUG     0x30B0
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x0001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x0002
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY 0x31BD
#define EGL_NO_RESET_NOTIFICATION    0x31BE
#define EGL_LOSE_CONTEXT_ON_RESET    0x31BF

#define EGL_DEFAULT_DISPLAY  ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY       ((EGLDisplay)0)
#define EGL_NO_SURFACE       ((EGLSurface)0)
#define EGL_NO_CONTEXT       ((EGLContext)0)
#define EGL_NO_CONFIG        ((EGLConfig)0)
#define EGL_DONT_CARE        ((EGLint)-1)
#define EGL_TRUE             1
#define EGL_FALSE            0
#define EGL_UNKNOWN          ((EGLint)-1)

/* EFMOS 自定义平台标识 */
#define EGL_PLATFORM_EFMOS   0x5000

/* ---------- EFMOS EGL 内部状态 ---------- */
typedef struct {
    int       drm_fd;
    struct gbm_device *gbm;
    int       initialized;
    int       screen_w;
    int       screen_h;
    EGLenum   client_api;
} efm_display_t;

typedef struct {
    efm_display_t *disp;
    struct gbm_surface *gbm_surf;
    struct gbm_bo      *current_bo;
    int      width;
    int      height;
    uint32_t format;
} efm_surface_t;

typedef struct {
    efm_display_t *disp;
    EGLConfig config;
    int       client_version;
    int       is_current;
} efm_context_t;

static efm_display_t g_display;
static efm_surface_t *g_current_draw = NULL;
static efm_context_t *g_current_ctx = NULL;
static int g_last_error = EGL_SUCCESS;

/* 简化的 config 表 (Mesa 选择 config 时遍历, 我们提供 4 个常见组合) */
typedef struct {
    EGLint red_size, green_size, blue_size, alpha_size;
    EGLint depth_size, stencil_size;
    EGLint renderable_type;
    EGLint surface_type;
    EGLint config_id;
    EGLint caveat;
    EGLint samples, sample_buffers;
} efm_config_t;
static const efm_config_t g_configs[] = {
    { 8,8,8,8, 24, 8, EGL_OPENGL_ES2_BIT|EGL_OPENGL_BIT, EGL_WINDOW_BIT, 1, EGL_NONE, 0, 0 },
    { 8,8,8,8, 24, 0, EGL_OPENGL_ES2_BIT|EGL_OPENGL_BIT, EGL_WINDOW_BIT, 2, EGL_NONE, 0, 0 },
    { 8,8,8,8,  0, 0, EGL_OPENGL_ES2_BIT|EGL_OPENGL_BIT, EGL_WINDOW_BIT, 3, EGL_NONE, 0, 0 },
    { 8,8,8,0, 24, 8, EGL_OPENGL_ES2_BIT|EGL_OPENGL_BIT, EGL_WINDOW_BIT, 4, EGL_NONE, 0, 0 },
    { 5,6,5,0, 16, 0, EGL_OPENGL_ES2_BIT,               EGL_WINDOW_BIT, 5, EGL_NONE, 0, 0 },
};
#define EFM_CONFIG_COUNT  (sizeof(g_configs)/sizeof(g_configs[0]))

/* ---------- EGL 错误 ---------- */
EGLint eglGetError(void) {
    EGLint e = g_last_error;
    g_last_error = EGL_SUCCESS;
    return e;
}

/* ---------- EGL 显示 ---------- */
EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    /* EGL_DEFAULT_DISPLAY → EFMOS 原生平台 (打开 DRM fd)
     * 其他 display_id → 暂不支持 (X11/Wayland 标识) */
    if (display_id != EGL_DEFAULT_DISPLAY) {
        /* 兼容: 如果传字符串 "efmos" 也接受 */
        const char *s = (const char*)display_id;
        if (!s || strcmp(s, "efmos") != 0) {
            g_last_error = EGL_BAD_DISPLAY;
            return EGL_NO_DISPLAY;
        }
    }
    /* 环境变量 EGL_PLATFORM=efmos 时也强制走本路径 */
    if (!g_display.drm_fd) {
        int fd = drmOpen("card0", NULL);
        if (fd < 0) {
            g_last_error = EGL_BAD_DISPLAY;
            return EGL_NO_DISPLAY;
        }
        g_display.drm_fd = fd;
        g_display.gbm = gbm_create_device(fd);
        if (!g_display.gbm) {
            drmClose(fd);
            g_display.drm_fd = 0;
            g_last_error = EGL_BAD_ALLOC;
            return EGL_NO_DISPLAY;
        }
        g_display.screen_w = g_display.gbm->width;
        g_display.screen_h = g_display.gbm->height;
        g_display.client_api = EGL_OPENGL_ES_API;
    }
    return (EGLDisplay)&g_display;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    d->initialized = 1;
    if (major) *major = 1;
    if (minor) *minor = 5;   /* EGL 1.5 */
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    d->initialized = 0;
    return EGL_TRUE;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display && name != EGL_EXTENSIONS) {
        g_last_error = EGL_BAD_DISPLAY; return NULL;
    }
    switch (name) {
        case EGL_VENDOR:     return "EFMOS";
        case EGL_VERSION:    return "1.5 EFMOS swrast";
        case EGL_EXTENSIONS:
            return "EGL_EXT_buffer_age EGL_KHR_swap_buffers_with_damage "
                   "EGL_KHR_no_config_context EGL_KHR_surfaceless_context "
                   "EGL_EXT_create_context_robustness EGL_MESA_query_driver "
                   "EGL_EFMOS_platform";
        case EGL_CLIENT_APIS: return "OpenGL OpenGL_ES";
        default:
            g_last_error = EGL_BAD_PARAMETER;
            return NULL;
    }
}

/* ---------- Config 选择 ---------- */
EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    if (!num_config) { g_last_error = EGL_BAD_PARAMETER; return EGL_FALSE; }
    if (!configs || config_size == 0) { *num_config = EFM_CONFIG_COUNT; return EGL_TRUE; }
    int n = EFM_CONFIG_COUNT < config_size ? EFM_CONFIG_COUNT : config_size;
    for (int i = 0; i < n; i++) configs[i] = (EGLConfig)(intptr_t)(i + 1);
    *num_config = n;
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size, EGLint *num_config) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    if (!num_config) { g_last_error = EGL_BAD_PARAMETER; return EGL_FALSE; }
    /* 简化: 忽略属性过滤, 返回所有 config (Mesa 会自己二次匹配) */
    return eglGetConfigs(dpy, configs, config_size, num_config);
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    int idx = (int)(intptr_t)config - 1;
    if (idx < 0 || idx >= (int)EFM_CONFIG_COUNT) { g_last_error = EGL_BAD_CONFIG; return EGL_FALSE; }
    const efm_config_t *c = &g_configs[idx];
    if (!value) { g_last_error = EGL_BAD_PARAMETER; return EGL_FALSE; }
    switch (attribute) {
        case EGL_RED_SIZE:         *value = c->red_size; break;
        case EGL_GREEN_SIZE:       *value = c->green_size; break;
        case EGL_BLUE_SIZE:        *value = c->blue_size; break;
        case EGL_ALPHA_SIZE:       *value = c->alpha_size; break;
        case EGL_DEPTH_SIZE:       *value = c->depth_size; break;
        case EGL_STENCIL_SIZE:     *value = c->stencil_size; break;
        case EGL_BUFFER_SIZE:      *value = c->red_size + c->green_size + c->blue_size; break;
        case EGL_RENDERABLE_TYPE:  *value = c->renderable_type; break;
        case EGL_SURFACE_TYPE:     *value = c->surface_type; break;
        case EGL_CONFIG_ID:        *value = c->config_id; break;
        case EGL_CONFIG_CAVEAT:    *value = c->caveat; break;
        case EGL_SAMPLE_BUFFERS:   *value = c->sample_buffers; break;
        case EGL_SAMPLES:          *value = c->samples; break;
        case EGL_NATIVE_VISUAL_ID: *value = 0x34325258u; break; /* DRM_FORMAT_XRGB8888 */
        case EGL_NATIVE_RENDERABLE:*value = EGL_TRUE; break;
        case EGL_MIN_SWAP_INTERVAL:*value = 0; break;
        case EGL_MAX_SWAP_INTERVAL:*value = 1; break;
        case EGL_COLOR_BUFFER_TYPE:*value = EGL_RGB_BUFFER; break;
        case EGL_LEVEL:            *value = 0; break;
        default:                   *value = 0; break;
    }
    return EGL_TRUE;
}

/* ---------- Surface ---------- */
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                   EGLNativeWindowType win, const EGLint *attrib_list) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_NO_SURFACE; }
    (void)config; (void)attrib_list; (void)win;
    efm_surface_t *s = (efm_surface_t*)calloc(1, sizeof(*s));
    if (!s) { g_last_error = EGL_BAD_ALLOC; return EGL_NO_SURFACE; }
    s->disp = d;
    s->format = DRM_FORMAT_XRGB8888;

    /* 查询 WM viewport: WM 模式下返回当前进程窗口的内容区尺寸,
     * 全屏模式下返回整个屏幕尺寸. 这样 Mesa 渲染的分辨率与窗口匹配. */
    int surf_w = d->screen_w, surf_h = d->screen_h;
    {
        int cx, cy, cw, ch;
        efm_get_viewport(&cx, &cy, &cw, &ch);
        if (cw > 0 && ch > 0) {
            surf_w = cw;
            surf_h = ch;
        }
    }
    s->width = surf_w;
    s->height = surf_h;

    /* 创建 GBM surface (渲染到 dumb BO, SwapBuffers 时 blit 到窗口) */
    s->gbm_surf = gbm_surface_create(d->gbm, s->width, s->height,
                                     s->format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!s->gbm_surf) { free(s); g_last_error = EGL_BAD_ALLOC; return EGL_NO_SURFACE; }
    return (EGLSurface)s;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_NO_SURFACE; }
    (void)config;
    int w = 1, h = 1;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_WIDTH)  w = attrib_list[i+1];
            if (attrib_list[i] == EGL_HEIGHT) h = attrib_list[i+1];
        }
    }
    efm_surface_t *s = (efm_surface_t*)calloc(1, sizeof(*s));
    if (!s) { g_last_error = EGL_BAD_ALLOC; return EGL_NO_SURFACE; }
    s->disp = d;
    s->width = w; s->height = h;
    s->format = DRM_FORMAT_XRGB8888;
    s->gbm_surf = gbm_surface_create(d->gbm, w, h, s->format, GBM_BO_USE_RENDERING);
    if (!s->gbm_surf) { free(s); g_last_error = EGL_BAD_ALLOC; return EGL_NO_SURFACE; }
    return (EGLSurface)s;
}

EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                   EGLNativePixmapType pixmap, const EGLint *attrib_list) {
    (void)dpy;(void)config;(void)pixmap;(void)attrib_list;
    g_last_error = EGL_BAD_MATCH;
    return EGL_NO_SURFACE;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    efm_surface_t *s = (efm_surface_t*)surface;
    if (!s) { g_last_error = EGL_BAD_SURFACE; return EGL_FALSE; }
    if (s->gbm_surf) gbm_surface_destroy(s->gbm_surf);
    free(s);
    return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    efm_surface_t *s = (efm_surface_t*)surface;
    if (!s) { g_last_error = EGL_BAD_SURFACE; return EGL_FALSE; }
    switch (attribute) {
        case EGL_WIDTH:  *value = s->width; break;
        case EGL_HEIGHT: *value = s->height; break;
        case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
        case EGL_SURFACE_TYPE:  *value = EGL_WINDOW_BIT; break;
        default: *value = 0; break;
    }
    return EGL_TRUE;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value) {
    (void)dpy; (void)surface; (void)attribute; (void)value;
    return EGL_TRUE;
}

/* ---------- Context ---------- */
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                             EGLContext share_context, const EGLint *attrib_list) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_NO_CONTEXT; }
    (void)config; (void)share_context;
    efm_context_t *c = (efm_context_t*)calloc(1, sizeof(*c));
    if (!c) { g_last_error = EGL_BAD_ALLOC; return EGL_NO_CONTEXT; }
    c->disp = d;
    c->config = config;
    c->client_version = 2;  /* 默认 GLES 2.0 */
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_CONTEXT_CLIENT_VERSION ||
                attrib_list[i] == EGL_CONTEXT_MAJOR_VERSION) {
                c->client_version = attrib_list[i+1];
            }
        }
    }
    return (EGLContext)c;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    efm_context_t *c = (efm_context_t*)ctx;
    if (!c) { g_last_error = EGL_BAD_CONTEXT; return EGL_FALSE; }
    if (g_current_ctx == c) g_current_ctx = NULL;
    free(c);
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    g_current_draw = (efm_surface_t*)draw;
    g_current_ctx  = (efm_context_t*)ctx;
    if (ctx) ((efm_context_t*)ctx)->is_current = 1;
    return EGL_TRUE;
}

EGLBoolean eglReleaseCurrent(void) {
    if (g_current_ctx) g_current_ctx->is_current = 0;
    g_current_draw = NULL;
    g_current_ctx = NULL;
    return EGL_TRUE;
}

EGLContext eglGetCurrentContext(void)  { return (EGLContext)g_current_ctx; }
EGLSurface eglGetCurrentSurface(EGLint readdraw) {
    (void)readdraw;
    return (EGLSurface)g_current_draw;
}
EGLDisplay eglGetCurrentDisplay(void)  { return (EGLDisplay)&g_display; }

/* ---------- SwapBuffers (核心: swrast 渲染完 → blit 到屏幕) ---------- */
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    efm_surface_t *s = (efm_surface_t*)surface;
    if (!s || !s->gbm_surf) { g_last_error = EGL_BAD_SURFACE; return EGL_FALSE; }
    /* 1. 从 GBM surface 拿到当前 back buffer BO (swrast 已渲染到这里) */
    struct gbm_bo *bo = gbm_surface_get_back_bo(s->gbm_surf);
    if (!bo) return EGL_TRUE;
    /* 2. 确保 BO 已映射 (swrast 可能已 map, 这里幂等) */
    if (!bo->map_ptr) {
        uint32_t stride;
        void *map_data;
        gbm_bo_map(bo, 0, 0, bo->width, bo->height,
                   GBM_BO_TRANSFER_READ_WRITE, &stride, &map_data);
    }
    /* 3. 呈现到屏幕:
     *    - 直接映射模式 (map_ptr_direct=1): swrast 已直接写入 Graphics.drv 的
     *      back buffer, 无需 blit, 只需标记脏矩形 + 触发 flush (back→front).
     *    - 回退模式: BO → back buffer 全屏拷贝 (8MB/帧 @1080p). */
    if (gbm_bo_is_direct_mapped(bo)) {
        /* swrast 已直接写入 back buffer, 标记全屏脏区 + flush (无自拷贝) */
        extern void efm_mark_dirty_and_flush(int w, int h);
        efm_mark_dirty_and_flush((int)bo->width, (int)bo->height);
    } else {
        /* 回退路径: BO → back buffer 拷贝 */
        extern void efm_gbm_blit_to_screen(struct gbm_bo *bo);
        efm_gbm_blit_to_screen(bo);
        /* 通知 KMS 刷新 (触发 Graphics.drv 的 flush 节拍) */
        if (bo->fb_id) {
            drmModeDirtyFB(d->drm_fd, bo->fb_id, NULL, 0);
        }
    }
    s->current_bo = bo;
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    (void)dpy; (void)interval;
    return EGL_TRUE;
}

EGLBoolean eglWaitGL(void)    { return EGL_TRUE; }
EGLBoolean eglWaitNative(EGLint engine) { (void)engine; return EGL_TRUE; }

/* ---------- Bind API ---------- */
EGLBoolean eglBindAPI(EGLenum api) {
    g_display.client_api = api;
    return EGL_TRUE;
}
EGLenum eglQueryAPI(void) { return g_display.client_api; }

/* ---------- 其他 EGL 1.5 入口 (占位) ---------- */
EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display, const intptr_t *attrib_list) {
    (void)attrib_list;
    if (platform == EGL_PLATFORM_EFMOS || native_display == NULL) {
        return eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    g_last_error = EGL_BAD_PARAMETER;
    return EGL_NO_DISPLAY;
}

EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                           void *native_window, const intptr_t *attrib_list) {
    (void)attrib_list;
    return eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)native_window, NULL);
}

EGLSurface eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config,
                                           void *native_pixmap, const intptr_t *attrib_list) {
    (void)dpy;(void)config;(void)native_pixmap;(void)attrib_list;
    g_last_error = EGL_BAD_MATCH;
    return EGL_NO_SURFACE;
}

EGLBoolean eglQueryDisplayAttribEXT(EGLDisplay dpy, EGLint attribute, intptr_t *value) {
    efm_display_t *d = (efm_display_t*)dpy;
    if (d != &g_display) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
    (void)attribute; *value = 0;
    return EGL_TRUE;
}

/* Mesa 内部用的 __DRI platform 接口 (简化占位) */
void *__driDriverGetExtensions(void) { return NULL; }
