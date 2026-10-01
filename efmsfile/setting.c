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

/* setting.c - EFMOS 设置程序 (编译为 setting.efs, 由内核 exec 加载)
 *
 * 通过内核 API 表 (0x9000) 访问内核功能:
 *   - 打印 (print/print_utf8)
 *   - 清屏 (clear_screen)
 *   - 文件读写 (file_read/file_write)
 *   - 键盘输入 (readline)
 *   - 语言切换 (get_lang/set_lang)
 *   - 保存设置 (save_settings)
 *
 * 编译: gcc -ffreestanding -nostdlib -fno-pic -no-pie -mno-red-zone -mcmodel=large -Ttext=0x100000
 * 入口: _start (必须为 .text 第一个函数, 被 objcopy 保留为二进制起始)
 */

/* [前向声明] _start 在文件最前面, 需要声明 setting_main */
static void setting_main(void);

/* ========== 入口点 (必须为 .text 第一个函数) ==========
 * 内核 exec 加载 .efs 后跳转到加载地址 (0x100000), 即 _start。
 * 保留内核栈帧, 调用 setting_main 后正确返回。
 * [BSS 注意] 内核用 flat-bin 加载 .efs (objcopy 只抽 .text/.rodata/.data),
 * 因此全局/静态变量必须显式 ={0} 初始化, 让 gcc 把它们从 .bss 挪到 .data。 */
__attribute__((naked))
void _start(void) {
    __asm__ volatile(
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"         /* 16 字节栈对齐 */
        "call setting_main\n\t"
        "leave\n\t"                   /* 恢复内核栈帧 */
        "ret\n\t"                     /* 返回到内核 exec */
    );
}

/* ========== 内核 API 表 (与内核 struct kernel_api 匹配) ========== */
#include "efmos/efm_api.h"
/* 鼠标事件 (字段顺序/大小/对齐必须与内核 struct mouse_event 完全一致) */
struct ev_mouse { int dx, dy; unsigned char btn; int x, y; };


/* 构建当前用户的配置文件路径: /users/data/<username>/setting.conf
 * 返回路径长度, 0=失败 (无当前用户) */
static int build_config_path(char *out, int outsz) {
    char username[32];
    if (!API->get_current_user || API->get_current_user(username, sizeof(username)) <= 0)
        return 0;
    const char *prefix = "/users/data/";
    int pl = 5; /* s_len("/users/data/") */
    pl = 0; while (prefix[pl]) pl++;
    int ul = 0; while (username[ul]) ul++;
    const char *suffix = "/setting.conf";
    int sl = 0; while (suffix[sl]) sl++;
    if (pl + ul + sl + 1 > outsz) return 0;
    int p = 0;
    for (int i = 0; i < pl; i++) out[p++] = prefix[i];
    for (int i = 0; i < ul; i++) out[p++] = username[i];
    for (int i = 0; i < sl; i++) out[p++] = suffix[i];
    out[p] = 0;
    return p;
}

/* ========== [新架构] EFS GUI 渲染协议 - 前向声明 + 结构体 ==========
 * gui_add_rect / gui_add_text 等函数被 _pix / _rect / _box 调用,
 * 因此必须放在绘图辅助函数的前面。 */
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
static struct efm_gfx_info g_gui = {0,0,0,{{0,0,0,0,0,0,0}},0,{{0,0,0,""}}};
static int g_cur_x = 0, g_cur_y = 0;   /* 文本光标 (像素) */
/* 与 compositor TTF_FONT_W/H (14x26) 和内核 api->font_w/h 一致, 保证进入桌面
 * (WM+compositor) 模式下 EFS 排版坐标与 compositor 实际绘制完全对齐, 不产生错位。
 * [1.2x 放大] 12→14, 22→26 */
static int g_fw = 14, g_fh = 26, g_vw = 1120;  /* 1120 = 80*14 ASCII 列 */
static void gui_add_rect(int x, int y, int w, int h,
                         unsigned int fill, unsigned int border, int has_border);
static void gui_add_text(int x, int y, unsigned int color, const char *utf8);
static void gui_add_codepoint(int x, int y, unsigned int color, unsigned int cp, int is_cjk);
static void gui_begin(void);
static void gui_flush(struct kernel_api *api);

/* ========== 帧缓冲与按钮绘制辅助 (WM-aware 版本) ==========
 * 传统全屏模式: 坐标 = 全局 FB 绝对坐标 (向后兼容)
 * WM 窗口模式:  坐标 = 当前进程窗口内容区局部坐标 (左上=0,0)
 *   - 鼠标事件 x/y 会被转换为相对内容区 (subtract viewport cx,cy)
 *   - 布局尺寸计算用 cw/ch (内容区宽高) 而不是全屏 hr/vr
 * 所有像素写入通过 API->put_pixel/fill_rect/draw_rect, 由内核负责裁剪/合成。 */
