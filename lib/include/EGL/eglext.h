/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Implements the Khronos EGL API (specification is the property of the Khronos Group).
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

/* EGL/eglext.h — EGL 扩展常量 (Mesa 引用子集) */
#ifndef _EGL_EGLEXT_H_
#define _EGL_EGLEXT_H_
#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* KHR_swap_buffers_with_damage */
#define EGL_SWAP_BEHAVIOR_PRESERVED_BIT 0x0400
#define EGL_BUFFER_AGE_KHR              0x378D
#define EGL_EXT_buffer_age               0x313D

/* KHR_no_config_context */
#define EGL_NO_CONFIG_KHR                ((EGLConfig)0)

/* KHR_surfaceless_context */
#define EGL_SURFACELESS_CONTEXT          0x31C7

/* EXT_create_context_robustness */
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT  0x30BF
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT 0x3138
#define EGL_NO_RESET_NOTIFICATION_EXT         0x31BE
#define EGL_LOSE_CONTEXT_ON_RESET_EXT         0x31BF

/* KHR_create_context */
#define EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR      0x0001
#define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR 0x0002
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR 0x0004
#define EGL_CONTEXT_OPENGL_NO_ERROR_KHR       0x31C3
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR 0x31BD
#define EGL_NO_RESET_NOTIFICATION_KHR         0x31BE
#define EGL_LOSE_CONTEXT_ON_RESET_KHR         0x31BF

/* MESA_query_driver */
#define EGL_MESA_query_driver 1

/* EXT_platform_base / EXT_platform_x11 / EXT_platform_wayland (我们用自己的 EFMOS 平台) */
#define EGL_PLATFORM_X11_EXT         0x31D5
#define EGL_PLATFORM_WAYLAND_EXT     0x31D8
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD

/* KHR_image_base */
#define EGL_NATIVE_PIXMAP_KHR         0x30B0
typedef void *EGLImageKHR;

/* Mesa driver query */
const char *eglGetDisplayDriverName(EGLDisplay dpy);
const char **eglGetDisplayDriverConfig(EGLDisplay dpy);

#ifdef __cplusplus
}
#endif
#endif
