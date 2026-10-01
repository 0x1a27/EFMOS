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

/* efmAether.c - EFMOS 桌面信息提供者 (编译为 efmAether.efs)
 * 加载地址: 0x900000 (9MB)
 * 入口: _start
 *
 * 【新架构 · 职责分工】
 *   - efmAether (本程序): 仅负责"提供桌面渲染信息", 不做任何像素操作.
 *     不写 framebuffer, 不分配 canvas, 不调用 fill_rect/draw_char/blit/flush.
 *     只在固定地址 0x6000 写入 struct aether_render_info (颜色方案 + 桌面文字 + 布局).
 *   - efmcompositor: 唯一的渲染执行者, 每帧主动读取 0x6000 的信息,
 *     用自己的 canvas + API->draw_char_unicode (TTF, SSAA, UTF-8, CJK)
 *     完成所有桌面/窗口/任务栏/文字的像素渲染 + flush.
 *
 * 为什么这样做:
 *   历史上 efmAether 与 efmcompositor 是两套独立渲染实现, 都写 framebuffer,
 *   导致渲染冲突 (撕裂/拖尾/闪烁/文字错位). 本重构彻底消除冲突:
 *   efmAether 退化为"桌面配置信息源", efmcompositor 是唯一的像素渲染者.
 *
 * 共享结构协议 (0x6000):
 *   struct aether_render_info {
 *     unsigned int magic;          // 写 AETHER_INFO_MAGIC (0x41455448) 表示就绪
 *     volatile unsigned int seq;   // 每次信息变化 +1, efmcompositor 据此刷新
 *     int font_w, font_h;          // TTF 字体单元尺寸 (efmcompositor 实际使用)
 *     unsigned int clr_*;          // 各种颜色方案
 *     struct aether_text_item {
 *         int x, y;
 *         unsigned int color;
 *         char text[96];           // UTF-8 字符串
 *     } desktop_title, start_text, extra[16];
 *     int extra_count;
 *   };
 *
 * 本程序只做 3 件事:
 *   1. 检查 kernel API 可用性
 *   2. 构造一份合理的桌面渲染信息 (颜色方案 + 桌面/Start 文案)
 *   3. 定期刷新 seq, 保持后台活动 (空闲时 sleep 33ms)
 */

/* ========== 内核 API 表 (必须与 kernel.c struct kernel_api 完全一致) ==========
 * [CRITICAL] 之前用精简 padding (_pad1[4]/_pad2[8]/_pad3[8]) 导致 sleep_ms/yield
 * 偏移错位 8 字节, 实际指向 load_driver/driver_count → efmAether 永不让出 CPU,
 * 饿死 efmcompositor → 桌面不渲染. 现使用与 efmcompositor.c 完全一致的全字段定义. */
#include "efmos/efm_api.h"


/* ========== aether_render_info 共享结构 (必须与 efmcompositor.c 完全一致) ========== */
#define AETHER_INFO_MAGIC  0x41455448u   /* "AETH" */
#define AETHER_INFO_ADDR   0x6000
#define AETHER_MAX_TEXT    16
#define AETHER_MAX_MENU    16

struct aether_text_item {
    int x, y;
    unsigned int color;
    char text[96];   /* UTF-8 */
};

/* 开始菜单项: label = 显示文字, program = 启动的 .efs 程序名 (不带 .efs 后缀) */
struct aether_menu_item {
    char label[32];     /* UTF-8 显示文字 */
    char program[32];    /* 对应 /EFMOS/<program>.efs */
};

struct aether_render_info {
    unsigned int magic;                    /* AETHER_INFO_MAGIC = 信息已就绪 */
    volatile unsigned int seq;             /* 每次更新 +1 */
    int font_w, font_h;                    /* TTF 字体单元格尺寸 */
    /* 颜色方案 */
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
    /* 自定义扩展文字 (桌面图标/小组件等) */
    struct aether_text_item extra[AETHER_MAX_TEXT];
    int extra_count;
    /* 开始菜单项 (点击开始按钮后显示) */
    struct aether_menu_item menu_items[AETHER_MAX_MENU];
    int menu_count;
};