static int g_cx_off = 0, g_cy_off = 0;  /* 内容区全局偏移 (WM 模式用于换算鼠标坐标) */
static int g_scr_w = 0, g_scr_h = 0;    /* 可用像素宽高 (= 内容区 或 全屏) */
static void viewport_refresh(void) {
    if (API->get_viewport) {
        int cx = 0, cy = 0, cw = 0, ch = 0;
        API->get_viewport(&cx, &cy, &cw, &ch);
        g_cx_off = cx; g_cy_off = cy;
        g_scr_w  = cw; g_scr_h  = ch;
    } else if (API->get_fb_info) {
        unsigned int hr = 0, vr = 0;
        API->get_fb_info(&hr, &vr, 0, 0);
        g_cx_off = 0; g_cy_off = 0;
        g_scr_w = (int)hr; g_scr_h = (int)vr;
    } else {
        /* 回退: 直接读 GOP FB (在 WM 模式下可能算出错误全屏值, 但 get_viewport 不存在意味着旧内核, 不会在 WM 里) */
        struct _fb { unsigned int hr; unsigned int vr; unsigned int _pad0[2]; unsigned int ppsl; unsigned int _pad1[3]; unsigned int *fb_base; };
        volatile struct _fb *fb = (volatile struct _fb*)0x1000;
        g_cx_off = 0; g_cy_off = 0;
        g_scr_w = (int)fb->hr; g_scr_h = (int)fb->vr;
    }
}
static void _pix(int x, int y, unsigned int c) {
    gui_add_rect(x, y, 1, 1, c, 0, 0);
}
static void _rect(int x1,int y1,int x2,int y2,unsigned int c) {
    if (x2<x1){int t=x1;x1=x2;x2=t;} if(y2<y1){int t=y1;y1=y2;y2=t;}
    gui_add_rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1, c, 0, 0);
}
static void _box(int x1,int y1,int x2,int y2,unsigned int bo,unsigned int fl) {
    if (x2<x1){int t=x1;x1=x2;x2=t;} if(y2<y1){int t=y1;y1=y2;y2=t;}
    gui_add_rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1, fl, bo, 1);
}
/* [透明底按钮 · setting] 只画 4 条边, 不填内部 = 按钮背景透出窗口底色 (深靛).
 * 把 4 条边拆成 4 个薄 fill_rect (1px 高/宽), 不依赖内核 draw_rect 对 COLOR_TRANSPARENT 的识别,
 * 走纯 GUI 协议, compositor + fallback 都一致。 */
static void _outline(int x1, int y1, int x2, int y2, unsigned int bo) {
    if (x2<x1){int t=x1;x1=x2;x2=t;} if(y2<y1){int t=y1;y1=y2;y2=t;}
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    /* 顶边 (y=y1, 全宽 1px 高) */
    gui_add_rect(x1, y1,     w, 1, bo, 0, 0);
    /* 底边 (y=y2, 全宽 1px 高) */
    if (h > 1) gui_add_rect(x1, y2, w, 1, bo, 0, 0);
    /* 左边 (x=x1, 不含两角 1px 宽) */
    if (h > 2) gui_add_rect(x1, y1 + 1, 1, h - 2, bo, 0, 0);
    /* 右边 (x=x2, 不含两角 1px 宽) */
    if (h > 2 && w > 1) gui_add_rect(x2, y1 + 1, 1, h - 2, bo, 0, 0);
}
#define BTN_BG     0x224488   /* 深蓝按钮底 */
#define BTN_BGHOV  0x3366CC   /* 悬停亮蓝 */
#define BTN_BORDER 0x88BBFF   /* 浅蓝边框 */
#define BTN_TEXT   0xFFFFFF   /* 白字 */
#define DESKTOP_BG 0x1A1A2E   /* 桌面背景 (深蓝黑) */
#define PANEL_BG   0x2B2B3C   /* 表单面板背景 */
#define TEXT_FG    0xFFFFFFu  /* 文字前景色 (白) */
#define COLOR_TRANSPARENT 0xFEEDFACEu  /* draw_char_unicode 透明底标记 (与 Graphics.drv/gfx_draw_char_unicode 一致) */
#define LKEY_ENTER -101
#define LKEY_BS    -102
#define LKEY_ESC   -103

/* ========== [新架构] EFS GUI 渲染协议 - 函数实现 ========== */
static void gui_add_rect(int x, int y, int w, int h,
                         unsigned int fill, unsigned int border, int has_border) {
    if (g_gui.rect_count >= EFM_GFX_MAX_RECTS) return;
    struct efm_gfx_rect *r = &g_gui.rects[g_gui.rect_count++];
    r->x = x; r->y = y; r->w = w; r->h = h;
    r->fill_color = fill; r->border_color = border; r->has_border = has_border;
}
static void gui_add_text(int x, int y, unsigned int color, const char *utf8) {
    if (g_gui.text_count >= EFM_GFX_MAX_TEXTS) return;
    struct efm_gfx_text *t = &g_gui.texts[g_gui.text_count++];
    t->x = x; t->y = y; t->color = color;
    int n = 0;
    while (utf8[n] && n < 95) { t->text[n] = utf8[n]; n++; }
    t->text[n] = 0;
}
static void gui_add_codepoint(int x, int y, unsigned int color, unsigned int cp, int is_cjk) {
    (void)x; (void)y;
    char buf[5]; int bl = 0;
    if (cp < 0x80) { buf[0] = (char)cp; bl = 1; }
    else if (cp < 0x800) { buf[0] = (char)(0xC0 | (cp >> 6)); buf[1] = (char)(0x80 | (cp & 0x3F)); bl = 2; }
    else { buf[0] = (char)(0xE0 | (cp >> 12)); buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[2] = (char)(0x80 | (cp & 0x3F)); bl = 3; }
    buf[bl] = 0;
    if (is_cjk) { (void)is_cjk; }
    /* 追加条件: 颜色相同 + 同一行 (y 相同) + x 连续 (上一尾 = 当前光标 x) */
    if (g_gui.text_count > 0) {
        struct efm_gfx_text *last = &g_gui.texts[g_gui.text_count - 1];
        if (last->color == color && last->y == g_cur_y) {
            /* 计算 last->text 的预期结尾 x = last->x + 所有字符宽度之和 */
            int exp_x = last->x;
            for (int i = 0; last->text[i]; ) {
                unsigned char c = (unsigned char)last->text[i];
                if (c < 0x80) { exp_x += g_fw; i++; }
                else if ((c & 0xE0) == 0xC0) { exp_x += 2*g_fw; i += 2; }
                else if ((c & 0xF0) == 0xE0) { exp_x += 2*g_fw; i += 3; }
                else { exp_x += g_fw; i++; }
            }
            if (exp_x == g_cur_x) {
                int olen = 0; while (last->text[olen]) olen++;
                if (olen + bl < 95) {
                    for (int i = 0; i < bl; i++) last->text[olen + i] = buf[i];
                    last->text[olen + bl] = 0;
                    return;
                }
            }
        }
    }
    if (g_gui.text_count < EFM_GFX_MAX_TEXTS) {
        struct efm_gfx_text *t = &g_gui.texts[g_gui.text_count++];
        t->x = g_cur_x;
        t->y = g_cur_y;
        t->color = color;
        for (int i = 0; i <= bl; i++) t->text[i] = buf[i];
    }
}
static void gui_begin(void) {
    /* [回退 magic=0 改动] compositor 通过 seq 变化检测内容更新,
     * gui_begin 到 gui_flush 之间 seq 不变, compositor 不会重绘此窗口,
     * 保持 canvas 上一帧. 清 magic 反而导致 compositor 走旧路径渲染空白. */
    g_gui.rect_count = 0;
    g_gui.text_count = 0;
}
/* [FALLBACK 辅助] UTF-8 → Unicode codepoint, 用于直接 draw_char_unicode 渲染 */
static int fallback_utf8_to_cp(const unsigned char **ps, unsigned int *out_cp, int *out_cjk) {
    const unsigned char *s = *ps;
    unsigned char c = *s;
    if (c < 0x80) { *out_cp = c; *out_cjk = 0; *ps = s + 1; return 1; }
    if ((c & 0xE0) == 0xC0 && s[1]) {
        *out_cp = ((unsigned int)(c & 0x1F) << 6) | ((unsigned int)(unsigned char)s[1] & 0x3F);
        *out_cjk = 1; *ps = s + 2; return 1;
    }
    if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
        *out_cp = ((unsigned int)(c & 0x0F) << 12) |
                  (((unsigned int)(unsigned char)s[1] & 0x3F) << 6) |
                  ((unsigned int)(unsigned char)s[2] & 0x3F);
        *out_cjk = 1; *ps = s + 3; return 1;
    }
    *ps = s + 1; return 0;
}

