/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Mirrors the libdrm (MIT license) public API.
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

/* xf86drm.h — Mesa 引用的 DRM 上层 API 头 (libdrm 兼容层)
 *
 * 包含:
 *  1. 内核 uapi 结构体 (drm_version, drm_mode_create_dumb 等)
 *     - Mesa 构建时: 优先使用 Mesa 自带的 drm-uapi/ 头, 避免重复定义
 *     - efmlib 独立构建时: 使用我们自己的 drm/drm_mode.h
 *  2. libdrm API 类型 (drmMode*, drmVersion, drmDevice 等)
 *  3. libdrm 函数声明 (drmOpen, drmModeGetResources 等)
 */
#ifndef _XF86DRM_H_
#define _XF86DRM_H_
#include <stdint.h>
#include <sys/types.h>   /* dev_t */

/* ---------- 内核 uapi: 优先 Mesa 自带, 回退到我们的 ---------- */
#if __has_include(<drm-uapi/drm.h>)
/* Mesa 构建: 使用 Mesa 自己的 drm-uapi 内核头, 避免与我们冲突 */
#include <drm-uapi/drm.h>
#include <drm-uapi/drm_mode.h>
#else
/* efmlib 独立构建: 使用我们自己的内核 uapi 定义 */
#include <drm/drm_mode.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- drm fd 打开/关闭 -------------------- */
int drmOpen(const char *name, const char *busid);
int drmOpenControl(const char *busid);
int drmOpenRender(const char *name);
int drmClose(int fd);

/* -------------------- ioctl 分发 -------------------- */
int drmIoctl(int fd, unsigned long request, void *arg);

/* -------------------- Master/Auth/Magic -------------------- */
int drmSetMaster(int fd);
int drmDropMaster(int fd);
int drmAuthMagic(int fd, unsigned long magic);
int drmGetMagic(int fd, unsigned long *magic);
int drmCheckModesettingSupported(const char *busid);
char *drmGetBusid(int fd);
int drmSetBusid(int fd, const char *busid);

/* -------------------- Command 包装 -------------------- */
int drmCommandNone(int fd, unsigned long drm);
int drmCommandRead(int fd, unsigned long drm, void *data, size_t size);
int drmCommandWrite(int fd, unsigned long drm, void *data, size_t size);
int drmCommandWriteRead(int fd, unsigned long drm, void *data, size_t size);

/* -------------------- Prime -------------------- */
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);

/* -------------------- Capabilities -------------------- */
#ifndef DRM_CAP_DUMB_BUFFER
#define DRM_CAP_DUMB_BUFFER         0x1
#endif
#ifndef DRM_CAP_VBLANK_HIGH_CRTC
#define DRM_CAP_VBLANK_HIGH_CRTC    0x2
#endif
#ifndef DRM_CAP_DUMB_PREFERRED_DEPTH
#define DRM_CAP_DUMB_PREFERRED_DEPTH 0x3
#endif
#ifndef DRM_CAP_DUMB_PREFER_SHADOW
#define DRM_CAP_DUMB_PREFER_SHADOW  0x4
#endif
#ifndef DRM_CAP_PRIME
#define DRM_CAP_PRIME               0x5
#define  DRM_PRIME_CAP_IMPORT       0x1
#define  DRM_PRIME_CAP_EXPORT       0x2
#endif
#ifndef DRM_CAP_TIMESTAMP_MONOTONIC
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x6
#endif
#ifndef DRM_CAP_ASYNC_PAGE_FLIP
#define DRM_CAP_ASYNC_PAGE_FLIP     0x7
#endif
#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH        0x8
#endif
#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT       0x9
#endif
#ifndef DRM_CAP_ADDFB2_MODIFIERS
#define DRM_CAP_ADDFB2_MODIFIERS    0x10
#endif
#ifndef DRM_CAP_PAGE_FLIP_TARGET
#define DRM_CAP_PAGE_FLIP_TARGET    0x11
#endif
#ifndef DRM_CAP_CRTC_IN_VBLANK_EVENT
#define DRM_CAP_CRTC_IN_VBLANK_EVENT 0x12
#endif
#ifndef DRM_CAP_SYNCOBJ
#define DRM_CAP_SYNCOBJ             0x13
#endif
#ifndef DRM_CAP_SYNCOBJ_TIMELINE
#define DRM_CAP_SYNCOBJ_TIMELINE    0x14
#endif
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
int drmSetClientCap(int fd, uint64_t capability, uint64_t value);

