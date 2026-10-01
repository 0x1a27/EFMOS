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

/* userman.c - EFMOS 用户管理器程序 (编译为 userman.efs)
 * 加载地址: 0x500000 (5MB, 不与其他程序冲突)
 * 入口: _start (二进制首字节, 必须为 .text 第一个函数)
 *
 * 功能:
 *   - 显示当前用户 & 所有用户列表
 *   - 切换用户 (login): 切换当前登录用户, shell 默认目录切到用户家目录
 *   - 创建新用户: 在 /users 下创建 <username> 目录
 *   - 删除用户: 只能删除非当前登录用户, 目录需为空
 *   - 中英文双语 (跟随内核 get_lang / set_lang, 与 setting 语言互通)
 *   - 鼠标点击选项 (左键直接点击按钮命中选择) */

/* ========== 内核 API 表 (必须与内核 struct kernel_api 字段顺序/对齐完全一致) ========== */
#include "efmos/efm_api.h"
struct ev_mouse { int dx, dy; unsigned char btn; int x, y; };

/* ========== 入口点 (必须为 .text 第一个函数!) ==========
 * 内核 run_efs 把 .efs 加载到 load_addr 后, 直接跳转到 load_addr 执行,
 * 即二进制首字节 = 入口。所以 _start 必须排在所有函数之前,
 * 否则 GCC 按源码顺序把其它 static 函数排到 .text 开头,
 * 内核会跳进错误的函数 → 程序立即崩溃/退出。
 * [BSS 注意] 内核用 flat-bin 加载 .efs (objcopy 只抽 .text/.rodata/.data),
 * 因此全局/静态变量必须显式 ={0} 初始化, 让 gcc 把它们从 .bss 挪到 .data。 */
static void userman_main(void);
__attribute__((naked))
void _start(void) {
    __asm__ volatile(
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"
        "call userman_main\n\t"
        "leave\n\t"
        "ret\n\t"
    );
}

/* ========== 密码哈希 (DJB2 变体 + 用户名盐) ==========
 *   非可逆加密, 存储哈希值而非明文。
 *   passwd 文件格式: 单行 8 位十六进制哈希值 */
static unsigned int hash_pw(const char *pw, const char *salt) {
    unsigned int h = 5381;
    while (*salt) { h = h * 33 + (unsigned char)*salt++; }
    while (*pw)   { h = h * 33 + (unsigned char)*pw++; }
    return h ^ (h >> 16);
}

/* 构建 /users/data/<username>/passwd 路径 */
static void build_passwd_path(char *out, int outsz, const char *username) {
    const char *p = "/users/data/";
    int pl = 0; while (p[pl]) pl++;
    int ul = 0; while (username[ul]) ul++;
    const char *s = "/passwd";
    int sl = 0; while (s[sl]) sl++;
    int n = 0;
    for (int i = 0; i < pl && n < outsz-1; i++) out[n++] = p[i];
    for (int i = 0; i < ul && n < outsz-1; i++) out[n++] = username[i];
    for (int i = 0; i < sl && n < outsz-1; i++) out[n++] = s[i];
    out[n] = 0;
}

/* 构建 /users/data/<username>/setting.conf 路径 */
static void build_setting_path(char *out, int outsz, const char *username) {
    const char *p = "/users/data/";
    int pl = 0; while (p[pl]) pl++;
    int ul = 0; while (username[ul]) ul++;
    const char *s = "/setting.conf";
    int sl = 0; while (s[sl]) sl++;
    int n = 0;
    for (int i = 0; i < pl && n < outsz-1; i++) out[n++] = p[i];
    for (int i = 0; i < ul && n < outsz-1; i++) out[n++] = username[i];
    for (int i = 0; i < sl && n < outsz-1; i++) out[n++] = s[i];
    out[n] = 0;
}