static void gui_flush(struct kernel_api *api) {
    g_gui.magic = EFM_GFX_MAGIC;
    g_gui.seq += 1;
    if (api->set_gfx_info) api->set_gfx_info(&g_gui);

    /* ===== [FALLBACK · 合成器启动前] 直接用 API 像素绘制 =====
     * 当 api->wm_enabled==0 (合成器未启动, 全屏模式)
     * 或 set_gfx_info 为空 (旧内核), 没有消费者读 gfx_info 协议数据.
     * 此时必须直接写 FB, 否则界面完全空白. */
    if (!api->wm_enabled || !api->set_gfx_info) {
        /* --- 1. 画所有矩形 (色块 + 边框) --- */
        for (int i = 0; i < g_gui.rect_count; i++) {
            struct efm_gfx_rect *r = &g_gui.rects[i];
            if (r->w <= 0 || r->h <= 0) continue;
            int x1 = r->x, y1 = r->y;
            int x2 = r->x + r->w - 1;
            int y2 = r->y + r->h - 1;
            if (r->has_border && api->draw_rect) {
                api->draw_rect(x1, y1, x2, y2, r->border_color, r->fill_color);
            } else if (api->fill_rect) {
                api->fill_rect(x1, y1, x2, y2, r->fill_color);
            }
        }
        /* --- 2. 画所有文字 (UTF-8 → codepoint → draw_char_unicode) --- */
        for (int i = 0; i < g_gui.text_count; i++) {
            struct efm_gfx_text *t = &g_gui.texts[i];
            if (!t->text[0]) continue;
            int cx = t->x, cy = t->y;
            const unsigned char *s = (const unsigned char *)t->text;
            while (*s) {
                unsigned int cp = 0;
                int cjk = 0;
                if (!fallback_utf8_to_cp(&s, &cp, &cjk)) continue;
                int cell_w = cjk ? (2 * g_fw) : g_fw;
                int cell_h = g_fh;
                if (api->draw_char_unicode) {
                    /* bg=COLOR_TRANSPARENT: 透明底, 叠加到之前绘制的矩形色块或窗口背景上 */
                    api->draw_char_unicode(cx, cy, cp, t->color, COLOR_TRANSPARENT, cell_w, cell_h);
                }
                cx += cell_w;
            }
        }
        /* --- 3. 合成器未接管时手动 flush --- */
        if (api->flush_now) api->flush_now();
    }
}
/* 文本光标 & tputc/tprint (GUI 协议版) */
static void treset(void) { g_cur_x = 0; g_cur_y = 0; }
static void tputc(struct kernel_api *api, int c) {
    if (c == '\n') { g_cur_x = 0; g_cur_y += g_fh; return; }
    if (c == '\r') { g_cur_x = 0; return; }
    if (c < 0x20) return;
    if (g_cur_x + g_fw > g_vw) { g_cur_x = 0; g_cur_y += g_fh; }
    gui_add_codepoint(g_cur_x, g_cur_y, TEXT_FG, (unsigned int)(unsigned char)c, 0);
    g_cur_x += g_fw;
    (void)api;
}
static void tprint(struct kernel_api *api, const char *s) {
    while (*s) tputc(api, *s++);
}
static void tprint_utf8(struct kernel_api *api, const char *s) {
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            tputc(api, (int)c);
            s++;
        } else if ((c & 0xE0) == 0xC0 && s[1]) {
            unsigned int cp = ((unsigned int)(c & 0x1F) << 6) |
                              ((unsigned int)(unsigned char)s[1] & 0x3F);
            if (g_cur_x + 2*g_fw > g_vw) { g_cur_x = 0; g_cur_y += g_fh; }
            gui_add_codepoint(g_cur_x, g_cur_y, TEXT_FG, cp, 1);
            g_cur_x += 2*g_fw;
            s += 2;
        } else if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
            unsigned int cp = ((unsigned int)(c & 0x0F) << 12) |
                              (((unsigned int)(unsigned char)s[1] & 0x3F) << 6) |
                              ((unsigned int)(unsigned char)s[2] & 0x3F);
            if (g_cur_x + 2*g_fw > g_vw) { g_cur_x = 0; g_cur_y += g_fh; }
            gui_add_codepoint(g_cur_x, g_cur_y, TEXT_FG, cp, 1);
            g_cur_x += 2*g_fw;
            s += 3;
        } else {
            s++;
        }
    }
}