#ifndef DRM_CLIENT_CAP_STEREO_3D
#define DRM_CLIENT_CAP_STEREO_3D        1
#endif
#ifndef DRM_CLIENT_CAP_UNIVERSAL_PLANES
#define DRM_CLIENT_CAP_UNIVERSAL_PLANES 2
#endif
#ifndef DRM_CLIENT_CAP_ATOMIC
#define DRM_CLIENT_CAP_ATOMIC           3
#endif
#ifndef DRM_CLIENT_CAP_ASPECT_RATIO
#define DRM_CLIENT_CAP_ASPECT_RATIO     4
#endif
#ifndef DRM_CLIENT_CAP_WRITEBACK_CONNECTORS
#define DRM_CLIENT_CAP_WRITEBACK_CONNECTORS 5
#endif
#ifndef DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT
#define DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT 6
#endif

/* -------------------- Version ioctl -------------------- */
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
drmVersionPtr drmGetVersion(int fd);
void          drmFreeVersion(drmVersionPtr v);

/* -------------------- DRM device enumeration (loader.c) -------------------- */
#define DRM_BUS_PCI            0
#define DRM_BUS_USB            1
#define DRM_BUS_PLATFORM       2
#define DRM_BUS_HOST1X         3

#define DRM_PLATFORM_DEVICE_NAME_LEN 512
#define DRM_HOST1X_DEVICE_NAME_LEN   512
#define DRM_DEVICE_GET_PCI_REVISION  (1 << 0)

typedef struct _drmPciBusInfo {
    uint16_t domain;
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
} drmPciBusInfo, *drmPciBusInfoPtr;

typedef struct _drmPciDeviceInfo {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subvendor_id;
    uint16_t subdevice_id;
    uint8_t  revision_id;
} drmPciDeviceInfo, *drmPciDeviceInfoPtr;

typedef struct _drmPlatformBusInfo {
    char fullname[DRM_PLATFORM_DEVICE_NAME_LEN];
} drmPlatformBusInfo, *drmPlatformBusInfoPtr;

typedef struct _drmHost1xBusInfo {
    char fullname[DRM_HOST1X_DEVICE_NAME_LEN];
} drmHost1xBusInfo, *drmHost1xBusInfoPtr;

typedef struct _drmDevice {
    char **nodes;
    int    available_nodes;
    int    bustype;
    union {
        drmPciBusInfoPtr      pci;
        drmPlatformBusInfoPtr platform;
        drmHost1xBusInfoPtr   host1x;
    } businfo;
    union {
        drmPciDeviceInfoPtr pci;
    } deviceinfo;
} drmDevice, *drmDevicePtr;

int  drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices);
void drmFreeDevices(drmDevicePtr devices[], int count);
int  drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device);
void drmFreeDevice(drmDevicePtr *device);
int  drmGetDeviceFromDevId(dev_t dev_id, uint32_t flags, drmDevicePtr *device);
char *drmGetDeviceNameFromFd2(int fd);
char *drmGetPrimaryDeviceNameFromFd(int fd);
char *drmGetRenderDeviceNameFromFd(int fd);
int  drmDevicesEqual(drmDevicePtr a, drmDevicePtr b);

/* -------------------- Misc -------------------- */
#define DRM_NODE_PRIMARY   0
#define DRM_NODE_CONTROL   1
#define DRM_NODE_RENDER    2
#define DRM_NODE_MAX       3

/* DRM device directory (libdrm 标准) */
#define DRM_DIR_NAME       "/dev/dri"
#define DRM_DEV_NAME       "%s/card%d"
#define DRM_CONTROL_DEV_NAME  "%s/controlD%d"
#define DRM_RENDER_DEV_NAME   "%s/renderD%d"

/* ======================================================================
 * libdrm Mode API (drmMode*) — 原 drm_mode.h 中的 libdrm 类型
 * 这些是 libdrm 用户态 API, 不在内核 uapi 中, 由我们提供.
 * ====================================================================== */

#ifndef DRM_DISPLAY_MODE_LEN
#define DRM_DISPLAY_MODE_LEN    32
#endif

