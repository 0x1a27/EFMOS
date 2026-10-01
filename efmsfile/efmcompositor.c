/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS - a 64-bit x86_64 UEFI operating system written in C.
 *
 * Copyright (C) 2026 0x1a27
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* efmcompositor.c - EFMOS Mesa 合成器 (编译为 efmcompositor.efs)
 * 加载地址: 0x800000 (8MB, 不与其他 .efs 冲突)
 * 入口: _start
 *
 * 【Mesa GL 渲染】
 * 登录后由内核 spawn_async 启动, 接管桌面渲染:
 *   - 优先通过 dlsym 加载 Mesa EGL/GL 函数, 用 eglCreateContext + glClear/glBegin 真正调用 Mesa swrast
 *   - EGL 初始化失败时回退到软件光栅化 (fill_rect/draw_char)
 *   - 通过 get_wm_snapshot 读取内核 WM 窗口状态 (位置/标题/内容/鼠标)
 *   - GL 路径: glClear → glBegin(GL_QUADS) 画窗口 → glEnd → eglSwapBuffers
 *     eglSwapBuffers → GBM BO → efm_gbm_blit_to_screen → Graphics.drv back buffer
 *   - 软件路径: fill_rect/draw_char → canvas → blit_to_window → Graphics.drv back buffer
 *   - 内核 WM 退化为窗口状态管理, 不再直接写 framebuffer
 *
 * 渲染管线 (GL 路径, 与 Mesa EGL swap 等价):
 *   glClear/glBegin (Mesa swrast) → GBM BO mmap → efm_gbm_blit_to_screen
 *   → Graphics.drv blit_buffer (写 back buffer + 脏矩形)
 *   → flush (back → front, 32-bit burst, 无撕裂)
 *
 * 早期引导 (bootloader → kernel console → efmlogin) 仍用内核原渲染机制.
 */

/* 引入 boot logo alpha 数据 (200x200, 来自 EFMOS-logo-b.png).
 * static 数组 = 40000 字节 → .rodata. efmcompositor.efs 约 64KB 限制,
 * 如有溢出风险需替换为运行时从 /EFMOS/logo.bin file_read 读取. */
#include "bootlogo.h"

/* ========== 内核 API 表 (必须与 kernel.c struct kernel_api 完全一致) ========== */
struct efs_dirent {
    char name[64];
    unsigned int size;
    unsigned int is_dir;
};

/* WM 共享状态结构 (必须与 kernel.c 一致) */
#define EFM_WM_MAX_WIN    32
#define EFM_WM_TITLE_LEN  64
#define EFM_WM_TBUF_COLS  120
#define EFM_WM_TBUF_ROWS  60

/* EFS 图形信息共享结构 (必须与 kernel.c 一致) */
#define EFM_GFX_MAGIC     0x45464758u
#define EFM_GFX_MAX_RECTS 32
#define EFM_GFX_MAX_TEXTS 32

struct efm_gfx_rect {
    int x, y, w, h;
    unsigned int fill_color;
    unsigned int border_color;
    int has_border;
};

struct efm_gfx_text {
    int x, y;
    unsigned int color;
    char text[96];
};

struct efm_gfx_info {
    unsigned int magic;
    volatile unsigned int seq;
    int rect_count;
    struct efm_gfx_rect rects[EFM_GFX_MAX_RECTS];
    int text_count;
    struct efm_gfx_text texts[EFM_GFX_MAX_TEXTS];
};

struct efm_wm_win_info {
    int wid;
    int x, y, w, h;
    char title[EFM_WM_TITLE_LEN];
    int z_order;
    int focused;
    int c_cx, c_cy;
    unsigned int c_fg, c_bg;
    int alive;
    int close_hover;
    int has_efm_gfx;
    int tbuf_cols, tbuf_rows;
    char *tbuf_ptr;
    struct efm_gfx_info *gfx_info_ptr;  /* EFS 图形信息 (新架构) */
};

struct efm_wm_snapshot {
    unsigned int magic;
    unsigned int screen_w, screen_h;
    int mouse_x, mouse_y, mouse_btn, mouse_visible;
    int window_count;
    int close_hover_wid;
    int taskbar_h;
    int title_h;
    int font_w, font_h;
    /* [EFS 图形脏检测计数器] 与 kernel 端 g_wm_gfx_dirty_seq 同步。
     * EFS 程序 (userman/efmlogin 等) 每次调用 fill_rect/draw_rect/put_pixel
     * 写入 back buffer 时, 内核使该字段 +1; Compositor 每帧对比新旧值,
     * 变化时强制重绘所有窗口内容区 (从 back buffer 复制最新像素到 canvas),
     * 避免"合成器脏检测漏检 EFS 图形变化 → canvas blit 覆盖按钮/图标"。 */
    unsigned int gfx_dirty_seq;
    struct efm_wm_win_info windows[EFM_WM_MAX_WIN];
    /* [开始菜单状态] 由 kernel wm_handle_mouse 维护, compositor 读取后渲染弹出菜单 */
    int menu_open;        /* 1 = 菜单已打开, 0 = 关闭 */
    int menu_hover_idx;   /* 当前鼠标悬停的菜单项索引 (-1 = 无) */
    int menu_item_count;  /* 菜单项总数 (来自 efmAether 的 menu_count) */
};

struct kernel_api {
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
    int  (*file_list)(const char *dir_path, struct efs_dirent *out, int max_count);
    int  (*file_delete)(const char *path);
    int  (*key_poll)(void);
    int  (*get_current_user)(char *buf, int bufsz);
    int  (*set_current_user)(const char *username);
    int  (*user_list)(struct efs_dirent *out, int max_count);
    int  (*user_create)(const char *username);
    int  (*user_delete)(const char *username);
    void *(*malloc)(unsigned long);
    void  (*free)(void*);
    int   (*spawn)(const char *name, const char *args);
    int   (*get_args)(char *buf, int max);
    int font_w;
    int font_h;
    int current_pid;
    int wm_enabled;
    void (*put_pixel)(int x, int y, unsigned int c);
    void (*fill_rect)(int x1, int y1, int x2, int y2, unsigned int c);
    void (*draw_rect)(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill);
    void (*get_viewport)(int *cx, int *cy, int *cw, int *ch);
    void (*get_fb_info)(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base);
    int  (*blit_to_window)(const void *src, int src_w, int src_h, int src_pitch);
    int   (*load_driver)(const char *path);
    int   (*driver_count)(void);
    int   (*driver_list)(char out_names[][32], int max);
    void  (*sleep_ms)(unsigned long ms);
    void  (*yield)(void);
    int   (*get_pid)(void);
    int   (*spawn_async)(const char *name, const char *args);
    int   (*set_priority)(int pid, int nice);
    int   (*get_wm_snapshot)(void *out, int max_bytes);
    void  (*set_compositor_active)(int active);
    void *(*dlsym)(const char *name);   /* 从已加载 SO 解析符号 (Mesa EGL/GL) */
    int   (*get_backbuffer)(void **out_ptr, int *out_pitch, int *out_w, int *out_h);
    void  (*mark_dirty_rect)(int x1, int y1, int x2, int y2);
    void  (*flush_now)(void);
    int   (*draw_char_unicode)(int x, int y, unsigned int codepoint,
                               unsigned int fg, unsigned int bg, int cell_w, int cell_h);
    void  (*set_gfx_info)(void *info);
};

#define API_MAGIC  0xEF110001
#define API        ((volatile struct kernel_api*)0x9000)

/* ========== 渲染信息共享结构 (0x6000) ==========
 * 【职责分工】
 *   - efmAether: 仅负责"提供桌面渲染信息" (UI 配置/布局/文案), 不做任何像素操作
 *     (不写 framebuffer/canvas, 不 blit, 不 flush, 不进行任何绘制调用).
 *   - efmcompositor: 唯一的渲染执行者, 读取此结构 + 自己的 get_wm_snapshot,
 *     用 Canvas + API->draw_char_unicode (TTF, UTF-8, CJK, SSAA) 完成所有像素渲染.
 * 【流程】
 *   efmAether 写 aether_render_info → magic=AETHER_INFO_MAGIC → seq 每更新 +1
 *   → efmcompositor 检测 magic/seq → 读取颜色+文字 → 渲染桌面
 *   → 彻底消除两套渲染冲突 (efmAether 不再写像素!) */
#define AETHER_INFO_MAGIC  0x41455448u  /* "AETH" */
#define AETHER_INFO_ADDR   0x6000
#define COMP_BACKEND_ADDR_OLD 0x6000    /* 旧 compositor_backend 原址, 已被 aether_render_info 替换 */
#define COMP_BACKEND_ADDR_NEW 0x7000    /* 兼容旧 efmAether.efs 时的备用地址 (本版本不再使用) */
#define AETHER_MAX_TEXT    16            /* 最多自定义文字条目 */
#define AETHER_MAX_MENU    16            /* 最多开始菜单项 */

struct aether_text_item {
    int x, y;
    unsigned int color;
    char text[96];   /* UTF-8 */
};

/* 开始菜单项: label = 显示文字, program = 启动的 .efs 程序名 */
struct aether_menu_item {
    char label[32];     /* UTF-8 显示文字 */
    char program[32];    /* /EFMOS/<program>.efs */
};

struct aether_render_info {
    unsigned int magic;                /* AETHER_INFO_MAGIC = 信息已就绪 */
    volatile unsigned int seq;         /* efmAether 每更新一次 +1 */
    int font_w, font_h;                /* TTF 字体单元格尺寸 */
    /* 颜色方案 (可由 efmAether 配置主题) */
    unsigned int clr_bg;
    unsigned int clr_desktop_text;
    unsigned int clr_border;
    unsigned int clr_title_active;
    unsigned int clr_title_inact;
    unsigned int clr_title_text;
    unsigned int clr_close_bg;
    unsigned int clr_close_hover;
    unsigned int clr_close_text;
    unsigned int clr_taskbar_bg;
    unsigned int clr_taskbar_btn;
    unsigned int clr_taskbar_act;
    /* 开始菜单颜色方案 */
    unsigned int clr_menu_bg;
    unsigned int clr_menu_item;
    unsigned int clr_menu_hover;
    unsigned int clr_menu_text;
    unsigned int clr_menu_border;
    /* 桌面标题文字 */
    struct aether_text_item desktop_title;
    /* 任务栏 Start 按钮文字 */
    struct aether_text_item start_text;
    /* 可扩展: 桌面图标文字, 小部件等 */
    struct aether_text_item extra[AETHER_MAX_TEXT];
    int extra_count;
    /* 开始菜单项 */
    struct aether_menu_item menu_items[AETHER_MAX_MENU];
    int menu_count;
};

/* ========== WM 颜色方案 (与 kernel.c WM_CLR_* 一致) ========== */
#define CLR_BG            0x001A1A2E   /* 桌面背景: 深蓝紫 */
#define CLR_DESKTOP_TEXT  0x00FFFFFF
#define CLR_BORDER        0x00444454
#define CLR_TITLE_ACTIVE  0x001976D2   /* 激活标题栏: 蓝 */
#define CLR_TITLE_INACT   0x0037474F   /* 非激活标题栏: 蓝灰 */
#define CLR_TITLE_TEXT    0x00FFFFFF
#define CLR_CLOSE_BG      0x00505050
#define CLR_CLOSE_HOVER   0x00C62828
#define CLR_CLOSE_TEXT    0x00FFFFFF
#define CLR_TASKBAR_BG    0x00263238
#define CLR_TASKBAR_BTN   0x0037474F
#define CLR_TASKBAR_ACT   0x001976D2