/* readline: GUI 协议渲染, 用 gui_add_rect 擦除退格, gui_add_codepoint 画字符 */
static int treadline(struct kernel_api *api, char *buf, int max) {
    int i = 0;
    for (;;) {
        int k = (api->key_poll) ? api->key_poll() : 0;
        if (k == 0) { for (volatile int j = 0; j < 300; j++) __asm__ volatile("pause"); if (api->yield) api->yield(); continue; }
        if (k == LKEY_ESC) { tputc(api, '\n'); gui_flush(api); return -1; }
        if (k == LKEY_ENTER) { tputc(api, '\n'); gui_flush(api); return i; }
        if (k == LKEY_BS) {
            if (i > 0) {
                i--;
                g_cur_x -= g_fw; if (g_cur_x < 0) g_cur_x = 0;
                /* 退格擦除: 用 DESKTOP_BG 覆盖 rect (GUI 模式下追加填充 rect). */
                gui_add_rect(g_cur_x, g_cur_y, g_fw, g_fh, DESKTOP_BG, 0, 0);
                gui_flush(api);
            }
            continue;
        }
        if (k > 0 && (char)k >= 0x20 && i < max - 1) {
            buf[i++] = (char)k;
            tputc(api, k);
            gui_flush(api);
        }
    }
}

/* 按钮信息表: 菜单显示后把每个按钮的矩形+对应 choice 值记下来, 鼠标命中测试 */
#define MAX_BTN 16
static struct { int x1,y1,x2,y2; char val; } g_btns[MAX_BTN] = {{0,0,0,0,0}};
static int g_btn_n = 0;
static void btn_add(int x1, int y1, int x2, int y2, char val) {
    if (g_btn_n >= MAX_BTN) return;
    g_btns[g_btn_n].x1 = x1; g_btns[g_btn_n].y1 = y1;
    g_btns[g_btn_n].x2 = x2; g_btns[g_btn_n].y2 = y2;
    g_btns[g_btn_n].val = val;
    g_btn_n++;
}
/* 命中检测: 左键点击坐标在矩形内返回 val (0x01..0xFF), 否则 0 */
static char btn_hit(int mx, int my) {
    for (int i = 0; i < g_btn_n; i++)
        if (mx >= g_btns[i].x1 && mx <= g_btns[i].x2 &&
            my >= g_btns[i].y1 && my <= g_btns[i].y2)
            return g_btns[i].val;
    return 0;
}
/* 交互选择: 同时 poll 鼠标(左键点击命中返回按钮) + 键盘(readline 返回首字符)
 * 返回: 0=取消, 1..9='1'..'9', '0'..'9' 原样 */
static char choose_digit(struct kernel_api *api) {
    g_btn_n = 0;  /* 每次选择先清空按钮表 — 显示菜单后菜单渲染函数会重新 btn_add */
    /* 为了让鼠标先刷新, 先 poll 一次 */
    for (int loop = 0;; loop++) {
        struct ev_mouse m;
        while (api->mouse_poll && api->mouse_poll(&m)) {
            /* 左键按下 (btn bit0=1) */
            if (m.btn & 1) {
                char h = btn_hit(m.x - g_cx_off, m.y - g_cy_off);
                if (h) return h;
            }
        }
        /* 键盘非阻塞检查: 调用 api->readline 是阻塞的, 我们不能用。
         * 所以这里改用: 10ms 量级忙等后尝试 readline? 不行。
         * 简化方案: 让 setting 的"选择"在渲染完菜单后直接进一个 40 行的小文本框
         * — 我们在渲染菜单时在末尾加一个"请输入数字或点击按钮"提示,
         *   readline 会阻塞, 但 readline 内部内核会继续 poll 鼠标事件,
         *   问题是 readline 会把所有键盘字符收集起来, 鼠标点击不会让 readline 返回。
         * 所以不能用 readline 做主选择循环。
         * — 正确做法: 我们提供一个"mini_poll_read1"函数, 它直接轮询内核 API,
         *   通过在 .efs 端绕不开 readline。让内核提供一个非阻塞的按键读取 API 更合理,
         *   但为了不破坏 API 表结构, 我们使用以下"折中"——
         *   对于 setting 菜单, 使用 getchar via api->put_char + 自读? 没有。
         *   方案 B: 我们不阻塞等待键盘数字输入, 而是让菜单每 200 万次循环
         *   调用一次 api->readline 读取 1 字符, 但那也不行。
         * — 最终方案: 在主循环选择菜单阶段, 我们用"光标闪烁 + 鼠标轮询 +
         *   readline 超时"这种做法很复杂。
         *   实际上用户要的是"能点鼠标就能选"。所以我们把选择循环改成:
         *     1. 只在 readline 内部支持鼠标点击（无效, readline 不返回按钮值）
         *     2. 于是我们改: 不再用 readline 来选择数字; 而是自己实现一个
         *        "getc"的版本 — 我们让内核在 mouse_poll 中也顺带 poll 键盘,
         *        通过共享内存告诉 efs 当前按键? 不行。
         *   3. 新做法: 直接用 readline, 但接受输入"数字回车"的键盘流,
         *      另外让鼠标点击后把"虚拟按键"通过一种 hack 方式反馈 — 不行。
         *
         *   [最可行方案]: .efs 程序直接用 8042 端口轮询! 因为 EFMOS 是 ring0!
         *   这样不用破坏 API 表结构, 直接 inb/outb 就能读键盘扫描码。
         *   → 这样 setting/fileman 自己做"鼠标点击 or 按键"的主选择逻辑,
         *     颜色输入场景再 fallback 到 api->readline 做字符串输入。*/
        (void)loop;
        /* 方案 C: 既然 inb 可用, 先实现 gks() 非阻塞读键 (ASCII),
         *         有键返回 >0, 无键返回 0 */
        break;
    }
    /* 永远走不到, 下面 choose_digit_real 替代 */
    return 0;
}