typedef struct drmModeModeInfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char     name[DRM_DISPLAY_MODE_LEN];
} drmModeModeInfo, *drmModeModeInfoPtr;

/* mode flags / type (libdrm 也可用, 如果内核 uapi 已定义则跳过) */
#ifndef DRM_MODE_FLAG_PHSYNC
#define DRM_MODE_FLAG_PHSYNC            (1 << 0)
#define DRM_MODE_FLAG_NHSYNC            (1 << 1)
#define DRM_MODE_FLAG_PVSYNC            (1 << 2)
#define DRM_MODE_FLAG_NVSYNC            (1 << 3)
#define DRM_MODE_FLAG_INTERLACE         (1 << 4)
#define DRM_MODE_FLAG_DBLSCAN           (1 << 5)
#define DRM_MODE_FLAG_PIXMUX            (1 << 12)
#define DRM_MODE_FLAG_3D_MASK           (0x1f << 27)
#define DRM_MODE_TYPE_PREFERRED          (1<<3)
#define DRM_MODE_TYPE_DRIVER             (1<<6)
#define DRM_MODE_TYPE_USERDEF            (1<<5)
#endif

/* connector status */
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED         1
#define DRM_MODE_DISCONNECTED      2
#define DRM_MODE_UNKNOWNCONNECTION 3
#endif

/* subconnector types (libdrm 用) */
#ifndef DRM_MODE_SUBCONNECTOR_Unknown
#define DRM_MODE_SUBCONNECTOR_Unknown 0
#endif

/* connector types */
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

/* subpixel */
#ifndef DRM_MODE_SUBPIXEL_UNKNOWN
#define DRM_MODE_SUBPIXEL_UNKNOWN      1
#define DRM_MODE_SUBPIXEL_HORIZONTAL_RGB 2
#define DRM_MODE_SUBPIXEL_HORIZONTAL_BGR 3
#define DRM_MODE_SUBPIXEL_VERTICAL_RGB   4
#define DRM_MODE_SUBPIXEL_VERTICAL_BGR   5
#define DRM_MODE_SUBPIXEL_NONE           6
#endif

/* --------------- Resources --------------- */
typedef struct drmModeRes {
    int        count_fbs;
    uint32_t  *fbs;
    int        count_crtcs;
    uint32_t  *crtcs;
    int        count_connectors;
    uint32_t  *connectors;
    int        count_encoders;
    uint32_t  *encoders;
    uint32_t   min_width, max_width;
    uint32_t   min_height, max_height;
    uint32_t   width, height;
    uint32_t   size_dp;
} drmModeRes, *drmModeResPtr;

/* --------------- Encoder --------------- */
typedef struct drmModeEncoder {
    uint32_t  encoder_id;
    uint32_t  encoder_type;
    uint32_t  crtc_id;
    uint32_t  possible_crtcs;
    uint32_t  possible_clones;
    int       count_connectors;
    uint32_t *connectors;
} drmModeEncoder, *drmModeEncoderPtr;

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

/* --------------- CRTC --------------- */
typedef struct drmModeCrtc {
    uint32_t          crtc_id;
    uint32_t          buffer_id;
    uint32_t          x, y;
    uint32_t          width, height;
    int               mode_valid;
    drmModeModeInfo   mode;
    int               gamma_size;
    int               count_connectors;
    uint32_t         *connectors;
} drmModeCrtc, *drmModeCrtcPtr;

/* --------------- Framebuffer --------------- */
typedef struct drmModeFB {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
    uint32_t pixel_format;
} drmModeFB, *drmModeFBPtr;

typedef struct drmModeFB2 {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pixel_format;
    uint64_t modifier;
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
} drmModeFB2, *drmModeFB2Ptr;

/* --------------- Connector --------------- */
typedef struct drmModeConnector {
    uint32_t connector_id;
    uint32_t encoder_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    int      connection;
    uint32_t mmWidth, mmHeight;
    uint32_t subpixel;

    int               count_modes;
    drmModeModeInfoPtr modes;
    int               count_encoders;
    uint32_t         *encoders;
    int               count_props;
    uint32_t         *props;
    uint64_t         *prop_values;
} drmModeConnector, *drmModeConnectorPtr;

