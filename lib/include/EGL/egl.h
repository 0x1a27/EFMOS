/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Implements the Khronos EGL API (specification is the property of the Khronos Group).
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

/* EGL/egl.h — Khronos EGL 最小子集 (Mesa + 应用层引用) */
#ifndef _EGL_EGL_H_
#define _EGL_EGL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* EGL 类型 */
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
typedef void *EGLLabelKHR;
typedef void (*EGLDEBUGPROCKHR)(EGLenum error,const char *command,EGLint messageType,
                                 EGLLabelKHR threadLabel,EGLLabelKHR objectLabel,const char* message);

/* 常量 */
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
#define EGL_CONFIG_CAVEAT          0x3051
#define EGL_SAMPLE_BUFFERS         0x3032
#define EGL_SAMPLES                0x3031
#define EGL_COLOR_BUFFER_TYPE      0x303F
#define EGL_RGB_BUFFER             0x308E

#define EGL_WINDOW_BIT             0x0001
#define EGL_PIXMAP_BIT             0x0002
#define EGL_PBUFFER_BIT            0x0004

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
#define EGL_DRAW                   0x3059
#define EGL_READ                   0x305A
#define EGL_BACK_BUFFER            0x3084
#define EGL_SINGLE_BUFFER          0x3085
#define EGL_RENDER_BUFFER          0x3086

#define EGL_OPENGL_ES_API          0x30A0
#define EGL_OPENVG_API             0x30A1
#define EGL_OPENGL_API             0x30A2

#define EGL_CONTEXT_CLIENT_VERSION   0x3098
#define EGL_CONTEXT_MAJOR_VERSION    0x3098
#define EGL_CONTEXT_MINOR_VERSION    0x30FB
#define EGL_CONTEXT_FLAGS_KHR        0x30FC
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x0001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x0002

#define EGL_DEFAULT_DISPLAY  ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY       ((EGLDisplay)0)
#define EGL_NO_SURFACE       ((EGLSurface)0)
#define EGL_NO_CONTEXT       ((EGLContext)0)
#define EGL_NO_CONFIG        ((EGLConfig)0)
#define EGL_DONT_CARE        ((EGLint)-1)
#define EGL_TRUE             1
#define EGL_FALSE            0
#define EGL_UNKNOWN          ((EGLint)-1)
#define EGL_NONE             0x3038

/* EFMOS 原生平台 */
#define EGL_PLATFORM_EFMOS   0x5000

/* ---------- API ---------- */
EGLint eglGetError(void);

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id);
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean eglTerminate(EGLDisplay dpy);
const char *eglQueryString(EGLDisplay dpy, EGLint name);

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                   EGLNativeWindowType win, const EGLint *attrib_list);
EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list);
EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                   EGLNativePixmapType pixmap, const EGLint *attrib_list);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);
EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value);

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                             EGLContext share_context, const EGLint *attrib_list);
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLBoolean eglReleaseCurrent(void);
EGLContext eglGetCurrentContext(void);
EGLSurface eglGetCurrentSurface(EGLint readdraw);
EGLDisplay eglGetCurrentDisplay(void);

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLBoolean eglWaitGL(void);
EGLBoolean eglWaitNative(EGLint engine);

EGLBoolean eglBindAPI(EGLenum api);
EGLenum eglQueryAPI(void);

/* EGL 1.5 */
EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display, const intptr_t *attrib_list);
EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                           void *native_window, const intptr_t *attrib_list);
EGLSurface eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config,
                                           void *native_pixmap, const intptr_t *attrib_list);

#ifdef __cplusplus
}
#endif
#endif /* _EGL_EGL_H_ */