/* -------- inb/outb 本地实现 (.efs 和内核同 ring0, 可直接操作 I/O 端口) -------- */
static inline unsigned char _inb(unsigned short p) { unsigned char v; __asm__ volatile("inb %1, %0":"=a"(v):"Nd"(p)); return v; }
static inline void _outb(unsigned short p, unsigned char v) { __asm__ volatile("outb %0, %1"::"a"(v),"Nd"(p)); }

#define LKEY_ENTER -101
#define LKEY_BS    -102
#define LKEY_ESC   -103

/* setting/fileman 用户态非阻塞键盘读 (Set 1 码)
 * 用 AUX 位 (bit5=0x20) 跳过鼠标字节, 只处理键盘 Set 1 扫描码 */
static int _kbd_getc_nonblock(void) {
    static int sh = 0, cap = 0;
    unsigned char st = _inb(0x64);
    if (!(st & 0x01)) return 0;
    if (st & 0xC0) { (void)_inb(0x60); return 0; }
    unsigned char b = _inb(0x60);

    /* AUX 位 (bit5=0x20) = 鼠标数据, 跳过 */
    if (st & 0x20) return 0;

    switch (b){case 0x00:case 0xAA:case 0xEE:case 0xFA:case 0xFC:case 0xFD:case 0xFE:case 0xFF: return 0;}

    /* Set 1 扩展前缀 0xE0: 读下一字节忽略 (方向键等) */
    if (b == 0xE0) {
        int w; for (w=0; w<20000; w++) { if (_inb(0x64) & 0x01) break; __asm__ volatile("pause"); }
        if (_inb(0x64) & 0x01) (void)_inb(0x60);
        return 0;
    }

    /* Set 1 release (0x81-0xFF) */
    if (b & 0x80) {
        unsigned char code = b & ~0x80;
        if (code == 0x2A || code == 0x36) sh = 0;
        return 0;
    }

    /* Set 1 make */
    if (b == 0x2A || b == 0x36) { sh = 1; return 0; }
    if (b == 0x3A) { cap = !cap; return 0; }
    if (b == 0x1C) return LKEY_ENTER;
    if (b == 0x0E) return LKEY_BS;
    if (b == 0x01) return LKEY_ESC;

    static const char lo[] = {
        0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
        'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
        'b','n','m',',','.','/',0,0,0,' '
    };
    static const char hi[] = {
        0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0,0,
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
        'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
        'B','N','M','<','>','?',0,0,0,' '
    };
    if (b >= sizeof(lo)) return 0;
    char c = lo[b]; if (c == 0) return 0;
    int is_letter = (c >= 'a' && c <= 'z');
    if (is_letter) { if (sh != cap) c = hi[b]; }
    else if (sh) c = hi[b];
    return (int)(unsigned char)c;
}

/* 选择一个数字 (0-9): 返回 '0'..'9' / 0=用户按 ESC 取消
 * 同时支持鼠标点击命中 (左键点击在 btn_add 注册的矩形内立即返回按钮 value)
 * [鼠标优化] 边缘检测: 只在按钮从松开→按下瞬间触发, 避免按住不放重复触发 */
static char choose_digit_real(struct kernel_api *api) {
    api->mouse_set_cursor(1);
    static unsigned char prev_btn = 0;
    /* 进入选择前, 排空残留事件, 等待按钮释放 */
    {
        struct ev_mouse d;
        int drain = 0;
        while (api->mouse_poll && api->mouse_poll(&d) && drain < 64) { prev_btn = d.btn; drain++; }
        while (prev_btn & 1) {
            drain = 0;
            while (api->mouse_poll && api->mouse_poll(&d) && drain < 64) { prev_btn = d.btn; drain++; }
            if (!(prev_btn & 1)) break;
            int k = (api->key_poll) ? api->key_poll() : 0;
            if (k == LKEY_ESC) return 0;
            for (volatile int j = 0; j < 300; j++) __asm__ volatile("pause");
        }
        prev_btn = 0;
    }
    for (;;) {
        struct ev_mouse m;
        int clicked = 0, cx = 0, cy = 0;
        int drain = 0;
        while (api->mouse_poll && api->mouse_poll(&m) && drain < 64) {
            if (!(prev_btn & 1) && (m.btn & 1)) {
                clicked = 1;
                /* [WM 模式] 鼠标事件返回的是全局屏幕绝对坐标,
                 * 按钮矩形是内容区局部坐标, 必须减去内容区在全局的偏移。 */
                cx = m.x - g_cx_off;
                cy = m.y - g_cy_off;
            }
            prev_btn = m.btn;
            drain++;
        }
        if (clicked) {
            char h = btn_hit(cx, cy);
            if (h) return h;
        }
        int k = (api->key_poll) ? api->key_poll() : 0;
        if (k == 0) { for (volatile int j = 0; j < 300; j++) __asm__ volatile("pause"); if (api->yield) api->yield(); continue; }
        if (k == LKEY_ESC) return 0;
        if (k == LKEY_ENTER) return 0;
        if (k > 0 && ((char)k >= '0' && (char)k <= '9')) return (char)k;
    }
}