/* --------------- Plane --------------- */
typedef struct drmModePlaneRes {
    uint32_t  count_planes;
    uint32_t *planes;
} drmModePlaneRes, *drmModePlaneResPtr;

typedef struct drmModePlane {
    uint32_t  count_formats;
    uint32_t *formats;
    uint32_t  plane_id;
    uint32_t  crtc_id;
    uint32_t  fb_id;
    uint32_t  possible_crtcs;
    uint32_t  gamma_size;
    uint32_t  x, y;
    uint32_t  crtc_x, crtc_y;
    uint32_t  crtc_w, crtc_h;
    uint32_t  src_x, src_y;
    uint32_t  src_w, src_h;
} drmModePlane, *drmModePlanePtr;

/* --------------- Property / Blob --------------- */
typedef struct drmModePropertyBlob {
    uint32_t blob_id;
    uint32_t length;
    void    *data;
} drmModePropertyBlob, *drmModePropertyBlobPtr;

typedef struct drmModePropertyRes {
    uint32_t prop_id;
    uint32_t flags;
    char     name[64];
    int      count_values;
    uint64_t *values;
    int      count_enums;
    struct { char name[64]; uint64_t value; } *enums;
    int      count_blobs;
    uint32_t *blob_ids;
} drmModePropertyRes, *drmModePropertyPtr;

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

typedef struct drmModeObjectProperties {
    uint32_t  count_props;
    uint32_t *props;
    uint64_t *prop_values;
} drmModeObjectProperties, *drmModeObjectPropertiesPtr;

typedef struct drmModeAtomicReq drmModeAtomicReq, *drmModeAtomicReqPtr;
typedef struct drmModeLessee drmModeLessee, *drmModeLesseePtr;

/* --------------- Mode API 函数 --------------- */
drmModeResPtr drmModeGetResources(int fd);
void          drmModeFreeResources(drmModeResPtr ptr);

drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtcId);
void           drmModeFreeCrtc(drmModeCrtcPtr ptr);
int            drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                              uint32_t x, uint32_t y,
                              uint32_t *connectors, int count,
                              drmModeModeInfoPtr mode);
int            drmModeCrtcSetGamma(int fd, uint32_t crtc_id, uint32_t size,
                                   uint16_t *r, uint16_t *g, uint16_t *b);
int            drmModeCrtcGetGamma(int fd, uint32_t crtc_id, uint32_t size,
                                   uint16_t *r, uint16_t *g, uint16_t *b);

drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t encoder_id);
void              drmModeFreeEncoder(drmModeEncoderPtr ptr);

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connectorId);
int                 drmModeAttachMode(int fd, uint32_t connector_id, drmModeModeInfoPtr mode_info);
int                 drmModeDetachMode(int fd, uint32_t connector_id, drmModeModeInfoPtr mode_info);
void                drmModeFreeConnector(drmModeConnectorPtr ptr);

/* FB */
drmModeFBPtr  drmModeGetFB(int fd, uint32_t fbId);
drmModeFB2Ptr drmModeGetFB2(int fd, uint32_t fbId);
void          drmModeFreeFB(drmModeFBPtr ptr);
void          drmModeFreeFB2(drmModeFB2Ptr ptr);
int           drmModeAddFB(int fd, uint32_t width, uint32_t height,
                           uint8_t depth, uint8_t bpp, uint32_t pitch,
                           uint32_t bo_handle, uint32_t *buf_id);
int           drmModeAddFB2(int fd, uint32_t width, uint32_t height,
                            uint32_t pixel_format, uint32_t bo_handles[4],
                            uint32_t pitches[4], uint32_t offsets[4],
                            uint32_t *buf_id, uint32_t flags);
int           drmModeAddFB2WithModifiers(int fd, uint32_t width, uint32_t height,
                                         uint32_t pixel_format, uint32_t bo_handles[4],
                                         uint32_t pitches[4], uint32_t offsets[4],
                                         uint64_t modifier[4],
                                         uint32_t *buf_id, uint32_t flags);
int           drmModeRmFB(int fd, uint32_t fbId);
int           drmModeDirtyFB(int fd, uint32_t fbId, void *clips, uint32_t num_clips);