/* 颜色方案 (深蓝紫桌面, 蓝灰标题栏, 蓝色激活态) */
#define CLR_BG            0x001A1A2E
#define CLR_DESKTOP_TEXT  0x00FFFFFF
#define CLR_BORDER        0x00444454
#define CLR_TITLE_ACTIVE  0x001976D2
#define CLR_TITLE_INACT   0x0037474F
#define CLR_TITLE_TEXT    0x00FFFFFF
#define CLR_CLOSE_BG      0x00505050
#define CLR_CLOSE_HOVER   0x00C62828
#define CLR_CLOSE_TEXT    0x00FFFFFF
#define CLR_TASKBAR_BG    0x00263238
#define CLR_TASKBAR_BTN   0x0037474F
#define CLR_TASKBAR_ACT   0x001976D2
/* 开始菜单颜色 (深色背景 + 蓝色高亮, 类似 Windows 10) */
#define CLR_MENU_BG       0x00263238
#define CLR_MENU_ITEM     0x0037474F
#define CLR_MENU_HOVER    0x001976D2
#define CLR_MENU_TEXT     0x00FFFFFF
#define CLR_MENU_BORDER   0x00444454

/* 字符单元格尺寸 (步进值): 增大可增加字符间距与行距.
 * glyph 实际渲染高度由 Graphics.drv 的 g_ttf_pixel_size=16 决定, 不受此值影响,
 * 多出的像素成为字符右侧/下方的间距.
 * [1.2x 放大] 12→14, 22→26 */
#define TTF_FONT_W  14   /* ASCII 字符步进 (1.2x: 原 12) */
#define TTF_FONT_H  26   /* 行高步进 (1.2x: 原 22) */

/* ========== 入口函数 (必须位于 .text 最前面) ========== */
void aether_main(void);

__attribute__((naked, section(".text.start")))
void _start(void) {
    __asm__ volatile(
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"
        "call aether_main\n\t"
        "leave\n\t"
        "ret\n\t"
    );
}

/* ========== COM1 串口调试输出 (不依赖内核 API) ========== */
static void cm_putc(char c) {
    __asm__ volatile(
        "mov $0x3FD, %%dx\n1: inb %%dx, %%al\n testb $0x20, %%al\n jz 1b\n"
        "mov $0x3F8, %%dx\n movb %0, %%al\n outb %%al, %%dx\n"
        : : "c"(c) : "rax", "rdx", "memory"
    );
}
static void cm_serial(const char *s) { while (*s) cm_putc(*s++); }

/* 安全的字符串拷贝 (dest 固定 96 字节, 兼容 UTF-8, 支持 volatile 目标) */
static void safe_strcpy96(volatile char *dest, const char *src) {
    int i = 0;
    for (; i < 95 && src[i]; i++) dest[i] = src[i];
    dest[i] = 0;
}