/* ========== 8x16 ASCII 点阵字体 (Codepage 437, 0x20..0x7E) ========== */
static const unsigned char g_font8x16[96][16] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00},
    {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    {0x30,0x30,0x30,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x38,0x38,0x38,0x30,0x60,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x38,0x38,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xF6,0xE6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x10,0x30,0xF0,0x10,0x10,0x10,0x10,0x10,0x10,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xFE,0xC0,0xC0,0xBC,0xC6,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x38,0x60,0xC0,0xC0,0xBC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xFE,0x06,0x06,0x0C,0x18,0x30,0x30,0x60,0x60,0x60,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x38,0x38,0x00,0x00,0x00,0x38,0x38,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x38,0x38,0x00,0x00,0x00,0x38,0x30,0x60,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x0C,0x18,0x30,0x60,0xC0,0x60,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xE6,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x62,0x66,0x66,0x66,0xFE,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xCE,0xCE,0xDE,0x7C,0x0C,0x0E,0x00,0x00},
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0x7C,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x7C,0x6C,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xC6,0xC6,0x6C,0x38,0x18,0x38,0x6C,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xFE,0xCE,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x7C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00},
    {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x7C,0x06,0x3E,0x46,0x46,0x46,0x3E,0x06,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xE0,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC0,0xC6,0x7C,0x06,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x6E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x38,0x6C,0x64,0x60,0x78,0x60,0x60,0x60,0x60,0x78,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x7E,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x0C,0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00},
    {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0xF0,0x60,0x60,0x00,0x00},
    {0x00,0x00,0x00,0x7E,0x66,0x66,0x66,0x7E,0x06,0x06,0x06,0x06,0x06,0x06,0x00,0x00},
    {0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x30,0x34,0x18,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCE,0x6C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x7C,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xCE,0x7E,0x06,0x0C,0xF8,0x00},
    {0x00,0x00,0x00,0xFE,0x8C,0x18,0x30,0x60,0xC0,0x86,0xFE,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1C,0x30,0x30,0x30,0xE0,0x30,0x30,0x30,0x1C,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18},
    {0x00,0x00,0xE0,0x30,0x30,0x30,0x1C,0x30,0x30,0x30,0xE0,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x70,0xD8,0x0C,0x06,0x06,0x0C,0xD8,0x70,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

#define FONT_W  10
#define FONT_H  18
/* 基础字形尺寸 (8x16 VGA 点阵, 上采样到 FONT_W x FONT_H) */
#define FONT_BASE_W  8
#define FONT_BASE_H  16

/* ========== Mesa EGL/GL 函数指针类型 ==========
 * 通过 API->dlsym 从已加载的 libEGL.so.1 / libgallium-25.2.so.1 解析.
 * 合成器是 flat binary, 不能动态链接, 所以用函数指针调用 Mesa. */

/* EGL 类型 (最小子集) */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef int   EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef void *EGLNativeDisplayType;
typedef void *EGLNativeWindowType;
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY  ((EGLDisplay)0)
#define EGL_NO_SURFACE  ((EGLSurface)0)
#define EGL_NO_CONTEXT  ((EGLContext)0)
#define EGL_NONE        0x3038
#define EGL_TRUE        1
#define EGL_FALSE       0
#define EGL_OPENGL_API  0x30A2
#define EGL_RED_SIZE    0x3024
#define EGL_GREEN_SIZE  0x3023
#define EGL_BLUE_SIZE   0x3022
#define EGL_ALPHA_SIZE  0x3021
#define EGL_SURFACE_TYPE         0x3033
#define EGL_WINDOW_BIT           0x0001
#define EGL_RENDERABLE_TYPE      0x3040
#define EGL_OPENGL_BIT           0x0008
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x0002
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD

/* GL 类型 (最小子集) */
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef int   GLint;
typedef unsigned int GLuint;
typedef long GLsizeiptr;      /* ptrdiff_t 等价 (flat binary 无 stddef.h) */
#ifndef NULL
#define NULL ((void*)0)
#endif
#define GL_COLOR_BUFFER_BIT   0x4000
#define GL_TRIANGLES          0x0004
#define GL_QUADS              0x0007
#define GL_PROJECTION         0x1701
#define GL_MODELVIEW          0x1700
#define GL_DEPTH_TEST         0x0B71
/* VBO */
#define GL_ARRAY_BUFFER       0x8892
#define GL_STATIC_DRAW        0x88E4
#define GL_DYNAMIC_DRAW       0x88E8
/* FBO */
#define GL_FRAMEBUFFER        0x8D40
#define GL_COLOR_ATTACHMENT0  0x8CE0
#define GL_TEXTURE_2D         0x0DE1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_LINEAR             0x2601
#define GL_NEAREST            0x2600
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_READ_FRAMEBUFFER   0x8CA8
#define GL_DRAW_FRAMEBUFFER   0x8CA9
#define GL_TEXTURE0           0x84C0
/* Alpha blend (鼠标光标抗锯齿) */
#define GL_BLEND              0x0BE2
#define GL_SRC_ALPHA          0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_TEXTURE_ENV        0x2300
#define GL_TEXTURE_ENV_MODE   0x2200
#define GL_MODULATE           0x2100
#define GL_REPLACE            0x1E02
#define GL_UNSIGNED_BYTE      0x1401
#define GL_RGBA               0x1908
#define GL_FLOAT              0x1406

/* 函数指针变量 */
static EGLDisplay (*p_eglGetDisplay)(EGLNativeDisplayType);
static EGLBoolean (*p_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
static EGLBoolean (*p_eglBindAPI)(EGLenum);
static EGLBoolean (*p_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
static EGLSurface (*p_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
static EGLContext (*p_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
static EGLBoolean (*p_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
static EGLBoolean (*p_eglSwapBuffers)(EGLDisplay, EGLSurface);

static void (*p_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*p_glClear)(GLbitfield);
static void (*p_glBegin)(GLenum);
static void (*p_glEnd)(void);
static void (*p_glVertex2f)(GLfloat, GLfloat);
static void (*p_glColor3f)(GLfloat, GLfloat, GLfloat);
static void (*p_glViewport)(GLint, GLint, GLint, GLint);
static void (*p_glMatrixMode)(GLenum);
static void (*p_glLoadIdentity)(void);
static void (*p_glOrtho)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
static void (*p_glEnable)(GLenum);
static void (*p_glDisable)(GLenum);
/* [优化] VBO 批量渲染: 一次 glDrawArrays 替代多次 glBegin/glEnd */
static void (*p_glGenBuffers)(GLint, GLuint*);
static void (*p_glBindBuffer)(GLenum, GLuint);
static void (*p_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*p_glEnableVertexAttribArray)(GLuint);
static void (*p_glDisableVertexAttribArray)(GLuint);
static void (*p_glVertexAttribPointer)(GLuint, GLint, GLenum, EGLBoolean, GLint, const void*);
static void (*p_glDrawArrays)(GLenum, GLint, GLint);
/* [优化] FBO 降分辨率渲染: 720p FBO → 1080p back buffer blit */
static void (*p_glGenFramebuffers)(GLint, GLuint*);
static void (*p_glBindFramebuffer)(GLenum, GLuint);
static void (*p_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static GLenum (*p_glCheckFramebufferStatus)(GLenum);
static void (*p_glGenTextures)(GLint, GLuint*);
static void (*p_glBindTexture)(GLenum, GLuint);
static void (*p_glTexImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLenum, GLenum, const void*);
static void (*p_glTexParameteri)(GLenum, GLenum, GLint);
static void (*p_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static void (*p_glActiveTexture)(GLenum);
/* 鼠标光标 alpha 纹理渲染 */
static void (*p_glTexCoord2f)(GLfloat, GLfloat);
static void (*p_glTexEnvi)(GLenum, GLenum, GLint);
static void (*p_glBlendFunc)(GLenum, GLenum);
static void (*p_glColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);

/* ========== 程序入口 (必须位于 .text 最前面, 内核从 load_addr 调用) ==========
 * efmcompositor.efs 加载地址 = 0x800000, 内核 efs_task_entry 直接跳转到该地址.
 * 因此 _start 必须是编译后 .text 段的第一个函数, 否则内核会错误调用排在
 * 最前面的其它函数 (如 load_gl_functions) 作为入口, 导致立即崩溃.
 * 这里通过前置声明 compositor_main/gl_render_loop, 并把 _start 放在所有
 * 函数定义之前来保证. */
void compositor_main(void);
static void gl_render_loop(void);

__attribute__((naked, section(".text.start")))
void _start(void) {
    __asm__ volatile(
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"
        "call compositor_main\n\t"
        "leave\n\t"
        "ret\n\t"
    );
}

/* ========== 直接 COM1 串口输出 (不依赖内核 API, 用于早期调试) ==========
 * [修复] 之前用 "a"(*_m) 约束把字符放 al, 但 inb %%dx,%%al 覆盖了 al,
 * 导致 movb %0,%%al 恢复的是 LSR 值而非字符, 输出全是垃圾 (0x60 反引号).
 * 改用 "c" 约束 (cl), al 仅用于 inb/outb, 输出前从 cl 恢复. */
static void cm_putc(char c) {
    __asm__ volatile(
        "mov $0x3FD, %%dx\n1: inb %%dx, %%al\n testb $0x20, %%al\n jz 1b\n"
        "mov $0x3F8, %%dx\n movb %0, %%al\n outb %%al, %%dx\n"
        : : "c"(c) : "rax", "rdx", "memory"
    );
}
static void cm_serial(const char *s) { while (*s) cm_putc(*s++); }

/* [防撕裂] 等待垂直同步 (VBlank) 前再 flush, 确保 back→front 拷贝在
 * 显示消隐期完成, 消除画面撕裂.
 * VGA Input Status #1 (port 0x3DA): bit3=1 表示处于垂直同步脉冲.
 * 策略: 先等当前 vsync 结束 (确保不追尾), 再等下一次 vsync 开始.
 * 超时保护: 若 100000 次读取仍未等到 (非 VGA 兼容硬件), 直接返回不阻塞. */
static void wait_vsync(void) {
    unsigned char s;
    int to;
    /* 等 vsync 结束 (如果当前正在 vsync) */
    to = 100000;
    do { __asm__ volatile("inb $0x3DA, %%al" : "=a"(s)); if (--to <= 0) return; } while (s & 0x08);
    /* 等下一次 vsync 开始 */
    to = 100000;
    do { __asm__ volatile("inb $0x3DA, %%al" : "=a"(s)); if (--to <= 0) return; } while (!(s & 0x08));
}

/* 通过 dlsym 加载所有 EGL/GL 函数指针.
 * 返回 0=成功 (含软件 GL 回退). dlsym 失败时自动加载软件 GL 实现. */
static void load_sw_gl_functions(void);  /* 前向声明: 定义在 efm_memset32 之后 */
static int load_gl_functions(void) {
    if (!API->dlsym) {
        load_sw_gl_functions();
        return 0;
    }
    p_eglGetDisplay         = (void*)API->dlsym("eglGetDisplay");
    p_eglInitialize         = (void*)API->dlsym("eglInitialize");
    p_eglBindAPI            = (void*)API->dlsym("eglBindAPI");
    p_eglChooseConfig       = (void*)API->dlsym("eglChooseConfig");
    p_eglCreateWindowSurface= (void*)API->dlsym("eglCreateWindowSurface");
    p_eglCreateContext      = (void*)API->dlsym("eglCreateContext");
    p_eglMakeCurrent        = (void*)API->dlsym("eglMakeCurrent");
    p_eglSwapBuffers        = (void*)API->dlsym("eglSwapBuffers");
    p_glClearColor          = (void*)API->dlsym("glClearColor");
    p_glClear               = (void*)API->dlsym("glClear");
    p_glBegin               = (void*)API->dlsym("glBegin");
    p_glEnd                 = (void*)API->dlsym("glEnd");
    p_glVertex2f            = (void*)API->dlsym("glVertex2f");
    p_glColor3f             = (void*)API->dlsym("glColor3f");
    p_glViewport            = (void*)API->dlsym("glViewport");
    p_glMatrixMode          = (void*)API->dlsym("glMatrixMode");
    p_glLoadIdentity        = (void*)API->dlsym("glLoadIdentity");
    p_glOrtho               = (void*)API->dlsym("glOrtho");
    p_glEnable              = (void*)API->dlsym("glEnable");
    p_glDisable             = (void*)API->dlsym("glDisable");
    /* [优化] VBO 批量渲染函数 (可选, 缺失则回退 glBegin/glEnd) */
    p_glGenBuffers          = (void*)API->dlsym("glGenBuffers");
    p_glBindBuffer          = (void*)API->dlsym("glBindBuffer");
    p_glBufferData          = (void*)API->dlsym("glBufferData");
    p_glEnableVertexAttribArray = (void*)API->dlsym("glEnableVertexAttribArray");
    p_glDisableVertexAttribArray= (void*)API->dlsym("glDisableVertexAttribArray");
    p_glVertexAttribPointer = (void*)API->dlsym("glVertexAttribPointer");
    p_glDrawArrays          = (void*)API->dlsym("glDrawArrays");
    /* [优化] FBO 降分辨率渲染函数 (可选, 缺失则全分辨率渲染) */
    p_glGenFramebuffers     = (void*)API->dlsym("glGenFramebuffers");
    p_glBindFramebuffer     = (void*)API->dlsym("glBindFramebuffer");
    p_glFramebufferTexture2D= (void*)API->dlsym("glFramebufferTexture2D");
    p_glCheckFramebufferStatus = (void*)API->dlsym("glCheckFramebufferStatus");
    p_glGenTextures         = (void*)API->dlsym("glGenTextures");
    p_glBindTexture         = (void*)API->dlsym("glBindTexture");
    p_glTexImage2D          = (void*)API->dlsym("glTexImage2D");
    p_glTexParameteri       = (void*)API->dlsym("glTexParameteri");
    p_glBlitFramebuffer     = (void*)API->dlsym("glBlitFramebuffer");
    p_glActiveTexture       = (void*)API->dlsym("glActiveTexture");
    /* 鼠标光标 alpha 纹理渲染 (可选) */
    p_glTexCoord2f          = (void*)API->dlsym("glTexCoord2f");
    p_glTexEnvi             = (void*)API->dlsym("glTexEnvi");
    p_glBlendFunc           = (void*)API->dlsym("glBlendFunc");
    p_glColor4f             = (void*)API->dlsym("glColor4f");
    /* 检查关键函数是否全部加载.
     * [软件 GL 回退] 若 Mesa 未加载 (dlsym 返回 NULL), 加载软件 GL 实现:
     *   直接渲染到 Graphics.drv 的 back buffer, 消除鼠标拖尾.
     *   eglCreateContext + glClear/glBegin 调用链与 Mesa 路径完全一致. */
    if (!p_eglGetDisplay || !p_eglInitialize || !p_eglCreateContext ||
        !p_eglMakeCurrent || !p_eglSwapBuffers || !p_glClear || !p_glBegin || !p_glEnd)
    {
        load_sw_gl_functions();
    }
    return 0;
}

/* ========== TSC 计时桩 (性能分析用) ==========
 * rdtsc 读取 CPU 时间戳计数器, 用于测量每帧/各阶段耗时.
 * 假设 TSC 频率 ~2GHz (常见 x86-64), 1ms ≈ 2,000,000 cycles.
 * 计时桩每秒输出一次: 帧率 + 渲染/swap 耗时, 帮助定位瓶颈. */
static inline unsigned long long rdtsc_now(void) {
    unsigned int lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}
#define TSC_PER_MS  2000000ULL   /* 假设 TSC 频率 2GHz */

/* 简单整数转字符串 (flat binary 无 snprintf) */
static int fmt_int(char *buf, int val) {
    if (val == 0) { buf[0] = '0'; return 1; }
    char tmp[16];
    int n = 0, neg = 0;
    unsigned int v;
    if (val < 0) { neg = 1; v = (unsigned int)(-val); } else v = (unsigned int)val;
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int pos = 0;
    if (neg) buf[pos++] = '-';
    while (n) buf[pos++] = tmp[--n];
    return pos;
}
static int fmt_str(char *buf, const char *s) {
    int n = 0; while (s[n]) buf[n] = s[n], n++; return n;
}

/* ========== Mesa EGL/GL 渲染状态 ========== */
static int g_gl_ready = 0;       /* 1 = EGL/GL 已初始化成功 */
static EGLDisplay  g_egl_dpy;
static EGLSurface  g_egl_surf;
static EGLContext  g_egl_ctx;

/* [优化] VBO 批量渲染状态
 * 把每帧所有窗口 quad 顶点收集到一个 VBO, 一次 glDrawArrays 提交,
 * 替代多次 glBegin/glEnd 立即模式 (每次 glBegin 都有命令解析开销).
 * 顶点格式: (x, y, r, g, b) interleaved, 每个 quad 4 顶点 (GL_QUADS).
 * 容量: 最多 32 窗口 * 4 顶点 + 任务栏 + 鼠标 ≈ 140 顶点 */
static GLuint g_vbo = 0;
static int    g_vbo_ready = 0;       /* 1 = VBO 可用 (函数指针齐全 + 缓冲已建) */
#define VBO_STRIDE  5                /* 5 float per vertex: x,y,r,g,b */
#define VBO_MAX_VERTS 768            /* 32窗口*12 + 任务栏 + 开始按钮 + 32窗口按钮 + 余量 */

/* [优化] FBO 降分辨率渲染状态
 * 在低分辨率 FBO (如 960x540) 渲染, 再 blit 到全分辨率 back buffer.
 * 像素填充率降为 (540/1080)^2 = 25%, llvmpipe 渲染时间 ~4x 加速.
 * blit 用 GL_LINEAR 过滤, 视觉质量可接受 (类似游戏降分辨率抗锯齿).
 * 若 FBO 函数不可用, 回退到全分辨率直接渲染. */
static GLuint g_fbo = 0;
static GLuint g_fbo_tex = 0;
static int    g_fbo_ready = 0;       /* 1 = FBO 可用 */
static int    g_fbo_w = 0, g_fbo_h = 0;  /* FBO 渲染分辨率 */
#define FBO_SCALE  0.5f              /* FBO 分辨率 = 屏幕分辨率 * 0.5 (1080p→540p) */

/* ========== 渲染状态 ========== */
static unsigned int *g_canvas = 0;   /* 像素缓冲 (screen-sized, 32bpp) */
static int g_cw = 0, g_ch = 0;       /* canvas 宽高 (像素) */
static int g_pitch = 0;              /* 每行像素数 (= g_cw) */

/* ========== 脏矩形追踪 ==========
 * 只在窗口/鼠标状态变化时重渲染 + blit, 未变化的帧跳过 (CPU 几乎零开销).
 * 场景脏标志: 窗口列表/标题/焦点/拖动变化时置 1.
 * 鼠标脏标志: 鼠标位置/可见性变化时置 1 (即使场景不变也要重画鼠标并 blit).
 * 上一帧的快照状态用于与本帧对比, 决定是否需要重渲染.
 *
 * [性能优化] 脏矩形: 鼠标移动时只重绘 + blit 鼠标新旧位置区域 (各 16x16),
 * 不再全屏重画. 全屏 blit 8MB → 鼠标区域 ~1KB, 快 8000 倍. */
static int g_scene_dirty = 1;        /* 1 = 场景需要重画 */
static int g_mouse_dirty = 1;        /* 1 = 鼠标位置变化需要重画 */
static int g_last_mx = -1, g_last_my = -1;       /* 上一帧鼠标位置 */
static int g_last_mouse_visible = 0;             /* 上一帧鼠标可见性 */
static int g_cur_mx = -1, g_cur_my = -1;         /* 当前帧鼠标位置 */
static int g_cur_mouse_visible = 0;              /* 当前帧鼠标可见性 */
static unsigned int g_last_gfx_dirty_seq = 0;    /* 上一帧 EFS 图形脏计数器 (kernel 端) */
/* [修复 · 按 WID 跟踪] 原实现按 windows[i] 数组索引存 seq, 当窗口列表在两帧间
 * 增删/排序时, 新窗口会读到旧窗口残留的 seq, 导致"旧 seq == 新 seq" → gfx_dirty=0
 * → 漏渲染 (新窗口第一帧、关闭后马上开新窗口 的场景最常见)。
 * WID 最多 64 (WM_MAX_WINDOWS=32, 递增永不复用), 用 [1..255] 稀疏数组按 WID 直接
 * 索引, 不再依赖 snap.windows[i] 顺序, 彻底消除错配。 */
#define LAST_SEQ_MAX_WID 256
static unsigned int g_last_gfx_info_seq_by_wid[LAST_SEQ_MAX_WID];
/* 保留原数组名 (不删, 作为只读 mirror, 最小化重构风险) */
static unsigned int g_last_gfx_info_seq[32];
static int g_last_window_count = -1;             /* 上一帧窗口数 */
static int g_last_focused_wid = -1;              /* 上一帧焦点窗口 */
static int g_last_menu_open = 0;                 /* 上一帧开始菜单打开状态 */
static int g_last_menu_hover = -1;               /* 上一帧开始菜单悬停项 */
/* [性能优化] 窗口位置/大小缓存: 检测拖动/调整大小, 触发 scene_dirty */
static int g_last_win_x[32], g_last_win_y[32];
static int g_last_win_w[32], g_last_win_h[32];

/* [性能优化] 窗口内容变化检测: 终端输出 (tbuf 文本变化) 不改窗口位置/大小,
 * 旧检测漏检 → 文本每 ~2s 才刷新 (严重卡顿). 现逐窗口哈希 tbuf + 跟踪
 * 光标位置/悬停/标题, 变化时触发该窗口的局部重绘 (而非全屏 8MB 重画). */
static int g_last_win_cx[32], g_last_win_cy[32];          /* 光标位置 */
static int g_last_win_hover[32];                          /* 关闭按钮悬停 */
static int g_last_win_wid[32];                            /* 窗口 wid (检测重排) */
static unsigned int g_last_win_hash[32];                  /* tbuf FNV-1a 哈希 */
static char g_last_win_title[32][EFM_WM_TITLE_LEN];       /* 标题文本 */
static int g_win_tracked = 0;                             /* 1 = 已初始化跟踪表 */

/* 脏矩形坐标 (inclusive). 鼠标移动时只更新这个区域.
 * 鼠标光标 8x11, 描边后最大 9x12, 留 2px 余量 → 11x14 */
#define MOUSE_CURSOR_W  11
#define MOUSE_CURSOR_H  14
static int g_dirty_x1 = 0, g_dirty_y1 = 0, g_dirty_x2 = 0, g_dirty_y2 = 0;
static int g_has_dirty_rect = 0;
/* [鼠标拖尾根治] 画鼠标前保存 back buffer 鼠标区域原始像素, flush 后立即恢复,
 * 让 back buffer 始终保持干净 (不含鼠标像素)。 */
static unsigned int g_mouse_save[MOUSE_CURSOR_W * MOUSE_CURSOR_H];
static int g_mouse_saved = 0;
static int g_mouse_save_x = -1, g_mouse_save_y = -1;

/* [脏矩形合并] 把 (x1,y1)-(x2,y2) 合并到当前脏矩形 */
static inline void dirty_union(int x1, int y1, int x2, int y2) {
    if (!g_has_dirty_rect) {
        g_dirty_x1 = x1; g_dirty_y1 = y1; g_dirty_x2 = x2; g_dirty_y2 = y2;
        g_has_dirty_rect = 1;
    } else {
        if (x1 < g_dirty_x1) g_dirty_x1 = x1;
        if (y1 < g_dirty_y1) g_dirty_y1 = y1;
        if (x2 > g_dirty_x2) g_dirty_x2 = x2;
        if (y2 > g_dirty_y2) g_dirty_y2 = y2;
    }
}

/* ========== SIMD/SSE2 加速批量操作 ==========
 * 替代逐像素循环. x86-64 必有 SSE2, 用 128-bit (16 字节 = 4 像素) 一次搬多个像素.
 *
 * efm_memcpy32: 32-bit 对齐的批量内存拷贝 (用于 blit canvas → back buffer 的内核侧
 *   已经由 Graphics.drv blit_buffer 处理, 这里是合成器内部场景拷贝/恢复用).
 * efm_memset32: 32-bit 填充 (用于 fill_rect 加速, 一次填 4 像素).
 *
 * 实测 1080p 全屏 fill: 逐像素 ~12ms. */
static inline void efm_memset32(void *dst, unsigned int val, unsigned int count) {
    /* 64-bit 展开: 把 val 复制成 8 字节, 一次写 2 像素, 比逐 32-bit 快 2 倍 */
    unsigned long long val64 = ((unsigned long long)val << 32) | val;
    unsigned long long *d64 = (unsigned long long*)dst;
    unsigned int pairs = count >> 1;
    while (pairs--) *d64++ = val64;
    if (count & 1) ((unsigned int*)dst)[count - 1] = val;
}

/* ========== 软件 GL/EGL 渲染器 (Mesa 未加载时的回退) ==========
 * 当 dlsym 无法解析 Mesa 符号时, 用软件实现替代 EGL/GL 函数.
 * 直接渲染到 Graphics.drv 的 back buffer, 通过 mark_dirty_rect + flush_now 呈现.
 *
 * 渲染管线 (与 Mesa GL 路径完全一致):
 *   eglMakeCurrent → 获取 back buffer 指针
 *   glClear        → 用 64 位展开填充 back buffer (全屏清屏, 消除拖尾)
 *   glBegin/glEnd  → 画 GL_QUADS 填充矩形 (窗口/任务栏)
 *   glDrawArrays   → VBO 批量渲染 (顶点数组 → 填充矩形)
 *   cursor texture → alpha 混合渲染鼠标光标 (纹理采样 + 混合)
 *   eglSwapBuffers → mark_dirty_rect(全屏) + flush_now (back→front)
 *
 * 优势: 每帧全屏重绘, 彻底消除鼠标拖尾; 无需 canvas 分配, 内存开销低;
 *        back buffer 直接写入, 渲染效率高. */

/* 软件渲染状态 */
static unsigned int *sw_backbuf = NULL;     /* back buffer 指针 */
static int sw_pitch = 0, sw_w = 0, sw_h = 0; /* back buffer 尺寸 */
static unsigned int sw_clear_color = 0xFF000000u;  /* glClear 清屏色 */
static unsigned int sw_cur_color = 0xFFFFFFFFu;     /* 当前 glColor 颜色 */
static int sw_begin_mode = 0;               /* glBegin 模式 */
static float sw_verts[8][2];                /* glBegin/glEnd 顶点坐标 */
static float sw_vert_tcoords[8][2];         /* 顶点纹理坐标 */
static int sw_vert_count = 0;               /* 当前顶点数 */
static int sw_blend_enabled = 0;            /* GL_BLEND 启用标志 */
static int sw_texture2d_enabled = 0;        /* GL_TEXTURE_2D 启用标志 */
/* 纹理存储 (鼠标光标 16x16 RGBA) */
#define SW_TEX_MAX 16
static unsigned int sw_tex_data[SW_TEX_MAX * SW_TEX_MAX];
static int sw_tex_w = 0, sw_tex_h = 0;
/* VBO 顶点数据存储 (x,y,r,g,b interleaved, stride=5) */
static float sw_vbo_data[VBO_MAX_VERTS * VBO_STRIDE];

/* --- 辅助: 填充 back buffer 矩形 (带裁剪) --- */
static void sw_fill_rect(int x1, int y1, int x2, int y2, unsigned int color) {
    if (!sw_backbuf) return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= sw_w) x2 = sw_w - 1;
    if (y2 >= sw_h) y2 = sw_h - 1;
    if (x1 > x2 || y1 > y2) return;
    for (int y = y1; y <= y2; y++) {
        unsigned int *row = sw_backbuf + y * sw_pitch;
        for (int x = x1; x <= x2; x++) row[x] = color;
    }
}

/* --- 辅助: 纹理矩形渲染 (鼠标光标 alpha 混合) --- */
static void sw_textured_quad(float vx0, float vy0, float vx1, float vy1) {
    if (!sw_backbuf || sw_tex_w <= 0 || sw_tex_h <= 0) return;
    int x0 = (int)vx0, y0 = (int)vy0;
    int x1 = (int)vx1, y1 = (int)vy1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= sw_w) x1 = sw_w - 1;
    if (y1 >= sw_h) y1 = sw_h - 1;
    if (x0 > x1 || y0 > y1) return;
    int qw = x1 - x0 + 1, qh = y1 - y0 + 1;
    for (int y = y0; y <= y1; y++) {
        int ty = ((y - y0) * sw_tex_h) / qh;
        if (ty >= sw_tex_h) ty = sw_tex_h - 1;
        unsigned int *row = sw_backbuf + y * sw_pitch;
        for (int x = x0; x <= x1; x++) {
            int tx = ((x - x0) * sw_tex_w) / qw;
            if (tx >= sw_tex_w) tx = sw_tex_w - 1;
            unsigned int texel = sw_tex_data[ty * sw_tex_w + tx];
            unsigned char a = (texel >> 24) & 0xFF;
            if (a == 0) continue;       /* 透明像素跳过 */
            if (a == 255) {
                row[x] = texel;          /* 不透明直接替换 */
            } else {
                /* alpha 混合: dst = src*a + dst*(1-a) */
                unsigned char sr = (texel >> 16) & 0xFF;
                unsigned char sg = (texel >> 8) & 0xFF;
                unsigned char sb = texel & 0xFF;
                unsigned char dr = (row[x] >> 16) & 0xFF;
                unsigned char dg = (row[x] >> 8) & 0xFF;
                unsigned char db = row[x] & 0xFF;
                unsigned int r = (sr * a + dr * (255 - a)) / 255;
                unsigned int g = (sg * a + dg * (255 - a)) / 255;
                unsigned int b = (sb * a + db * (255 - a)) / 255;
                row[x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

/* --- EGL 函数实现 --- */
static EGLDisplay sw_eglGetDisplay(EGLNativeDisplayType d) { (void)d; return (EGLDisplay)1; }
static EGLBoolean sw_eglInitialize(EGLDisplay d, EGLint *maj, EGLint *min) {
    (void)d; if (maj) *maj = 1; if (min) *min = 5; return 1;
}
static EGLBoolean sw_eglBindAPI(EGLenum api) { (void)api; return 1; }
static EGLBoolean sw_eglChooseConfig(EGLDisplay d, const EGLint *a, EGLConfig *c, EGLint n, EGLint *num) {
    (void)d; (void)a; (void)n;
    if (c) c[0] = (EGLConfig)1;
    if (num) *num = 1;
    return 1;
}
static EGLSurface sw_eglCreateWindowSurface(EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint *a) {
    (void)d; (void)c; (void)w; (void)a; return (EGLSurface)1;
}
static EGLContext sw_eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext s, const EGLint *a) {
    (void)d; (void)c; (void)s; (void)a; return (EGLContext)1;
}
static EGLBoolean sw_eglMakeCurrent(EGLDisplay d, EGLSurface s, EGLSurface s2, EGLContext c) {
    (void)d; (void)s; (void)s2; (void)c;
    void *ptr = NULL; int pitch = 0, w = 0, h = 0;
    if (API->get_backbuffer(&ptr, &pitch, &w, &h) == 0 && ptr) {
        sw_backbuf = (unsigned int*)ptr;
        /* pitch 是字节/行, 转换为像素/行 (32bpp = 4 字节/像素) */
        sw_pitch = pitch / 4;
        if (sw_pitch < w) sw_pitch = w;  /* 安全兜底 */
        sw_w = w;
        sw_h = h;
        return 1;
    }
    return 0;
}
static EGLBoolean sw_eglSwapBuffers(EGLDisplay d, EGLSurface s) {
    (void)d; (void)s;
    if (sw_w > 0 && sw_h > 0) {
        API->mark_dirty_rect(0, 0, sw_w - 1, sw_h - 1);
        API->flush_now();
    }
    return 1;
}

/* --- GL 基础函数 --- */
static void sw_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    sw_clear_color = ((unsigned int)(a * 255.0f + 0.5f) << 24) |
        ((unsigned int)(r * 255.0f + 0.5f) << 16) |
        ((unsigned int)(g * 255.0f + 0.5f) << 8) |
        (unsigned int)(b * 255.0f + 0.5f);
}
static void sw_glClear(GLbitfield mask) {
    (void)mask;
    if (!sw_backbuf) return;
    unsigned long total = (unsigned long)sw_pitch * (unsigned long)sw_h;
    unsigned long long val64 = ((unsigned long long)sw_clear_color << 32) | sw_clear_color;
    unsigned long long *d64 = (unsigned long long*)sw_backbuf;
    unsigned long pairs = total >> 1;
    while (pairs--) *d64++ = val64;
    if (total & 1) ((unsigned int*)sw_backbuf)[total - 1] = sw_clear_color;
}
static void sw_glViewport(GLint x, GLint y, GLint w, GLint h) { (void)x; (void)y; (void)w; (void)h; }
static void sw_glMatrixMode(GLenum mode) { (void)mode; }
static void sw_glLoadIdentity(void) { /* no-op */ }
static void sw_glOrtho(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
    (void)l; (void)r; (void)b; (void)t; (void)n; (void)f;
}
static void sw_glEnable(GLenum cap) {
    if (cap == GL_BLEND) sw_blend_enabled = 1;
    else if (cap == GL_TEXTURE_2D) sw_texture2d_enabled = 1;
}
static void sw_glDisable(GLenum cap) {
    if (cap == GL_BLEND) sw_blend_enabled = 0;
    else if (cap == GL_TEXTURE_2D) sw_texture2d_enabled = 0;
}

/* --- GL 立即模式 (glBegin/glEnd) --- */
static void sw_glBegin(GLenum mode) { sw_begin_mode = mode; sw_vert_count = 0; }
static void sw_glEnd(void) {
    if (sw_begin_mode == GL_QUADS && sw_vert_count >= 4) {
        float minx = sw_verts[0][0], maxx = minx;
        float miny = sw_verts[0][1], maxy = miny;
        for (int i = 1; i < 4; i++) {
            if (sw_verts[i][0] < minx) minx = sw_verts[i][0];
            if (sw_verts[i][0] > maxx) maxx = sw_verts[i][0];
            if (sw_verts[i][1] < miny) miny = sw_verts[i][1];
            if (sw_verts[i][1] > maxy) maxy = sw_verts[i][1];
        }
        if (sw_texture2d_enabled && sw_tex_w > 0) {
            sw_textured_quad(minx, miny, maxx, maxy);
        } else {
            sw_fill_rect((int)minx, (int)miny, (int)maxx, (int)maxy, sw_cur_color);
        }
    }
    sw_begin_mode = 0;
    sw_vert_count = 0;
}
static void sw_glVertex2f(GLfloat x, GLfloat y) {
    if (sw_vert_count < 8) { sw_verts[sw_vert_count][0] = x; sw_verts[sw_vert_count][1] = y; sw_vert_count++; }
}
static void sw_glColor3f(GLfloat r, GLfloat g, GLfloat b) {
    sw_cur_color = (0xFFu << 24) |
        ((unsigned int)(r * 255.0f + 0.5f) << 16) |
        ((unsigned int)(g * 255.0f + 0.5f) << 8) |
        (unsigned int)(b * 255.0f + 0.5f);
}
static void sw_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    sw_cur_color = ((unsigned int)(a * 255.0f + 0.5f) << 24) |
        ((unsigned int)(r * 255.0f + 0.5f) << 16) |
        ((unsigned int)(g * 255.0f + 0.5f) << 8) |
        (unsigned int)(b * 255.0f + 0.5f);
}
static void sw_glTexCoord2f(GLfloat s, GLfloat t) {
    if (sw_vert_count < 8) { sw_vert_tcoords[sw_vert_count][0] = s; sw_vert_tcoords[sw_vert_count][1] = t; }
}

/* --- VBO 函数 (批量渲染) --- */
static void sw_glGenBuffers(GLint n, GLuint *ids) { for (int i = 0; i < n; i++) ids[i] = 1; }
static void sw_glBindBuffer(GLenum target, GLuint id) { (void)target; (void)id; }
static void sw_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    (void)target; (void)usage;
    int count = (int)((unsigned long)size / (VBO_STRIDE * sizeof(float)));
    if (count > VBO_MAX_VERTS) count = VBO_MAX_VERTS;
    if (count < 0) count = 0;
    if (data) {
        const float *src = (const float*)data;
        for (int i = 0; i < count * VBO_STRIDE; i++) sw_vbo_data[i] = src[i];
    }
}
static void sw_glEnableVertexAttribArray(GLuint idx) { (void)idx; }
static void sw_glDisableVertexAttribArray(GLuint idx) { (void)idx; }
static void sw_glVertexAttribPointer(GLuint idx, GLint sz, GLenum t, EGLBoolean n, GLint stride, const void *ptr) {
    (void)idx; (void)sz; (void)t; (void)n; (void)stride; (void)ptr;
}
static void sw_glDrawArrays(GLenum mode, GLint first, GLint count) {
    if (mode != GL_QUADS || first < 0) return;
    int end = first + count;
    if (end > VBO_MAX_VERTS) end = VBO_MAX_VERTS;
    for (int i = first; i + 3 < end; i += 4) {
        float *v = &sw_vbo_data[i * VBO_STRIDE];
        float minx = v[0], maxx = v[0], miny = v[1], maxy = v[1];
        for (int j = 1; j < 4; j++) {
            float x = v[j * VBO_STRIDE + 0], y = v[j * VBO_STRIDE + 1];
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
        unsigned int color = (0xFFu << 24) |
            ((unsigned int)(v[2] * 255.0f + 0.5f) << 16) |
            ((unsigned int)(v[3] * 255.0f + 0.5f) << 8) |
            (unsigned int)(v[4] * 255.0f + 0.5f);
        sw_fill_rect((int)minx, (int)miny, (int)maxx, (int)maxy, color);
    }
}

/* --- 纹理函数 --- */
static void sw_glGenTextures(GLint n, GLuint *ids) { for (int i = 0; i < n; i++) ids[i] = 1; }
static void sw_glBindTexture(GLenum target, GLuint id) { (void)target; (void)id; }
static void sw_glTexImage2D(GLenum target, GLint level, GLint internal,
                            GLint w, GLint h, GLint border,
                            GLenum format, GLenum type, const void *data) {
    (void)target; (void)level; (void)internal; (void)border; (void)format; (void)type;
    if (w > SW_TEX_MAX) w = SW_TEX_MAX;
    if (h > SW_TEX_MAX) h = SW_TEX_MAX;
    sw_tex_w = w; sw_tex_h = h;
    if (data) {
        const unsigned char *src = (const unsigned char*)data;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int si = (y * w + x) * 4;
                sw_tex_data[y * sw_tex_w + x] =
                    ((unsigned int)src[si+3] << 24) |
                    ((unsigned int)src[si+0] << 16) |
                    ((unsigned int)src[si+1] << 8) |
                    (unsigned int)src[si+2];
            }
    }
}
static void sw_glTexParameteri(GLenum target, GLenum pname, GLint param) { (void)target; (void)pname; (void)param; }
static void sw_glBlendFunc(GLenum sf, GLenum df) { (void)sf; (void)df; }
static void sw_glTexEnvi(GLenum target, GLenum pname, GLint param) { (void)target; (void)pname; (void)param; }

/* --- FBO stub (不使用 FBO, 返回失败让 fbo_init 跳过) --- */
static void sw_glGenFramebuffers(GLint n, GLuint *ids) { for (int i = 0; i < n; i++) ids[i] = 0; }
static void sw_glBindFramebuffer(GLenum target, GLuint id) { (void)target; (void)id; }
static void sw_glFramebufferTexture2D(GLenum t, GLenum a, GLenum tt, GLuint id, GLint l) { (void)t; (void)a; (void)tt; (void)id; (void)l; }
static GLenum sw_glCheckFramebufferStatus(GLenum target) { (void)target; return 0; } /* 非 COMPLETE, 让 fbo_init 失败 */
static void sw_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                                  GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                  GLbitfield mask, GLenum filter) {
    (void)sx0; (void)sy0; (void)sx1; (void)sy1; (void)dx0; (void)dy0; (void)dx1; (void)dy1; (void)mask; (void)filter;
}
static void sw_glActiveTexture(GLenum unit) { (void)unit; }

/* 加载软件 GL 函数指针 (Mesa 不可用时的回退) */
static void load_sw_gl_functions(void) {
    p_eglGetDisplay         = sw_eglGetDisplay;
    p_eglInitialize         = sw_eglInitialize;
    p_eglBindAPI            = sw_eglBindAPI;
    p_eglChooseConfig       = sw_eglChooseConfig;
    p_eglCreateWindowSurface= sw_eglCreateWindowSurface;
    p_eglCreateContext      = sw_eglCreateContext;
    p_eglMakeCurrent        = sw_eglMakeCurrent;
    p_eglSwapBuffers        = sw_eglSwapBuffers;
    p_glClearColor          = sw_glClearColor;
    p_glClear               = sw_glClear;
    p_glBegin               = sw_glBegin;
    p_glEnd                 = sw_glEnd;
    p_glVertex2f            = sw_glVertex2f;
    p_glColor3f             = sw_glColor3f;
    p_glViewport            = sw_glViewport;
    p_glMatrixMode          = sw_glMatrixMode;
    p_glLoadIdentity        = sw_glLoadIdentity;
    p_glOrtho               = sw_glOrtho;
    p_glEnable              = sw_glEnable;
    p_glDisable             = sw_glDisable;
    p_glGenBuffers          = sw_glGenBuffers;
    p_glBindBuffer          = sw_glBindBuffer;
    p_glBufferData          = sw_glBufferData;
    p_glEnableVertexAttribArray = sw_glEnableVertexAttribArray;
    p_glDisableVertexAttribArray= sw_glDisableVertexAttribArray;
    p_glVertexAttribPointer = sw_glVertexAttribPointer;
    p_glDrawArrays          = sw_glDrawArrays;
    p_glGenTextures         = sw_glGenTextures;
    p_glBindTexture         = sw_glBindTexture;
    p_glTexImage2D          = sw_glTexImage2D;
    p_glTexParameteri       = sw_glTexParameteri;
    p_glGenFramebuffers     = sw_glGenFramebuffers;
    p_glBindFramebuffer     = sw_glBindFramebuffer;
    p_glFramebufferTexture2D= sw_glFramebufferTexture2D;
    p_glCheckFramebufferStatus = sw_glCheckFramebufferStatus;
    p_glBlitFramebuffer     = sw_glBlitFramebuffer;
    p_glActiveTexture       = sw_glActiveTexture;
    p_glTexCoord2f          = sw_glTexCoord2f;
    p_glTexEnvi             = sw_glTexEnvi;
    p_glBlendFunc           = sw_glBlendFunc;
    p_glColor4f             = sw_glColor4f;
}

/* [性能] 窗口文本缓冲哈希 (FNV-1a): 检测终端输出变化, 触发局部重绘.
 * 只哈希实际使用的 cols*rows 区域 (典型 120*60=7200 字节), 单窗口 < 0.05ms.
 * 变化时只需重绘该窗口区域 (~1MB), 而非全屏 8MB 重画. */
static unsigned int tbuf_hash(const char *p, int cols, int rows) {
    if (!p || cols <= 0 || rows <= 0) return 0;
    unsigned int h = 2166136261u;
    int n = cols * rows;
    for (int i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= 16777619u;
    }
    return h;
}

/* ========== 像素绘制辅助 ========== */

/* 透明背景标记: bg 为此值时 draw_char 不画背景像素 (纯前景叠加, 不覆盖下层内容) */
#define COLOR_TRANSPARENT  0xFEEDFACEu

static inline void px(int x, int y, unsigned int color) {
    if ((unsigned int)x >= (unsigned int)g_cw || (unsigned int)y >= (unsigned int)g_ch) return;
    g_canvas[y * g_pitch + x] = color;
}

/* alpha 混合: 把 src (RGB) 以 alpha 混合到 canvas (x,y) 像素上.
 * a=0 完全透明 (不变), a=255 完全不透明 (替换).
 * 用于鼠标光标边缘抗锯齿, 让光标在任何背景上都清晰可见且无硬边. */
static inline void px_blend(int x, int y, unsigned int src_rgb, unsigned int a) {
    if ((unsigned int)x >= (unsigned int)g_cw || (unsigned int)y >= (unsigned int)g_ch) return;
    if (a == 0) return;
    unsigned int *dst = &g_canvas[y * g_pitch + x];
    if (a >= 255) { *dst = src_rgb; return; }
    unsigned int d = *dst;
    /* canvas 像素格式: 0x00RRGGBB (BGRX, 与 GOP 一致) */
    unsigned int sr = (src_rgb >> 16) & 0xFF;
    unsigned int sg = (src_rgb >> 8)  & 0xFF;
    unsigned int sb =  src_rgb        & 0xFF;
    unsigned int dr = (d >> 16) & 0xFF;
    unsigned int dg = (d >> 8)  & 0xFF;
    unsigned int db =  d        & 0xFF;
    unsigned int ia = 255 - a;
    unsigned int r = (sr * a + dr * ia + 127) / 255;
    unsigned int g = (sg * a + dg * ia + 127) / 255;
    unsigned int b = (sb * a + db * ia + 127) / 255;
    *dst = (r << 16) | (g << 8) | b;
}

static void fill_rect(int x1, int y1, int w, int h, unsigned int color) {
    if (x1 < 0) { w += x1; x1 = 0; }
    if (y1 < 0) { h += y1; y1 = 0; }
    if (x1 + w > g_cw) w = g_cw - x1;
    if (y1 + h > g_ch) h = g_ch - y1;
    if (w <= 0 || h <= 0) return;
    /* 【SSE2 加速】 每行用 efm_memset32 批量填充, 一次 4 像素, 比逐像素快 8 倍 */
    for (int y = 0; y < h; y++) {
        unsigned int *row = g_canvas + (y1 + y) * g_pitch + x1;
        efm_memset32(row, color, (unsigned int)w);
    }
}

static void draw_rect_border(int x, int y, int w, int h, unsigned int color) {
    fill_rect(x, y, w, 2, color);
    fill_rect(x, y + h - 2, w, 2, color);
    fill_rect(x, y, 2, h, color);
    fill_rect(x + w - 2, y, 2, h, color);
}

/* [内容区裁剪] 填充矩形, 但裁剪到指定的裁剪矩形 (clip_x1..clip_x2, clip_y1..clip_y2).
 * 用于 render_single_window 中 EFS rect 的填充, 确保按钮/面板不会画到窗口边框/标题栏外.
 * fill_rect 只裁剪到全局 canvas (g_cw x g_ch), 不裁剪到窗口内容区, 所以需要本函数. */
static void fill_rect_clip(int x1, int y1, int w, int h, unsigned int color,
                            int clip_x1, int clip_y1, int clip_x2, int clip_y2) {
    int x2 = x1 + w - 1, y2 = y1 + h - 1;
    if (x1 < clip_x1) x1 = clip_x1;
    if (y1 < clip_y1) y1 = clip_y1;
    if (x2 > clip_x2) x2 = clip_x2;
    if (y2 > clip_y2) y2 = clip_y2;
    if (x1 > x2 || y1 > y2) return;
    fill_rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1, color);
}

/* [内容区裁剪] 画矩形边框 (2px), 用原始坐标画 4 条边, 每条边单独裁剪到裁剪区.
 * 解决 render_single_window 中 rect 被裁剪后边框画错位置的问题:
 * 原实现用裁剪后的 (rx1,ry1,rx2,ry2) 画边框, 如果按钮右边/下边被裁剪,
 * 右/下边框会画在裁剪后的边界 (按钮看起来变窄), 而非原始终位置.
 * 本函数用原始 (x,y,w,h) 画 4 条边, 每条边裁剪到内容区, 被裁掉的边不画,
 * 可见边的位置正确. */
static void draw_rect_border_clip(int x, int y, int w, int h, unsigned int color,
                                    int clip_x1, int clip_y1, int clip_x2, int clip_y2) {
    int bthk = 2;
    /* 顶边: y..y+bthk-1 */
    fill_rect_clip(x, y, w, bthk, color, clip_x1, clip_y1, clip_x2, clip_y2);
    /* 底边: y+h-bthk..y+h-1 */
    fill_rect_clip(x, y + h - bthk, w, bthk, color, clip_x1, clip_y1, clip_x2, clip_y2);
    /* 左边: x..x+bthk-1 */
    fill_rect_clip(x, y, bthk, h, color, clip_x1, clip_y1, clip_x2, clip_y2);
    /* 右边: x+w-bthk..x+w-1 */
    fill_rect_clip(x + w - bthk, y, bthk, h, color, clip_x1, clip_y1, clip_x2, clip_y2);
}

/* draw_char: 渲染字符 (FONT_W x FONT_H 像素).
 * 基础字形为 8x16 VGA 点阵 (g_font8x16), 通过最近邻上采样到 FONT_W x FONT_H.
 * bg == COLOR_TRANSPARENT 时: 只画前景像素, 背景像素保持不变 (不覆盖下层内容).
 * 这是 "不渲染下方背景" 的核心: 文字光标/桌面文字/窗口标题 都可以用透明背景,
 * 让文字直接叠加在已有内容上, 而不是填充一个色块. */
static void draw_char(int x, int y, char ch, unsigned int fg, unsigned int bg) {
    unsigned char c = (unsigned char)ch;
    if (c < 0x20 || c > 0x7E) c = 0x20;
    const unsigned char *glyph = g_font8x16[c - 0x20];
    int transparent = (bg == COLOR_TRANSPARENT);
    for (int r = 0; r < FONT_H; r++) {
        int src_r = r * FONT_BASE_H / FONT_H;  /* 最近邻上采样: 输出行→输入行 */
        if (src_r >= FONT_BASE_H) src_r = FONT_BASE_H - 1;
        unsigned char mask = glyph[src_r];
        for (int c2 = 0; c2 < FONT_W; c2++) {
            int src_c = c2 * FONT_BASE_W / FONT_W;  /* 最近邻上采样: 输出列→输入列 */
            if (src_c >= FONT_BASE_W) src_c = FONT_BASE_W - 1;
            if (mask & (1 << (7 - src_c))) {
                px(x + c2, y + r, fg);
            } else if (!transparent) {
                px(x + c2, y + r, bg);
            }
            /* transparent 且 mask=0: 不画, 保留下层像素 */
        }
    }
}

static void draw_text(int x, int y, const char *s, unsigned int fg, unsigned int bg) {
    int cx = x;
    while (*s) {
        draw_char(cx, y, *s, fg, bg);
        cx += FONT_W;
        s++;
    }
}

/* ========== 鼠标光标绘制 (纯叠加, 无背景) ==========
 * 设计: 光标形状用 alpha 混合画到 canvas 上, 不画任何背景方块.
 *   - 外圈黑色描边: alpha=255 (完全不透明), 保证在任何背景上可见
 *   - 内部白色填充: alpha=255
 *   - 边缘像素: alpha=128 (半透明), 实现抗锯齿, 平滑过渡
 * 光标形状 (11x16):
 *   行 0-7: 逐渐变宽的三角形 (箭头)
 *   行 8-10: 尾巴分叉
 * 关键: 只画 mask=1 的像素, mask=0 的像素完全不碰 (保留下层内容). */
static void draw_mouse(int mx, int my) {
    static const unsigned char shape[11] = {0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0x38,0x1C,0x0E};
    const unsigned int black = 0x000000;
    const unsigned int white = 0xFFFFFF;
    const unsigned int red   = 0xFF2222;

    /* 第一遍: 画黑色描边 (在形状像素的右/下方相邻位置, 仅当该位置不在形状内) */
    for (int r = 0; r < 11; r++) {
        unsigned char mask = shape[r];
        for (int c = 0; c < 8; c++) {
            if (!(mask & (1 << (7 - c)))) continue;
            /* 右侧描边 */
            if (c < 7 && !(mask & (1 << (7 - (c + 1))))) {
                px_blend(mx + c + 1, my + r, black, 255);
            }
            /* 下方描边 */
            unsigned char next_mask = (r < 10) ? shape[r + 1] : 0;
            if (!(next_mask & (1 << (7 - c)))) {
                px_blend(mx + c, my + r + 1, black, 255);
            }
        }
    }
    /* 第二遍: 画白色填充 (覆盖在描边之上) */
    for (int r = 0; r < 11; r++) {
        unsigned char mask = shape[r];
        for (int c = 0; c < 8; c++) {
            if (mask & (1 << (7 - c))) {
                px(mx + c, my + r, white);
            }
        }
    }
    /* 箭头尖: 红色高亮 */
    px(mx, my, red);
}

/* [性能优化] draw_mouse_to_buf: 直接画鼠标到指定 back buffer (不经过 canvas).
 * 用于鼠标移动时的局部更新: 避免全屏 blit, 只在 back buffer 的鼠标区域画光标.
 * buf: back buffer 指针, pitch_px: 每行像素数, (mx,my): 鼠标左上角 */
static void draw_mouse_to_buf(unsigned int *buf, int pitch_px, int mx, int my,
                              int buf_w, int buf_h) {
    static const unsigned char shape[11] = {0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0x38,0x1C,0x0E};
    const unsigned int black = 0x000000;
    const unsigned int white = 0xFFFFFF;
    const unsigned int red   = 0xFF2222;
    /* 画白色箭头 + 黑色描边 (右下方), 确保在任意背景上可见 */
    for (int r = 0; r < 11; r++) {
        unsigned char mask = shape[r];
        for (int c = 0; c < 8; c++) {
            if (!(mask & (1 << (7 - c)))) continue;
            int x = mx + c, y = my + r;
            /* 黑色描边: 右/下相邻像素 */
            int rx = x + 1, dy = y + 1;
            if (rx >= 0 && rx < buf_w && y >= 0 && y < buf_h) buf[y * pitch_px + rx] = black;
            if (x >= 0 && x < buf_w && dy >= 0 && dy < buf_h) buf[dy * pitch_px + x] = black;
            /* 白色主体 */
            if (x >= 0 && x < buf_w && y >= 0 && y < buf_h) {
                buf[y * pitch_px + x] = white;
            }
        }
    }
    if (mx >= 0 && mx < buf_w && my >= 0 && my < buf_h) {
        buf[my * pitch_px + mx] = red;
    }
}

/* ========== 窗口排序 (按 z_order 升序, 底→顶) ========== */
static void sort_windows(struct efm_wm_win_info *arr, int n) {
    for (int i = 1; i < n; i++) {
        struct efm_wm_win_info key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j].z_order > key.z_order) {
            arr[j+1] = arr[j];
            j--;
        }
        arr[j+1] = key;
    }
}

/* ========== UTF-8 TTF 文字渲染 (draw_char_unicode 叠加到 back buffer) ==========
 * 设计:
 *   render_scene / render_single_window 只画"色块" (背景/边框/按钮色) 到 canvas,
 *   不画任何文字. canvas blit 到 back buffer 后, 调用 render_ttf_text_overlay
 *   通过 API->draw_char_unicode (透明背景) 叠加 TTF 文字, 实现高质量 CJK UTF-8 文字.
 *   bg=COLOR_TRANSPARENT(0xFEEDFACE) 表示 alpha 混合, 不覆盖下层像素. */

/* UTF-8 解码: 从 *pp 读取一个 Unicode codepoint, 推进 *pp */
static unsigned int utf8_decode_comp(const char **pp) {
    const unsigned char *p = (const unsigned char*)*pp;
    unsigned char c = *p;
    if (c < 0x80) { *pp = (const char*)(p+1); return c; }
    if ((c & 0xE0) == 0xC0) {
        unsigned int cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
        *pp = (const char*)(p+2); return cp;
    }
    if ((c & 0xF0) == 0xE0) {
        unsigned int cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *pp = (const char*)(p+3); return cp;
    }
    *pp = (const char*)(p+1); return '?';
}

/* 字符单元格尺寸 (步进值): 增大可增加字符间距与行距.
 * glyph 实际渲染高度由 Graphics.drv 的 g_ttf_pixel_size=16 决定, 不受此值影响,
 * 多出的像素成为字符右侧/下方的间距.
 * [1.2x 放大] 12→14, 22→26 */
#define TTF_FONT_W  14   /* ASCII 字符步进 (1.2x: 原 12) */
#define TTF_FONT_H  26   /* 行高步进 (1.2x: 原 22) */

/* 画一条 UTF-8 字符串到 back buffer (透明背景, TTF 渲染) */
static void draw_ttf_string_back(int x, int y, const char *s, unsigned int fg) {
    if (!s || !API->draw_char_unicode) return;
    while (*s) {
        unsigned int cp = utf8_decode_comp(&s);
        if (cp == 0) break;
        int cell_w = (cp < 0x80) ? TTF_FONT_W : (2 * TTF_FONT_W);
        int adv = API->draw_char_unicode(x, y, cp, fg, COLOR_TRANSPARENT, cell_w, TTF_FONT_H);
        x += (adv > 0) ? adv : cell_w;   /* 用驱动返回的统一等宽步进, 保持与渲染坐标严格同步 */
    }
}

/* [内容区裁剪] 画 UTF-8 字符串到 back buffer, 逐字符裁剪到指定的矩形区域.
 * 用于 EFS GUI 文字渲染: 文字坐标是窗口内容区相对坐标 + (cx,cy) 偏移,
 * 必须裁剪到窗口内容区, 否则文字会画到窗口边框/标题栏/相邻窗口上.
 * 完全在裁剪区外的字符跳过; 部分在裁剪区内的字符仍调用 draw_char_unicode
 * (内核端 draw_char_unicode 在 WM 模式下会对 current_pid 窗口做内容区裁剪,
 *  但 compositor 调用时 current_pid=compositor, 不会裁剪, 所以这里手动跳过
 *  完全在外的字符, 减少越界绘制). */
static void draw_ttf_string_back_clip(int x, int y, const char *s, unsigned int fg,
                                        int clip_x1, int clip_y1,
                                        int clip_x2, int clip_y2) {
    if (!s || !API->draw_char_unicode) return;
    while (*s) {
        unsigned int cp = utf8_decode_comp(&s);
        if (cp == 0) break;
        int cell_w = (cp < 0x80) ? TTF_FONT_W : (2 * TTF_FONT_W);
        /* 字符 AABB: (x, y) .. (x+cell_w-1, y+TTF_FONT_H-1) */
        int cx1 = x, cy1 = y, cx2 = x + cell_w - 1, cy2 = y + TTF_FONT_H - 1;
        /* 完全在裁剪区外 → 跳过此字符 */
        if (cx2 < clip_x1 || cx1 > clip_x2 || cy2 < clip_y1 || cy1 > clip_y2) {
            x += cell_w;
            continue;
        }
        int adv = API->draw_char_unicode(x, y, cp, fg, COLOR_TRANSPARENT, cell_w, TTF_FONT_H);
        x += (adv > 0) ? adv : cell_w;
    }
}

/* 根据 EFMOS-logo-b.png 的 alpha (200x200 boot_logo_alpha) 绘制缩小版 logo.
 * 使用最近邻采样 + alpha over 混合, 绘制到指定矩形 dst_x,dst_y,dst_w,dst_h.
 * fg: 前景色 (RGB, 通常传 0x00FFFFFF = 白色).
 * back_buf/back_pitch_px/back_w/back_h: 目标 framebuffer (与 draw_tbuf_back_clip 相同约定). */
static void draw_start_logo(unsigned int *back_buf, int back_pitch_px,
                            int back_w, int back_h,
                            int dst_x, int dst_y, int dst_w, int dst_h,
                            unsigned int fg) {
    if (dst_w <= 0 || dst_h <= 0) return;
    if (!back_buf || back_pitch_px <= 0 || back_w <= 0 || back_h <= 0) return;
    unsigned char r = (fg >> 16) & 0xFF, g = (fg >> 8) & 0xFF, b = fg & 0xFF;
    if (fg == 0x00FFFFFF) { r = 0xFF; g = 0xFF; b = 0xFF; }
    /* 夹紧到屏幕 */
    int x1 = dst_x, y1 = dst_y, x2 = dst_x + dst_w - 1, y2 = dst_y + dst_h - 1;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= back_w) x2 = back_w - 1;
    if (y2 >= back_h) y2 = back_h - 1;
    if (x1 > x2 || y1 > y2) return;
    for (int py = y1; py <= y2; py++) {
        int ly = py - dst_y;
        int sy = (ly * LOGO_H) / dst_h;
        if (sy < 0) sy = 0; if (sy >= LOGO_H) sy = LOGO_H - 1;
        unsigned int *row = back_buf + py * back_pitch_px;
        for (int px = x1; px <= x2; px++) {
            int lx = px - dst_x;
            int ssx = (lx * LOGO_W) / dst_w;
            if (ssx < 0) ssx = 0; if (ssx >= LOGO_W) ssx = LOGO_W - 1;
            unsigned char a = boot_logo_alpha[sy * LOGO_W + ssx];
            if (a == 0) continue;
            unsigned int dst_pix = row[px];
            unsigned char dr = (dst_pix >> 16) & 0xFF, dg = (dst_pix >> 8) & 0xFF, db = dst_pix & 0xFF;
            unsigned int inv = 255 - a;
            unsigned char nr = (r * a + dr * inv) / 255;
            unsigned char ng = (g * a + dg * inv) / 255;
            unsigned char nb = (b * a + db * inv) / 255;
            row[px] = (0xFFu << 24) | ((unsigned int)nr << 16) | ((unsigned int)ng << 8) | nb;
        }
    }
}

/* 画单个 codepoint 到 back buffer (TTF, 透明背景) */
static void draw_ttf_cp_back(int x, int y, unsigned int cp, unsigned int fg) {
    if (!API->draw_char_unicode) return;
    int cell_w = (cp < 0x80) ? TTF_FONT_W : (2 * TTF_FONT_W);
    API->draw_char_unicode(x, y, cp, fg, COLOR_TRANSPARENT, cell_w, TTF_FONT_H);
}

/* [性能] 画 tbuf 终端文本到 back buffer. clip_y1/clip_y2 = 脏矩形 y 范围
 * (像素坐标, 含端点, clip_y1<0 表示不裁剪). 裁剪后只渲染可见行的文字. */
static void draw_tbuf_back_clip(int x, int y, const char *tbuf, int cols, int rows,
                                int max_w, int max_h, unsigned int fg,
                                int clip_y1, int clip_y2) {
    if (!tbuf || cols <= 0 || rows <= 0 || !API->draw_char_unicode) return;
    int max_rows = max_h / TTF_FONT_H;
    if (max_rows > rows) max_rows = rows;
    int max_x = x + max_w;
    int r_start = 0, r_end = max_rows;
    /* y 裁剪: 只渲染与脏矩形重叠的行 */
    if (clip_y1 < clip_y2) {
        int row0 = (clip_y1 - y) / TTF_FONT_H;
        int rowN = (clip_y2 - y + TTF_FONT_H - 1) / TTF_FONT_H;
        if (row0 > r_start) r_start = row0;
        if (rowN < r_end) r_end = rowN;
    }
    if (r_start < 0) r_start = 0;
    if (r_end > max_rows) r_end = max_rows;
    for (int r = r_start; r < r_end; r++) {
        const char *p = tbuf + r * cols;
        const char *row_end = p + cols;
        int cx = x, cy = y + r * TTF_FONT_H;
        while (p < row_end && cx < max_x) {
            unsigned char c = (unsigned char)*p;
            if (c == 0) { p++; cx += TTF_FONT_W; continue; }
            int seq_len;
            if (c < 0x80) seq_len = 1;
            else if ((c & 0xE0) == 0xC0) seq_len = 2;
            else if ((c & 0xF0) == 0xE0) seq_len = 3;
            else seq_len = 1;
            if (p + seq_len > row_end) break;
            unsigned int cp = utf8_decode_comp(&p);
            int cell_w = (cp < 0x80) ? TTF_FONT_W : (2 * TTF_FONT_W);
            if (cx + cell_w > max_x) break;
            API->draw_char_unicode(cx, cy, cp, fg, COLOR_TRANSPARENT, cell_w, TTF_FONT_H);
            cx += cell_w;
        }
    }
}

/* 旧接口: 保留无裁剪版本, 全场景渲染时调用 */
static void draw_tbuf_back(int x, int y, const char *tbuf, int cols, int rows,
                           int max_w, int max_h, unsigned int fg) {
    draw_tbuf_back_clip(x, y, tbuf, cols, rows, max_w, max_h, fg, 0, -1);
}

/* [性能] 脏矩形与 AABB 重叠测试. 返回 1=有交集, 0=不重叠.
 * 坐标均为像素, 含端点. */
static inline int aabb_overlap(int ax1, int ay1, int ax2, int ay2,
                                int bx1, int by1, int bx2, int by2) {
    return !(ax2 < bx1 || bx2 < ax1 || ay2 < by1 || by2 < ay1);
}

/* [性能] 带脏矩形裁剪的 TTF 文字叠加. clip_y1/clip_y2<0 表示无裁剪.
 * 鼠标移动/单窗口变化等局部刷新时, 只渲染脏矩形覆盖的文字, 避免整屏逐字渲染. */
static void render_ttf_text_overlay_clip(struct efm_wm_snapshot *snap,
                                          unsigned int *back_buf, int back_pitch_px,
                                          int back_w, int back_h,
                                          struct aether_render_info *ai,
                                          int clip_x1, int clip_y1,
                                          int clip_x2, int clip_y2) {
    int sw = (int)snap->screen_w;
    int sh = (int)snap->screen_h;
    int tbar_h = snap->taskbar_h;
    int title_h = snap->title_h;
    /* no_clip: 全屏渲染路径 */
    int no_clip = (clip_y1 < 0 || clip_y2 < 0);

    /* 1. 桌面标题 & extra 文本 */
    if (ai && ai->magic == AETHER_INFO_MAGIC && ai->desktop_title.text[0]) {
        int tx = ai->desktop_title.x, ty = ai->desktop_title.y;
        if (no_clip || (ty + TTF_FONT_H > clip_y1 && ty < clip_y2))
            draw_ttf_string_back(tx, ty, ai->desktop_title.text, ai->desktop_title.color);
    } else {
        if (no_clip || (10 + TTF_FONT_H > clip_y1 && 10 < clip_y2))
            draw_ttf_string_back(10, 10, "EFMOS Desktop", 0x00FFFFFF);
    }
    if (ai && ai->magic == AETHER_INFO_MAGIC) {
        for (int i = 0; i < ai->extra_count && i < AETHER_MAX_TEXT; i++) {
            int tx = ai->extra[i].x, ty = ai->extra[i].y;
            if (no_clip || (ty + TTF_FONT_H > clip_y1 && ty < clip_y2))
                draw_ttf_string_back(tx, ty, ai->extra[i].text, ai->extra[i].color);
        }
    }

    /* 窗口排序 */
    struct efm_wm_win_info sorted[EFM_WM_MAX_WIN];
    int cnt = snap->window_count;
    if (cnt > EFM_WM_MAX_WIN) cnt = EFM_WM_MAX_WIN;
    for (int i = 0; i < cnt; i++) sorted[i] = snap->windows[i];
    sort_windows(sorted, cnt);

    /* 2. 逐窗口: 标题 + 关闭按钮 X + tbuf + 光标 */
    for (int k = 0; k < cnt; k++) {
        struct efm_wm_win_info *w = &sorted[k];
        int wx1 = w->x, wy1 = w->y, wx2 = w->x + w->w - 1, wy2 = w->y + w->h - 1;
        /* 窗口 AABB 与脏矩形无交集 → 跳过整个窗口文字渲染 */
        if (!no_clip && !aabb_overlap(wx1, wy1, wx2, wy2, clip_x1, clip_y1, clip_x2, clip_y2))
            continue;

        int tx = w->x + 8;
        int ty = w->y + (title_h - TTF_FONT_H) / 2 + 2;
        if (no_clip || (ty + TTF_FONT_H > clip_y1 && ty < clip_y2))
            draw_ttf_string_back(tx, ty, w->title, 0x00FFFFFF);

        int btn_sz = title_h - 6;
        if (btn_sz < 10) btn_sz = 10;
        int bx = w->x + w->w - btn_sz - 6;
        int by = w->y + 3;
        if (no_clip || (by + btn_sz > clip_y1 && by < clip_y2)) {
            int xx = bx + (btn_sz - TTF_FONT_W) / 2;
            int xy = by + (btn_sz - TTF_FONT_H) / 2;
            draw_ttf_cp_back(xx, xy, (unsigned int)'X', 0x00FFFFFF);
        }

        int cx = w->x + 2;
        int cy = w->y + title_h + 2;
        int cw = w->w - 4;
        int ch2 = w->h - title_h - 4;
        if (cw > 0 && ch2 > 0 && w->tbuf_ptr && w->tbuf_cols > 0 && w->tbuf_rows > 0) {
            int tbuf_clip_y1 = no_clip ? 0 : (clip_y1 < cy ? cy : clip_y1);
            int tbuf_clip_y2 = no_clip ? -1 : (clip_y2 > cy + ch2 ? cy + ch2 - 1 : clip_y2);
            draw_tbuf_back_clip(cx, cy, w->tbuf_ptr, w->tbuf_cols, w->tbuf_rows,
                                cw, ch2, w->c_fg, tbuf_clip_y1, tbuf_clip_y2);
        }

        if (w->focused) {
            int ccx = cx + (w->c_cx * TTF_FONT_W);
            int ccy = cy + (w->c_cy * TTF_FONT_H);
            if (no_clip || (ccy + TTF_FONT_H > clip_y1 && ccy < clip_y2)) {
                if (ccx + 2 <= cx + cw && ccy + TTF_FONT_H <= cy + ch2) {
                    for (int yy = 0; yy < TTF_FONT_H; yy++) {
                        for (int xx2 = 0; xx2 < 2; xx2++) {
                            int bx2 = ccx + xx2, by2 = ccy + yy;
                            if (bx2 >= 0 && by2 >= 0 && bx2 < back_w && by2 < back_h)
                                back_buf[by2 * back_pitch_px + bx2] = w->c_fg;
                        }
                    }
                }
            }
        }

        /* [新架构] 从 gfx_info_ptr 协议绘制 EFS GUI 文字
         * 所有文字坐标都是窗口内容区相对坐标, 由 compositor 统一加 cx/cy 偏移.
         * [修复] 文字必须裁剪到窗口内容区 (cx,cy,cx+cw-1,cy+ch2-1), 否则会画到
         * 窗口边框/标题栏/相邻窗口上, 造成"文字按钮错位"和残影. */
        if (w->gfx_info_ptr && w->gfx_info_ptr->magic == EFM_GFX_MAGIC) {
            struct efm_gfx_info *gi = w->gfx_info_ptr;
            int tc = gi->text_count;
            if (tc < 0) tc = 0;
            if (tc > EFM_GFX_MAX_TEXTS) tc = EFM_GFX_MAX_TEXTS;
            /* 内容区边界 (绝对屏幕坐标) */
            int cax2 = cx + cw - 1;
            int cay2 = cy + ch2 - 1;
            for (int i = 0; i < tc; i++) {
                struct efm_gfx_text *t = &gi->texts[i];
                if (!t->text[0]) continue;
                int tx2 = cx + t->x;
                int ty3 = cy + t->y;
                /* 脏矩形裁剪: 文字至少一个像素在脏矩形内才画 */
                int tx_end = tx2 + 96 * TTF_FONT_W;  /* 粗略上限 */
                int ty_end = ty3 + TTF_FONT_H;
                if (!no_clip && (ty_end < clip_y1 || ty3 > clip_y2 || tx_end < clip_x1 || tx2 > clip_x2))
                    continue;
                /* 内容区裁剪: 完全在内容区外则跳过整条文字 */
                if (tx2 > cax2 || ty3 > cay2 || tx_end < cx || ty_end < cy)
                    continue;
                /* 逐字符裁剪到内容区, 防止越界绘制到窗口外 */
                draw_ttf_string_back_clip(tx2, ty3, t->text, t->color,
                                           cx, cy, cax2, cay2);
            }
        }
    }

    /* 3. 任务栏文字 */
    int ty2 = sh - tbar_h;
    int tbar_y1 = ty2, tbar_y2 = sh - 1;
    /* Logo 正方形: 边长 = tbar_h - 6, 与 render_scene 的 start_sz 一致 */
    int start_sz = tbar_h - 6;
    if (start_sz < 20) start_sz = 20;
    int logo_sz = start_sz;
    int logo_x = 4;  /* 与 render_scene 的 fill_rect(4, ...) 对齐 */
    int logo_y = ty2 + 3 + (start_sz - logo_sz) / 2;  /* 垂直居中在按钮内 */
    /* 开始按钮区域宽度: start_sz + 左右 4px padding */
    int start_btn_area_w = start_sz + 12;
    if (no_clip || (tbar_y2 >= clip_y1 && tbar_y1 <= clip_y2)) {
        /* 开始按钮: 优先用用户定制文字; 默认绘制 EFMOS-logo-b.png 的 alpha logo */
        if (ai && ai->magic == AETHER_INFO_MAGIC && ai->start_text.text[0]) {
            draw_ttf_string_back(ai->start_text.x, ai->start_text.y,
                                 ai->start_text.text, ai->start_text.color);
        } else {
            draw_start_logo(back_buf, back_pitch_px, back_w, back_h,
                            logo_x, logo_y, logo_sz, logo_sz, 0x00FFFFFF);
        }
        int tbx = start_sz + 12;  /* 必须与 render_scene 的 tbx 一致, 否则文字与色块错位 */
        for (int k = 0; k < cnt; k++) {
            struct efm_wm_win_info *w = &sorted[k];
            int bw = 18 * TTF_FONT_W;
            if (bw > sw - tbx - 10) break;
            draw_ttf_string_back(tbx + 4, ty2 + (tbar_h - TTF_FONT_H) / 2,
                                 w->title, 0x00FFFFFF);
            tbx += bw + 4;
        }
    }

    /* 4. 开始菜单项文字 (菜单打开时绘制, 与 render_scene 的色块对齐) */
    if (snap->menu_open && snap->menu_item_count > 0 &&
        ai && ai->magic == AETHER_INFO_MAGIC) {
        int menu_w = 160;
        int item_h = TTF_FONT_H + 8;
        int menu_h = snap->menu_item_count * item_h + 6;
        int menu_x = 4;
        int menu_y = ty2 - menu_h;
        if (menu_y < 0) menu_y = 0;
        unsigned int txt_clr = ai->clr_menu_text ? ai->clr_menu_text : 0x00FFFFFF;
        /* 菜单区域与裁剪框相交时才绘制文字 */
        int menu_y2 = menu_y + menu_h - 1;
        if (no_clip || (menu_y2 >= clip_y1 && menu_y <= clip_y2)) {
            int n = snap->menu_item_count;
            if (n > AETHER_MAX_MENU) n = AETHER_MAX_MENU;
            for (int i = 0; i < n; i++) {
                /* 文字垂直居中在 item_h 内: y = 顶部 + (item_h - TTF_FONT_H)/2 */
                int tx = menu_x + 10;  /* 左侧 10px 内边距 */
                int ty3 = menu_y + 3 + i * item_h + (item_h - TTF_FONT_H) / 2;
                draw_ttf_string_back(tx, ty3, ai->menu_items[i].label, txt_clr);
            }
        }
    }
}

/* 兼容无裁剪的旧接口 (全屏渲染) */
static void render_ttf_text_overlay(struct efm_wm_snapshot *snap,
                                     unsigned int *back_buf, int back_pitch_px,
                                     int back_w, int back_h,
                                     struct aether_render_info *ai) {
    render_ttf_text_overlay_clip(snap, back_buf, back_pitch_px, back_w, back_h, ai,
                                  0, -1, 0, -1);
}

/* ========== Mesa 渲染管线: 分离场景渲染 + 光标叠加 ==========
 * 设计 (与 Mesa EGL surface + cursor plane 等价):
 *   render_scene() → 渲染桌面/窗口/任务栏到 canvas (不含鼠标)
 *   render_cursor() → 把鼠标光标 alpha 叠加到 canvas 之上
 *   present() → 把 canvas blit 到 back buffer → flush 到 front
 *
 * 分离原因:
 *   1. 鼠标光标是纯叠加层, 只画形状像素, 不碰背景 (无 save/restore)
 *   2. 场景变化频率低 (~30FPS), 鼠标可独立高频更新
 *   3. 光标用 alpha 混合, 在任何背景上都清晰可见且无硬边 */

static int g_last_scene_w = 0;
static int g_last_scene_h = 0;

/* render_scene: 渲染桌面 + 窗口 + 任务栏 (不含鼠标) */
/* [性能] 单窗口渲染到 canvas (仅色块: 边框+标题栏+内容区背景, 不含文字/光标).
 * 文字/光标 由 canvas blit → back buffer 之后通过 render_ttf_text_overlay 叠加,
 * 以使用 API->draw_char_unicode 的高质量 TTF UTF-8 CJK 渲染. */
static void render_single_window(struct efm_wm_win_info *w, int title_h) {
    /* 外边框 */
    draw_rect_border(w->x, w->y, w->w, w->h, CLR_BORDER);
    /* 标题栏 (色块, 标题文字由 TTF 叠加层画) */
    unsigned int tbg = w->focused ? CLR_TITLE_ACTIVE : CLR_TITLE_INACT;
    fill_rect(w->x + 2, w->y + 2, w->w - 4, title_h - 2, tbg);
    /* 关闭按钮 (色块, X 文字由 TTF 叠加层画) */
    int btn_sz = title_h - 6;
    if (btn_sz < 10) btn_sz = 10;
    int bx = w->x + w->w - btn_sz - 6;
    int by = w->y + 3;
    unsigned int cbg = w->close_hover ? CLR_CLOSE_HOVER : CLR_CLOSE_BG;
    fill_rect(bx, by, btn_sz, btn_sz, cbg);
    /* 内容区处理:
     * - EFS 图形窗口 (has_efm_gfx=1): 从 back buffer 复制 EFS 程序绘制的图形像素
     *   到 canvas, 确保 canvas blit 不覆盖登录按钮/矩形等 EFS 图形.
     * - 普通 shell 窗口: 填充 c_bg 内容背景色.
     * 注意: 终端 tbuf 文字和光标一律不画到 canvas, 由 TTF 叠加层统一画. */
    int cx = w->x + 2;
    int cy = w->y + title_h + 2;
    int cw = w->w - 4;
    int ch2 = w->h - title_h - 4;
    /* [新架构 · 核心修复] gfx_info_ptr 协议优先级最高:
     * EFS 程序不再直接写像素到 back buffer, 而是构造渲染信息 (rects + texts)
     * 通过 set_gfx_info 提交给 compositor 统一绘制. 这样彻底消除:
     *   1. EFS ↔ compositor 争用 back buffer → 残影/撕裂
     *   2. 内核 WM 坐标转换与 compositor 坐标不一致 → 文字/按钮错位
     * 所有坐标都是窗口内容区相对坐标 (0,0=内容区左上角), 由 compositor 统一加偏移. */
    if (w->gfx_info_ptr && w->gfx_info_ptr->magic == EFM_GFX_MAGIC) {
        struct efm_gfx_info *gi = w->gfx_info_ptr;
        /* 内容区背景: 先填充 c_bg (如果协议没指定背景, 则用默认).
         * 协议第 0 个 rect 可能是全屏背景, 但 EFS 程序应提供. 为安全起见, 先填默认. */
        fill_rect(cx, cy, cw, ch2, w->c_bg);
        /* 绘制所有矩形 (按钮/面板/边框) */
        int rc = gi->rect_count;
        if (rc < 0) rc = 0;
        if (rc > EFM_GFX_MAX_RECTS) rc = EFM_GFX_MAX_RECTS;
        /* 内容区裁剪边界 (rx 必须在 cx..cx+cw-1 内, ry 必须在 cy..cy+ch2-1 内) */
        int cax1 = cx, cay1 = cy, cax2 = cx + cw - 1, cay2 = cy + ch2 - 1;
        for (int i = 0; i < rc; i++) {
            struct efm_gfx_rect *r = &gi->rects[i];
            /* [修复] 保留原始坐标用于边框, 裁剪只影响填充范围.
             * 原实现把 rx1/ry1/rx2/ry2 裁剪后同时用于填充和边框,
             * 导致按钮被内容区裁剪时右/下边框画在裁剪后的边界 (按钮看起来变窄).
             * 现在填充用 fill_rect_clip (裁剪到内容区), 边框用 draw_rect_border_clip
             * (原始坐标画 4 条边, 每条边单独裁剪), 边框位置始终正确. */
            int orig_x = cx + r->x;
            int orig_y = cy + r->y;
            /* 跳过完全在内容区外的 rect */
            if (orig_x > cax2 || orig_y > cay2 ||
                orig_x + r->w - 1 < cax1 || orig_y + r->h - 1 < cay1)
                continue;
            /* 填充: 裁剪到内容区 */
            fill_rect_clip(orig_x, orig_y, r->w, r->h, r->fill_color,
                           cax1, cay1, cax2, cay2);
            /* 边框: 用原始坐标画, 每条边裁剪到内容区 */
            if (r->has_border) {
                draw_rect_border_clip(orig_x, orig_y, r->w, r->h, r->border_color,
                                      cax1, cay1, cax2, cay2);
            }
        }
        /* 绘制所有文字 — 在 render_ttf_text_overlay_clip 中处理 (需 draw_char_unicode 坐标) */
    } else if (w->has_efm_gfx) {
        /* [旧兼容路径] 有 EFS 图形但没有 gfx_info_ptr 协议数据:
         * 从 back buffer 复制像素 (旧实现, 有残影风险) */
        void *back_ptr = 0; int back_pitch = 0, back_w = 0, back_h = 0;
        if (API->get_backbuffer && API->get_backbuffer(&back_ptr, &back_pitch, &back_w, &back_h) == 0 && back_ptr) {
            unsigned int *bb = (unsigned int*)back_ptr;
            int bp = back_pitch / 4;
            for (int y = 0; y < ch2; y++) {
                int sy = cy + y;
                if (sy < 0 || sy >= g_ch || sy >= back_h) continue;
                for (int x = 0; x < cw; x++) {
                    int sx = cx + x;
                    if (sx < 0 || sx >= g_cw || sx >= back_w) continue;
                    g_canvas[sy * g_pitch + sx] = bb[sy * bp + sx];
                }
            }
        } else {
            fill_rect(cx, cy, cw, ch2, w->c_bg);
        }
    } else {
        fill_rect(cx, cy, cw, ch2, w->c_bg);
    }
}

static void render_scene(struct efm_wm_snapshot *snap) {
    int sw = (int)snap->screen_w;
    int sh = (int)snap->screen_h;
    int tbar_h = snap->taskbar_h;
    g_last_scene_w = sw;
    g_last_scene_h = sh;
    /* 更新当前鼠标位置 (供 render_single_window 恢复图形时跳过鼠标区域) */
    g_cur_mx = snap->mouse_x;
    g_cur_my = snap->mouse_y;
    g_cur_mouse_visible = snap->mouse_visible;

    /* 读取 aether_render_info (efmAether 提供的桌面颜色方案) */
    struct aether_render_info *ai =
        (struct aether_render_info *)(unsigned long)AETHER_INFO_ADDR;
    unsigned int clr_bg = CLR_BG;
    unsigned int clr_taskbar_bg = CLR_TASKBAR_BG;
    unsigned int clr_taskbar_btn = CLR_TASKBAR_BTN;
    unsigned int clr_taskbar_act = CLR_TASKBAR_ACT;
    unsigned int clr_menu_bg = CLR_TASKBAR_BG;
    unsigned int clr_menu_item = CLR_TASKBAR_BTN;
    unsigned int clr_menu_hover = CLR_TASKBAR_ACT;
    unsigned int clr_menu_border = CLR_BORDER;
    if (ai && ai->magic == AETHER_INFO_MAGIC) {
        if (ai->clr_bg) clr_bg = ai->clr_bg;
        if (ai->clr_taskbar_bg) clr_taskbar_bg = ai->clr_taskbar_bg;
        if (ai->clr_taskbar_btn) clr_taskbar_btn = ai->clr_taskbar_btn;
        if (ai->clr_taskbar_act) clr_taskbar_act = ai->clr_taskbar_act;
        if (ai->clr_menu_bg) clr_menu_bg = ai->clr_menu_bg;
        if (ai->clr_menu_item) clr_menu_item = ai->clr_menu_item;
        if (ai->clr_menu_hover) clr_menu_hover = ai->clr_menu_hover;
        if (ai->clr_menu_border) clr_menu_border = ai->clr_menu_border;
    }

    /* 1. 桌面背景 (色块, 桌面文字 + Start 文字由 TTF 叠加层画) */
    fill_rect(0, 0, sw, sh, clr_bg);

    /* 2. 窗口排序 + 绘制 (底→顶, 仅色块, 文字由 TTF 叠加层画) */
    struct efm_wm_win_info sorted[EFM_WM_MAX_WIN];
    int cnt = snap->window_count;
    if (cnt > EFM_WM_MAX_WIN) cnt = EFM_WM_MAX_WIN;
    for (int i = 0; i < cnt; i++) sorted[i] = snap->windows[i];
    sort_windows(sorted, cnt);
    for (int k = 0; k < cnt; k++) render_single_window(&sorted[k], snap->title_h);

    /* 3. 任务栏 (仅色块, 按钮文字由 TTF 叠加层画) */
    int ty = sh - tbar_h;
    fill_rect(0, ty, sw, tbar_h, clr_taskbar_bg);
    /* 开始按钮: 正方形, 边长 = tbar_h - 6 (上下各 3px 间距) */
    int start_sz = tbar_h - 6;
    if (start_sz < 20) start_sz = 20;
    /* 开始按钮按下态: 菜单打开时用激活色高亮 */
    unsigned int clr_start_btn = snap->menu_open ? clr_menu_hover : clr_taskbar_btn;
    fill_rect(4, ty + 3, start_sz, start_sz, clr_start_btn);
    int tbx = start_sz + 12;  /* 窗口标题按钮起始 x */
    for (int k = 0; k < cnt; k++) {
        struct efm_wm_win_info *w = &sorted[k];
        int bw = 18 * TTF_FONT_W;
        if (bw > sw - tbx - 10) break;
        unsigned int bbg = w->focused ? clr_taskbar_act : clr_taskbar_btn;
        fill_rect(tbx, ty + 2, bw, tbar_h - 4, bbg);
        tbx += bw + 4;
    }

    /* 4. 开始菜单弹出 (仅色块, 文字由 TTF 叠加层画)
     * 位置: 左下角开始按钮正上方, 类似 Windows 开始菜单.
     * 宽度: 160px (容纳 12 个 CJK 字符或 ~20 个 ASCII 字符)
     * 每项高度: TTF_FONT_H + 8 (上下各 4px 间距) */
    if (snap->menu_open && snap->menu_item_count > 0) {
        int menu_w = 160;
        int item_h = TTF_FONT_H + 8;
        int menu_h = snap->menu_item_count * item_h + 6;  /* 底部 6px padding */
        int menu_x = 4;  /* 与开始按钮左对齐 */
        int menu_y = ty - menu_h;  /* 紧贴任务栏上方 */
        if (menu_y < 0) menu_y = 0;
        /* 背景 + 边框 */
        fill_rect(menu_x, menu_y, menu_w, menu_h, clr_menu_bg);
        draw_rect_border(menu_x, menu_y, menu_w, menu_h, clr_menu_border);
        /* 菜单项 */
        for (int i = 0; i < snap->menu_item_count && i < AETHER_MAX_MENU; i++) {
            int iy = menu_y + 3 + i * item_h;
            unsigned int iclr = (i == snap->menu_hover_idx) ? clr_menu_hover : clr_menu_item;
            fill_rect(menu_x + 3, iy, menu_w - 6, item_h, iclr);
        }
    }
}

/* render_cursor: 鼠标光标叠加. [已弃用] 鼠标不再画到 canvas (会破坏 "干净 canvas"
 * 假设导致鼠标拖尾), 改由 present 阶段直接画到 back buffer. 保留函数避免外部引用断裂. */
static void render_cursor(struct efm_wm_snapshot *snap) {
    if (snap->mouse_visible) {
        draw_mouse(snap->mouse_x, snap->mouse_y);
    }
}

/* ========== 主程序 ========== */
/* 注: _start 与 compositor_main/gl_render_loop 的前置声明已移至文件顶部
 * (load_gl_functions 之前), 以保证 _start 位于 .text 段起始 (load_addr). */

/* ========== [优化] VBO 初始化 ==========
 * 创建一个静态 VBO, 每帧用 glBufferData(gl_DYNAMIC_DRAW) 更新顶点数据.
 * 顶点属性: location 0 = vec2 position, location 1 = vec3 color.
 * 注意: 用 compatibility profile 的 glOrtho + 顶点属性, 不需要 shader. */
static void vbo_init(void) {
    if (!p_glGenBuffers || !p_glBindBuffer || !p_glBufferData ||
        !p_glEnableVertexAttribArray || !p_glVertexAttribPointer || !p_glDrawArrays) {
        g_vbo_ready = 0;
        return;
    }
    p_glGenBuffers(1, &g_vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    /* 预分配 VBO_MAX_VERTS * VBO_STRIDE * sizeof(float) 字节 */
    p_glBufferData(GL_ARRAY_BUFFER,
                   VBO_MAX_VERTS * VBO_STRIDE * sizeof(float),
                   NULL, GL_DYNAMIC_DRAW);
    g_vbo_ready = 1;
}

/* [优化] VBO 批量提交: 把收集的顶点一次性 glDrawArrays 提交.
 * verts: 顶点数组, 每顶点 VBO_STRIDE 个 float (x,y,r,g,b)
 * count: 顶点数 (必须为 4 的倍数, GL_QUADS) */
static void vbo_draw(const float *verts, int count) {
    if (!g_vbo_ready || count <= 0) return;
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, count * VBO_STRIDE * sizeof(float),
                   verts, GL_DYNAMIC_DRAW);
    /* position = attribute 0: vec2 float, stride = 5*4 = 20 bytes, offset 0 */
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 2, 0x1406 /*GL_FLOAT*/, 0, VBO_STRIDE * 4, (void*)0);
    /* color = attribute 3 (compat profile glColor 风格): vec3 float, offset 8 */
    p_glEnableVertexAttribArray(3);
    p_glVertexAttribPointer(3, 3, 0x1406 /*GL_FLOAT*/, 0, VBO_STRIDE * 4, (void*)(2 * 4));
    p_glDrawArrays(GL_QUADS, 0, count);
    p_glDisableVertexAttribArray(0);
    p_glDisableVertexAttribArray(3);
}

/* ========== [优化] FBO 初始化 ==========
 * 创建一个降分辨率的 FBO (屏幕分辨率 * FBO_SCALE).
 * 渲染流程: 绑定 FBO → 在低分辨率渲染 → 绑定默认 FBO → blit 到 back buffer.
 * 像素填充率降为 FBO_SCALE^2, llvmpipe 渲染时间显著降低. */
static void fbo_init(int screen_w, int screen_h) {
    if (!p_glGenFramebuffers || !p_glBindFramebuffer || !p_glFramebufferTexture2D ||
        !p_glGenTextures || !p_glBindTexture || !p_glTexImage2D || !p_glTexParameteri ||
        !p_glBlitFramebuffer) {
        g_fbo_ready = 0;
        return;
    }
    g_fbo_w = (int)(screen_w * FBO_SCALE);
    g_fbo_h = (int)(screen_h * FBO_SCALE);
    if (g_fbo_w < 2 || g_fbo_h < 2) { g_fbo_ready = 0; return; }

    /* 创建 FBO 纹理 (颜色附件) */
    p_glGenTextures(1, &g_fbo_tex);
    p_glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* 内部格式 GL_RGBA8 = 0x8058, 格式 GL_RGBA = 0x1908, 类型 GL_UNSIGNED_BYTE = 0x1401 */
    p_glTexImage2D(GL_TEXTURE_2D, 0, 0x8058, g_fbo_w, g_fbo_h, 0,
                   0x1908, 0x1401, NULL);

    /* 创建 FBO 并附加纹理 */
    p_glGenFramebuffers(1, &g_fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, g_fbo_tex, 0);
    GLenum status = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        g_fbo_ready = 0;
        return;
    }
    /* 解绑 FBO, 回到默认 framebuffer */
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_fbo_ready = 1;
}

