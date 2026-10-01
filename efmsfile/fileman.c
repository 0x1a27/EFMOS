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

/* fileman.c - EFMOS 文件管理器程序 (编译为 fileman.efs)
 * 加载地址: 0x300000 (3MB, 不与内核和 setting.efs 冲突)
 * 入口: _start (二进制首字节)
 *
 * 功能:
 *   - 浏览目录 (列出文件/子目录, 显示大小)
 *   - 进入子目录 / 返回上级目录
 *   - 查看文件内容
 *   - 删除文件 (带确认)
 *   - 中英文双语 (跟随 setting 的语言设置)
 *
 * [新架构] EFS GUI 渲染协议:
 *   不再直接写像素/调用 api->print, 而是构造渲染信息 (rects + texts),
 *   通过 set_gfx_info 提交给 compositor 统一绘制. 彻底消除残影/撕裂/错位.
 */

/* ========== 内核 API 表 (与内核 struct kernel_api 匹配) ========== */
#include "efmos/efm_api.h"
struct ev_mouse { int dx, dy; unsigned char btn; int x, y; };

/* ========== [新架构] EFS GUI 渲染协议 ========== */
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
/* [TTF 字号同步] 与 compositor 的 TTF_FONT_W/H (12x22) 和内核 api->font_w/h 一致.
 * 之前为 8x16 (旧点阵字体), 导致进入桌面后 compositor 用 12x22 实际画, 但 EFS
 * 用 8x16 排版 (按钮位置/行高/字符步进错位)。现在即使在 main 赋值 api->font_w/h
 * 之前 (极端边界情况), 任何早期 gui_add_codepoint 调用也用正确 TTF 尺寸。 */
static int g_fw = 14, g_fh = 26, g_vw = 1120, g_vh = 650;  /* 1120 = 80*14, 650 ≈ 25*26 */

/* 透明背景标记: 必须在 gui_flush 之前定义 (gui_flush fallback 传给 draw_char_unicode) */
#define COLOR_TRANSPARENT 0xFEEDFACEu  /* 与 Graphics.drv/gfx_draw_char_unicode 一致 */

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
static void gui_flush(struct kernel_api *api) {
    /* 原子提交: 先递增 seq (compositor 检测到变化才重绘), 再确认 magic.
     * compositor 通过 seq 变化检测, 只有 seq 变了才读 rect/text_count,
     * 所以 gui_begin 清零计数不会导致 compositor 渲染空白. */
    g_gui.magic = EFM_GFX_MAGIC;
    g_gui.seq += 1;
    if (api->set_gfx_info) api->set_gfx_info(&g_gui);

    /* ===== [FALLBACK · 合成器启动前] 直接用 API 像素绘制 =====
     * 当 api->wm_enabled==0 (合成器未启动, 如 efmlogin/userman 在登录前全屏)
     * 或 set_gfx_info 为空 (旧内核), 没有消费者读 gfx_info 协议数据.
     * 此时必须直接写 FB, 否则界面完全空白.
     * 视图坐标: g_gui 内存的是"内容区相对坐标". wm_enabled=0 时, 内容区=全屏
     * (左上角=0,0), 直接画即可; wm_enabled=1 但找不到窗口时, API 会做裁剪. */
    if (!api->wm_enabled || !api->set_gfx_info) {
        /* --- 1. 画所有矩形 (色块 + 边框) --- */
        for (int i = 0; i < g_gui.rect_count; i++) {
            struct efm_gfx_rect *r = &g_gui.rects[i];
            if (r->w <= 0 || r->h <= 0) continue;
            int x1 = r->x, y1 = r->y;
            int x2 = r->x + r->w - 1;
            int y2 = r->y + r->h - 1;
            if (r->has_border && api->draw_rect) {
                /* draw_rect(x1,y1,x2,y2,border,fill) 一次完成填充+描边 */
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
                int used = 1, cjk = 0;
                unsigned char c = *s;
                if (c < 0x80) {
                    cp = c; used = 1;
                } else if ((c & 0xE0) == 0xC0 && s[1]) {
                    cp = ((unsigned int)(c & 0x1F) << 6) |
                         ((unsigned int)(unsigned char)s[1] & 0x3F);
                    used = 2; cjk = 1;
                } else if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
                    cp = ((unsigned int)(c & 0x0F) << 12) |
                         (((unsigned int)(unsigned char)s[1] & 0x3F) << 6) |
                         ((unsigned int)(unsigned char)s[2] & 0x3F);
                    used = 3; cjk = 1;
                } else {
                    used = 1; /* 非法字节, 跳过 */
                    s += used; continue;
                }
                int cell_w = cjk ? (2 * g_fw) : g_fw;
                int cell_h = g_fh;
                if (api->draw_char_unicode) {
                    /* bg 传 0 (黑色), 因为矩形已在步骤 1 画过背景, 文字底层色块
                     * 一般匹配文本背景色; 无 alpha, 纯写像素. */
                    /* bg=COLOR_TRANSPARENT: 文字背景透明, 直接叠加到矩形/背景色上, 不盖底色形成黑方块 */
                    api->draw_char_unicode(cx, cy, cp, t->color, COLOR_TRANSPARENT, cell_w, cell_h);
                }
                cx += cell_w;
                s += used;
            }
        }
        /* [纯 Shell 修复 · 双缓冲] 非 WM 且非合成器接管时,
         * fill_rect/draw_char_unicode 写的是 Graphics.drv back buffer,
         * 必须显式 flush_now → front buffer 才显示, 否则 fileman 界面只留在
         * back buffer, 屏幕显示灰底, 用户以为"fileman 无法正常打开"。 */
        if (api->flush_now) api->flush_now();
    }
}