/* 检查用户是否设置了密码: 读 passwd 文件, 返回 1=有密码, 0=无密码 */
static int user_has_password(const char *username) {
    char path[128]; build_passwd_path(path, sizeof(path), username);
    char buf[32];
    int r = API->file_read ? API->file_read(path, buf, sizeof(buf)-1) : -1;
    if (r <= 0) return 0;
    buf[r] = 0;
    /* 检查是否为有效哈希 (至少 1 个十六进制字符) */
    for (int i = 0; i < r; i++) {
        char c = buf[i];
        if ((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')) return 1;
    }
    return 0;
}

/* 验证密码: 返回 1=正确, 0=错误 */
static int verify_password(const char *username, const char *pw) {
    char path[128]; build_passwd_path(path, sizeof(path), username);
    char buf[32];
    int r = API->file_read ? API->file_read(path, buf, sizeof(buf)-1) : -1;
    if (r <= 0) return 0;
    buf[r] = 0;
    /* 解析存储的哈希 (十六进制) */
    unsigned int stored = 0;
    for (int i = 0; i < r; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r' || c == 0) break;
        unsigned int d;
        if (c>='0'&&c<='9') d = c-'0';
        else if (c>='a'&&c<='f') d = c-'a'+10;
        else if (c>='A'&&c<='F') d = c-'A'+10;
        else break;
        stored = (stored << 4) | d;
    }
    unsigned int actual = hash_pw(pw, username);
    return (stored == actual) ? 1 : 0;
}

/* 保存密码哈希到 /users/data/<username>/passwd */
static void save_password(const char *username, const char *pw) {
    char path[128]; build_passwd_path(path, sizeof(path), username);
    unsigned int h = hash_pw(pw, username);
    char hex[12]; int n = 0;
    const char *hc = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) hex[n++] = hc[(h >> (i*4)) & 0xF];
    hex[n++] = '\n'; hex[n] = 0;
    if (API->file_write) API->file_write(path, hex, n);
}

/* ========== 帧缓冲 & 按钮辅助常量 ==========
 * 必须与内核 struct gop_fb 完全一致:
 *   struct gop_fb { unsigned long long fb_base; unsigned int hr, vr, ppsl; }; */
struct _fb {
    unsigned long long fb_base;
    unsigned int hr, vr, ppsl;
};
#define FB ((volatile struct _fb*)0x1000)
/* 桌面表单风格颜色 (与 efmlogin 统一) */
#define DESKTOP_BG   0x1A1A2E
#define PANEL_BG     0x2B2B3C
#define PANEL_BORDER 0x6E6E8E
#define TITLE_BAR_BG 0x3D3D5C
#define BTN_OUTLINE  0x9AB6D8
#define BTN_CURRENT  0xE0C060   /* 当前用户高亮描边 */
#define COLOR_TRANSPARENT 0xFEEDFACEu  /* 透明背景标记 (draw_char_unicode) */
#define TEXT_FG           0xFFFFFFu    /* 文字前景色 (白) */
#define MAX_BTN 48
#define MAX_USERS 32
#define LKEY_ENTER -101
#define LKEY_BS    -102
#define LKEY_ESC   -103

/* ========== [新架构] EFS GUI 渲染协议 ==========
 * 不再直接写像素到 back buffer, 而是构造渲染信息 (rects + texts),
 * 由 compositor 统一从 set_gfx_info 提交的指针读取并绘制. */
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
static int g_cur_x = 0, g_cur_y = 0;   /* 文本光标 (像素) - 在 gui_add_codepoint 内使用, 必须先于其声明 */
/* 与 compositor/kernel api font_w/h (12x22) 同步: 进入桌面后 WM 模式下布局
 * (按钮大小/输入框位置/光标位置) 与 compositor TTF 实际绘制尺寸对齐, 避免文字错位。
 * gui_flush fallback 需在此定义, 否则 fallback 内 cell_w/cell_h = 8x16 错误。 */
static int g_fw = 14, g_fh = 26, g_vw = 1120;
/* [WM 模式] 窗口内容区全局偏移: 鼠标事件 x/y 是屏幕绝对坐标,
 * 按钮命中测试前必须减去此偏移 (与 setting.c 一致). */
static int g_cx_off = 0, g_cy_off = 0;
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
    g_gui.rect_count = 0;
    g_gui.text_count = 0;
}
static void gui_flush(struct kernel_api *api) {
    g_gui.magic = EFM_GFX_MAGIC;
    g_gui.seq += 1;
    if (api->set_gfx_info) api->set_gfx_info(&g_gui);

    /* ===== [FALLBACK · 合成器启动前] 直接用 API 像素绘制 =====
     * userman 在登录流程中可能早于合成器 (wm_enabled=0),
     * 此时没有消费者读 gfx_info, 必须直接写 FB 保证界面可见. */
    if (!api->wm_enabled || !api->set_gfx_info) {
        /* --- 1. 矩形 --- */
        for (int i = 0; i < g_gui.rect_count; i++) {
            struct efm_gfx_rect *r = &g_gui.rects[i];
            if (r->w <= 0 || r->h <= 0) continue;
            int x1 = r->x, y1 = r->y;
            int x2 = r->x + r->w - 1;
            int y2 = r->y + r->h - 1;
            if (r->has_border && api->draw_rect)
                api->draw_rect(x1, y1, x2, y2, r->border_color, r->fill_color);
            else if (api->fill_rect)
                api->fill_rect(x1, y1, x2, y2, r->fill_color);
        }
        /* --- 2. 文字 --- */
        for (int i = 0; i < g_gui.text_count; i++) {
            struct efm_gfx_text *t = &g_gui.texts[i];
            if (!t->text[0]) continue;
            int cx = t->x, cy = t->y;
            const unsigned char *s = (const unsigned char *)t->text;
            while (*s) {
                unsigned int cp = 0; int cjk = 0;
                unsigned char c = *s;
                int used = 1;
                if (c < 0x80) { cp = c; used = 1; }
                else if ((c & 0xE0) == 0xC0 && s[1]) {
                    cp = ((unsigned int)(c & 0x1F) << 6) | ((unsigned int)(unsigned char)s[1] & 0x3F);
                    used = 2; cjk = 1;
                } else if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
                    cp = ((unsigned int)(c & 0x0F) << 12) |
                         (((unsigned int)(unsigned char)s[1] & 0x3F) << 6) |
                         ((unsigned int)(unsigned char)s[2] & 0x3F);
                    used = 3; cjk = 1;
                } else { s++; continue; }
                int cell_w = cjk ? (2 * g_fw) : g_fw;
                int cell_h = g_fh;
                if (api->draw_char_unicode)
                    /* bg=COLOR_TRANSPARENT: 透明底叠加, 不盖按钮/面板底色 */
                    api->draw_char_unicode(cx, cy, cp, t->color, COLOR_TRANSPARENT, cell_w, cell_h);
                cx += cell_w;
                s += used;
            }
        }
        if (api->flush_now) api->flush_now();
    }
}

/* ========== 全局变量 (显式初始化, 避免 BSS 未初始化) ========== */
static struct { int x1,y1,x2,y2; char val; } g_btns[MAX_BTN] = {{0,0,0,0,0}};
static int g_btn_n = 0;
static struct efs_dirent g_users[MAX_USERS] = {{{0},0,0}};
static int g_user_count = 0;
static char g_current[32] = "";

/* ========== 文字渲染 (GUI 协议版: 不再直接调用 draw_char_unicode) ========== */
/* g_fw/g_fh/g_vw 已提前到 g_cur_x 旁定义 (gui_flush fallback 需要) */