/* [优化] FBO 绑定 + 设置低分辨率视口 */
static void fbo_bind_render(void) {
    if (!g_fbo_ready) return;
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    p_glViewport(0, 0, g_fbo_w, g_fbo_h);
    /* FBO 坐标系: 重新设置正交投影到 FBO 分辨率 */
    p_glMatrixMode(GL_PROJECTION);
    p_glLoadIdentity();
    p_glOrtho(0.0f, (GLfloat)g_fbo_w, (GLfloat)g_fbo_h, 0.0f, -1.0f, 1.0f);
    p_glMatrixMode(GL_MODELVIEW);
    p_glLoadIdentity();
}

/* [优化] FBO 解绑 + blit 到默认 framebuffer (back buffer).
 * 把 FBO 内容 (g_fbo_w x g_fbo_h) 放大到屏幕分辨率 (sw x sh).
 * 源: FBO, 目标: 默认 framebuffer (EGL surface = back buffer). */
static void fbo_unbind_and_blit(int screen_w, int screen_h) {
    if (!g_fbo_ready) return;
    /* 绑定默认 framebuffer (0 = EGL window surface) */
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    /* 恢复默认 framebuffer 视口 */
    p_glViewport(0, 0, screen_w, screen_h);
    /* blit: FBO (0,0,fw,fh) → screen (0,0,sw,sh), GL_LINEAR 放大过滤 */
    p_glBlitFramebuffer(0, 0, g_fbo_w, g_fbo_h,
                        0, 0, screen_w, screen_h,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

/* ========== [优化] 鼠标光标 alpha 纹理 ==========
 * 用户要求: "仅渲染指针本体, 不要渲染整个矩形".
 * 方案: 生成一个 16x16 RGBA 纹理, 只有箭头本体像素 alpha=255 (不透明),
 *       其余像素 alpha=0 (完全透明, 不绘制). 用 alpha blend 渲染,
 *       透明像素被丢弃, 只有箭头形状出现在屏幕上, 无矩形背景.
 *
 * 箭头形状 (11x16, 白色填充 + 黑色描边):
 *   行 0: 1 pixel  (尖端)
 *   行 1-7: 逐渐变宽 (2..8 pixels)
 *   行 8-10: 分叉尾巴 (3 pixels) */
static GLuint g_cursor_tex = 0;
static int    g_cursor_ready = 0;

/* 生成 16x16 鼠标光标 RGBA 纹理.
 * 像素格式: RGBA, 每像素 4 字节.
 * 箭头本体: 白色 (255,255,255,255)
 * 描边:     黑色 (0,0,0,255)
 * 背景:     透明 (0,0,0,0) */
static void cursor_texture_init(void) {
    if (!p_glGenTextures || !p_glBindTexture || !p_glTexImage2D || !p_glTexParameteri) {
        g_cursor_ready = 0;
        return;
    }
    /* 箭头形状位图: 每行 16 bit, bit=1 表示该像素是箭头本体 */
    static const unsigned short shape[16] = {
        0x8000, /* 行 0:  X..............  尖端 */
        0xC000, /* 行 1:  XX.............  */
        0xE000, /* 行 2:  XXX............  */
        0xF000, /* 行 3:  XXXX...........  */
        0xF800, /* 行 4:  XXXXX..........  */
        0xFC00, /* 行 5:  XXXXXX.........  */
        0xFE00, /* 行 6:  XXXXXXX........  */
        0xFF00, /* 行 7:  XXXXXXXX.......  最宽 */
        0xF800, /* 行 8:  XXXXX..........  变窄 */
        0x9800, /* 行 9:  X..XX.........  分叉 */
        0x88C0, /* 行 10: X...XX.......  */
        0x0860, /* 行 11: ....XX.X.....  */
        0x0430, /* 行 12: .....X..X....  */
        0x0218, /* 行 13: ......X..X...  */
        0x010C, /* 行 14: .......X..X..  */
        0x0000, /* 行 15: (空) */
    };
    unsigned char tex[16 * 16 * 4];
    /* 先全部清为透明 */
    for (int i = 0; i < 16 * 16; i++) {
        tex[i * 4 + 0] = 0;
        tex[i * 4 + 1] = 0;
        tex[i * 4 + 2] = 0;
        tex[i * 4 + 3] = 0;
    }
    /* 填充箭头本体: 白色不透明 */
    for (int y = 0; y < 16; y++) {
        unsigned short mask = shape[y];
        for (int x = 0; x < 16; x++) {
            if (mask & (0x8000 >> x)) {
                int idx = (y * 16 + x) * 4;
                tex[idx + 0] = 255;  /* R */
                tex[idx + 1] = 255;  /* G */
                tex[idx + 2] = 255;  /* B */
                tex[idx + 3] = 255;  /* A = 不透明 */
            }
        }
    }
    /* 描边: 在箭头本体边缘外侧画黑色像素 (右/下方相邻, 且该位置不在本体中) */
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 15; x++) {
            int in_shape = (shape[y] & (0x8000 >> x)) != 0;
            if (!in_shape) {
                /* 检查左侧或上方是否在箭头内 → 此处是描边位置 */
                int left_in = (x > 0) && (shape[y] & (0x8000 >> (x - 1)));
                int up_in = (y > 0) && (shape[y - 1] & (0x8000 >> x));
                if (left_in || up_in) {
                    int idx = (y * 16 + x) * 4;
                    tex[idx + 0] = 0;    /* R */
                    tex[idx + 1] = 0;    /* G */
                    tex[idx + 2] = 0;    /* B */
                    tex[idx + 3] = 255;  /* A = 不透明 */
                }
            }
        }
    }
    p_glGenTextures(1, &g_cursor_tex);
    p_glBindTexture(GL_TEXTURE_2D, g_cursor_tex);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexImage2D(GL_TEXTURE_2D, 0, 0x8058 /*GL_RGBA8*/, 16, 16, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, tex);
    g_cursor_ready = 1;
}