/* 文本光标 & tputc/tprint (GUI 协议版) */
#define TEXT_FG  0xFFFFFFu
#define DESKTOP_BG 0x1A1A2E   /* 深靛 (与 efmlogin/userman/setting/efmAether 统一) */
/* COLOR_TRANSPARENT 已提前到 g_fw 旁定义 (gui_flush fallback 需要) */
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
/* readline: GUI 协议版 */
#define LKEY_ENTER -101
#define LKEY_BS    -102
#define LKEY_ESC   -103
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

/* ========== 辅助函数前向声明 ========== */
static void _pix(int x, int y, unsigned int c);
static void _rect(int x1,int y1,int x2,int y2,unsigned int c);
static void _box(int x1,int y1,int x2,int y2,unsigned int bo,unsigned int fl);
static void btn_add(int x1,int y1,int x2,int y2,char val);
static char btn_hit(int mx,int my);
static inline unsigned char _inb(unsigned short p);
static int _kbd_getc_nonblock(void);
static char choose_key(struct kernel_api *api);
static int s_len(const char *s);
static int s_cmp(const char *a, const char *b);
static char *s_cpy(char *d, const char *s);
static char *u2s(unsigned int v, char *buf);
static int read_dir(struct kernel_api *api, const char *path);
static void draw_file_list(struct kernel_api *api, int zh);
static void fileman_main(void);

/* ========== 入口点 (必须为 .text 第一个函数) ========== */
__attribute__((naked))
void _start(void) {
    __asm__ volatile(
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"
        "call fileman_main\n\t"
        "leave\n\t"
        "ret\n\t"
    );
}

/* ========== 矩形辅助 (GUI 协议版) ========== */
#define BTN_BG     0x1A3A5C
#define BTN_BORDER 0x6A9AD0
#define DIR_BG     0x2A5C3A
#define FILE_BG    0x3A3A5C
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