static void treset(void) { g_cur_x = 0; g_cur_y = 0; }
/* 将文本光标设置到指定行 (用于同步按钮与文字位置) */
static void tsetrow(int row) { g_cur_x = 0; g_cur_y = row * g_fh; }
/* 获取当前光标行号 */
static int tgetrow(void) { return g_cur_y / g_fh; }

/* tflush: 提交 GUI 数据给 compositor (替换原来的 flush_now) */
static void tflush(struct kernel_api *api) {
    gui_flush(api);
}

/* tputc: 追加单个字符到 GUI text 条目 (无直接像素写) */
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

/* tprint_utf8: UTF-8 字符串追加为 GUI text 条目 */
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

/* readline: GUI 协议渲染, 用 gui_add_rect 擦除退格, gui_add_codepoint 画字符 */
static int treadline(struct kernel_api *api, char *buf, int max) {
    int i = 0;
    for (;;) {
        int k = (api->key_poll) ? api->key_poll() : 0;
        if (k == 0) { for (volatile int j = 0; j < 300; j++) __asm__ volatile("pause"); if (api->yield) api->yield(); continue; }
        if (k == LKEY_ESC) {
            tputc(api, '\n'); tflush(api);
            return -1;
        }
        if (k == LKEY_ENTER) {
            tputc(api, '\n'); tflush(api);
            return i;
        }
        if (k == LKEY_BS) {
            if (i > 0) {
                i--;
                g_cur_x -= g_fw; if (g_cur_x < 0) g_cur_x = 0;
                /* 退格擦除: 用 PANEL_BG 覆盖 rect (GUI 模式下追加 PANEL_BG 填充 rect).
                 * 退格擦除的 rect 会在当前 texts[i-1] 后面, 由 compositor 覆盖文字 */
                gui_add_rect(g_cur_x, g_cur_y, g_fw, g_fh, PANEL_BG, 0, 0);
                tflush(api);
            }
            continue;
        }
        if (k > 0 && (char)k >= 0x20 && i < max - 1) {
            buf[i++] = (char)k;
            tputc(api, k);
            tflush(api);
        }
    }
}

/* ========== 前向声明 ========== */
static void _pix(int x, int y, unsigned int c);
static void _rect(int x1,int y1,int x2,int y2,unsigned int c);
static void _box(int x1,int y1,int x2,int y2,unsigned int bo,unsigned int fl);
static void _outline(int x1,int y1,int x2,int y2,unsigned int bo);
static void draw_form_bg(struct kernel_api *api);
static void btn_add(int x1,int y1,int x2,int y2,char val);
static char btn_hit(int mx,int my);
static char choose_key(struct kernel_api *api);
static int s_len(const char *s);
static int s_cmp(const char *a, const char *b);
static char *s_cpy(char *d, const char *s);
static char *u2s(unsigned int v, char *buf);
static void refresh_users(struct kernel_api *api);
static void print_user_list(struct kernel_api *api, int zh);

/* ========== 矩形辅助 (GUI 协议版: 追加到 g_gui.rects[], 由 compositor 统一绘制) ========== */
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
static void _outline(int x1,int y1,int x2,int y2,unsigned int bo) {
    if (x2<x1){int t=x1;x1=x2;x2=t;} if(y2<y1){int t=y1;y1=y2;y2=t;}
    gui_add_rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1, PANEL_BG, bo, 1);
}
/* 绘制桌面表单背景: 桌面底色 + 面板 + 标题栏 + 边框 */
static void draw_form_bg(struct kernel_api *api) {
    int FW = (api->font_w > 0) ? api->font_w : 8;
    int FH = (api->font_h > 0) ? api->font_h : 16;
    int vw = 80 * FW, vh = 25 * FH;
    if (api->get_viewport) api->get_viewport(0, 0, &vw, &vh);
    _rect(0, 0, vw - 1, vh - 1, DESKTOP_BG);
    int px1 = FW, py1 = FH;
    int px2 = vw - FW - 1, py2 = vh - FH - 1;
    /* [修复] rect 槽位只有 EFM_GFX_MAX_RECTS(32) 个: 原实现用 _pix 逐像素画
     * 面板边框, 产生 ~2900 个 1x1 rect, 瞬间耗尽 32 个槽位, 导致后续 5 个
     * 按钮 rect 被 gui_add_rect 静默丢弃 → 窗口内按钮完全不显示.
     * 改为 _box 单 rect (填充+边框合一), 整个背景只用 4 个槽位. */
    _box(px1, py1, px2, py2, PANEL_BORDER, PANEL_BG);
    int title_h = FH + 4;
    _rect(px1, py1, px2, py1 + title_h, TITLE_BAR_BG);
    /* 标题栏覆盖了面板顶边 1px 边框, 补画一条 */
    _rect(px1, py1, px2, py1, PANEL_BORDER);
}
static void btn_add(int x1,int y1,int x2,int y2,char val){
    if(g_btn_n>=MAX_BTN) return;
    g_btns[g_btn_n].x1=x1; g_btns[g_btn_n].y1=y1;
    g_btns[g_btn_n].x2=x2; g_btns[g_btn_n].y2=y2;
    g_btns[g_btn_n].val=val; g_btn_n++;
}
static char btn_hit(int mx,int my){
    /* [修复] 鼠标坐标是屏幕绝对坐标, 先减去内容区偏移再命中测试,
     * 否则窗口模式下按钮永远点不中 (之前只在全屏模式碰巧正确). */
    mx -= g_cx_off; my -= g_cy_off;
    for(int i=0;i<g_btn_n;i++)
        if(mx>=g_btns[i].x1&&mx<=g_btns[i].x2&&my>=g_btns[i].y1&&my<=g_btns[i].y2)
            return g_btns[i].val;
    return 0;
}