/* 渲染鼠标光标: 用 alpha 纹理, 仅绘制箭头本体像素, 无矩形背景.
 * (mx,my) = 光标左上角屏幕坐标. scale = FBO 缩放因子. */
static void cursor_render(int mx, int my, float scale) {
    if (!g_cursor_ready || !p_glEnable || !p_glDisable || !p_glBindTexture ||
        !p_glBegin || !p_glEnd || !p_glTexCoord2f || !p_glBlendFunc) return;
    /* 启用 alpha blend: 透明像素 (alpha=0) 不写入, 不透明像素 (alpha=255) 替换 */
    p_glEnable(GL_BLEND);
    p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    p_glEnable(GL_TEXTURE_2D);
    p_glBindTexture(GL_TEXTURE_2D, g_cursor_tex);
    if (p_glTexEnvi) p_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    /* 画一个 16x16 quad, 纹理坐标覆盖整个光标纹理.
     * alpha=0 的像素被 blend 丢弃, 只有箭头本体出现在屏幕上. */
    float x0 = (float)mx * scale;
    float y0 = (float)my * scale;
    float x1 = (float)(mx + 16) * scale;
    float y1 = (float)(my + 16) * scale;
    p_glBegin(GL_QUADS);
    p_glTexCoord2f(0.0f, 0.0f); p_glVertex2f(x0, y0);
    p_glTexCoord2f(1.0f, 0.0f); p_glVertex2f(x1, y0);
    p_glTexCoord2f(1.0f, 1.0f); p_glVertex2f(x1, y1);
    p_glTexCoord2f(0.0f, 1.0f); p_glVertex2f(x0, y1);
    p_glEnd();
    /* 恢复状态: 禁用纹理和 blend, 不影响后续窗口渲染 */
    p_glDisable(GL_TEXTURE_2D);
    p_glDisable(GL_BLEND);
}