#define MAX_BTN 24
static struct { int x1,y1,x2,y2; char val; } g_btns[MAX_BTN] = {{0,0,0,0,0}};
static int g_btn_n = 0;
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
static inline unsigned char _inb(unsigned short p) { unsigned char v; __asm__ volatile("inb %1, %0":"=a"(v):"Nd"(p)); return v; }
/* 用户态非阻塞键盘读 */
static int _kbd_getc_nonblock(void) {
    static int sh=0,cap=0;
    unsigned char st = _inb(0x64);
    if(!(st & 0x01)) return 0;
    if(st & 0xC0){(void)_inb(0x60);return 0;}
    unsigned char b = _inb(0x60);
    if (st & 0x20) return 0;
    switch(b){case 0x00:case 0xAA:case 0xEE:case 0xFA:case 0xFC:case 0xFD:case 0xFE:case 0xFF:return 0;}
    if (b == 0xE0) {
        int w;for(w=0;w<20000;w++){if(_inb(0x64)&0x01)break;__asm__ volatile("pause");}
        if(_inb(0x64)&0x01)(void)_inb(0x60);
        return 0;
    }
    if (b & 0x80) {
        unsigned char code = b & ~0x80;
        if (code == 0x2A || code == 0x36) sh = 0;
        return 0;
    }
    if (b == 0x2A || b == 0x36) { sh = 1; return 0; }
    if (b == 0x3A) { cap = !cap; return 0; }
    if (b == 0x1C) return LKEY_ENTER;
    if (b == 0x0E) return LKEY_BS;
    if (b == 0x01) return LKEY_ESC;
    static const char lo[]={
        0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
        'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
        'b','n','m',',','.','/',0,0,0,' '
    };
    static const char hi[]={
        0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0,0,
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
        'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
        'B','N','M','<','>','?',0,0,0,' '
    };
    if(b>=sizeof(lo))return 0;
    char c=lo[b]; if(c==0)return 0;
    int is_letter=(c>='a'&&c<='z');
    if(is_letter){if(sh!=cap)c=hi[b];}else if(sh)c=hi[b];
    return (int)(unsigned char)c;
}

/* 通用选择: 边缘检测鼠标点击 */
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
            if (k == LKEY_ESC) return 0x1B;
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
        if(k==LKEY_ESC)return 0x1B;
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
static void path_join(const char *parent, const char *child, char *out, int outsz) {
    int pl = s_len(parent);
    int cl = s_len(child);
    if (pl == 1 && parent[0] == '/') {
        if (1 + cl + 1 > outsz) return;
        out[0] = '/'; s_cpy(out+1, child);
    } else {
        if (pl + 1 + cl + 1 > outsz) return;
        s_cpy(out, parent);
        out[pl] = '/';
        s_cpy(out+pl+1, child);
    }
}
static void path_parent(const char *path, char *out, int outsz) {
    int pl = s_len(path);
    if (pl <= 1) { out[0] = '/'; out[1] = 0; return; }
    int end = pl - 1;
    if (path[end] == '/') end--;
    int last = -1;
    for (int i = end; i >= 0; i--) { if (path[i] == '/') { last = i; break; } }
    if (last <= 0) { out[0] = '/'; out[1] = 0; return; }
    if (last + 1 > outsz) return;
    for (int i = 0; i < last; i++) out[i] = path[i];
    out[last] = 0;
}

/* ========== 文件管理器主逻辑 ========== */
#define MAX_ENTRIES 64
#define PAGE_SIZE 8
static struct efs_dirent g_entries[MAX_ENTRIES] = {{{0},0,0}};
static char g_cwd[256] = {0};

static void print_banner(struct kernel_api *api) {
    int zh = (api->get_lang() == 1);
    tprint(api, "======================================\n");
    if (zh) tprint_utf8(api, "     EFMOS 文件管理器 v2.0\n");
    else    tprint(api, "     EFMOS File Manager v2.0\n");
    tprint(api, "======================================\n");
}

/* 绘制整个桌面背景 + 填充 */
static void draw_bg(void) {
    _rect(0, 0, g_vw - 1, g_vh - 1, DESKTOP_BG);
}

