/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Mirrors the Linux kernel DRM uapi headers (GPL-2.0-or-later / BSD-2-Clause).
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

/* drm/drm_fourcc.h — Mesa 到处引用 DRM_FORMAT_* 四字符编码 */
#ifndef DRM_FOURCC_H
#define DRM_FOURCC_H
#include <stdint.h>

#define fourcc_code(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                                  ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define DRM_FORMAT_C8           fourcc_code('C', '8', ' ', ' ') /* [7:0] C */
#define DRM_FORMAT_RGB332       fourcc_code('R', 'G', 'B', '8') /* [7:0] R:G:B 3:3:2 */
#define DRM_FORMAT_BGR233       fourcc_code('B', 'G', 'R', '8') /* [7:0] B:G:R 2:3:3 */
#define DRM_FORMAT_XRGB4444     fourcc_code('X', 'R', '1', '2') /* [15:0] x:R:G:B 4:4:4:4 */
#define DRM_FORMAT_XBGR4444     fourcc_code('X', 'B', '1', '2') /* [15:0] x:B:G:R 4:4:4:4 */
#define DRM_FORMAT_RGBX4444     fourcc_code('R', 'X', '1', '2') /* [15:0] R:G:B:x 4:4:4:4 */
#define DRM_FORMAT_BGRX4444     fourcc_code('B', 'X', '1', '2') /* [15:0] B:G:R:x 4:4:4:4 */
#define DRM_FORMAT_ARGB4444     fourcc_code('A', 'R', '1', '2') /* [15:0] A:R:G:B 4:4:4:4 */
#define DRM_FORMAT_ABGR4444     fourcc_code('A', 'B', '1', '2') /* [15:0] A:B:G:R 4:4:4:4 */
#define DRM_FORMAT_RGBA4444     fourcc_code('R', 'A', '1', '2') /* [15:0] R:G:B:A 4:4:4:4 */
#define DRM_FORMAT_BGRA4444     fourcc_code('B', 'A', '1', '2') /* [15:0] B:G:R:A 4:4:4:4 */
#define DRM_FORMAT_XRGB1555     fourcc_code('X', 'R', '1', '5') /* [15:0] x:R:G:B 1:5:5:5 */
#define DRM_FORMAT_XBGR1555     fourcc_code('X', 'B', '1', '5') /* [15:0] x:B:G:R 1:5:5:5 */
#define DRM_FORMAT_ARGB1555     fourcc_code('A', 'R', '1', '5') /* [15:0] A:R:G:B 1:5:5:5 */
#define DRM_FORMAT_ABGR1555     fourcc_code('A', 'B', '1', '5') /* [15:0] A:B:G:R 1:5:5:5 */
#define DRM_FORMAT_RGB565       fourcc_code('R', 'G', '1', '6') /* [15:0] R:G:B 5:6:5 */
#define DRM_FORMAT_BGR565       fourcc_code('B', 'G', '1', '6') /* [15:0] B:G:R 5:6:5 */
#define DRM_FORMAT_RGB888       fourcc_code('R', 'G', '2', '4') /* [23:0] R:G:B 8:8:8 little endian */
#define DRM_FORMAT_BGR888       fourcc_code('B', 'G', '2', '4') /* [23:0] B:G:R 8:8:8 little endian */
#define DRM_FORMAT_XRGB8888     fourcc_code('X', 'R', '2', '4') /* [31:0] x:R:G:B 8:8:8:8 little endian */
#define DRM_FORMAT_XBGR8888     fourcc_code('X', 'B', '2', '4') /* [31:0] x:B:G:R 8:8:8:8 little endian */
#define DRM_FORMAT_RGBX8888     fourcc_code('R', 'X', '2', '4') /* [31:0] R:G:B:x 8:8:8:8 little endian */
#define DRM_FORMAT_BGRX8888     fourcc_code('B', 'X', '2', '4') /* [31:0] B:G:R:x 8:8:8:8 little endian */
#define DRM_FORMAT_ARGB8888     fourcc_code('A', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */
#define DRM_FORMAT_ABGR8888     fourcc_code('A', 'B', '2', '4') /* [31:0] A:B:G:R 8:8:8:8 little endian */
#define DRM_FORMAT_RGBA8888     fourcc_code('R', 'A', '2', '4') /* [31:0] R:G:B:A 8:8:8:8 little endian */
#define DRM_FORMAT_BGRA8888     fourcc_code('B', 'A', '2', '4') /* [31:0] B:G:R:A 8:8:8:8 little endian */
#define DRM_FORMAT_ARGB2101010  fourcc_code('A', 'R', '3', '0') /* [31:0] A:R:G:B 2:10:10:10 little endian */
#define DRM_FORMAT_ABGR2101010  fourcc_code('A', 'B', '3', '0') /* [31:0] A:B:G:R 2:10:10:10 little endian */

/* YUV 常用 */
#define DRM_FORMAT_YUYV         fourcc_code('Y', 'U', 'Y', 'V')
#define DRM_FORMAT_UYVY         fourcc_code('U', 'Y', 'V', 'Y')
#define DRM_FORMAT_NV12         fourcc_code('N', 'V', '1', '2')
#define DRM_FORMAT_NV21         fourcc_code('N', 'V', '2', '1')
#define DRM_FORMAT_YUV420       fourcc_code('Y', 'U', '1', '2')
#define DRM_FORMAT_YVU420       fourcc_code('Y', 'V', '1', '2')

/* modifier 基本值 */
#define DRM_FORMAT_MOD_VENDOR_NONE    0
#define DRM_FORMAT_MOD_LINEAR         0ULL
#define DRM_FORMAT_MOD_INVALID        ((1ULL << 56) - 1)

#define fourcc_mod_code(vendor,val) ((((uint64_t)(vendor))<<56) | ((val) & ((1ULL<<56)-1)))
#endif
