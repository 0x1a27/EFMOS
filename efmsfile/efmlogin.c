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

/* efmlogin.c - EFMOS 登录程序 (编译为 efmlogin.efs)
 * 加载地址: 0x600000 (6MB, 不与其他程序冲突)
 * 入口: _start (二进制首字节, 必须为 .text 第一个函数)
 *
 * 功能 (仅登录, 不允许用户管理):
 *   - 列出已有账户 (/users 下的子目录, 由内核 user_list 返回)
 *   - 点击/按键选择用户, 输入密码验证后登录
 *   - 登录成功 → 退出回到内核, 内核进入 Shell
 *   - 中英文双语 (读取系统配置 /users/sys.conf, 内核启动时已加载到 efm_lang)
 *   - 语言切换会写入 /users/sys.conf 持久化 (供下次开机登录界面使用)
 *   - 鼠标点击选项
 * [安全] 本程序仅由内核开机时调用, Shell 中输入 efmlogin 会被内核拒绝 */

/* ========== 内核 API 表 (必须与内核 struct kernel_api 字段顺序/对齐完全一致) ========== */
#include "efmos/efm_api.h"
struct ev_mouse { int dx, dy; unsigned char btn; int x, y; };

/* ========== 入口点 (必须为 .text 第一个函数!) ==========
 * 内核 run_efs 把 .efs 加载到 load_addr 后, 直接跳转到 load_addr 执行,
 * 即二进制首字节 = 入口。所以 _start 必须排在所有函数之前,
 * 否则 GCC 按源码顺序把其它 static 函数排到 .text 开头 → 内核跳进错误函数。 */
static void efmlogin_main(void);
__attribute__((naked))
void _start(void) {
    __asm__ volatile(
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"
        "call efmlogin_main\n\t"
        "leave\n\t"
        "ret\n\t"
    );
}

/* ========== 密码哈希 (DJB2 变体 + 用户名盐, 与 userman 完全一致) ========== */
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