static int show_dir_page(struct kernel_api *api, int count, int page) {
    const int FW = g_fw;
    const int FH = g_fh;
    int zh = (api->get_lang() == 1);
    int total_pages = (count + PAGE_SIZE - 1) / PAGE_SIZE;
    if (total_pages == 0) total_pages = 1;
    int cur_page = page / PAGE_SIZE + 1;

    if (zh) tprint_utf8(api, "路径: ");
    else    tprint(api, "Path: ");
    tprint(api, g_cwd);
    char nb[16];
    tprint(api, "  (");
    tprint(api, u2s((unsigned int)count, nb));
    if (zh) tprint_utf8(api, " 项, 第 ");
    else    tprint(api, " items, page ");
    tprint(api, u2s((unsigned int)cur_page, nb));
    tprint(api, "/");
    tprint(api, u2s((unsigned int)total_pages, nb));
    tprint(api, ")\n\n");

    g_btn_n = 0;
    int bx1 = 1*FW, bx2 = 76*FW;

    int shown = 0;
    for (int idx = page; idx < page + PAGE_SIZE && idx < count; idx++) {
        int row = 5 + shown;
        int y1 = row*FH - 2, y2 = (row+1)*FH;
        unsigned int bg = g_entries[idx].is_dir ? DIR_BG : FILE_BG;
        _box(bx1, y1, bx2, y2, BTN_BORDER, bg);
        btn_add(bx1, y1, bx2, y2, '1' + shown);

        char numbuf[8];
        tprint(api, "  "); tprint(api, u2s((unsigned int)(shown+1), numbuf)); tprint(api, ". ");
        if (g_entries[idx].is_dir) {
            if (zh) tprint_utf8(api, "[目录] ");
            else    tprint(api, "[DIR ] ");
        } else {
            if (zh) tprint_utf8(api, "[文件] ");
            else    tprint(api, "[FILE] ");
        }
        tprint(api, g_entries[idx].name);
        if (!g_entries[idx].is_dir) {
            tprint(api, "  (");
            tprint(api, u2s(g_entries[idx].size, numbuf));
            if (zh) tprint_utf8(api, " 字节)");
            else    tprint(api, " bytes)");
        }
        tprint(api, "\n");
        shown++;
    }

    if (count == 0) {
        if (zh) tprint_utf8(api, "  (空目录)\n");
        else    tprint(api, "  (empty directory)\n");
        shown = 1;
    }

    while (shown < PAGE_SIZE) { tprint(api, "\n"); shown++; }

    tprint(api, "\n");
    int crow = 5 + PAGE_SIZE + 1;

    int is_root = (g_cwd[0] == '/' && g_cwd[1] == 0);
    int col = 0;
    if (!is_root) {
        int y1 = crow*FH-2, y2 = (crow+1)*FH;
        _box(1*FW, y1, 14*FW, y2, BTN_BORDER, BTN_BG);
        btn_add(1*FW, y1, 14*FW, y2, '0');
        if (zh) tprint_utf8(api, " 0.返回上级 ");
        else    tprint(api, " 0.Up ");
        col = 16;
    }
    if (cur_page > 1) {
        int y1 = crow*FH-2, y2 = (crow+1)*FH;
        _box(col*FW, y1, (col+11)*FW, y2, BTN_BORDER, BTN_BG);
        btn_add(col*FW, y1, (col+11)*FW, y2, 'P');
        if (zh) { tprint_utf8(api, " P.上一页 "); col += 13; }
        else    { tprint(api, " P.Prev "); col += 9; }
    }
    if (cur_page < total_pages) {
        int y1 = crow*FH-2, y2 = (crow+1)*FH;
        _box(col*FW, y1, (col+11)*FW, y2, BTN_BORDER, BTN_BG);
        btn_add(col*FW, y1, (col+11)*FW, y2, 'N');
        if (zh) { tprint_utf8(api, " N.下一页 "); col += 13; }
        else    { tprint(api, " N.Next "); col += 9; }
    }
    {
        int y1 = crow*FH-2, y2 = (crow+1)*FH;
        _box(col*FW, y1, (col+10)*FW, y2, BTN_BORDER, BTN_BG);
        btn_add(col*FW, y1, (col+10)*FW, y2, 'Q');
        if (zh) tprint_utf8(api, " Q.退出 ");
        else    tprint(api, " Q.Quit ");
    }
    tprint(api, "\n\n");
    if (zh) tprint_utf8(api, "选择 (数字键/鼠标点击): ");
    else    tprint(api, "Select (key or click): ");

    return total_pages;
}