/* ========== [优化] 窗口动画状态 ==========
 * 为系统添加动画, 使窗口操作更流畅:
 *   - 位置插值 (lerp): 窗口目标位置变化时, 显示位置平滑过渡, 避免跳变
 *   - 出现淡入: 新窗口 alpha 从 0→1 渐变出现
 *   - 焦点过渡: 焦点切换时标题栏颜色平滑过渡
 * 每帧用 lerp 趋近目标值, 因子 0.3 (约 3 帧到位, ~50ms, 视觉流畅无延迟感). */
struct win_anim {
    int   wid;
    int   used;
    float disp_x, disp_y;      /* 当前显示位置 (lerp 趋近 target) */
    float alpha;               /* 当前 alpha (0=透明, 1=不透明) */
    int   prev_alive;          /* 上一帧是否 alive (检测新窗口) */
};
static struct win_anim g_anim[EFM_WM_MAX_WIN];
static int g_anim_init_done = 0;

#define ANIM_LERP  0.3f        /* 位置插值因子 (越大越快到位) */
#define ANIM_ALPHA_STEP 0.1f   /* alpha 每帧增量 (10帧≈160ms 淡入) */

/* 更新窗口动画状态: 读取 WM 快照, 更新 display 位置和 alpha */
static void anim_update(struct efm_wm_snapshot *snap) {
    if (!g_anim_init_done) {
        for (int i = 0; i < EFM_WM_MAX_WIN; i++) g_anim[i].used = 0;
        g_anim_init_done = 1;
    }
    /* 标记本帧 alive 的窗口 */
    int cnt = snap->window_count;
    if (cnt > EFM_WM_MAX_WIN) cnt = EFM_WM_MAX_WIN;
    for (int i = 0; i < cnt; i++) {
        int wid = snap->windows[i].wid;
        /* 找到或分配 anim 槽 */
        struct win_anim *a = 0;
        for (int j = 0; j < EFM_WM_MAX_WIN; j++) {
            if (g_anim[j].used && g_anim[j].wid == wid) { a = &g_anim[j]; break; }
        }
        if (!a) {
            for (int j = 0; j < EFM_WM_MAX_WIN; j++) {
                if (!g_anim[j].used) { a = &g_anim[j]; a->wid = wid; a->used = 1;
                    a->disp_x = (float)snap->windows[i].x;
                    a->disp_y = (float)snap->windows[i].y;
                    a->alpha = 0.0f;  /* 新窗口从透明开始淡入 */
                    break; }
            }
        }
        if (!a) continue;
        a->prev_alive = 1;
        /* lerp 显示位置 → 目标位置 */
        float tx = (float)snap->windows[i].x;
        float ty = (float)snap->windows[i].y;
        a->disp_x += (tx - a->disp_x) * ANIM_LERP;
        a->disp_y += (ty - a->disp_y) * ANIM_LERP;
        /* alpha 淡入 */
        if (a->alpha < 1.0f) {
            a->alpha += ANIM_ALPHA_STEP;
            if (a->alpha > 1.0f) a->alpha = 1.0f;
        }
        /* 回写显示位置到快照 (渲染用 disp 位置) */
        snap->windows[i].x = (int)(a->disp_x + 0.5f);
        snap->windows[i].y = (int)(a->disp_y + 0.5f);
    }
    /* 清理消失的窗口 */
    for (int j = 0; j < EFM_WM_MAX_WIN; j++) {
        if (g_anim[j].used && !g_anim[j].prev_alive) {
            g_anim[j].used = 0;
        }
        g_anim[j].prev_alive = 0;
    }
}