/* ========== 无 libc 字符串辅助 ========== */
static int s_len(const char *s) { int l = 0; while (*s++) l++; return l; }
static int s_cmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *(unsigned char*)a - *(unsigned char*)b; }
static char *s_cpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
static char *s_cat(char *d, const char *s) { d += s_len(d); s_cpy(d, s); return d; }

/* 整数转十进制字符串 */
static char *u_toa(unsigned int v, char *buf) {
    char tmp[16]; int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return buf; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int j = 0; while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0; return buf;
}

/* 整数转十六进制字符串 (带 0x) */
static char *u_toa_hex(unsigned int v, char *buf) {
    char tmp[16]; int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return buf; }
    while (v) { int d = v & 0xF; tmp[i++] = d < 10 ? '0' + d : 'A' + d - 10; v >>= 4; }
    buf[0] = '0'; buf[1] = 'x';
    int j = 2; while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0; return buf;
}

/* 计算文字像素宽度 (ASCII=FW, CJK=2*FW) */
static int text_w_px(const char *s, int fw) {
    int w = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) { w += fw; s++; }
        else if (c >= 0xE0) { w += 2*fw; s += 3; }
        else if (c >= 0xC0) { w += 2*fw; s += 2; }
        else s++;
    }
    return w;
}

/* 在 GUI 协议下居中打印: 计算文字像素宽, 输出前导空格 tprint 居中 */
static void print_centered(struct kernel_api *api, const char *text,
                           int btn_x1_px, int btn_w_px, int fw) {
    int tw = text_w_px(text, fw);
    int lead_px = btn_x1_px + (btn_w_px - tw) / 2;
    if (lead_px < 0) lead_px = 0;
    int lead_spaces = lead_px / fw;
    for (int i = 0; i < lead_spaces; i++) tprint(api, " ");
}

/* 十六进制字符串转整数 */
static unsigned int a_toi_hex(const char *s) {
    unsigned int v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s;
        if (c >= '0' && c <= '9') v = v * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = v * 16 + (c - 'A' + 10);
        else break;
        s++;
    }
    return v;
}

/* ========== 配置文件解析 ========== */
struct settings {
    int lang;           /* 0=English, 1=中文 */
    int cursor_blink;   /* 0=off, 1=on */
    unsigned int fg;    /* foreground color */
    unsigned int bg;    /* background color */
};

/* 长度受限键名比较 (不依赖 null 终止, 因为 key 后面跟 '=' 而非 '\0') */
static int key_eq(const char *a, int al, const char *b, int bl) {
    if (al != bl) return 0;
    for (int i = 0; i < al; i++)
        if ((unsigned char)a[i] != (unsigned char)b[i]) return 0;
    return 1;
}

/* 从配置文本解析设置 */
static void parse_config(struct settings *s, const char *text) {
    s->lang = 0; s->cursor_blink = 1; s->fg = 0xFFFFFF; s->bg = 0x000000;
    const char *p = text;
    while (*p) {
        const char *eq = p;
        while (*eq && *eq != '=' && *eq != '\n') eq++;
        if (*eq != '=') { while (*p && *p != '\n') p++; if (*p) p++; continue; }
        int klen = (int)(eq - p);
        const char *val = eq + 1;
        const char *nl = val;
        while (*nl && *nl != '\n') nl++;
        int vlen = (int)(nl - val);
        /* 匹配 key (使用 key_eq 而非 s_cmp, 因为 key 后面是 '=' 不是 '\0') */
        if (key_eq(p, klen, "lang", 4)) {
            s->lang = (vlen > 0 && val[0] == '1') ? 1 : 0;
        } else if (key_eq(p, klen, "cursor_blink", 12)) {
            s->cursor_blink = (vlen > 0 && val[0] == '1') ? 1 : 0;
        } else if (key_eq(p, klen, "fg", 2)) {
            char vb[32]; int i; for (i = 0; i < vlen && i < 31; i++) vb[i] = val[i]; vb[i] = 0;
            s->fg = a_toi_hex(vb);
        } else if (key_eq(p, klen, "bg", 2)) {
            char vb[32]; int i; for (i = 0; i < vlen && i < 31; i++) vb[i] = val[i]; vb[i] = 0;
            s->bg = a_toi_hex(vb);
        }
        p = nl; if (*p) p++;
    }
}

/* 生成配置文本, 返回长度 */
static int build_config(char *buf, const struct settings *s) {
    char *p = buf;
    char num[20];
    s_cpy(p, "lang="); p += 5;
    u_toa((unsigned int)s->lang, num); s_cpy(p, num); p += s_len(num);
    *p++ = '\n';
    s_cpy(p, "cursor_blink="); p += 13;
    u_toa((unsigned int)s->cursor_blink, num); s_cpy(p, num); p += s_len(num);
    *p++ = '\n';
    s_cpy(p, "fg="); p += 3;
    u_toa_hex(s->fg, num); s_cpy(p, num); p += s_len(num);
    *p++ = '\n';
    s_cpy(p, "bg="); p += 3;
    u_toa_hex(s->bg, num); s_cpy(p, num); p += s_len(num);
    *p++ = '\n';
    return (int)(p - buf);
}