/* 查看文件内容 (GUI 协议版) */
static void view_file(struct kernel_api *api, const char *path) {
    int zh = (api->get_lang() == 1);
    gui_begin();
    treset();
    draw_bg();
    print_banner(api);
    if (zh) tprint_utf8(api, "--- 查看文件 ---\n");
    else    tprint(api, "--- View File ---\n");
    tprint(api, path); tprint(api, "\n\n");

    static char content[4096];
    int n = api->file_read(path, content, sizeof(content) - 1);
    if (n < 0) n = 0;
    content[n] = 0;

    char nb[16];
    tprint(api, "("); tprint(api, u2s((unsigned int)n, nb));
    if (zh) tprint_utf8(api, " 字节)\n\n");
    else    tprint(api, " bytes)\n\n");
    /* 逐字打印 (防止溢出 text buffer) */
    for (int i = 0; i < n; i++) {
        char c = content[i];
        if (c == '\n' || c >= 0x20) tputc(api, (int)(unsigned char)c);
    }
    tprint(api, "\n--- EOF ---\n");

    if (zh) tprint_utf8(api, "\n按 Enter 返回... ");
    else    tprint(api, "\nPress Enter to return... ");
    gui_flush(api);
    char b[32];
    treadline(api, b, sizeof(b));
}

/* 删除文件 (带确认, GUI 协议版) */
static void delete_file(struct kernel_api *api, const char *path, const char *name) {
    int zh = (api->get_lang() == 1);
    for (;;) {
        gui_begin();
        treset();
        draw_bg();
        print_banner(api);
        if (zh) tprint_utf8(api, "--- 删除文件 ---\n");
        else    tprint(api, "--- Delete File ---\n");
        tprint(api, name); tprint(api, "\n\n");
        if (zh) tprint_utf8(api, "确认删除此文件? (Y/N): ");
        else    tprint(api, "Confirm delete? (Y/N): ");
        gui_flush(api);

        char b[32];
        int r = treadline(api, b, sizeof(b));
        (void)r;
        char confirm = (b[0] == 'Y' || b[0] == 'y') ? 1 : 0;

        gui_begin();
        treset();
        draw_bg();
        print_banner(api);
        if (zh) tprint_utf8(api, "--- 删除文件 ---\n");
        else    tprint(api, "--- Delete File ---\n");
        tprint(api, name); tprint(api, "\n\n");
        if (confirm) {
            int ret = api->file_delete(path);
            if (ret == 0) {
                if (zh) tprint_utf8(api, "\n文件已删除!\n");
                else    tprint(api, "\nFile deleted!\n");
            } else {
                if (zh) tprint_utf8(api, "\n删除失败! (可能是目录或受保护文件)\n");
                else    tprint(api, "\nDelete failed! (may be a directory or protected)\n");
            }
        } else {
            if (zh) tprint_utf8(api, "\n已取消删除。\n");
            else    tprint(api, "\nDelete cancelled.\n");
        }
        if (zh) tprint_utf8(api, "按 Enter 返回... ");
        else    tprint(api, "Press Enter to return... ");
        gui_flush(api);
        treadline(api, b, sizeof(b));
        return;
    }
}