/* ========== Mesa GL 渲染主循环 ==========
 * 用 eglCreateContext + glClear/glBegin 真正调用 Mesa swrast 渲染.
 * 渲染管线: glClear(清屏) → glBegin(GL_QUADS) 画窗口 → glEnd → eglSwapBuffers.
 * eglSwapBuffers 内部走 GBM BO → blit_to_screen → GOP framebuffer. */
static void gl_render_loop(void) {
    /* EGL 初始化 */
    g_egl_dpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!g_egl_dpy) {
        API->print("compositor: eglGetDisplay failed, fallback to softpipe\n");
        return;
    }
    EGLint major = 0, minor = 0;
    if (!p_eglInitialize(g_egl_dpy, &major, &minor)) {
        API->print("compositor: eglInitialize failed\n");
        return;
    }
    API->print("compositor: EGL initialized (");
    API->print("1.5");
    API->print(")\n");

    p_eglBindAPI(EGL_OPENGL_API);

    /* 选择 config: RGBA8888, OpenGL renderable, window surface */
    EGLint cfg_attribs[] = {
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num_cfg = 0;
    if (!p_eglChooseConfig(g_egl_dpy, cfg_attribs, &cfg, 1, &num_cfg) || num_cfg == 0) {
        API->print("compositor: eglChooseConfig failed\n");
        return;
    }

    /* 创建 EGL window surface (EGLNativeWindowType=0, EFMOS 平台忽略) */
    g_egl_surf = p_eglCreateWindowSurface(g_egl_dpy, cfg, (EGLNativeWindowType)0, 0);
    if (!g_egl_surf) {
        API->print("compositor: eglCreateWindowSurface failed\n");
        return;
    }

    /* 创建 OpenGL context (compatibility profile, 可用 glBegin/glEnd 立即模式) */
    EGLint ctx_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
        EGL_NONE
    };
    g_egl_ctx = p_eglCreateContext(g_egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (!g_egl_ctx) {
        API->print("compositor: eglCreateContext failed\n");
        return;
    }
    if (!p_eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_egl_ctx)) {
        API->print("compositor: eglMakeCurrent failed\n");
        return;
    }
    g_gl_ready = 1;
    API->print("compositor: Mesa GL context ready, switching to GL rendering\n");

    /* 设置正交投影: 左上原点, 与像素坐标一致 */
    unsigned int hr = 0, vr = 0, ppsl = 0;
    unsigned int *fb_base = 0;
    API->get_fb_info(&hr, &vr, &ppsl, &fb_base);
    int sw = (int)hr, sh = (int)vr;
    p_glViewport(0, 0, sw, sh);
    p_glMatrixMode(GL_PROJECTION);
    p_glLoadIdentity();
    p_glOrtho(0.0f, (GLfloat)sw, (GLfloat)sh, 0.0f, -1.0f, 1.0f);  /* Y 翻转: 左上原点 */
    p_glMatrixMode(GL_MODELVIEW);
    p_glLoadIdentity();
    p_glDisable(GL_DEPTH_TEST);

    /* [优化] 初始化 VBO 批量渲染 (函数指针齐全时启用, 否则回退 glBegin/glEnd) */
    vbo_init();
    if (g_vbo_ready) API->print("compositor: VBO batch rendering enabled\n");
    /* [优化] 初始化 FBO 降分辨率渲染 (1080p→540p, 像素填充率降 75%) */
    fbo_init(sw, sh);
    if (g_fbo_ready) {
        char buf[80];
        int pos = 0;
        pos += fmt_str(buf + pos, "compositor: FBO render at ");
        pos += fmt_int(buf + pos, g_fbo_w);
        pos += fmt_str(buf + pos, "x");
        pos += fmt_int(buf + pos, g_fbo_h);
        pos += fmt_str(buf + pos, " (scale 0.5)\n");
        buf[pos] = 0;
        API->print(buf);
    }
    /* [优化] 初始化鼠标光标 alpha 纹理 (仅渲染指针本体, 无矩形背景) */
    cursor_texture_init();
    if (g_cursor_ready) API->print("compositor: cursor alpha texture ready\n");

    /* 通知内核: 合成器接管渲染 */
    API->set_compositor_active(1);

    /* WM 快照缓冲 */
    struct efm_wm_snapshot snap;
    int idle_frames = 0;

    /* [性能] TSC 校准: 测量实际 TSC 频率, 用于自适应帧率控制和准确计时 */
    unsigned long long g_tsc_per_ms = TSC_PER_MS;
    {
        unsigned long long tc0 = rdtsc_now();
        API->sleep_ms(20);
        unsigned long long tc1 = rdtsc_now();
        unsigned long long measured = (tc1 - tc0) / 20;
        if (measured >= 100000 && measured <= 10000000) g_tsc_per_ms = measured;
    }

    /* [优化] GL 主循环: 60 FPS + 脏矩形检测 + 鼠标光标 + 计时桩
     * 优化点:
     *   1. 脏矩形检测: 场景/鼠标无变化时跳过 glClear/glBegin/eglSwapBuffers,
     *      CPU 占用从 ~100% 降到 ~5% (静态桌面).
     *   2. 鼠标光标: GL_QUADS 画白色箭头, 无背景方块 (alpha=1 不透明, 但形状
     *      是箭头而非方块, 视觉上无"背景不透明"问题).
     *   3. 60 FPS: 16ms 间隔 (vs 旧 33ms = 30 FPS).
     *   4. 计时桩: 每秒输出帧率 + 渲染/swap 耗时, 定位瓶颈. */
    unsigned long long last_report_tsc = rdtsc_now();
    unsigned long long render_tsc_sum = 0;
    unsigned long long swap_tsc_sum = 0;
    int frame_count = 0;
    int rendered_count = 0;

    while (1) {
        unsigned long long gl_frame_start = rdtsc_now();
        int n = API->get_wm_snapshot(&snap, (int)sizeof(snap));
        if (n < 0) { API->sleep_ms(16); API->yield(); continue; }

        /* [优化] 窗口动画: 位置插值 (拖动平滑) + 淡入 (新窗口渐变出现).
         * anim_update 修改 snap.windows[i].x/y 为插值后的显示位置. */
        anim_update(&snap);
        /* 动画期间窗口在移动, 必须每帧重渲染 */
        int animating = 0;
        for (int i = 0; i < snap.window_count && i < EFM_WM_MAX_WIN; i++) {
            /* 检查是否有窗口还在动画中 (disp 与 target 差距 > 1px) */
            for (int j = 0; j < EFM_WM_MAX_WIN; j++) {
                if (g_anim[j].used && g_anim[j].wid == snap.windows[i].wid) {
                    float dx = (float)snap.windows[i].x - g_anim[j].disp_x;
                    float dy = (float)snap.windows[i].y - g_anim[j].disp_y;
                    if (dx > 1.0f || dx < -1.0f || dy > 1.0f || dy < -1.0f) animating = 1;
                    if (g_anim[j].alpha < 1.0f) animating = 1;
                    break;
                }
            }
        }
        if (animating) g_scene_dirty = 1;

        /* ===== 脏矩形检测: 对比本帧与上一帧状态 ===== */
        int cur_focused = -1;
        for (int i = 0; i < snap.window_count; i++) {
            if (snap.windows[i].focused) { cur_focused = snap.windows[i].wid; break; }
        }
        if (snap.window_count != g_last_window_count ||
            cur_focused != g_last_focused_wid)
            g_scene_dirty = 1;
        if (snap.mouse_x != g_last_mx || snap.mouse_y != g_last_my ||
            snap.mouse_visible != g_last_mouse_visible)
            g_mouse_dirty = 1;
        /* 周期性强制刷新 (每 60 帧 = 1 秒): 兜底, 防 tbuf 文本变化漏检 */
        if (++idle_frames >= 60) { g_scene_dirty = 1; idle_frames = 0; }

        int need_render = g_scene_dirty || g_mouse_dirty;
        int mouse_only = g_mouse_dirty && !g_scene_dirty;  /* 仅鼠标移动, 场景无变化 */

        if (need_render) {
            unsigned long long tr0 = rdtsc_now();

            /* [性能优化] 鼠标单独更新: 场景无变化时跳过 GL 渲染,
             * 直接 blit 上一帧的 FBO 内容 + 在新位置画光标.
             * 节省 llvmpipe 清屏 + 窗口/任务栏 GL 绘制 (约 15-30ms). */
            if (!mouse_only) {
                /* 全场景 GL 渲染 */
            float scale = FBO_SCALE;
            if (g_fbo_ready) {
                fbo_bind_render();   /* 绑定 FBO, 视口和投影设为 g_fbo_w x g_fbo_h */
            } else {
                scale = 1.0f;
            }

            /* 1. 清屏: 深蓝紫背景 (CLR_BG = 0x001A1A2E) */
            p_glClearColor(0.102f, 0.102f, 0.180f, 1.0f);
            p_glClear(GL_COLOR_BUFFER_BIT);

            /* 2. 画窗口 (GL_QUADS):
             *    [优化] VBO 批量渲染: 收集所有 quad 顶点到数组, 一次 glDrawArrays.
             *    若 VBO 不可用, 回退到 glBegin/glEnd 立即模式. */
            int cnt = snap.window_count;
            if (cnt > EFM_WM_MAX_WIN) cnt = EFM_WM_MAX_WIN;
            struct efm_wm_win_info sorted[EFM_WM_MAX_WIN];
            for (int i = 0; i < cnt; i++) sorted[i] = snap.windows[i];
            sort_windows(sorted, cnt);

            if (g_vbo_ready) {
                /* [优化] VBO 路径: 收集顶点, 一次提交 */
                float verts[VBO_MAX_VERTS * VBO_STRIDE];
                int nv = 0;
                #define VBO_PUSH(x, y, r, g, b) do { \
                    if (nv < VBO_MAX_VERTS) { \
                        verts[nv*5+0] = (float)(x) * scale; \
                        verts[nv*5+1] = (float)(y) * scale; \
                        verts[nv*5+2] = (r); verts[nv*5+3] = (g); verts[nv*5+4] = (b); \
                        nv++; \
                    } } while(0)
                for (int k = 0; k < cnt; k++) {
                    struct efm_wm_win_info *w = &sorted[k];
                    float tr, tg, tb;
                    if (w->focused) { tr = 0.098f; tg = 0.463f; tb = 0.824f; }
                    else            { tr = 0.218f; tg = 0.278f; tb = 0.310f; }
                    /* 窗口阴影 (深灰色, 偏移 4px 右下, 模拟投影) */
                    VBO_PUSH(w->x + 4,     w->y + 4,     0.08f, 0.08f, 0.10f);
                    VBO_PUSH(w->x + w->w + 4, w->y + 4,  0.08f, 0.08f, 0.10f);
                    VBO_PUSH(w->x + w->w + 4, w->y + w->h + 4, 0.08f, 0.08f, 0.10f);
                    VBO_PUSH(w->x + 4,     w->y + w->h + 4, 0.08f, 0.08f, 0.10f);
                    /* 标题栏 */
                    VBO_PUSH(w->x,         w->y,             tr, tg, tb);
                    VBO_PUSH(w->x + w->w,  w->y,             tr, tg, tb);
                    VBO_PUSH(w->x + w->w,  w->y + snap.title_h, tr, tg, tb);
                    VBO_PUSH(w->x,         w->y + snap.title_h, tr, tg, tb);
                    /* 内容区 */
                    float cr = ((w->c_bg >> 16) & 0xFF) / 255.0f;
                    float cg = ((w->c_bg >> 8)  & 0xFF) / 255.0f;
                    float cb = ( w->c_bg        & 0xFF) / 255.0f;
                    int cy = w->y + snap.title_h;
                    int ch2 = w->h - snap.title_h;
                    VBO_PUSH(w->x,         cy,        cr, cg, cb);
                    VBO_PUSH(w->x + w->w,  cy,        cr, cg, cb);
                    VBO_PUSH(w->x + w->w,  cy + ch2,  cr, cg, cb);
                    VBO_PUSH(w->x,         cy + ch2,  cr, cg, cb);
                }
                /* 任务栏背景 */
                int tbar_h = snap.taskbar_h;
                int ty = sh - tbar_h;
                float tbr = 0.149f, tbg = 0.196f, tbb = 0.220f;
                VBO_PUSH(0,  ty,  tbr, tbg, tbb);
                VBO_PUSH(sw, ty, tbr, tbg, tbb);
                VBO_PUSH(sw, sh, tbr, tbg, tbb);
                VBO_PUSH(0,  sh,  tbr, tbg, tbb);
                /* 开始按钮: 正方形 (与 render_scene 一致) */
                int start_sz = tbar_h - 6;
                if (start_sz < 20) start_sz = 20;
                float sbr = 0.218f, sbg = 0.278f, sbb = 0.310f;  /* CLR_TASKBAR_BTN */
                VBO_PUSH(4,          ty + 3,          sbr, sbg, sbb);
                VBO_PUSH(4 + start_sz, ty + 3,          sbr, sbg, sbb);
                VBO_PUSH(4 + start_sz, ty + 3 + start_sz, sbr, sbg, sbb);
                VBO_PUSH(4,          ty + 3 + start_sz, sbr, sbg, sbb);
                /* 窗口标题按钮 */
                int tbx = start_sz + 12;
                for (int k = 0; k < cnt; k++) {
                    struct efm_wm_win_info *w = &sorted[k];
                    int bw = 18 * TTF_FONT_W;
                    if (bw > sw - tbx - 10) break;
                    float br, bg, bb;
                    if (w->focused) { br = 0.098f; bg = 0.463f; bb = 0.824f; }
                    else            { br = 0.218f; bg = 0.278f; bb = 0.310f; }
                    VBO_PUSH(tbx,      ty + 2,     br, bg, bb);
                    VBO_PUSH(tbx + bw, ty + 2,     br, bg, bb);
                    VBO_PUSH(tbx + bw, ty + tbar_h - 2, br, bg, bb);
                    VBO_PUSH(tbx,      ty + tbar_h - 2, br, bg, bb);
                    tbx += bw + 4;
                }
                vbo_draw(verts, nv);
                #undef VBO_PUSH
            } else {
                /* 回退路径: glBegin/glEnd 立即模式 (VBO 不可用时) */
                for (int k = 0; k < cnt; k++) {
                    struct efm_wm_win_info *w = &sorted[k];
                    float tr, tg, tb;
                    if (w->focused) { tr = 0.098f; tg = 0.463f; tb = 0.824f; }
                    else            { tr = 0.218f; tg = 0.278f; tb = 0.310f; }
                    p_glBegin(GL_QUADS);
                    p_glColor3f(tr, tg, tb);
                    p_glVertex2f((float)w->x * scale,         (float)w->y * scale);
                    p_glVertex2f((float)(w->x + w->w) * scale, (float)w->y * scale);
                    p_glVertex2f((float)(w->x + w->w) * scale, (float)(w->y + snap.title_h) * scale);
                    p_glVertex2f((float)w->x * scale,         (float)(w->y + snap.title_h) * scale);
                    p_glEnd();

                    float cr = ((w->c_bg >> 16) & 0xFF) / 255.0f;
                    float cg = ((w->c_bg >> 8)  & 0xFF) / 255.0f;
                    float cb = ( w->c_bg        & 0xFF) / 255.0f;
                    int cy = w->y + snap.title_h;
                    int ch2 = w->h - snap.title_h;
                    p_glBegin(GL_QUADS);
                    p_glColor3f(cr, cg, cb);
                    p_glVertex2f((float)w->x * scale,      (float)cy * scale);
                    p_glVertex2f((float)(w->x + w->w) * scale, (float)cy * scale);
                    p_glVertex2f((float)(w->x + w->w) * scale, (float)(cy + ch2) * scale);
                    p_glVertex2f((float)w->x * scale,      (float)(cy + ch2) * scale);
                    p_glEnd();
                }
                /* 任务栏背景 + 开始按钮 + 窗口按钮 */
                int tbar_h = snap.taskbar_h;
                int ty = sh - tbar_h;
                p_glBegin(GL_QUADS);
                p_glColor3f(0.149f, 0.196f, 0.220f);
                p_glVertex2f(0.0f,       (float)ty * scale);
                p_glVertex2f((float)sw * scale, (float)ty * scale);
                p_glVertex2f((float)sw * scale, (float)sh * scale);
                p_glVertex2f(0.0f,       (float)sh * scale);
                p_glEnd();
                /* 开始按钮: 正方形 */
                int start_sz = tbar_h - 6;
                if (start_sz < 20) start_sz = 20;
                p_glBegin(GL_QUADS);
                p_glColor3f(0.218f, 0.278f, 0.310f);
                p_glVertex2f((float)4 * scale,              (float)(ty + 3) * scale);
                p_glVertex2f((float)(4 + start_sz) * scale,  (float)(ty + 3) * scale);
                p_glVertex2f((float)(4 + start_sz) * scale,  (float)(ty + 3 + start_sz) * scale);
                p_glVertex2f((float)4 * scale,              (float)(ty + 3 + start_sz) * scale);
                p_glEnd();
                /* 窗口标题按钮 */
                int tbx = start_sz + 12;
                for (int k = 0; k < cnt; k++) {
                    struct efm_wm_win_info *w = &sorted[k];
                    int bw = 18 * TTF_FONT_W;
                    if (bw > sw - tbx - 10) break;
                    float br, bg, bb;
                    if (w->focused) { br = 0.098f; bg = 0.463f; bb = 0.824f; }
                    else            { br = 0.218f; bg = 0.278f; bb = 0.310f; }
                    p_glBegin(GL_QUADS);
                    p_glColor3f(br, bg, bb);
                    p_glVertex2f((float)tbx * scale,      (float)(ty + 2) * scale);
                    p_glVertex2f((float)(tbx + bw) * scale, (float)(ty + 2) * scale);
                    p_glVertex2f((float)(tbx + bw) * scale, (float)(ty + tbar_h - 2) * scale);
                    p_glVertex2f((float)tbx * scale,      (float)(ty + tbar_h - 2) * scale);
                    p_glEnd();
                    tbx += bw + 4;
                }
            }

            } /* end if (!mouse_only) — 鼠标单独更新跳过 GL 渲染 */

            render_tsc_sum += rdtsc_now() - tr0;

            /* FBO blit: 把 FBO 内容放大到 back buffer (全场景 + 鼠标更新都需要) */
            if (g_fbo_ready) {
                unsigned long long tb0 = rdtsc_now();
                fbo_unbind_and_blit(sw, sh);
                /* FBO blit 后恢复屏幕坐标投影 (光标用全分辨率渲染) */
                p_glMatrixMode(GL_PROJECTION);
                p_glLoadIdentity();
                p_glOrtho(0.0f, (GLfloat)sw, (GLfloat)sh, 0.0f, -1.0f, 1.0f);
                p_glMatrixMode(GL_MODELVIEW);
                p_glLoadIdentity();
                render_tsc_sum += rdtsc_now() - tb0;
            }

            /* 鼠标光标: 在全分辨率 back buffer 上渲染 (FBO blit 之后),
             * 确保光标始终锐利, 不受 FBO 放大模糊影响.
             * alpha 纹理仅渲染指针本体, 无矩形背景. */
            if (snap.mouse_visible) {
                cursor_render(snap.mouse_x, snap.mouse_y, 1.0f);
            }

            /* 5. 交换缓冲: Mesa swrast → GBM BO → back buffer → flush */
            unsigned long long ts0 = rdtsc_now();
            p_eglSwapBuffers(g_egl_dpy, g_egl_surf);
            swap_tsc_sum += rdtsc_now() - ts0;

            /* 更新上一帧状态 */
            g_scene_dirty = 0;
            g_mouse_dirty = 0;
            g_last_window_count = snap.window_count;
            g_last_focused_wid = cur_focused;
            g_last_mx = snap.mouse_x;
            g_last_my = snap.mouse_y;
            g_last_mouse_visible = snap.mouse_visible;
            rendered_count++;
        }

        /* [性能] 自适应帧率: 测量本帧耗时 (含 GL 渲染 + eglSwapBuffers),
         * 只 sleep 剩余时间到 16ms 目标. llvmpipe 渲染可能耗时 20-40ms,
         * 旧方案额外 sleep 16ms 导致帧率仅 15-25 FPS; 自适应后可达 30-60 FPS. */
        {
            unsigned long long gl_frame_end = rdtsc_now();
            int elapsed_ms = (int)((gl_frame_end - gl_frame_start) / g_tsc_per_ms);
            int rem = 16 - elapsed_ms;
            if (rem < 1) rem = 1;
            if (rem > 33) rem = 33;
            API->sleep_ms(rem);
        }
        API->yield();

        /* ===== 计时桩: 每秒输出一次帧率 + 渲染/swap 耗时 ===== */
        frame_count++;
        unsigned long long t1 = rdtsc_now();
        if (t1 - last_report_tsc >= g_tsc_per_ms * 1000) {
            char buf[160];
            int pos = 0;
            pos += fmt_str(buf + pos, "compositor: ");
            pos += fmt_int(buf + pos, frame_count);
            pos += fmt_str(buf + pos, " fps (rendered ");
            pos += fmt_int(buf + pos, rendered_count);
            pos += fmt_str(buf + pos, "), render ");
            pos += fmt_int(buf + pos, (int)(render_tsc_sum / g_tsc_per_ms));
            pos += fmt_str(buf + pos, "ms, swap ");
            pos += fmt_int(buf + pos, (int)(swap_tsc_sum / g_tsc_per_ms));
            pos += fmt_str(buf + pos, "ms\n");
            buf[pos] = 0;
            API->print(buf);
            last_report_tsc = t1;
            render_tsc_sum = 0;
            swap_tsc_sum = 0;
            frame_count = 0;
            rendered_count = 0;
        }
    }
}