/* ========== 设置程序主逻辑 ========== */
static void setting_main(void) {
    struct kernel_api *api = API;
    if (api->magic != 0xEF110001) return;   /* API 表无效, 直接返回 */

    /* [WM-aware] 获取本进程"虚拟屏幕"尺寸 = 窗口内容区 (窗口模式) 或 全屏 (登录前)
     * 每次重绘菜单前刷新, 允许用户拖动窗口改变大小后布局自适应。 */
    viewport_refresh();

    /* 字体像素尺寸 (ASCII). 与内核对齐, 替代硬编码 8x16. */
    const int FW = (api->font_w > 0) ? api->font_w : 8;
    const int FH = (api->font_h > 0) ? api->font_h : 16;
    /* 初始化 GUI 协议文字渲染全局参数 */
    g_fw = FW; g_fh = FH;
    g_vw = 80 * FW;
    if (g_scr_w > 0) g_vw = g_scr_w;

    api->mouse_set_cursor(1);   /* 进入设置就显示鼠标 */

    /* 读取当前配置 */
    char config_path[128];
    if (!build_config_path(config_path, sizeof(config_path))) {
        /* 无当前用户: 使用默认配置 */
        config_path[0] = 0;
    }
    char config[512];
    int clen = -1;
    if (config_path[0])
        clen = api->file_read(config_path, config, sizeof(config) - 1);
    if (clen < 0) clen = 0;
    config[clen] = 0;

    struct settings s;
    parse_config(&s, config);

    /* 若 setting.conf 为空 (新用户首次打开 setting), 回退读 /users/sys.conf 的语言,
     * 保持与登录界面一致; 其他字段用 parse_config 默认值。 */
    if (clen == 0) {
        char sbuf[64];
        int sl = api->file_read ? api->file_read("/users/sys.conf", sbuf, sizeof(sbuf)-1) : -1;
        if (sl > 0) {
            sbuf[sl] = 0;
            const char *p = sbuf;
            while (*p) {
                if (p[0]=='l' && p[1]=='a' && p[2]=='n' && p[3]=='g' && p[4]=='=') {
                    s.lang = (p[5] == '1') ? 1 : 0;
                    break;
                }
                while (*p && *p != '\n') p++;
                if (*p) p++;
            }
        }
    }

    /* 同步语言到内核 (让菜单立即显示正确语言) */
    api->set_lang(s.lang);

    for (;;) {
        /* [新架构] api->clear_screen 仅用于 WM 模式清 tbuf, 不再画像素.
         * 改用 gui_begin() 重置 GUI 数据计数 → 重绘所有 rects + texts. */
        gui_begin();
        treset();
        g_btn_n = 0;  /* 清空按钮表 */
        /* 桌面背景: 填充整个视口 (compositor 也会填 c_bg, 但显式画可覆盖) */
        _rect(0, 0, g_vw - 1, (g_scr_h > 0 ? g_scr_h : 25*FH) - 1, DESKTOP_BG);

        /* 标题 */
        if (s.lang) tprint_utf8(api, "==== EFMOS 设置 ====\n\n");
        else        tprint(api, "==== EFMOS Settings ====\n\n");

        /* 按钮高 2*FH-2 (≈30px), 文字水平+垂直居中.
         * 左列: 1/2/3   右列: 4/5/0. 每行占2文本行.
         * [修复] 按钮宽度根据实际视口宽度 g_vw 自适应, 不再硬编码 37*FW.
         * 原来固定 37*FW=444, 右列按钮右边=923px, 但窗口内容区可能只有 740px,
         * 导致右列按钮溢出被裁剪/错位. */
        int btn_h = 2 * FH - 2;
        int sgap_x = 2 * FW;
        int sleft_x1 = 1 * FW;
        int savail_w = g_vw - sleft_x1 - 2;  /* 可用宽度 (留右边距) */
        int sbtn_w = (savail_w - sgap_x) / 2;
        if (sbtn_w < 12 * FW) sbtn_w = 12 * FW;  /* 最小宽度 */
        int sright_x1 = sleft_x1 + sbtn_w + sgap_x;
        int s_cur_y = 2 * FH;

        char buf_l[64], buf_r[64], valbuf[32];

#define SET_BOX_XY(x1, y1, w, h, v) do { \
    int yy1 = (y1), yy2 = (y1) + (h) - 1; \
    int xx1 = (x1), xx2 = (x1) + (w) - 1; \
    /* [透明底按钮] _outline 只画 4 条边, 不填内部 → 透出 DESKTOP_BG 深靛.
     * 原先 _box(BTN_BG=深蓝) 填的实心色块在 GUI 有文字时不统一, 且 fallback
     * 渲染路径中实心按钮会压下面的装饰元素。*/ \
    _outline(xx1, yy1, xx2, yy2, BTN_BORDER); \
    btn_add(xx1, yy1, xx2, yy2, (v)); \
} while(0)

        /* Row 1: 左=1 语言, 右=4 背景色 */
        SET_BOX_XY(sleft_x1, s_cur_y, sbtn_w, btn_h, '1');
        SET_BOX_XY(sright_x1, s_cur_y, sbtn_w, btn_h, '4');
        /* Row 2: 左=2 光标闪烁, 右=5 保存退出 */
        SET_BOX_XY(sleft_x1, s_cur_y + 2*FH, sbtn_w, btn_h, '2');
        SET_BOX_XY(sright_x1, s_cur_y + 2*FH, sbtn_w, btn_h, '5');
        /* Row 3: 左=3 前景色, 右=0 不保存退出 */
        SET_BOX_XY(sleft_x1, s_cur_y + 4*FH, sbtn_w, btn_h, '3');
        SET_BOX_XY(sright_x1, s_cur_y + 4*FH, sbtn_w, btn_h, '0');
#undef SET_BOX_XY

        /* [修复] 文字直接定位到按钮中心 (像素精确), 不再用 tprint 空格对齐.
         * 原实现用 tprint("\n") + 空格定位文字, 但按钮是像素精确的,
         * 文字行高 FH=26 与按钮高 btn_h 不匹配, 导致文字和按钮垂直错位.
         * 现在直接设置 g_cur_x/g_cur_y 到按钮中心, 与 userman.c 一致. */
        for (int row = 0; row < 3; row++) {
            int btn_y = s_cur_y + row * 2 * FH;
            int text_y = btn_y + (btn_h - FH) / 2;
            if (row == 0) {
                if (s.lang) { s_cpy(buf_l, "1. 语言: 中文"); }
                else        { s_cpy(buf_l, "1. Language: English"); }
                if (s.lang) { s_cpy(buf_r, "4. 背景色: "); s_cat(buf_r, u_toa_hex(s.bg, valbuf)); }
                else        { s_cpy(buf_r, "4. Background: "); s_cat(buf_r, u_toa_hex(s.bg, valbuf)); }
            } else if (row == 1) {
                if (s.lang) { s_cpy(buf_l, "2. 光标闪烁: "); s_cat(buf_l, s.cursor_blink ? "开" : "关"); }
                else        { s_cpy(buf_l, "2. Cursor blink: "); s_cat(buf_l, s.cursor_blink ? "On" : "Off"); }
                if (s.lang) { s_cpy(buf_r, "5. 保存并退出"); }
                else        { s_cpy(buf_r, "5. Save and exit"); }
            } else {
                if (s.lang) { s_cpy(buf_l, "3. 前景色: "); s_cat(buf_l, u_toa_hex(s.fg, valbuf)); }
                else        { s_cpy(buf_l, "3. Foreground: "); s_cat(buf_l, u_toa_hex(s.fg, valbuf)); }
                if (s.lang) { s_cpy(buf_r, "0. 不保存退出"); }
                else        { s_cpy(buf_r, "0. Discard & exit"); }
            }
            int ltw = text_w_px(buf_l, FW);
            int rtw = text_w_px(buf_r, FW);
            /* 左列文字: 水平居中在左按钮内 */
            g_cur_y = text_y;
            g_cur_x = sleft_x1 + (sbtn_w - ltw) / 2;
            if (s.lang) tprint_utf8(api, buf_l); else tprint(api, buf_l);
            /* 右列文字: 水平居中在右按钮内 */
            g_cur_y = text_y;
            g_cur_x = sright_x1 + (sbtn_w - rtw) / 2;
            if (s.lang) tprint_utf8(api, buf_r); else tprint(api, buf_r);
        }
        /* 提示行: 在按钮区下方 */
        g_cur_x = 0;
        g_cur_y = s_cur_y + 6 * FH + 4;
        if (s.lang) tprint_utf8(api, "请选择 (数字键 / 鼠标点击按钮): ");
        else        tprint(api, "Choose (key 0-5 or click button): ");

        /* [新架构] 提交 GUI 数据给 compositor → 等待用户点击/按键 */
        gui_flush(api);

        /* 读取用户选择: 数字按键 or 鼠标左键点击按钮命中 */
        char ch = choose_digit_real(api);
        if (ch == 0) break;   /* 0 = 用户按 ESC 取消, 视为退出 */

        if (ch == '0' || ch == 'q' || ch == 'Q') {
            break;   /* 不保存退出 */
        } else if (ch == '1') {
            /* 切换语言 */
            s.lang = !s.lang;
            api->set_lang(s.lang);
        } else if (ch == '2') {
            /* 切换光标闪烁 */
            s.cursor_blink = !s.cursor_blink;
        } else if (ch == '3' || ch == '4') {
            /* 输入颜色值 (GUI 协议渲染 + treadline) */
            gui_begin();
            treset();
            _rect(0, 0, g_vw - 1, (g_scr_h > 0 ? g_scr_h : 25*FH) - 1, DESKTOP_BG);
            if (s.lang) tprint_utf8(api, "==== EFMOS 设置 ====\n\n输入十六进制颜色 (如 0xFFFFFF): ");
            else        tprint(api, "==== EFMOS Settings ====\n\nEnter hex color (e.g. 0xFFFFFF): ");
            gui_flush(api);
            char color[32];
            int cr = treadline(api, color, sizeof(color));
            if (cr > 0) {
                unsigned int c = a_toi_hex(color);
                if (ch == '3') s.fg = c;
                else           s.bg = c;
            }
        } else if (ch == '5') {
            /* 保存并退出 (GUI 协议提示) */
            char newconf[256];
            int len = build_config(newconf, &s);
            gui_begin();
            treset();
            _rect(0, 0, g_vw - 1, (g_scr_h > 0 ? g_scr_h : 25*FH) - 1, DESKTOP_BG);
            if (s.lang) tprint_utf8(api, "==== EFMOS 设置 ====\n\n");
            else        tprint(api, "==== EFMOS Settings ====\n\n");
            if (config_path[0] && api->file_write(config_path, newconf, len) == 0) {
                api->set_lang(s.lang);
                api->save_settings();
                if (s.lang) tprint_utf8(api, "\n设置已保存!\n");
                else        tprint(api, "\nSettings saved!\n");
                gui_flush(api);
                for (volatile int i = 0; i < 5000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                break;
            } else {
                if (s.lang) tprint_utf8(api, "\n保存失败!\n");
                else        tprint(api, "\nSave failed!\n");
                gui_flush(api);
                for (volatile int i = 0; i < 5000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
            }
        }
    }
    api->mouse_set_cursor(1);  /* 返回 shell, 光标由内核决定显示 */
}