/* 通用选择: 返回任意可打印 ASCII 字符 (含字母数字), 0=ESC/Enter/取消
 * 同时支持鼠标左键点击按钮命中 -> 立即返回按钮注册值
 * [鼠标优化] 边缘检测: 只在按钮从松开→按下瞬间触发, 避免按住不放重复触发 */
static char choose_key(struct kernel_api *api) {
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
            if (api->yield) api->yield();
        }
        prev_btn = 0;
    }
    for(;;) {
        struct ev_mouse m;
        int clicked = 0, cx = 0, cy = 0;
        int drain = 0;
        while (api->mouse_poll && api->mouse_poll(&m) && drain < 64) {
            if (!(prev_btn & 1) && (m.btn & 1)) {
                clicked = 1; cx = m.x; cy = m.y;
            }
            prev_btn = m.btn;
            drain++;
        }
        if (clicked) {
            char h = btn_hit(cx, cy);
            if (h) return h;
        }
        int k = (api->key_poll) ? api->key_poll() : 0;
        if(k==0){for(volatile int j=0;j<300;j++) __asm__ volatile("pause"); if(api->yield) api->yield(); continue;}
        if(k==LKEY_ESC)return 0;
        if(k==LKEY_ENTER)return 0;
        if(k>0 && (char)k >= 0x20) return (char)k;
    }
}

/* ========== 字符串辅助 ========== */
static int s_len(const char *s) { int l=0; while(*s++)l++; return l; }
static int s_cmp(const char *a, const char *b) { while(*a && *a==*b){a++;b++;} return *(unsigned char*)a-*(unsigned char*)b; }
static char *s_cpy(char *d, const char *s) { char *r=d; while((*d++=*s++)); return r; }
static char *u2s(unsigned int v, char *buf) {
    char tmp[16]; int i=0;
    if(!v){buf[0]='0';buf[1]=0;return buf;}
    while(v){tmp[i++]='0'+(v%10);v/=10;}
    int j=0; while(i>0) buf[j++]=tmp[--i];
    buf[j]=0; return buf;
}

/* ========== 用户信息操作 ========== */
/* 刷新用户列表和当前用户 */
static void refresh_users(struct kernel_api *api) {
    g_user_count = 0;
    if (api->user_list)
        g_user_count = api->user_list(g_users, MAX_USERS);
    if (g_user_count < 0) g_user_count = 0;
    if (api->get_current_user)
        api->get_current_user(g_current, sizeof(g_current));
}

/* 读取用户 setting.conf 的 lang 字段确定显示语言。
 * 有当前用户 → 读 /users/data/<user>/setting.conf 的 lang=
 * 无当前用户 (首次运行) → 读 /users/sys.conf 的 lang=
 * 读不到则返回 0 (默认英文)。 */