/* ========== 合成器后端函数 (供 efmAether 通过 compositor_backend 调用) ==========
 * 这两个函数将 efmcompositor 的 canvas 基础设施暴露给 efmAether:
 *   compositor_blit_canvas_full  — 全屏 canvas → back buffer (64-bit 展开)
 *   compositor_finalize_frame    — 鼠标 + mark_dirty + flush + 恢复鼠标区域
 * efmAether 在 render_scene + TTF 文字叠加后调用这两个函数完成帧呈现.
 * 函数访问 efmcompositor 的全局变量 (g_canvas/g_cw/g_ch/g_pitch/g_mouse_save),
 * 在 flat-memory 模型下 efmAether 可直接调用 (共享地址空间). */

static void compositor_blit_canvas_full(void) {
    if (!g_canvas) return;
    void *back_ptr = 0;
    int back_pitch = 0, back_w = 0, back_h = 0;
    if (API->get_backbuffer(&back_ptr, &back_pitch, &back_w, &back_h) != 0 || !back_ptr)
        return;
    unsigned int *dst = (unsigned int*)back_ptr;
    int dpitch = back_pitch / 4;
    int rows = (g_ch < back_h) ? g_ch : back_h;
    int cols = (g_cw < back_w) ? g_cw : back_w;
    for (int y = 0; y < rows; y++) {
        unsigned int *s = g_canvas + y * g_pitch;
        unsigned int *d = dst + y * dpitch;
        int pairs = cols >> 1;
        unsigned long long *s8 = (unsigned long long*)s;
        unsigned long long *d8 = (unsigned long long*)d;
        while (pairs--) *d8++ = *s8++;
        if (cols & 1) d[cols - 1] = s[cols - 1];
    }
}

/* finalize_frame: 在 efmAether 完成 TTF 文字叠加后调用.
 * 流程: 保存鼠标区域 → 画鼠标 → mark_dirty → flush → 恢复鼠标区域
 * 让 back buffer 始终保持干净 (不含鼠标像素), 避免下一帧 render_scene
 * 从 back buffer 复制到 canvas 时产生鼠标拖尾. */
static void compositor_finalize_frame(int mx, int my, int mouse_visible,
                                      int x1, int y1, int x2, int y2) {
    void *back_ptr = 0;
    int back_pitch = 0, back_w = 0, back_h = 0;
    if (API->get_backbuffer(&back_ptr, &back_pitch, &back_w, &back_h) != 0 || !back_ptr) {
        /* 回退: blit_to_window */
        if (API->blit_to_window && g_canvas)
            API->blit_to_window(g_canvas, g_cw, g_ch, g_pitch * 4);
        if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
        if (x2 >= g_cw) x2 = g_cw - 1; if (y2 >= g_ch) y2 = g_ch - 1;
        API->mark_dirty_rect(x1, y1, x2, y2);
        API->flush_now();
        return;
    }
    unsigned int *dst = (unsigned int*)back_ptr;
    int dpitch = back_pitch / 4;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= back_w) x2 = back_w - 1;
    if (y2 >= back_h) y2 = back_h - 1;

    if (mouse_visible) {
        int mx2 = mx + MOUSE_CURSOR_W - 1, my2 = my + MOUSE_CURSOR_H - 1;
        if (mx < x1) x1 = mx;
        if (my < y1) y1 = my;
        if (mx2 > x2) x2 = mx2;
        if (my2 > y2) y2 = my2;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= back_w) x2 = back_w - 1;
        if (y2 >= back_h) y2 = back_h - 1;
        /* 保存鼠标区域原始像素 */
        for (int y = 0; y < MOUSE_CURSOR_H; y++) {
            for (int x = 0; x < MOUSE_CURSOR_W; x++) {
                int bx = mx + x, by = my + y;
                if (bx >= 0 && by >= 0 && bx < back_w && by < back_h)
                    g_mouse_save[y * MOUSE_CURSOR_W + x] = dst[by * dpitch + bx];
                else
                    g_mouse_save[y * MOUSE_CURSOR_W + x] = 0;
            }
        }
        g_mouse_saved = 1;
        g_mouse_save_x = mx;
        g_mouse_save_y = my;
        draw_mouse_to_buf(dst, dpitch, mx, my, back_w, back_h);
    }

    API->mark_dirty_rect(x1, y1, x2, y2);
    API->flush_now();

    /* flush 后恢复鼠标区域, 让 back buffer 保持干净 */
    if (g_mouse_saved) {
        int smx = g_mouse_save_x, smy = g_mouse_save_y;
        for (int y = 0; y < MOUSE_CURSOR_H; y++) {
            for (int x = 0; x < MOUSE_CURSOR_W; x++) {
                int bx = smx + x, by = smy + y;
                if (bx >= 0 && by >= 0 && bx < back_w && by < back_h)
                    dst[by * dpitch + bx] = g_mouse_save[y * MOUSE_CURSOR_W + x];
            }
        }
        g_mouse_saved = 0;
    }
}