/* 检查用户是否设置了密码: 返回 1=有密码, 0=无密码 */
static int user_has_password(const char *username) {
    char path[128]; build_passwd_path(path, sizeof(path), username);
    char buf[32];
    int r = API->file_read ? API->file_read(path, buf, sizeof(buf)-1) : -1;
    if (r <= 0) return 0;
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

/* ========== 帧缓冲 & 按钮辅助常量 ==========
 * 必须与内核 struct gop_fb 完全一致:
 *   struct gop_fb { unsigned long long fb_base; unsigned int hr, vr, ppsl; }; */
struct _fb {
    unsigned long long fb_base;
    unsigned int hr, vr, ppsl;
};
#define FB ((volatile struct _fb*)0x1000)
/* 桌面表单风格颜色 */
#define DESKTOP_BG   0x1A1A2E   /* 桌面背景 (深蓝黑) */
#define PANEL_BG     0x2B2B3C   /* 表单面板背景 */
#define PANEL_BORDER 0x6E6E8E   /* 表单边框 */
#define TITLE_BAR_BG 0x3D3D5C   /* 标题栏背景 */
#define BTN_OUTLINE  0x9AB6D8   /* 按钮描边色 */
#define BTN_HOVER    0xE0E0FF   /* 按钮高亮描边 (暂未使用) */
#define COLOR_TRANSPARENT 0xFEEDFACEu  /* 透明背景标记 (draw_char_unicode) */
#define TEXT_FG           0xFFFFFFu    /* 文字前景色 (白) */
#define MAX_BTN 48
#define MAX_USERS 32
#define LKEY_ENTER -101
#define LKEY_BS    -102
#define LKEY_ESC   -103

/* ========== [新架构] EFS GUI 渲染协议 ==========
 * 不再直接写像素到 back buffer, 而是构造渲染信息 (rects + texts),
 * 由 compositor 统一从 set_gfx_info 提交的指针读取并绘制.
 * 协议完全匹配 kernel.c 和 efmcompositor.c 的定义. */
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
/* 单例 GUI 信息对象 (进程地址空间与 compositor 共享)
 * [必须显式初始化] EFS 用 objcopy -j .text/.rodata/.data 打包, BSS 段不被复制进 .efs,
 * 省略 ={...} 会进入 BSS → 运行时内存是乱码 → compositor 读不到 magic/seq/rects. */
static struct efm_gfx_info g_gui = {0,0,0,{{0,0,0,0,0,0,0}},0,{{0,0,0,""}}};
/* 文本光标 (像素) + 字体尺寸 + 视口宽 (放在 gui_* 函数之前, 因为 gui_add_codepoint 直接用 g_cur_x/g_cur_y) */
static int g_cur_x = 0, g_cur_y = 0;
/* 登录前 fallback 也需要 TTF 真实尺寸 12x22, 否则第一帧按钮/密码框文字按 8x16 排版
 * 与 TTF 实际 cell 尺寸不一致, 出现文字与色块错位。 */
static int g_fw = 14, g_fh = 26, g_vw = 1120;  /* 1120 = 80*14 ASCII 列 */
/* 辅助: 向 g_gui.rects[] 追加 */
static void gui_add_rect(int x, int y, int w, int h,
                         unsigned int fill, unsigned int border, int has_border) {
    if (g_gui.rect_count >= EFM_GFX_MAX_RECTS) return;
    struct efm_gfx_rect *r = &g_gui.rects[g_gui.rect_count++];
    r->x = x; r->y = y; r->w = w; r->h = h;
    r->fill_color = fill; r->border_color = border; r->has_border = has_border;
}
/* 辅助: 向 g_gui.texts[] 追加 (UTF-8, 最大 95 字节) */
static void gui_add_text(int x, int y, unsigned int color, const char *utf8) {
    if (g_gui.text_count >= EFM_GFX_MAX_TEXTS) return;
    struct efm_gfx_text *t = &g_gui.texts[g_gui.text_count++];
    t->x = x; t->y = y; t->color = color;
    int n = 0;
    while (utf8[n] && n < 95) { t->text[n] = utf8[n]; n++; }
    t->text[n] = 0;
}
/* 追加单个 codepoint 到 texts[]. 为了减少条目数, 尽量把 codepoint 拼接到最后一个 text 条目. */
static void gui_add_codepoint(int x, int y, unsigned int color, unsigned int cp, int is_cjk) {
    (void)x; (void)y; /* x/y 由 g_cur_x 管理, 此处忽略 */
    /* 转换 codepoint → UTF-8 字节 */
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
    /* 新建 text 条目: x/y 使用当前光标位置 g_cur_x/g_cur_y 外部已设置 */
    if (g_gui.text_count < EFM_GFX_MAX_TEXTS) {
        struct efm_gfx_text *t = &g_gui.texts[g_gui.text_count++];
        t->x = g_cur_x;
        t->y = g_cur_y;
        t->color = color;
        for (int i = 0; i <= bl; i++) t->text[i] = buf[i];
    }
}
/* 新帧开始: 重置计数 (不重置 magic/seq, 在 flush 时更新) */
static void gui_begin(void) {
    g_gui.rect_count = 0;
    g_gui.text_count = 0;
}
/* 帧提交: 写 magic, seq++, 通知 compositor */
/* [FALLBACK 辅助] UTF-8 → Unicode codepoint, 用于直接 draw_char_unicode 渲染 */
static int fb_utf8_to_cp(const unsigned char **ps, unsigned int *out_cp, int *out_cjk) {
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
     * efmlogin 在合成器启动前运行 (wm_enabled=0), 没有消费者读 gfx_info,
     * 若没有 fallback 会完全空白! 直接用 API 把 rects/texts 写进 FB. */
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
        /* --- 2. 文字 (UTF-8 → draw_char_unicode) --- */
        for (int i = 0; i < g_gui.text_count; i++) {
            struct efm_gfx_text *t = &g_gui.texts[i];
            if (!t->text[0]) continue;
            int cx = t->x, cy = t->y;
            const unsigned char *s = (const unsigned char *)t->text;
            while (*s) {
                unsigned int cp = 0; int cjk = 0;
                if (!fb_utf8_to_cp(&s, &cp, &cjk)) continue;
                int cell_w = cjk ? (2 * g_fw) : g_fw;
                int cell_h = g_fh;
                if (api->draw_char_unicode)
                    /* bg=COLOR_TRANSPARENT: 不覆盖按钮背景色, 避免文字下方黑方块 */
                    api->draw_char_unicode(cx, cy, cp, t->color, COLOR_TRANSPARENT, cell_w, cell_h);
                cx += cell_w;
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

/* ========== [新架构] 渲染抽象层 ==========
 * _pix/_rect/_box/_outline → 不再直接写像素, 而是追加到 g_gui.rects[]
 * tputc/tprint → 不再直接画字, 而是追加到 g_gui.texts[]
 * 所有坐标都是窗口内容区相对坐标 (0,0 = 内容区左上角), compositor 统一加偏移. */

static void treset(void) { g_cur_x = 0; g_cur_y = 0; }

static void tsetrow(int row) { g_cur_x = 0; g_cur_y = row * g_fh; }
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

/* [新架构] readline: 用 GUI 协议渲染输入字符, 退格时用 PANEL_BG 重绘.
 * 每次字符变化都需要 gui_begin → 重建整个帧 → gui_flush (通过重新进入主循环或重新绘制).
 * 为简化, 每次输入后用 rects/texts 增量更新并 flush. */
static int treadline(struct kernel_api *api, char *buf, int max) {
    int i = 0;
    for (;;) {
        int k = (api->key_poll) ? api->key_poll() : 0;
        if (k == 0) { for (volatile int j = 0; j < 300; j++) __asm__ volatile("pause"); if (api->yield) api->yield(); continue; }
        if (k == LKEY_ESC) { tputc(api, '\n'); tflush(api); return -1; }
        if (k == LKEY_ENTER) { tputc(api, '\n'); tflush(api); return i; }
        if (k == LKEY_BS) {
            if (i > 0) {
                i--;
                g_cur_x -= g_fw; if (g_cur_x < 0) g_cur_x = 0;
                /* 退格擦除: 用 PANEL_BG 覆盖 (GUI 模式下退格需要外部调用方重建整个帧).
                 * treadline 内无法知道完整布局, 因此简单地追加一个 PANEL_BG 填充 rect.
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
static char *u2s(unsigned int v, char *buf);
static void refresh_users(struct kernel_api *api);
static void print_user_list(struct kernel_api *api, int zh);
static void save_sys_lang(struct kernel_api *api, int lang);

/* ========== 图形辅助实现 (GUI 协议版, 不再直接写像素) ==========
 * _pix/_rect/_box/_outline → 追加到 g_gui.rects[], 由 compositor 统一绘制.
 * 所有坐标都是 (x1,y1,x2,y2) 闭区间, 需要转换为 (x,y,w,h). */
static void _pix(int x, int y, unsigned int c) {
    /* 单点: w=1, h=1, 无透明边框 */
    gui_add_rect(x, y, 1, 1, c, 0, 0);
}
static void _rect(int x1,int y1,int x2,int y2,unsigned int c) {
    if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
    gui_add_rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1, c, 0, 0);
}
static void _box(int x1,int y1,int x2,int y2,unsigned int bo,unsigned int fl) {
    if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
    /* fill_rect 主体 + 2px 边框 */
    gui_add_rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1, fl, bo, 1);
}
static void _outline(int x1,int y1,int x2,int y2,unsigned int bo) {
    if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
    /* 透明填充 + 边框 (has_border=1, fill_color=PANEL_BG) */
    gui_add_rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1, PANEL_BG, bo, 1);
}
static void btn_add(int x1,int y1,int x2,int y2,char val){
    if(g_btn_n>=MAX_BTN) return;
    g_btns[g_btn_n].x1=x1; g_btns[g_btn_n].y1=y1;
    g_btns[g_btn_n].x2=x2; g_btns[g_btn_n].y2=y2;
    g_btns[g_btn_n].val=val; g_btn_n++;
}
static char btn_hit(int mx,int my){
    for(int i=0;i<g_btn_n;i++)
        if(mx>=g_btns[i].x1&&mx<=g_btns[i].x2&&my>=g_btns[i].y1&&my<=g_btns[i].y2)
            return g_btns[i].val;
    return 0;
}

/* 通用选择: 返回可打印 ASCII 字符, 0=ESC/Enter/取消
 * 同时支持鼠标左键点击按钮命中 */
static char choose_key(struct kernel_api *api) {
    api->mouse_set_cursor(1);
    static unsigned char prev_btn = 0;
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
static char *u2s(unsigned int v, char *buf) {
    char tmp[16]; int i=0;
    if(!v){buf[0]='0';buf[1]=0;return buf;}
    while(v){tmp[i++]='0'+(v%10);v/=10;}
    int j=0; while(i>0) buf[j++]=tmp[--i];
    buf[j]=0; return buf;
}

/* ========== 用户信息操作 ========== */
static void refresh_users(struct kernel_api *api) {
    g_user_count = 0;
    if (api->user_list)
        g_user_count = api->user_list(g_users, MAX_USERS);
    if (g_user_count < 0) g_user_count = 0;
}

/* 绘制桌面表单背景: 桌面底色 + 面板 + 标题栏 */
static void draw_form_bg(struct kernel_api *api) {
    int FW = (api->font_w > 0) ? api->font_w : 8;
    int FH = (api->font_h > 0) ? api->font_h : 16;
    int vw = 80 * FW, vh = 25 * FH;
    if (api->get_viewport) api->get_viewport(0, 0, &vw, &vh);

    /* 桌面背景 */
    _rect(0, 0, vw - 1, vh - 1, DESKTOP_BG);

    /* 面板 (留 1 格边距)
     * [修复] 原实现用 _pix 逐像素画边框, 产生 ~2900 个 1x1 rect,
     * 瞬间耗尽 EFM_GFX_MAX_RECTS(32) 个槽位, 后续按钮 rect 全部被丢弃.
     * 改为 _box 单 rect (填充+边框合一). */
    int px1 = FW, py1 = FH;
    int px2 = vw - FW - 1, py2 = vh - FH - 1;
    _box(px1, py1, px2, py2, PANEL_BORDER, PANEL_BG);

    /* 标题栏 */
    int title_h = FH + 4;
    _rect(px1, py1, px2, py1 + title_h, TITLE_BAR_BG);

    /* 标题栏覆盖了面板顶边 1px 边框, 补画一条 */
    _rect(px1, py1, px2, py1, PANEL_BORDER);
}

/* 打印用户列表 (仅描边按钮, 点击登录)
 * [对齐修复] 按钮与文字同起点 (x=0), 按钮覆盖整行文字, 垂直用 g_cur_y 同步. */
static void print_user_list(struct kernel_api *api, int zh) {
    const int FW = (api->font_w > 0) ? api->font_w : 8;
    const int FH = (api->font_h > 0) ? api->font_h : 16;
    int bx1 = 0, bx2 = 70*FW;
    for (int i = 0; i < g_user_count; i++) {
        /* 按钮高度 FH+6, 文字垂直居中 (y1 上移 3px) */
        int y1 = g_cur_y - 3, y2 = g_cur_y + FH + 2;
        _outline(bx1, y1, bx2, y2, BTN_OUTLINE);
        btn_add(bx1, y1, bx2, y2, 'A' + i);

        tprint(api, "  [");
        char nbuf[8];
        tprint(api, u2s((unsigned int)(i+1), nbuf));
        tprint(api, "] ");
        tprint(api, g_users[i].name);
        tputc(api, '\n');
    }
    if (g_user_count == 0) {
        if (zh) tprint_utf8(api, "  (无账户, 请联系管理员或重启进入用户管理)\n");
        else    tprint(api, "  (No accounts, contact admin or reboot)\n");
    }
}

/* 保存系统语言到 /users/sys.conf (供下次开机登录界面使用) */
static void save_sys_lang(struct kernel_api *api, int lang) {
    char cfg[16];
    cfg[0]='l'; cfg[1]='a'; cfg[2]='n'; cfg[3]='g'; cfg[4]='=';
    cfg[5] = lang ? '1' : '0';
    cfg[6]='\n'; cfg[7]=0;
    if (api->file_write) api->file_write("/users/sys.conf", cfg, 7);
}

/* 读取 /users/sys.conf 的 lang 字段确定登录界面语言。
 * 登录前尚无当前用户, 系统语言由 sys.conf 决定。
 * 读不到则返回 0 (默认英文)。 */
static int load_sys_lang(struct kernel_api *api) {
    char buf[64];
    int r = api->file_read ? api->file_read("/users/sys.conf", buf, sizeof(buf)-1) : -1;
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

/* ========== 主菜单渲染与操作 ========== */
static void efmlogin_main(void) {
    struct kernel_api *api = API;
    if (api->magic != 0xEF110001) return;

    /* 字体像素尺寸: 与内核对齐, 替代硬编码 8x16 */
    const int FW = (api->font_w > 0) ? api->font_w : 8;
    const int FH = (api->font_h > 0) ? api->font_h : 16;
    /* 初始化透明文字渲染全局参数 */
    g_fw = FW; g_fh = FH;
    g_vw = 80 * FW;
    if (api->get_viewport) { int cw = 0, ch = 0; api->get_viewport(0, 0, &cw, &ch); if (cw > 0) g_vw = cw; }

    api->mouse_set_cursor(1);

    /* --- efmlogin_menu (用户选择 + 操作按钮) 内联辅助宏 --- */
#define EFML_BX1_COLS   2
#define EFML_BX2_COLS  70
#define EFML_PW_X1_COLS  2
#define EFML_PW_X2_COLS 36
#define EFML_ROW(r)     (r)
#define EFML_Y1(r)      ((r)*FH)
#define EFML_Y2(r)      ((r + 1)*FH - 1)
#define EFML_X1(cols)   ((cols)*FW)
#define EFML_X2(cols)   ((cols)*FW)

    for (;;) {
        refresh_users(api);
        /* [新架构] api->clear_screen 仅用于 WM 模式清 tbuf, 不再画像素.
         * 改用 gui_begin() 重置 GUI 数据计数 → 重绘所有 rects + texts. */
        gui_begin();
        treset();
        g_btn_n = 0;
        /* 语言以 /users/sys.conf 为准 (登录前无当前用户) */
        int zh = (load_sys_lang(api) == 1);
        if (api->set_lang) api->set_lang(zh ? 1 : 0);

        /* 绘制桌面表单背景 */
        draw_form_bg(api);

        /* 标题 (在标题栏区域) */
        if (zh) tprint_utf8(api, "  EFMOS 系统登录\n");
        else    tprint(api, "  EFMOS System Login\n");
        tputc(api, '\n');

        if (zh) {
            tprint_utf8(api, "  账户总数: ");
        } else {
            tprint(api, "  Account count: ");
        }
        char nb[16]; tprint(api, u2s((unsigned int)g_user_count, nb));
        tprint(api, "\n\n");

        /* 用户列表 */
        if (zh) tprint_utf8(api, "  用户列表 (点击/按键选择)\n");
        else    tprint(api, "  User list (click/press key)\n");
        print_user_list(api, zh);
        tputc(api, '\n');

        /* 底部按钮区 — 按钮高 2*FH-2 (≈30px), 文字水平+垂直居中.
         * 左=1 切换语言, 右=0 重启. */
        int btn_h = 2 * FH - 2;
        int const_btn_w = 32 * FW;
        int gap_x = 2 * FW;
        int left_x1 = 1 * FW;
        int right_x1 = left_x1 + const_btn_w + gap_x;
        int right_x2 = right_x1 + const_btn_w - 1;
        int cur_row_y = g_cur_y;

        /* 画一行两个按钮 */
        _outline(left_x1, cur_row_y, left_x1 + const_btn_w - 1, cur_row_y + btn_h - 1, BTN_OUTLINE);
        btn_add(left_x1, cur_row_y, left_x1 + const_btn_w - 1, cur_row_y + btn_h - 1, '1');
        _outline(right_x1, cur_row_y, right_x2, cur_row_y + btn_h - 1, BTN_OUTLINE);
        btn_add(right_x1, cur_row_y, right_x2, cur_row_y + btn_h - 1, '0');
        /* 居中文字 */
        {
            int py = g_cur_y;
            const char *lt = zh ? "1. 切换语言" : "1. Toggle lang";
            const char *rt = zh ? "0. 重启" : "0. Reboot";
            g_cur_y = cur_row_y + (btn_h - FH) / 2;
            g_cur_x = left_x1 + (const_btn_w - text_w_px(lt, FW)) / 2;
            if (zh) tprint_utf8(api, lt); else tprint(api, lt);
            g_cur_x = right_x1 + (const_btn_w - text_w_px(rt, FW)) / 2;
            if (zh) tprint_utf8(api, rt); else tprint(api, rt);
            g_cur_y = py;
        }
        g_cur_y += btn_h + 2;

        tputc(api, '\n');
        if (zh) {
            tprint_utf8(api, "  点击用户名登录, 或按 A-");
            if (g_user_count <= 26) {
                char last[4]; last[0] = 'A' + ((g_user_count > 0) ? (g_user_count-1) : 0); last[1] = 0;
                tprint(api, last);
            }
            tprint_utf8(api, " 键\n  ESC 取消\n\n  选择: ");
        } else {
            tprint(api, "  Click username or press A-");
            if (g_user_count <= 26) {
                char last[4]; last[0] = 'A' + ((g_user_count > 0) ? (g_user_count-1) : 0); last[1] = 0;
                tprint(api, last);
            }
            tprint(api, " key\n  ESC to cancel\n\n  Choose: ");
        }
        tflush(api);

        char ch = choose_key(api);
        if (ch == 0) continue;

        /* ---------- 处理用户登录 (点击用户列表: 'A'..'Z' / 'a'..'z') ---------- */
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            int idx = ((ch >= 'A' && ch <= 'Z') ? (ch - 'A') : (ch - 'a'));
            if (idx < g_user_count) {
                const char *uname = g_users[idx].name;
                /* 密码验证: 所有用户必须输入密码 */
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
                } else {
                    /* 无密码账户: 安全起见也要求设置 (异常情况, 提示) */
                    if (zh) tprint_utf8(api, "该账户未设置密码, 无法登录! 请重启进入用户管理设置密码。\n");
                    else    tprint(api, "This account has no password, cannot login! Reboot into user manager to set one.\n");
                    tflush(api);
                    pw_ok = 0;
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
                for (volatile int i = 0; i < 4000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
                continue;
            }
        }

        /* ---------- 0 重启 ---------- */
        if (ch == '0' || ch == 'q' || ch == 'Q') {
            if (api->reboot) api->reboot();
            break;
        }

        /* ---------- 1 切换语言 (写入 /users/sys.conf 持久化) ---------- */
        if (ch == '1') {
            if (api->get_lang && api->set_lang) {
                int newlang = (api->get_lang() == 1) ? 0 : 1;
                api->set_lang(newlang);
                save_sys_lang(api, newlang);
                if (zh) tprint_utf8(api, "语言已切换并保存到系统配置。\n");
                else    tprint(api, "Language switched and saved to system config.\n");
                tflush(api);
                for (volatile int i = 0; i < 3000000; i++) __asm__ volatile("pause"); if (api->yield) api->yield();
            }
            continue;
        }
    }

EXIT_TO_SHELL:
    api->clear_screen();
}