/* ========== 主程序: 仅写 aether_render_info, 不做任何像素操作 ========== */
void aether_main(void) {
    cm_serial("AE:enter_info_provider\n");

    /* 1. 检查 kernel API magic */
    if (API->magic != API_MAGIC) {
        cm_serial("AE:bad_magic\n");
        return;
    }
    cm_serial("AE:magic_ok\n");

    /* 2. 检查需要的最小 API 子集 (只需要 sleep_ms + yield + print 做调试) */
    if (!API->sleep_ms || !API->yield) {
        cm_serial("AE:api_null_minimal\n");
        return;
    }
    cm_serial("AE:api_ok_minimal\n");
    if (API->print) API->print("efmAether: info provider mode active (0x6000)\n");

    /* 等待一小段时间, 确保 efmcompositor 已先启动并清零 0x6000 区域.
     * (若 efmcompositor 未启动, 本程序也安全 — 只是 0x6000 无人读取而已) */
    API->sleep_ms(80);
    cm_serial("AE:slept_80ms\n");

    /* 3. 获取 0x6000 指针并填写桌面渲染信息
     *    注意: 先填所有字段, 最后再写 magic (让 efmcompositor 看到原子的完整信息) */
    volatile struct aether_render_info *ai =
        (volatile struct aether_render_info *)(unsigned long)AETHER_INFO_ADDR;

    /* 字体单元尺寸 (与 efmcompositor 的 TTF_FONT_W/H 保持一致) */
    ai->font_w = TTF_FONT_W;
    ai->font_h = TTF_FONT_H;

    /* 颜色方案 */
    ai->clr_bg = CLR_BG;
    ai->clr_desktop_text = CLR_DESKTOP_TEXT;
    ai->clr_border = CLR_BORDER;
    ai->clr_title_active = CLR_TITLE_ACTIVE;
    ai->clr_title_inact = CLR_TITLE_INACT;
    ai->clr_title_text = CLR_TITLE_TEXT;
    ai->clr_close_bg = CLR_CLOSE_BG;
    ai->clr_close_hover = CLR_CLOSE_HOVER;
    ai->clr_close_text = CLR_CLOSE_TEXT;
    ai->clr_taskbar_bg = CLR_TASKBAR_BG;
    ai->clr_taskbar_btn = CLR_TASKBAR_BTN;
    ai->clr_taskbar_act = CLR_TASKBAR_ACT;

    /* 开始菜单颜色方案 */
    ai->clr_menu_bg     = CLR_MENU_BG;
    ai->clr_menu_item    = CLR_MENU_ITEM;
    ai->clr_menu_hover   = CLR_MENU_HOVER;
    ai->clr_menu_text    = CLR_MENU_TEXT;
    ai->clr_menu_border  = CLR_MENU_BORDER;

    /* 开始菜单项: label = 显示文字, program = /EFMOS/<program>.efs
     * 点击菜单项 → 内核 efs_spawn_async(program) 在新窗口启动 */
    {
        int n = 0;
        #define ADD_MENU(lbl, prog) do { \
            if (n < AETHER_MAX_MENU) { \
                int i=0; const char *s=lbl; while(s[i] && i<31){ai->menu_items[n].label[i]=s[i];i++;} ai->menu_items[n].label[i]=0; \
                i=0; s=prog; while(s[i] && i<31){ai->menu_items[n].program[i]=s[i];i++;} ai->menu_items[n].program[i]=0; \
                n++; } } while(0)
        ADD_MENU("Setting",      "setting");
        ADD_MENU("User Manager", "userman");
        ADD_MENU("Terminal",     "shell");
        #undef ADD_MENU
        ai->menu_count = n;
        if (n == 3) cm_serial("AE:menu_count=3\n");
        else if (n == 0) cm_serial("AE:menu_count=0\n");
        else { cm_serial("AE:menu_count=other\n"); }
    }

    /* 桌面标题文字 (efmcompositor 在 (10,10) 画 TTF UTF-8) */
    ai->desktop_title.x = 10;
    ai->desktop_title.y = 10;
    ai->desktop_title.color = CLR_DESKTOP_TEXT;
    safe_strcpy96(ai->desktop_title.text, "EFMOS Desktop (Aether Info)");

    /* 任务栏 Start 按钮: 留空 start_text.text 让 efmcompositor 绘制默认 logo
     * (EFMOS-logo-b.png 的 alpha map), 这样显示的是实际图形而非文字 "EFMOS".
     * 如果要换回文字, 取消下面 safe_strcpy96 注释并清空下一行即可. */
    ai->start_text.x = 6;
    ai->start_text.y = 0;
    ai->start_text.color = CLR_TITLE_TEXT;
    ai->start_text.text[0] = 0;
    /* safe_strcpy96(ai->start_text.text, "EFMOS"); */

    /* 自定义扩展文字: 可以在这里添加桌面图标文字、时钟、提示等.
     * 预留 0 个, efmcompositor 按 extra_count 实际值显示. */
    ai->extra_count = 0;

    /* seq=1 表示首次提交, 最后写 magic = 告诉 efmcompositor "可以读了" */
    ai->seq = 1;
    ai->magic = AETHER_INFO_MAGIC;

    cm_serial("AE:info_written@0x6000\n");
    if (API->print) API->print("efmAether: render info written to 0x6000, seq=1\n");

    /* 4. 后台循环: 定期刷新 seq 以保持信息新鲜, 空闲时让出 CPU.
     *    [关键修复] 每 ~1 秒重写完整 menu 数据 (menu_count + menu_items),
     *    防止被其他代码覆盖后丢失 (之前只 seq++ 不重写数据,
     *    一旦 menu_count 被清零就永远无法恢复). */
    unsigned int ticks = 0;
    while (1) {
        API->sleep_ms(33);   /* ~30Hz 唤醒检查 */
        API->yield();
        ticks++;
        /* 每 ~1 秒 (~30 ticks * 33ms) 重写 menu 数据并 seq++,
         * 确保 menu_count 持久化, 防止被覆盖后无法恢复 */
        if (ticks % 30 == 0) {
            if (ai->magic == AETHER_INFO_MAGIC) {
                /* 重写菜单项, 防止被覆盖 */
                {
                    int n = 0;
                    #define ADD_MENU_LOOP(lbl, prog) do { \
                        if (n < AETHER_MAX_MENU) { \
                            int i=0; const char *s=lbl; while(s[i] && i<31){ai->menu_items[n].label[i]=s[i];i++;} ai->menu_items[n].label[i]=0; \
                            i=0; s=prog; while(s[i] && i<31){ai->menu_items[n].program[i]=s[i];i++;} ai->menu_items[n].program[i]=0; \
                            n++; } } while(0)
                    ADD_MENU_LOOP("Setting",      "setting");
                    ADD_MENU_LOOP("User Manager", "userman");
                    ADD_MENU_LOOP("Terminal",     "shell");
                    #undef ADD_MENU_LOOP
                    ai->menu_count = n;
                }
                ai->seq += 1;
            }
        }
    }
}