void compositor_main(void) {
    /* [DEBUG] 直接写 COM1 串口, 不依赖 API, 确认 compositor_main 被进入 */
    cm_serial("CM:enter\n");
    if (API->magic != API_MAGIC) {
        cm_serial("CM:bad_magic\n");
        return;
    }
    cm_serial("CM:magic_ok\n");

    /* 【前置运行时检查】 所有用到的 API 必须非空, 缺失立即退出,
     *  保持内核渲染路径不被破坏, 避免"合成器退出但 g_compositor_active 仍 1"的卡死态. */
    if (!API->get_fb_info || !API->malloc || !API->free ||
        !API->set_compositor_active || !API->get_wm_snapshot ||
        !API->blit_to_window || !API->sleep_ms || !API->yield || !API->print)
    {
        cm_serial("CM:api_null\n");
        return;
    }

    cm_serial("CM:api_ok\n");
    API->print("compositor: entered compositor_main OK\n");

    /* 启动小延迟: 让 shell_loop 先运行一轮处理就绪事件, 防止"合成器立刻抢 CPU"
     * 导致 shell_loop 刚进入就被换出, 鼠标首帧没被绘制. */
    API->sleep_ms(50);
    cm_serial("CM:slept\n");
    API->print("compositor: sleep_ms returned, trying GL path\n");

    /* [GL 路径暂用 Canvas 替代]
     * GL 渲染路径 (gl_render_loop) 仅用 OpenGL 绘制纯色窗口框架 (标题栏/内容区/任务栏),
     * 缺少 2 个关键能力, 会导致登录界面"只剩一堆矩形不见文字和按钮":
     *   1. 不渲染 EFS 程序 tbuf_ptr 文本终端内容 (无 draw_char)
     *   2. 不恢复 EFS 程序 (userman/efmlogin) 通过 fill_rect/draw_rect 写入 back buffer 的图形按钮
     * 因此优先使用 Canvas 软件光栅化路径 (render_scene/render_single_window), 它已包含
     * 完整的终端文本渲染 + EFS 图形恢复逻辑, 能正确显示登录/用户管理界面。
     * GL 路径保留为未来优化项 (当 Compositor 原生支持 EFS 窗口内的 GL 渲染时再启用)。 */
    cm_serial("CM:use_sw_path_direct\n");
    API->print("compositor: using canvas software rasterizer path (text + EFS graphics enabled)\n");

    /* 获取屏幕尺寸 */
    unsigned int hr = 0, vr = 0, ppsl = 0;
    unsigned int *fb_base = 0;
    API->get_fb_info(&hr, &vr, &ppsl, &fb_base);
    if (!hr || !vr) { cm_serial("CM:bad_fb\n"); API->print("compositor: invalid fb info\n"); return; }

    g_cw = (int)hr;
    g_ch = (int)vr;
    g_pitch = g_cw;
    cm_serial("CM:fb_ok\n");

    /* 【画布大小保护】 高于 1920x1200 不启动合成 (可能显存不足).
     * 实际上 bootloader 已限制 <= 1920x1200, 这里再做一层保险. */
    if (g_cw > 1920 || g_ch > 1200) {
        cm_serial("CM:res_hi\n");
        API->print("compositor: resolution too high, skip\n");
        return;
    }

    /* 分配像素缓冲 (= Mesa GBM BO mmap 区域) */
    unsigned long buf_sz = (unsigned long)g_cw * (unsigned long)g_ch * 4;
    g_canvas = (unsigned int*)API->malloc(buf_sz);
    if (!g_canvas) {
        cm_serial("CM:no_mem\n");
        API->print("compositor: FATAL - cannot allocate canvas (heap too small)\n");
        return;
    }
    cm_serial("CM:canvas_ok\n");
    /* 用 0 清画布, 防止分配后写未初始化内存造成"屏幕彩噪/乱码" */
    {
        unsigned int *p = g_canvas;
        unsigned long cnt = (unsigned long)g_cw * (unsigned long)g_ch;
        for (unsigned long i = 0; i < cnt; i++) p[i] = 0xFF000000u; /* 黑色 + alpha 全 1 */
    }

    /* 通知内核: 合成器接管渲染 */
    API->set_compositor_active(1);
    cm_serial("CM:active\n");

    /* [职责分工 — 0x6000 = aether_render_info]
     * 新架构:
     *   - 0x6000 = aether_render_info (efmAether 写, efmcompositor 读)
     *   - efmcompositor 始终是唯一的渲染执行者, 不再睡眠等待 claimed
     *   - compositor_backend (旧接口) 已不再使用, 避免与 aether_render_info 地址冲突
     * efmAether 启动时:
     *   1. 打开 efmcompositor 后不要认领 compositor_backend
     *   2. 而是把 0x6000 当 aether_render_info* 写入颜色方案 + 文字 + seq++
     *   3. efmcompositor 每帧检测 magic==AETHER_INFO_MAGIC 并读取生效 */
    {
        /* 0x6000 = aether_render_info (efmAether 写, efmcompositor 读)
         * [关键修复] 仅当 efmAether 尚未写入 (magic != AETHER_INFO_MAGIC) 时
         * 才初始化默认值. 之前无条件清零 0x6000 会覆盖 efmAether 已写入的
         * menu_count/menu_items, 导致开始菜单永远 mc=0. */
        struct aether_render_info *ai =
            (struct aether_render_info *)(unsigned long)AETHER_INFO_ADDR;
        if (ai->magic != AETHER_INFO_MAGIC) {
            /* efmAether 未启动, 写入默认值保证桌面仍能显示 */
            volatile unsigned char *p = (volatile unsigned char*)ai;
            for (unsigned int i = 0; i < sizeof(struct aether_render_info); i++) p[i] = 0;
            ai->font_w = TTF_FONT_W;
            ai->font_h = TTF_FONT_H;
            ai->clr_bg = 0x00102040;
            ai->clr_desktop_text = 0x00FFFFFF;
            ai->clr_taskbar_bg = 0x00202030;
            ai->clr_taskbar_btn = 0x00404060;
            ai->clr_taskbar_act = 0x006080A0;
            ai->desktop_title.x = 10;
            ai->desktop_title.y = 10;
            ai->desktop_title.color = 0x00FFFFFF;
            {
                const char *s = "EFMOS Desktop";
                char *d = ai->desktop_title.text;
                for (int i = 0; i < 95 && s[i]; i++) d[i] = s[i];
                d[95] = 0;
            }
            ai->extra_count = 0;
            ai->seq = 1;
            ai->magic = AETHER_INFO_MAGIC;
            cm_serial("CM:ai_ready(default)\n");
        } else {
            /* efmAether 已写入, 保留其数据不动 */
            cm_serial("CM:ai_ready(aether)\n");
        }
        API->print("compositor: aether_render_info @0x6000 ready\n");
    }

    /* WM 快照缓冲 (在栈上, ~4KB) */
    struct efm_wm_snapshot snap;
    /* 错误计数器: blit 连续失败超过 30 次则主动退出并交还渲染给内核 */
    int fail_cnt = 0;
    /* 空闲帧计数: 用于周期性强制刷新 (防止脏检测遗漏, 如 tbuf 文本变化) */
    int idle_frames = 0;
    /* [DEBUG] 前 5 帧输出串口日志, 确认渲染循环正常 */
    int dbg_frames = 0;

    /* 进入软件路径: 复位脏检测状态, 强制首帧全屏渲染. GL 路径可能已修改
     * g_last_* (窗口数/焦点/鼠标), 若不复位, 首帧 structural=0 会跳过渲染,
     * 导致黑屏直到下次状态变化. canvas 刚分配为全黑, 必须立刻 full blit. */
    g_scene_dirty = 1;
    g_mouse_dirty = 1;
    g_last_window_count = -1;
    g_last_focused_wid = -1;
    g_last_menu_open = 0;
    g_last_menu_hover = -1;
    g_last_mx = -1; g_last_my = -1; g_last_mouse_visible = 0;

    /* [性能] TSC 校准: 测量实际 TSC 频率, 用于自适应帧率控制.
     * sleep_ms(20) 前后读取 TSC, 计算每毫秒 TSC 周期数.
     * 校准失败则回退到默认 2GHz 估算值. */
    unsigned long long g_tsc_per_ms = TSC_PER_MS;
    {
        unsigned long long tc0 = rdtsc_now();
        API->sleep_ms(20);
        unsigned long long tc1 = rdtsc_now();
        unsigned long long measured = (tc1 - tc0) / 20;
        if (measured >= 100000 && measured <= 10000000) g_tsc_per_ms = measured;
    }

    /* [性能] tbuf 哈希缓存: 每帧只计算一次, 脏检测和状态保存复用, 避免重复计算 */
    unsigned int cur_hash[32];

    /* 主循环: 脏矩形 + 局部重绘优化
     * [性能优化] 四级渲染策略 (从快到慢):
     *   1. 无变化: 跳过渲染和 blit, CPU 几乎零开销 (33ms 轮询)
     *   2. 仅鼠标移动: 只 blit 鼠标新旧位置 (~11x14 两个小区域, ~1KB)
     *   3. 单窗口内容变化 (终端输出): 只重绘 + blit 该窗口区域 (~1MB)
     *   4. 结构变化 (窗口数/焦点/位置/大小/标题): 全屏重画 + 全屏 blit (~16MB)
     *
     * 关键: canvas 始终保持"无鼠标"的干净场景快照. 鼠标由 present 阶段直接画到
     * back buffer (不画到 canvas), 这样鼠标移动时拷贝 canvas 即可擦旧鼠标, 无拖尾. */
    /* [性能] idle  streak: 连续 idle 帧计数, 用于自适应轮询间隔 */
    static int g_idle_streak_compositor = 0;

    while (1) {
        unsigned long long frame_start = rdtsc_now();
        int n = API->get_wm_snapshot(&snap, (int)sizeof(snap));
        if (n < 0) {
            API->sleep_ms(16);
            API->yield();
            continue;
        }

        /* ===== 脏检测 ===== */
        int cur_focused = -1;
        for (int i = 0; i < snap.window_count; i++) {
            if (snap.windows[i].focused) { cur_focused = snap.windows[i].wid; break; }
        }

        /* 鼠标变化 */
        if (snap.mouse_x != g_last_mx || snap.mouse_y != g_last_my ||
            snap.mouse_visible != g_last_mouse_visible) {
            g_mouse_dirty = 1;
        }

        /* 结构变化: 窗口数/焦点/标题/位置/大小/开始菜单状态 → 全屏重画 */
        int structural = (snap.window_count != g_last_window_count) ||
                         (cur_focused != g_last_focused_wid) || g_scene_dirty;
        /* 开始菜单状态变化 (打开/关闭/悬停项) → 强制全屏重画 */
        if (snap.menu_open != g_last_menu_open ||
            snap.menu_hover_idx != g_last_menu_hover) {
            structural = 1;
        }

        int gfx_dirty = (snap.gfx_dirty_seq != g_last_gfx_dirty_seq);

        /* [性能] idle 快速路径:
         *   每 8 帧做一次 O(n) tbuf_hash 全量检测 (7/8 帧跳过 FNV-1a),
         *   其余帧只做 O(1) 光标/悬停轻量检测, 哈希沿用上次值.
         *   结构变化/gfx_dirty 立即强制全量哈希. */
        int content_idx = -1;
        int n_content = 0;
        int loop_n = snap.window_count;
        if (loop_n > 32) loop_n = 32;
        idle_frames++;
        int force_full_hash = (structural || gfx_dirty || (idle_frames & 7) == 0);

        for (int i = 0; i < loop_n; i++) {
            struct efm_wm_win_info *w = &snap.windows[i];
            if (w->x != g_last_win_x[i] || w->y != g_last_win_y[i] ||
                w->w != g_last_win_w[i] || w->h != g_last_win_h[i] ||
                w->wid != g_last_win_wid[i]) {
                structural = 1;
            }
            {
                int tdiff = 0;
                for (int t = 0; t < EFM_WM_TITLE_LEN; t++) {
                    if (w->title[t] != g_last_win_title[i][t]) { tdiff = 1; break; }
                    if (w->title[t] == 0) break;
                }
                if (tdiff) structural = 1;
            }
            /* [新架构 · WID 跟踪修复] gfx_info_ptr->seq 变化 → GUI 内容更新 (按钮/文字变化)
             * 改为按 w->wid 索引 g_last_gfx_info_seq_by_wid[], 避免窗口增删/排序时
             * 数组索引错位导致旧 seq 与新 seq 错配 → 漏渲染第一帧。 */
            if (w->gfx_info_ptr && w->gfx_info_ptr->magic == EFM_GFX_MAGIC) {
                unsigned int curseq = w->gfx_info_ptr->seq;
                unsigned int oldseq = 0;
                int wid_safe = (w->wid > 0 && w->wid < LAST_SEQ_MAX_WID) ? w->wid : 0;
                if (wid_safe) oldseq = g_last_gfx_info_seq_by_wid[wid_safe];
                if (curseq != oldseq) {
                    /* 标记为内容变化, 并作为图形脏更新 */
                    content_idx = i;
                    n_content++;
                    gfx_dirty = 1;
                    /* [调试] 每次 GUI 内容更新打印一次 (不受 dbg_frames<10 限制),
                     * 便于诊断桌面启动后才打开的窗口 (setting/userman) 的渲染协议
                     * 是否真正到达 compositor: 无此行 = EFS 侧未提交; rc=0 = 数据空. */
                    {
                        char _db[128]; int _dp = 0;
                        _dp += fmt_str(_db+_dp, "CM:gfxupd wid=");
                        _dp += fmt_int(_db+_dp, w->wid);
                        _dp += fmt_str(_db+_dp, " rc=");
                        _dp += fmt_int(_db+_dp, w->gfx_info_ptr->rect_count);
                        _dp += fmt_str(_db+_dp, " tc=");
                        _dp += fmt_int(_db+_dp, w->gfx_info_ptr->text_count);
                        _dp += fmt_str(_db+_dp, " sq=");
                        _dp += fmt_int(_db+_dp, (int)curseq);
                        _dp += fmt_str(_db+_dp, "\n");
                        _db[_dp] = 0;
                        cm_serial(_db);
                    }
                }
            }
            int cursor_or_hover = (w->c_cx != g_last_win_cx[i] || w->c_cy != g_last_win_cy[i] ||
                                  w->close_hover != g_last_win_hover[i]);
            if (structural) {
                cur_hash[i] = 0;
            } else if (force_full_hash) {
                unsigned int h = tbuf_hash(w->tbuf_ptr, w->tbuf_cols, w->tbuf_rows);
                cur_hash[i] = h;
                if (h != g_last_win_hash[i] || cursor_or_hover) {
                    content_idx = i;
                    n_content++;
                }
            } else {
                cur_hash[i] = g_last_win_hash[i];
                if (cursor_or_hover) {
                    content_idx = i;
                    n_content++;
                }
            }
        }

        /* [性能] idle 快速路径: 完全无变化 + 非强制帧 → 跳过渲染
         * [60fps] 固定 16ms 轮询, 确保用户交互时响应延迟 ≤16ms */
        int fully_idle = !structural && !gfx_dirty && !g_mouse_dirty && n_content == 0;
        if (fully_idle && !force_full_hash) {
            g_last_mx = snap.mouse_x;
            g_last_my = snap.mouse_y;
            g_last_mouse_visible = snap.mouse_visible;
            g_last_gfx_dirty_seq = snap.gfx_dirty_seq;
            /* [新架构 · WID 跟踪] idle 路径也要同步每窗口 gfx_info_seq.
             * 先全量清零 by_wid 再按当前 snap 回填, 确保已关闭窗口的旧 seq 不残留. */
            {
                for (int _k = 0; _k < LAST_SEQ_MAX_WID; _k++) g_last_gfx_info_seq_by_wid[_k] = 0;
                for (int i = 0; i < loop_n; i++) {
                    struct efm_wm_win_info *w = &snap.windows[i];
                    int wid_safe = (w->wid > 0 && w->wid < LAST_SEQ_MAX_WID) ? w->wid : 0;
                    unsigned int seqv = 0;
                    if (w->gfx_info_ptr && w->gfx_info_ptr->magic == EFM_GFX_MAGIC)
                        seqv = w->gfx_info_ptr->seq;
                    if (wid_safe) g_last_gfx_info_seq_by_wid[wid_safe] = seqv;
                    g_last_gfx_info_seq[i] = seqv;
                }
            }
            g_idle_streak_compositor++;
            /* 60fps 轮询: 16ms 目标帧间隔 */
            unsigned long long frame_end = rdtsc_now();
            int elapsed_ms = (int)((frame_end - frame_start) / g_tsc_per_ms);
            int rem = 16 - elapsed_ms;
            if (rem > 0 && rem <= 50) API->sleep_ms(rem);
            API->yield();
            continue;
        }
        g_idle_streak_compositor = 0; /* 有变化立即重置 streak */

        /* 兜底: 每 600 帧强制全屏刷新 (~20s), 防 tbuf 漏检.
         * (8 帧一次全量哈希已足够及时检测内容变化) */
        if (idle_frames >= 600) {
            structural = 1;
            idle_frames = 0;
        }

        /* 重置脏矩形 */
        g_has_dirty_rect = 0;

        /* ===== 渲染调度 ===== */
        int need_full_blit = 0;   /* 全屏 blit (结构变化) */
        int need_rect_blit = 0;   /* 局部 blit (鼠标/单窗口内容变化) */

        /* gfx_dirty 已在脏检测阶段声明 (line 2483). 不再重复声明. */

        if (structural) {
            /* 全屏重画 canvas (不含鼠标) */
            if (dbg_frames < 5) cm_serial("CM:render_scene\n");
            render_scene(&snap);
            need_full_blit = 1;
            g_scene_dirty = 0;
            g_mouse_dirty = 0;
        } else {
            /* 局部: 单窗口内容变化 → 只重画该窗口到 canvas */
            g_cur_mx = snap.mouse_x;
            g_cur_my = snap.mouse_y;
            g_cur_mouse_visible = snap.mouse_visible;
            if (gfx_dirty) {
                /* EFS 图形变了: 对所有活动窗口重绘内容区,
                 * 并把每个窗口矩形合并到脏矩形中, 确保 blit 覆盖所有变更区域。
                 * [修复] 必须按 z_order 排序后渲染 (底→顶), 否则焦点窗口可能被
                 * 低 z 值窗口覆盖, 导致内容错位/残影。与 render_scene 一致。 */
                int cnt = snap.window_count;
                if (cnt > EFM_WM_MAX_WIN) cnt = EFM_WM_MAX_WIN;
                struct efm_wm_win_info gfx_sorted[EFM_WM_MAX_WIN];
                for (int i = 0; i < cnt; i++) gfx_sorted[i] = snap.windows[i];
                sort_windows(gfx_sorted, cnt);
                for (int i = 0; i < cnt; i++) {
                    struct efm_wm_win_info *w = &gfx_sorted[i];
                    render_single_window(w, snap.title_h);
                    dirty_union(w->x, w->y, w->x + w->w - 1, w->y + w->h - 1);
                }
                /* 不清除 g_mouse_dirty: 鼠标可能在桌面区域 (非窗口), 需要下方
                 * 鼠标处理逻辑把旧鼠标位置合并到脏矩形, 否则桌面上的旧鼠标残留 → 拖尾 */
            } else if (n_content == 1) {
                /* [修复] 单窗口内容变化时, 也要重绘所有 z_order 更高的窗口,
                 * 否则变更窗口的内容会覆盖上层窗口 (重叠区域残影/错位)。 */
                struct efm_wm_win_info *w = &snap.windows[content_idx];
                int changed_z = w->z_order;
                render_single_window(w, snap.title_h);
                dirty_union(w->x, w->y, w->x + w->w - 1, w->y + w->h - 1);
                /* 重绘所有 z_order 更高 (在上层) 的窗口, 恢复其被覆盖的像素 */
                {
                    int cnt2 = snap.window_count;
                    if (cnt2 > EFM_WM_MAX_WIN) cnt2 = EFM_WM_MAX_WIN;
                    struct efm_wm_win_info above_sorted[EFM_WM_MAX_WIN];
                    int na = 0;
                    for (int i = 0; i < cnt2; i++) {
                        if (i == content_idx) continue;
                        if (snap.windows[i].z_order > changed_z &&
                            aabb_overlap(w->x, w->y, w->x + w->w - 1, w->y + w->h - 1,
                                         snap.windows[i].x, snap.windows[i].y,
                                         snap.windows[i].x + snap.windows[i].w - 1,
                                         snap.windows[i].y + snap.windows[i].h - 1))
                            above_sorted[na++] = snap.windows[i];
                    }
                    sort_windows(above_sorted, na);
                    for (int i = 0; i < na; i++) {
                        render_single_window(&above_sorted[i], snap.title_h);
                        dirty_union(above_sorted[i].x, above_sorted[i].y,
                                    above_sorted[i].x + above_sorted[i].w - 1,
                                    above_sorted[i].y + above_sorted[i].h - 1);
                    }
                }
            } else if (n_content >= 2) {
                /* 多窗口同时变化 → 全屏重画 (罕见, 如批量刷新) */
                render_scene(&snap);
                need_full_blit = 1;
                g_mouse_dirty = 0;
            }
            /* 鼠标变化 (可与单窗口内容变化叠加进同一脏矩形) */
            if (!need_full_blit && g_mouse_dirty) {
                if (g_last_mouse_visible && g_last_mx >= 0 && g_last_my >= 0) {
                    int ox1 = g_last_mx, oy1 = g_last_my;
                    int ox2 = g_last_mx + MOUSE_CURSOR_W - 1;
                    int oy2 = g_last_my + MOUSE_CURSOR_H - 1;
                    if (ox1 < 0) ox1 = 0; if (oy1 < 0) oy1 = 0;
                    dirty_union(ox1, oy1, ox2, oy2);
                }
                if (snap.mouse_visible) {
                    int nx1 = snap.mouse_x, ny1 = snap.mouse_y;
                    int nx2 = snap.mouse_x + MOUSE_CURSOR_W - 1;
                    int ny2 = snap.mouse_y + MOUSE_CURSOR_H - 1;
                    if (nx1 < 0) nx1 = 0; if (ny1 < 0) ny1 = 0;
                    dirty_union(nx1, ny1, nx2, ny2);
                }
                g_mouse_dirty = 0;
            }
            if (!need_full_blit && g_has_dirty_rect) need_rect_blit = 1;
        }

        /* ===== 呈现到屏幕: 色块 canvas→back → TTF 文字叠加 → 鼠标 → flush ===== */
        if (need_full_blit || need_rect_blit) {
            int dx1, dy1, dx2, dy2;
            if (need_full_blit) {
                dx1 = 0; dy1 = 0; dx2 = g_cw - 1; dy2 = g_ch - 1;
            } else {
                dx1 = g_dirty_x1; dy1 = g_dirty_y1;
                dx2 = g_dirty_x2; dy2 = g_dirty_y2;
            }
            if (dx1 < 0) dx1 = 0; if (dy1 < 0) dy1 = 0;
            if (dx2 >= g_cw) dx2 = g_cw - 1; if (dy2 >= g_ch) dy2 = g_ch - 1;

            void *back_ptr = 0;
            int back_pitch = 0, back_w = 0, back_h = 0;
            if (dx1 <= dx2 && dy1 <= dy2 &&
                API->get_backbuffer(&back_ptr, &back_pitch, &back_w, &back_h) == 0 && back_ptr) {
                int rw = dx2 - dx1 + 1;
                int rh = dy2 - dy1 + 1;
                unsigned int *src_base = g_canvas;
                unsigned int *dst_base = (unsigned int*)back_ptr;
                int dst_pitch_px = back_pitch / 4;
                /* Step 1: 逐行拷贝 canvas (色块) → back buffer (64-bit 展开) */
                for (int y = 0; y < rh; y++) {
                    unsigned int *s = src_base + (dy1 + y) * g_pitch + dx1;
                    unsigned int *d = dst_base + (dy1 + y) * dst_pitch_px + dx1;
                    int pairs = rw >> 1;
                    unsigned long long *s8 = (unsigned long long*)s;
                    unsigned long long *d8 = (unsigned long long*)d;
                    while (pairs--) *d8++ = *s8++;
                    if (rw & 1) d[rw - 1] = s[rw - 1];
                }

                /* Step 2: TTF 文字叠加 (透明背景 alpha 混合, 不覆盖下层色块/EFS图形).
                 * [性能]
                 *   need_full_blit: 全屏无裁剪叠加 (所有文字)
                 *   need_rect_blit: 脏矩形裁剪叠加 (只画覆盖到的窗口/区域) */
                {
                    struct aether_render_info *ai =
                        (struct aether_render_info *)(unsigned long)AETHER_INFO_ADDR;
                    if (need_full_blit) {
                        render_ttf_text_overlay(&snap, dst_base, dst_pitch_px,
                                                back_w, back_h, ai);
                    } else {
                        render_ttf_text_overlay_clip(&snap, dst_base, dst_pitch_px,
                                                     back_w, back_h, ai,
                                                     dx1, dy1, dx2, dy2);
                    }
                }

                /* Step 3: 鼠标 - 画前保存原始像素, flush 后恢复 (根治拖尾) */
                if (snap.mouse_visible) {
                    int mx = snap.mouse_x, my = snap.mouse_y;
                    int mx2 = mx + MOUSE_CURSOR_W - 1, my2 = my + MOUSE_CURSOR_H - 1;
                    if (mx < dx1) dx1 = mx;
                    if (my < dy1) dy1 = my;
                    if (mx2 > dx2) dx2 = mx2;
                    if (my2 > dy2) dy2 = my2;
                    if (dx1 < 0) dx1 = 0; if (dy1 < 0) dy1 = 0;
                    if (dx2 >= back_w) dx2 = back_w - 1;
                    if (dy2 >= back_h) dy2 = back_h - 1;
                    for (int y = 0; y < MOUSE_CURSOR_H; y++) {
                        for (int x = 0; x < MOUSE_CURSOR_W; x++) {
                            int bx = mx + x, by = my + y;
                            if (bx >= 0 && by >= 0 && bx < back_w && by < back_h)
                                g_mouse_save[y * MOUSE_CURSOR_W + x] = dst_base[by * dst_pitch_px + bx];
                            else
                                g_mouse_save[y * MOUSE_CURSOR_W + x] = 0;
                        }
                    }
                    g_mouse_saved = 1;
                    g_mouse_save_x = mx;
                    g_mouse_save_y = my;
                    draw_mouse_to_buf(dst_base, dst_pitch_px, mx, my, back_w, back_h);
                }
                API->mark_dirty_rect(dx1, dy1, dx2, dy2);
                /* [防撕裂] 在 vsync 期间 flush, back→front 拷贝与扫描线不重叠 */
                wait_vsync();
                API->flush_now();
                /* Step 4: flush 后立即恢复鼠标区域, 让 back buffer 保持干净
                 * (不含鼠标像素, 防止下一帧从 back buffer 复制时带入鼠标拖尾) */
                if (g_mouse_saved) {
                    int mx = g_mouse_save_x, my = g_mouse_save_y;
                    for (int y = 0; y < MOUSE_CURSOR_H; y++) {
                        for (int x = 0; x < MOUSE_CURSOR_W; x++) {
                            int bx = mx + x, by = my + y;
                            if (bx >= 0 && by >= 0 && bx < back_w && by < back_h)
                                dst_base[by * dst_pitch_px + bx] = g_mouse_save[y * MOUSE_CURSOR_W + x];
                        }
                    }
                    g_mouse_saved = 0;
                }
                fail_cnt = 0;
                if (dbg_frames < 5) {
                    char _rb[48]; int _rp = 0;
                    _rp += fmt_str(_rb+_rp, need_full_blit ? "CM:fblit " : "CM:rblit ");
                    _rp += fmt_int(_rb+_rp, rw);
                    _rp += fmt_str(_rb+_rp, "x");
                    _rp += fmt_int(_rb+_rp, rh);
                    _rp += fmt_str(_rb+_rp, "\n");
                    _rb[_rp] = 0;
                    cm_serial(_rb);
                }
            } else {
                /* get_backbuffer 失败 (无 Graphics 驱动): canvas → front 直接 blit.
                 * 用 save/under 保持 canvas 干净: 存鼠标区 → 画鼠标 → blit → 恢复. */
                int msx = snap.mouse_x, msy = snap.mouse_y;
                int msw = MOUSE_CURSOR_W + 2, msh = MOUSE_CURSOR_H + 2;
                unsigned int save_region[16 * 16];
                int saved = 0;
                if (snap.mouse_visible && msx < g_cw && msy < g_ch &&
                    msx + msw > 0 && msy + msh > 0) {
                    for (int y = 0; y < msh; y++) {
                        for (int x = 0; x < msw; x++) {
                            int cx = msx + x, cy = msy + y;
                            if (cx >= 0 && cy >= 0 && cx < g_cw && cy < g_ch)
                                save_region[y * 16 + x] = g_canvas[cy * g_pitch + cx];
                        }
                    }
                    draw_mouse(msx, msy);
                    saved = 1;
                }
                int rc = API->blit_to_window(g_canvas, g_cw, g_ch, g_pitch * 4);
                if (saved) {
                    for (int y = 0; y < msh; y++) {
                        for (int x = 0; x < msw; x++) {
                            int cx = msx + x, cy = msy + y;
                            if (cx >= 0 && cy >= 0 && cx < g_cw && cy < g_ch)
                                g_canvas[cy * g_pitch + cx] = save_region[y * 16 + x];
                        }
                    }
                }
                if (rc < 0) fail_cnt++; else fail_cnt = 0;
            }
        }

        if (fail_cnt > 30) {
            API->print("compositor: blit failed too many times, reverting to kernel WM\n");
            API->set_compositor_active(0);
            if (g_canvas && API->free) API->free(g_canvas);
            g_canvas = 0;
            return;
        }

        /* 更新上一帧状态 (含内容跟踪表, 供下帧脏检测) */
        g_last_mx = snap.mouse_x;
        g_last_my = snap.mouse_y;
        g_last_mouse_visible = snap.mouse_visible;
        g_last_gfx_dirty_seq = snap.gfx_dirty_seq;  /* 同步 kernel 端图形脏序列 */
        g_last_window_count = snap.window_count;
        g_last_focused_wid = cur_focused;
        g_last_menu_open = snap.menu_open;
        g_last_menu_hover = snap.menu_hover_idx;
        for (int i = 0; i < loop_n; i++) {
            struct efm_wm_win_info *w = &snap.windows[i];
            g_last_win_x[i] = w->x;
            g_last_win_y[i] = w->y;
            g_last_win_w[i] = w->w;
            g_last_win_h[i] = w->h;
            g_last_win_wid[i] = w->wid;
            g_last_win_cx[i] = w->c_cx;
            g_last_win_cy[i] = w->c_cy;
            g_last_win_hover[i] = w->close_hover;
            g_last_win_hash[i] = cur_hash[i];
            /* mirror 数组 (只读): 单窗口简单按 snap index 填 seq */
            if (w->gfx_info_ptr && w->gfx_info_ptr->magic == EFM_GFX_MAGIC)
                g_last_gfx_info_seq[i] = w->gfx_info_ptr->seq;
            else
                g_last_gfx_info_seq[i] = 0;
            for (int t = 0; t < EFM_WM_TITLE_LEN; t++) {
                g_last_win_title[i][t] = w->title[t];
                if (w->title[t] == 0) break;
            }
        }
        /* [新架构 · WID 跟踪] 每帧一次性同步 by_wid 序列.
         * 先清零再回填: 确保已关闭/不存在的旧 WID 残留 seq 不遗留到下一帧.
         * 放循环外, 每次整帧只做一次清零+回填 (之前误放 for 里是 loop_n^2 开销 + 清零覆盖). */
        {
            for (int _k = 0; _k < LAST_SEQ_MAX_WID; _k++) g_last_gfx_info_seq_by_wid[_k] = 0;
            for (int i = 0; i < loop_n; i++) {
                struct efm_wm_win_info *w2 = &snap.windows[i];
                int wid_safe = (w2->wid > 0 && w2->wid < LAST_SEQ_MAX_WID) ? w2->wid : 0;
                unsigned int seqv = 0;
                if (w2->gfx_info_ptr && w2->gfx_info_ptr->magic == EFM_GFX_MAGIC)
                    seqv = w2->gfx_info_ptr->seq;
                if (wid_safe) g_last_gfx_info_seq_by_wid[wid_safe] = seqv;
            }
        }

        if (dbg_frames < 10) {
            char _db[192]; int _dp = 0;
            _dp += fmt_str(_db+_dp, "CM:f");
            _dp += fmt_int(_db+_dp, dbg_frames);
            _dp += fmt_str(_db+_dp, " nw=");
            _dp += fmt_int(_db+_dp, snap.window_count);
            _dp += fmt_str(_db+_dp, " full=");
            _dp += fmt_int(_db+_dp, need_full_blit);
            _dp += fmt_str(_db+_dp, " rect=");
            _dp += fmt_int(_db+_dp, need_rect_blit);
            _dp += fmt_str(_db+_dp, " nc=");
            _dp += fmt_int(_db+_dp, n_content);
            _dp += fmt_str(_db+_dp, " gfx=");
            _dp += fmt_int(_db+_dp, gfx_dirty);
            _dp += fmt_str(_db+_dp, " seq=");
            _dp += fmt_int(_db+_dp, (int)snap.gfx_dirty_seq);
            _dp += fmt_str(_db+_dp, " mx=");
            _dp += fmt_int(_db+_dp, snap.mouse_x);
            _dp += fmt_str(_db+_dp, " my=");
            _dp += fmt_int(_db+_dp, snap.mouse_y);
            _dp += fmt_str(_db+_dp, " mv=");
            _dp += fmt_int(_db+_dp, snap.mouse_visible);
            _dp += fmt_str(_db+_dp, " mo=");
            _dp += fmt_int(_db+_dp, snap.menu_open);
            _dp += fmt_str(_db+_dp, " mc=");
            _dp += fmt_int(_db+_dp, snap.menu_item_count);
            _dp += fmt_str(_db+_dp, "\n");
            _db[_dp] = 0;
            cm_serial(_db);
            /* 逐窗口信息: wid/has_efm_gfx/tbuf_ptr/标题首字符 */
            for (int i = 0; i < snap.window_count && i < 4; i++) {
                struct efm_wm_win_info *w = &snap.windows[i];
                int tbuf_nonzero = 0;
                if (w->tbuf_ptr) {
                    for (int k = 0; k < 16; k++) {
                        if (w->tbuf_ptr[k] != 0) { tbuf_nonzero = 1; break; }
                    }
                }
                _dp = 0;
                _dp += fmt_str(_db+_dp, "CM: w");
                _dp += fmt_int(_db+_dp, i);
                _dp += fmt_str(_db+_dp, " wid=");
                _dp += fmt_int(_db+_dp, w->wid);
                _dp += fmt_str(_db+_dp, " gfx=");
                _dp += fmt_int(_db+_dp, w->has_efm_gfx);
                _dp += fmt_str(_db+_dp, " tbuf=");
                _dp += fmt_int(_db+_dp, tbuf_nonzero);
                _dp += fmt_str(_db+_dp, " ptr=");
                _dp += fmt_int(_db+_dp, (int)(unsigned long)w->tbuf_ptr);
                _dp += fmt_str(_db+_dp, " x=");
                _dp += fmt_int(_db+_dp, w->x);
                _dp += fmt_str(_db+_dp, " y=");
                _dp += fmt_int(_db+_dp, w->y);
                _dp += fmt_str(_db+_dp, " w=");
                _dp += fmt_int(_db+_dp, w->w);
                _dp += fmt_str(_db+_dp, " h=");
                _dp += fmt_int(_db+_dp, w->h);
                _dp += fmt_str(_db+_dp, " alive=");
                _dp += fmt_int(_db+_dp, w->alive);
                /* [调试] gfx_info_ptr 状态: 指针/magic/rect_count/text_count/seq */
                _dp += fmt_str(_db+_dp, " gip=");
                _dp += fmt_int(_db+_dp, (int)(unsigned long)w->gfx_info_ptr);
                if (w->gfx_info_ptr && w->gfx_info_ptr->magic == EFM_GFX_MAGIC) {
                    _dp += fmt_str(_db+_dp, " mg=ok");
                    _dp += fmt_str(_db+_dp, " rc=");
                    _dp += fmt_int(_db+_dp, w->gfx_info_ptr->rect_count);
                    _dp += fmt_str(_db+_dp, " tc=");
                    _dp += fmt_int(_db+_dp, w->gfx_info_ptr->text_count);
                    _dp += fmt_str(_db+_dp, " sq=");
                    _dp += fmt_int(_db+_dp, (int)w->gfx_info_ptr->seq);
                } else {
                    _dp += fmt_str(_db+_dp, " mg=no");
                }
                _dp += fmt_str(_db+_dp, "\n");
                _db[_dp] = 0;
                cm_serial(_db);
            }
            dbg_frames++;
        }

        /* [性能] 自适应帧率: 60FPS 目标 (16ms 帧间隔).
         * 渲染耗时 < 16ms → sleep 剩余差值; 渲染 ≥16ms → sleep 1ms 让出 CPU.
         * vsync 等待已包含在本帧 elapsed_ms 内, 不会导致 sleep 超时. */
        {
            unsigned long long frame_end = rdtsc_now();
            int elapsed_ms = (int)((frame_end - frame_start) / g_tsc_per_ms);
            int rem = 16 - elapsed_ms;
            if (rem < 1) rem = 1;
            if (rem > 33) rem = 33;
            API->sleep_ms(rem);
        }
        API->yield();
    }
}