/* Planes */
drmModePlaneResPtr drmModeGetPlaneResources(int fd);
void               drmModeFreePlaneResources(drmModePlaneResPtr ptr);
drmModePlanePtr    drmModeGetPlane(int fd, uint32_t plane_id);
void               drmModeFreePlane(drmModePlanePtr ptr);
int                drmModeSetPlane(int fd, uint32_t plane_id, uint32_t crtc_id,
                                   uint32_t fb_id, uint32_t flags,
                                   int32_t crtc_x, int32_t crtc_y,
                                   uint32_t crtc_w, uint32_t crtc_h,
                                   uint32_t src_x, uint32_t src_y,
                                   uint32_t src_w, uint32_t src_h);
int                drmModeDisablePlane(int fd, uint32_t plane_id);

/* Property */
drmModePropertyPtr     drmModeGetProperty(int fd, uint32_t prop_id);
void                   drmModeFreeProperty(drmModePropertyPtr ptr);
drmModePropertyBlobPtr drmModeGetPropertyBlob(int fd, uint32_t blob_id);
void                   drmModeFreePropertyBlob(drmModePropertyBlobPtr ptr);
drmModeObjectPropertiesPtr drmModeObjectGetProperties(int fd, uint32_t id, uint32_t type);
void                       drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr);
int                        drmModeObjectSetProperty(int fd, uint32_t id, uint32_t type,
                                                    uint32_t prop_id, uint64_t value);

#ifndef DRM_MODE_OBJECT_CRTC
#define DRM_MODE_OBJECT_CRTC       0xCCCCCCCC
#endif
#ifndef DRM_MODE_OBJECT_CONNECTOR
#define DRM_MODE_OBJECT_CONNECTOR  0xC0C0C0C0
#endif
#ifndef DRM_MODE_OBJECT_ENCODER
#define DRM_MODE_OBJECT_ENCODER    0xE0E0E0E0
#endif
#ifndef DRM_MODE_OBJECT_MODE
#define DRM_MODE_OBJECT_MODE       0xDEDEDEDE
#endif
#ifndef DRM_MODE_OBJECT_PROPERTY
#define DRM_MODE_OBJECT_PROPERTY   0xB0B0B0B0
#endif
#ifndef DRM_MODE_OBJECT_FB
#define DRM_MODE_OBJECT_FB         0xFBFBFBFB
#endif
#ifndef DRM_MODE_OBJECT_BLOB
#define DRM_MODE_OBJECT_BLOB       0xBBBBBBBB
#endif
#ifndef DRM_MODE_OBJECT_PLANE
#define DRM_MODE_OBJECT_PLANE      0xEEEEEEEE
#endif
#ifndef DRM_MODE_OBJECT_ANY
#define DRM_MODE_OBJECT_ANY        0
#endif

/* page flip / event */
typedef void (*drmEventContextVBlankPtr)(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data);
typedef void (*drmEventContextPageFlipPtr)(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data);
typedef struct _drmEventContext {
    int version;
    drmEventContextVBlankPtr vblank_handler;
    drmEventContextPageFlipPtr page_flip_handler;
} drmEventContext, *drmEventContextPtr;
#define DRM_EVENT_CONTEXT_VERSION 2
int drmHandleEvent(int fd, drmEventContextPtr evctx);

#ifndef DRM_MODE_PAGE_FLIP_EVENT
#define DRM_MODE_PAGE_FLIP_EVENT 0x01
#define DRM_MODE_PAGE_FLIP_ASYNC 0x02
#endif
int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void *user_data);
int drmModePageFlipTarget(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags,
                          void *user_data, uint32_t target);

/* Atomic */
drmModeAtomicReqPtr drmModeAtomicAlloc(void);
int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t obj_id,
                             uint32_t prop_id, uint64_t value);
int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user);
void drmModeAtomicFree(drmModeAtomicReqPtr req);
#ifndef DRM_MODE_ATOMIC_TEST_ONLY
#define DRM_MODE_ATOMIC_TEST_ONLY    (1<<0)
#define DRM_MODE_ATOMIC_NONBLOCK     (1<<1)
#define DRM_MODE_ATOMIC_ALLOW_MODESET (1<<2)
#define DRM_MODE_ATOMIC_PAGE_FLIP_EVENT (1<<3)
#endif

/* SyncObj */
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);

#ifdef __cplusplus
}
#endif
#endif /* _XF86DRM_H_ */