/* 文件操作子菜单 (GUI 协议版) */
static void file_submenu(struct kernel_api *api, const char *path, const char *name, unsigned int size) {
    int zh = (api->get_lang() == 1);
    for (;;) {
        gui_begin();
        treset();
        draw_bg();
        print_banner(api);
        if (zh) tprint_utf8(api, "--- 文件操作 ---\n");
        else    tprint(api, "--- File Actions ---\n");
        tprint(api, name); tprint(api, "\n");
        char nb[16];
        if (zh) { tprint_utf8(api, "大小: "); tprint(api, u2s(size, nb)); tprint_utf8(api, " 字节\n\n"); }
        else    { tprint(api, "Size: "); tprint(api, u2s(size, nb)); tprint(api, " bytes\n\n"); }

        const int FW = g_fw;
        const int FH = g_fh;
        g_btn_n = 0;
        int bx1 = 1*FW, bx2 = 50*FW;
        _box(bx1, 5*FH-2, bx2, 6*FH, BTN_BORDER, FILE_BG); btn_add(bx1,5*FH-2,bx2,6*FH,'1');
        _box(bx1, 6*FH-2, bx2, 7*FH, BTN_BORDER, 0x5C1A1A); btn_add(bx1,6*FH-2,bx2,7*FH,'2');
        _box(bx1, 7*FH-2, bx2, 8*FH, BTN_BORDER, BTN_BG); btn_add(bx1,7*FH-2,bx2,8*FH,'0');

        if (zh) {
            tprint_utf8(api, "  [1] 查看文件内容\n");
            tprint_utf8(api, "  [2] 删除文件\n");
            tprint_utf8(api, "  [0] 返回目录\n\n");
            tprint_utf8(api, "选择: ");
        } else {
            tprint(api, "  [1] View content\n");
            tprint(api, "  [2] Delete file\n");
            tprint(api, "  [0] Back to directory\n\n");
            tprint(api, "Select: ");
        }
        gui_flush(api);

        char ch = choose_key(api);
        if (ch == 0x1B || ch == '0' || ch == 0) return;
        if (ch == '1') { view_file(api, path); }
        else if (ch == '2') { delete_file(api, path, name); return; }
    }
}

/* ========== 主函数 (GUI 协议版) ========== */
static void fileman_main(void) {
    struct kernel_api *api = API;
    if (api->magic != 0xEF110001) return;

    /* 初始化字体/视口尺寸 */
    const int FW = (api->font_w > 0) ? api->font_w : 8;
    const int FH = (api->font_h > 0) ? api->font_h : 16;
    g_fw = FW; g_fh = FH;
    g_vw = 80 * FW;
    g_vh = 25 * FH;
    if (api->get_viewport) {
        int cw = 0, ch = 0;
        api->get_viewport(0, 0, &cw, &ch);
        if (cw > 0) g_vw = cw;
        if (ch > 0) g_vh = ch;
    }

    api->mouse_set_cursor(1);
    g_cwd[0] = '/'; g_cwd[1] = 0;

    int page = 0;

    for (;;) {
        gui_begin();
        treset();
        draw_bg();
        print_banner(api);

        int count = api->file_list ? api->file_list(g_cwd, g_entries, MAX_ENTRIES) : -1;
        if (count < 0) {
            int zh = (api->get_lang() == 1);
            if (zh) tprint_utf8(api, "无法读取目录, 返回根目录...\n");
            else    tprint(api, "Cannot read directory, returning to root...\n");
            g_cwd[0] = '/'; g_cwd[1] = 0;
            page = 0;
            gui_flush(api);
            char b[32]; treadline(api, b, sizeof(b));
            continue;
        }

        if (page > count) page = 0;
        if (page % PAGE_SIZE != 0) page = (page / PAGE_SIZE) * PAGE_SIZE;

        (void)show_dir_page(api, count, page);
        gui_flush(api);

        char ch = choose_key(api);
        if (ch == 0x1B) break;
        if (ch == 0) { continue; }
        if (ch == 'Q' || ch == 'q') break;

        if (ch == '0') {
            char parent[256];
            path_parent(g_cwd, parent, sizeof(parent));
            s_cpy(g_cwd, parent);
            page = 0;
            continue;
        }
        if (ch == 'P' || ch == 'p') {
            if (page >= PAGE_SIZE) page -= PAGE_SIZE;
            continue;
        }
        if (ch == 'N' || ch == 'n') {
            if (page + PAGE_SIZE < count) page += PAGE_SIZE;
            continue;
        }
        if (ch >= '1' && ch <= '8') {
            int sel = (ch - '1') + page;
            if (sel < count) {
                if (g_entries[sel].is_dir) {
                    char newpath[256];
                    path_join(g_cwd, g_entries[sel].name, newpath, sizeof(newpath));
                    s_cpy(g_cwd, newpath);
                    page = 0;
                } else {
                    char fpath[256];
                    path_join(g_cwd, g_entries[sel].name, fpath, sizeof(fpath));
                    file_submenu(api, fpath, g_entries[sel].name, g_entries[sel].size);
                }
            }
        }
    }
    gui_begin();
    gui_flush(api);
}