static int load_user_lang(struct kernel_api *api) {
    char path[128];
    if (g_current[0]) build_setting_path(path, sizeof(path), g_current);
    else {
        const char *sysp = "/users/sys.conf";
        int i = 0; while (sysp[i]) { path[i] = sysp[i]; i++; } path[i] = 0;
    }
    char buf[256];
    int r = api->file_read ? api->file_read(path, buf, sizeof(buf)-1) : -1;
    if (r <= 0) return 0;
    buf[r] = 0;
    const char *p = buf;
    while (*p) {
        if (p[0]=='l' && p[1]=='a' && p[2]=='n' && p[3]=='g' && p[4]=='=')
            return (p[5] == '1') ? 1 : 0;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 0;
}

/* 打印用户列表 (仅描边按钮, 点击切换登录) */
static void print_user_list(struct kernel_api *api, int zh) {
    const int FW = (api->font_w > 0) ? api->font_w : 8;
    const int FH = (api->font_h > 0) ? api->font_h : 16;
    int bx1 = 0, bx2 = 70*FW;
    for (int i = 0; i < g_user_count; i++) {
        /* 按钮高度 FH+6, 文字垂直居中 (y1 上移 3px) */
        int y1 = g_cur_y - 3, y2 = g_cur_y + FH + 2;
        /* 当前用户用高亮描边, 其他用普通描边 */
        unsigned int oc = (s_cmp(g_users[i].name, g_current) == 0) ? BTN_CURRENT : BTN_OUTLINE;
        _outline(bx1, y1, bx2, y2, oc);
        btn_add(bx1, y1, bx2, y2, 'A' + i);

        tprint(api, "  [");
        char nbuf[8];
        tprint(api, u2s((unsigned int)(i+1), nbuf));
        tprint(api, "] ");
        tprint(api, g_users[i].name);
        if (s_cmp(g_users[i].name, g_current) == 0) {
            if (zh) tprint_utf8(api, "  <-");
            else    tprint(api, "  <-");
        }
        tprint(api, "\n");
    }
    if (g_user_count == 0) {
        if (zh) tprint_utf8(api, "  (无用户, 请先创建)\n");
        else    tprint(api, "  (no users, create one)\n");
    }
}

/* ========== 主菜单渲染与操作 ========== */
static void userman_main(void) {
    struct kernel_api *api = API;
    if (api->magic != 0xEF110001) return;

    const int FW = (api->font_w > 0) ? api->font_w : 8;
    const int FH = (api->font_h > 0) ? api->font_h : 16;
    /* 初始化透明文字渲染全局参数 */
    g_fw = FW; g_fh = FH;
    g_vw = 80 * FW;
    if (api->get_viewport) { int vx = 0, vy = 0, cw = 0, ch = 0; api->get_viewport(&vx, &vy, &cw, &ch); g_cx_off = vx; g_cy_off = vy; if (cw > 0) g_vw = cw; }

    api->mouse_set_cursor(1);

    for (;;) {
        refresh_users(api);
        /* [新架构] api->clear_screen 仅用于 WM 模式清 tbuf, 不再画像素.
         * 改用 gui_begin() 重置 GUI 数据计数 → 重绘所有 rects + texts. */
        gui_begin();
        treset();
        g_btn_n = 0;
        int zh = (load_user_lang(api) == 1);
        if (api->set_lang) api->set_lang(zh ? 1 : 0);

        /* 绘制桌面表单背景 */
        draw_form_bg(api);

        /* 标题 (在标题栏区域) */
        if (zh) tprint_utf8(api, "  EFMOS 用户管理\n");
        else    tprint(api, "  EFMOS User Manager\n");
        tprint(api, "\n");

        if (zh) {
            tprint_utf8(api, "  当前用户: "); tprint(api, g_current); tprint(api, "\n");
            tprint_utf8(api, "  用户总数: ");
        } else {
            tprint(api, "  Current: "); tprint(api, g_current); tprint(api, "\n");
            tprint(api, "  Count: ");
        }
        char nb[16]; tprint(api, u2s((unsigned int)g_user_count, nb));
        tprint(api, "\n\n");

        /* 用户列表 */
        if (zh) tprint_utf8(api, "  用户列表 (点击切换登录)\n");
        else    tprint(api, "  User list (click to switch)\n");
        print_user_list(api, zh);
        tprint(api, "\n");

        /* 底部按钮区 — 按钮高 2*FH-2 (≈30px), 文字水平+垂直居中.
         * 左列: 1/3   右列: 2/4   底部全宽: 0.
         * [修复] 按钮宽度根据实际视口宽度 g_vw 自适应, 不再硬编码 32*FW.
         * 原来固定 32*FW=384, 右列按钮右边=803px, 但窗口内容区可能只有 740px,
         * 导致右列按钮溢出被裁剪/错位.
         * 按钮宽度基于面板宽度 (px2 - px1 + 1), 与 draw_form_bg 的面板对齐. */
        int btn_h = 2 * FH - 2;  /* 按钮高度 (约2行) */
        int gap_x = 2 * FW;
        int left_x1 = 1 * FW;  /* = draw_form_bg 的 px1 = FW */
        int panel_x2 = g_vw - FW - 1;  /* = draw_form_bg 的 px2 */
        int avail_w = panel_x2 - left_x1 + 1;  /* 面板内部可用宽度 */
        int const_btn_w = (avail_w - gap_x) / 2;
        if (const_btn_w < 10 * FW) const_btn_w = 10 * FW;  /* 最小宽度 */
        int right_x1 = left_x1 + const_btn_w + gap_x;
        int right_x2 = right_x1 + const_btn_w - 1;
        int full_x2 = right_x2;
        int cur_row_y = g_cur_y;

        /* Row 1: 左=1 切换语言, 右=2 创建用户 */
        _outline(left_x1, cur_row_y, left_x1 + const_btn_w - 1, cur_row_y + btn_h - 1, BTN_OUTLINE);
        btn_add(left_x1, cur_row_y, left_x1 + const_btn_w - 1, cur_row_y + btn_h - 1, '1');
        _outline(right_x1, cur_row_y, right_x2, cur_row_y + btn_h - 1, BTN_OUTLINE);
        btn_add(right_x1, cur_row_y, right_x2, cur_row_y + btn_h - 1, '2');
        {
            int py = g_cur_y;
            const char *lt = zh ? "1. 切换语言" : "1. Language";
            const char *rt = zh ? "2. 创建用户" : "2. Create user";
            g_cur_y = cur_row_y + (btn_h - FH) / 2;
            g_cur_x = left_x1 + (const_btn_w - text_w_px(lt, FW)) / 2;
            if (zh) tprint_utf8(api, lt); else tprint(api, lt);
            g_cur_x = right_x1 + (const_btn_w - text_w_px(rt, FW)) / 2;
            if (zh) tprint_utf8(api, rt); else tprint(api, rt);
            g_cur_y = py;
        }
        g_cur_y += btn_h + 2;
        cur_row_y = g_cur_y;

        /* Row 2: 左=3 删除用户, 右=4 修改密码 */
        _outline(left_x1, cur_row_y, left_x1 + const_btn_w - 1, cur_row_y + btn_h - 1, BTN_OUTLINE);
        btn_add(left_x1, cur_row_y, left_x1 + const_btn_w - 1, cur_row_y + btn_h - 1, '3');
        _outline(right_x1, cur_row_y, right_x2, cur_row_y + btn_h - 1, BTN_OUTLINE);
        btn_add(right_x1, cur_row_y, right_x2, cur_row_y + btn_h - 1, '4');
        {
            int py = g_cur_y;
            const char *lt = zh ? "3. 删除用户" : "3. Delete user";
            const char *rt = zh ? "4. 修改密码" : "4. Change pwd";
            g_cur_y = cur_row_y + (btn_h - FH) / 2;
            g_cur_x = left_x1 + (const_btn_w - text_w_px(lt, FW)) / 2;
            if (zh) tprint_utf8(api, lt); else tprint(api, lt);
            g_cur_x = right_x1 + (const_btn_w - text_w_px(rt, FW)) / 2;
            if (zh) tprint_utf8(api, rt); else tprint(api, rt);
            g_cur_y = py;
        }
        g_cur_y += btn_h + 2;
        cur_row_y = g_cur_y;

        /* Row 3: 全宽=0 退出 (居中) */
        _outline(0, cur_row_y, full_x2, cur_row_y + btn_h - 1, BTN_OUTLINE);
        btn_add(0, cur_row_y, full_x2, cur_row_y + btn_h - 1, '0');
        {
            int py = g_cur_y;
            const char *t = zh ? "0. 退出到登录" : "0. Exit to login";
            int full_w = full_x2 + 1;
            g_cur_y = cur_row_y + (btn_h - FH) / 2;
            g_cur_x = (full_w - text_w_px(t, FW)) / 2;
            if (zh) tprint_utf8(api, t); else tprint(api, t);
            g_cur_y = py;
        }
        g_cur_y += btn_h + 2;

        tprint(api, "\n");
        if (zh) {
            tprint_utf8(api, "  点击用户名或按 A-");
            if (g_user_count <= 26) {
                char last[4]; last[0] = 'A' + ((g_user_count > 0) ? (g_user_count-1) : 0); last[1] = 0;
                tprint(api, last);
            }
            tprint_utf8(api, " 登录\n  ESC 取消\n\n  选择: ");
        } else {
            tprint(api, "  Click user or press A-");
            if (g_user_count <= 26) {
                char last[4]; last[0] = 'A' + ((g_user_count > 0) ? (g_user_count-1) : 0); last[1] = 0;
                tprint(api, last);
            }
            tprint(api, " to login\n  ESC to cancel\n\n  Choose: ");
        }

        tflush(api);
        char ch = choose_key(api);
        if (ch == 0) continue;

        /* ---------- 处理用户登录 (点击用户列表: 'A'..'Z' / 'a'..'z') ---------- */
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            int idx = ((ch >= 'A' && ch <= 'Z') ? (ch - 'A') : (ch - 'a'));
            if (idx < g_user_count) {
                const char *uname = g_users[idx].name;
                if (s_cmp(uname, g_current) == 0) {
                    if (zh) tprint_utf8(api, "\n已是当前用户!\n");
                    else    tprint(api, "\nAlready current user!\n");
                    tflush(api);
                } else {
                    /* 密码验证: 若该用户设置了密码则需输入 */
                    int pw_ok = 1;
                    if (user_has_password(uname)) {
                        if (zh) tprint_utf8(api, "请输入密码: ");
                        else    tprint(api, "Enter password: ");
                        tflush(api);
                        char pw[64];
                        int pr = treadline(api, pw, sizeof(pw));
                        if (pr <= 0) {
                            if (zh) tprint_utf8(api, "已取消登录。\n");
                            else    tprint(api, "Login cancelled.\n");
                            tflush(api);
                            pw_ok = -1;
                        } else {
                            pw[pr] = 0;
                            if (!verify_password(uname, pw)) {
                                if (zh) tprint_utf8(api, "密码错误!\n");
                                else    tprint(api, "Wrong password!\n");
                                tflush(api);
                                pw_ok = 0;
                            }
                        }
                    }
                    if (pw_ok == 1) {
                        int r = api->set_current_user ? api->set_current_user(uname) : -1;
                        if (r == 0) {
                            if (zh) {
                                tprint_utf8(api, "\n登录成功! 欢迎 "); tprint(api, uname);
                                tprint_utf8(api, "!\n正在进入 Shell...\n");
                            } else {
                                tprint(api, "\nLogin success! Welcome "); tprint(api, uname);
                                tprint(api, "!\nEntering Shell...\n");
                            }
                            tflush(api);
                            for (volatile int i = 0; i < 4000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                            goto EXIT_TO_SHELL;
                        } else {
                            if (zh) tprint_utf8(api, "\n登录失败! (用户目录不存在)\n");
                            else    tprint(api, "\nLogin failed! (user directory missing)\n");
                            tflush(api);
                        }
                    }
                }
                for (volatile int i = 0; i < 4000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                continue;
            }
        }

        /* ---------- 0 退出 (必须已登录才能进入 Shell) ---------- */
        if (ch == '0' || ch == 'q' || ch == 'Q') {
            refresh_users(api);
            if (!g_current[0]) {
                tprint(api, "\n");
                if (zh) tprint_utf8(api, "错误: 尚未登录! 请先点击用户列表输入密码登录。\n");
                else    tprint(api, "ERROR: Not logged in! Click a user in list and enter password first.\n");
                tflush(api);
                for (volatile int i = 0; i < 5000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                continue;
            }
            break;
        }

        /* ---------- 1 切换语言 ----------
         * 已登录 → 修改当前用户的 setting.conf 的 lang= 字段
         * 未登录 (首次运行无用户) → 修改系统文件夹 /users/sys.conf 的 lang= 字段 */
        if (ch == '1') {
            if (api->get_lang && api->set_lang) {
                int newlang = (api->get_lang() == 1) ? 0 : 1;
                api->set_lang(newlang);
                /* 选择目标配置文件: 有当前用户用 setting.conf, 否则用 sys.conf */
                char spath[128];
                if (g_current[0]) build_setting_path(spath, sizeof(spath), g_current);
                else {
                    const char *sysp = "/users/sys.conf";
                    int i = 0; while (sysp[i]) { spath[i] = sysp[i]; i++; } spath[i] = 0;
                }
                char cfg[512]; int clen = 0;
                cfg[0] = 0;
                if (api->file_read) {
                    clen = api->file_read(spath, cfg, sizeof(cfg)-1);
                    if (clen < 0) clen = 0;
                    cfg[clen] = 0;
                }
                char newcfg[768]; int np = 0;
                int replaced = 0;
                const char *p = cfg;
                while (*p) {
                    const char *nl = p;
                    while (*nl && *nl != '\n') nl++;
                    int linelen = (int)(nl - p);
                    if (linelen >= 5 && p[0]=='l'&&p[1]=='a'&&p[2]=='n'&&p[3]=='g'&&p[4]=='=') {
                        newcfg[np++] = 'l'; newcfg[np++] = 'a'; newcfg[np++] = 'n';
                        newcfg[np++] = 'g'; newcfg[np++] = '=';
                        newcfg[np++] = newlang ? '1' : '0';
                        newcfg[np++] = '\n';
                        replaced = 1;
                    } else {
                        for (int i = 0; i < linelen; i++) newcfg[np++] = p[i];
                        if (*nl == '\n') newcfg[np++] = '\n';
                    }
                    p = nl; if (*p == '\n') p++;
                }
                if (!replaced) {
                    newcfg[np++] = 'l'; newcfg[np++] = 'a'; newcfg[np++] = 'n';
                    newcfg[np++] = 'g'; newcfg[np++] = '=';
                    newcfg[np++] = newlang ? '1' : '0';
                    newcfg[np++] = '\n';
                }
                if (api->file_write) api->file_write(spath, newcfg, np);
                if (api->save_settings) api->save_settings();
            }
            continue;
        }

        /* ---------- 2 创建用户 ---------- */
        if (ch == '2') {
            tprint(api, "\n");
            if (zh) tprint_utf8(api, "--- 创建新用户 ---\n请输入新用户名 (字母/数字/下划线, 28字符内): ");
            else    tprint(api, "--- Create New User ---\nEnter username (a-z/A-Z/0-9/_, <=28 chars): ");
            tflush(api);
            char username[64];
            int r = treadline(api, username, sizeof(username));
            if (r <= 0) {
                if (zh) tprint_utf8(api, "已取消。\n"); else tprint(api, "Cancelled.\n");
                tflush(api);
                for (volatile int i = 0; i < 3000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                continue;
            }
            int valid = 1;
            if (r < 1 || r > 28) valid = 0;
            else for (int i = 0; i < r; i++) {
                char c = username[i];
                if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) { valid = 0; break; }
            }
            if (!valid) {
                if (zh) tprint_utf8(api, "用户名无效! 仅允许字母/数字/下划线, 1-28字符。\n");
                else    tprint(api, "Invalid username! Only a-z/A-Z/0-9/_ allowed, 1-28 chars.\n");
                tflush(api);
                for (volatile int i = 0; i < 4000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                continue;
            }
            /* 确保字符串终止 */
            username[r] = 0;
            int rc = api->user_create ? api->user_create(username) : -1;
            if (rc == 0) {
                if (zh) {
                    tprint_utf8(api, "成功! 用户已创建: "); tprint(api, username);
                    tprint_utf8(api, " (目录: /users/"); tprint(api, username); tprint_utf8(api, ")\n");
                } else {
                    tprint(api, "Success! User created: "); tprint(api, username);
                    tprint(api, " (path: /users/"); tprint(api, username); tprint(api, ")\n");
                }
                tflush(api);
                /* 设置密码 (强制要求, 需确认; 取消则回滚删除刚创建的用户) */
                int pw_done = 0;
                while (!pw_done) {
                    if (zh) tprint_utf8(api, "请设置登录密码 (必填, 1-31字符): ");
                    else    tprint(api, "Set login password (required, 1-31 chars): ");
                    tflush(api);
                    char pw[64];
                    int pr = treadline(api, pw, sizeof(pw));
                    if (pr < 0) {  /* ESC 取消 */
                        if (zh) tprint_utf8(api, "已取消, 删除未设密码的新用户。\n");
                        else    tprint(api, "Cancelled, removing user without password.\n");
                        tflush(api);
                        if (api->user_delete) api->user_delete(username);
                        pw_done = -1; break;
                    }
                    if (pr == 0 || pr > 31) {
                        if (zh) tprint_utf8(api, "密码不能为空且不超过31字符!\n");
                        else    tprint(api, "Password cannot be empty or exceed 31 chars!\n");
                        tflush(api);
                        continue;
                    }
                    pw[pr] = 0;
                    if (zh) tprint_utf8(api, "请再次输入密码以确认: ");
                    else    tprint(api, "Re-enter password to confirm: ");
                    tflush(api);
                    char conf[64];
                    int cr = treadline(api, conf, sizeof(conf));
                    if (cr < 0) {
                        if (zh) tprint_utf8(api, "已取消, 删除未设密码的新用户。\n");
                        else    tprint(api, "Cancelled, removing user without password.\n");
                        tflush(api);
                        if (api->user_delete) api->user_delete(username);
                        pw_done = -1; break;
                    }
                    conf[cr] = 0;
                    int match = (cr == pr);
                    for (int i = 0; match && i < pr; i++) if (conf[i] != pw[i]) match = 0;
                    if (!match) {
                        if (zh) tprint_utf8(api, "两次输入不一致, 请重新设置!\n");
                        else    tprint(api, "Passwords don't match, please retry!\n");
                        tflush(api);
                        continue;
                    }
                    save_password(username, pw);
                    if (zh) tprint_utf8(api, "密码已设置, 用户创建完成。\n");
                    else    tprint(api, "Password set, user creation complete.\n");
                    tflush(api);
                    pw_done = 1;
                    /* 新建用户成功 → 自动登录并进入 Shell */
                    int ar = api->set_current_user ? api->set_current_user(username) : -1;
                    if (ar == 0) {
                        if (zh) {
                            tprint_utf8(api, "自动登录成功! 欢迎 "); tprint(api, username);
                            tprint_utf8(api, "!\n正在进入 Shell...\n");
                        } else {
                            tprint(api, "Auto-login success! Welcome "); tprint(api, username);
                            tprint(api, "!\nEntering Shell...\n");
                        }
                        tflush(api);
                        for (volatile int i = 0; i < 4000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                        goto EXIT_TO_SHELL;
                    } else {
                        if (zh) tprint_utf8(api, "自动登录失败, 请手动选择用户登录。\n");
                        else    tprint(api, "Auto-login failed, please login manually.\n");
                        tflush(api);
                    }
                }
            } else {
                if (zh) tprint_utf8(api, "失败! 用户已存在或无法创建目录。\n");
                else    tprint(api, "Failed! User already exists or cannot create dir.\n");
                tflush(api);
            }
            for (volatile int i = 0; i < 4000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
            continue;
        }

        /* ---------- 3 删除用户 ---------- */
        if (ch == '3') {
            tprint(api, "\n");
            if (zh) tprint_utf8(api, "--- 删除用户 ---\n请输入要删除的用户名 (不能是当前用户, 目录必须为空): ");
            else    tprint(api, "--- Delete User ---\nEnter username to delete (not current, dir must be empty): ");
            tflush(api);
            char username[64];
            int r = treadline(api, username, sizeof(username));
            if (r <= 0) {
                if (zh) tprint_utf8(api, "已取消。\n"); else tprint(api, "Cancelled.\n");
                tflush(api);
                for (volatile int i = 0; i < 3000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                continue;
            }
            if (zh) {
                tprint_utf8(api, "确认删除用户 '"); tprint(api, username);
                tprint_utf8(api, "'? (Y/N): ");
            } else {
                tprint(api, "Confirm delete user '"); tprint(api, username);
                tprint(api, "'? (Y/N): ");
            }
            tflush(api);
            char confirm[16];
            treadline(api, confirm, sizeof(confirm));
            if (!(confirm[0] == 'Y' || confirm[0] == 'y')) {
                if (zh) tprint_utf8(api, "已取消删除。\n"); else tprint(api, "Delete cancelled.\n");
                tflush(api);
                for (volatile int i = 0; i < 3000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                continue;
            }
            int rc = api->user_delete ? api->user_delete(username) : -1;
            if (rc == 0) {
                if (zh) { tprint_utf8(api, "成功! 用户 '"); tprint(api, username); tprint_utf8(api, "' 已删除。\n"); }
                else    { tprint(api, "Success! User '"); tprint(api, username); tprint(api, "' deleted.\n"); }
                tflush(api);
            } else {
                if (zh) tprint_utf8(api, "失败! 可能原因: 用户不存在 / 是当前登录用户 / 目录非空。\n");
                else    tprint(api, "Failed! Possible: not exist / current user / dir not empty.\n");
                tflush(api);
            }
            for (volatile int i = 0; i < 5000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
            continue;
        }

        /* ---------- 4 修改密码 (当前用户) ---------- */
        if (ch == '4') {
            tprint(api, "\n");
            /* 必须先登录才能改密码 */
            if (!g_current[0]) {
                if (zh) tprint_utf8(api, "错误: 尚未登录, 请先登录后再修改密码。\n");
                else    tprint(api, "ERROR: Must be logged in to change password.\n");
                tflush(api);
                for (volatile int i = 0; i < 4000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                continue;
            }
            if (zh) tprint_utf8(api, "--- 修改密码 ---\n");
            else    tprint(api, "--- Change Password ---\n");
            /* 若已有密码, 先验证旧密码 */
            if (user_has_password(g_current)) {
                if (zh) tprint_utf8(api, "请输入旧密码: ");
                else    tprint(api, "Enter old password: ");
                tflush(api);
                char oldpw[64];
                int or = treadline(api, oldpw, sizeof(oldpw));
                if (or <= 0) {
                    if (zh) tprint_utf8(api, "已取消。\n"); else tprint(api, "Cancelled.\n");
                    tflush(api);
                    for (volatile int i = 0; i < 3000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                    continue;
                }
                oldpw[or] = 0;
                if (!verify_password(g_current, oldpw)) {
                    if (zh) tprint_utf8(api, "旧密码错误!\n");
                    else    tprint(api, "Wrong old password!\n");
                    tflush(api);
                    for (volatile int i = 0; i < 3000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                    continue;
                }
            }
            /* 输入新密码 (强制非空, 需确认) */
            {
                int changed = 0;
                while (!changed) {
                    if (zh) tprint_utf8(api, "请输入新密码 (1-31字符, 必填): ");
                    else    tprint(api, "Enter new password (required, 1-31 chars): ");
                    tflush(api);
                    char newpw[64];
                    int nr = treadline(api, newpw, sizeof(newpw));
                    if (nr < 0) {
                        if (zh) tprint_utf8(api, "已取消。\n");
                        else    tprint(api, "Cancelled.\n");
                        tflush(api);
                        break;
                    }
                    if (nr == 0 || nr > 31) {
                        if (zh) tprint_utf8(api, "密码不能为空且不超过31字符!\n");
                        else    tprint(api, "Password cannot be empty or exceed 31 chars!\n");
                        tflush(api);
                        continue;
                    }
                    newpw[nr] = 0;
                    if (zh) tprint_utf8(api, "请再次输入新密码: ");
                    else    tprint(api, "Re-enter new password: ");
                    tflush(api);
                    char conf[64];
                    int cr = treadline(api, conf, sizeof(conf));
                    if (cr < 0) {
                        if (zh) tprint_utf8(api, "已取消。\n");
                        else    tprint(api, "Cancelled.\n");
                        tflush(api);
                        break;
                    }
                    conf[cr] = 0;
                    int match = (cr == nr);
                    for (int i = 0; match && i < nr; i++) {
                        if (conf[i] != newpw[i]) match = 0;
                    }
                    if (!match) {
                        if (zh) tprint_utf8(api, "两次输入不一致! 密码未修改。\n");
                        else    tprint(api, "Passwords don't match! Not changed.\n");
                        tflush(api);
                        continue;
                    }
                    save_password(g_current, newpw);
                    if (zh) tprint_utf8(api, "密码已修改。\n");
                    else    tprint(api, "Password changed.\n");
                    tflush(api);
                    changed = 1;
                }
            }
            for (volatile int i = 0; i < 3000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
            continue;
        }
    }

EXIT_TO_SHELL:
    api->clear_screen();
}
