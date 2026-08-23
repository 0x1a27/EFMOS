/* EFMOS Kernel - Complete with Fully Fixed AHCI Driver and ext4 Driver */
#define COM1_PORT 0x3F8
#define NULL ((void*)0)

/* ========== 编译期模型守卫 ==========
 * [致命] 本内核必须用 -mcmodel=large 编译!
 * -mcmodel=kernel 假设代码/数据在 0xFFFFFFFF80000000+ (高半核),
 * 但我们的内核链接/加载在低物理地址 (0x200000)。
 * 如果误用 -mcmodel=kernel:
 *   - 编译器会把全局变量地址编成 0xFFFFFFFFxxxxxxxx
 *   - 运行时 CR2=0xFFFFFFFFxxxxxxxx 的 #PF (Page Fault) 立即崩溃
 * -mcmodel=large 用 movabs 64 位绝对寻址,按真实链接地址引用,
 * 不管高/低地址都正确。
 * 下面守卫在编译期就报错,不让错误的 .o 生成。 */
#if !defined(__code_model_large__)
#  error "kernel.c must be compiled with -mcmodel=large. Check Makefile kernel/kernel.elf rule: gcc ... -mcmodel=large -c kernel/kernel.c"
#endif
/* mcmodel=large 同时隐含 -fno-pic, 但再明确守卫一下 */
#ifdef __PIE__
#  error "kernel.c must NOT be compiled as PIE. Add -fno-pic -no-pie"
#endif

struct gdt_entry { unsigned short limit_low, base_low; unsigned char base_mid, access, granularity, base_high; } __attribute__((packed));
struct gdt_ptr { unsigned short limit; unsigned long long base; } __attribute__((packed));
struct idt_entry { unsigned short offset_low, selector; unsigned char ist, flags; unsigned short offset_mid; unsigned int offset_high, zero; } __attribute__((packed));
struct idt_ptr { unsigned short limit; unsigned long long base; } __attribute__((packed));
struct gop_fb { unsigned long long fb_base; unsigned int hr, vr, ppsl; } __attribute__((packed));
struct file_entry { char path[64]; unsigned int size; unsigned int offset; unsigned int type; };

/* 字体尺寸: 必须与 efmcompositor.c 的 TTF_FONT_W/TTF_FONT_H 一致.
 * 之前内核用 10x18 而 compositor 用 12x22, 导致窗口尺寸/光标位置/任务栏高度
 * 与实际 TTF 渲染不匹配, 造成文字错位/裁剪/按钮不对齐.
 * [1.2x 放大] 12→14, 22→26 (12*1.2=14.4→14, 22*1.2=26.4→26) */
#define FONT_W  14
#define FONT_H  26

/* ===== Window Manager 常量 (必须在 FONT_W/FONT_H 之后, 且尽早, 供全文件使用) ===== */
#define WM_MAX_WINDOWS  32
#define WM_TITLE_H      (FONT_H + 6)   /* 标题栏高度 (含边框间距) */
#define WM_TASKBAR_H    (3 * FONT_H + 8)   /* 任务栏高度: 加大以容纳 TTF 字体 (22px) + 上下间距 */
#define WM_BORDER       2
#define WM_CLOSE_W      (FONT_W * 3)   /* 关闭按钮宽 */
#define WM_MIN_W        (10 * FONT_W)
#define WM_MIN_H        (WM_TITLE_H + 3 * FONT_H)
/* 窗口文本缓冲区尺寸 (每个窗口存储的字符网格) */
#define WM_TBUF_COLS  120
#define WM_TBUF_ROWS  60
/* WM 颜色 (ARGB 小端: 0xAABBGGRR 或直接填 0x00BBGGRR 因代码最后会 shift 写 fb; 这里直接匹配 fill_rect 用的格式 = 0x00RRGGBB? 不, 看定义: draw.c中 fill_rect 用 base[yy*pitch+xx] = color; GOP format 通常是 BGR, 所以 0x00RRGGBB 会在屏上显示 BGR, 但由于 bootloader 设置了 pixel format, 这里直接取历史值即可. 后面定义再 cover 一次时就不算重复了 - 实际宏可重复定义 (只要相同). */
#define WM_CLR_BG           0x001A1A2E   /* 桌面背景: 深靛 (与 efmlogin/userman/setting/efmAether 统一) */
#define WM_CLR_TITLE_ACTIVE 0x001976D2   /* 激活标题: 蓝色 */
#define WM_CLR_TITLE_INACT  0x0078909C   /* 未激活标题: 蓝灰 */
#define WM_CLR_TITLE_TEXT   0x00FFFFFF   /* 标题文字: 白 */
#define WM_CLR_CONTENT_BG   0x001A1A2E   /* 内容区: 深靛 (统一) */
#define WM_CLR_CONTENT_FG   0x00FFFFFF   /* 内容文字: 白 */
#define WM_CLR_BORDER       0x00333344   /* 边框: 紫灰 (匹配深靛) */
#define WM_CLR_CLOSE_BG     0x00E53935   /* 关闭按钮: 红 */
#define WM_CLR_CLOSE_HOVER  0x00C62828
#define WM_CLR_CLOSE_TEXT   0x00FFFFFF
#define WM_CLR_TASKBAR_BG   0x00161626   /* 任务栏: 深靛变种 (比 BG 略暗) */
#define WM_CLR_TASKBAR_BTN  0x0037474F   /* 任务栏按钮: 蓝灰 */
#define WM_CLR_TASKBAR_ACT  0x001976D2   /* 激活窗口按钮: 蓝 */
#define WM_CLR_DESKTOP_TEXT 0x00FFFFFF
/* 透明背景标记: 用于 EFS 图形窗口的文本渲染, 只画前景像素不覆盖下层图形 */
#define COLOR_TRANSPARENT 0xFEEDFACEu
/* WM 命中检测 */
#define WM_HIT_INSIDE   0
#define WM_HIT_TITLE    1
#define WM_HIT_CLOSE    2
/* ===== END Window Manager 常量 ===== */

/* ===== LAPIC / APIC 常量 (寄存器地址 / 位标志) ===== */
#define LAPIC_EOI         0x0B0
#define LAPIC_SIVR        0x0F0
#define LAPIC_ICRLO       0x300
#define LAPIC_ICRHI       0x310
#define LAPIC_LVT_TIMER   0x320
#define LAPIC_TIMER_DIV   0x3E0
#define LAPIC_TIMER_INIT  0x380
#define LAPIC_TIMER_CUR   0x390
#define LAPIC_ID          0x020
#define LAPIC_ENABLE      (1<<8)
#define LAPIC_PERIODIC    (1<<17)
#define LAPIC_MASKED      (1<<16)
/* ===== END LAPIC / APIC 常量 ===== */

/* ===== 驱动子系统公共头 (尽早引入: disk_read_sector 等需要 drv_disk_ops) =====
 * 在文件后半部分 5175 行还有第二次 #include — C 头文件一般带 #pragma once,
 * 这里手动加 include guard 避免重复包含副作用。drv_common.h 里仅类型和常量定义,
 * 重复引入无危害, 但为了严谨, 用自定义 guard。*/
#ifndef DRV_COMMON_H_FIRST_INCLUDE_GUARD
#define DRV_COMMON_H_FIRST_INCLUDE_GUARD
#include "drv_common.h"
#endif

/* bootlogo.h: 用户上传的系统图标 (200x200 alpha 灰度图, 0=透明, 255=不透明白) */
#include "bootlogo.h"

static struct gdt_entry gdt[7]; static struct gdt_ptr gdt_ptr;
static struct idt_entry idt[256]; static struct idt_ptr idt_ptr;

/* [EFS 恢复机制] 当用户程序调用 exit() 时会触发 int 0x3 (#DB 异常),
 * 异常处理器检测到后通过这些全局变量恢复内核状态 */
static volatile int      efs_active = 0;        /* EFS 程序运行标志 */
static unsigned long     efs_saved_rsp = 0;    /* 内核保存的 rsp */
static unsigned long     efs_saved_rbp = 0;    /* 内核保存的 rbp */
static unsigned long     efs_load_addr = 0;    /* EFS 加载地址 */
static unsigned int      efs_bin_size = 0;     /* EFS 二进制大小 */
static void             *efs_stack_ptr = 0;    /* EFS 栈指针 (用于释放) */
static void             *efs_return_point = 0; /* EFS 返回点地址 (label address) */
/* [gcc 返回值修复] EFS 程序的退出码:
 *   ≥ 0 = main() / _start() ret 返回值 (rax)
 *   -256 = EFS 程序异常 (#GP/#PF/#DB 等), 由 exception_handler 捕获设置 */
static volatile int      efs_exit_code = 0;

/* [字符渲染统一] 内核不再包含位图字体表, 所有字符渲染统一由 TTF 驱动
 * (Graphics.drv) 的 draw_char_unicode 接口完成. 早期引导 (驱动加载前)
 * 的输出通过 COM1 串口完成, 不依赖帧缓冲字符渲染. */

/* ========== 双语支持宏 (提前定义, 供所有函数使用) ==========
 * LANG  = 当前语言 (0=英文, 1=中文)
 * TR(en,zh) = 根据 LANG 返回英文字符串或中文字符串
 * 所有面向用户的提示都应该用 TR 宏, 但命令名与 Usage 语法保持英文原样 */
static int efm_lang = 0;   /* 内核语言状态: 0=英文, 1=中文 (供 .efs 程序读写) */
#define LANG  (efm_lang)
#define TR(en, zh)  ((LANG == 1) ? (zh) : (en))

static void draw_pixel(unsigned int x, unsigned int y, unsigned int color);  /* 前向声明 (定义在 Graphics 段) */
static void fill_rect(int x, int y, int w, int h, unsigned int color);        /* 前向声明 (定义在 Graphics 段) */
static unsigned int bg;   /* 前向声明: 背景色全局变量 (定义在 Graphics 段) */

/* freestanding 内核没有 libc, 自己实现 int 绝对值 (替代 stdlib.h abs) */
static inline int abs_int(int x) { return (x < 0) ? -x : x; }
#define abs(x)  abs_int(x)

/* ---------- [Graphics.drv 快速拦截层] 前向声明 ----------
 * g_drv_gfx_ptr / drv_attach_gfx_ops / drv_gfx_maybe_flush 等实现在驱动子系统区域 (~line 2010),
 * 但 draw_pixel/fill_rect 等在文件前面就要用. 这里统一前向声明.
 *
 * [Flush 策略 - 2026+ 最终版]
 *  ------------------------------
 * 闪烁根因: 之前每次 fill_rect/draw_char/scroll_up 后都 drv_gfx_maybe_flush() 一次,
 * 导致 wm_composite 合成一帧期间 (会 fill_rect 几千次) 触发几千次 back→front 拷贝,
 * 表现就是高频闪烁 + 画面撕裂 (合成半中间就被 flush 到 front buffer 了).
 *
 * 新策略: 建立 "帧" (Frame) 边界语义:
 *  1. 进入 wm_composite 整帧前  → g_gfx_frame_depth++   (帧内: 禁止自动 flush)
 *  2. 整帧所有绘制完成 (窗口/任务栏/鼠标/光标全到位) 后
 *     → g_gfx_frame_depth-- 直到 0, 若深度回到 0 则 drv_gfx_force_flush() 一次
 *  3. 未进入帧 (早期引导 / 非 WM 模式): 保留操作后 maybe_flush, 保证早期可见
 *  4. 支持 Frame 嵌套 (wm_composite 内部调用的 draw_rect_border/fill_rect 也算本帧内部, 不累加 flush).
 *  ------------------------------ */
#include "drv_common.h"   /* 提供 struct drv_gfx_ops 等定义 */
static const struct drv_gfx_ops  *g_drv_gfx_ptr;   /* 实现在 ~2012 */
static void drv_attach_gfx_ops(const struct drv_gfx_ops *ops); /* 实现在 ~5448 */
/* g_gfx_frame_depth: 进入帧计数, >0 表示 "正在整帧合成中, 先别 flush".
 * 用计数而非 bool 以便 wm_composite → fill_rect → draw_pixel 任意嵌套. */
static int  g_gfx_frame_depth = 0;
/* 帧内是否产生过脏像素 (即有内容写过后缓冲).
 * 若帧内没有任何绘制, 帧尾不必无意义 flush. */
static int  g_gfx_dirty_in_frame = 0;
static inline void drv_gfx_mark_dirty(void);      /* 标记后缓冲被写过, 帧尾需要 flush */
static inline void drv_gfx_mark_dirty_rect(int x1, int y1, int x2, int y2);  /* 手动指定脏矩形 */
static int efs_get_backbuffer(void **out_ptr, int *out_pitch, int *out_w, int *out_h);
static void efs_mark_dirty_rect(int x1, int y1, int x2, int y2);
static void efs_flush_now(void);
static int  efs_draw_char_unicode(int x, int y, unsigned int codepoint,
                                  unsigned int fg, unsigned int bg, int cell_w, int cell_h);
static inline void drv_gfx_maybe_flush(void);     /* 非帧内才 flush (保留 API 兼容) */
static inline void drv_gfx_frame_begin(void);     /* 开始一帧 (depth++) */
static inline void drv_gfx_frame_end(void);       /* 结束一帧 (depth--, 到0时真正 flush 一次) */
static inline void drv_gfx_force_flush(void);     /* 无条件立即 flush (忽略 depth) */

/* [合成器标志早声明] efs_blit_to_window (line ~4527) 需要检查此标志决定是否 flush.
 * 实际定义在 WM 全局变量区 (line ~8533). */
static int g_compositor_active;

/* [WM 标志早声明] draw_cursor() line ~518 需要用到: WM 模式下全局块状光标变 NOP,
 *  否则 cursor_xy (早期遗留值, 通常 0,0) 会把光标画到桌面背景上.
 *  实现在 WM 段 (~line 8220). */
static int g_wm_enabled;

/* ---------- 内核入口 _start ----------
 * 必须是 .text 中第一个函数 (VMA = link.ld 设定的入口地址, 0x200000)。
 * bootloader 加载内核 ELF 后跳转到 ehdr.e_entry (即此处 VMA)。
 *
 * [Early 串口诊断]
 * 在进入 kmain 前后通过 I/O 端口直接写串口 (COM1=0x3F8), 全程只用
 * 寄存器, 不访问全局变量, 不依赖栈深度, 这样即便 mcmodel 错了
 * (全局地址编到 0xFFFFFFFFxxxx) 也能看到诊断输出, 精准定位:
 *   [K] 刚进入 _start (bootloader->kernel 跳转成功)
 *   [S] 栈已设置
 *   [M] 即将 call kmain
 *   [R] kmain 返回了 (不应该发生, kmain 是 noreturn)
 * 缺任何一个字符就说明崩在那两个字母之间的步骤。 */
void kmain(void);   /* 前向声明 (kmain 定义在文件末尾) */

__attribute__((naked, noreturn))
void _start(void) {
    __asm__ volatile(
        /* ---- [K] 刚进入内核, 发字符 'K' ---- */
        "mov $0x3F8, %%dx\n\t"         /* COM1 data port for TX */
        "add $5, %%dx\n\t"             /* LSR = 0x3F8+5 */
        "1: inb %%dx, %%al\n\t"
        "testb $0x20, %%al\n\t"
        "jz 1b\n\t"
        "mov $0x3F8, %%dx\n\t"         /* COM1 TX */
        "mov $'K', %%al\n\t"
        "outb %%al, %%dx\n\t"

        /* 设置栈: 16MB 顶 (避开内核 2MB VMA 与低内存) */
        "movabs $0x1000000, %%rsp\n\t"
        "xor %%rbp, %%rbp\n\t"
        "and $-16, %%rsp\n\t"

        /* ---- [S] 栈设置完毕, 发字符 'S' ---- */
        "mov $0x3F8+5, %%dx\n\t"
        "2: inb %%dx, %%al\n\t"
        "testb $0x20, %%al\n\t"
        "jz 2b\n\t"
        "mov $0x3F8, %%dx\n\t"
        "mov $'S', %%al\n\t"
        "outb %%al, %%dx\n\t"

        /* ---- [M] 即将 call kmain, 发字符 'M' ---- */
        "mov $0x3F8+5, %%dx\n\t"
        "3: inb %%dx, %%al\n\t"
        "testb $0x20, %%al\n\t"
        "jz 3b\n\t"
        "mov $0x3F8, %%dx\n\t"
        "mov $'M', %%al\n\t"
        "outb %%al, %%dx\n\t"

        "call kmain\n\t"

        /* ---- [R] kmain 返回 (异常), 发 'R' 然后死循环 ---- */
        "mov $0x3F8+5, %%dx\n\t"
        "4: inb %%dx, %%al\n\t"
        "testb $0x20, %%al\n\t"
        "jz 4b\n\t"
        "mov $0x3F8, %%dx\n\t"
        "mov $'R', %%al\n\t"
        "outb %%al, %%dx\n\t"
        "cli\n\t"
        "5: hlt\n\t"
        "jmp 5b\n\t"
    ::: "memory", "rax", "rdx", "rcx"
    );
    __builtin_unreachable();
}

/* ---------- I/O & Serial ---------- */
static void outb(unsigned short port, unsigned char val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
static unsigned char inb(unsigned short port) { unsigned char val; asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port)); return val; }
static unsigned short inw(unsigned short port) { unsigned short val; asm volatile("inw %1, %0" : "=a"(val) : "Nd"(port)); return val; }
static void outw(unsigned short port, unsigned short val) { asm volatile("outw %0, %1" : : "a"(val), "Nd"(port)); }
static void outl(unsigned short port, unsigned int val) { asm volatile("outl %0, %1" : : "a"(val), "Nd"(port)); }
static unsigned int inl(unsigned short port) { unsigned int val; asm volatile("inl %1, %0" : "=a"(val) : "Nd"(port)); return val; }

static inline void serial_putc(char c) { while (!(inb(0x3FD) & 0x20)); outb(COM1_PORT, (unsigned char)c); }
static void serial_write(const char *s) { while (*s) serial_putc(*s++); }
static void serial_put_dec(int v) {
    char buf[16]; int i = 0;
    if (v < 0) { serial_putc('-'); v = -v; }
    if (v == 0) { serial_putc('0'); return; }
    while (v > 0 && i < 15) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}
static void serial_write_hex(unsigned long long v) {
    char h[] = "0123456789ABCDEF";
    char buf[17];
    for (int i = 15; i >= 0; i--) buf[15-i] = h[(v >> (i*4)) & 0xF];
    buf[16] = 0;
    serial_write(buf);
}
static char serial_getchar(void) { while (!(inb(0x3FD) & 0x01)); return inb(COM1_PORT); }

/* ---------- Graphics ---------- */
/* [鼠标光标 save 坐标] 提前定义 (含初始化) 以便 fill_screen 可重置它们.
 *   -1 = 未保存任何背景. 原位置在 8xx 的光标渲染节内 */
static int s_save_x = -1, s_save_y = -1;

static void draw_pixel(unsigned int x, unsigned int y, unsigned int color) {
    /* [GFX 驱动优先] 注册后走 Graphics.drv 的双缓冲+像素格式转换, 避免直接写 front 造成闪烁 */
    if (g_drv_gfx_ptr && g_drv_gfx_ptr->put_pixel) {
        g_drv_gfx_ptr->put_pixel((int)x, (int)y, color);
        drv_gfx_mark_dirty();
        return;
    }
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb->fb_base || x >= fb->hr || y >= fb->vr) return;
    volatile unsigned int *fbuf = (unsigned int*)(unsigned long)fb->fb_base;
    fbuf[y * fb->ppsl + x] = color;
}
static void fill_screen(unsigned int color) {
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb->fb_base) return;
    if (g_drv_gfx_ptr && g_drv_gfx_ptr->fill_rect) {
        /* 用驱动 fill_rect (后缓冲), 由 frame_end 或显式 maybe_flush 统一刷 */
        g_drv_gfx_ptr->fill_rect(0, 0, (int)(fb->hr - 1), (int)(fb->vr - 1), color);
        drv_gfx_mark_dirty();
        drv_gfx_maybe_flush();
    } else {
        for (unsigned int y = 0; y < fb->vr; y++)
            for (unsigned int x = 0; x < fb->hr; x++)
                draw_pixel(x, y, color);
    }
    /* 全屏填色后, 鼠标背景保存的像素全部失效, 必须作废否则下一次
     *   draw_mouse_cursor 的 restore 会把旧像素写回来产生白点 */
    s_save_x = s_save_y = -1;
}
static unsigned int bg, fg;
/* [纯 TTF] 所有字符渲染通过 Graphics.drv 的 draw_char_unicode 完成.
 * 不再包含位图字体回退, 确保字体统一无闪烁. */
static void draw_char(int x, int y, char ch, unsigned int color) {
    if (g_drv_gfx_ptr && g_drv_gfx_ptr->draw_char_unicode) {
        int rc = g_drv_gfx_ptr->draw_char_unicode(x, y, (unsigned int)(unsigned char)ch,
                                                   color, bg, FONT_W, FONT_H);
        if (rc > 0) drv_gfx_mark_dirty();
    }
}

/* ---------- 5x7 像素位图字体 (用于启动 logo 大字渲染) ----------
 * 每个字母 5 列 × 7 行, 每行用 1 字节 (bit4..bit0 = 列4..列0)
 * TTF 驱动加载前无法用 draw_char_unicode, 且需要大号字体, 这里自带的
 * 位图字体可缩放到任意大小. 字符表索引: E=0,F=1,M=2,O=3,S=4,v=5,1=6,0=7,dot=8 */
static const unsigned char font5x7[9][7] = {
    /* E: 0 */  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
    /* F: 1 */  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
    /* M: 2 */  { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 },
    /* O: 3 */  { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    /* S: 4 */  { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E },
    /* v: 5 */  { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04 },
    /* 1: 6 */  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    /* 0: 7 */  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
    /* .: 8 */  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 },
};

/* 用位图字体绘制一个字符, 可指定缩放倍数 (1px → scale×scale 像素块) */
static void draw_bitmap_char(int x, int y, int ch_idx, int scale,
                             unsigned int color) {
    if (ch_idx < 0 || ch_idx >= 9) return;
    for (int row = 0; row < 7; row++) {
        unsigned char bits = font5x7[ch_idx][row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                int px = x + col * scale;
                int py = y + row * scale;
                fill_rect(px, py, scale, scale, color);
            }
        }
    }
}

/* [EFMOS Logo] 左侧: 用户上传的系统图标 (bootlogo.h alpha 图)
 *              右上: 白色 "EFMOS" 大字 (5x7 位图字体缩放)
 *              右下: 小字 "v1.0.0" 版本号
 * 参数 scale: 保留 (自动根据屏幕计算最优缩放)
 * 返回: logo 占用的总像素高度 (供调用者计算后续文本起始 y) */
static int draw_efmos_logo(int scale) {
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb->fb_base) return 0;

    /* 背景色 (深靛色 0x1A1A2E) + 白色前景 + 灰色版本号 */
    unsigned int bg_r = 0x1A, bg_g = 0x1A, bg_b = 0x2E;
    unsigned int bg_u32 = (bg_r << 16) | (bg_g << 8) | bg_b;
    unsigned int white = 0x00FFFFFF;
    unsigned int gray  = 0x00A8B0CC;   /* 浅灰紫, 比白淡但清晰 */

    int iw = LOGO_W, ih = LOGO_H;
    int sw = (int)fb->hr, sh = (int)fb->vr;

    /* --- 计算图标尺寸: 屏幕高度的 25%, 不超过 150px --- */
    int icon_target = sh * 25 / 100;
    if (icon_target > 150) icon_target = 150;
    if (icon_target < 40) icon_target = 40;
    int icon_sx = icon_target / iw;
    if (icon_sx < 1) icon_sx = 1;
    int dw = iw * icon_sx, dh = ih * icon_sx;

    /* --- 计算 "EFMOS" 大字尺寸 --- */
    int text_scale = (dh * 70 / 100) / 7;
    if (text_scale < 2) text_scale = 2;
    if (text_scale > 12) text_scale = 12;
    int char_w = 5 * text_scale;
    int char_h = 7 * text_scale;
    int char_gap = text_scale;
    int total_text_w = 5 * char_w + 4 * char_gap;
    int total_text_h = char_h;

    /* --- 计算 "v1.0.0" 小字尺寸 (字号为大字的 30%, 最小 1px) --- */
    int ver_scale = text_scale * 30 / 100;
    if (ver_scale < 1) ver_scale = 1;
    int ver_cw = 5 * ver_scale;
    int ver_ch = 7 * ver_scale;
    int ver_gap = ver_scale;
    /* "v1.0.0" = 6 chars: v,1,.,0,.,0 */
    static const int ver_chars[6] = {5,6,8,7,8,7};
    int total_ver_w = 6 * ver_cw + 5 * ver_gap;
    int total_ver_h = ver_ch;
    int ver_margin = text_scale;   /* 大字底部与版本号之间间距 */

    /* --- 右侧高度 = 大字 + 间距 + 版本号 --- */
    int right_block_h = total_text_h + ver_margin + total_ver_h;

    /* --- 布局: 图标 + 间距 + (大字+小字块), 整体居中 --- */
    int gap = dw / 4;
    if (gap < 12) gap = 12;
    int total_w = dw + gap + total_text_w;
    if (total_ver_w > total_text_w) total_w = dw + gap + total_ver_w;
    int total_h = (dh > right_block_h) ? dh : right_block_h;
    if (total_w > sw) total_w = sw;

    int start_x = (sw > total_w) ? (sw - total_w) / 2 : 0;
    int start_y = sh * 8 / 100;
    if (start_y < 8) start_y = 8;

    int icon_x = start_x;
    int icon_y = start_y + (total_h - dh) / 2;
    /* 右侧文字块: 右对齐到 total_w 右边界 (大字占 full total_text_w) */
    int text_x = start_x + total_w - total_text_w;
    int text_y = start_y + (total_h - right_block_h) / 2;
    int ver_x  = start_x + total_w - total_ver_w;
    int ver_y  = text_y + total_text_h + ver_margin;

    /* --- 填充整个 logo 区域背景色 --- */
    fill_rect(start_x, start_y, total_w, total_h, bg_u32);

    /* --- 渲染图标 (双线性插值 alpha 图) --- */
    for (int y = 0; y < dh; y++) {
        unsigned int fy_q16 = (dh > 1) ? ((unsigned int)y * (unsigned int)(ih - 1) * 65536U) / (unsigned int)(dh - 1) : 0;
        int sy0 = (int)(fy_q16 >> 16);
        int sy1 = (sy0 + 1 < ih) ? sy0 + 1 : sy0;
        unsigned int wy  = (fy_q16 & 0xFFFF) >> 8;
        for (int x = 0; x < dw; x++) {
            unsigned int fx_q16 = (dw > 1) ? ((unsigned int)x * (unsigned int)(iw - 1) * 65536U) / (unsigned int)(dw - 1) : 0;
            int sx0 = (int)(fx_q16 >> 16);
            int sx1 = (sx0 + 1 < iw) ? sx0 + 1 : sx0;
            unsigned int wx  = (fx_q16 & 0xFFFF) >> 8;

            unsigned int a00 = boot_logo_alpha[sy0 * LOGO_W + sx0];
            unsigned int a10 = boot_logo_alpha[sy0 * LOGO_W + sx1];
            unsigned int a01 = boot_logo_alpha[sy1 * LOGO_W + sx0];
            unsigned int a11 = boot_logo_alpha[sy1 * LOGO_W + sx1];

            unsigned int top = a00 * (256 - wx) + a10 * wx;
            unsigned int bot = a01 * (256 - wx) + a11 * wx;
            unsigned int val = (top * (256 - wy) + bot * wy) / 65536U;
            if (val == 0) continue;

            unsigned int a = val;
            unsigned int r = (bg_r * (255 - a) + 255U * a + 127) / 255;
            unsigned int g = (bg_g * (255 - a) + 255U * a + 127) / 255;
            unsigned int b = (bg_b * (255 - a) + 255U * a + 127) / 255;
            unsigned int color = (r << 16) | (g << 8) | b;

            draw_pixel((unsigned int)(icon_x + x), (unsigned int)(icon_y + y), color);
        }
    }

    /* --- 渲染 "EFMOS" 白色大字 (右对齐) --- */
    for (int i = 0; i < 5; i++) {
        draw_bitmap_char(text_x + i * (char_w + char_gap), text_y, i, text_scale, white);
    }

    /* --- 渲染 "v1.0.0" 灰色小字 (大字右下角, 右对齐) --- */
    for (int i = 0; i < 6; i++) {
        draw_bitmap_char(ver_x + i * (ver_cw + ver_gap), ver_y, ver_chars[i], ver_scale, gray);
    }

    drv_gfx_maybe_flush();
    return start_y + total_h + 8;
}
static int cursor_x = 0, cursor_y = 0;
static unsigned int bg = 0x001A1A2E, fg = 0x00FFFFFF;

/* [新增] 闪烁光标: 在 (cursor_x, cursor_y) 绘制/擦除块状光标。
 * [WM 模式修正] 当 g_wm_enabled=1 时, 所有窗口的光标归属各窗口内容区,
 *   由 wm_composite 在窗口内容中绘制 (c_cx / c_cy + 窗口偏移), 全局 draw_cursor
 *   必须退化为 NOP, 否则 cursor_x/y 是旧值 (如 0,0), 会把光标画到桌面背景.
 *   - 不 WM 模式 (早期引导/切换到非 WM): 保留原行为 (cursor_xy 全局块状). */
static int cursor_visible = 0;   /* 0=隐藏 1=显示 */
static void draw_cursor(int show) {
    /* [WM 模式] 禁用全局块状光标绘制 (光标由 wm_composite 画在各 focus 窗口内部) */
    if (g_wm_enabled) return;
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb->fb_base) return;
    unsigned int color = show ? fg : bg;
    /* 块状光标: FONT_W x FONT_H 像素, 位于当前光标位置 */
    for (int row = 0; row < FONT_H; row++)
        for (int col = 0; col < FONT_W; col++)
            draw_pixel(cursor_x + col, cursor_y + row, color);
    cursor_visible = show;
}
static void hide_cursor(void) { if (cursor_visible) draw_cursor(0); }
static void show_cursor(void) { if (!cursor_visible) draw_cursor(1); }

static void scroll_up(void) {
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb->fb_base) return;
    /* [桌面脱离内核] WM/合成器激活时, 滚动由 efmAether 通过 tbuf 重绘处理,
     * 内核不再直接滚动帧缓冲, 避免与桌面渲染冲突. */
    if (g_wm_enabled || g_compositor_active) return;
    /* [GFX 驱动优先] Graphics.drv 的 scroll_up 用 32-bit burst 行移动 + 后缓冲,
     * 比逐像素快得多, 也不会闪烁. flush 推迟到 frame_end 或非帧内立即刷. */
    if (g_drv_gfx_ptr && g_drv_gfx_ptr->scroll_up) {
        g_drv_gfx_ptr->scroll_up(FONT_H);
        drv_gfx_mark_dirty();
        drv_gfx_maybe_flush();
        return;
    }
    unsigned int *fbuf = (unsigned int*)(unsigned long)fb->fb_base;
    for (unsigned int y = FONT_H; y < fb->vr; y++)
        for (unsigned int x = 0; x < fb->hr; x++)
            fbuf[(y-FONT_H)*fb->ppsl + x] = fbuf[y*fb->ppsl + x];
    for (unsigned int y = fb->vr-FONT_H; y < fb->vr; y++)
        for (unsigned int x = 0; x < fb->hr; x++)
            fbuf[y*fb->ppsl + x] = bg;
}
/* UTF-8 累积状态: put_char 逐字节接收, 检测 3 字节 CJK 序列 */
static int utf8_pos = 0;
static unsigned char utf8_buf[3];

/* [WM] 防止 put_char 重定向 → wm_window_putc → wm_composite → draw_char 死递归的保护标志 */
static volatile int g_wm_in_render = 0;
/* [WM] 画面脏标记: 1=窗口内容有更新需要合成, 0=最近已合成过无变化。
 *   - put_char 重定向到窗口 / wm_window_putc 写入后: 仅置 1 (不再立即合成, 避免字符合成风暴)
 *   - wm_handle_mouse: 立即合成 + 清零
 *   - idle 轮询节拍: 若 dirty=1 则合成 + 清零; 若 idle=0 则跳过 */
static volatile int g_wm_dirty = 0;
/* [WM] EFS 像素级绘制 API 的脏序列计数器 (给 Compositor 做图形变化脏检测).
 *   - efs_put_pixel / efs_fill_rect_api / efs_draw_rect_api 的 WM 分支每次真正写入
 *     back buffer 时 +1, 单调递增 (回绕仍有效, 只要 != 旧值就表示有变化).
 *   - Compositor 每帧对比快照的 gfx_dirty_seq, 变化时强制重绘窗口内容区
 *     (从 back buffer 把 EFS 图形像素拷回 canvas), 解决"合成器脏检测漏检
 *     EFS 图形变化 → canvas blit 覆盖 EFS 程序画的按钮"问题。 */
static volatile unsigned int g_wm_gfx_dirty_seq = 0;

/* ---- WM 前向声明 (WM 代码在文件后半部分, put_char/efs_* 需提前引用) ---- */
struct wm_window;
static struct wm_window *wm_find_by_pid(int pid);
static void wm_window_putc(struct wm_window *w, char c);
static void wm_composite(void);
static void wm_window_clear_row(struct wm_window *w, int row);
static void wm_window_advance(struct wm_window *w, int n);
static void wm_window_set_has_efm_gfx(struct wm_window *w, int v);
static int g_wm_enabled;  /* bool: 窗口管理器已启用 */

/* ---- EFS 参数缓冲: 暂定义 (完整定义在 4805 附近) ---- */
static char efs_argbuf[1024];
static int  efs_arglen;

/* ---- 调度/任务/多核/任务相关 前向声明 ---- */
struct task_struct;
static void sched_yield(void);
static int lapic_timer_check(void);
static void task_exit(unsigned long code);
static struct task_struct *task_create(const char *name, void (*entry)(void*), void *arg, int kstack_pages);
static int wm_create_window(const char *title, int pid, int w, int h, int *out_wid);
static int wm_handle_mouse(void);
static void wm_init(void);
/* [2026+ 调度优化] 新增调度相关: 睡眠/唤醒/优先级调整 + spawn_async 包装 */
static void task_sleep_ms(unsigned long ms);
static int  task_set_priority(int pid, int nice);
static int  efs_spawn_async_with_args(const char *name, const char *args);
/* Mesa 合成器 API (实现在 WM 段之后, 此处前向声明) */
static int  efs_get_wm_snapshot(void *out, int max_bytes);
static void efs_set_compositor_active(int active);
static void *efs_dlsym(const char *name);
static void efs_set_gfx_info(void *info);
/* EFS 全局状态 保存到 当前任务 / 从新任务 恢复 (调度切换时使用) */
static void efs_state_save_current(void);   /* 全局 -> g_current->efs */
static void efs_state_restore_to(struct task_struct *t);  /* t->efs -> 全局 + api->current_pid */
static void lapic_write(unsigned int reg, unsigned int val);  /* LAPIC MMIO 写 (原 static inline 改普通声明) */
static void efs_setup_api_table(void);                      /* 给 EFS 设置私有 API 表 */

/* ---- 封装: 访问 struct task_struct/wm_window 成员的辅助函数
 *        (定义在文件后半, struct 定义之后; 让前半代码能使用不完整类型) ---- */
static void task_print_ps_all(void);     /* ps 命令的全部输出 (循环打印每个任务) */
static void wm_print_status_all(void);   /* wminfo 命令的全部输出 */
static int  wm_window_cy(struct wm_window *w);   /* return w->c_cy */
static void wm_window_set_cx(struct wm_window *w, int cx);  /* w->c_cx = cx */
static int  wm_window_content_is_cjk_width_at(struct wm_window *w, int cx_px, int cy_px);
/* [前向声明] draw_cursor 等早期函数需要先知道 WM 是否启用，从而禁用全局块状光标绘制。
 * 真正定义在 line ~8220 (WM 段)。重复 tentative (C 允许 tentative 定义多次)。 */
static int g_wm_enabled;

/* ---- g_current / task_struct 访问器 (避免前半代码使用未定义全局) ---- */
static int  current_is_valid(void);                     /* g_current != NULL */
static int  current_get_pid(void);                      /* g_current ? g_current->pid : -1 */
static int  current_timeslice_dec_and_check(void);      /* if (g_current && >0) --, return >0 after decr */
static int  task_ptr_get_pid(struct task_struct *t);    /* t->pid  (efs_spawn_async 用) */
static int  wm_window_exists_for_pid(int pid);          /* 替代 wm_find_by_pid_exists 的循环 */
/* fill_rect 前向声明已在 line 124, 此处避免重复 */
/* WM 内容区几何访问器 (wm_content_* 实现在 WM 段 struct 之后, 供像素 API 用) */
static int wm_content_x(struct wm_window *w);
static int wm_content_y(struct wm_window *w);
static int wm_content_w(struct wm_window *w);
static int wm_content_h(struct wm_window *w);
/* WM 窗口按 pid 查找 (返回指针, 像素 API 要访问窗口属性) */
static struct wm_window *wm_find_by_pid(int pid);

static void put_char(char c) {
    /* [WM 重定向] 启用窗口管理器后, 内核 Shell 的 put_char 输出重定向到 pid=1 的 Shell 窗口
     *  前提: WM 已启用 + 未在 WM 合成渲染路径中 (防递归) + Shell 窗口存在
     *  [性能修复] 仅置 dirty=1, 不立即合成, 避免每个字符触发一次全屏绘制。 */
    if (g_wm_enabled && !g_wm_in_render) {
        struct wm_window *sw = wm_find_by_pid(1);
        if (sw) {
            wm_window_putc(sw, c);
            g_wm_dirty = 1;
            return;
        } else {
            /* [修复3a 安全回落] WM 已启用但找不到 Shell 窗口 (罕见的初始化切换瞬间).
             * [桌面脱离内核] 合成器激活时, 不写帧缓冲 (避免与 efmAether 冲突),
             * 仅丢弃输出. 合成器未激活时走旧的"桌面临时通知带"路径. */
            if (g_compositor_active) return;
            struct gop_fb *fb = (struct gop_fb*)0x1000;
            if (fb && fb->fb_base) {
                int limit_y = (int)fb->vr - WM_TASKBAR_H - FONT_H;
                if (limit_y < 0) limit_y = 0;
                if (cursor_y != limit_y) cursor_y = limit_y;
                int was_vis = cursor_visible;
                if (was_vis) draw_cursor(0);
                if (c == '\n') { cursor_x = 0; /* 停留在同一行, 不清, 仅回列首 */ }
                else {
                    if (cursor_x + FONT_W > (int)fb->hr) {
                        /* 行满: 清行回到列首再画 */
                        fill_rect(0, cursor_y, fb->hr, FONT_H, 0x00161626);
                        cursor_x = 0;
                    }
                    draw_char(cursor_x, cursor_y, c, 0x00FFFFFF);
                    cursor_x += FONT_W;
                }
                if (was_vis) draw_cursor(1);
                /* 不落回全局 fb 原始 scroll_up 路径: 直接 return */
                return;
            }
        }
    }

    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb->fb_base) return;
    int was_vis = cursor_visible;
    if (was_vis) draw_cursor(0);

    /* UTF-8 3 字节序列检测 (CJK: 0xE0-0xEF 开头) */
    if ((unsigned char)c >= 0xE0 && (unsigned char)c <= 0xEF) {
        utf8_pos = 1; utf8_buf[0] = (unsigned char)c;
        if (was_vis) draw_cursor(1);
        return;
    }
    if (utf8_pos == 1 && (unsigned char)c >= 0x80 && (unsigned char)c <= 0xBF) {
        utf8_pos = 2; utf8_buf[1] = (unsigned char)c;
        if (was_vis) draw_cursor(1);
        return;
    }
    if (utf8_pos == 2 && (unsigned char)c >= 0x80 && (unsigned char)c <= 0xBF) {
        utf8_buf[2] = (unsigned char)c;
        utf8_pos = 0;
        /* 解码 3 字节 UTF-8 → Unicode 码点 */
        unsigned int codepoint = ((unsigned int)(utf8_buf[0] & 0x0F) << 12) |
                                 ((unsigned int)(utf8_buf[1] & 0x3F) << 6) |
                                  (unsigned int)(utf8_buf[2] & 0x3F);
        /* [TTF 优先] 驱动支持 Unicode 渲染时直接用 TTF 字体 */
        if (g_drv_gfx_ptr && g_drv_gfx_ptr->draw_char_unicode) {
            if (cursor_x + 2*FONT_W > fb->hr) {
                cursor_x = 0; cursor_y += FONT_H;
                if (cursor_y+FONT_H > fb->vr) { scroll_up(); cursor_y = fb->vr-FONT_H; }
            }
            int rc = g_drv_gfx_ptr->draw_char_unicode(cursor_x, cursor_y, codepoint,
                                                       fg, bg, 2*FONT_W, FONT_H);
            if (rc > 0) {
                drv_gfx_mark_dirty();
                cursor_x += 2*FONT_W;
                if (cursor_x + FONT_W > fb->hr) {
                    cursor_x = 0; cursor_y += FONT_H;
                    if (cursor_y+FONT_H > fb->vr) { scroll_up(); cursor_y = fb->vr-FONT_H; }
                }
                if (was_vis) draw_cursor(1);
                return;
            }
        }
        /* [纯 TTF] CJK 字符渲染失败时不回退到位图字体, 直接跳过. */
        if (was_vis) draw_cursor(1);
        return;
    }
    /* 非 UTF-8 续字节但有未完成序列 → 放弃 */
    utf8_pos = 0;

    if (c == '\n') { cursor_x = 0; cursor_y += FONT_H; if (cursor_y+FONT_H > fb->vr) { scroll_up(); cursor_y = fb->vr-FONT_H; } if (was_vis) draw_cursor(1); return; }
    draw_char(cursor_x, cursor_y, c, fg); cursor_x += FONT_W;
    if (cursor_x+FONT_W > fb->hr) { cursor_x = 0; cursor_y += FONT_H; if (cursor_y+FONT_H > fb->vr) { scroll_up(); cursor_y = fb->vr-FONT_H; } }
    if (was_vis) draw_cursor(1);
}

static void print_string(const char *s) { while (*s) put_char(*s++);
    /* [纯 Shell · 双缓冲修复] 非 WM 且非合成器时, draw_char/TTF 写的是
     * Graphics.drv back buffer, 必须显式 flush 到 front buffer 才可见。
     * 之前桌面模式由 wm_composite/合成器定期 flush; 取消桌面后没有合成者,
     * 导致 welcome/prompt 文字停留在 back buffer, 屏幕像是"没有正常进 shell"。
     * 这里在整串输出后统一 flush (比逐字 flush 高效), 覆盖所有 print_string 调用。 */
    if (!g_wm_enabled && !g_compositor_active) drv_gfx_maybe_flush();
}

/* [efmshell] 计算字符串显示宽度 (列数): ASCII=1, CJK/扩展=2.
 * 供增强行编辑引擎 (efs_readline_ext) 计算 CJK prompt/输入的光标像素位置,
 * 避免按字节计数导致光标错位 (旧 bug: strlen 把中文每字节算 1 列)。 */
static int str_display_width(const char *s, int bytes) {
    int w = 0; int i = 0;
    while (i < bytes && s[i]) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80)      { w += 1; i += 1; }
        else if ((c & 0xE0) == 0xC0) { w += 2; i += 2; }   /* 2-byte CJK/latin-ext */
        else if ((c & 0xF0) == 0xE0) { w += 2; i += 3; }   /* 3-byte CJK (常见中文) */
        else if ((c & 0xF8) == 0xF0) { w += 2; i += 4; }   /* 4-byte 扩展区按 2 宽算 */
        else                       { w += 1; i += 1; }
    }
    return w;
}

/* ---------- 2D 绘图辅助 (矩形/直线) ---------- */
/* fill_rect 前向声明已在 line 124, 此处避免重复 */
static void draw_line(int x0, int y0, int x1, int y1, unsigned int color);

static void draw_line(int x0, int y0, int x1, int y1, unsigned int color) {
    /* Bresenham line algorithm */
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        draw_pixel((unsigned int)x0, (unsigned int)y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void fill_rect(int x, int y, int w, int h, unsigned int color) {
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb || !fb->fb_base) return;
    if (w <= 0 || h <= 0) return;
    /* [GFX 驱动优先] Graphics.drv 的 fill_rect: 32-bit burst + 双缓冲 (不会逐像素写到 front)
     * 注意: fill_rect 的参数是 x,y,w,h; 驱动 fill_rect 是 x1,y1,x2,y2 (inclusive) */
    if (g_drv_gfx_ptr && g_drv_gfx_ptr->fill_rect) {
        int x1 = x, y1 = y;
        int x2 = x + w - 1, y2 = y + h - 1;
        if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
        if ((unsigned int)x2 >= fb->hr) x2 = (int)fb->hr - 1;
        if ((unsigned int)y2 >= fb->vr) y2 = (int)fb->vr - 1;
        if (x1 <= x2 && y1 <= y2) {
            g_drv_gfx_ptr->fill_rect(x1, y1, x2, y2, color);
            drv_gfx_mark_dirty();
        }
        return;
    }
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > (int)fb->hr) x1 = (int)fb->hr;
    if (y1 > (int)fb->vr) y1 = (int)fb->vr;
    unsigned int *base = (unsigned int*)(unsigned long)fb->fb_base;
    int pitch = (int)fb->ppsl;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            base[yy * pitch + xx] = color;
}
static void draw_rect_border(int x, int y, int w, int h, unsigned int color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}
/* 在指定 (x,y) 以颜色 fg 输出一个 ASCII 字符 (不清除整行背景, 只清字符块) */
static void draw_char_at(int x, int y, char ch, unsigned int c_fg, unsigned int c_bg) {
    unsigned int old_bg = bg; bg = c_bg;
    /* [纯 TTF] 所有字符通过 Graphics.drv 的 draw_char_unicode 渲染 */
    if (g_drv_gfx_ptr && g_drv_gfx_ptr->draw_char_unicode) {
        int rc = g_drv_gfx_ptr->draw_char_unicode(x, y, (unsigned int)(unsigned char)ch,
                                                   c_fg, c_bg, FONT_W, FONT_H);
        if (rc > 0) drv_gfx_mark_dirty();
    }
    bg = old_bg;
}
/* 在 (x,y) 输出字符串 (ASCII 为主), 遇到 CJK 字符跳过或简化处理 */
static void draw_text_at(int x, int y, const char *s, unsigned int c_fg, unsigned int c_bg) {
    int cx = x;
    while (*s) {
        unsigned char uc = (unsigned char)*s;
        if (uc >= 0xE0 && uc <= 0xEF) { /* UTF-8 CJK 3-byte: 简单跳过, 避免错位 */
            if (s[1] && s[2]) s += 3; else break;
            continue;
        }
        if (uc >= 0x80) { s++; continue; }
        draw_char_at(cx, y, (char)uc, c_fg, c_bg);
        cx += FONT_W;
        s++;
    }
}

/* ---------- Keyboard ----------
 * [完善] 支持: 退格, 回车, Esc, Tab, 方向键 (0xE0 前缀), Shift 上档,
 *           Ctrl+L 清屏, 大小写锁定 (Caps)
 * 返回 >=0x20 为可打印 ASCII; 负值为特殊键代码 (见 KEY_*)。
 */
#define KEY_BACKSPACE  -1   /* 0x0E */
#define KEY_TAB        -2   /* 0x0F */
#define KEY_ENTER      -3   /* 0x1C */
#define KEY_ESC        -4   /* 0x01 */
#define KEY_UP         -5
#define KEY_DOWN       -6
#define KEY_LEFT       -7
#define KEY_RIGHT      -8
#define KEY_HOME       -9
#define KEY_END        -10
#define KEY_DELETE     -11

/* =======================================================================
 * [关键修复 4.0 — FINAL] 键盘终极可用 + 鼠标光标无鬼影渲染
 * =======================================================================
 * 一、键盘为什么"永远打不出字"的真实根因 (连续 3 轮对话的凶手):
 *   1. 修复 2.x 把 8042 配置寄存器的 bit5 (Translate) 关掉了:
 *        cfg &= ~0x30   // 把 keyboard/mouse 的 Set2→Set1 硬件翻译都禁了 !
 *      结果 USB→PS2 桥 (QEMU/真实机都用它) 输出的是 Set 2 扫描码,
 *      Set 2 普通键 make 例如 'a' = 0x1C? — 不! Set 2 'a' make = 0x1C — 看起来一样.
 *      但 release 码 Set 2 是 "0xF0 + make", 且 Set 2 的数字/字母码值跟 Set 1 差很多!
 *      例: Set 2 空格键 make = 0x29 (不是 Set 1 的 0x39)
 *           Set 2 'M' make  = 0x3A (不是 Set 1 的 0x32)
 *           Set 2 '[' make  = 0x54 (不是 Set 1 的 0x1A)
 *      → 我们的 lower[] 是按 Set 1 码值写的, 碰到 Set 2 码全部查不到.
 *      → 用户以为"打不出字", 实际上按键都被 kb_scan_to_key 当"非白名单" return 0 了.
 *
 *   2. poll_inputs 空轮询: 用"队列 + 一次性清空缓冲"的非阻塞模式替代阻塞读,
 *      结果要么 OBF=0 被 `return;` 早退 (键盘按键按了没来得及清 OBF 就被跳过),
 *      要么扩展扫描码 0xE0 后续字节排队超时 200 轮还没凑齐就直接丢.
 *
 *   3. 鼠标 3 字节组装跟键盘输入抢 8042 读通道, 造成状态机不同步.
 *
 * 现在的最终方案 (回归简单, 拒绝架构花哨):
 *   KEYBOARD:
 *   ┌ 重新启用 8042 硬件翻译 (cfg | 0x20 不要清 bit5)
 *   ├ kb_read_raw_blocking(): 阻塞读, 条件严格: OBF=1, AUX=0, PE=0, TO=0
 *   │   内部用 s_drain_mouse_all() 先把 AUX=1 的字节全部抽走 (入鼠标包组装)
 *   │   读到 0x00/0xAA/0xEE/0xFA/0xFC/0xFD/0xFE/0xFF 伪码直接继续循环
 *   └ kbdq_pop(): 取出完整逻辑扫描码 (Set1 扩展永远是 0xE0+1byte)
 *        0xF0 release 前缀 (万一硬件翻译失败漏进来) 兼容处理
 *
 *   MOUSE:
 *   ┌ s_drain_mouse_all(): 每次读键盘前, 把 8042 缓冲里所有 AUX=1 字节抽干净入队
 *   ├ mouse_poll(): 也先 drain_mouse_all 再出队最新事件
 *   └ 光标渲染: 16x16 像素背景保存 (s_cursor_save[sx,sy,16x16])
 *        hide_mouse_cursor() → 用 saved 像素把 fb 写回 (完美擦除, 无残留)
 *        draw_mouse_cursor() → 先 hide, 再保存新 16x16, 再画箭头
 *        → 解决"鼠标移动后在文字上留鬼影, 看起来像渲染出问题"的用户反馈
 * =====================================================================*/

/* ---------- 8042 I/O 辅助: 写命令/数据并等待 IBF 清空 ---------- */
static void ps2_wait_ibf(void) {
    for (int i = 0; i < 200000; i++) {
        if (!(inb(0x64) & 0x02)) return;
        asm volatile("pause");
    }
}
static unsigned char ps2_wait_obf(void) {
    for (int i = 0; i < 400000; i++) {
        unsigned char st = inb(0x64);
        if (st & 0x01) {
            /* 初始化握手阶段, ACK 都是键盘侧响应 (AUX=0), 直接读 */
            return inb(0x60);
        }
        asm volatile("pause");
    }
    return 0xFC;
}
static void ps2_cmd(unsigned char cmd) {
    ps2_wait_ibf(); outb(0x64, cmd);
}
static void ps2_data(unsigned char d) {
    ps2_wait_ibf(); outb(0x60, d);
}
/* 写命令到 AUX (鼠标): 先发 0xD4 to 0x64, 再发 data to 0x60 */
static void ps2_aux_write(unsigned char d) {
    ps2_wait_ibf(); outb(0x64, 0xD4);
    ps2_wait_ibf(); outb(0x60, d);
}

/* ---------- 鼠标 3 字节组装 & 事件队列 (独立, 不影响键盘) ---------- */
struct mouse_event {
    int dx, dy;
    unsigned char btn;
    int x, y;
};
#define MOUSEQ_SIZE 64
static volatile struct mouse_event mouseq[MOUSEQ_SIZE];
static volatile int mouseq_head, mouseq_tail;
static int s_mx = 320, s_my = 240;
/* [鼠标拖尾修复] 上一次画鼠标的绝对坐标 (s_prev_mx, s_prev_my).
 *  下一帧 wm_composite 合成 Phase 1 时, 会先把 s_prev_mx,s_prev_my 下 16x16 区域
 *  从 back buffer 用下层像素正确重写 (实际 wm_composite 总是重写整个桌面+窗口,
 *  但 back buffer flush 是按脏矩形拷贝的 — 旧鼠标如果在"非脏区域", flush 不拷 → 拖尾!
 *  所以关键是: 把 (s_prev_mx,s_prev_my) 和 (s_mx,s_my) 两个 16x16 矩形都强制放进脏矩形,
 *  确保 flush 覆盖 front buffer 上的旧鼠标. */
static int s_prev_mx = -1, s_prev_my = -1;
static int s_mouse_visible = 0;
static unsigned char s_mbuf[3];
static int s_midx = 0;
static int s_mwatchdog = 0;   /* 组装看门狗: 超过阈值未完成包则复位, 防止永久失步 */

static inline int mouseq_full(void)  { return ((mouseq_head + 1) % MOUSEQ_SIZE) == mouseq_tail; }
static inline int mouseq_empty(void) { return mouseq_head == mouseq_tail; }
static void mouseq_push(const struct mouse_event *e) {
    if (mouseq_full()) {
        /* 【溢出修复】 队列满时不静默丢弃, 而是覆盖最旧的事件 (tail 前进一格).
         * 这样快速拖动时最新事件不会丢, 鼠标位置始终跟手。 */
        mouseq_tail = (mouseq_tail + 1) % MOUSEQ_SIZE;
    }
    mouseq[mouseq_head] = *e; mouseq_head = (mouseq_head + 1) % MOUSEQ_SIZE;
}
static int mouseq_pop(struct mouse_event *out) {
    if (mouseq_empty()) return 0;
    *out = mouseq[mouseq_tail]; mouseq_tail = (mouseq_tail + 1) % MOUSEQ_SIZE;
    return 1;
}

/* =======================================================================
 * [终极架构] 原始字节 rawq + 语法分类器 classify_rawq()
 * =======================================================================
 *  IDE / AHCI 模式下, QEMU 的 USB→PS2 桥对 0x64 st&AUX 位标注不可靠,
 *  会把 "鼠标 3 字节包" 的字节全部标成 AUX=0 (当成键盘), 导致:
 *    - 用户按键盘没反应 (因为鼠标字节占满了键盘缓冲区)
 *    - 或者鼠标字节被当成键盘扫描码, 产生乱码 ' ` " 符号
 *
 *  解决方案: **完全不看 st&0x08 (AUX 位)** 分类靠数据结构!
 *
 *  语法特征对照表:
 *  【鼠标 Streaming 3 字节包】:
 *     B0 格式: [Y_ov X_ov Y_sgn X_sgn 1 M R L]
 *              特征: bit3 **必须恒等于 1** (这是识别鼠标包头最强依据!)
 *     B1/B2: X/Y 位移 (0x00~0xFF 任意值, 不区分 make/release)
 *
 *  【键盘 Set 1 扫描码 (8042 硬件翻译后输出)】:
 *     Make:    0x01 ~ 0x7F  (Esc=0x01, 1=0x02 ... space=0x39 ... Delete=0x53)
 *     Release: 0x81 ~ 0xFE  (Make | 0x80)
 *     Ext:     0xE0 + Make(0x01~0x7F) / 0xE0 + Release(0x81~0xFE)
 *              (兼容漏网 Set2: 0xE0+0xF0+xx → 0xE080|(xx|0x80))
 *     伪码: 0x00/0xAA/0xEE/0xFA/0xFC/0xFD/0xFE/0xFF → 直接丢
 *
 *  因为"鼠标 B0 bit3=1"是鼠标 Streaming 协议强制位,
 *  而键盘 Set 1 扫描码中 bit3=1 的码值有: 0x18(O), 0x28(L), 0x39(空格), 0x19(P), etc
 *  — 但这些码如果是鼠标包开头, 就没有 B1/B2 跟着. 分类器通过"读 ahead 判断"剥离.
 *
 *  规则 (按优先级):
 *    ① 遇到伪码 → 丢
 *    ② 字节 b 满足 (b & 0x08) && (接下来还有至少 2 字节), 且 "鼠标 B1/B2 特征验证通过"
 *       → 这 3 字节是鼠标包 → mouse_on_byte() 组装
 *       【B1/B2 鼠标特征】: 原始键盘扫描码不会出现"连续 3 个字节都不像键盘码";
 *         实际上只要"B0 bit3=1 且 B0 != 空格 0x39 && B0 != 0x19 && ..."常见码就判鼠标
 *         (最稳健做法: B0 带按钮标志 bit0/1/2 → 立即判鼠标)
 *    ③ 0xE0 + 扩展 → 键盘扩展序列
 *    ④ Set 1 make/release → 键盘逻辑扫描码入 kbdq
 *    ⑤ 不确定 (可能鼠标 B0 有了还缺 B1/B2) → 停下来, 等下次凑齐
 * =====================================================================*/

/* ---------- rawq: 8042 OBF 原始字节环形缓冲 (256 byte) ---------- */
#define RAWQ_SIZE 256
static volatile unsigned char rawq[RAWQ_SIZE];
static volatile int rawq_head, rawq_tail;
static inline int rawq_full(void)  { return ((rawq_head + 1) % RAWQ_SIZE) == rawq_tail; }
static inline int rawq_empty(void) { return rawq_head == rawq_tail; }
static inline int rawq_len(void)   { return (rawq_head - rawq_tail + RAWQ_SIZE) % RAWQ_SIZE; }
static void rawq_push(unsigned char b) {
    if (rawq_full()) { rawq_tail = (rawq_tail + 1) % RAWQ_SIZE; }   /* 满了丢最旧的 */
    rawq[rawq_head] = b; rawq_head = (rawq_head + 1) % RAWQ_SIZE;
}
static unsigned char rawq_peek(int off) { return rawq[(rawq_tail + off) % RAWQ_SIZE]; }
static void rawq_drop(int n) { rawq_tail = (rawq_tail + n) % RAWQ_SIZE; }

/* ---------- kbdq: 完整逻辑扫描码队列 (Set1 make + release + ext) ---------- */
#define KBDQ_SIZE 128
static volatile unsigned int kbdq[KBDQ_SIZE];
static volatile int kbdq_head, kbdq_tail;
static inline int kbdq_full(void)  { return ((kbdq_head + 1) % KBDQ_SIZE) == kbdq_tail; }
static inline int kbdq_empty(void) { return kbdq_head == kbdq_tail; }
static void kbdq_push(unsigned int sc) {
    if (kbdq_full()) return;
    kbdq[kbdq_head] = sc; kbdq_head = (kbdq_head + 1) % KBDQ_SIZE;
}
static unsigned int kbdq_pop(void) {
    unsigned int v = kbdq[kbdq_tail]; kbdq_tail = (kbdq_tail + 1) % KBDQ_SIZE;
    return v;
}

/* 把 3 字节直接送给 mouse_on_byte 组装成事件并 push 到 mouseq
 * (mouse_on_byte 定义在 push_mouse_triplet 下方) */
static void mouse_on_byte(unsigned char b);   /* 前向声明 */
static void push_mouse_triplet(unsigned char b0, unsigned char b1, unsigned char b2) {
    mouse_on_byte(b0);
    mouse_on_byte(b1);
    mouse_on_byte(b2);
}

/* 组装鼠标 Streaming 3-byte 包: B0[Y_ov X_ov Y_sgn X_sgn 1 M R L] B1[X] B2[Y] */
static void mouse_on_byte(unsigned char b) {
    /* 【看门狗】 若 s_midx > 0 已超过 6 字节仍未完成 3 字节包, 说明失步
     * (可能误吞了键盘字节), 复位重新同步。 */
    if (s_midx > 0) {
        s_mwatchdog++;
        if (s_mwatchdog > 6) { s_midx = 0; s_mwatchdog = 0; }
        /* 键盘扩展前缀 0xE0 不可能是鼠标数据, 若组装中途遇到则放弃当前包 */
        if (b == 0xE0) { s_midx = 0; s_mwatchdog = 0; return; }
    }
    if (s_midx == 0 && (b & 0x08) == 0) return;     /* 包同步: B0 bit3 必须为 1 */
    s_mbuf[s_midx++] = b;
    if (s_midx < 3) return;
    s_midx = 0;
    s_mwatchdog = 0;
    if (s_mbuf[0] & 0xC0) return;                     /* 溢出丢 */
    int dx = (int)s_mbuf[1];
    if (s_mbuf[0] & 0x10) dx |= ~0xFF;                /* X_sgn 扩展 */
    int dy = (int)s_mbuf[2];
    if (s_mbuf[0] & 0x20) dy |= ~0xFF;                /* Y_sgn 扩展 */
    dy = -dy;
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    int w = fb->hr > 0 ? (int)fb->hr : 640;
    int h = fb->vr > 0 ? (int)fb->vr : 480;
    s_mx += dx; s_my += dy;
    if (s_mx < 0) s_mx = 0; if (s_mx >= w) s_mx = w - 1;
    if (s_my < 0) s_my = 0; if (s_my >= h) s_my = h - 1;
    struct mouse_event ev;
    ev.dx = dx; ev.dy = dy;
    ev.btn = s_mbuf[0] & 0x07;
    ev.x = s_mx; ev.y = s_my;
    mouseq_push(&ev);
}

/* classify_rawq(): 用 8042 状态寄存器 AUX 位 (bit5=0x20) 分离鼠标/键盘
 * =====================================================================
 * 【根因】 用户串口日志 SCA(08)KEY(7) / SCA(18)KEY(o) 证明:
 *   ① 0x08→'7', 0x18→'o' 是 Set 1 码表映射 → 键盘输出 Set 1 (翻译已启用)
 *   ② 这些字节是鼠标 B0/B1/B2, 被当成键盘码灌进 kbdq → 键盘被淹没
 *
 * 【修复】 8042 状态寄存器 bit5 (AUX_OBF) 可靠区分:
 *   AUX=1 → 鼠标字节 → mouse_on_byte() 直接组装 3 字节包
 *   AUX=0 → 键盘字节 → rawq → Set 1 扫描码处理 → kbdq
 *   不再需要任何 bit3/Always-1/强信号 猜测! */
/* is_fake: 过滤 PS/2 键盘控制器伪码 (非扫描码字节)。
 *   0x00 = 键盘缓冲区空 / 键盘故障
 *   0xEE = Echo 响应
 *   0xFA = ACK (命令应答)
 *   0xFC/0xFD/0xFE = 键盘错误/重发请求
 *   0xFF = 键盘错误
 * [关键修复] 0xAA 不在此列表! 0xAA 是 Set 1 左 Shift 的释放码 (0x2A | 0x80)。
 *   0xAA 同时也是 BAT 自检通过码, 但 BAT 只在系统启动时发一次,
 *   误当左 Shift RELEASE 处理只是清一个已为 0 的位, 无害。
 *   反之, 如果过滤 0xAA, 左 Shift 永远不释放 → 所有字母变成大写/符号。 */
static int is_fake(unsigned char b) {
    switch (b) { case 0x00:case 0xEE:case 0xFA:case 0xFC:case 0xFD:case 0xFE:case 0xFF: return 1; default: return 0; }
}

static void classify_rawq(void) {
    /* ── 第一步: 用 AUX 位分离鼠标/键盘 ──
     * 【AUX 不可靠修复】 QEMU USB→PS2 桥在 IDE/AHCI 模式下, 8042 状态寄存器
     *   bit5 (AUX_OBF) 标注不可靠: 鼠标 3 字节包的第 2/3 字节可能被标成 AUX=0,
     *   被误判为键盘字节灌进 rawq → 鼠标包永远组装不完整 → 鼠标不动。
     * 修复: 若 s_midx > 0 (正处于鼠标包组装中途), 即使 AUX=0 也当鼠标字节处理。 */
    for (int i = 0; i < 128; i++) {
        unsigned char st = inb(0x64);
        if (!(st & 0x01)) break;                       /* OBF=0 */
        if (st & 0xC0) { (void)inb(0x60); continue; }  /* PE/TO 错误丢弃 */
        unsigned char b = inb(0x60);
        if ((st & 0x20) || s_midx > 0) {
            mouse_on_byte(b);                          /* AUX=1 或组装中途: 鼠标 */
        } else {
            rawq_push(b);                              /* AUX=0 且非组装中途: 键盘 */
        }
    }

    /* ── 第二步: 处理 rawq 中的 Set 1 键盘扫描码 ── */
    for (;;) {
        int n = rawq_len();
        if (n < 1) return;
        unsigned char b0 = rawq_peek(0);

        if (is_fake(b0)) { rawq_drop(1); continue; }

        /* Set 1 扩展前缀 0xE0 — 0xE0 XX (XX: make 或 make|0x80 release) */
        if (b0 == 0xE0) {
            if (n < 2) return;
            kbdq_push(0xE000u | (unsigned int)rawq_peek(1));
            rawq_drop(2);
            continue;
        }

        /* Set 1 make (0x01-0x7F) */
        if (b0 >= 0x01 && b0 <= 0x7F) {
            kbdq_push((unsigned int)b0);
            rawq_drop(1);
            continue;
        }

        /* Set 1 release (0x81-0xFF = make|0x80) */
        if (b0 >= 0x81) {
            kbdq_push((unsigned int)b0);
            rawq_drop(1);
            continue;
        }

        /* 0x00 / 0x80 → 丢 */
        rawq_drop(1);
    }
}

/* ---------- 键盘接口 (由 classify_rawq() 统一填充 kbdq) ---------- */
/* kb_has_data: 非阻塞探测 — 先 classify_rawq() 用 AUX 位分离鼠标/键盘, 然后看 kbdq。*/
static int kb_has_data(void) {
    classify_rawq();
    return !kbdq_empty();
}

/* 兼容旧接口: 阻塞拿到一个键盘逻辑扫描码, 然后剥掉低 8 位做"原始字节"返回 (忽略扩展序列).
 * ok=0 理论上不会超时, 所以永远 ok=1 (保留接口兼容) */
static unsigned char kb_read_raw_safe(int *ok) {
    while (!kb_has_data()) { asm volatile("pause"); }
    unsigned int sc = kbdq_pop();
    *ok = 1;
    /* Set 1 普通码低 8 位有效, 扩展码只取 sub byte — 不建议调用者用这个接口,
     * 这里仅兼容遗留代码, 正常路径请走 kbdq_pop */
    if (sc & 0xE000) return (unsigned char)(sc & 0xFF);
    return (unsigned char)(sc & 0xFF);
}

/* kb_scan_to_key: 接受 Set 1 扫描码 (8042 翻译已启用, QEMU 默认 bit6=1)
 *   Make:    0x01..0x7F
 *   Release: 0x81..0xFF (make | 0x80)
 *   扩展:    0xE0 XX (XX: make 或 make|0x80)
 *
 * [修复 Shift 长按 2 字符后无法取消]
 *   原方案 (int 计数器 +1/-1) 的缺陷:
 *     ① 用户在按住 Shift 期间切换上下文 (shell→EFS 程序→返回 shell),
 *        每个上下文的 shift/caps 都是局部变量, 初始化为 0 → 物理按键仍按着, 但 shift=0
 *        再按 Shift release, 计数器会变 -1, 非零 → shift 永远"卡死"。
 *     ② 长按 Shift, 键盘 typematic 连续发送 MAKE(0x2A) → counter 不断 ++,
 *        如果中途有任何 release 丢失 (鼠标字节误分类吃掉/队列溢出), 后续再多 release
 *        也不能把 counter 减回 0。
 *     ③ 部分 BIOS/键盘会给 PrintScreen 序列发送 0xE0 0x2A / 0xE0 0x36 (fake Shift),
 *        release 序列缺 0xE0, 会把原 shift 减到负数。
 *   解决方案: 用【物理按键位图 + 计数器钳位】双重保证
 *     - static g_shift_pressed (bit0=LShift 0x2A, bit1=RShift 0x36) → 跨调用/跨上下文持久化,
 *       MAKE=SET, RELEASE=CLEAR, 最终 shift = POPCOUNT(g_shift_pressed) ∈ {0,1,2}
 *     - 计数器钳制在 [0, 2] 之间, 防 typematic 重复 MAKE 把 ++ 到 >2
 *     - CapsLock 也做位图 + 只在 RELEASE 切态 (原 MAKE 切态遇到 typematic 连续 MAKE 会反复翻转)
 *
 *   g_shift_pressed 是静态变量, 但 efs_readline 每次重新进入时 shift/caps 仍被
 *   调用者作为局部变量初始化为 0 → 本函数在每次调用时把 shift 同步到位图结果,
 *   所以调用者就算 reset shift, 下一次 MAKE/RELEASE 时会恢复一致。 */
static int kb_scan_to_key(unsigned int sc, int *shift, int *caps) {
    /* 物理键位图 (跨调用持久化):
     *   bit0 = LShift (0x2A) 按下
     *   bit1 = RShift (0x36) 按下
     *   静态初始化 = 0; 系统启动时没按着任何键。 */
    static unsigned char g_shift_pressed = 0;
    /* CapsLock 状态跨调用持久化: 调用者 *caps 可能为 0, 我们需要同步真实状态。
     * CapsLock 是"一次 MAKE 切换一次", 但 typematic 连续 MAKE 不该反复切换。
     * 用位图记: 必须先看到 RELEASE, 再 MAKE 才切换。 */
    static unsigned char g_caps_held = 0;   /* 当前 CapsLock 键物理按着吗? */
    static int g_caps_state = 0;            /* 真实 CapsLock 开关状态 (按用户视角) */

    int is_release = 0;
    unsigned int code;
    int is_ext = 0;
    if (sc & 0xE000) {
        is_ext = 1;
        unsigned char sub = (unsigned char)(sc & 0xFF);
        is_release = (sub & 0x80) ? 1 : 0;
        code = (unsigned int)(sub & ~0x80);
    } else {
        unsigned char sub = (unsigned char)(sc & 0xFF);
        is_release = (sub & 0x80) ? 1 : 0;
        code = (unsigned int)(sub & ~0x80);
    }

    /* Set 1: LShift=0x2A, RShift=0x36, CapsLock=0x3A
     * Shift 处理: 忽略 is_ext (PrintScreen 组合带 0xE0 前缀也得处理)
     * 用位图: 0x2A 对应 bit0, 0x36 对应 bit1。
     * MAKE: 置位; RELEASE: 清位;
     * 结果 shift = popcount(pressed), 钳制在 0..2。*/
    if (code == 0x2A || code == 0x36) {
        unsigned char bit = (code == 0x2A) ? 0x01 : 0x02;
        if (is_release) {
            g_shift_pressed &= ~bit;   /* 清位: 保证是 0, 即使 typematic 重复 RELEASE */
        } else {
            g_shift_pressed |= bit;    /* 置位: 重复 MAKE 也保持 1 */
        }
        int n = 0;
        if (g_shift_pressed & 0x01) n++;
        if (g_shift_pressed & 0x02) n++;
        /* 钳制 [0,2], 然后写回调用者的 *shift */
        if (n < 0) n = 0; if (n > 2) n = 2;
        *shift = n;
        return 0;
    }
    /* CapsLock: 非扩展键 + 真正物理 MAKE(按下上升沿检测), 必须 g_caps_held==0 才翻转一次 */
    if (!is_ext && code == 0x3A) {
        if (is_release) {
            g_caps_held = 0;
        } else {
            if (!g_caps_held) {
                /* 只有从 not-held → held 的第一次 MAKE 才翻转 (防止 typematic 重复翻转) */
                g_caps_held = 1;
                g_caps_state = !g_caps_state;
            }
        }
        *caps = g_caps_state;  /* 同步回调用者 */
        return 0;
    }
    /* 每次处理完修饰键后, 把 *caps 同步到持久状态 (调用者的 *caps 可能是新调用重置为 0 的局部变量) */
    if (is_release) { /* 此处没有触及 Shift/Caps, 但仍同步持久化 CapsLock 状态到调用者 (保持一致) */
        *caps = g_caps_state;
        return 0;
    }
    if (code == 0) return 0;
    *caps = g_caps_state;  /* 保持一致 */

    /* Set 1 扩展键 (0xE0 前缀):
     *   Up=0x48  Down=0x50  Left=0x4B  Right=0x4D
     *   Home=0x47 End=0x4F  Del=0x53 */
    if (is_ext) {
        if (code == 0x48) return KEY_UP;
        if (code == 0x50) return KEY_DOWN;
        if (code == 0x4B) return KEY_LEFT;
        if (code == 0x4D) return KEY_RIGHT;
        if (code == 0x47) return KEY_HOME;
        if (code == 0x4F) return KEY_END;
        if (code == 0x53) return KEY_DELETE;
        return 0;
    }

    /* Set 1: Esc=0x01, Backspace=0x0E, Tab=0x0F, Enter=0x1C */
    if (code == 0x01) return KEY_ESC;
    if (code == 0x0E) return KEY_BACKSPACE;
    if (code == 0x0F) return KEY_TAB;
    if (code == 0x1C) return KEY_ENTER;

    /* Set 1 ASCII 表 (索引 = make 码) */
    static const char lower_set1[] = {
        0, 0,'1','2','3','4','5','6','7','8','9','0','-','=', 0, 0,
       'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
       'd','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v',
       'b','n','m',',','.','/', 0, 0, 0,' '
    };
    static const char upper_set1[] = {
        0, 0,'!','@','#','$','%','^','&','*','(',')','_','+', 0, 0,
       'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
       'D','F','G','H','J','K','L',':','"','~', 0,'|','Z','X','C','V',
       'B','N','M','<','>','?', 0, 0, 0,' '
    };
    if (code >= sizeof(lower_set1)) return 0;
    char c = lower_set1[code];
    if (c == 0) return 0;
    int is_letter = (c >= 'a' && c <= 'z');
    if (is_letter) {
        if (*shift != *caps) c = upper_set1[code];
    } else if (*shift) {
        c = upper_set1[code];
    }
    return (int)(unsigned char)c;
}

/* ---------- PS/2 鼠标/键盘 控制器初始化 ---------- */
static void ps2_init(void) {
    mouseq_head = mouseq_tail = 0;
    s_midx = 0;
    s_mwatchdog = 0;

    /* 关键: 分类状态队列全部清零, 避免 boot 前残留导致键盘按键被吞 */
    rawq_head = rawq_tail = 0;
    kbdq_head = kbdq_tail = 0;

    for (int i = 0; i < 16; i++) if (inb(0x64) & 0x01) (void)inb(0x60); else break;

    ps2_cmd(0xA8);                                   /* ② 启用第二端口 (AUX = 鼠标) */
    ps2_cmd(0x20); unsigned char cfg = ps2_wait_obf();  /* ③ 读配置字节 */
    /* 8042 配置字节 (OSDev):
     *   bit0=IRQ1  bit1=IRQ12  bit2=SYS  bit3=0
     *   bit4=Port1时钟(1=禁用)  bit5=Port2时钟(1=禁用)
     *   bit6=翻译(1=Set2→Set1)  bit7=0
     * 【修复】 之前 cfg|=0x30 禁用了两个时钟! 现在:
     *   cfg |= 0x40  确保翻译开启 (QEMU 默认已开, 显式置位保险)
     *   cfg &= ~0x30 确保两个时钟都开启 (清 bit4/bit5)
     *   cfg &= ~0x02 关 IRQ12 (系统轮询模式, IDT 未注册 IRQ12 handler,
     *              开启会导致鼠标点击触发三故障打印垃圾字符如反引号 0x60) */
    cfg |= 0x40;                          /* bit6: Set2→Set1 翻译 */
    cfg &= ~0x30;                         /* 清 bit4/bit5: 两个时钟都开启 */
    cfg &= ~0x03;                         /* bit0/bit1: 关 IRQ1+IRQ12 (轮询模式, 不需要中断) */
    ps2_cmd(0x60); ps2_data(cfg);        /* ④ 写回配置 */

    ps2_aux_write(0xF6); (void)ps2_wait_obf();   /* ⑤ Set Defaults */
    ps2_aux_write(0xF4); (void)ps2_wait_obf();   /* ⑥ Enable Streaming */

    /* 清空初始化期间产生的字节, 用 AUX 位 (bit5=0x20) 分发 */
    for (int i = 0; i < 64; i++) if (inb(0x64) & 0x01) {
        unsigned char st = inb(0x64);
        unsigned char v = inb(0x60);
        if (st & 0x20) mouse_on_byte(v);           /* AUX=1: 鼠标 */
    } else break;
}

/* ---------- 鼠标对外 API ---------- */
static void draw_mouse_cursor(void);    /* 前向声明 (mouse_set_cursor 会先调用) */
static int mouse_poll(struct mouse_event *out) {
    /* 先 classify 所有 rawq, 把鼠标包都组装好入队 */
    classify_rawq();
    return mouseq_pop(out);
}

/* ---------- 光标渲染: 背景保存+恢复 (16x16 方块) ---------- */
#define CUR_SIZE 16
static unsigned int s_cursor_save[CUR_SIZE * CUR_SIZE];   /* 保存上一次光标覆盖的 16x16 背景 */
/* s_save_x / s_save_y 提前声明在 256-257 行 (供 fill_screen 重置);
 *   这里在首次绘制前默认 s_save_x = s_save_y = -1 (未保存) */

/* 从 fb 把 (x,y) 起的 16x16 像素保存到 s_cursor_save
 * 如果 x,y 靠边超出屏幕, 裁剪着填 (超界填 0) */
static void save_cursor_bg(int x, int y) {
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb || !fb->fb_base) return;
    unsigned int *base = (unsigned int*)(unsigned long)fb->fb_base;
    int pitch = (int)fb->ppsl;
    int w = (int)fb->hr, h = (int)fb->vr;
    unsigned int *dst = s_cursor_save;
    for (int r = 0; r < CUR_SIZE; r++) {
        for (int c = 0; c < CUR_SIZE; c++) {
            int sx = x + c, sy = y + r;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h)
                *dst++ = base[sy * pitch + sx];
            else
                *dst++ = 0;
        }
    }
    s_save_x = x; s_save_y = y;
}

/* 把保存的背景写回 fb (擦除光标, 完美恢复原来的文字/图形像素) */
static void restore_cursor_bg(void) {
    if (s_save_x < 0) return;
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb || !fb->fb_base) { s_save_x = s_save_y = -1; return; }
    unsigned int *base = (unsigned int*)(unsigned long)fb->fb_base;
    int pitch = (int)fb->ppsl;
    int w = (int)fb->hr, h = (int)fb->vr;
    int x = s_save_x, y = s_save_y;
    const unsigned int *src = s_cursor_save;
    for (int r = 0; r < CUR_SIZE; r++) {
        for (int c = 0; c < CUR_SIZE; c++, src++) {
            int sx = x + c, sy = y + r;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h)
                base[sy * pitch + sx] = *src;
        }
    }
    s_save_x = s_save_y = -1;
}

/* 显示/隐藏鼠标 (对外):
 *  shell 切换行输出字符 / clear_screen 前, 先 hide_mouse_cursor() 避免留鬼影.
 * [WM 模式] hide 变 NOP: 不再 restore_cursor_bg (那会写旧像素到 front buffer,
 *  与双缓冲冲突). WM 模式下鼠标不需要 hide — wm_composite 每帧重画整个画面,
 *  flush 覆盖旧鼠标, 然后在 Phase 3 画新鼠标. */
static void hide_mouse_cursor(void) {
    if (g_wm_enabled) return;  /* WM 模式: NOP */
    restore_cursor_bg();
}

/* 对外: 显示/隐藏鼠标 — 定义放在 save/restore 之后 (用到 s_save_x/save_cursor_bg) */
static void mouse_set_cursor(int show) {
    int was_vis = s_mouse_visible;
    s_mouse_visible = show ? 1 : 0;
    if (!was_vis && s_mouse_visible) {
        /* 刚从隐藏切到显示 → 立即画一帧 (首次建立 save_cursor_bg) */
        draw_mouse_cursor();
    }
    if (was_vis && !s_mouse_visible) {
        /* 刚从显示切到隐藏 → 恢复背景 */
        if (s_save_x >= 0) restore_cursor_bg();
        s_save_x = s_save_y = -1;
    }
}

/* 11x11 像素箭头 + 黑描边 + 红色热点 (形状精确, 不覆盖 save 以外区域) */
static void draw_mouse_cursor(void) {
    if (!s_mouse_visible) return;
    /* 【桌面脱离内核】 WM 模式和合成器激活时, 内核完全不画鼠标 —
     * 鼠标由外部合成器 (efmAether/efmcompositor) 渲染.
     * 仅早期引导 (非 WM 控制台模式) 走内核鼠标绘制路径. */
    if (g_compositor_active || g_wm_enabled) return;
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb || !fb->fb_base) return;
    unsigned int *base = (unsigned int*)(unsigned long)fb->fb_base;
    int pitch = (int)fb->ppsl;
    int w = (int)fb->hr, h = (int)fb->vr;

    /* ① 如果位置没变, 就不用重画 (避免每帧闪烁) */
    if (s_save_x == s_mx && s_save_y == s_my) return;

    /* [WM 模式] 不做 save/restore — 鼠标直接画在 front buffer 上.
     * 旧 save/restore 机制与双缓冲冲突:
     *    save 从 front 读 (stale) → restore 写 front → 被 flush 覆盖 → 闪烁+撕裂.
     * 新模式: wm_composite 每帧重画整个 back buffer (包括鼠标区域), flush 覆盖旧鼠标,
     *    然后 draw_mouse_cursor 在 flush 之后直接画新位置箭头到 front. 无需 save/restore.
     * [鼠标拖尾最终修复]
     *    鼠标单独画在 front, 位置变化时旧箭头残留在 front → 从 back buffer 把
     *    (s_prev_mx, s_prev_my) 起 16x16 像素拷回 front (覆盖旧箭头) → 再画新箭头 → 零拖尾. */
    if (g_wm_enabled) {
        /* WM 模式: 先擦 front 上的旧鼠标箭头 → 再画新箭头到 front */
        if (s_prev_mx >= 0 && s_prev_my >= 0) {
            /* 用 back buffer 对应像素覆盖 front (旧鼠标) — 零残影 */
            if (g_drv_gfx_ptr && g_drv_gfx_ptr->copy_back_rect_to_front) {
                g_drv_gfx_ptr->copy_back_rect_to_front(s_prev_mx, s_prev_my, CUR_SIZE, CUR_SIZE);
            } else {
                /* 驱动没实现接口 (vga.drv 等) → 退化为用当前 fill_rect 背景色盖 (有残影兜底) */
                struct gop_fb *fb2 = (struct gop_fb*)0x1000;
                if (fb2 && fb2->fb_base) {
                    unsigned int *base2 = (unsigned int*)(unsigned long)fb2->fb_base;
                    int pitch2 = (int)fb2->ppsl;
                    for (int r = 0; r < CUR_SIZE; r++) {
                        for (int c = 0; c < CUR_SIZE; c++) {
                            int px = s_prev_mx + c, py = s_prev_my + r;
                            if (px >= 0 && px < (int)fb2->hr && py >= 0 && py < (int)fb2->vr)
                                base2[py * pitch2 + px] = WM_CLR_BG;
                        }
                    }
                }
            }
        }
        int ox = s_mx, oy = s_my;
        s_save_x = s_mx; s_save_y = s_my;  /* 记录位置用于 "位置没变" 优化, 但不保存像素 */
        s_prev_mx = s_mx; s_prev_my = s_my; /* 记住这帧位置, 下次画时先擦掉 */
        unsigned int black = 0x000000, white = 0xFFFFFF, red = 0xFF2222;
        const unsigned char shape[11] = { 0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0x38,0x1C,0x0E };
        for (int r = 0; r < 11; r++) {
            unsigned char mask = shape[r];
            for (int c = 0; c < 8; c++) {
                if (!(mask & (1 << (7-c)))) continue;
                int px = ox + c, py = oy + r;
                if (r <= 14) {
                    int pxb = ox + c, pyb = oy + r + 1;
                    if (pxb >= 0 && pxb < w && pyb >= 0 && pyb < h) base[pyb * pitch + pxb] = black;
                }
                if (c <= 14) {
                    int pxb = ox + c + 1, pyb = oy + r;
                    if (pxb >= 0 && pxb < w && pyb >= 0 && pyb < h) base[pyb * pitch + pxb] = black;
                }
                if (px >= 0 && px < w && py >= 0 && py < h) base[py * pitch + px] = white;
            }
        }
        if (ox >= 0 && ox < w && oy >= 0 && oy < h) base[oy * pitch + ox] = red;
        return;
    }

    /* [非 WM 模式] 旧 save/restore 路径 (兼容早期引导) */
    /* ② 先把上次的光标擦掉 (恢复上次保存的背景) */
    if (s_save_x >= 0) restore_cursor_bg();
    /* ③ 保存新 (mx,my) 的 16x16 背景 */
    save_cursor_bg(s_mx, s_my);
    /* ④ 在 save_x,save_y 之上 (裁剪到 fb 范围) 画箭头 */
    int ox = s_save_x, oy = s_save_y;
    unsigned int black = 0x000000, white = 0xFFFFFF, red = 0xFF2222;
    /* 11 行 shape: 每行的像素 bitmask (MSB = 列 0)
     * [描边限定] 只在 r<=9, c<=7 时画描边, 确保所有像素落在 CUR_SIZE (16) 范围内,
     *   避免描边超出 save/restore 区域导致 EFS 跳转后残留白点 */
    const unsigned char shape[11] = { 0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0x38,0x1C,0x0E };
    for (int r = 0; r < 11; r++) {
        unsigned char mask = shape[r];
        for (int c = 0; c < 8; c++) {
            if (!(mask & (1 << (7-c)))) continue;
            int px = ox + c, py = oy + r;
            /* 描边: (r+1,c) + (r,c+1), 但限制描边像素在 16x16 save 范围内 (r+1 < 16 => r <= 14, c+1 < 16 => c <= 15, 实际形状 r<=10, c<=7, 都满足) */
            if (r <= 14) {
                int pxb = ox + c, pyb = oy + r + 1;
                if (pxb >= 0 && pxb < w && pyb >= 0 && pyb < h) base[pyb * pitch + pxb] = black;
            }
            if (c <= 14) {
                int pxb = ox + c + 1, pyb = oy + r;
                if (pxb >= 0 && pxb < w && pyb >= 0 && pyb < h) base[pyb * pitch + pxb] = black;
            }
            if (px >= 0 && px < w && py >= 0 && py < h) base[py * pitch + px] = white;
        }
    }
    /* 红点: 热点 (0,0) 一个像素 */
    if (ox >= 0 && ox < w && oy >= 0 && oy < h) base[oy * pitch + ox] = red;
}

/* ---------- String Helpers ---------- */
static int my_strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *(unsigned char*)a - *(unsigned char*)b; }
static int my_strncmp(const char *a, const char *b, int n) { for (int i=0; i<n; i++) { if (a[i]!=b[i]) return a[i]-b[i]; if (!a[i]) return 0; } return 0; }
static int my_strlen(const char *s) { int l=0; while (*s++) l++; return l; }
static void my_strcpy(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; }
static void my_strcat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static void my_memcpy(void *d, const void *s, unsigned int n) { unsigned char *dd=d; const unsigned char *ss=s; while (n--) *dd++ = *ss++; }
static void my_memset(void *d, unsigned char v, unsigned int n) { unsigned char *dd=d; while (n--) *dd++ = v; }
static void my_strncpy(char *d, const char *s, int n) { int i=0; while (i<n && s[i]) { d[i]=s[i]; i++; } if (i<n) d[i]=0; }
static void print_dec(unsigned int v) { char buf[12]; int i=10; buf[11]=0; if (!v) buf[i--]='0'; else for (; v && i>=0; i--, v/=10) buf[i] = '0' + (v%10); print_string(buf+i+1); }
static void print_hex(unsigned long long v) {
    serial_write_hex(v); serial_write(" ");
    for (int i=60; i>=0; i-=4) put_char("0123456789ABCDEF"[(v>>i)&0xF]);
}

/* ========== AHCI Driver (Fully Fixed) ==========
 *
 * 关键修复说明:
 * 1. 端口寄存器指针算术: 原代码 ahci_abar + 0x100 + p*0x80 按 unsigned int 元素
 *    偏移计算,实际偏移了 0x400 字节而非 0x100 字节。修复为 ahci_abar + 0x40 + p*0x20
 *    (0x100/4=0x40, 0x80/4=0x20),使指针指向正确的端口寄存器基址。
 *
 * 2. 命令头(Command Header)构造: 原代码将 PRDTL 放在 dword[1],CFL 放在 dword[0]。
 *    正确布局: dword[0] = (PRDTL << 16) | CFL, dword[1] = PRDBC(输出,初始化为0)。
 *
 * 3. H2D FIS 构造: 原代码字节顺序完全错乱(FIS类型与C标志合并,命令放在保留字段,
 *    LBA放在扩展LBA字段)。正确布局按 AHCI 规范逐字节映射。
 *
 * 4. PRDT 条目: 原代码只写3个 dword 且 I/DBC 在 dword[2]。PRDT 条目应为4个 dword:
 *    DBA, DBAU, reserved, (I<<31 | DBC)。修复后 I/DBC 在 dword[3]。
 *
 * 5. BSY/DRQ 检查: 原代码检查 PxCI(offset 0x38) 的 0x88 位,应检查 PxTFD
 *    (offset 0x20) 的 BSY(bit7)/DRQ(bit3) 以及 PxCI 的命令槽空闲。
 *
 * 6. 命令: 使用 READ DMA EXT (0x25) 替代 READ SECTORS (0x20),更适合 AHCI DMA 传输。
 *
 * 7. 多扇区读取: 新增 ahci_read_blocks 支持一次读取多个扇区,大幅提升 ext4 性能。
 */
static volatile unsigned int *ahci_abar = NULL;
static int ahci_port = -1;

/* AHCI 内存布局:
 * 0x90000 - 命令列表 (1KB 对齐, 32个槽 x 32字节)
 * 0x91000 - FIS 接收区 (256字节对齐)
 * 0x92000 - 命令表 0 (128字节对齐, 128字节 FIS + PRDT)
 */
#define AHCI_CMD_LIST_BASE   0x90000
#define AHCI_FIS_RECV_BASE   0x91000
#define AHCI_CMD_TABLE_BASE  0x92000

static unsigned int pci_config_read(unsigned int bus, unsigned int dev, unsigned int func, unsigned int offset) {
    outl(0xCF8, 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC));
    return inl(0xCFC);
}
static void pci_config_write(unsigned int bus, unsigned int dev, unsigned int func, unsigned int offset, unsigned int val) {
    outl(0xCF8, 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC));
    outl(0xCFC, val);
}

static int ahci_find_controller(void) {
    /* [引导加速] AHCI/SATA 控制器几乎总是挂在根总线 (bus 0) 上
     * (QEMU q35 的 ICH9-AHCI 在 00:1f.2)。先扫 bus 0 命中即返回,
     * 避免对 256 条总线做全量 PCI 枚举, 大幅缩短引导时间;
     * 仅当 bus 0 未命中时才回退到全量扫描, 兼容多桥真实硬件。 */
    int found = 0;
    for (int pass = 0; pass < 2 && !found; pass++) {
        int bus_hi = (pass == 0) ? 1 : 256;   /* pass0: 仅 bus0; pass1: bus0..255 */
        for (int bus = 0; bus < bus_hi && !found; bus++) {
        for (int dev = 0; dev < 32 && !found; dev++) {
            for (int func = 0; func < 8 && !found; func++) {
                unsigned int vendor = pci_config_read(bus, dev, func, 0);
                if (vendor == 0xFFFF) continue;
                if (((pci_config_read(bus, dev, func, 0x08) >> 16) & 0xFFFF) == 0x0106) {
                    /* 读取 BAR5 (AHCI ABAR, 64位) */
                    unsigned int bar5_low = pci_config_read(bus, dev, func, 0x24);
                    unsigned int bar5_high = pci_config_read(bus, dev, func, 0x28);
                    unsigned long long bar5 = ((unsigned long long)bar5_high << 32) | bar5_low;
                    bar5 &= ~0xF;  /* 清除类型位 */
                    ahci_abar = (volatile unsigned int*)(unsigned long)bar5;
                    serial_write("AHCI BAR5 = "); serial_write_hex(bar5); serial_write("\n");

                    /* 开启总线主控和内存空间 */
                    unsigned int cmd = pci_config_read(bus, dev, func, 0x04);
                    cmd |= (1 << 1) | (1 << 2);  /* Memory Space + Bus Master */
                    pci_config_write(bus, dev, func, 0x04, cmd);

                    /* 启用 AHCI (GHC.AE = bit31), 不进行全局复位 */
                    ahci_abar[0x04/4] |= (1 << 31);
                    int timeout = 0;
                    while (!(ahci_abar[0x04/4] & (1 << 31))) {
                        if (++timeout > 10000) { found = -1; break; }
                        asm volatile("pause");
                    }
                    if (found == -1) break;

                    unsigned int cap = ahci_abar[0x00/4];
                    serial_write("AHCI CAP = "); serial_write_hex(cap); serial_write("\n");
                    if (cap == 0 || cap == 0xFFFFFFFF) { found = -1; break; }
                    found = 1;
                }
            }
        }
        }
    }
    return (found == 1) ? 1 : 0;
}

static int ahci_port_init(void) {
    unsigned int pi = ahci_abar[0x0C / 4];  /* Ports Implemented */
    serial_write("PI = "); serial_write_hex(pi); serial_write("\n");
    int timeout;
    int found_port = -1;

    for (int p = 0; p < 32; p++) {
        if (!(pi & (1 << p))) continue;
        serial_write("Trying port "); serial_write_hex(p); serial_write("\n");

        /* [修复] 端口寄存器基址: 0x100字节偏移 / 4 = 0x40 元素偏移
         * 每个端口 0x80字节 / 4 = 0x20 元素偏移 */
        volatile unsigned int *port_regs = ahci_abar + 0x40 + p * 0x20;

        /* 1. 停止 DMA 引擎 (PxCMD.ST=0, PxCMD.FRE=0) */
        port_regs[0x18/4] &= ~((1 << 0) | (1 << 4));
        timeout = 0;
        while ((port_regs[0x18/4] & ((1 << 15) | (1 << 14))) && timeout < 100000) {
            asm volatile("pause");
            timeout++;
        }
        if (timeout >= 100000) {
            serial_write("   stop engine timeout\n");
            continue;
        }

        /* 2. 清除中断状态和错误 (写1清零) */
        port_regs[0x10/4] = port_regs[0x10/4];  /* PxIS */
        port_regs[0x30/4] = port_regs[0x30/4];  /* PxSERR */

        /* 3. COMRESET: PxSCTL.DET=1, 禁用部分电源管理 */
        port_regs[0x2C/4] = (port_regs[0x2C/4] & ~0xF) | 1;  /* DET=1 */
        port_regs[0x2C/4] &= ~(0xF << 8);                    /* IPM=0 (允许所有状态) */

        /* 保持复位约 20ms */
        for (volatile int i = 0; i < 200000; i++) asm volatile("pause");

        /* 释放复位: DET=0 */
        port_regs[0x2C/4] = (port_regs[0x2C/4] & ~0xF) | 0;

        /* 等待 DET 实际清零 */
        timeout = 0;
        while ((port_regs[0x2C/4] & 0xF) && timeout < 100000) {
            asm volatile("pause");
            timeout++;
        }

        /* 4. 等待设备就绪 (PxSSTS.DET == 3: 设备已检测且通信建立) */
        unsigned int ssts;
        timeout = 0;
        do {
            ssts = port_regs[0x28/4];
            if ((ssts & 0xF) == 0x3) break;
            for (volatile int i = 0; i < 100000; i++) asm volatile("pause"); /* ~10ms */
        } while (++timeout < 500); /* 最多 ~5 秒 */

        serial_write("Port "); serial_write_hex(p);
        serial_write(": PxSSTS = "); serial_write_hex(ssts); serial_write("\n");

        /* 再次清除 COMRESET 产生的错误 */
        port_regs[0x30/4] = port_regs[0x30/4];

        if ((ssts & 0xF) == 0x3) {
            found_port = p;
            break;
        }
    }

    if (found_port < 0) return -1;
    ahci_port = found_port;

    volatile unsigned int *port_regs = ahci_abar + 0x40 + found_port * 0x20;

    /* 5. 再次停止引擎,配置命令列表和 FIS 接收区基址 */
    port_regs[0x18/4] &= ~((1 << 0) | (1 << 4));
    timeout = 0;
    while ((port_regs[0x18/4] & ((1 << 15) | (1 << 14))) && timeout < 100000) {
        asm volatile("pause");
        timeout++;
    }

    /* 清零命令列表和 FIS 接收区 */
    my_memset((void*)AHCI_CMD_LIST_BASE, 0, 1024);
    my_memset((void*)AHCI_FIS_RECV_BASE, 0, 256);

    /* PxCLB/PxCLBU - 命令列表基址 (1KB对齐) */
    port_regs[0x00/4] = AHCI_CMD_LIST_BASE;
    port_regs[0x04/4] = 0;
    /* PxFB/PxFBU - FIS 接收区基址 (256字节对齐) */
    port_regs[0x08/4] = AHCI_FIS_RECV_BASE;
    port_regs[0x0C/4] = 0;

    /* 6. 启动引擎: 先 FRE 后 ST */
    port_regs[0x18/4] |= (1 << 4);   /* FRE: FIS Receive Enable */
    for (volatile int i = 0; i < 1000; i++) asm volatile("pause");
    port_regs[0x18/4] |= (1 << 0);   /* ST: Start */

    /* 等待 CR (Command Running) 和 FR (FIS Receive Running) 置位 */
    timeout = 0;
    while (!(port_regs[0x18/4] & (1 << 15)) || !(port_regs[0x18/4] & (1 << 14))) {
        if (++timeout > 100000) {
            serial_write("AHCI engine start timeout\n");
            return -1;
        }
        asm volatile("pause");
    }
    serial_write("AHCI port ready.\n");
    return 0;
}

/* [完善] AHCI 端口错误恢复: 清除错误状态, COMRESET, 重启引擎
 * 在读写命令检测到错误时调用, 恢复端口到可用状态 */
static int ahci_port_error_recovery(void) {
    if (ahci_port < 0) return -1;
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    serial_write("AHCI: error recovery\n");

    /* 1. 停止引擎 (PxCMD.ST=0, PxCMD.FRE=0) */
    port_regs[0x18/4] &= ~((1 << 0) | (1 << 4));
    int timeout = 0;
    while ((port_regs[0x18/4] & ((1 << 15) | (1 << 14))) && timeout < 100000) {
        asm volatile("pause"); timeout++;
    }

    /* 2. 清除所有中断和错误状态 */
    port_regs[0x10/4] = port_regs[0x10/4];  /* PxIS: 写1清零 */
    port_regs[0x30/4] = port_regs[0x30/4];  /* PxSERR: 写1清零 */

    /* 3. COMRESET: PxSCTL.DET=1 */
    port_regs[0x2C/4] = (port_regs[0x2C/4] & ~0xF) | 1;
    for (volatile int i = 0; i < 200000; i++) asm volatile("pause");
    port_regs[0x2C/4] &= ~0xF;  /* 释放复位 */

    /* 4. 等待设备就绪 (PxSSTS.DET == 3) */
    timeout = 0;
    unsigned int ssts;
    do {
        ssts = port_regs[0x28/4];
        if ((ssts & 0xF) == 0x3) break;
        for (volatile int i = 0; i < 100000; i++) asm volatile("pause");
    } while (++timeout < 300);
    if ((ssts & 0xF) != 0x3) { serial_write("AHCI: recovery failed, no device\n"); return -1; }

    /* 5. 重新启动引擎: FRE 后 ST */
    port_regs[0x18/4] |= (1 << 4);  /* FRE */
    for (volatile int i = 0; i < 1000; i++) asm volatile("pause");
    port_regs[0x18/4] |= (1 << 0);  /* ST */
    timeout = 0;
    while (!(port_regs[0x18/4] & (1 << 15)) || !(port_regs[0x18/4] & (1 << 14))) {
        if (++timeout > 100000) { serial_write("AHCI: recovery: engine start failed\n"); return -1; }
        asm volatile("pause");
    }
    serial_write("AHCI: recovery complete\n");
    return 0;
}

/* [完善] AHCI 命令执行前: 清除中断/错误状态, 等待端口就绪
 * 检查: BSY/DRQ 清除 + PxCI 命令槽 0 空闲
 * [修复] 增加 PxCI 检查: 若上一条命令未完成, 等待其结束 */
static int ahci_wait_port_ready(void) {
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    int timeout = 0;
    /* 等待 BSY/DRQ 清除 */
    while ((port_regs[0x20/4] & 0x88) && timeout < 500000) {
        if (++timeout % 50000 == 0) {
            /* 检查是否有严重错误 */
            if (port_regs[0x30/4] & 0xFFFFFFFF) {
                serial_write("AHCI: PxSERR before cmd: "); serial_write_hex(port_regs[0x30/4]); serial_write("\n");
                ahci_port_error_recovery();
                return -1;
            }
        }
        asm volatile("pause");
    }
    if (timeout >= 500000) {
        serial_write("AHCI: port busy timeout\n");
        ahci_port_error_recovery();
        return -1;
    }
    /* [新增] 等待 PxCI bit0 清除 (命令槽 0 空闲) */
    timeout = 0;
    while ((port_regs[0x38/4] & 1) && timeout < 500000) {
        if (++timeout % 100000 == 0) {
            unsigned int tfd = port_regs[0x20/4];
            if (tfd & 0x01) {  /* ERR */
                ahci_port_error_recovery();
                return -1;
            }
        }
        asm volatile("pause");
    }
    if (timeout >= 500000) {
        serial_write("AHCI: PxCI stuck, recovering\n");
        ahci_port_error_recovery();
        return -1;
    }
    /* 清除命令前的中断和错误 */
    port_regs[0x10/4] = port_regs[0x10/4];
    port_regs[0x30/4] = port_regs[0x30/4];
    return 0;
}

/* [完善] AHCI 命令执行后: 检查错误, 必要时恢复 */
static int ahci_check_cmd_result(void) {
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    unsigned int tfd = port_regs[0x20/4];
    unsigned int serr = port_regs[0x30/4];
    unsigned int is = port_regs[0x10/4];

    /* 清除状态 */
    port_regs[0x10/4] = is;
    port_regs[0x30/4] = serr;

    if (tfd & 0x01) {  /* ERR */
        serial_write("AHCI: cmd error PxTFD="); serial_write_hex(tfd);
        serial_write(" PxSERR="); serial_write_hex(serr); serial_write("\n");
        ahci_port_error_recovery();
        return -1;
    }
    /* 检查 PxIS 中的致命错误位 */
    if (is & 0xBC000) {  /* IFMP|HBDS|HBFS|TFES|INFS */
        serial_write("AHCI: fatal irq PxIS="); serial_write_hex(is); serial_write("\n");
        ahci_port_error_recovery();
        return -1;
    }
    return 0;
}

/* [前向声明] ahci_build_prdt 定义在下方, 读写路径需提前调用 */
static int ahci_build_prdt(unsigned char *cmd_table_base, const void *buf, unsigned int byte_count);

/* [新增] 多扇区读取: 一次 READ DMA EXT 命令读取最多 256 个扇区 */
static int ahci_read_blocks(unsigned int lba, unsigned int count, void *buf) {
    if (ahci_port < 0 || count == 0) return -1;
    if (count > 256) count = 256;  /* 单次最多 256 扇区 (128KB) */

    /* [修复] 正确的端口寄存器指针 */
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;

    /* [完善] 使用统一的端口就绪检查 + 错误恢复 */
    if (ahci_wait_port_ready() != 0) return -1;

    /* [修复] 构造命令头 (Command Header, 32字节 = 8 dword)
     * dword0: bit0-4=CFL(5), bit5=A, bit6=W, bit7=P, bit16-31=PRDTL
     * dword1: PRDBC (输出, 初始化0)
     * dword2: CTBA (命令表基址低32位)
     * dword3: CTBAU (命令表基址高32位) */
    volatile unsigned int *cmd_header = (volatile unsigned int*)AHCI_CMD_LIST_BASE;
    for (int i = 0; i < 8; i++) cmd_header[i] = 0;  /* 清零整个命令头 */
    cmd_header[0] = 5;  /* CFL=5 (20字节FIS), PRDTL 稍后由 ahci_build_prdt 设置 */
    cmd_header[1] = 0;               /* PRDBC = 0 (输出) */
    cmd_header[2] = AHCI_CMD_TABLE_BASE;  /* CTBA */
    cmd_header[3] = 0;               /* CTBAU */

    /* 构造命令表 (128字节 FIS 区 + PRDT 区) */
    volatile unsigned int *cmd_table = (volatile unsigned int*)AHCI_CMD_TABLE_BASE;
    for (int i = 0; i < 128; i++) cmd_table[i] = 0;  /* 清零 512 字节 */

    /* [修复] H2D FIS 构造 (20字节 = 5 dword, 小端序)
     *
     * 字节布局 (AHCI 规范):
     *   byte 0: FIS 类型 = 0x27
     *   byte 1: C(bit7) + PM Port(bit0-3) = 0x80 (C=1, port 0)
     *   byte 2: 命令 = 0x25 (READ DMA EXT)
     *   byte 3: Features(低)
     *   byte 4: LBA 低 (bit 0-7)
     *   byte 5: LBA 中 (bit 8-15)
     *   byte 6: LBA 高 (bit 16-23)
     *   byte 7: Device = 0xE0 (LBA 模式)
     *   byte 8: LBA 低扩展 (bit 24-31)
     *   byte 9: LBA 中扩展 (bit 32-39)
     *   byte 10: LBA 高扩展 (bit 40-47)
     *   byte 11: Features(高)
     *   byte 12: 扇区数(低)
     *   byte 13: 扇区数(高)
     *   byte 14: 保留
     *   byte 15: Control
     *
     * 小端 dword 映射:
     *   fis[0] = byte0 | (byte1<<8) | (byte2<<16) | (byte3<<24)
     *   fis[1] = byte4 | (byte5<<8) | (byte6<<16) | (byte7<<24)
     *   fis[2] = byte8 | (byte9<<8) | (byte10<<16) | (byte11<<24)
     *   fis[3] = byte12 | (byte13<<8) | (byte14<<16) | (byte15<<24)
     */
    volatile unsigned int *fis = cmd_table;  /* FIS 在命令表偏移 0 */
    fis[0] = 0x27 | (0x80 << 8) | (0x25 << 16);  /* type=0x27, C=1, cmd=READ_DMA_EXT */
    fis[1] = (lba & 0xFF)
           | (((lba >> 8) & 0xFF) << 8)
           | (((lba >> 16) & 0xFF) << 16)
           | (0xE0 << 24);                          /* LBA低/中/高 + Device=0xE0 */
    fis[2] = ((lba >> 24) & 0xFF);                  /* LBA扩展 (32位LBA, 高位为0) */
    fis[3] = (count & 0xFF) | (((count >> 8) & 0xFF) << 8);  /* 扇区数 */
    fis[4] = 0;                                      /* 保留 */

    /* [完善] 使用 ahci_build_prdt 构造 PRDT 表 (支持多条目, 自动设置 I 位)
     * 替代原先手写单条目, 当缓冲区跨越 4MB 边界时自动拆分 */
    int prdt_n = ahci_build_prdt((unsigned char*)AHCI_CMD_TABLE_BASE, buf, count * 512);
    if (prdt_n <= 0) { serial_write("AHCI: read build_prdt failed\n"); return -1; }
    cmd_header[0] |= ((unsigned int)prdt_n << 16);  /* PRDTL = prdt_n */

    /* 下发命令: PxCI bit0 置位 (使用命令槽 0) */
    port_regs[0x38/4] = 1;

    /* 等待命令完成 (PxCI bit0 清零) */
    int timeout = 0;
    while (port_regs[0x38/4] & 1) {
        if (++timeout > 5000000) {
            serial_write("AHCI: read timeout, PxCI="); serial_write_hex(port_regs[0x38/4]);
            serial_write(" PxTFD="); serial_write_hex(port_regs[0x20/4]); serial_write("\n");
            ahci_port_error_recovery();
            return -1;
        }
        asm volatile("pause");
    }

    /* [完善] 使用统一的错误检查 + 恢复 */
    return ahci_check_cmd_result();
}

/* 单扇区读取 (兼容接口) */
static int ahci_read_sector(unsigned int lba, void *buf) {
    return ahci_read_blocks(lba, 1, buf);
}

/* [新增] AHCI 多扇区写入: WRITE DMA EXT (0x35)
 * 与 read_blocks 结构相同, 区别:
 *   - FIS 命令 = 0x35
 *   - 命令头 W 位 (bit6) 置位, 提示 HBA 写方向
 *   - PRDT 指向源数据缓冲区 */
static int ahci_write_blocks(unsigned int lba, unsigned int count, const void *buf) {
    if (ahci_port < 0 || count == 0) return -1;
    if (count > 256) count = 256;

    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;

    /* [完善] 统一端口就绪检查 */
    if (ahci_wait_port_ready() != 0) return -1;

    /* 命令头: W=1 (bit6) */
    volatile unsigned int *cmd_header = (volatile unsigned int*)AHCI_CMD_LIST_BASE;
    for (int i = 0; i < 8; i++) cmd_header[i] = 0;
    cmd_header[0] = (1 << 6) | 5;  /* W=1, CFL=5 (PRDTL 稍后由 ahci_build_prdt 设置) */
    cmd_header[1] = 0;
    cmd_header[2] = AHCI_CMD_TABLE_BASE;
    cmd_header[3] = 0;

    volatile unsigned int *cmd_table = (volatile unsigned int*)AHCI_CMD_TABLE_BASE;
    for (int i = 0; i < 128; i++) cmd_table[i] = 0;

    /* H2D FIS: WRITE DMA EXT = 0x35 */
    volatile unsigned int *fis = cmd_table;
    fis[0] = 0x27 | (0x80 << 8) | (0x35 << 16);
    fis[1] = (lba & 0xFF)
           | (((lba >> 8) & 0xFF) << 8)
           | (((lba >> 16) & 0xFF) << 16)
           | (0xE0 << 24);
    fis[2] = ((lba >> 24) & 0xFF);
    fis[3] = (count & 0xFF) | (((count >> 8) & 0xFF) << 8);

    /* [完善] 使用 ahci_build_prdt 构造 PRDT 表 (支持多条目) */
    int prdt_n = ahci_build_prdt((unsigned char*)AHCI_CMD_TABLE_BASE, buf, count * 512);
    if (prdt_n <= 0) { serial_write("AHCI: write build_prdt failed\n"); return -1; }
    cmd_header[0] |= ((unsigned int)prdt_n << 16);  /* PRDTL = prdt_n */

    port_regs[0x38/4] = 1;
    int timeout = 0;
    while (port_regs[0x38/4] & 1) {
        if (++timeout > 5000000) {
            serial_write("AHCI: write timeout\n");
            ahci_port_error_recovery();
            return -1;
        }
        asm volatile("pause");
    }

    /* [完善] 统一错误检查 + 恢复 */
    return ahci_check_cmd_result();
}

static int ahci_write_sector(unsigned int lba, const void *buf) {
    return ahci_write_blocks(lba, 1, buf);
}

/* ========== AHCI 高级功能 ==========
 * [新增] 多 PRDT 支持: 当传输大于 4MB 时自动拆分为多个 PRDT 条目
 * [新增] FLUSH CACHE EXT: 写入后刷新磁盘缓存, 保证数据持久化
 * [新增] IDENTIFY DEVICE: 读取磁盘信息 (总扇区数等)
 */

/* [新增] 构造 PRDT 表: 将连续缓冲区拆分为多个 PRDT 条目
 * 每个 PRDT 条目最大 4MB (DBC 22位), 我们的缓冲区通常 < 128KB 故 1 条即可
 * 返回条目数, 写入 cmd_table 偏移 0x80 处
 * 命令表 PRDT 区最多容纳 (4096 - 0x80) / 16 = 248 条目 */
static int ahci_build_prdt(unsigned char *cmd_table_base, const void *buf, unsigned int byte_count) {
    volatile unsigned int *prd = (volatile unsigned int*)(cmd_table_base + 0x80);
    unsigned int remaining = byte_count;
    unsigned long long addr = (unsigned long long)(unsigned long)buf;
    int n = 0;
    while (remaining > 0 && n < 248) {
        unsigned int chunk = remaining;
        if (chunk > 0x400000) chunk = 0x400000;  /* 每条目最大 4MB */
        prd[n * 4 + 0] = (unsigned int)(addr & 0xFFFFFFFF);         /* DBA */
        prd[n * 4 + 1] = (unsigned int)(addr >> 32);                /* DBAU */
        prd[n * 4 + 2] = 0;                                          /* 保留 */
        prd[n * 4 + 3] = chunk - 1;  /* DBC = chunk-1 (I 位稍后设置) */
        addr += chunk;
        remaining -= chunk;
        n++;
    }
    /* 最后一个条目设置 I (中断) 位 */
    if (n > 0) prd[(n - 1) * 4 + 3] |= 0x80000000;
    return n;
}

/* [新增] AHCI FLUSH CACHE EXT (0xEA): 刷新磁盘写缓存到介质
 * 在 ext4 元数据写入后调用, 保证崩溃一致性 (替代 JBD2 日志的简化方案) */
static int ahci_flush_cache(void) {
    if (ahci_port < 0) return -1;
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    if (ahci_wait_port_ready() != 0) return -1;

    volatile unsigned int *cmd_header = (volatile unsigned int*)AHCI_CMD_LIST_BASE;
    for (int i = 0; i < 8; i++) cmd_header[i] = 0;
    cmd_header[0] = (0 << 16) | 5;  /* PRDTL=0, CFL=5, W=1 (写方向) */
    cmd_header[0] |= (1 << 6);       /* W bit */
    cmd_header[2] = AHCI_CMD_TABLE_BASE;
    cmd_header[3] = 0;

    volatile unsigned int *cmd_table = (volatile unsigned int*)AHCI_CMD_TABLE_BASE;
    for (int i = 0; i < 128; i++) cmd_table[i] = 0;

    /* H2D FIS: FLUSH CACHE EXT = 0xEA */
    volatile unsigned int *fis = cmd_table;
    fis[0] = 0x27 | (0x80 << 8) | (0xEA << 16);
    fis[1] = 0;
    fis[2] = 0;
    fis[3] = 0;

    port_regs[0x38/4] = 1;
    int timeout = 0;
    while (port_regs[0x38/4] & 1) {
        if (++timeout > 3000000) {
            serial_write("AHCI: flush timeout\n");
            return -1;
        }
        asm volatile("pause");
    }
    return ahci_check_cmd_result();
}

/* [新增] AHCI IDENTIFY DEVICE (0xEC): 读取磁盘信息
 * 返回 512 字节的 IDENTIFY 数据到 buf, 包含总扇区数 (word 100-103, LBA48) */
static int ahci_identify_device(unsigned char *buf) {
    if (ahci_port < 0) return -1;
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    if (ahci_wait_port_ready() != 0) return -1;

    volatile unsigned int *cmd_header = (volatile unsigned int*)AHCI_CMD_LIST_BASE;
    for (int i = 0; i < 8; i++) cmd_header[i] = 0;
    cmd_header[0] = (1 << 16) | 5;  /* PRDTL=1, CFL=5 */
    cmd_header[2] = AHCI_CMD_TABLE_BASE;
    cmd_header[3] = 0;

    volatile unsigned int *cmd_table = (volatile unsigned int*)AHCI_CMD_TABLE_BASE;
    for (int i = 0; i < 128; i++) cmd_table[i] = 0;

    /* H2D FIS: IDENTIFY DEVICE = 0xEC */
    volatile unsigned int *fis = cmd_table;
    fis[0] = 0x27 | (0x80 << 8) | (0xEC << 16);
    fis[1] = 0;
    fis[2] = 0;
    fis[3] = 0;

    /* PRDT: 512 字节到 buf */
    volatile unsigned int *prd = cmd_table + 0x80/4;
    prd[0] = (unsigned int)(unsigned long)buf;
    prd[1] = 0;
    prd[2] = 0;
    prd[3] = 0x80000000 | (512 - 1);

    port_regs[0x38/4] = 1;
    int timeout = 0;
    while (port_regs[0x38/4] & 1) {
        if (++timeout > 2000000) {
            serial_write("AHCI: identify timeout\n");
            return -1;
        }
        asm volatile("pause");
    }
    if (ahci_check_cmd_result() != 0) return -1;
    return 0;
}

/* [新增] 获取磁盘总扇区数 (LBA48), 通过 IDENTIFY DEVICE word 100-103 */
static unsigned long long ahci_get_sector_count(void) {
    unsigned char id_buf[512];
    if (ahci_identify_device(id_buf) != 0) return 0;
    /* word 100 = offset 200, word 101 = 202, word 102 = 204, word 103 = 206 */
    unsigned long long sectors = *(unsigned int*)(id_buf + 200);
    sectors |= ((unsigned long long)*(unsigned int*)(id_buf + 204) << 32);
    /* 检查是否支持 LBA48 (word 83 bit 10) */
    unsigned short word83 = *(unsigned short*)(id_buf + 83 * 2);
    if (!(word83 & 0x400)) {
        /* 仅支持 LBA28, 使用 word 60-61 */
        sectors = *(unsigned int*)(id_buf + 60 * 2);
    }
    return sectors;
}

static int ahci_init_driver(void) {
    if (ahci_find_controller() != 1) return -1;
    if (ahci_port_init() != 0) return -1;
    /* [新增] 读取磁盘容量 */
    unsigned long long sectors = ahci_get_sector_count();
    if (sectors) {
        serial_write("AHCI disk size: "); serial_write_hex(sectors);
        serial_write(" sectors ("); serial_write_hex(sectors / 2 / 1024);
        serial_write(" MB)\n");
    }
    return 0;
}

/* ========== IDE Driver ========== */
static int ide_read_sector(unsigned int lba, void *buf) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);
    outb(0x1F7, 0x20);
    int timeout = 0;
    while (++timeout < 1000000) { if (!(inb(0x1F7) & 0x80)) break; asm volatile("pause"); }
    timeout = 0;
    while (++timeout < 1000000) {
        unsigned char status = inb(0x1F7);
        if (status & 0x08) break;
        if (status & 0x01) return -1;
        asm volatile("pause");
    }
    unsigned short *ptr = buf;
    for (int i = 0; i < 256; i++) ptr[i] = inw(0x1F0);
    return 0;
}

static int ide_init_driver(void) {
    unsigned char test[512];
    if (ide_read_sector(0, test) != 0) return -1;
    if (*(unsigned short*)(test + 0x1FE) != 0xAA55) return -1;
    return 0;
}

/* [新增] IDE 单扇区写入: WRITE SECTORS (0x30) */
static int ide_write_sector(unsigned int lba, const void *buf) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);
    outb(0x1F7, 0x30);  /* WRITE SECTORS */
    int timeout = 0;
    while (++timeout < 1000000) { if (!(inb(0x1F7) & 0x80)) break; asm volatile("pause"); }
    timeout = 0;
    while (++timeout < 1000000) {
        unsigned char status = inb(0x1F7);
        if (status & 0x08) break;  /* DRQ */
        if (status & 0x01) return -1;
        asm volatile("pause");
    }
    const unsigned short *ptr = buf;
    for (int i = 0; i < 256; i++) {
        unsigned short v = ptr[i];
        outw(0x1F0, v);
    }
    timeout = 0;
    while (++timeout < 1000000) {
        unsigned char status = inb(0x1F7);
        if (!(status & 0x80)) break;  /* 等待 BSY 清除 */
        asm volatile("pause");
    }
    return 0;
}

/* ---------- Unified Disk Access ----------
 * [驱动拦截] 驱动加载前后都要可访问。
 * 方案:
 *   - 驱动注册函数 drv_find_disk() 稍后定义, 这里用前向声明指针 (或 weak 包装)。
 *   - 更简洁: 直接暴露 g_drv_disk_* 指针 (由 drv_attach_disk_ops 设置)
 *     驱动注册成功后指针非空, 调用。否则走原 AHCI/IDE 路径。
 *   这样 disk_read_sector 等可以在驱动子系统之前定义。*/
/* 外置: drv_attach_disk_ops() 在驱动子系统里写这两个指针 */
static const struct drv_disk_ops *g_drv_disk_ptr = 0;   /* 初始空, 驱动注册后非空 */
/* 外置: drv_attach_gfx_ops() — Graphics 驱动注册后, fill_rect/draw_char/scroll 等全部切到
 * 驱动实现 (双缓冲+抗锯齿+渐变). 驱动没加载时继续用内核内置直接写 FB (早期引导阶段). */
static const struct drv_gfx_ops *g_drv_gfx_ptr = 0;
/* 驱动侧写这两个指针的辅助函数 (在驱动注册时调用, 定义在后面) */
static void drv_attach_disk_ops(const struct drv_disk_ops *ops);
static void drv_attach_gfx_ops(const struct drv_gfx_ops *ops);

/* ===== [Graphics Flush 框架 - 最终版] =====
 * 核心语义: 区分 "正在帧合成中 (depth > 0)" 和 "不在帧 (depth == 0)".
 *   - 正在帧内: 所有写操作先 accumulate 到后缓冲, 只标记 dirty, 绝不 flush 半成的画面.
 *   - 离开帧 (depth 回到 0): 若帧内有脏像素, 才一次性 flush 整个脏矩形.
 *   - 不在帧 (早期引导阶段 / 非 WM 模式): 保留旧行为 - 每个 maybe_flush 调用都会立即刷, 保证早期输出可见. */

/* [mark_dirty] 每次像素/矩形/字符写后调用, 累计 "这帧有改动". */
static inline void drv_gfx_mark_dirty(void) {
    if (g_gfx_frame_depth > 0) {
        g_gfx_dirty_in_frame = 1;
        return;
    }
    /* 不在帧内: 不累计, 交给 maybe_flush 立刻刷 (早期引导模式) */
}

/* [mark_dirty_rect] 手动指定脏矩形 (inclusive x1,y1,x2,y2).
 * 用途: blit_buffer 等绕过 put_pixel/fill_rect 直接写 back buffer 后,
 * 需要把被写区域 (及鼠标相关区域) 加进脏矩形. */
static inline void drv_gfx_mark_dirty_rect(int x1, int y1, int x2, int y2) {
    g_gfx_dirty_in_frame = 1;
    if (g_drv_gfx_ptr && g_drv_gfx_ptr->mark_dirty_rect) {
        g_drv_gfx_ptr->mark_dirty_rect(x1, y1, x2, y2);
    }
}

/* [maybe_flush] 旧 API: 不在帧内才真的 flush; 帧内 NOP (等 frame_end 统一刷).
 * 保留 "每次可能修改后调用" 的写法, 但语义升级为 "非帧内立即刷, 帧内推迟刷".
 * [鼠标光标保护] flush 会把 back buffer 拷到 front buffer, 覆盖鼠标箭头,
 *   同时使 s_cursor_save 保存的像素过期 (flush 前保存的 = 旧 front buffer 内容).
 *   下次 draw_mouse_cursor 的 restore_cursor_bg 会把过期像素写回 → 白色残留.
 *   修复: flush 前先 restore_cursor_bg (擦除箭头, 使 front 干净),
 *         flush 后作废 s_cursor_save (设 -1), 再 draw_mouse_cursor (重新 save + 画箭头). */
static inline void drv_gfx_maybe_flush(void) {
    if (!g_drv_gfx_ptr || !g_drv_gfx_ptr->flush) return;
    if (g_gfx_frame_depth > 0) {
        /* 帧内: 只记 dirty, 不刷 (避免半帧内容被暴露到 front buffer) */
        g_gfx_dirty_in_frame = 1;
        return;
    }
    /* 非帧内 (早期引导 / 非 WM): 立即刷 */
    int mvis = (!g_wm_enabled && !g_compositor_active && s_mouse_visible && s_save_x >= 0);
    if (mvis) restore_cursor_bg();
    g_drv_gfx_ptr->flush();
    if (mvis) {
        s_save_x = s_save_y = -1;  /* 作废旧 save, 避免 draw_mouse_cursor restore 过期像素 */
        draw_mouse_cursor();       /* 重新 save_cursor_bg + 画箭头到新 flush 后的 front */
    }
}

/* [force_flush] 无条件马上 flush (无视 depth, 清 dirty).
 * 用于: frame_end 离开最外层帧时; 或非帧内合成路径的个别 "我必须立刻看到结果" 场景.
 * [鼠标光标保护] 同 maybe_flush — flush 前隐藏, flush 后重画. */
static inline void drv_gfx_force_flush(void) {
    if (!g_drv_gfx_ptr || !g_drv_gfx_ptr->flush) return;
    int mvis = (!g_wm_enabled && !g_compositor_active && s_mouse_visible && s_save_x >= 0);
    if (mvis) restore_cursor_bg();
    g_drv_gfx_ptr->flush();
    g_gfx_dirty_in_frame = 0;
    if (mvis) {
        s_save_x = s_save_y = -1;
        draw_mouse_cursor();
    }
}

/* [frame_begin] 进入一帧. 可嵌套 (depth++, 每层都要配对 frame_end). */
static inline void drv_gfx_frame_begin(void) {
    g_gfx_frame_depth++;
}

/* [frame_end] 离开一帧. 当 depth 回到 0 (最外层结束) 且帧内有 dirty 时, 真正 flush 一次. */
static inline void drv_gfx_frame_end(void) {
    if (g_gfx_frame_depth > 0) g_gfx_frame_depth--;
    if (g_gfx_frame_depth == 0) {
        if (g_gfx_dirty_in_frame) {
            drv_gfx_force_flush();
        }
    }
}
/* 内联辅助: 如果驱动可用, 优先驱动多扇区; 一次读失败返回 -1 让调用者不 fallback
 * (若返回 -1 调用者可直接认为失败, 因为驱动优先级高于内置实现。) */

static int disk_type = 0;
/* [修复] 返回 int (0=成功, -1=失败): 原 void 导致 disk_read_n 中
 * IDE 分支 `disk_read_sector(...) != 0` 编译错误, 且无法传播读盘失败 */
static int disk_read_sector(unsigned int lba, void *buf) {
    if (g_drv_disk_ptr && g_drv_disk_ptr->read_blocks) {
        if (g_drv_disk_ptr->read_blocks(lba, 1, buf) == 0) return 0;
        /* 驱动明确失败, 不回退 (避免两个驱动顺序问题) */
        return -1;
    }
    if (disk_type == 2) return ahci_read_sector(lba, buf);
    else if (disk_type == 1) return ide_read_sector(lba, buf);
    return -1;
}
static int disk_write_sector(unsigned int lba, const void *buf) {
    if (g_drv_disk_ptr && g_drv_disk_ptr->write_blocks) {
        if (g_drv_disk_ptr->write_blocks(lba, 1, buf) == 0) return 0;
        return -1;
    }
    if (disk_type == 2) return ahci_write_sector(lba, buf);
    else if (disk_type == 1) return ide_write_sector(lba, buf);
    return -1;
}

/* [修复] 统一多扇区读取: AHCI 一次读取全部扇区, IDE 逐扇区读取
 * 返回 0=成功, -1=失败 (调用方可据此中止, 避免长时间卡死)
 * [驱动拦截] 若磁盘驱动注册, 优先驱动 (驱动可 4K 原生扇区优化) */
static int disk_read_n(unsigned int lba, unsigned char *buf, int n) {
    if (n <= 0) return 0;
    if (g_drv_disk_ptr && g_drv_disk_ptr->read_blocks) {
        /* 驱动可能支持更大块, 这里每次最多 8192 扇区 (4MB), 足够 4K 对齐 + 大读 */
        while (n > 0) {
            int chunk = n > 8192 ? 8192 : n;
            if (g_drv_disk_ptr->read_blocks(lba, chunk, buf) != 0) return -1;
            lba += chunk;
            buf += (unsigned long)chunk * 512;
            n -= chunk;
        }
        return 0;
    }
    if (disk_type == 2) {
        /* AHCI: 支持多扇区, 最多 256 扇区/次 */
        while (n > 0) {
            int chunk = n > 256 ? 256 : n;
            if (ahci_read_blocks(lba, chunk, buf) != 0) return -1;
            lba += chunk;
            buf += chunk * 512;
            n -= chunk;
        }
    } else if (disk_type == 1) {
        for (int i = 0; i < n; i++) if (disk_read_sector(lba + i, buf + i * 512) != 0) return -1;
    } else return -1;
    return 0;
}

/* [新增] 统一多扇区写入
 * [驱动拦截] 优先磁盘驱动 */
static int disk_write_n(unsigned int lba, const unsigned char *buf, int n) {
    if (n <= 0) return 0;
    if (g_drv_disk_ptr && g_drv_disk_ptr->write_blocks) {
        while (n > 0) {
            int chunk = n > 8192 ? 8192 : n;
            if (g_drv_disk_ptr->write_blocks(lba, chunk, buf) != 0) return -1;
            lba += chunk;
            buf += (unsigned long)chunk * 512;
            n -= chunk;
        }
        return 0;
    }
    if (disk_type == 2) {
        while (n > 0) {
            int chunk = n > 256 ? 256 : n;
            if (ahci_write_blocks(lba, chunk, buf) != 0) return -1;
            lba += chunk;
            buf += chunk * 512;
            n -= chunk;
        }
    } else if (disk_type == 1) {
        for (int i = 0; i < n; i++) if (disk_write_sector(lba + i, buf + i * 512) != 0) return -1;
    } else return -1;
    return 0;
}

/* [新增] 刷新磁盘写缓存 (保证数据持久化), 用于 ext4 写入后的 sync
 * [驱动拦截] 优先磁盘驱动 */
static int disk_flush(void) {
    if (g_drv_disk_ptr && g_drv_disk_ptr->flush) {
        return g_drv_disk_ptr->flush();
    }
    if (disk_type == 2) return ahci_flush_cache();
    /* IDE 无显式 flush 命令 (IDE FLUSH CACHE = 0xE7, 此处简化省略) */
    return 0;
}

/* [新增] 获取磁盘总扇区数
 * [驱动拦截] 优先磁盘驱动 */
static unsigned long long disk_get_sector_count(void) {
    if (g_drv_disk_ptr && g_drv_disk_ptr->size_sectors) {
        return g_drv_disk_ptr->size_sectors();
    }
    if (disk_type == 2) return ahci_get_sector_count();
    return 0;
}

/* [新增] 块级读写: 以 ext4 块号为单位 (bs 对齐)
 * 前向声明, 实现在 ext4_start_lba 定义之后 */
static int ext4_read_block(unsigned int blk, void *buf);
static int ext4_write_block(unsigned int blk, const void *buf);

/* ========== ext4 Driver (Fully Fixed) ==========
 *
 * 关键修复说明:
 * 1. GDT 位置计算: 原代码固定从 block 1 读取 GDT, 对于 bs=1024 会读到超级块本身。
 *    修复: bs=1024 时 GDT 从 block 2 开始, bs>1024 时从 block 1 开始。
 *
 * 2. Extent 树遍历: ext4 默认使用 extent 而非直接块指针。原代码直接读 i_block[]
 *    作为物理块号, 对于 ext4 分区完全错误。修复: 实现 extent 树遍历, 支持
 *    depth=0 (叶节点在 inode 内) 和 depth>=1 (需读取索引块)。
 *
 * 3. 目录/文件读取: find_in_dir/read_file_content 使用 blocks[i] 直接
 *    索引, 修复为通过 ext4_block_lookup 进行逻辑块到物理块的映射。
 */
static unsigned long long ext4_start_lba; static unsigned int ext4_bs;
static unsigned int inodes_per_group, inode_size; static unsigned char *gdt_buf = (unsigned char*)0x60000;
/* [新增] 用于写操作的超级块字段 */
static unsigned int sb_blocks_per_group;   /* s_blocks_per_group */
static unsigned int sb_num_groups;         /* 块组数 */
static unsigned int sb_first_data_block;   /* s_first_data_block */
static unsigned int sb_inodes_count;       /* s_inodes_count */
static unsigned char sb_buf[1024];         /* 超级块缓存 (offset 0 = sb 起始) */
static unsigned int sb_gdt_start_block;    /* GDT 起始块号 */
static unsigned int sb_gdt_blocks;         /* GDT 占用块数 */

/* ext4 extent 结构体布局 (每个 12 字节) */
struct ext4_extent_header {
    unsigned short eh_magic;      /* 0xF30A */
    unsigned short eh_entries;    /* 条目数 */
    unsigned short eh_max;        /* 最大条目数 */
    unsigned short eh_depth;      /* 0=叶节点, >0=索引节点 */
    unsigned int eh_generation;
};

struct ext4_extent {
    unsigned int ee_block;        /* 逻辑块号 */
    unsigned short ee_len;        /* 块数 (>32768 表示未初始化) */
    unsigned short ee_start_hi;   /* 起始物理块高16位 */
    unsigned int ee_start_lo;     /* 起始物理块低32位 */
};

struct ext4_extent_idx {
    unsigned int ei_block;        /* 逻辑块号 */
    unsigned int ei_leaf_lo;      /* 子节点块号低32位 */
    unsigned short ei_leaf_hi;    /* 子节点块号高16位 */
    unsigned short ei_unused;
};

static void ext4_init(void) {
    ext4_start_lba = *(unsigned long long*)0x1500; ext4_bs = *(unsigned int*)0x1508;
    if (!ext4_start_lba || !ext4_bs) { serial_write("ext4: invalid params\n"); return; }

    /* 读取超级块 (位于分区偏移 1024 字节 = LBA +2) */
    disk_read_n((unsigned int)(ext4_start_lba + 2), sb_buf, 2);
    if (*(unsigned short*)(sb_buf + 0x38) != 0xEF53) { serial_write("ext4: bad superblock\n"); return; }

    inodes_per_group = *(unsigned int*)(sb_buf + 0x28);
    inode_size = *(unsigned short*)(sb_buf + 0x58);
    if (!inode_size) inode_size = 128;
    sb_blocks_per_group = *(unsigned int*)(sb_buf + 0x20);
    sb_first_data_block = *(unsigned int*)(sb_buf + 0x14);
    sb_inodes_count = *(unsigned int*)(sb_buf);

    serial_write("ext4: inodes_per_group="); serial_write_hex(inodes_per_group);
    serial_write(" inode_size="); serial_write_hex(inode_size);
    serial_write(" bs="); serial_write_hex(ext4_bs); serial_write("\n");

    /* [修复] GDT 起始块计算:
     * - bs=1024: 超级块在 block 1, GDT 从 block 2 开始
     * - bs>1024: 超级块在 block 0 (偏移1024字节处), GDT 从 block 1 开始 */
    sb_gdt_start_block = (ext4_bs == 1024) ? 2 : 1;

    sb_num_groups = (sb_inodes_count + inodes_per_group - 1) / inodes_per_group;
    unsigned int gdt_bytes = sb_num_groups * 32;
    sb_gdt_blocks = (gdt_bytes + ext4_bs - 1) / ext4_bs;

    serial_write("ext4: groups="); serial_write_hex(sb_num_groups);
    serial_write(" bpg="); serial_write_hex(sb_blocks_per_group);
    serial_write(" gdt_blocks="); serial_write_hex(sb_gdt_blocks); serial_write("\n");

    for (unsigned int i = 0; i < sb_gdt_blocks; i++)
        disk_read_n((unsigned int)(ext4_start_lba + (sb_gdt_start_block + i) * ext4_bs / 512),
                     gdt_buf + i * ext4_bs, ext4_bs / 512);

    serial_write("ext4: ready (rw)\n");
}

/* [块级读写实现] 以 ext4 块号为单位 (bs 对齐) */
static int ext4_read_block(unsigned int blk, void *buf) {
    return disk_read_n((unsigned int)(ext4_start_lba + blk * ext4_bs / 512), buf, ext4_bs / 512);
}
static int ext4_write_block(unsigned int blk, const void *buf) {
    return disk_write_n((unsigned int)(ext4_start_lba + blk * ext4_bs / 512), buf, ext4_bs / 512);
}

static unsigned int get_itable(unsigned int group) {
    return *(unsigned int*)(gdt_buf + group * 32 + 8);  /* bg_inode_table_lo */
}

static void read_inode(unsigned int ino, unsigned char *inode_buf) {
    if (ino == 0) return;
    /* [修复] 限制拷贝到 256 字节: 调用者多用 256 字节栈缓冲,
     * 若 inode_size>256 (如 512) 会溢出栈。ext4 关键字段均在首 128 字节内。 */
    unsigned int isz = inode_size > 256 ? 256 : inode_size;
    my_memset(inode_buf, 0, isz);  /* 预清零, 避免读盘失败时使用残留数据 */
    unsigned int group = (ino - 1) / inodes_per_group;
    unsigned int idx = (ino - 1) % inodes_per_group;
    if (group >= sb_num_groups) return;  /* 越界保护 */
    unsigned int itable = get_itable(group);
    if (!itable) return;
    unsigned int off = (idx * inode_size) % ext4_bs;
    unsigned int target = itable + (idx * inode_size) / ext4_bs;
    unsigned char *buf = (unsigned char*)0x70000;
    if (disk_read_n((unsigned int)(ext4_start_lba + target * ext4_bs / 512), buf, ext4_bs / 512) != 0)
        return;  /* 读盘失败: inode_buf 保持清零状态 */
    my_memcpy(inode_buf, buf + off, isz);
}

/* [新增] Extent 树遍历: 将逻辑块号映射为物理块号
 * 支持 depth=0 (叶节点内联在 inode i_block 中) 和 depth>=1 (需读索引块)
 * 缓冲区 0x75000 用于读取 extent 索引/叶块 */
static unsigned int ext4_block_lookup(unsigned char *inode, unsigned int logical) {
    unsigned int *i_block = (unsigned int*)(inode + 40);  /* i_block[0..14], 60字节 */
    unsigned short magic = *(unsigned short*)i_block;

    /* 检查是否使用 extent (magic = 0xF30A) */
    if (magic != 0xF30A) {
        /* 传统直接块指针 (ext2/3 兼容): i_block[0..11] = 12个直接块 */
        if (logical < 12) return i_block[logical];
        return 0;  /* 不支持间接/二级间接/三级间接块 */
    }

    /* Extent 树根节点在 i_block[0..14] (60字节)
     * 头部12字节 + 最多4个条目 (每个12字节) */
    unsigned char *node = (unsigned char*)i_block;
    struct ext4_extent_header *eh = (struct ext4_extent_header*)node;
    unsigned short entries = eh->eh_entries;
    unsigned short depth = eh->eh_depth;

    /* 迭代遍历 extent 树 */
    unsigned char *ext_buf = (unsigned char*)0x75000;
    int max_iter = 8;  /* 防止无限循环 */

    while (max_iter-- > 0) {
        if (depth == 0) {
            /* 叶节点: 直接包含 extent 条目 */
            for (int i = 0; i < entries; i++) {
                struct ext4_extent *ext = (struct ext4_extent*)(node + 12 + i * 12);
                unsigned int ee_block = ext->ee_block;
                unsigned short ee_len = ext->ee_len;

                /* 处理未初始化 extent (ee_len > 32768) */
                if (ee_len > 32768) ee_len -= 32768;

                if (logical >= ee_block && logical < ee_block + ee_len) {
                    unsigned long long start = ((unsigned long long)ext->ee_start_hi << 32) | ext->ee_start_lo;
                    return (unsigned int)(start + (logical - ee_block));
                }
            }
            return 0;  /* 稀疏块或超出范围 */
        } else {
            /* 索引节点: 包含 extent_idx 条目, 需要找到正确的子节点 */
            unsigned int found_leaf = 0;
            unsigned long long leaf_block = 0;

            for (int i = 0; i < entries; i++) {
                struct ext4_extent_idx *idx = (struct ext4_extent_idx*)(node + 12 + i * 12);

                /* 查找覆盖 logical 的索引条目 */
                if (i + 1 < entries) {
                    struct ext4_extent_idx *next = (struct ext4_extent_idx*)(node + 12 + (i + 1) * 12);
                    if (logical >= next->ei_block) continue;
                }

                leaf_block = ((unsigned long long)idx->ei_leaf_hi << 32) | idx->ei_leaf_lo;
                found_leaf = 1;
                break;
            }

            if (!found_leaf) return 0;

            /* 读取子节点块 */
            if (disk_read_n((unsigned int)(ext4_start_lba + (unsigned int)leaf_block * ext4_bs / 512),
                        ext_buf, ext4_bs / 512) != 0) {
                serial_write("ext4: extent leaf read failed\n");
                return 0;
            }

            /* 切换到子节点 */
            node = ext_buf;
            eh = (struct ext4_extent_header*)node;
            entries = eh->eh_entries;
            depth = eh->eh_depth;

            /* 验证 magic */
            if (eh->eh_magic != 0xF30A) {
                serial_write("ext4: bad extent node magic\n");
                return 0;
            }
        }
    }

    serial_write("ext4: extent lookup exceeded max depth\n");
    return 0;
}

/* 元数据/块读写缓冲 (在 read_inode_data 之前定义, 供其读取块数据时使用) */
static unsigned char ext4_meta_buf[16384];      /* 元数据/块写缓冲 (max ext4_bs=16K) */
#define EXT4_META_BUF      ext4_meta_buf

/* [新增] 读取 inode 数据: 通过 extent 映射读取所有数据块
 * [修复] 失败即中止: 某块读取失败时停止后续读取, 避免大目录导致的长时间卡死
 * [修复] 当 buf < ext4_bs 时, 先读整块到临时缓冲再拷贝能放下的部分 */
static unsigned int read_inode_data(unsigned char *inode, unsigned char *buf, unsigned int max_size) {
    unsigned int fsize = *(unsigned int*)(inode + 4);  /* i_size_lo */
    if (fsize > max_size) fsize = max_size;
    if (fsize == 0) return 0;

    unsigned int blks = (fsize + ext4_bs - 1) / ext4_bs;
    for (unsigned int i = 0; i < blks; i++) {
        unsigned int off = i * ext4_bs;
        if (off >= max_size) break;  /* 缓冲区已满 */

        unsigned int phys = ext4_block_lookup(inode, i);
        if (phys) {
            /* 读整块到临时缓冲, 再拷贝能放下的部分到 buf
             * (修复: 当 buf < ext4_bs 时, 原代码直接 break 导致小文件读不出) */
            if (ext4_read_block(phys, EXT4_META_BUF) != 0) {
                return off;  /* 读盘失败 */
            }
            unsigned int copy = ext4_bs;
            if (off + copy > fsize)    copy = fsize - off;
            if (off + copy > max_size) copy = max_size - off;
            my_memcpy(buf + off, EXT4_META_BUF, copy);
        } else {
            /* 稀疏块或未初始化: 填零 */
            unsigned int fill = ext4_bs;
            if (off + fill > fsize)    fill = fsize - off;
            if (off + fill > max_size) fill = max_size - off;
            my_memset(buf + off, 0, fill);
        }
    }
    return fsize;
}

/* [关键修复] 目录扫描读缓冲: 静态 BSS 数组 (真实 RAM)。
 * 原先使用 0xC0000 (Video BIOS ROM 区), 磁盘读入的数据写入 ROM 丢失,
 * find_in_dir 读到 BIOS 垃圾, 导致 cd 报 "not found"。 */
static unsigned char dir_scan_buf[131072];   /* 128KB 目录扫描缓冲 */

static unsigned int find_in_dir(unsigned int dir_ino, const char *name) {
    unsigned char inode[256]; read_inode(dir_ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if (!(mode & 0x4000)) return 0;

    /* [关键修复] 使用 dir_scan_buf (静态 RAM), 不再用 0xC0000 ROM 区 */
    unsigned char *dir_buf = dir_scan_buf;
    unsigned int fsize = read_inode_data(inode, dir_buf, 128 * 1024);

    unsigned char *ptr = dir_buf;
    while (ptr + 8 <= dir_buf + fsize) {
        unsigned int ino = *(unsigned int*)ptr;
        unsigned short rec = *(unsigned short*)(ptr + 4);
        unsigned char nlen = *(unsigned char*)(ptr + 6);
        /* 合法性检查: rec 至少 8 字节且不越界 */
        if (rec < 8 || ptr + rec > dir_buf + fsize) break;
        if (ino && nlen && nlen <= rec - 8 && my_strncmp((char*)ptr + 8, name, nlen) == 0 && name[nlen] == 0)
            return ino;
        ptr += rec;
    }
    return 0;
}

static int read_file_content(unsigned int ino, unsigned char *buf, unsigned int max_size) {
    unsigned char inode[256]; read_inode(ino, inode);
    return (int)read_inode_data(inode, buf, max_size);
}

/* [新增] 分块流式读取: 从 inode 数据的 file_offset 处读取 size 字节到 target。
 * 支持大文件直接加载到任意内存地址 (绕过 fs_load_buf 1MB 上限),
 * 逐块读 ext4_bs 到 EXT4_META_BUF 再拷贝, 跳过 offset 之前/size 之后的部分。
 * 用于 run_efs 流式加载大体积 .efs 二进制到 load_addr。返回实际读取字节数。 */
static unsigned int read_file_range(unsigned int ino, unsigned char *target,
                                    unsigned int file_offset, unsigned int size) {
    unsigned char inode[256]; read_inode(ino, inode);
    unsigned int fsize = *(unsigned int*)(inode + 4);   /* i_size_lo */
    if (file_offset >= fsize) return 0;
    unsigned int want = size;
    if (file_offset + want > fsize) want = fsize - file_offset;
    unsigned int first_blk = file_offset / ext4_bs;
    unsigned int last_blk  = (file_offset + want - 1) / ext4_bs;
    unsigned int done = 0;
    for (unsigned int b = first_blk; b <= last_blk; b++) {
        unsigned int phys = ext4_block_lookup(inode, b);
        unsigned int blk_start = b * ext4_bs;
        unsigned int blk_end   = blk_start + ext4_bs;
        unsigned int copy_start = (file_offset > blk_start) ? file_offset : blk_start;
        unsigned int copy_end   = (file_offset + want < blk_end) ? (file_offset + want) : blk_end;
        unsigned int copy = copy_end - copy_start;
        unsigned int src_off = copy_start - blk_start;
        if (phys) {
            if (ext4_read_block(phys, EXT4_META_BUF) != 0) break;   /* 读盘失败中止 */
            my_memcpy(target + done, EXT4_META_BUF + src_off, copy);
        } else {
            my_memset(target + done, 0, copy);   /* 稀疏/未初始化块填零 */
        }
        done += copy;
    }
    return done;
}

/* ========== ext4 Write Layer ==========
 *
 * 实现完整的 ext4 写入支持:
 *  - 块位图分配/释放 (alloc_block / free_block)
 *  - inode 位图分配/释放 (alloc_inode / free_inode)
 *  - inode 写回 (write_inode)
 *  - 目录项添加/删除 (add_dir_entry / remove_dir_entry)
 *  - extent 树更新 (extent_add)
 *  - GDT/SB 计数维护 (update_gdt_counts / update_sb_counts)
 *  - 高级操作: ext4_create_file / ext4_mkdir / ext4_unlink / ext4_write_file
 *
 * 缓冲区布局 (均为内核 BSS 静态数组, 位于真实 RAM):
 *   ext4_blk_bmp_buf - 块/inode 位图临时缓冲 (ext4_bs)
 *   ext4_dir_buf     - 目录块缓冲 (ext4_bs)
 *   ext4_meta_buf    - 元数据写缓冲 (ext4_bs)
 *   dir_scan_buf     - 目录扫描读缓冲 (128KB, 用于 ls/cd/du/rmdir)
 *
 * 注意: 不实现 JBD2 日志, 采用有序写入 (先数据后元数据)。
 * 对虚拟磁盘测试足够; 真实生产环境需日志保证崩溃一致性。
 */

/* [关键修复] 缓冲区改为静态 BSS 数组, 不再使用 0xB0000-0xE0000 物理地址。
 * 原先 0xB0000-0xBFFFF 是 VGA 显存, 0xC0000-0xEFFFF 是 BIOS ROM 区:
 *   - 0xC0000 (目录读缓冲) 是 Video BIOS ROM, 写入会丢失 -> find_in_dir 读到
 *     BIOS 垃圾而非目录项, 导致 mkdir 成功但 cd 报 "not found"
 *   - 0xB0000-0xB8000 (VGA 显存) 在 QEMU 上可写但非通用 RAM
 * 静态 BSS 数组位于内核映像所在的真实 RAM, 读写可靠, 彻底消除此类故障。*/
static unsigned char ext4_blk_bmp_buf[16384];   /* 块/inode 位图缓冲 (max ext4_bs=16K) */
static unsigned char ext4_dir_buf[16384];       /* 目录块缓冲 */
#define EXT4_BLK_BMP_BUF  ext4_blk_bmp_buf
#define EXT4_DIR_BUF       ext4_dir_buf

/* 组描述符字段读写 (32字节组描述符, 低32位字段) */
static unsigned int gdt_get_block_bitmap(unsigned int group) {
    return *(unsigned int*)(gdt_buf + group * 32 + 0);
}
static unsigned int gdt_get_inode_bitmap(unsigned int group) {
    return *(unsigned int*)(gdt_buf + group * 32 + 4);
}
static unsigned int gdt_get_inode_table(unsigned int group) {
    return *(unsigned int*)(gdt_buf + group * 32 + 8);
}
static unsigned short gdt_get_free_blocks(unsigned int group) {
    return *(unsigned short*)(gdt_buf + group * 32 + 12);
}
static unsigned short gdt_get_free_inodes(unsigned int group) {
    return *(unsigned short*)(gdt_buf + group * 32 + 14);
}
static unsigned short gdt_get_used_dirs(unsigned int group) {
    return *(unsigned short*)(gdt_buf + group * 32 + 16);
}
static void gdt_set_free_blocks(unsigned int group, unsigned short v) {
    *(unsigned short*)(gdt_buf + group * 32 + 12) = v;
}
static void gdt_set_free_inodes(unsigned int group, unsigned short v) {
    *(unsigned short*)(gdt_buf + group * 32 + 14) = v;
}
static void gdt_set_used_dirs(unsigned int group, unsigned short v) {
    *(unsigned short*)(gdt_buf + group * 32 + 16) = v;
}

/* 写回单个组描述符到磁盘 GDT (每组描述符 32 字节) */
static void gdt_write_back(unsigned int group) {
    unsigned int byte_off = group * 32;
    unsigned int blk_in_gdt = byte_off / ext4_bs;
    unsigned int off_in_blk = byte_off % ext4_bs;
    /* 读-改-写 GDT 块 */
    if (ext4_read_block(sb_gdt_start_block + blk_in_gdt, EXT4_META_BUF) != 0) return;
    my_memcpy(EXT4_META_BUF + off_in_blk, gdt_buf + byte_off, 32);
    ext4_write_block(sb_gdt_start_block + blk_in_gdt, EXT4_META_BUF);
}

/* 超级块计数: s_free_blocks_count_lo (0x0C), s_free_inodes_count (0x10)
 * 注意: 完整实现应同时更新 _hi 字段, 此处假设块数 < 4G */
static void sb_add_free_blocks(int delta) {
    unsigned int v = *(unsigned int*)(sb_buf + 0x0C);
    if (delta > 0) v += delta; else v -= (unsigned int)(-delta);
    *(unsigned int*)(sb_buf + 0x0C) = v;
}
static void sb_add_free_inodes(int delta) {
    unsigned int v = *(unsigned int*)(sb_buf + 0x10);
    if (delta > 0) v += delta; else v -= (unsigned int)(-delta);
    *(unsigned int*)(sb_buf + 0x10) = v;
}

/* 写回超级块到磁盘 (位于分区偏移 1024, 即 bs=1024 时 block 1, 否则 block 0 的偏移1024处) */
static void sb_write_back(void) {
    if (ext4_bs == 1024) {
        /* 超级块占 block 1 整块 */
        ext4_write_block(1, sb_buf);
    } else {
        /* block 0 的前 1024 字节是引导区, 超级块在偏移 1024 */
        unsigned char *blk = EXT4_META_BUF;
        ext4_read_block(0, blk);
        my_memcpy(blk + 1024, sb_buf, 1024);
        ext4_write_block(0, blk);
    }
}

/* [块分配] 在指定组的块位图中找到第一个空闲块并置位
 * 返回物理块号, 0 表示失败 (块号从 1 开始有效, 0 用于错误) */
static unsigned int alloc_block_in_group(unsigned int group) {
    unsigned int bmp_blk = gdt_get_block_bitmap(group);
    if (!bmp_blk || bmp_blk == 0xFFFFFFFF) return 0;
    if (ext4_read_block(bmp_blk, EXT4_BLK_BMP_BUF) != 0) return 0;

    unsigned int bits = ext4_bs * 8;  /* 位图位数 = 一个块 */
    for (unsigned int i = 0; i < bits; i++) {
        unsigned char mask = 1 << (i & 7);
        if (!(EXT4_BLK_BMP_BUF[i >> 3] & mask)) {
            EXT4_BLK_BMP_BUF[i >> 3] |= mask;
            ext4_write_block(bmp_blk, EXT4_BLK_BMP_BUF);
            /* 计算物理块号:
             * 块组 g 的第一个数据块 = first_data_block + g * blocks_per_group
             * 位图第 i 位对应组内第 i 个块 */
            unsigned int phys = sb_first_data_block + group * sb_blocks_per_group + i;
            /* 更新 GDT 计数 */
            unsigned short fb = gdt_get_free_blocks(group);
            if (fb > 0) gdt_set_free_blocks(group, fb - 1);
            gdt_write_back(group);
            sb_add_free_blocks(-1);
            sb_write_back();
            return phys;
        }
    }
    return 0;
}

/* [块分配] 跨组查找空闲块 (优先在 group, 满则向后扫) */
static unsigned int alloc_block(unsigned int hint_group) {
    for (unsigned int g = hint_group; g < sb_num_groups; g++) {
        if (gdt_get_free_blocks(g) > 0) {
            unsigned int b = alloc_block_in_group(g);
            if (b) return b;
        }
    }
    for (unsigned int g = 0; g < hint_group; g++) {
        if (gdt_get_free_blocks(g) > 0) {
            unsigned int b = alloc_block_in_group(g);
            if (b) return b;
        }
    }
    serial_write("ext4: no free blocks\n");
    return 0;
}

/* [块释放] 清除位图位, 增加计数 */
static void free_block(unsigned int phys_blk) {
    if (!phys_blk) return;
    unsigned int group = (phys_blk - sb_first_data_block) / sb_blocks_per_group;
    unsigned int idx = (phys_blk - sb_first_data_block) % sb_blocks_per_group;
    if (group >= sb_num_groups) return;
    unsigned int bmp_blk = gdt_get_block_bitmap(group);
    if (ext4_read_block(bmp_blk, EXT4_BLK_BMP_BUF) != 0) return;
    EXT4_BLK_BMP_BUF[idx >> 3] &= ~(1 << (idx & 7));
    ext4_write_block(bmp_blk, EXT4_BLK_BMP_BUF);
    unsigned short fb = gdt_get_free_blocks(group);
    gdt_set_free_blocks(group, fb + 1);
    gdt_write_back(group);
    sb_add_free_blocks(1);
    sb_write_back();
}

/* [inode 分配] 在指定组的 inode 位图中分配一个 inode
 * 返回 inode 号 (>0), 0 表示失败 */
static unsigned int alloc_inode_in_group(unsigned int group) {
    unsigned int bmp_blk = gdt_get_inode_bitmap(group);
    if (!bmp_blk || bmp_blk == 0xFFFFFFFF) return 0;
    if (ext4_read_block(bmp_blk, EXT4_BLK_BMP_BUF) != 0) return 0;
    /* inode 位图位数 = inodes_per_group */
    for (unsigned int i = 0; i < inodes_per_group; i++) {
        unsigned char mask = 1 << (i & 7);
        if (!(EXT4_BLK_BMP_BUF[i >> 3] & mask)) {
            EXT4_BLK_BMP_BUF[i >> 3] |= mask;
            ext4_write_block(bmp_blk, EXT4_BLK_BMP_BUF);
            /* inode 号 = group * inodes_per_group + i + 1 (1-based) */
            unsigned int ino = group * inodes_per_group + i + 1;
            unsigned short fi = gdt_get_free_inodes(group);
            if (fi > 0) gdt_set_free_inodes(group, fi - 1);
            gdt_write_back(group);
            sb_add_free_inodes(-1);
            sb_write_back();
            return ino;
        }
    }
    return 0;
}

/* [inode 分配] 跨组查找 (目录尽量分散, 简化: 顺序查找) */
static unsigned int alloc_inode(unsigned int hint_group, int is_dir) {
    (void)is_dir;  /* 简化: 不实现 ORLOFF 目录分配策略 */
    for (unsigned int g = hint_group; g < sb_num_groups; g++) {
        if (gdt_get_free_inodes(g) > 0) {
            unsigned int ino = alloc_inode_in_group(g);
            if (ino) {
                if (is_dir) { gdt_set_used_dirs(g, gdt_get_used_dirs(g) + 1); gdt_write_back(g); }
                return ino;
            }
        }
    }
    for (unsigned int g = 0; g < hint_group; g++) {
        if (gdt_get_free_inodes(g) > 0) {
            unsigned int ino = alloc_inode_in_group(g);
            if (ino) {
                if (is_dir) { gdt_set_used_dirs(g, gdt_get_used_dirs(g) + 1); gdt_write_back(g); }
                return ino;
            }
        }
    }
    serial_write("ext4: no free inodes\n");
    return 0;
}

/* [inode 释放] */
static void free_inode(unsigned int ino) {
    if (!ino) return;
    unsigned int group = (ino - 1) / inodes_per_group;
    unsigned int idx = (ino - 1) % inodes_per_group;
    if (group >= sb_num_groups) return;
    unsigned int bmp_blk = gdt_get_inode_bitmap(group);
    if (ext4_read_block(bmp_blk, EXT4_BLK_BMP_BUF) != 0) return;
    EXT4_BLK_BMP_BUF[idx >> 3] &= ~(1 << (idx & 7));
    ext4_write_block(bmp_blk, EXT4_BLK_BMP_BUF);
    unsigned short fi = gdt_get_free_inodes(group);
    gdt_set_free_inodes(group, fi + 1);
    gdt_write_back(group);
    sb_add_free_inodes(1);
    sb_write_back();
}

/* [inode 写回] 将 inode_buf 写回磁盘 inode 表
 * [修复] 仅拷贝 min(inode_size,256) 字节: 调用者缓冲为 256 字节,
 * 且保留磁盘上扩展属性区 (256+) 不被破坏 */
static void write_inode(unsigned int ino, const unsigned char *inode_buf) {
    if (!ino) return;
    unsigned int isz = inode_size > 256 ? 256 : inode_size;
    unsigned int group = (ino - 1) / inodes_per_group;
    unsigned int idx = (ino - 1) % inodes_per_group;
    unsigned int itable = gdt_get_inode_table(group);
    unsigned int off = (idx * inode_size) % ext4_bs;
    unsigned int target = itable + (idx * inode_size) / ext4_bs;
    if (ext4_read_block(target, EXT4_META_BUF) != 0) return;
    my_memcpy(EXT4_META_BUF + off, inode_buf, isz);
    ext4_write_block(target, EXT4_META_BUF);
}

/* [extent 添加] 为 inode 添加一段连续物理块
 * 支持 depth=0 (内联) 和 depth=1 (索引+叶节点) 自动分裂
 * depth=0 满 4 条目时, 分配叶块, 提升为 depth=1
 * depth=1 时, 在最后一个叶节点添加, 叶满则创建新叶+新索引
 * 使用静态 BSS 缓冲区 leaf_buf/idx_buf (真实 RAM) */
static unsigned char ext4_leaf_buf[16384];   /* extent 叶节点缓冲 (max ext4_bs) */
static unsigned char ext4_idx_buf[16384];    /* extent 索引块缓冲 (depth>=2) */
static int extent_add(unsigned char *inode, unsigned int logical,
                      unsigned int phys, unsigned int count) {
    unsigned int *i_block = (unsigned int*)(inode + 40);
    struct ext4_extent_header *eh = (struct ext4_extent_header*)i_block;
    unsigned char *leaf_buf = ext4_leaf_buf;
    unsigned char *idx_buf = ext4_idx_buf;
    unsigned short leaf_max = (ext4_bs - 12) / 12;  /* 叶节点/索引块最大条目数 */

    if (eh->eh_magic != 0xF30A) {
        eh->eh_magic = 0xF30A;
        eh->eh_entries = 0;
        eh->eh_max = 4;
        eh->eh_depth = 0;
        eh->eh_generation = 0;
    }

    if (eh->eh_depth == 0) {
        /* --- depth=0: 内联叶节点 --- */
        if (eh->eh_entries < eh->eh_max) {
            struct ext4_extent *ext = (struct ext4_extent*)((unsigned char*)i_block + 12 + eh->eh_entries * 12);
            ext->ee_block = logical;
            ext->ee_len = (unsigned short)count;
            ext->ee_start_hi = 0;
            ext->ee_start_lo = phys;
            eh->eh_entries++;
            return 0;
        }
        /* 内联满了, 分裂为 depth=1 */
        unsigned int group = 0;
        unsigned int leaf_phys = alloc_block(group);
        if (!leaf_phys) return -1;
        /* 复制现有 4 条目到叶块 */
        my_memset(leaf_buf, 0, ext4_bs);
        struct ext4_extent_header *leh = (struct ext4_extent_header*)leaf_buf;
        leh->eh_magic = 0xF30A;
        leh->eh_entries = eh->eh_entries;
        leh->eh_max = leaf_max;
        leh->eh_depth = 0;
        leh->eh_generation = 0;
        my_memcpy(leaf_buf + 12, (unsigned char*)i_block + 12, eh->eh_entries * 12);
        /* 添加新条目到叶块 */
        struct ext4_extent *ne = (struct ext4_extent*)(leaf_buf + 12 + leh->eh_entries * 12);
        ne->ee_block = logical;
        ne->ee_len = (unsigned short)count;
        ne->ee_start_hi = 0;
        ne->ee_start_lo = phys;
        leh->eh_entries++;
        ext4_write_block(leaf_phys, leaf_buf);
        /* inode 中创建索引 */
        my_memset(i_block, 0, 60);
        eh->eh_magic = 0xF30A;
        eh->eh_entries = 1;
        eh->eh_max = 4;
        eh->eh_depth = 1;
        eh->eh_generation = 0;
        struct ext4_extent_idx *idx = (struct ext4_extent_idx*)((unsigned char*)i_block + 12);
        idx->ei_block = 0;
        idx->ei_leaf_lo = leaf_phys;
        idx->ei_leaf_hi = 0;
        idx->ei_unused = 0;
        return 0;
    }

    /* --- depth>=1: 索引节点 ---
     * 顺序追加场景: 始终向最后一个索引/叶节点添加。
     * depth=1: inode 内联索引 (最多 4 条目), 索引指向叶块
     * depth=2: inode 单条索引指向一个 depth=1 索引块, 该块再指向叶块
     *   (单文件容量: bs=4096 时约 340*340*4096 ≈ 473MB; bs=1024 时约 84*84*1024 ≈ 7MB)
     * 当 depth=1 内联索引满 (4 条目) 时, 提升为 depth=2 */
    if (eh->eh_depth == 2) {
        /* depth=2: 读取索引块, 在其中查找最后叶节点 */
        struct ext4_extent_idx *root_idx = (struct ext4_extent_idx*)((unsigned char*)i_block + 12);
        unsigned int idx_phys = root_idx->ei_leaf_lo;
        if (ext4_read_block(idx_phys, idx_buf) != 0) return -1;
        struct ext4_extent_header *ieh = (struct ext4_extent_header*)idx_buf;
        struct ext4_extent_idx *last_idx2 = (struct ext4_extent_idx*)(idx_buf + 12 + (ieh->eh_entries - 1) * 12);
        unsigned int leaf_phys = last_idx2->ei_leaf_lo;
        if (ext4_read_block(leaf_phys, leaf_buf) != 0) return -1;
        struct ext4_extent_header *leh = (struct ext4_extent_header*)leaf_buf;
        if (leh->eh_entries < leaf_max) {
            struct ext4_extent *ext = (struct ext4_extent*)(leaf_buf + 12 + leh->eh_entries * 12);
            ext->ee_block = logical; ext->ee_len = (unsigned short)count;
            ext->ee_start_hi = 0; ext->ee_start_lo = phys;
            leh->eh_entries++;
            ext4_write_block(leaf_phys, leaf_buf);
            return 0;
        }
        /* 叶满: 新建叶 + 索引块中添加索引条目 */
        unsigned int new_leaf = alloc_block(0);
        if (!new_leaf) return -1;
        if (ieh->eh_entries >= ieh->eh_max) {
            serial_write("ext4: depth-2 index block full (depth 3 not supported)\n");
            free_block(new_leaf); return -1;
        }
        my_memset(leaf_buf, 0, ext4_bs);
        leh->eh_magic = 0xF30A; leh->eh_entries = 1; leh->eh_max = leaf_max;
        leh->eh_depth = 0; leh->eh_generation = 0;
        struct ext4_extent *ne = (struct ext4_extent*)(leaf_buf + 12);
        ne->ee_block = logical; ne->ee_len = (unsigned short)count;
        ne->ee_start_hi = 0; ne->ee_start_lo = phys;
        ext4_write_block(new_leaf, leaf_buf);
        struct ext4_extent_idx *nidx = (struct ext4_extent_idx*)(idx_buf + 12 + ieh->eh_entries * 12);
        nidx->ei_block = logical; nidx->ei_leaf_lo = new_leaf;
        nidx->ei_leaf_hi = 0; nidx->ei_unused = 0;
        ieh->eh_entries++;
        ext4_write_block(idx_phys, idx_buf);
        return 0;
    }

    /* depth=1: inode 内联索引 */
    struct ext4_extent_idx *last_idx = (struct ext4_extent_idx*)((unsigned char*)i_block + 12 + (eh->eh_entries - 1) * 12);
    unsigned int leaf_phys = last_idx->ei_leaf_lo;
    if (ext4_read_block(leaf_phys, leaf_buf) != 0) return -1;
    struct ext4_extent_header *leh = (struct ext4_extent_header*)leaf_buf;
    if (leh->eh_entries < leaf_max) {
        /* 叶节点有空间 */
        struct ext4_extent *ext = (struct ext4_extent*)(leaf_buf + 12 + leh->eh_entries * 12);
        ext->ee_block = logical;
        ext->ee_len = (unsigned short)count;
        ext->ee_start_hi = 0;
        ext->ee_start_lo = phys;
        leh->eh_entries++;
        ext4_write_block(leaf_phys, leaf_buf);
        return 0;
    }
    /* 叶满, 需要新建叶节点 + 新索引条目 */
    unsigned int new_leaf = alloc_block(0);
    if (!new_leaf) return -1;
    if (eh->eh_entries >= eh->eh_max) {
        /* [完善] depth=1 内联索引已满 (4 条目): 提升为 depth=2
         * 1) 分配新索引块, 将 inode 中 4 个索引条目搬迁至该块 (depth=1)
         * 2) inode 重置为 depth=2, 单条索引指向新索引块
         * 3) 在新索引块中追加指向 new_leaf 的索引条目 */
        unsigned int idx_phys = alloc_block(0);
        if (!idx_phys) { free_block(new_leaf); return -1; }
        unsigned int first_block = ((struct ext4_extent_idx*)((unsigned char*)i_block + 12))->ei_block;
        my_memset(idx_buf, 0, ext4_bs);
        struct ext4_extent_header *ieh = (struct ext4_extent_header*)idx_buf;
        ieh->eh_magic = 0xF30A;
        ieh->eh_entries = eh->eh_entries;   /* 4 */
        ieh->eh_max = leaf_max;
        ieh->eh_depth = 1;
        ieh->eh_generation = 0;
        my_memcpy(idx_buf + 12, (unsigned char*)i_block + 12, eh->eh_entries * 12);
        /* 在索引块末尾追加新叶的索引条目 */
        struct ext4_extent_idx *nidx = (struct ext4_extent_idx*)(idx_buf + 12 + ieh->eh_entries * 12);
        nidx->ei_block = logical; nidx->ei_leaf_lo = new_leaf;
        nidx->ei_leaf_hi = 0; nidx->ei_unused = 0;
        ieh->eh_entries++;
        ext4_write_block(idx_phys, idx_buf);
        /* 创建新叶节点内容 (复用 leaf_buf, 旧叶内容不再需要) */
        my_memset(leaf_buf, 0, ext4_bs);
        leh->eh_magic = 0xF30A; leh->eh_entries = 1; leh->eh_max = leaf_max;
        leh->eh_depth = 0; leh->eh_generation = 0;
        struct ext4_extent *ne = (struct ext4_extent*)(leaf_buf + 12);
        ne->ee_block = logical; ne->ee_len = (unsigned short)count;
        ne->ee_start_hi = 0; ne->ee_start_lo = phys;
        ext4_write_block(new_leaf, leaf_buf);
        /* inode 重置为 depth=2 */
        my_memset(i_block, 0, 60);
        eh->eh_magic = 0xF30A; eh->eh_entries = 1; eh->eh_max = 4;
        eh->eh_depth = 2; eh->eh_generation = 0;
        struct ext4_extent_idx *root_idx = (struct ext4_extent_idx*)((unsigned char*)i_block + 12);
        root_idx->ei_block = first_block; root_idx->ei_leaf_lo = idx_phys;
        root_idx->ei_leaf_hi = 0; root_idx->ei_unused = 0;
        return 0;
    }
    /* 索引有空间: 新建叶节点 + inode 中追加索引条目 */
    my_memset(leaf_buf, 0, ext4_bs);
    leh->eh_magic = 0xF30A;
    leh->eh_entries = 1;
    leh->eh_max = leaf_max;
    leh->eh_depth = 0;
    leh->eh_generation = 0;
    struct ext4_extent *ne = (struct ext4_extent*)(leaf_buf + 12);
    ne->ee_block = logical;
    ne->ee_len = (unsigned short)count;
    ne->ee_start_hi = 0;
    ne->ee_start_lo = phys;
    ext4_write_block(new_leaf, leaf_buf);
    /* 添加索引条目 */
    struct ext4_extent_idx *nidx = (struct ext4_extent_idx*)((unsigned char*)i_block + 12 + eh->eh_entries * 12);
    nidx->ei_block = logical;
    nidx->ei_leaf_lo = new_leaf;
    nidx->ei_leaf_hi = 0;
    nidx->ei_unused = 0;
    eh->eh_entries++;
    return 0;
}

/* [extent 释放] 递归释放 inode 的所有数据块
 * 支持 depth=0 (内联叶), depth=1 (索引→叶), depth=2 (索引→索引块→叶)
 * inode 为已读取的 inode 缓冲区 */
static void extent_free_all(unsigned char *inode) {
    unsigned int *i_block = (unsigned int*)(inode + 40);
    if (*(unsigned short*)i_block != 0xF30A) return;
    struct ext4_extent_header *eh = (struct ext4_extent_header*)i_block;
    unsigned char *leaf_buf = ext4_leaf_buf;   /* [关键修复] 静态 RAM, 不再用 0xB9000 VGA 区 */
    unsigned char *idx_buf = ext4_idx_buf;

    if (eh->eh_depth == 0) {
        /* 直接释放 extent */
        for (int i = 0; i < eh->eh_entries; i++) {
            struct ext4_extent *ext = (struct ext4_extent*)((unsigned char*)i_block + 12 + i * 12);
            unsigned int start = ext->ee_start_lo;
            unsigned short elen = ext->ee_len;
            if (elen > 32768) elen -= 32768;
            for (unsigned int k = 0; k < elen; k++) free_block(start + k);
        }
        return;
    }

    /* depth>=1: inode 中是 extent_idx 条目。
     * depth=1 时 ei_leaf 指向叶块; depth=2 时 ei_leaf 指向 depth=1 索引块 */
    for (int i = 0; i < eh->eh_entries; i++) {
        struct ext4_extent_idx *idx = (struct ext4_extent_idx*)((unsigned char*)i_block + 12 + i * 12);
        unsigned int child_phys = idx->ei_leaf_lo;

        if (eh->eh_depth == 1) {
            /* child 是叶节点: 释放其 extent + 叶块本身 */
            if (ext4_read_block(child_phys, leaf_buf) != 0) continue;
            struct ext4_extent_header *leh = (struct ext4_extent_header*)leaf_buf;
            for (int j = 0; j < leh->eh_entries; j++) {
                struct ext4_extent *ext = (struct ext4_extent*)(leaf_buf + 12 + j * 12);
                unsigned int start = ext->ee_start_lo;
                unsigned short elen = ext->ee_len;
                if (elen > 32768) elen -= 32768;
                for (unsigned int k = 0; k < elen; k++) free_block(start + k);
            }
            free_block(child_phys);
        } else {
            /* depth>=2: child 是 depth=1 索引块, 遍历其索引释放各叶 + 索引块本身 */
            if (ext4_read_block(child_phys, idx_buf) != 0) continue;
            struct ext4_extent_header *ieh = (struct ext4_extent_header*)idx_buf;
            for (int j = 0; j < ieh->eh_entries; j++) {
                struct ext4_extent_idx *sub = (struct ext4_extent_idx*)(idx_buf + 12 + j * 12);
                unsigned int leaf_phys = sub->ei_leaf_lo;
                if (ext4_read_block(leaf_phys, leaf_buf) != 0) continue;
                struct ext4_extent_header *leh = (struct ext4_extent_header*)leaf_buf;
                for (int k = 0; k < leh->eh_entries; k++) {
                    struct ext4_extent *ext = (struct ext4_extent*)(leaf_buf + 12 + k * 12);
                    unsigned int start = ext->ee_start_lo;
                    unsigned short elen = ext->ee_len;
                    if (elen > 32768) elen -= 32768;
                    for (unsigned int m = 0; m < elen; m++) free_block(start + m);
                }
                free_block(leaf_phys);
            }
            free_block(child_phys);  /* 释放索引块本身 */
        }
    }
}

/* [目录项添加] 在目录 dir_ino 中添加 (name -> ino, file_type)
 * 策略: 扫描现有目录块找空闲空间 (rec_len > 实际需要的), 否则分配新块
 * 成功返回 0 */
static int add_dir_entry(unsigned int dir_ino, const char *name,
                         unsigned int ino, unsigned char file_type) {
    unsigned char dir_inode[256];
    read_inode(dir_ino, dir_inode);
    unsigned int fsize = *(unsigned int*)(dir_inode + 4);  /* i_size_lo */
    unsigned int nlen = my_strlen(name);
    if (nlen > 255) nlen = 255;
    /* 新目录项所需空间: 8字节头 + name_len 向上取整到 4 */
    unsigned int need = 8 + ((nlen + 3) & ~3);

    /* 遍历现有目录块, 尝试在末尾分割 rec_len */
    unsigned int blks = (fsize + ext4_bs - 1) / ext4_bs;
    for (unsigned int b = 0; b < blks; b++) {
        unsigned int phys = ext4_block_lookup(dir_inode, b);
        if (!phys) continue;
        if (ext4_read_block(phys, EXT4_DIR_BUF) != 0) continue;
        unsigned int off = 0;
        while (off < ext4_bs) {
            unsigned int entry_ino = *(unsigned int*)(EXT4_DIR_BUF + off);
            unsigned short rec = *(unsigned short*)(EXT4_DIR_BUF + off + 4);
            unsigned char elen = *(unsigned char*)(EXT4_DIR_BUF + off + 6);
            /* [修复] 原代码误用未定义的 ptr/dir_buf (编译错误), 此处遍历单个目录块,
             * 边界应以块内偏移 off 对齐到 ext4_bs */
            if (rec < 8 || off + rec > ext4_bs) break;
            /* 实际占用 = 8 + ((elen+3)&~3); rec_len 末尾条目可能含填充 */
            unsigned int actual = 8 + ((elen + 3) & ~3);
            if (entry_ino != 0 && rec - actual >= need) {
                /* 分割: 当前条目缩短到 actual, 后部作为新条目 */
                unsigned short new_rec = rec - (unsigned short)actual;
                *(unsigned short*)(EXT4_DIR_BUF + off + 4) = (unsigned short)actual;
                unsigned int noff = off + actual;
                *(unsigned int*)(EXT4_DIR_BUF + noff) = ino;
                *(unsigned short*)(EXT4_DIR_BUF + noff + 4) = new_rec;
                *(unsigned char*)(EXT4_DIR_BUF + noff + 6) = (unsigned char)nlen;
                *(unsigned char*)(EXT4_DIR_BUF + noff + 7) = file_type;
                my_memcpy(EXT4_DIR_BUF + noff + 8, name, nlen);
                ext4_write_block(phys, EXT4_DIR_BUF);
                return 0;
            }
            off += rec;
        }
    }

    /* 无空间, 分配新目录块 */
    unsigned int group = (dir_ino - 1) / inodes_per_group;
    unsigned int new_phys = alloc_block(group);
    if (!new_phys) return -1;
    my_memset(EXT4_DIR_BUF, 0, ext4_bs);
    /* 整块作为一个目录项 (rec_len = ext4_bs) */
    *(unsigned int*)(EXT4_DIR_BUF) = ino;
    *(unsigned short*)(EXT4_DIR_BUF + 4) = (unsigned short)ext4_bs;
    *(unsigned char*)(EXT4_DIR_BUF + 6) = (unsigned char)nlen;
    *(unsigned char*)(EXT4_DIR_BUF + 7) = file_type;
    my_memcpy(EXT4_DIR_BUF + 8, name, nlen);
    ext4_write_block(new_phys, EXT4_DIR_BUF);

    /* 在 inode 的 extent 树中添加新块 (逻辑块号 = blks) */
    if (extent_add(dir_inode, blks, new_phys, 1) != 0) {
        free_block(new_phys);
        return -1;
    }
    /* 更新目录大小 */
    unsigned int new_size = (blks + 1) * ext4_bs;
    *(unsigned int*)(dir_inode + 4) = new_size;
    write_inode(dir_ino, dir_inode);
    return 0;
}

/* [目录项删除] 在目录中清空指定 inode 的目录项 (ino 置0, 不收缩 rec_len)
 * 返回 0=成功, -1=未找到 */
static int remove_dir_entry(unsigned int dir_ino, unsigned int target_ino) {
    unsigned char dir_inode[256];
    read_inode(dir_ino, dir_inode);
    unsigned int fsize = *(unsigned int*)(dir_inode + 4);
    unsigned int blks = (fsize + ext4_bs - 1) / ext4_bs;
    for (unsigned int b = 0; b < blks; b++) {
        unsigned int phys = ext4_block_lookup(dir_inode, b);
        if (!phys) continue;
        if (ext4_read_block(phys, EXT4_DIR_BUF) != 0) continue;
        unsigned int off = 0;
        unsigned int prev_off = 0xFFFFFFFF;
        while (off < ext4_bs) {
            unsigned int entry_ino = *(unsigned int*)(EXT4_DIR_BUF + off);
            unsigned short rec = *(unsigned short*)(EXT4_DIR_BUF + off + 4);
            /* [修复] 原代码误用未定义的 ptr/dir_buf (编译错误), 同 add_dir_entry */
            if (rec < 8 || off + rec > ext4_bs) break;
            if (entry_ino == target_ino) {
                /* 清空 inode 号 */
                *(unsigned int*)(EXT4_DIR_BUF + off) = 0;
                /* 合并 rec_len 到前一项 (若非首项) */
                if (prev_off != 0xFFFFFFFF) {
                    unsigned short prev_rec = *(unsigned short*)(EXT4_DIR_BUF + prev_off + 4);
                    *(unsigned short*)(EXT4_DIR_BUF + prev_off + 4) = prev_rec + rec;
                }
                ext4_write_block(phys, EXT4_DIR_BUF);
                return 0;
            }
            prev_off = off;
            off += rec;
        }
    }
    return -1;
}

/* [创建空 inode] 初始化一个新 inode 并写回, 返回 inode 号
 * mode: 文件类型+权限 (如 0x41ED=目录0755, 0x81A4=文件0644) */
static unsigned int ext4_create_inode(unsigned int dir_ino, unsigned short mode) {
    unsigned int group = (dir_ino - 1) / inodes_per_group;
    int is_dir = (mode & 0x4000) != 0;
    unsigned int ino = alloc_inode(group, is_dir);
    if (!ino) return 0;
    unsigned char inode_buf[256];
    my_memset(inode_buf, 0, inode_size);
    *(unsigned short*)(inode_buf + 0) = mode;       /* i_mode */
    *(unsigned int*)(inode_buf + 4) = 0;            /* i_size_lo */
    *(unsigned int*)(inode_buf + 8) = 0;            /* i_atime (简化为0) */
    *(unsigned int*)(inode_buf + 0x0C) = 0;         /* i_ctime */
    *(unsigned int*)(inode_buf + 0x10) = 0;         /* i_mtime */
    *(unsigned int*)(inode_buf + 0x1C) = 0x80000;   /* i_flags = EXT4_EXTENTS_FL */
    write_inode(ino, inode_buf);
    return ino;
}

/* [ext4 创建文件] 在 dir_ino 中创建名为 name 的普通文件
 * 返回 inode 号, 0=失败
 * [关键修复] 原"已存在就返回0"导致 efs_file_write 在第一次创建后
 * 再次写入时走到 ino=0 分支, 写永远失败。现在已存在则直接返回已有 inode */
static unsigned int ext4_create_file(unsigned int dir_ino, const char *name) {
    unsigned int existing = find_in_dir(dir_ino, name);
    if (existing) return existing;    /* 已存在: 返回已有 inode, 不重新创建 */
    unsigned int ino = ext4_create_inode(dir_ino, 0x81A4);  /* 0644 普通文件 */
    if (!ino) return 0;
    if (add_dir_entry(dir_ino, name, ino, 1) != 0) {  /* file_type=1 普通文件 */
        free_inode(ino);
        return 0;
    }
    disk_flush();   /* [持久化] 创建文件落盘 */
    return ino;
}

/* [ext4 创建目录] 创建子目录, 自动添加 . 和 ..
 * [关键修复] 同 create_file: 已存在直接返回已有目录 inode */
static unsigned int ext4_mkdir_inode(unsigned int dir_ino, const char *name) {
    unsigned int existing = find_in_dir(dir_ino, name);
    if (existing) return existing;    /* 已存在: 返回已有目录 inode */
    unsigned int ino = ext4_create_inode(dir_ino, 0x41ED);  /* 0755 目录 */
    if (!ino) return 0;

    /* 分配第一个数据块用于 . 和 .. */
    unsigned int group = (dir_ino - 1) / inodes_per_group;
    unsigned int phys = alloc_block(group);
    if (!phys) { free_inode(ino); return 0; }

    unsigned char inode_buf[256];
    read_inode(ino, inode_buf);
    if (extent_add(inode_buf, 0, phys, 1) != 0) {
        free_block(phys); free_inode(ino); return 0;
    }
    *(unsigned int*)(inode_buf + 4) = ext4_bs;  /* 目录大小 = 1 块 */
    write_inode(ino, inode_buf);

    /* 写入 . 和 .. */
    my_memset(EXT4_DIR_BUF, 0, ext4_bs);
    /* "." : inode=ino, rec_len=12, name_len=1, type=2 */
    *(unsigned int*)(EXT4_DIR_BUF) = ino;
    *(unsigned short*)(EXT4_DIR_BUF + 4) = 12;
    *(unsigned char*)(EXT4_DIR_BUF + 6) = 1;
    *(unsigned char*)(EXT4_DIR_BUF + 7) = 2;
    EXT4_DIR_BUF[8] = '.';
    /* ".." : inode=dir_ino, rec_len=ext4_bs-12, name_len=2, type=2 */
    *(unsigned int*)(EXT4_DIR_BUF + 12) = dir_ino;
    *(unsigned short*)(EXT4_DIR_BUF + 16) = (unsigned short)(ext4_bs - 12);
    *(unsigned char*)(EXT4_DIR_BUF + 18) = 2;
    *(unsigned char*)(EXT4_DIR_BUF + 19) = 2;
    EXT4_DIR_BUF[20] = '.';
    EXT4_DIR_BUF[21] = '.';
    ext4_write_block(phys, EXT4_DIR_BUF);

    /* 在父目录添加条目 */
    if (add_dir_entry(dir_ino, name, ino, 2) != 0) {  /* type=2 目录 */
        free_block(phys); free_inode(ino); return 0;
    }
    disk_flush();   /* [持久化] 创建目录落盘 (确保 cd 能立即找到) */
    return ino;
}

/* [ext4 删除文件] unlink: 移除目录项, 释放 inode 和数据块 */
static int ext4_unlink(unsigned int dir_ino, const char *name) {
    unsigned int ino = find_in_dir(dir_ino, name);
    if (!ino) { serial_write("ext4: not found\n"); return -1; }
    unsigned char inode[256];
    read_inode(ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if (mode & 0x4000) { serial_write("ext4: is a directory (use rmdir)\n"); return -1; }

    /* 释放所有数据块 (含 depth=1 叶节点) */
    extent_free_all(inode);
    my_memset(inode, 0, inode_size);
    write_inode(ino, inode);
    free_inode(ino);
    remove_dir_entry(dir_ino, ino);
    disk_flush();   /* [持久化] 删除操作落盘 */
    return 0;
}

/* [ext4 删除空目录] rmdir */
static int ext4_rmdir(unsigned int dir_ino, const char *name) {
    unsigned int ino = find_in_dir(dir_ino, name);
    if (!ino) { serial_write("ext4: not found\n"); return -1; }
    unsigned char inode[256];
    read_inode(ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if (!(mode & 0x4000)) { serial_write("ext4: not a directory\n"); return -1; }

    /* 检查是否为空 (除 . 和 .. 外无其他条目) */
    /* [关键修复] 使用 dir_scan_buf (静态 RAM), 不再用 0xE0000 BIOS ROM 区 */
    unsigned char *dir_buf = dir_scan_buf;
    unsigned int fsize = read_inode_data(inode, dir_buf, 56 * 1024);
    unsigned char *ptr = dir_buf;
    while (ptr + 8 <= dir_buf + fsize) {
        unsigned int eino = *(unsigned int*)ptr;
        unsigned short rec = *(unsigned short*)(ptr + 4);
        unsigned char nlen = *(unsigned char*)(ptr + 6);
        if (rec < 8 || ptr + rec > dir_buf + fsize) break;
        if (eino != 0 && nlen > 0) {
            char nm[256]; my_strncpy(nm, (char*)ptr + 8, nlen); nm[nlen] = 0;
            if (!(my_strcmp(nm, ".")==0 || my_strcmp(nm, "..")==0)) {
                serial_write("ext4: directory not empty\n");
                return -1;
            }
        }
        ptr += rec;
    }

    /* 释放数据块 (含 depth=1 叶节点) */
    extent_free_all(inode);
    my_memset(inode, 0, inode_size);
    write_inode(ino, inode);
    free_inode(ino);
    /* 更新组 used_dirs 计数 */
    unsigned int group = (ino - 1) / inodes_per_group;
    unsigned short ud = gdt_get_used_dirs(group);
    if (ud > 0) gdt_set_used_dirs(group, ud - 1);
    gdt_write_back(group);
    remove_dir_entry(dir_ino, ino);
    disk_flush();   /* [持久化] 删除目录操作落盘 */
    return 0;
}

/* [ext4 写入文件] 将 data (len 字节) 写入 inode, 覆盖原内容
 * 分配所需数据块, 建立 extent 映射, 更新 i_size
 * 返回写入字节数, -1=失败 */
static int ext4_write_file(unsigned int ino, const unsigned char *data, unsigned int len) {
    unsigned char inode[256];
    read_inode(ino, inode);

    /* 释放原有数据块 (含 depth=1 叶节点块) */
    extent_free_all(inode);
    /* 重置 extent 树 */
    my_memset(inode + 40, 0, 60);
    unsigned int group = (ino - 1) / inodes_per_group;

    unsigned int need_blks = (len + ext4_bs - 1) / ext4_bs;
    /* 逐块分配并写入, 尝试合并连续物理块为单个 extent */
    unsigned int b = 0;
    while (b < need_blks) {
        unsigned int phys = alloc_block(group);
        if (!phys) { serial_write("ext4: out of blocks\n"); return -1; }
        my_memset(EXT4_META_BUF, 0, ext4_bs);
        unsigned int chunk = len - b * ext4_bs;
        if (chunk > ext4_bs) chunk = ext4_bs;
        my_memcpy(EXT4_META_BUF, data + b * ext4_bs, chunk);
        ext4_write_block(phys, EXT4_META_BUF);

        /* 尝试合并后续连续块 */
        unsigned int run = 1;
        while (b + run < need_blks && run < 32768) {
            unsigned int next_phys = alloc_block(group);
            if (next_phys != phys + run) {
                /* 不连续, 释放并停止合并 */
                if (next_phys) free_block(next_phys);
                break;
            }
            my_memset(EXT4_META_BUF, 0, ext4_bs);
            chunk = len - (b + run) * ext4_bs;
            if (chunk > ext4_bs) chunk = ext4_bs;
            my_memcpy(EXT4_META_BUF, data + (b + run) * ext4_bs, chunk);
            ext4_write_block(next_phys, EXT4_META_BUF);
            run++;
        }

        if (extent_add(inode, b, phys, run) != 0) {
            serial_write("ext4: extent_add failed\n");
            return -1;
        }
        b += run;
    }
    *(unsigned int*)(inode + 4) = len;  /* i_size_lo */
    write_inode(ino, inode);
    disk_flush();   /* [持久化] 刷新磁盘写缓存, 保证文件数据落盘 */
    return (int)len;
}

/* [ext4 追加写入] 读取现有内容, 追加新数据, 重新写入
 * [修复] 缓冲区从 0xD0000 移至 0x80000 (64KB), 避免与 dir_buf 0xC0000 重叠
 * 原先 find_in_dir 会覆盖 0xD0000 处的文件内容, 导致 cp/mv/append 数据损坏 */
static int ext4_append_file(unsigned int ino, const unsigned char *data, unsigned int len) {
    unsigned char *rbuf = (unsigned char*)0x80000;  /* 64KB 读取缓冲 (0x80000-0x8FFFF, 不与 AHCI 0x90000 冲突) */
    int old_size = read_file_content(ino, rbuf, 65536);
    if (old_size < 0) old_size = 0;
    if ((unsigned int)old_size + len > 65536) {
        serial_write("ext4: append too large (max 64KB)\n");
        return -1;
    }
    my_memcpy(rbuf + old_size, data, len);
    return ext4_write_file(ino, rbuf, old_size + len);
}

/* [新增] ext4 符号链接 (symlink): 创建指向 target 的符号链接
 * - 短链接 (<= 60 字节): 存储在 i_block[] 中 (inline/fast symlink), 无数据块
 * - 长链接 (> 60 字节): 分配数据块存储目标路径
 * i_mode = S_IFLNK | 0777 = 0xA1FF
 * 返回 inode 号, 0=失败 */
static unsigned int ext4_symlink(unsigned int dir_ino, const char *name, const char *target) {
    if (!name || !target || !target[0]) return 0;
    if (find_in_dir(dir_ino, name)) {
        serial_write("ext4: symlink exists\n");
        return 0;
    }
    unsigned int tlen = my_strlen(target);
    unsigned int ino = ext4_create_inode(dir_ino, 0xA1FF);  /* S_IFLNK | 0777 */
    if (!ino) return 0;

    unsigned char inode_buf[256];
    read_inode(ino, inode_buf);
    *(unsigned int*)(inode_buf + 4) = tlen;  /* i_size = 目标路径长度 */

    if (tlen <= 60) {
        /* 快速符号链接: 目标路径存储在 i_block[] (60 字节) */
        my_memset(inode_buf + 40, 0, 60);
        my_memcpy(inode_buf + 40, target, tlen);
        /* 设置 EXT4_INLINE_DATA_FL (0x10000000) */
        *(unsigned int*)(inode_buf + 0x1C) = 0x80000 | 0x10000000;
        write_inode(ino, inode_buf);
    } else {
        /* 长符号链接: 分配数据块存储目标路径 */
        *(unsigned int*)(inode_buf + 0x1C) = 0x80000;  /* EXT4_EXTENTS_FL */
        unsigned int group = (dir_ino - 1) / inodes_per_group;
        unsigned int need_blks = (tlen + ext4_bs - 1) / ext4_bs;
        unsigned int b = 0;
        while (b < need_blks) {
            unsigned int phys = alloc_block(group);
            if (!phys) { free_inode(ino); return 0; }
            my_memset(EXT4_META_BUF, 0, ext4_bs);
            unsigned int chunk = tlen - b * ext4_bs;
            if (chunk > ext4_bs) chunk = ext4_bs;
            my_memcpy(EXT4_META_BUF, target + b * ext4_bs, chunk);
            ext4_write_block(phys, EXT4_META_BUF);
            if (extent_add(inode_buf, b, phys, 1) != 0) {
                free_block(phys); free_inode(ino); return 0;
            }
            b++;
        }
        write_inode(ino, inode_buf);
    }

    /* 在父目录添加目录项 (file_type=7 符号链接) */
    if (add_dir_entry(dir_ino, name, ino, 7) != 0) {
        extent_free_all(inode_buf);
        free_inode(ino);
        return 0;
    }
    disk_flush();   /* [持久化] 符号链接落盘 */
    return ino;
}

/* [新增] 读取符号链接目标
 * 返回目标路径长度, -1=失败 (非符号链接或读取错误) */
static int ext4_readlink(unsigned int ino, char *buf, unsigned int bufsize) {
    unsigned char inode[256];
    read_inode(ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if ((mode & 0xF000) != 0xA000) return -1;  /* 非符号链接 */

    unsigned int tlen = *(unsigned int*)(inode + 4);
    if (tlen >= bufsize) tlen = bufsize - 1;

    /* 检查是否为快速符号链接 (i_block 无 extent magic, 或 inline data flag) */
    unsigned int flags = *(unsigned int*)(inode + 0x1C);
    unsigned short magic = *(unsigned short*)(inode + 40);
    if (tlen <= 60 && (magic != 0xF30A || (flags & 0x10000000))) {
        /* 快速符号链接: 目标在 i_block[] */
        my_memcpy(buf, inode + 40, tlen);
        buf[tlen] = 0;
        return (int)tlen;
    }
    /* 长符号链接: 从数据块读取 */
    int sz = read_file_content(ino, (unsigned char*)buf, bufsize - 1);
    if (sz < 0) return -1;
    buf[sz] = 0;
    return sz;
}

/* [新增] ext4 截断 (truncate): 将文件截断到指定大小
 * - new_size < 当前大小: 释放多余块, 缩小 extent
 * - new_size > 当前大小: 分配新块, 填零, 扩展 extent
 * 返回 0=成功, -1=失败 */
static int ext4_truncate(unsigned int ino, unsigned int new_size) {
    unsigned char inode[256];
    read_inode(ino, inode);
    unsigned int cur_size = *(unsigned int*)(inode + 4);
    unsigned short mode = *(unsigned short*)(inode);
    if (mode & 0x4000) { serial_write("ext4: cannot truncate directory\n"); return -1; }

    if (new_size == cur_size) return 0;

    if (new_size < cur_size) {
        /* 缩小: 释放超出 new_size 的块
         * 简化: 释放所有块后重写保留部分 (避免复杂的 extent 分割) */
        unsigned int keep_blks = (new_size + ext4_bs - 1) / ext4_bs;
        unsigned int *i_block = (unsigned int*)(inode + 40);
        struct ext4_extent_header *eh = (struct ext4_extent_header*)i_block;

        if (eh->eh_magic == 0xF30A && eh->eh_depth == 0) {
            /* depth=0: 遍历 extent, 修改/删除超出范围的条目 */
            for (int i = 0; i < eh->eh_entries; i++) {
                struct ext4_extent *ext = (struct ext4_extent*)((unsigned char*)i_block + 12 + i * 12);
                unsigned int ee_end = ext->ee_block + ext->ee_len;
                if (ext->ee_block >= keep_blks) {
                    /* 整个 extent 超出: 释放所有块 */
                    unsigned int start = ext->ee_start_lo;
                    unsigned short elen = ext->ee_len;
                    for (unsigned int k = 0; k < elen; k++) free_block(start + k);
                    /* 移除该条目 (后续条目前移) */
                    for (int j = i; j < eh->eh_entries - 1; j++) {
                        my_memcpy(i_block + 12 + j * 12, i_block + 12 + (j+1) * 12, 12);
                    }
                    eh->eh_entries--;
                    i--;  /* 重新检查当前位置 */
                } else if (ee_end > keep_blks) {
                    /* 部分超出: 缩短 extent */
                    unsigned short new_len = (unsigned short)(keep_blks - ext->ee_block);
                    unsigned int freed_start = ext->ee_start_lo + new_len;
                    unsigned short freed_len = (unsigned short)(ext->ee_len - new_len);
                    for (unsigned int k = 0; k < freed_len; k++) free_block(freed_start + k);
                    ext->ee_len = new_len;
                }
            }
        } else if (eh->eh_magic == 0xF30A && eh->eh_depth >= 1) {
            /* depth>=1: 简化为释放全部后重建 (复杂场景的退化处理) */
            unsigned char *rbuf = (unsigned char*)0x80000;
            unsigned int read_sz = read_inode_data(inode, rbuf, new_size);
            extent_free_all(inode);
            my_memset(inode + 40, 0, 60);
            if (new_size > 0 && read_sz > 0) {
                /* 重写保留部分 */
                unsigned int group = (ino - 1) / inodes_per_group;
                unsigned int need = (new_size + ext4_bs - 1) / ext4_bs;
                for (unsigned int b = 0; b < need; b++) {
                    unsigned int phys = alloc_block(group);
                    if (!phys) break;
                    my_memset(EXT4_META_BUF, 0, ext4_bs);
                    unsigned int chunk = new_size - b * ext4_bs;
                    if (chunk > ext4_bs) chunk = ext4_bs;
                    my_memcpy(EXT4_META_BUF, rbuf + b * ext4_bs, chunk);
                    ext4_write_block(phys, EXT4_META_BUF);
                    extent_add(inode, b, phys, 1);
                }
            }
        }
        *(unsigned int*)(inode + 4) = new_size;
        write_inode(ino, inode);
        disk_flush();   /* [持久化] 截断(缩小)落盘 */
        return 0;
    }

    /* 扩大: 分配新块, 填零 */
    unsigned int cur_blks = (cur_size + ext4_bs - 1) / ext4_bs;
    unsigned int new_blks = (new_size + ext4_bs - 1) / ext4_bs;
    unsigned int group = (ino - 1) / inodes_per_group;
    for (unsigned int b = cur_blks; b < new_blks; b++) {
        unsigned int phys = alloc_block(group);
        if (!phys) { serial_write("ext4: truncate expand out of blocks\n"); break; }
        my_memset(EXT4_META_BUF, 0, ext4_bs);
        ext4_write_block(phys, EXT4_META_BUF);
        if (extent_add(inode, b, phys, 1) != 0) { free_block(phys); break; }
    }
    *(unsigned int*)(inode + 4) = new_size;
    write_inode(ino, inode);
    disk_flush();   /* [持久化] 截断(扩大)落盘 */
    return 0;
}

/* ========== Memory File System ==========
 * current_path / current_dir_ino 被用户系统 API 读写, 必须是非 static 全局 */
static unsigned int *get_file_count_ptr(void) { return (unsigned int*)0x1100; }
static struct file_entry *get_file_table(void) { return (struct file_entry*)(0x1100 + 4); }
static unsigned char *get_file_content_base(void) { return (unsigned char*)0x20000; }
char current_path[256] = "/";
unsigned int current_dir_ino = 2;

static void resolve_path(const char *input, char *output) {
    if (input[0] == '/') { my_strcpy(output, input); return; }
    my_strcpy(output, current_path);
    int len = my_strlen(output);
    if (output[len-1] != '/') { output[len] = '/'; output[len+1] = 0; }
    my_strcat(output, input);
}
static int find_entry(const char *path) {
    unsigned int count = *get_file_count_ptr();
    struct file_entry *ft = get_file_table();
    for (unsigned int i = 0; i < count; i++) if (my_strcmp(ft[i].path, path) == 0) return i;
    return -1;
}
static int add_entry(const char *path, unsigned int type, unsigned int size, unsigned char *content) {
    unsigned int *count_ptr = get_file_count_ptr();
    unsigned int count = *count_ptr;
    if (count >= 512) return -1;
    struct file_entry *ft = get_file_table();
    for (unsigned int i = 0; i < count; i++) if (my_strcmp(ft[i].path, path) == 0) return -1;
    my_strcpy(ft[count].path, path);
    ft[count].type = type;
    ft[count].size = size;
    ft[count].offset = 0;
    if (type == 0 && size > 0) {
        unsigned char *base = get_file_content_base();
        unsigned int used = 0;
        for (unsigned int i = 0; i < count; i++) used += ft[i].size;
        ft[count].offset = used;
        if (content) my_memcpy(base + used, content, size);
    }
    (*count_ptr)++;
    return 0;
}
static int remove_entry(const char *path) {
    unsigned int *count_ptr = get_file_count_ptr();
    unsigned int count = *count_ptr;
    int idx = find_entry(path);
    if (idx < 0) return -1;
    struct file_entry *ft = get_file_table();
    if (ft[idx].type == 1) {
        int prefix_len = my_strlen(path);
        for (unsigned int i = 0; i < count; i++) {
            if (i == idx) continue;
            if (my_strncmp(ft[i].path, path, prefix_len) == 0 && ft[i].path[prefix_len] == '/')
                return -1;
        }
    }
    if (idx < count - 1) ft[idx] = ft[count - 1];
    (*count_ptr)--;
    return 0;
}

/* ========== Extra String Helpers ========== */
static int my_atoi(const char *s) { int sign=1, v=0; if (*s=='-'){sign=-1;s++;} while (*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;} return v*sign; }
static char my_toupper(char c) { if (c>='a'&&c<='z') return c-32; return c; }
static int my_strchr(const char *s, char c) { for (int i=0; s[i]; i++) if (s[i]==c) return i; return -1; }

/* ========== Memory FS Helpers ========== */
static unsigned int get_mem_file_size(const char *path) {
    int idx = find_entry(path);
    if (idx < 0) return 0xFFFFFFFF;
    struct file_entry *ft = get_file_table();
    return ft[idx].size;
}
static int get_mem_file_type(const char *path) {
    int idx = find_entry(path);
    if (idx < 0) return -1;
    struct file_entry *ft = get_file_table();
    return ft[idx].type;
}
static unsigned char *get_mem_file_data(const char *path) {
    int idx = find_entry(path);
    if (idx < 0) return NULL;
    struct file_entry *ft = get_file_table();
    return get_file_content_base() + ft[idx].offset;
}
static int copy_mem_file(const char *src, const char *dst) {
    int idx = find_entry(src);
    if (idx < 0) return -1;
    struct file_entry *ft = get_file_table();
    if (ft[idx].type != 0) return -1;
    unsigned char *data = get_file_content_base() + ft[idx].offset;
    return add_entry(dst, 0, ft[idx].size, data);
}
static int rename_mem_file(const char *src, const char *dst) {
    if (copy_mem_file(src, dst) != 0) return -1;
    return remove_entry(src);
}
static int append_mem_file(const char *path, unsigned char *data, unsigned int len) {
    int idx = find_entry(path);
    if (idx < 0) return -1;
    struct file_entry *ft = get_file_table();
    if (ft[idx].type != 0) return -1;
    unsigned char *base = get_file_content_base();
    unsigned int used = 0;
    unsigned int count = *get_file_count_ptr();
    for (unsigned int i = 0; i < count; i++) used += ft[i].size;
    if (used + len > 0x40000) return -1;  /* 文件内容区上限 256KB */
    unsigned int old_size = ft[idx].size;
    unsigned int old_offset = ft[idx].offset;
    /* 重定位到末尾 */
    ft[idx].offset = used;
    my_memcpy(base + old_offset, base + used, old_size);
    my_memcpy(base + used + old_size, data, len);
    ft[idx].size = old_size + len;
    return 0;
}

/* ========== ext4 Recursive Helpers ==========
 * 递归遍历 ext4 目录树, 用于 tree 和 find 命令
 * [关键修复] 使用 dir_scan_buf (静态 RAM, 不再用 0xE0000 BIOS ROM 区)。
 * 递归调用会覆盖共享缓冲区, 每次递归返回后重新读取本目录继续遍历。 */
static void ext4_tree_recursive(unsigned int dir_ino, int depth, int *count) {
    unsigned char inode[256]; read_inode(dir_ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if (!(mode & 0x4000)) return;

    unsigned char *dir_buf = dir_scan_buf;
    unsigned int fsize = read_inode_data(inode, dir_buf, 56 * 1024);
    unsigned char *ptr = dir_buf;
    while (ptr + 8 <= dir_buf + fsize) {
        unsigned int ino = *(unsigned int*)ptr;
        unsigned short rec = *(unsigned short*)(ptr + 4);
        unsigned char nlen = *(unsigned char*)(ptr + 6);
        unsigned char ftype = *(unsigned char*)(ptr + 7);
        if (rec < 8 || ptr + rec > dir_buf + fsize) break;
        if (ino && nlen) {
            char name[256]; my_strncpy(name, (char*)ptr + 8, nlen); name[nlen] = 0;
            /* 跳过 . 和 .. */
            if (!(my_strcmp(name, ".")==0 || my_strcmp(name, "..")==0)) {
                for (int i = 0; i < depth; i++) print_string("  ");
                if (ftype == 2) { print_string("["); print_string(name); print_string("]\n"); }
                else { print_string("  "); print_string(name); print_string("\n"); }
                (*count)++;
                if (ftype == 2) {
                    ext4_tree_recursive(ino, depth + 1, count);
                    /* 递归覆盖了 dir_scan_buf, 重新读取本目录后继续 */
                    fsize = read_inode_data(inode, dir_buf, 56 * 1024);
                }
            }
        }
        ptr += rec;
    }
}

/* 递归查找: 在 dir_ino 子树中查找名为 name 的条目, 返回 inode 号
 * max_depth 防止循环引用导致的无限递归 */
static unsigned int ext4_find_recursive(unsigned int dir_ino, const char *name, int max_depth) {
    if (max_depth <= 0) return 0;
    unsigned char inode[256]; read_inode(dir_ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if (!(mode & 0x4000)) return 0;

    unsigned char *dir_buf = dir_scan_buf;   /* [关键修复] 静态 RAM, 不再用 0xE0000 ROM 区 */
    unsigned int fsize = read_inode_data(inode, dir_buf, 56 * 1024);
    unsigned char *ptr = dir_buf;
    while (ptr + 8 <= dir_buf + fsize) {
        unsigned int ino = *(unsigned int*)ptr;
        unsigned short rec = *(unsigned short*)(ptr + 4);
        unsigned char nlen = *(unsigned char*)(ptr + 6);
        unsigned char ftype = *(unsigned char*)(ptr + 7);
        if (rec < 8 || ptr + rec > dir_buf + fsize) break;
        if (ino && nlen) {
            char entry_name[256]; my_strncpy(entry_name, (char*)ptr + 8, nlen); entry_name[nlen] = 0;
            if (!(my_strcmp(entry_name, ".")==0 || my_strcmp(entry_name, "..")==0)) {
                if (my_strcmp(entry_name, name) == 0) return ino;
                if (ftype == 2) {
                    unsigned int found = ext4_find_recursive(ino, name, max_depth - 1);
                    if (found) return found;
                    /* 递归覆盖了 dir_scan_buf, 重新读取本目录后继续 */
                    fsize = read_inode_data(inode, dir_buf, 56 * 1024);
                }
            }
        }
        ptr += rec;
    }
    return 0;
}

/* 统计 ext4 目录大小 (字节) - 递归求和所有文件大小 */
static unsigned long long ext4_du_recursive(unsigned int dir_ino, int max_depth) {
    if (max_depth <= 0) return 0;
    unsigned char inode[256]; read_inode(dir_ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if (!(mode & 0x4000)) {
        /* 普通文件: 返回其大小 */
        return *(unsigned int*)(inode + 4);
    }
    unsigned char *dir_buf = dir_scan_buf;   /* [关键修复] 静态 RAM, 不再用 0xA0000 VGA 区 */
    unsigned int fsize = read_inode_data(inode, dir_buf, 56 * 1024);
    unsigned long long total = 0;
    unsigned char *ptr = dir_buf;
    while (ptr + 8 <= dir_buf + fsize) {
        unsigned int ino = *(unsigned int*)ptr;
        unsigned short rec = *(unsigned short*)(ptr + 4);
        unsigned char nlen = *(unsigned char*)(ptr + 6);
        if (rec < 8 || ptr + rec > dir_buf + fsize) break;
        if (ino && nlen) {
            char name[256]; my_strncpy(name, (char*)ptr + 8, nlen); name[nlen] = 0;
            if (!(my_strcmp(name, ".")==0 || my_strcmp(name, "..")==0)) {
                total += ext4_du_recursive(ino, max_depth - 1);
                /* 递归覆盖了 dir_scan_buf, 重新读取本目录后继续 */
                fsize = read_inode_data(inode, dir_buf, 56 * 1024);
            }
        }
        ptr += rec;
    }
    return total;
}

/* ========== Path Resolver & File Loader ==========
 * 统一路径解析: 支持 绝对/相对路径, '.', '..', 连续斜杠
 * 修复旧版 cd 命令中 while((next=token)) 在空 token 时的无限循环
 */
static unsigned int resolve_inode(const char *arg, char *full_path) {
    if (!arg || !arg[0]) return 0;
    unsigned int cur_ino;
    char cur_path[256];

    if (arg[0] == '/') {
        cur_ino = 2;                       /* ext4 根目录 */
        my_strcpy(cur_path, "/");
        arg++;
    } else {
        cur_ino = current_dir_ino;
        my_strcpy(cur_path, current_path);
    }

    char buf[256];
    my_strcpy(buf, arg);
    char *p = buf;
    while (*p) {
        while (*p == '/') p++;             /* 跳过连续斜杠 */
        if (!*p) break;
        char *token = p;
        while (*p && *p != '/') p++;
        if (*p) { *p = 0; p++; }

        if (my_strcmp(token, ".") == 0) continue;
        if (my_strcmp(token, "..") == 0) {
            if (cur_ino != 2) {
                unsigned int parent = find_in_dir(cur_ino, "..");
                if (parent) {
                    cur_ino = parent;
                    /* 路径回退一级: "/a/b" -> "/a", "/a" -> "/" */
                    if (my_strcmp(cur_path, "/") != 0) {
                        int i = my_strlen(cur_path) - 1;
                        while (i > 0 && cur_path[i] != '/') i--;
                        if (i == 0) my_strcpy(cur_path, "/");
                        else cur_path[i] = 0;
                    }
                }
            }
            continue;
        }
        unsigned int ino = find_in_dir(cur_ino, token);
        if (!ino) return 0;                /* 未找到 */
        cur_ino = ino;
        if (my_strcmp(cur_path, "/") != 0) my_strcat(cur_path, "/");
        my_strcat(cur_path, token);
    }
    if (full_path) my_strcpy(full_path, cur_path);
    return cur_ino;
}

/* ext4 文件读取缓冲区 (1MB — 无大小限制) */
static unsigned char fs_load_buf[0x100000];


/* ========== EFS Executable Loader ==========
 * .efs 文件格式 (与 Makefile 生成规则一致):
 *   字节 0-7 : 加载地址 (64 位小端, 实际只用低 32 位)
 *   字节 8-11: 二进制大小 (32 位大端)
 *   字节 12+ : 纯二进制 (objcopy -O binary 产物, 入口为起始字节)
 * 加载后跳转到加载地址执行; 程序通过 0x9000 处的内核 API 表访问内核功能。
 * 当 shell 收到非内置命令时, 自动在 /EFMOS 下查找 <cmd>.efs 并执行。 */

/* 目录条目 (.efs 程序侧字段对齐必须完全一致) */
struct efs_dirent {
    char name[64];          /* 文件名 (不含路径) */
    unsigned int size;      /* 字节数 (文件); 目录为其 i_size */
    unsigned int is_dir;    /* 1=目录, 0=普通文件 */
};

struct kernel_api {
    unsigned int magic;
    unsigned int _pad;
    void (*put_char)(char);
    void (*print)(const char*);
    void (*print_utf8)(const char*);
    void (*clear_screen)(void);
    int (*file_read)(const char*, char*, int);
    int (*file_write)(const char*, const char*, int);
    int (*file_exists)(const char*);
    int (*mkdir)(const char*);
    int (*readline)(char*, int);
    void (*reboot)(void);
    int (*get_lang)(void);
    void (*set_lang)(int);
    int (*save_settings)(void);
    /* [新增 3.0] 鼠标 API:
     *   mouse_poll:   非阻塞取一个鼠标事件, 1=成功 out=填充, 0=无事件
     *   mouse_set_cursor: show=1 显示/0=隐藏 内核绘制的箭头光标
     * mouse_event 结构 (.efs 程序侧要字段对齐完全一致):
     *   struct { int dx,dy; unsigned char btn; int x,y; };  dx/dy 相对位移, btn bit0=L 1=R 2=M, x/y 绝对坐标 */
    int  (*mouse_poll)(void *out_event);
    void (*mouse_set_cursor)(int show);
    /* [新增] 文件管理 API:
     *   file_list:  列出目录内容到 out 数组, 返回条目数 (>=0) 或 -1
     *   file_delete: 删除一个普通文件, 返回 0=成功 -1=失败 */
    int  (*file_list)(const char *dir_path, struct efs_dirent *out, int max_count);
    int  (*file_delete)(const char *path);
    /* [新增] 非阻塞键盘输入 API (统一由内核 classify 硬件字节, 避免 .efs 程序 IN 0x60 和内核抢读):
     *   返回值: 0=无按键; 正数=可打印 ASCII (0x20..0x7E); 负数=特殊键:
     *        -101=Enter  -102=Backspace  -103=ESC */
    int  (*key_poll)(void);
    /* ========== 用户系统 API (User Management) ========== */
    /* get_current_user: 把当前登录用户名写入 buf (最多 bufsz-1 字节),
     *   返回写入字节数 (>=0), -1=未登录或失败 */
    int  (*get_current_user)(char *buf, int bufsz);
    /* set_current_user: 切换当前用户为 username, 返回 0=成功 -1=用户不存在
     *   (会更新 shell 默认目录到 /users/<username>) */
    int  (*set_current_user)(const char *username);
    /* user_list: 列出 /users 下的用户目录, out[i].name 为用户名,
     *   out[i].is_dir 始终为 1, out[i].size 为该用户目录大小。
     *   返回条目数 (>=0), -1=失败。 */
    int  (*user_list)(struct efs_dirent *out, int max_count);
    /* user_create: 创建新用户 (在 /users 下创建 <username> 目录).
     *   返回 0=成功, -1=失败 (用户已存在 / 无效名) */
    int  (*user_create)(const char *username);
    /* user_delete: 删除用户 (删除 /users/<username> 目录, 要求为空目录)
     *   返回 0=成功, -1=失败 (当前用户不可删除/非空/不存在) */
    int  (*user_delete)(const char *username);
    /* ========== 扩展 API (为编译器等大型程序准备) ==========
     * malloc/free: 内核堆动态内存 (32MB~64MB 区域)
     * spawn: 运行另一个 /EFMOS/*.efs 并等待其返回 (gcc 调用 as/ld 的基础)
     * get_args: 取本程序启动时的命令行参数串 (程序名之后的参数, 空格分隔) */
    void *(*malloc)(unsigned long);
    void  (*free)(void*);
    int   (*spawn)(const char *name, const char *args);
    int   (*get_args)(char *buf, int max);
    /* 2024+ 扩展: 字体像素尺寸 (ASCII). CJK 宽 = 2*font_w, 高 = font_h.
     * 用于 .efs 程序计算按钮/布局坐标, 替代硬编码 8/16. */
    int font_w;
    int font_h;
    /* 2025+ 窗口系统:
     *   current_pid: 本 EFS 进程所属 pid (由 efs_setup_api_table / WM 启动时写入)
     *   wm_enabled:  内核是否处于窗口模式 (1=窗口化, 0=传统全屏)
     * 当 wm_enabled=1 且 current_pid>0 时, api->put_char/print/clear_screen
     * 会自动重定向到 current_pid 绑定的窗口内容区 (由内核 wm_redirect_* 实现)。 */
    int current_pid;
    int wm_enabled;
    /* 2025+ 窗口像素绘制 API:
     *   当 wm_enabled=1 时, 所有坐标以当前进程的窗口内容区为参考系 (左上=(0,0)),
     *   并被裁剪到内容区内; 当 wm_enabled=0 时, 坐标为全局 FB 绝对坐标 (向后兼容)。
     * 没有这些 API 的旧版 .efs 程序直接写 0x1000 (GOP FB struct) 会破坏窗口画面,
     *   建议所有程序切换到这些安全 API。 */
    void (*put_pixel)(int x, int y, unsigned int c);
    void (*fill_rect)(int x1, int y1, int x2, int y2, unsigned int c);
    void (*draw_rect)(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill);
    /* 取当前进程"虚拟屏幕"几何:
     *   wm_enabled=1  → 返回内容区 (cx,cy) 为全局 FB 偏移, (cw,ch) 为尺寸
     *   wm_enabled=0  → 返回 (0,0, hr, vr)
     * 程序如果需要在全屏 vs 窗口化模式下自适应布局, 可以用这些值决定绘制区域。 */
    void (*get_viewport)(int *cx, int *cy, int *cw, int *ch);
    /* 取底层 GOP fb 信息 (只读, 不要直接写 fb_base; 用 put_pixel/fill_rect 安全写入) */
    void (*get_fb_info)(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base);
    /* [Mesa 集成] 批量 blit: 把外部像素缓冲区写入当前进程窗口的内容区.
     * 用途: Mesa swrast 渲染完一帧后, eglSwapBuffers 调用此函数把结果写入
     *       Graphics.drv 的 back buffer (而非直接写 front), 保持双缓冲一致性.
     * 参数: src=源像素, src_w/h=源宽高, src_pitch=源每行字节数
     * 返回: 0=成功, -1=失败
     * WM 感知: wm_enabled=1 时自动重定向到窗口内容区并裁剪 */
    int  (*blit_to_window)(const void *src, int src_w, int src_h, int src_pitch);
    /* ========== 驱动子系统 API (EFMOS 2026) ==========
     * load_driver: 读取磁盘上 <path> 指定的 .drv 文件, 加载到其头部 load_addr,
     *   调用入口 (drv_entry) 并通过内部 iface 注册其 ops。返回注册的驱动数/非负=ok,
     *   -1=文件找不到, -2=头部非法, -3=加载地址冲突, -4=注册失败。
     * driver_count: 当前 registry 内驱动数 (用于 ps/drv 调试输出)
     * driver_list:  复制驱动名 (每个 32 字节) 到 out_names, 返回条目数 (最多 max) */
    int   (*load_driver)(const char *path);
    int   (*driver_count)(void);
    int   (*driver_list)(char out_names[][32], int max);
    /* ========== 2026+ 多线程调度 API ==========
     *   sleep_ms:  当前任务睡眠至少 ms 毫秒 (TSC 近似精度)
     *   yield:     主动让出 CPU (不睡眠, 立刻可被再次调度)
     *   get_pid:   返回当前任务 pid
     *   spawn_async: 创建新内核线程运行另一个 /EFMOS/*.efs (不等返回, 返回子 pid)
     *   set_priority(pid, nice): 调整指定任务 nice [0..39], 0=最高优先级 */
    void  (*sleep_ms)(unsigned long ms);
    void  (*yield)(void);
    int   (*get_pid)(void);
    int   (*spawn_async)(const char *name, const char *args);
    int   (*set_priority)(int pid, int nice);
    /* ========== 2026+ Mesa 合成器 API ==========
     * get_wm_snapshot: 把当前 WM 窗口状态 + 鼠标状态拷贝到 out 缓冲区.
     *   返回窗口数 (>=0), -1=失败. out 必须足够大 (见 efm_wm_snapshot).
     * set_compositor_active: 通知内核合成器已接管渲染 (1=接管, 0=交还内核).
     *   合成器接管后, 内核 wm_composite 不再直接渲染, 仅维护窗口状态. */
    int   (*get_wm_snapshot)(void *out, int max_bytes);
    void  (*set_compositor_active)(int active);
    void *(*dlsym)(const char *name);   /* 从已加载 SO 解析符号 (给合成器调用 Mesa EGL/GL) */
    /* [优化] 获取 Graphics.drv 的 back buffer 指针, 让 Mesa swrast 直接渲染到 back buffer.
     * 消除 BO→back buffer 全屏拷贝. 返回 0=成功. */
    int  (*get_backbuffer)(void **out_ptr, int *out_pitch, int *out_w, int *out_h);
    /* [优化] 标记 back buffer 脏矩形 (inclusive) + 立即 flush 到 front.
     * 直接映射模式下 swrast 直接写 back buffer, 需手动标记脏区并 flush. */
    void (*mark_dirty_rect)(int x1, int y1, int x2, int y2);
    void (*flush_now)(void);
    /* [2026+ TTF] Unicode 字符渲染 (TTF 字体, 透明 alpha 混合).
     * bg=0xFEEDFACE 时为透明模式: 不画背景方块, 字形 alpha 混合到现有像素.
     * 返回字符实际像素宽度 (0=失败/不支持). cell_w/cell_h<=0 时只画字形不限制单元. */
    int  (*draw_char_unicode)(int x, int y, unsigned int codepoint,
                              unsigned int fg, unsigned int bg, int cell_w, int cell_h);
    /* [新架构] 设置当前进程窗口的 EFS 图形信息指针.
     * EFS 图形程序 (efmlogin/userman/fileman/setting) 分配 efm_gfx_info 结构,
     * 填写矩形+文字信息后调用此 API, 内核把指针存入窗口, efmcompositor 通过
     * get_wm_snapshot 读取并渲染. EFS 程序不再直接写 back buffer. */
    void (*set_gfx_info)(void *info);
    /* [efmshell 独立] chdir: 切换内核全局 CWD (current_dir_ino + current_path).
     * 供独立 shell 程序 (efmshell.efs) 同步自己的 cwd, 使 run_efs 的 CWD 查找
     * 和 resolve_inode 的相对路径解析与 shell 视角一致. 返回 0=成功 -1=失败. */
    int  (*chdir)(const char *path);
    /* [efmshell 全功能移植] 增强行编辑引擎: 原 kernel 内建 shell_loop 的输入部分
     * (历史 Up/Down, 行内 Left/Right/Home/End, Backspace/Delete, Esc 清行,
     *  Tab 补全, 闪烁光标) 移植为内核 API, 在 EFS 任务上下文内执行.
     * prompt 由本函数绘制 (整行重绘需要). hist_get: idx 0=最近, NULL=无更多.
     * complete: 收集 prefix 候选到 matches (每条 match_len 字节), 返回数量.
     * 返回输入长度; Esc 中止返回 -1. */
    int  (*readline_ext)(const char *prompt, char *buf, int max,
                         const char *(*hist_get)(int idx),
                         int (*complete)(const char *prefix, char *matches, int max_n, int match_len));
    /* [efmshell 全功能移植] 多行文本编辑器 (write/append/edit 命令).
     * 可视化编辑: 跨行光标移动, Backspace/Delete, ENDOFFILE 结束符, Esc 取消.
     * 控制台模式完整移植原 kb_read_text; WM 窗口模式为逐行编辑版.
     * 返回文本长度; Esc 取消返回 -1. */
    int  (*read_text)(char *buf, int max);
    /* [efmshell 全功能移植] 文件系统/系统杂项操作 (无法经现有扁平 API 表达的操作):
     *   "ln" a=target b=linkname -> ext4_symlink (返回 ino/负值)
     *   "readlink" a=link out=target
     *   "truncate" a=file b=size(十进制)
     *   "rmdir" a=dir
     *   "sync" -> disk_flush
     *   "ps" out=任务列表文本    "wminfo" out=WM 状态文本
     *   "sysinfo" out=struct efm_sysinfo (二进制, 见实现处定义)
     * 返回 0=成功 (文本 op 返回字节数), 负值=失败, -2=未知 op. */
    int  (*fsop)(const char *op, const char *a, const char *b, char *out, int outsz);
};

/* ---------- Mesa 合成器共享窗口状态结构 ---------- */
#define EFM_WM_MAX_WIN    32
#define EFM_WM_TITLE_LEN  64
#define EFM_WM_TBUF_COLS  120
#define EFM_WM_TBUF_ROWS  60

/* ========== EFS 图形信息共享结构 (efm_gfx_info) ==========
 * 【职责分工 · 新架构】
 *   EFS 图形程序 (efmlogin/userman/fileman/setting) 不再直接调用 fill_rect/
 *   draw_rect/put_pixel 写 back buffer, 而是填写此结构, 由 efmcompositor 统一渲染.
 *   矩形 (色块/边框) 由 efmcompositor 画到 canvas; 文字由 TTF 叠加层画到 back buffer.
 * 坐标: 所有 x/y/w/h 均为窗口内容区相对坐标 (左上=0,0), efmcompositor 转为绝对坐标.
 * 原子性: EFS 程序先填所有字段, 最后写 magic=EFM_GFX_MAGIC 表示就绪; 更新时 seq++. */
#define EFM_GFX_MAGIC     0x45464758u  /* "EFGX" */
#define EFM_GFX_MAX_RECTS 32
#define EFM_GFX_MAX_TEXTS 32

struct efm_gfx_rect {
    int x, y, w, h;
    unsigned int fill_color;
    unsigned int border_color;
    int has_border;   /* 0=纯填充, 1=带 2px 边框 */
};

struct efm_gfx_text {
    int x, y;
    unsigned int color;
    char text[96];    /* UTF-8 */
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
    /* [EFS 图形标志] 1=窗口内容区有 EFS 程序直接写入 back buffer 的图形元素
     * (按钮/矩形/边框等). 合成器据此决定是否从 back buffer 恢复图形, 还是
     * 直接填充背景色后画文字。 */
    int has_efm_gfx;
    int tbuf_cols, tbuf_rows;
    /* tbuf_ptr 指向内核 g_wm_windows[i].tbuf (共享地址空间, 合成器可直接读) */
    char *tbuf_ptr;
    /* [新架构] EFS 图形信息指针 (由 EFS 程序通过 set_gfx_info 设置).
     * 指向 EFS 程序分配的 efm_gfx_info 结构 (共享地址空间, 合成器可直接读). */
    struct efm_gfx_info *gfx_info_ptr;
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
    /* [EFS 图形脏检测计数器]
     * EFS 程序 (userman/efmlogin 等) 通过 fill_rect/draw_rect/put_pixel 直接修改
     * back buffer 时, 此计数器递增。Compositor 每帧对比新旧值, 变化时强制重绘
     * 对应窗口内容区, 确保 EFS 图形像素被正确从 back buffer 恢复到 canvas,
     * 不会被 canvas 的 blit 操作覆盖。单调递增, 溢出回绕仍可对比 (!= 即表示变化)。 */
    unsigned int gfx_dirty_seq;
    struct efm_wm_win_info windows[EFM_WM_MAX_WIN];
    /* [开始菜单状态] 由 wm_handle_mouse 维护, compositor 读取渲染弹出菜单 */
    int menu_open;        /* 1 = 菜单已打开 */
    int menu_hover_idx;   /* 鼠标悬停的菜单项索引 (-1 = 无) */
    int menu_item_count;  /* 菜单项总数 */
};

#define EFS_API_MAGIC 0xEF110001

/* ---------- WM 函数前向声明 (efs_* 包装函数会用到) ---------- */
struct wm_window;
static void wm_redirect_putc(int pid, char ch);
static void wm_redirect_print(int pid, const char *s);
static void wm_redirect_clear(int pid);
static void wm_composite(void);
static int  wm_find_by_pid_exists(int pid);
/* 新增 WM-safe 像素级绘制 API 前向声明 */
static void efs_put_pixel(int x, int y, unsigned int c);
static void efs_fill_rect_api(int x1, int y1, int x2, int y2, unsigned int c);
static void efs_draw_rect_api(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill);
static void efs_get_viewport(int *cx, int *cy, int *cw, int *ch);
static void efs_get_fb_info(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base);
static int  efs_blit_to_window(const void *src, int src_w, int src_h, int src_pitch);
/* ---- 内联辅助: 若 WM 启用 + 调用者有窗口, 返回 1 表示走重定向 ---- */
static inline int efs_should_redirect(void) {
    struct kernel_api *a = (struct kernel_api*)0x9000;
    if (a->magic != EFS_API_MAGIC) return 0;
    if (!a->wm_enabled || a->current_pid <= 0) return 0;
    if (!wm_find_by_pid_exists(a->current_pid)) return 0;
    /* [关键修复] compositor 激活后不走 WM 重定向: compositor 有自己的窗口 (pid=3),
     * 如果不排除, compositor 调用 draw_char_unicode/fill_rect/put_pixel 时,
     * 内核会把 compositor 传入的屏幕绝对坐标当作窗口相对坐标, 再加一次窗口偏移,
     * 导致文字/矩形画到错误位置 (文字按钮错位的根因). */
    if (g_compositor_active) return 0;
    return 1;
}

/* ---------- 内核 API 包装函数 ---------- */
/* [双缓冲+鼠标修复] 非 WM 模式下 flush back buffer → front 后, front 上的鼠标
 * 箭头被覆盖。置 s_save_x/s_save_y = -1 让下次 draw_mouse_cursor 重画。 */
static inline void efs_gfx_flush_nonwm(void) {
    drv_gfx_maybe_flush();
    s_save_x = s_save_y = -1;
}
static void efs_put_char(char c) {
    if (efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        wm_redirect_putc(a->current_pid, c);
        g_wm_dirty = 1;  /* [性能修复] 只置脏, 不立即合成; idle 轮询节拍统一刷 */
        return;
    }
    /* [桌面脱离内核] 合成器激活时, 无窗口进程的文本输出不写帧缓冲,
     * 避免与 efmAether 的桌面渲染冲突 (文字残影/撕裂). */
    if (g_compositor_active) return;
    put_char(c);
    /* [修复] 全屏模式: 只 mark dirty, 不立即 flush (避免每字符刷屏导致闪屏)。
     * EFS 程序的 gui_flush 末尾会调 flush_now() 统一刷一次。 */
    drv_gfx_mark_dirty();
}
static void efs_print(const char *s) {
    if (efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        wm_redirect_print(a->current_pid, s);
        g_wm_dirty = 1;
        return;
    }
    if (g_compositor_active) return;
    print_string(s);
    /* [修复] 全屏模式: 整串输出后 flush 一次 (比逐字符 flush 高效) */
    efs_gfx_flush_nonwm();
}
static void efs_print_utf8(const char *s) {
    if (efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        wm_redirect_print(a->current_pid, s);
        g_wm_dirty = 1;
        return;
    }
    if (g_compositor_active) return;
    print_string(s);
    /* [修复] 全屏模式: 整串输出后 flush 一次 */
    efs_gfx_flush_nonwm();
}
static void efs_clear_screen(void) {
    if (efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        wm_redirect_clear(a->current_pid);
        /* wm_redirect_clear 内部已经 wm_composite */
        return;
    }
    /* [桌面脱离内核] 合成器激活时, 无窗口进程的清屏不写帧缓冲,
     * 避免覆盖 efmAether 已渲染的桌面. */
    if (g_compositor_active) { cursor_x = 0; cursor_y = 0; cursor_visible = 0; return; }
    /* 传统全屏模式: 清屏 */
    hide_mouse_cursor();
    fill_screen(bg);
    cursor_x = 0; cursor_y = 0;
    cursor_visible = 0;
}
static int efs_file_read(const char *path, char *buf, int max) {
    unsigned int ino = resolve_inode(path, NULL);
    if (!ino) return -1;
    return read_file_content(ino, (unsigned char*)buf, (unsigned int)max);
}
/* 拆分绝对路径为 "父目录" + "末级名", 返回父目录 inode (0=失败) */
static unsigned int efs_resolve_parent(const char *path, char *child, int child_sz) {
    int len = my_strlen(path);
    int last = -1;
    for (int i = 0; i < len; i++) if (path[i] == '/') last = i;
    if (last < 0) return 0;
    int cl = len - last - 1;
    if (cl <= 0 || cl >= child_sz) return 0;
    my_memcpy(child, path + last + 1, cl);
    child[cl] = 0;
    char parent[256];
    if (last == 0) { parent[0] = '/'; parent[1] = 0; }
    else { my_memcpy(parent, path, last); parent[last] = 0; }
    return resolve_inode(parent, NULL);
}
static int efs_file_write(const char *path, const char *buf, int len) {
    if (!path || !path[0]) return -1;
    if (len < 0) return -1;

    /* [关键修复] 相对路径 -> 绝对路径, 否则 efs_resolve_parent 因 path 无 "/" 返回 0 (parent not found) */
    char full[256]; resolve_path(path, full);
    path = full;

    unsigned int ino = resolve_inode(path, NULL);
    if (!ino) {
        /* 文件不存在: 创建新文件 */
        char child[128];
        unsigned int pino = efs_resolve_parent(path, child, sizeof(child));
        if (!pino) {
            serial_write("efs_file_write: parent not found for "); serial_write(path); serial_write("\n");
            print_string(TR("write: parent directory not found: ","写入: 上级目录未找到: ")); print_string(path); print_string("\n");
            return -1;
        }
        ino = ext4_create_file(pino, child);
        if (!ino) {
            serial_write("efs_file_write: ext4_create_file failed for "); serial_write(path); serial_write("\n");
            print_string(TR("write: create failed: ","写入: 创建失败: ")); print_string(path); print_string("\n");
            return -1;
        }
    } else {
        /* 文件已存在: 确认不是目录 (否则无法用 write 覆盖) */
        unsigned char chk[256]; read_inode(ino, chk);
        unsigned short mode = *(unsigned short*)(chk + 0);
        if (mode & 0x4000) {   /* S_IFDIR */
            serial_write("efs_file_write: is a directory\n");
            print_string(TR("write: is a directory: ","写入: 是一个目录: ")); print_string(path); print_string("\n");
            return -1;
        }
    }
    int w = ext4_write_file(ino, (const unsigned char*)buf, (unsigned int)len);
    if (w < 0) {
        serial_write("efs_file_write: ext4_write_file failed for "); serial_write(path); serial_write("\n");
        print_string(TR("write: ext4 write failed: ","写入: ext4 写入失败: ")); print_string(path); print_string("\n");
        return -1;
    }
    return 0;
}

static int efs_file_exists(const char *path) {
    return resolve_inode(path, NULL) ? 1 : 0;
}
/* [efmshell 独立] 切换内核全局 CWD (供独立 shell 同步 cwd).
 * 逻辑与 cd 命令一致: resolve_inode 解析 (支持 . / .. / 相对 / 绝对) → 校验是目录 → 更新全局. */
static int efs_chdir(const char *path) {
    if (!path || !path[0]) return -1;
    char full_path[256];
    unsigned int target_ino = resolve_inode(path, full_path);
    if (!target_ino) return -1;
    unsigned char inode_buf[256];
    read_inode(target_ino, inode_buf);
    unsigned short mode = *(unsigned short*)(inode_buf);
    if (!(mode & 0x4000)) return -1;   /* 不是目录 */
    current_dir_ino = target_ino;
    my_strcpy(current_path, full_path);
    return 0;
}
static int efs_mkdir(const char *path) {
    char child[128];
    unsigned int pino = efs_resolve_parent(path, child, sizeof(child));
    if (!pino) return -1;
    return ext4_mkdir_inode(pino, child) ? 0 : -1;
}

/* 列出目录内容到 efs_dirent 数组 (供文件管理器使用)。
 * 返回填充的条目数 (>=0), -1=路径无效或非目录。
 * 跳过 "." 和 ".." 条目 (导航由调用方维护路径字符串完成)。 */
static int efs_file_list(const char *dir_path, struct efs_dirent *out, int max_count) {
    if (!dir_path || !out || max_count <= 0) return -1;
    unsigned int dir_ino = resolve_inode(dir_path, NULL);
    if (!dir_ino) return -1;
    unsigned char inode[256]; read_inode(dir_ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if (!(mode & 0x4000)) return -1;   /* 非目录 */

    unsigned char *dir_buf = dir_scan_buf;
    unsigned int fsize = read_inode_data(inode, dir_buf, 128 * 1024);
    if (fsize > 128 * 1024) fsize = 128 * 1024;  /* 硬上限, 防止越界 */

    int count = 0;
    unsigned char *ptr = dir_buf;
    int guard = 0;
    while (ptr + 8 <= dir_buf + fsize && count < max_count && guard++ < 65536) {
        unsigned int ino = *(unsigned int*)ptr;
        unsigned short rec = *(unsigned short*)(ptr + 4);
        unsigned char nlen = *(unsigned char*)(ptr + 6);
        unsigned char ftype = *(unsigned char*)(ptr + 7);
        if (rec < 8 || rec > 4096 || ptr + rec > dir_buf + fsize) break;  /* rec=0 或过大则终止 */
        if (ino && nlen && nlen <= rec - 8 && nlen < 64) {
            /* 跳过 "." 和 ".." */
            if (!(nlen == 1 && ptr[8] == '.') &&
                !(nlen == 2 && ptr[8] == '.' && ptr[9] == '.')) {
                my_memcpy(out[count].name, ptr + 8, nlen);
                out[count].name[nlen] = 0;
                out[count].is_dir = (ftype == 2) ? 1 : 0;
                /* 读条目 inode 取真实大小 */
                unsigned char ein[256]; read_inode(ino, ein);
                out[count].size = *(unsigned int*)(ein + 4);  /* i_size_lo */
                count++;
            }
        }
        ptr += rec;
    }
    return count;
}

/* 删除一个普通文件 (不能删目录, 目录需用 rmdir)。
 * path 为绝对路径。返回 0=成功 -1=失败。 */
static int efs_file_delete(const char *path) {
    if (!path || !path[0]) return -1;
    char child[128];
    unsigned int pino = efs_resolve_parent(path, child, sizeof(child));
    if (!pino) return -1;
    return ext4_unlink(pino, child);
}

/* ============================================================
 *  WM-safe 像素级绘制 API:
 *  - wm_enabled=1: 坐标以 owner 窗口内容区左上=(0,0), 裁剪到内容区内部
 *  - wm_enabled=0: 坐标=全局 FB 绝对坐标 (向后兼容)
 *  完成后 g_wm_dirty=1, 下一合成节拍刷新到屏幕。
 * ============================================================ */
static void efs_put_pixel(int x, int y, unsigned int c) {
    if (g_wm_enabled && efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        struct wm_window *w = wm_find_by_pid(a->current_pid);
        if (!w) return;
        if (x < 0 || y < 0) return;
        int cw = wm_content_w(w), ch = wm_content_h(w);
        if (x >= cw || y >= ch) return;
        /* [修复] 用 draw_pixel 走 Graphics.drv back buffer, 与 draw_char 一致.
         * 旧代码用 efs_fb_pix_raw 直接写 front buffer, 导致像素与文字不同步. */
        draw_pixel((unsigned)(wm_content_x(w) + x), (unsigned)(wm_content_y(w) + y), c);
        g_wm_dirty = 1;
        g_wm_gfx_dirty_seq++;
        wm_window_set_has_efm_gfx(w, 1);
    } else {
        /* [修复] 全屏模式: 走 back buffer, 只 mark dirty 不立即 flush。
         * 旧代码每 fill_rect 都 flush → efmlogin 画 10+ 个 rect 每个都刷屏一次,
         * 中间态暴露到 front buffer → 严重闪屏。
         * EFS 程序的 gui_flush 末尾会调 flush_now() 统一刷一次。 */
        if (x >= 0 && y >= 0) {
            draw_pixel((unsigned)x, (unsigned)y, c);
            drv_gfx_mark_dirty();
        }
    }
}
static void efs_fill_rect_api(int x1, int y1, int x2, int y2, unsigned int c) {
    if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
    if (g_wm_enabled && efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        struct wm_window *w = wm_find_by_pid(a->current_pid);
        if (!w) return;
        int cw = wm_content_w(w), ch = wm_content_h(w);
        int ox = wm_content_x(w), oy = wm_content_y(w);
        if (x1 < 0) x1 = 0;   if (y1 < 0) y1 = 0;
        if (x2 >= cw) x2 = cw - 1;
        if (y2 >= ch) y2 = ch - 1;
        if (x1 > x2 || y1 > y2) return;
        fill_rect(ox + x1, oy + y1, x2 - x1 + 1, y2 - y1 + 1, c);
        g_wm_dirty = 1;
        g_wm_gfx_dirty_seq++;
        wm_window_set_has_efm_gfx(w, 1);
    } else {
        /* [修复] 全屏模式: 只 mark dirty, 不逐次 flush (gui_flush 末尾统一 flush) */
        fill_rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1, c);
        drv_gfx_mark_dirty();
    }
}
static void efs_draw_rect_api(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill) {
    /* 先填内部, 再画 4 条边 (border 颜色) */
    if (x2 >= x1 + 2 && y2 >= y1 + 2)
        efs_fill_rect_api(x1 + 1, y1 + 1, x2 - 1, y2 - 1, fill);
    /* 4 条边: 顶 底 左 右
     * [修复] 用 draw_pixel 走 back buffer (与 fill/text 一致), 旧代码用
     * efs_fb_pix_raw 直接写 front buffer 导致边框与填充/文字不同步. */
    if (g_wm_enabled && efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        struct wm_window *w = wm_find_by_pid(a->current_pid);
        if (!w) return;
        int cw = wm_content_w(w), ch = wm_content_h(w);
        int ox = wm_content_x(w), oy = wm_content_y(w);
        for (int x = x1; x <= x2; x++) {
            if (x < 0 || x >= cw) continue;
            if (y1 >= 0 && y1 < ch) draw_pixel((unsigned)(ox + x), (unsigned)(oy + y1), border);
            if (y2 != y1 && y2 >= 0 && y2 < ch) draw_pixel((unsigned)(ox + x), (unsigned)(oy + y2), border);
        }
        for (int y = y1 + 1; y <= y2 - 1; y++) {
            if (y < 0 || y >= ch) continue;
            if (x1 >= 0 && x1 < cw) draw_pixel((unsigned)(ox + x1), (unsigned)(oy + y), border);
            if (x2 != x1 && x2 >= 0 && x2 < cw) draw_pixel((unsigned)(ox + x2), (unsigned)(oy + y), border);
        }
        g_wm_dirty = 1;
        g_wm_gfx_dirty_seq++;
        wm_window_set_has_efm_gfx(w, 1);
    } else {
        struct gop_fb *fb = (struct gop_fb*)0x1000;
        int hr = fb ? (int)fb->hr : 0, vr = fb ? (int)fb->vr : 0;
        for (int x = x1; x <= x2; x++) {
            if (x < 0 || x >= hr) continue;
            if (y1 >= 0 && y1 < vr) draw_pixel((unsigned)x, (unsigned)y1, border);
            if (y2 != y1 && y2 >= 0 && y2 < vr) draw_pixel((unsigned)x, (unsigned)y2, border);
        }
        for (int y = y1 + 1; y <= y2 - 1; y++) {
            if (y < 0 || y >= vr) continue;
            if (x1 >= 0 && x1 < hr) draw_pixel((unsigned)x1, (unsigned)y, border);
            if (x2 != x1 && x2 >= 0 && x2 < hr) draw_pixel((unsigned)x2, (unsigned)y, border);
        }
        /* [修复] 全屏模式: 只 mark dirty, 不逐次 flush (避免闪屏) */
        drv_gfx_mark_dirty();
    }
}
static void efs_get_viewport(int *cx, int *cy, int *cw, int *ch) {
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (g_wm_enabled && efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        struct wm_window *w = wm_find_by_pid(a->current_pid);
        if (w) {
            if (cx) *cx = wm_content_x(w);
            if (cy) *cy = wm_content_y(w);
            if (cw) *cw = wm_content_w(w);
            if (ch) *ch = wm_content_h(w);
            return;
        }
    }
    if (cx) *cx = 0;
    if (cy) *cy = 0;
    if (cw) *cw = fb ? (int)fb->hr : 0;
    if (ch) *ch = fb ? (int)fb->vr : 0;
}
static void efs_get_fb_info(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base) {
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb) return;
    if (hr) *hr = fb->hr;
    if (vr) *vr = fb->vr;
    if (ppsl) *ppsl = fb->ppsl;
    if (fb_base) *fb_base = (unsigned int*)(unsigned long)fb->fb_base;
}

/* [Mesa 集成核心] 批量 blit: 把外部像素缓冲区 (src) 写入当前进程窗口的内容区.
 * 这是 Mesa swrast → EFMOS 渲染管线的"最后一公里":
 *   Mesa swrast 渲染完一帧 → eglSwapBuffers → efm_gbm_blit_to_screen → 本函数
 *   → 通过 Graphics.drv 的 blit_buffer 写入 back buffer → 下一次 flush 到屏幕
 *
 * WM 感知:
 *   wm_enabled=1 → 查询 current_pid 的窗口, 把 src 拷贝到窗口内容区 (WM 重定向 + 裁剪)
 *   wm_enabled=0 → 拷贝到全屏 (0,0)
 *
 * 参数:
 *   src:       源像素缓冲区 (32-bit, 与 GOP 格式一致)
 *   src_w/h:   源缓冲区宽高 (像素)
 *   src_pitch: 源每行字节数 (通常 = src_w * 4)
 * 返回: 0=成功, -1=失败 (无驱动/无窗口/参数非法) */
static int efs_blit_to_window(const void *src, int src_w, int src_h, int src_pitch) {
    if (!src || src_w <= 0 || src_h <= 0 || src_pitch <= 0) return -1;
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb || !fb->fb_base) return -1;

    /* 【Mesa 合成器 present 路径】 合成器激活时, blit 是全屏呈现:
     *   - 不重定向到窗口 (合成器没有窗口)
     *   - 不标记鼠标脏矩形 (鼠标已由合成器烘焙到 canvas 中, 整个 canvas 一起 blit)
     *   - 立即 force_flush (合成器自己管帧率, 不依赖 wm_composite 的节拍)
     * 这条路径等价于 Mesa EGL 的 eglSwapBuffers: canvas → back buffer → front. */
    if (g_compositor_active) {
        if (g_drv_gfx_ptr && g_drv_gfx_ptr->blit_buffer) {
            g_drv_gfx_ptr->blit_buffer(0, 0, src_w, src_h, src, src_pitch);
            drv_gfx_force_flush();
            return 0;
        }
        /* 无驱动: 直接写 front buffer (逐行 32-bit 拷贝) */
        unsigned int *base = (unsigned int*)(unsigned long)fb->fb_base;
        int fb_pitch = (int)fb->ppsl;
        int cw = src_w, ch = src_h;
        if (cw > (int)fb->hr) cw = (int)fb->hr;
        if (ch > (int)fb->vr) ch = (int)fb->vr;
        const unsigned int *s = (const unsigned int*)src;
        for (int y = 0; y < ch; y++) {
            unsigned int *d = base + (unsigned long)y * (unsigned long)fb_pitch;
            const unsigned int *r = s + (unsigned long)y * (unsigned long)(src_pitch / 4);
            for (int x = 0; x < cw; x++) d[x] = r[x];
        }
        return 0;
    }

    int dst_x = 0, dst_y = 0;
    int clip_w = src_w, clip_h = src_h;

    /* WM 模式: 查窗口内容区, 重定向 + 裁剪 (仅非合成器路径) */
    if (g_wm_enabled && efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        struct wm_window *w = wm_find_by_pid(a->current_pid);
        if (!w) return -1;
        int cw = wm_content_w(w), ch = wm_content_h(w);
        if (cw <= 0 || ch <= 0) return -1;
        dst_x = wm_content_x(w);
        dst_y = wm_content_y(w);
        /* 源缓冲区可能比窗口内容区大或小, 取最小值 */
        if (clip_w > cw) clip_w = cw;
        if (clip_h > ch) clip_h = ch;
        g_wm_dirty = 1;
    }

    /* 优先用 Graphics.drv 的 blit_buffer (写 back buffer + 脏矩形) */
    if (g_drv_gfx_ptr && g_drv_gfx_ptr->blit_buffer) {
        g_drv_gfx_ptr->blit_buffer(dst_x, dst_y, clip_w, clip_h, src, src_pitch);
        drv_gfx_mark_dirty();
        /* [鼠标拖尾修复] 强制把鼠标旧位置 + 新位置两个 16x16 区域标记为 dirty.
         * 原因: blit_buffer 只写 back buffer, flush 只搬脏矩形到 front.
         *   如果 Mesa 渲染内容覆盖了"鼠标旧位置或新位置下面的区域",
         *   那么 flush 必须把这些区域从 back 搬到 front,
         *   否则 copy_back_rect_to_front 从 back 读了新内容写到 front,
         *   front 周围仍是旧内容 → 出现 16x16 的"新内容补丁块". */
        if (s_prev_mx >= 0 && s_prev_my >= 0) {
            /* 旧鼠标 16x16 强制 dirty */
            drv_gfx_mark_dirty_rect(s_prev_mx, s_prev_my, s_prev_mx + 15, s_prev_my + 15);
        }
        if (s_mx >= 0 && s_my >= 0) {
            /* 新鼠标 16x16 强制 dirty */
            drv_gfx_mark_dirty_rect(s_mx, s_my, s_mx + 15, s_my + 15);
        }
        return 0;
    }

    /* 回退: 无驱动时直接写 front buffer (逐行 32-bit 拷贝) */
    unsigned int *base = (unsigned int*)(unsigned long)fb->fb_base;
    int fb_pitch = (int)fb->ppsl;
    /* clip 到屏幕 */
    if (dst_x < 0) { clip_w += dst_x; dst_x = 0; }
    if (dst_y < 0) { clip_h += dst_y; dst_y = 0; }
    if (dst_x + clip_w > (int)fb->hr) clip_w = (int)fb->hr - dst_x;
    if (dst_y + clip_h > (int)fb->vr) clip_h = (int)fb->vr - dst_y;
    if (clip_w <= 0 || clip_h <= 0) return -1;
    const unsigned int *s = (const unsigned int*)src;
    for (int y = 0; y < clip_h; y++) {
        unsigned int *dst_row = base + (unsigned long)(dst_y + y) * (unsigned long)fb_pitch;
        const unsigned int *src_row = s + (unsigned long)y * (unsigned long)(src_pitch / 4);
        for (int x = 0; x < clip_w; x++)
            dst_row[dst_x + x] = src_row[x];
    }
    return 0;
}

static int efs_readline(char *buf, int max) {
    int i = 0, shift = 0, caps = 0;
    if (max <= 1) { if (buf) buf[0] = 0; return 0; }

    /* [WM 模式] 本进程有窗口时, 不能直接操作全局 cursor_x/y/draw_char。
     *    所有输出必须通过 put_char/print 重定向。
     *    策略: 在内存里维护 buf[i] (已输入字符), 每次变更:
     *      - 写 '\r' 回到当前行行首
     *      - 输出 N 个空格覆盖旧行
     *      - 再 '\r' 回到行首, 输出 buf[0..i-1] 最新内容
     *      (由于 redirect put_char 把 \n 换成 c_cx=0;c_cy++, 我们不用 \n 换行)
     * [非 WM 模式] 全局 cursor 可用, 走原逻辑逐字符退格绘制。
     * [双缓冲修复] Graphics.drv 加载后, put_char/draw_char 写 back buffer,
     *   非 WM 模式下必须显式 drv_gfx_maybe_flush() 才能让用户看到输入的字符,
     *   否则 readline 看起来"卡死" (键盘有输入但屏幕不显示)。 */
    const int in_wm = efs_should_redirect();
    show_cursor();
    if (!in_wm) efs_gfx_flush_nonwm();

    while (i < max - 1) {
        if (!kb_has_data()) {
            /* 鼠标绘制: WM 模式由空闲轮询统一维护, 这里 sched_yield 让 shell 有机会跑;
             * 非 WM 模式直接画。 */
            if (in_wm) {
                g_wm_dirty = 1;
                lapic_timer_check();
                sched_yield();  /* 让 shell 重合成 + 处理鼠标事件 + 切回其他任务 */
            } else {
                if (s_mouse_visible) draw_mouse_cursor();
            }
            asm volatile("pause");
            continue;
        }
        unsigned int sc = kbdq_pop();
        int key = kb_scan_to_key(sc, &shift, &caps);
        if (key == 0) continue;
        if (!in_wm) { hide_mouse_cursor(); }
        if (key == KEY_ESC) {
            hide_cursor();
            put_char('\n');
            if (!in_wm) efs_gfx_flush_nonwm();
            return -1;
        }
        if (key == KEY_ENTER) {
            put_char('\n');
            if (!in_wm) efs_gfx_flush_nonwm();
            break;
        }
        if (key == KEY_BACKSPACE) {
            if (i > 0) {
                i--;
                if (in_wm) {
                    /* 重绘: \r 回到行首, 写 N+i 个空格覆盖, \r 再回来写新行 */
                    int cols = (int)sizeof(efs_argbuf); if (cols < 256) cols = 256;
                    put_char('\r');
                    for (int k = 0; k < cols; k++) put_char(' ');
                    put_char('\r');
                    for (int k = 0; k < i; k++) put_char(buf[k]);
                } else {
                    hide_cursor();
                    cursor_x -= FONT_W; if (cursor_x < 0) cursor_x = 0;
                    draw_char(cursor_x, cursor_y, ' ', bg);
                    show_cursor();
                    efs_gfx_flush_nonwm();  /* flush 覆盖了 front 上的鼠标, s_save=-1 让下次重画 */
                }
            }
            continue;
        }
        if (key > 0) {
            char c = (char)key;
            if (c >= 0x20) {
                buf[i++] = c; put_char(c);
                if (!in_wm) efs_gfx_flush_nonwm();
            }
        }
    }
    hide_cursor();
    if (!in_wm) efs_gfx_flush_nonwm();
    buf[i] = 0;
    return i;
}

/* ========== [efmshell] 增强行编辑引擎 (原 kernel 内建 shell_loop 输入部分移植) ==========
 * 完整功能: 命令历史 (Up/Down), 行内编辑 (Left/Right/Home/End), Backspace/Delete,
 * Esc 清行, Tab 补全 (回调收集候选), 闪烁光标 (控制台模式, 轮询驱动)。
 * 所有历史修复随原实现一并移植:
 *   - redraw_line 用 drv_gfx_frame_begin/end 锁 flush (防半行/残留闪屏)
 *   - 清行用全屏宽 fill_rect 而非逐 cell draw_char(' ') (防 TTF SPACE 残像素)
 *   - 光标列计算用 str_display_width (ASCII=1, CJK=2, 防 CJK prompt 光标错位)
 *   - Down 键 hist_idx >= 0 才能回到当前输入行 (原 hist_idx > 0 漏 0 的 bug 修复)
 *   - key > 0 才转 char (KEY_* 负值截断成 0xFB 乱字符的 bug 修复) */

/* 整行重绘: 清当前行 + prompt + buf[0..pos) + 光标定位到 pos.
 * [WM 模式] 用当前进程窗口的内容区行; [控制台] 全局 framebuffer。 */
static void sh_redraw_line(const char *prompt, const char *buf, int pos) {
    hide_cursor();
    /* [WM 模式] 当前进程有窗口时, 在窗口文本缓冲区内重绘整行 */
    if (g_wm_enabled && !g_wm_in_render) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        struct wm_window *sw = wm_find_by_pid(a->current_pid);
        if (sw) {
            wm_window_clear_row(sw, wm_window_cy(sw));
            wm_window_set_cx(sw, 0);
            for (const char *p = prompt; *p; p++) wm_window_putc(sw, *p);
            for (int i = 0; buf[i]; i++) wm_window_putc(sw, buf[i]);
            /* 光标定位: 行首 + prompt 显示宽 + pos 显示宽 (advance 按字符步进) */
            wm_window_set_cx(sw, 0);
            for (const char *p = prompt; *p; p++) wm_window_advance(sw, 1);
            for (int i = 0; i < pos; i++) wm_window_advance(sw, 1);
            wm_composite();
            return;
        }
    }
    /* [控制台模式] 防闪屏: frame_begin/end 期间 maybe_flush 为 NOP,
     * 所有 fill_rect/draw_char 只写 back buffer, frame_end 唯一一次 flush。 */
    drv_gfx_frame_begin();
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    /* 清行: 单个全屏宽 fill_rect, 不走 draw_char(' ') 的 TTT SPACE 路径
     * (SPACE 轮廓空 → notdef/静态缓冲残像素 → 行尾"DDDDDD"残影的根因)。 */
    fill_rect(0, cursor_y, (int)fb->hr, FONT_H, bg);
    cursor_x = 0;
    print_string(prompt);
    for (int i = 0; buf[i]; i++) put_char(buf[i]);
    /* CJK 光标列: str_display_width 而非 strlen (中文用户名/路径按 2 列计) */
    int prompt_w = str_display_width(prompt, my_strlen(prompt));
    int buf_w    = str_display_width(buf, pos);
    cursor_x = (prompt_w + buf_w) * FONT_W;
    drv_gfx_frame_end();   /* 唯一一次 flush 出口 */
    show_cursor();
}

/* 补全候选的最长公共前缀长度 (matches 每条 match_len 字节) */
static int sh_common_prefix_len(char *matches, int n, int match_len) {
    if (n <= 0) return 0;
    int len = 0;
    while (matches[len]) {
        char c = matches[len];
        int all = 1;
        for (int i = 1; i < n; i++) if (matches[i * match_len + len] != c) { all = 0; break; }
        if (!all) break;
        len++;
    }
    return len;
}

/* 增强行编辑: prompt 由本函数绘制/重绘; 历史与补全由 efmshell 通过回调提供 */
static int efs_readline_ext(const char *prompt, char *buf, int max,
                            const char *(*hist_get)(int idx),
                            int (*complete_cb)(const char *prefix, char *matches, int max_n, int match_len)) {
    int pos = 0;
    int hist_idx = -1;   /* -1 = 当前输入, 0..N = 历史 */
    int shift = 0, caps = 0;
    unsigned int blink_counter = 0;
    const int in_wm = efs_should_redirect();
    if (max <= 1) { if (buf) buf[0] = 0; return 0; }
    buf[0] = 0;

    show_cursor();
    /* 入口即整行重绘一次: 在当前行画出 prompt (旧 shell_loop 打印 prompt 的等价物) */
    sh_redraw_line(prompt, buf, pos);
    if (!in_wm) efs_gfx_flush_nonwm();

    while (1) {
        /* [非阻塞轮询] 空闲: 闪烁光标 + 鼠标事件/箭头 + 协作让出 CPU */
        if (!kb_has_data()) {
            blink_counter++;
            lapic_timer_check();          /* 抢占调度: 时间片到期则让出 */
            if (g_wm_enabled) {
                while (wm_handle_mouse()) { }   /* drain 所有鼠标事件 (拖动/焦点) */
            }
            /* 合成器激活时高频 yield, 防止 shell 自旋饿死合成器 */
            unsigned int yield_threshold = g_compositor_active ? 200u : 2000u;
            if (blink_counter > yield_threshold) {
                blink_counter = 0;
                if (!g_wm_enabled) {
                    if (cursor_visible) draw_cursor(0);
                    else draw_cursor(1);
                    if (!g_compositor_active) drv_gfx_maybe_flush();
                }
                sched_yield();
                if (g_wm_enabled && g_wm_dirty) wm_composite();
            }
            if (s_mouse_visible && !g_compositor_active && !g_wm_enabled)
                draw_mouse_cursor();
            asm volatile("pause");
            continue;
        }
        /* 有键: 光标稳定显示, 读完整逻辑扫描码 */
        if (!cursor_visible) show_cursor();
        blink_counter = 0;

        unsigned int sc = kbdq_pop();
        int key = kb_scan_to_key(sc, &shift, &caps);
        if (key == 0) continue;   /* 修饰键状态变化或无效键 */

        hide_mouse_cursor();   /* 写文字前先擦鼠标箭头, 防残影 */

        /* Up: 往前翻历史 (hist_idx+1 = 更远的一条; 0=最近) */
        if (key == KEY_UP) {
            if (hist_get) {
                const char *h = hist_get(hist_idx + 1);
                if (h) {
                    hist_idx++;
                    my_strcpy(buf, h); pos = my_strlen(buf);
                    sh_redraw_line(prompt, buf, pos);
                }
            }
            continue;
        }
        /* Down: 往回翻 (hist_idx>=0 才处理, 保证 0 时能回到当前输入行) */
        if (key == KEY_DOWN) {
            if (hist_idx > 0) {
                hist_idx--;
                const char *h = hist_get ? hist_get(hist_idx) : 0;
                my_strcpy(buf, h ? h : ""); pos = my_strlen(buf);
            } else {
                hist_idx = -1; buf[0] = 0; pos = 0;
            }
            sh_redraw_line(prompt, buf, pos);
            continue;
        }
        if (key == KEY_LEFT)  { if (pos > 0)    { pos--; sh_redraw_line(prompt, buf, pos); } continue; }
        if (key == KEY_RIGHT) { if (buf[pos])   { pos++; sh_redraw_line(prompt, buf, pos); } continue; }
        if (key == KEY_HOME)  { pos = 0; sh_redraw_line(prompt, buf, pos); continue; }
        if (key == KEY_END)   { while (buf[pos]) pos++; sh_redraw_line(prompt, buf, pos); continue; }
        if (key == KEY_ESC)   { buf[0] = 0; pos = 0; hist_idx = -1; sh_redraw_line(prompt, buf, pos); continue; }
        if (key == KEY_DELETE) {
            if (buf[pos]) {
                for (int i = pos; buf[i]; i++) buf[i] = buf[i+1];
                sh_redraw_line(prompt, buf, pos);
            }
            continue;
        }
        /* Tab: 补全到最长公共前缀; 唯一匹配自动补全并附加空格 */
        if (key == KEY_TAB) {
            if (!complete_cb) continue;
            int start = pos;
            while (start > 0 && buf[start-1] != ' ') start--;
            int plen = pos - start;
            if (plen <= 0 || plen >= 64) continue;
            char prefix[64];
            for (int i = 0; i < plen; i++) prefix[i] = buf[start + i];
            prefix[plen] = 0;
            char matches[48][64];   /* 3KB, EFS 任务栈 1MB 充足 */
            int n = complete_cb(prefix, (char*)matches, 48, 64);
            if (n <= 0) continue;
            if (n == 1) {
                int mlen = my_strlen(matches[0]);
                if (start + mlen + 1 >= max) continue;
                for (int i = 0; i < mlen; i++) buf[start + i] = matches[0][i];
                buf[start + mlen] = ' ';
                buf[start + mlen + 1] = 0;
                pos = start + mlen + 1;
                sh_redraw_line(prompt, buf, pos);
            } else {
                int cpl = sh_common_prefix_len((char*)matches, n, 64);
                if (cpl > plen) {
                    if (start + cpl < max) {
                        for (int i = 0; i < cpl; i++) buf[start + i] = matches[0][i];
                        buf[start + cpl] = 0;
                        pos = start + cpl;
                    }
                }
                efs_put_char('\n');
                for (int i = 0; i < n; i++) { efs_print(matches[i]); efs_print("  "); }
                efs_put_char('\n');
                sh_redraw_line(prompt, buf, pos);
            }
            continue;
        }
        /* Enter: 结束输入 (调用方执行命令后再次调用本函数画新 prompt) */
        if (key == KEY_ENTER) {
            hide_cursor();
            efs_put_char('\n');
            if (!in_wm) efs_gfx_flush_nonwm();
            buf[pos] = 0;
            return pos;
        }
        if (key == KEY_BACKSPACE) {
            if (pos > 0) {
                for (int i = pos - 1; buf[i]; i++) buf[i] = buf[i+1];
                pos--;
                sh_redraw_line(prompt, buf, pos);
            }
            continue;
        }
        /* 可打印字符: 在 pos 处插入 (key>0 显式判断, KEY_* 负值不做 char 截断) */
        if (key > 0) {
            char c = (char)key;
            if (c >= 0x20 && pos < max - 1) {
                int len = my_strlen(buf);
                for (int i = len; i >= pos; i--) buf[i+1] = buf[i];
                buf[pos++] = c;
                sh_redraw_line(prompt, buf, pos);
            }
            continue;
        }
    }
}

/* ========== [efmshell] 多行文本编辑器 (原 kb_read_text 移植, write/append/edit 用) ==========
 * ENDOFFILE 结束符识别 (整行独占才算): Enter 时取 buf 最后一行, trim 空白后
 * 与 "ENDOFFILE" 严格比较 → 相等则剥离该行结束; 否则正常换行。这样正文中
 * "pre ENDOFFILE post" 等不独占整行的字样被当正文保存。
 * [控制台模式] 完整可视化编辑: 跨行光标移动 (Left/Right/Home/End), Backspace
 *   删除左侧 (末尾删除快速路径), Delete 删除光标下字符, Tab 插入 4 空格。
 * [WM 窗口模式] 逐行编辑版 (已提交行不可回改): 行内完整编辑 + Enter 换行。 */
static int sh_tx0 = 0, sh_ty0 = 0;   /* 文本起点 (像素): 进入编辑器时的 cursor_x/y */

static int sh_pos_to_rc(const char *buf, int i, int pos, int *out_col) {
    int row = 0, col = 0;
    for (int k = 0; k < pos && k < i; k++) {
        if (buf[k] == '\n') { row++; col = 0; }
        else { col++; }
    }
    if (out_col) *out_col = col;
    return row;
}

/* 仅移动光标到 pos (不重绘文字) — Left/Right/Home/End 快速路径 */
static void sh_move_cursor_to_pos(const char *buf, int i, int pos) {
    int row = 0, col_px = sh_tx0;
    for (int k = 0; k < pos && k < i; k++) {
        unsigned char c = (unsigned char)buf[k];
        if (c == '\n') { row++; col_px = sh_tx0; }
        else if (c < 0x80) { col_px += FONT_W; }
        else if ((c & 0xE0) == 0xC0 && k+1 < i) { col_px += 2*FONT_W; k++; }
        else if ((c & 0xF0) == 0xE0 && k+2 < i) { col_px += 2*FONT_W; k += 2; }
        else { col_px += FONT_W; }
    }
    cursor_x = col_px;
    cursor_y = sh_ty0 + row * FONT_H;
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (fb && fb->fb_base) {
        int max_y = (int)fb->vr - FONT_H;
        if (cursor_y > max_y) cursor_y = max_y;
        if (cursor_y < 0) cursor_y = 0;
    }
}

/* 从 (sh_tx0, sh_ty0) 起重画 buf[0..i], 光标定位到 pos。
 * put_char 自动滚屏会移动 sh_ty0, 用 pos 反推行号修正 sh_ty0。 */
static void sh_redraw_whole_text(const char *buf, int i, int pos) {
    hide_cursor();
    /* [防闪屏] 清多行+画全文期间锁 flush, 完整内容一次显示 */
    drv_gfx_frame_begin();
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    int max_rows = (int)fb->vr / FONT_H;
    int start_row = sh_ty0 / FONT_H;
    int y0 = start_row * FONT_H;
    int rows = max_rows - start_row;
    if (rows > 0) fill_rect(0, y0, (int)fb->hr, rows * FONT_H, bg);
    cursor_x = sh_tx0; cursor_y = sh_ty0;
    for (int k = 0; k < i; k++) put_char(buf[k]);
    cursor_x = sh_tx0; cursor_y = sh_ty0;
    for (int k = 0; k < pos; k++) put_char(buf[k]);
    int col;
    int r = sh_pos_to_rc(buf, i, pos, &col);
    int new_ty0 = cursor_y - r * FONT_H;
    if (new_ty0 < 0) new_ty0 = 0;
    sh_ty0 = new_ty0;
    drv_gfx_frame_end();
    show_cursor();
}

/* 返回 s 去掉前后空格/tab/\r 后的窗口, 长度经 *out_len 返回 */
static const char *sh_trim_str(const char *s, int len, int *out_len) {
    int l = 0, r = len - 1;
    while (l <= r && (s[l]==' ' || s[l]=='\t' || s[l]=='\r')) l++;
    while (r >= l && (s[r]==' ' || s[r]=='\t' || s[r]=='\r')) r--;
    if (out_len) *out_len = (l <= r) ? (r - l + 1) : 0;
    return s + l;
}

static int efs_read_text(char *buf, int max) {
    int i = 0, pos = 0;           /* i=总长度, pos=插入点 */
    int shift = 0, caps = 0;
    unsigned int blink_counter = 0;
    if (max <= 1) { if (buf) buf[0] = 0; return 0; }

    const int in_wm = efs_should_redirect();

    if (in_wm) {
        /* [WM 窗口模式] 逐行编辑: 行内完整编辑, Enter 提交该行入 buf。
         * 已提交行不可回改 (窗口文本缓冲不支持向上重绘)。 */
        char line[256];
        int lp = 0;
        static const char END_MARK[] = "ENDOFFILE";
        while (i < max - 1) {
            if (!kb_has_data()) {
                g_wm_dirty = 1;
                lapic_timer_check();
                sched_yield();    /* 让出 CPU 给合成器/其他任务 */
                asm volatile("pause");
                continue;
            }
            unsigned int sc = kbdq_pop();
            int key = kb_scan_to_key(sc, &shift, &caps);
            if (key == 0) continue;
            if (key == KEY_ESC) { efs_put_char('\n'); return -1; }
            if (key == KEY_ENTER) {
                int tlen; const char *t = sh_trim_str(line, lp, &tlen);
                int match = (tlen == 9);
                if (match) { for (int q = 0; q < 9; q++) if (t[q] != END_MARK[q]) { match = 0; break; } }
                if (match) { buf[i] = 0; efs_put_char('\n'); return i; }
                /* 普通换行: 提交该行 */
                for (int q = 0; q < lp && i < max - 2; q++) buf[i++] = line[q];
                buf[i++] = '\n';
                efs_put_char('\n');
                lp = 0; line[0] = 0;
                continue;
            }
            if (key == KEY_BACKSPACE) {
                if (lp > 0) {
                    lp--;
                    line[lp] = 0;
                    /* \r + 覆盖空格 + \r + 重画行 */
                    efs_put_char('\r');
                    for (int q = 0; q < 256; q++) efs_put_char(' ');
                    efs_put_char('\r');
                    for (int q = 0; q < lp; q++) efs_put_char(line[q]);
                }
                continue;
            }
            if (key > 0) {
                char c = (char)key;
                if (c >= 0x20 && lp < (int)sizeof(line) - 1) {
                    line[lp++] = c; line[lp] = 0;
                    efs_put_char(c);
                }
                continue;
            }
        }
        buf[i] = 0;
        return i;
    }

    /* [控制台模式] 完整可视化多行编辑 (原 kb_read_text) */
    show_cursor();
    sh_tx0 = cursor_x; sh_ty0 = cursor_y;

    while (i < max - 1) {
        if (!kb_has_data()) {
            blink_counter++;
            if (blink_counter > 300000u) {
                blink_counter = 0;
                /* 闪烁前先 flush, 保证"新字 + 光标切换"同步显示 */
                drv_gfx_maybe_flush();
                s_save_x = s_save_y = -1;
                if (cursor_visible) draw_cursor(0);
                else draw_cursor(1);
            }
            if (s_mouse_visible) draw_mouse_cursor();
            if ((blink_counter & 0x3FFu) == 0) { lapic_timer_check(); sched_yield(); }
            asm volatile("pause");
            continue;
        }
        if (!cursor_visible) show_cursor();
        blink_counter = 0;

        unsigned int sc = kbdq_pop();
        int key = kb_scan_to_key(sc, &shift, &caps);
        if (key == 0) continue;
        hide_mouse_cursor();

        /* Esc → 取消输入 */
        if (key == KEY_ESC) { hide_cursor(); efs_put_char('\n'); efs_gfx_flush_nonwm(); return -1; }

        /* Enter → 换行/ENDOFFILE 结束 */
        if (key == KEY_ENTER) {
            int last_nl = -1;
            for (int k = 0; k < i; k++) if (buf[k] == '\n') last_nl = k;
            const char *line = buf + (last_nl + 1);
            int line_len = i - (last_nl + 1);
            int tlen; const char *t = sh_trim_str(line, line_len, &tlen);
            static const char END_MARK[] = "ENDOFFILE";
            int match = (tlen == 9);
            if (match) { for (int q = 0; q < 9; q++) if (t[q] != END_MARK[q]) { match = 0; break; } }
            if (match) {
                /* 整行独占 ENDOFFILE → 剥离最后一行结束输入 */
                i = (last_nl >= 0) ? last_nl : 0;
                buf[i] = 0;
                hide_cursor();
                efs_put_char('\n');
                efs_gfx_flush_nonwm();
                return i;
            }
            for (int k = i; k > pos; k--) buf[k] = buf[k - 1];
            buf[pos++] = '\n'; i++;
            sh_redraw_whole_text(buf, i, pos);
            continue;
        }
        /* Backspace → 删除 pos 左侧字符 */
        if (key == KEY_BACKSPACE) {
            if (pos > 0) {
                if (pos == i) {
                    /* 末尾删除快速路径: 只擦一个字符, 不重绘整个 buffer */
                    hide_cursor();
                    pos--; i--;
                    buf[i] = 0;
                    unsigned char dc = (unsigned char)buf[pos];
                    int cw = (dc >= 0x80) ? 2*FONT_W : FONT_W;
                    fill_rect(cursor_x - cw, cursor_y, cw, FONT_H, bg);
                    cursor_x -= cw;
                    if (cursor_x < sh_tx0) {
                        cursor_y -= FONT_H;
                        int k = pos - 1;
                        while (k >= 0 && buf[k] != '\n') k--;
                        int line_w = sh_tx0;
                        for (int j = k + 1; j < pos; j++) {
                            unsigned char c = (unsigned char)buf[j];
                            if (c < 0x80) line_w += FONT_W;
                            else if ((c & 0xE0) == 0xC0) { line_w += 2*FONT_W; j++; }
                            else if ((c & 0xF0) == 0xE0) { line_w += 2*FONT_W; j += 2; }
                            else line_w += FONT_W;
                        }
                        cursor_x = line_w;
                    }
                    show_cursor();
                    efs_gfx_flush_nonwm();
                } else {
                    for (int k = pos - 1; k < i; k++) buf[k] = buf[k + 1];
                    pos--; i--;
                    sh_redraw_whole_text(buf, i, pos);
                }
            }
            continue;
        }
        /* Delete → 删除光标下字符 */
        if (key == KEY_DELETE) {
            if (pos < i) {
                for (int k = pos; k < i; k++) buf[k] = buf[k + 1];
                i--;
                sh_redraw_whole_text(buf, i, pos);
            }
            continue;
        }
        if (key == KEY_LEFT) {
            if (pos > 0) {
                pos--;
                hide_cursor();
                sh_move_cursor_to_pos(buf, i, pos);
                show_cursor();
                efs_gfx_flush_nonwm();
            }
            continue;
        }
        if (key == KEY_RIGHT) {
            if (pos < i) {
                pos++;
                hide_cursor();
                sh_move_cursor_to_pos(buf, i, pos);
                show_cursor();
                efs_gfx_flush_nonwm();
            }
            continue;
        }
        if (key == KEY_HOME) {
            int k = pos - 1;
            while (k >= 0 && buf[k] != '\n') k--;
            pos = k + 1;
            hide_cursor();
            sh_move_cursor_to_pos(buf, i, pos);
            show_cursor();
            efs_gfx_flush_nonwm();
            continue;
        }
        if (key == KEY_END) {
            int k = pos;
            while (k < i && buf[k] != '\n') k++;
            pos = k;
            hide_cursor();
            sh_move_cursor_to_pos(buf, i, pos);
            show_cursor();
            efs_gfx_flush_nonwm();
            continue;
        }
        /* Tab → 插入 4 个空格 */
        if (key == KEY_TAB) {
            int need = 4;
            if (i + need > max - 1) need = (max - 1) - i;
            if (need <= 0) continue;
            for (int k = i + need - 1; k >= pos + need; k--) buf[k] = buf[k - need];
            for (int q = 0; q < need; q++) buf[pos + q] = ' ';
            pos += need; i += need;
            sh_redraw_whole_text(buf, i, pos);
            continue;
        }
        /* 可打印字符: 在 pos 插入 (末尾追加快速路径, 中间插入整段重绘) */
        if (key > 0) {
            char c = (char)key;
            if (c >= 0x20 && i < max - 1) {
                if (pos == i) {
                    hide_cursor();
                    put_char(c);
                    buf[pos++] = c; i++;
                    buf[i] = 0;
                    show_cursor();
                    efs_gfx_flush_nonwm();
                } else {
                    for (int k = i; k > pos; k--) buf[k] = buf[k - 1];
                    buf[pos++] = c; i++;
                    sh_redraw_whole_text(buf, i, pos);
                }
            }
            continue;
        }
    }
    hide_cursor();
    efs_put_char('\n');
    efs_gfx_flush_nonwm();
    buf[i] = 0;
    return i;
}
static void efs_reboot(void) {
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    while (1) asm volatile("hlt");
}
static int efs_get_lang(void) { return efm_lang; }
static void efs_set_lang(int l) { efm_lang = l; }
static int efs_save_settings(void) { return 0; }  /* 设置已由 file_write 持久化 */
/* [鼠标 API 包装] */
static int efs_mouse_poll(void *out_event) {
    if (!out_event) return 0;
    struct mouse_event ev;
    int got = mouse_poll(&ev);
    if (got) {
        /* 拷贝到用户结构 (字段顺序: int dx,dy; u8 btn; int x,y) — 与我们的 struct 完全一致, 直接 memcpy */
        my_memcpy(out_event, &ev, sizeof(struct mouse_event));
    }
    /* [WM 模式修复] 桌面合成模式下, 鼠标光标由空闲轮询的 draw_mouse_cursor 统一维护。
     * 这里不能再直接 draw_mouse_cursor(), 否则会直接写全局 fb 破坏合成结果,
     * 触发 fill_rect 擦掉窗口内容 → 画面乱。置 g_wm_dirty=1 让下一节拍重绘即可。 */
    if (!g_wm_enabled) {
        if (s_mouse_visible) draw_mouse_cursor();
    } else if (got || s_mouse_visible) {
        g_wm_dirty = 1;
    }
    /* [协作调度] 无鼠标事件时让出 CPU, 同 efs_key_poll, 防止忙等饿死 shell。 */
    if (!got && g_wm_enabled) { lapic_timer_check(); sched_yield(); }
    return got ? 1 : 0;
}
static void efs_mouse_set_cursor(int show) {
    mouse_set_cursor(show);
    if (g_wm_enabled) g_wm_dirty = 1;
}

/* [.efs 键盘 API] 统一由内核 classify_rawq() 从 0x60 读硬件字节,
 * 再从 kbdq 取扫描码转成键值。避免 .efs 程序直接 IN 0x60 与
 * mouse_poll/classify_rawq 抢读导致的丢键/卡死问题。
 * 返回: 0=无键; >0=ASCII 可打印字符; <0 特殊键 (-101=Enter -102=BS -103=ESC) */
static int efs_key_poll(void) {
    static int sh = 0, cap = 0;
    /* [WM 模式修复] 不要在这里直接画鼠标 (会覆盖桌面), 仅置脏标记交给空闲轮询统一重绘。
     * 非 WM 模式 (登录前 / 全屏 EFS) 仍保留鼠标绘制。 */
    if (g_wm_enabled) {
        if (s_mouse_visible) g_wm_dirty = 1;
    } else {
        if (s_mouse_visible) draw_mouse_cursor();
    }
    if (!kb_has_data()) {
        /* [协作调度] 无键时让出 CPU, 避免 .efs 程序忙等饿死 shell 任务。
         *   这是 start 命令启动的程序卡死的根因之一: EFS 程序在 key_poll
         *   空转循环中从不 sched_yield → shell 永远得不到运行 → 画面不刷新。 */
        if (g_wm_enabled) { lapic_timer_check(); sched_yield(); }
        return 0;
    }
    unsigned int sc = kbdq_pop();
    int key = kb_scan_to_key(sc, &sh, &cap);
    if (key == 0) return 0;                 /* 非 make 或不可识别 */
    if (key == KEY_ENTER)      return -101;  /* LKEY_ENTER */
    if (key == KEY_BACKSPACE)  return -102;  /* LKEY_BS */
    if (key == KEY_ESC)        return -103;  /* LKEY_ESC */
    /* [efmshell] 扩展特殊键码: 供 efmshell 及后续 EFS 程序实现行编辑/导航.
     * 旧程序 (userman/fileman/setting) 只比对特定值, 未知负值被其忽略, 向后兼容. */
    if (key == KEY_UP)         return -104;  /* LKEY_UP */
    if (key == KEY_DOWN)       return -105;  /* LKEY_DOWN */
    if (key == KEY_LEFT)       return -106;  /* LKEY_LEFT */
    if (key == KEY_RIGHT)      return -107;  /* LKEY_RIGHT */
    if (key == KEY_HOME)       return -108;  /* LKEY_HOME */
    if (key == KEY_END)        return -109;  /* LKEY_END */
    if (key == KEY_DELETE)     return -110;  /* LKEY_DELETE */
    if (key == KEY_TAB)        return -111;  /* LKEY_TAB */
    if (key > 0 && key >= 0x20 && key <= 0x7E) return key;  /* 可打印 ASCII */
    return 0;                                /* 其他忽略 */
}

/* ========== 用户系统: 全局变量 ==========
 * 用显式初始化从 .bss 挪到 .data, 避免 flat-bin 加载时未初始化 */
static char current_username[32] = "";  /* 当前登录用户, 默认空=未登录 */

/* ---------- 用户系统 API 包装函数 ---------- */
static int efs_get_current_user(char *buf, int bufsz) {
    if (!buf || bufsz <= 1) return -1;
    int len = my_strlen(current_username);
    if (len >= bufsz) len = bufsz - 1;
    my_memcpy(buf, current_username, len);
    buf[len] = 0;
    return len;
}

/* 工具: 拼接用户目录路径 /users/<username> 到 out (最大 outsz-1) */
static void build_user_path(const char *username, char *out, int outsz) {
    if (outsz < 16) { if (outsz) out[0] = 0; return; }
    const char *prefix = "/users/";
    int pl = my_strlen(prefix);
    int ul = my_strlen(username);
    if (pl + ul + 1 > outsz) ul = outsz - pl - 1;
    my_memcpy(out, prefix, pl);
    my_memcpy(out + pl, username, ul);
    out[pl + ul] = 0;
}

/* 检查用户名是否有效: 非空、长度<=28、不含 '/' ' ' '\\' 等非法字符 */
static int is_valid_username(const char *u) {
    if (!u || !u[0]) return 0;
    int i = 0;
    while (u[i]) {
        char c = u[i];
        if (c == '/' || c == '\\' || c == ' ' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 0;
        if (++i > 28) return 0;
    }
    return i > 0 ? 1 : 0;
}

/* 切换当前用户: 返回 0=成功, -1=失败。同时会把 shell 的 current_dir_ino 重置为新用户的 home 目录
 * (shell 下次显示 prompt 时会重新解析 current_path) */
static int efs_set_current_user(const char *username) {
    if (!is_valid_username(username)) return -1;
    /* 验证 /users/<username> 目录存在 */
    char upath[256]; build_user_path(username, upath, sizeof(upath));
    unsigned int uino = resolve_inode(upath, NULL);
    if (!uino) return -1;
    unsigned char chk[256]; read_inode(uino, chk);
    unsigned short mode = *(unsigned short*)(chk + 0);
    if (!(mode & 0x4000)) return -1;  /* 必须是目录 */

    /* 切换当前用户 */
    int ul = my_strlen(username);
    if (ul >= (int)sizeof(current_username)) ul = sizeof(current_username) - 1;
    my_memcpy(current_username, username, ul);
    current_username[ul] = 0;

    /* 同步切换 shell 默认目录到该用户的家目录 (供 shell_main 使用) */
    current_dir_ino = uino;
    int pl = my_strlen(upath);
    if (pl >= 256) pl = 255;
    my_memcpy(current_path, upath, pl);
    current_path[pl] = 0;

    /* 不再写 users.conf — 当前用户仅在内存中, 开机时通过 userman 登录 */
    return 0;
}

/* 列出用户: 以 /users/users.conf 内容为准 (每行一个用户名)。
 * users.conf 是用户注册表的权威来源, /users/<name> 目录仅是家目录。
 * 返回填充的条目数 (>=0)。 */
static int efs_user_list(struct efs_dirent *out, int max_count) {
    if (!out || max_count <= 0) return 0;
    /* 读取 users.conf 内容 */
    char buf[2048];
    int n = efs_file_read("/users/users.conf", buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = 0;
    /* 逐行解析用户名 */
    int count = 0;
    const char *p = buf;
    while (*p && count < max_count) {
        /* 跳过前导空白/换行 */
        while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        /* 提取一行用户名 */
        int len = 0;
        while (p[len] && p[len] != '\n' && p[len] != '\r' && len < 63) len++;
        if (len > 0 && is_valid_username(p)) {
            my_memcpy(out[count].name, p, len);
            out[count].name[len] = 0;
            out[count].size = 0;
            out[count].is_dir = 1;   /* 兼容: 旧代码可能检查 is_dir */
            count++;
        }
        p += len;
    }
    return count;
}

/* 创建新用户: 在 /users 下创建 <username> 目录 + data 目录,
 *   并把用户名追加到 /users/users.conf (用户注册表, 权威来源) */
static int efs_user_create(const char *username) {
    if (!is_valid_username(username)) return -1;
    char upath[256]; build_user_path(username, upath, sizeof(upath));
    /* 已存在? 失败 */
    if (resolve_inode(upath, NULL)) return -1;
    /* 创建 /users/<username> */
    if (efs_mkdir(upath) != 0) return -1;
    /* 创建 /users/data/<username> (存放个性化数据) */
    char dpath[256];
    const char *prefix = "/users/data/";
    int pl = my_strlen(prefix);
    int ul = my_strlen(username);
    my_memcpy(dpath, prefix, pl);
    my_memcpy(dpath + pl, username, ul);
    dpath[pl + ul] = 0;
    (void)efs_mkdir(dpath);  /* 已存在不算错误 */
    /* 追加用户名到 /users/users.conf (每行一个用户名)
     * efs_file_write 是全量覆盖语义, 需先读旧内容再拼接重写 */
    {
        char conf[2048];
        int cl = efs_file_read("/users/users.conf", conf, sizeof(conf) - 64);
        if (cl < 0) cl = 0;
        conf[cl] = 0;
        /* 拼接 "\n<username>\n" */
        int wp = cl;
        if (wp > 0 && conf[wp - 1] != '\n') conf[wp++] = '\n';
        my_memcpy(conf + wp, username, ul);
        wp += ul;
        conf[wp++] = '\n';
        efs_file_write("/users/users.conf", conf, wp);
        serial_write("user_create: appended user to users.conf\n");
    }
    /* 为新用户写入默认 setting.conf (/users/data/<username>/setting.conf)
     * 默认格式与 setting.c 的 build_config 保持一致:
     *   lang=0 (跟随系统默认英文), cursor_blink=1, fg=FFFFFF, bg=B0BEC5 */
    {
        const char *suffix = "/setting.conf";
        int sl = 0; while (suffix[sl]) sl++;
        int dl = pl + ul;          /* dpath[dl] 是 /users/data/<username> 末尾 */
        if (dl + sl + 1 < (int)sizeof(dpath)) {
            my_memcpy(dpath + dl, suffix, sl);
            dpath[dl + sl] = 0;
            const char *def = "lang=0\ncursor_blink=1\nfg=FFFFFF\nbg=1A1A2E\n";
            int bl = 0; while (def[bl]) bl++;
            (void)efs_file_write(dpath, def, bl);
            serial_write("user_create: default setting.conf written\n");
        }
    }
    return 0;
}

/* 删除用户: 从 users.conf 移除用户名行 + 删目录, 不能删当前登录用户 */
static int efs_user_delete(const char *username) {
    if (!is_valid_username(username)) return -1;
    /* 不能删除当前用户 */
    if (my_strcmp(current_username, username) == 0) return -1;
    char upath[256]; build_user_path(username, upath, sizeof(upath));
    /* 从 /users/users.conf 移除该用户名行 (重写文件, 跳过匹配行) */
    {
        char conf[2048];
        int cl = efs_file_read("/users/users.conf", conf, sizeof(conf) - 1);
        if (cl > 0) {
            conf[cl] = 0;
            char out[2048]; int wp = 0;
            const char *p = conf;
            int ul = my_strlen(username);
            while (*p) {
                /* 取一行 */
                int len = 0;
                while (p[len] && p[len] != '\n') len++;
                /* 比较该行是否等于 username (精确匹配, 长度+内容) */
                int match = (len == ul);
                for (int i = 0; match && i < ul; i++)
                    if (p[i] != username[i]) match = 0;
                if (!match && len > 0) {
                    my_memcpy(out + wp, p, len);
                    wp += len;
                    out[wp++] = '\n';
                }
                p += len;
                if (*p == '\n') p++;
            }
            efs_file_write("/users/users.conf", out, wp);
            serial_write("user_delete: removed user from users.conf\n");
        }
    }
    /* 删除 /users/<username> 目录 (必须为空) */
    char child[128];
    unsigned int pino = efs_resolve_parent(upath, child, sizeof(child));
    if (!pino) return -1;
    int r = ext4_rmdir(pino, child);
    /* 同时删除 /users/data/<username> 目录 (用户数据) */
    {
        char dpath[256];
        const char *prefix = "/users/data/";
        int pl = my_strlen(prefix);
        int ul = my_strlen(username);
        my_memcpy(dpath, prefix, pl);
        my_memcpy(dpath + pl, username, ul);
        dpath[pl + ul] = 0;
        char dchild[128];
        unsigned int dpino = efs_resolve_parent(dpath, dchild, sizeof(dchild));
        if (dpino) {
            /* 删除 data 目录下的 setting.conf / passwd (如果存在) */
            char fpath[256];
            my_memcpy(fpath, dpath, pl + ul);
            my_memcpy(fpath + pl + ul, "/passwd", 8);
            (void)efs_file_delete(fpath);
            my_memcpy(fpath, dpath, pl + ul);
            my_memcpy(fpath + pl + ul, "/setting.conf", 14);
            (void)efs_file_delete(fpath);
            (void)ext4_rmdir(dpino, dchild);
        }
    }
    return r;
}

/* ========== 内核堆分配器 (增强版) ==========
 * 基础: UEFI 身份映射, 堆区 32MB~64MB。
 * 增强特性:
 *   1. 自旋锁: 多核安全 (SMP 场景)
 *   2. Canary 越界检测: 在用户数据尾 16 字节写 0xDEADBEEF 魔数, free 时校验
 *   3. Size-classes: 小内存 (8~4096) 走 O(1) freelist buckets,
 *      大内存 (>4KB) 走传统 first-fit 边界标记链表
 *   4. 诊断统计: alloc/free 计数, 泄漏统计, 当前分配量峰值 */
#define KHEAP_BASE  0x4000000UL   /* 64MB (在所有 .drv 加载地址之上, 避免 back buffer
                                    * 分配与驱动代码冲突: vga=32MB ahci=36MB gfx=40MB) */
#define KHEAP_SIZE  0x4000000UL   /* 64MB (64MB~128MB, 高分辨率下 back buffer+canvas 约 16MB) */
#define KHEAP_ALIGN 16UL
#define KHEAP_CANARY 0xDEADBEEFCAFEBABEULL

/* [简易自旋锁] xchg 实现, 禁止在持有锁期间进入睡眠 (否则死锁) */
typedef volatile int spinlock_t;
#define SPIN_INIT {0}
static inline void spin_lock(spinlock_t *lk) {
    int expected = 0;
    while (__atomic_exchange_n(lk, 1, __ATOMIC_ACQUIRE) != 0) {
        while (__atomic_load_n(lk, __ATOMIC_RELAXED)) asm volatile("pause");
    }
}
static inline void spin_unlock(spinlock_t *lk) {
    __atomic_store_n(lk, 0, __ATOMIC_RELEASE);
}

struct mblock {
    unsigned long size;        /* 含头总大小 */
    struct mblock *next;       /* 空闲链表 (按地址排序, 仅含空闲块) */
    struct mblock *prev;
    int free;                  /* 1=空闲 0=已分配 */
};

/* [Size-class 桶] 8/16/32/64/128/256/512/1024/2048/4096 => 10 档, O(1) 分配释放
 * 注意: 实际给用户的有效字节 (payload) 是 class_sz - sizeof(mblock) - 8B canary,
 * 所以我们按 "size + head + canary ≤ class_sz" 选择桶 */
#define SC_NUM 10
static const unsigned long g_sc_sz[SC_NUM] = { 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384 };
static struct mblock *g_sc_head[SC_NUM];   /* 每档的单链表 (push/pop head) */

static struct mblock *kheap_head = (struct mblock*)0;   /* 大内存 first-fit 空闲链表 */
static spinlock_t  kheap_lock = SPIN_INIT;
static int kheap_inited = 0;

/* 堆诊断统计: 计数器均被锁保护 */
static unsigned long g_st_alloc_bytes = 0;   /* 当前已分配字节 (payload 总和) */
static unsigned long g_st_alloc_peak  = 0;   /* 历史峰值 */
static unsigned long g_st_alloc_cnt   = 0;   /* malloc 总次数 */
static unsigned long g_st_free_cnt    = 0;   /* free 总次数 */
static unsigned long g_st_canary_fail = 0;   /* canary 检测失败次数 (越界写) */
static unsigned long g_st_double_free = 0;   /* 双重释放次数 */

/* [kheap canary 一致性] payload 字节数 = block_size - 头 - canary(8).
 * size-class 桶分配时 canary 必须写在这个偏移处 (与 free 检查位置一致),
 * 否则 need_payload(用户请求对齐) != payload_sz(桶块实际容量) 会导致 free 误报 canary corrupted. */
static inline unsigned long kheap_payload_sz(unsigned long block_size) {
    return block_size - sizeof(struct mblock) - 8;
}

/* 写/校验 canary: 放在 (payload 首 + request_sz), 最多 8 字节 */
static inline void *canary_addr(struct mblock *b, unsigned long req_sz) {
    unsigned char *pb = (unsigned char*)b + sizeof(struct mblock);
    return pb + req_sz;
}
static inline void canary_write(struct mblock *b, unsigned long req_sz) {
    /* 把尾 canary 写到 user 数据之后。若剩余空间不足 8B, 写到 payload 末尾之前,
     * 这样 free 时可稳定定位到 8B canary。采用简化实现: 统一写在 (data + (size & ~7ULL)) */
    unsigned long *c = (unsigned long*)canary_addr(b, req_sz);
    *c = KHEAP_CANARY;
}
static inline int canary_check(struct mblock *b, unsigned long req_sz) {
    unsigned long *c = (unsigned long*)canary_addr(b, req_sz);
    if (*c != KHEAP_CANARY) return 0;
    return 1;
}

static void kheap_init(void) {
    struct mblock *b = (struct mblock*)KHEAP_BASE;
    b->size = KHEAP_SIZE;
    b->next = b->prev = 0;
    b->free = 1;
    kheap_head = b;
    for (int i = 0; i < SC_NUM; i++) g_sc_head[i] = 0;
    g_st_alloc_bytes = g_st_alloc_peak = 0;
    g_st_alloc_cnt = g_st_free_cnt = g_st_canary_fail = g_st_double_free = 0;
    kheap_inited = 1;
}

/* 把一块从大链表摘下并分配。返回 data 指针。total = 用户申请对齐后 + 头 + 8B canary + 对齐 */
static void *__alloc_big(struct mblock *b, unsigned long total, unsigned long need_payload) {
    /* 分裂: 剩余够再放一个块 */
    if (b->size >= total + sizeof(struct mblock) + KHEAP_ALIGN) {
        struct mblock *nb = (struct mblock*)((unsigned char*)b + total);
        nb->size = b->size - total;
        nb->next = b->next;
        nb->prev = b;
        nb->free = 1;
        if (b->next) b->next->prev = nb;
        b->next = nb;
        b->size = total;
    }
    struct mblock *pv = b->prev, *nx = b->next;
    if (pv) pv->next = nx; else kheap_head = nx;
    if (nx) nx->prev = pv;
    b->next = b->prev = 0;
    b->free = 0;
    unsigned char *data = (unsigned char*)b + sizeof(struct mblock);
    canary_write(b, need_payload);
    return data;
}

void *kheap_malloc(unsigned long size) {
    if (!kheap_inited) kheap_init();
    if (size == 0) return 0;
    /* 需要字节数: payload = 对齐(size), total = payload + 头 + canary(8B) */
    unsigned long need_payload = ((size + KHEAP_ALIGN - 1) / KHEAP_ALIGN) * KHEAP_ALIGN;
    unsigned long total = need_payload + sizeof(struct mblock) + 8;

    void *res = 0;
    spin_lock(&kheap_lock);
    /* 小分配: 先查 size-class O(1) 桶 */
    if (total <= g_sc_sz[SC_NUM - 1]) {
        int cls = -1;
        for (int i = 0; i < SC_NUM; i++) {
            if (g_sc_sz[i] >= total) { cls = i; break; }
        }
        if (cls >= 0 && g_sc_head[cls]) {
            struct mblock *b = g_sc_head[cls];
            g_sc_head[cls] = b->next;
            b->next = 0;
            b->free = 0;
            unsigned char *data = (unsigned char*)b + sizeof(struct mblock);
            /* [canary 一致性] 桶块的 b->size = g_sc_sz[cls], free 时 payload_sz = b->size - 头 - 8.
             * canary 必须写在 payload_sz 偏移处 (而非 need_payload), 否则 free 时位置不一致误报. */
            canary_write(b, kheap_payload_sz(b->size));
            res = data;
        } else if (cls >= 0) {
            /* 桶空: 从大链表切一块整 class 的块 (给后续快速分配)
             * 策略: 向大链表申请 g_sc_sz[cls] 大小, 走 big path, 但记录为 class-cls 块 */
            unsigned long want_total = g_sc_sz[cls];
            struct mblock *fb = kheap_head;
            while (fb && (!fb->free || fb->size < want_total)) fb = fb->next;
            if (fb) {
                struct mblock *nb = (struct mblock*)((unsigned char*)fb + want_total);
                nb->size = fb->size - want_total;
                nb->next = fb->next;
                nb->prev = fb;
                nb->free = 1;
                if (fb->next) fb->next->prev = nb;
                fb->next = nb;
                fb->size = want_total;
                /* 从链表摘除 fb */
                struct mblock *pv = fb->prev, *nx = fb->next;
                if (pv) pv->next = nx; else kheap_head = nx;
                if (nx) nx->prev = pv;
                /* 把 fb 作为已分配块返回 */
                fb->next = 0; fb->prev = 0; fb->free = 0;
                unsigned char *data = (unsigned char*)fb + sizeof(struct mblock);
                /* [canary 一致性] 同上, 用 payload_sz 而非 need_payload */
                canary_write(fb, kheap_payload_sz(fb->size));
                res = data;
            }
        }
    }
    /* 大分配 or 桶空 but 走大链表 */
    if (!res) {
        for (struct mblock *b = kheap_head; b; b = b->next) {
            if (!b->free || b->size < total) continue;
            res = __alloc_big(b, total, need_payload);
            break;
        }
    }
    if (res) {
        g_st_alloc_cnt++;
        g_st_alloc_bytes += need_payload;
        if (g_st_alloc_bytes > g_st_alloc_peak) g_st_alloc_peak = g_st_alloc_bytes;
    }
    spin_unlock(&kheap_lock);
    return res;
}

static void kheap_free(void *ptr) {
    if (!ptr) return;
    struct mblock *b = (struct mblock*)((unsigned char*)ptr - sizeof(struct mblock));
    spin_lock(&kheap_lock);
    if (b->free) { g_st_double_free++; spin_unlock(&kheap_lock); return; }

    /* 1) canary 校验: 用户数据区 = ptr 开始, 真实 payload 大小需要从 b->size 反推
     * 简化: payload 字节数 = b->size - sizeof(mblock) - 8 (canary);
     *       写/校验均按此值 (用户申请 <= payload) */
    unsigned long payload_sz = b->size - sizeof(struct mblock) - 8;
    if (!canary_check(b, payload_sz)) {
        g_st_canary_fail++;
        /* [诊断] 不 panic, 但串口打日志, 仍按正常释放 (防止泄漏) */
        serial_write("HEAP: canary corrupted at block ");
        serial_write_hex((unsigned long long)((unsigned long)b));
        serial_write(" size="); serial_write_hex((unsigned long long)b->size);
        serial_write(" fails="); serial_write_hex((unsigned long long)g_st_canary_fail);
        serial_write("\n");
    }
    g_st_free_cnt++;
    if (g_st_alloc_bytes >= payload_sz) g_st_alloc_bytes -= payload_sz;
    else g_st_alloc_bytes = 0;

    b->free = 1;

    /* 2) size-class 快速放回桶 (如果整 block 大小正好匹配桶的 class_sz) */
    int cls = -1;
    for (int i = 0; i < SC_NUM; i++) {
        if (g_sc_sz[i] == b->size) { cls = i; break; }
    }
    if (cls >= 0) {
        b->next = g_sc_head[cls];
        g_sc_head[cls] = b;
        spin_unlock(&kheap_lock);
        return;
    }

    /* 3) 按地址插入大空闲链表 */
    if (!kheap_head || b < kheap_head) {
        b->prev = 0; b->next = kheap_head;
        if (kheap_head) kheap_head->prev = b;
        kheap_head = b;
    } else {
        struct mblock *p = kheap_head;
        while (p->next && p->next < b) p = p->next;
        b->next = p->next; b->prev = p;
        if (p->next) p->next->prev = b;
        p->next = b;
    }
    /* 合并后继 & 前驱 */
    if (b->next && (unsigned char*)b + b->size == (unsigned char*)b->next) {
        struct mblock *n = b->next;
        b->size += n->size;
        b->next = n->next;
        if (n->next) n->next->prev = b;
    }
    if (b->prev && (unsigned char*)b->prev + b->prev->size == (unsigned char*)b) {
        struct mblock *p = b->prev;
        p->size += b->size;
        p->next = b->next;
        if (b->next) b->next->prev = p;
    }
    spin_unlock(&kheap_lock);
}

/* 对外暴露堆统计 (用于 free/mem 命令显示) */
static void kheap_get_stats(unsigned long *alloc_bytes, unsigned long *alloc_peak,
                            unsigned long *alloc_cnt, unsigned long *free_cnt,
                            unsigned long *canary_fail, unsigned long *double_free) {
    spin_lock(&kheap_lock);
    if (alloc_bytes) *alloc_bytes = g_st_alloc_bytes;
    if (alloc_peak)  *alloc_peak  = g_st_alloc_peak;
    if (alloc_cnt)   *alloc_cnt   = g_st_alloc_cnt;
    if (free_cnt)    *free_cnt    = g_st_free_cnt;
    if (canary_fail) *canary_fail = g_st_canary_fail;
    if (double_free) *double_free = g_st_double_free;
    spin_unlock(&kheap_lock);
}

/* 简化 realloc (内部使用时 gcc.c 的 g_realloc 最终调用 efs_realloc,
 * 当前 gcc.c 通过 malloc/copy/free 模拟; 这里提供优化版 realloc) */
static void *kheap_realloc(void *old_ptr, unsigned long old_sz, unsigned long new_sz) {
    (void)old_sz;
    void *np = kheap_malloc(new_sz);
    if (!np) return 0;
    if (old_ptr && old_sz) {
        unsigned long copy_sz = old_sz < new_sz ? old_sz : new_sz;
        my_memcpy(np, old_ptr, (int)copy_sz);
        kheap_free(old_ptr);
    }
    return np;
}

/* ============================================================
 *  物理页帧分配器 (Page Frame Allocator)
 *  - 以 4KB 为单位管理从 64MB 往上的物理内存
 *  - 位图 bitmap: 每 1 bit 代表 1 页, 0=free, 1=used
 *  - 优先从 64MB+ (避开内核与 kheap 32-64MB) 的连续区域分配
 *  ============================================================ */
#define PAGE_SIZE   4096UL
#define PAGE_SHIFT  12
#define PFA_BASE    (64UL * 1024UL * 1024UL)   /* 页分配起始物理地址: 64MB */
#define PFA_BITMAP_MAX_PAGES  (16UL * 1024UL)    /* 最多管理 16384 页 = 64MB */
#define PFA_TOTAL_BYTES (PFA_BITMAP_MAX_PAGES * PAGE_SIZE)
static unsigned char g_pfa_bitmap[PFA_BITMAP_MAX_PAGES / 8];
static int g_pfa_inited = 0;
static spinlock_t g_pfa_lock = SPIN_INIT;
static unsigned long g_pfa_alloc_count = 0;

static inline void pfa_mark_used(unsigned long pfn) {
    g_pfa_bitmap[pfn >> 3] |= (1u << (pfn & 7));
}
static inline void pfa_mark_free(unsigned long pfn) {
    g_pfa_bitmap[pfn >> 3] &= ~(1u << (pfn & 7));
}
static inline int  pfa_is_used(unsigned long pfn) {
    return (g_pfa_bitmap[pfn >> 3] >> (pfn & 7)) & 1u;
}
static void pfa_init(void) {
    for (unsigned long i = 0; i < sizeof(g_pfa_bitmap); i++) g_pfa_bitmap[i] = 0;
    g_pfa_inited = 1;
    g_pfa_alloc_count = 0;
}
/* 分配 n 个连续物理页, 返回物理地址 (或 NULL). 优先低 PFN。 */
static void *pfa_alloc_pages(unsigned long n) {
    if (!g_pfa_inited) pfa_init();
    if (n == 0 || n > PFA_BITMAP_MAX_PAGES) return 0;
    spin_lock(&g_pfa_lock);
    unsigned long start = 0;
    unsigned long run = 0;
    for (unsigned long pfn = 0; pfn < PFA_BITMAP_MAX_PAGES; pfn++) {
        if (!pfa_is_used(pfn)) {
            if (run == 0) start = pfn;
            run++;
            if (run == n) {
                for (unsigned long i = 0; i < n; i++) pfa_mark_used(start + i);
                g_pfa_alloc_count += n;
                spin_unlock(&g_pfa_lock);
                return (void*)(PFA_BASE + start * PAGE_SIZE);
            }
        } else {
            run = 0;
        }
    }
    spin_unlock(&g_pfa_lock);
    return 0;
}
static void pfa_free_pages(void *ptr, unsigned long n) {
    if (!ptr || !g_pfa_inited) return;
    unsigned long phys = (unsigned long)ptr;
    if (phys < PFA_BASE || (phys - PFA_BASE) % PAGE_SIZE != 0) return;
    unsigned long start_pfn = (phys - PFA_BASE) / PAGE_SIZE;
    if (start_pfn + n > PFA_BITMAP_MAX_PAGES) return;
    spin_lock(&g_pfa_lock);
    for (unsigned long i = 0; i < n; i++) pfa_mark_free(start_pfn + i);
    if (g_pfa_alloc_count >= n) g_pfa_alloc_count -= n;
    spin_unlock(&g_pfa_lock);
}

/* ============================================================
 *  LAPIC 周期性定时器 + 抢占式调度 (Preemptive Tick)
 *  - 每 ~10ms 触发一次 (Periodic LVTT timer)
 *  - 定时器中断处理时: 对当前任务剩余时间片--, 归零则 g_need_resched=1
 *  - sched_yield() 增强: 检查时间片, 并调用 schedule() 做完整 round-robin
 *  - 因为当前系统 PIC 未 remap / 8259 屏蔽, 我们直接用 LAPIC LVTT + IRQ handler
 *    但系统目前 IDT 32~255 未填具体 handler; 用轮询 LAPIC Timer 寄存器也可。
 *    简化实现: 在所有 poll 循环的 idle 期, 周期性调用
 *    lapic_timer_check(), 若 count 已过零则触发 tick + 可能 sched_yield()
 *  ============================================================ */
static unsigned long g_tick_count = 0;
static int g_lapic_timer_inited = 0;

static void lapic_timer_init(void) {
    /* [紧急修复] LAPIC timer 的 vector 0x20 ISR 尚未在 IDT 中注册,
     * 若开启 PERIODIC 模式会在 4~10ms 后触发 #GP/#DF 三故障跑飞 (屏幕打印垃圾字符如反引号 0x60)。
     * 安全降级: 先 MASK LVTT (完全不产生 CPU 中断), 仅通过 lapic_timer_check 的轮询节流来模拟
     * 时间片到期, 仍保留协作调度 yield 能力。 */
    lapic_write(LAPIC_TIMER_DIV, 0x03);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_MASKED);   /* MASKED: 禁止生成中断到 CPU, 计数继续 */
    lapic_write(LAPIC_TIMER_INIT, 0x40000);        /* 内部计数自由跑, 用于未来校准, 不发 IRQ */
    g_lapic_timer_inited = 1;
    g_tick_count = 0;
    serial_write("LAPIC: timer configured (MASKED polling-only, no IRQ)\n");
}
/* 非阻塞检查当前 tick 是否到期; 返回 1=已触发一次 sched_yield() */
static int lapic_timer_check(void) {
    if (!g_lapic_timer_inited) lapic_timer_init();
    /* LAPIC_CUR 为 0 代表计数归 0 一次 (或初始)。
     * 简化: 读当前计数寄存器, 若它与上次近似, 用 tick 计数轮询;
     * 更简单: 直接每次检测都给时间片--, 到 0 就切。为避免过快, 加 throttle。 */
    static unsigned long last_throttle = 0;
    last_throttle++;
    if (last_throttle < 100000UL) return 0;  /* 大约 ~1ms 才扣一次 (粗略校准) */
    last_throttle = 0;
    g_tick_count++;
    /* 通过访问器操作 time_slice: 返回 1 表示时间片耗尽需要调度 */
    if (current_timeslice_dec_and_check()) {
        sched_yield();
        return 1;
    }
    return 0;
}

/* ============================================================
 *  EFS 异步启动 (进程级多任务)
 *  把 .efs 程序包装成独立 task_struct:
 *   - 分配独立内核栈 (TASK_STACK_SIZE) + 独立用户栈 (EFS_STACK_SIZE 1MB)
 *   - 加载 .efs 二进制到 load_addr
 *   - task entry = efs_task_entry: 设置好 api 表, 切用户栈, 调用 entry, exit 清理
 *  返回 0=成功, <0=错误
 *  ============================================================ */
struct efs_task_param {
    unsigned int  load_addr;
    unsigned int  bin_size;
    char          name[32];
    char          args[256];   /* 启动参数 (如 "--background"), 空串表示无参数 */
};
/* 清理 EFS 资源的全局表: pid -> 已分配栈, 便于 exit 时释放 */
#define MAX_EFS_TASKS 64
static struct {
    int pid;
    void *user_stack;
    void *bin_pages;      /* 用 pfa 申请的 backing pages (若启用) */
    unsigned long bin_sz;
    unsigned int  load_addr;
} g_efs_task_map[MAX_EFS_TASKS];
static spinlock_t g_efs_map_lock = SPIN_INIT;

static void efs_task_map_register(int pid, void *ustack,
                                   unsigned int load_addr, unsigned long bin_sz) {
    spin_lock(&g_efs_map_lock);
    for (int i = 0; i < MAX_EFS_TASKS; i++) {
        if (g_efs_task_map[i].pid == 0) {
            g_efs_task_map[i].pid = pid;
            g_efs_task_map[i].user_stack = ustack;
            g_efs_task_map[i].bin_pages = 0;
            g_efs_task_map[i].bin_sz = bin_sz;
            g_efs_task_map[i].load_addr = load_addr;
            break;
        }
    }
    spin_unlock(&g_efs_map_lock);
}
static void efs_task_map_unregister(int pid) {
    spin_lock(&g_efs_map_lock);
    for (int i = 0; i < MAX_EFS_TASKS; i++) {
        if (g_efs_task_map[i].pid == pid) {
            if (g_efs_task_map[i].user_stack)
                kheap_free(g_efs_task_map[i].user_stack);
            my_memset(&g_efs_task_map[i], 0, sizeof(g_efs_task_map[i]));
            break;
        }
    }
    spin_unlock(&g_efs_map_lock);
}

/* efs 任务主函数: entry_fn 从 task_trampoline 进入, arg 指向堆上 struct efs_task_param
 * (因为 trampoline 调 entry_fn(arg) 后会自动 task_exit) */
static void efs_task_entry(void *arg) {
    /* [DEBUG] 裸调试: 入口立即打印, 不依赖局部变量/寄存器, 用于确认合成器任务是否进入 efs_task_entry.
     * 崩溃若无此输出, 说明在 trampoline→task_entry 切换或更早 (switch_to) 就炸了. */
    serial_write("EFS[ENTRY]: enter efs_task_entry\n");
    struct efs_task_param *p = (struct efs_task_param*)arg;
    if (!p) { serial_write("EFS[ENTRY]: p=NULL, exit\n"); task_exit(1); return; }
    serial_write("EFS[ENTRY]: p OK, alloc ustack\n");

    /* 给这个 EFS 进程独立的 1MB 用户栈 */
    #define EFS_TASK_STACK 0x100000UL
    void *ustack = kheap_malloc(EFS_TASK_STACK);
    if (!ustack) { serial_write("EFS[ENTRY]: ustack alloc fail\n"); kheap_free(p); task_exit(2); return; }
    serial_write("EFS[ENTRY]: ustack OK, compute top\n");
    /* [同 run_efs_ex 修复] 先 16 字节向下对齐, 再减 8 → 保证 call 前 rsp == 8 mod 16。
     * 之前 ((ustack+SZ-8) & ~0xF) 在栈底末位=0 时得到 0 mod 16, 违反 ABI。*/
    unsigned long ustack_top = (((unsigned long)ustack + EFS_TASK_STACK) & ~0xFUL) - 8UL;

    serial_write("EFS[ENTRY]: register map\n");
    /* 注册到清理表 (用当前任务的 pid — g_current 在进入本函数前已被调度器设置) */
    int mypid = current_get_pid();
    efs_task_map_register(mypid, ustack, p->load_addr, p->bin_size);

    /* 设置全局 EFS 上下文 (异常处理器用) */
    unsigned long krsp, krbp;
    __asm__ volatile("mov %%rsp, %0; mov %%rbp, %1" : "=r"(krsp), "=r"(krbp));
    efs_saved_rsp = krsp;
    efs_saved_rbp = krbp;
    efs_load_addr = p->load_addr;
    efs_bin_size  = p->bin_size;
    efs_stack_ptr = ustack;
    efs_return_point = &&efs_done;
    efs_active = 1;

    /* 刷新 API 表 (第一次或每次 efs 任务启动) */
    efs_setup_api_table();

    /* 复制启动参数到 API 参数缓存 (供 efs_get_args 读取)
     * 优先使用 par->args (如 "--background"), 为空则用程序名 */
    if (p->args[0]) {
        int al = 0; while (p->args[al] && al < (int)sizeof(efs_argbuf)-1 && al < 255) al++;
        for (int i = 0; i < al; i++) efs_argbuf[i] = p->args[i];
        efs_arglen = al;
        efs_argbuf[al] = 0;
    } else {
        int nl = 0; while (p->name[nl] && nl < 31) nl++;
        for (int i = 0; i < nl && i < (int)sizeof(efs_argbuf)-1; i++)
            efs_argbuf[i] = p->name[i];
        efs_arglen = nl;
        efs_argbuf[nl] = 0;
    }

    serial_write("EFS[TASK]: pid="); serial_write_hex(mypid);
    serial_write(" name="); serial_write(p->name);
    serial_write(" @"); serial_write_hex(p->load_addr);
    serial_write(" sz="); serial_write_hex(p->bin_size); serial_write("\n");

    /* [关键修复] 同 run_efs_ex: 切栈后不能访问 rbp 偏移局部变量,
     * 用寄存器传递 entry_fn 和 用户栈顶, rbp = rsp (用户栈) */
    void (*volatile entry)(void) = (void(*)(void))(unsigned long)p->load_addr;
    register unsigned long r_entry __asm__("r12") = (unsigned long)entry;
    register unsigned long r_top   __asm__("r13") = ustack_top;
    efs_active = 1;
    serial_write("EFS[ENTRY]: jumping to entry @");
    serial_write_hex((unsigned long)entry);
    serial_write(" ustack_top=");
    serial_write_hex(ustack_top);
    serial_write(" first_bytes=");
    {
        unsigned char *fb = (unsigned char*)(unsigned long)p->load_addr;
        for (int i = 0; i < 8; i++) { serial_write_hex((unsigned long long)fb[i]); serial_write(" "); }
    }
    serial_write("\n");
    __asm__ volatile(
        "mov %%r13, %%rsp\n"
        "mov %%r13, %%rbp\n"
        "call *%%r12\n"
        : : "r"(r_entry), "r"(r_top) : "memory"
    );
    serial_write("EFS[ENTRY]: entry returned normally\n");
    /* [正常返回] 同 run_efs_ex: 立即恢复任务内核栈与 rbp,
     * 否则 kheap_free(p) 在用户栈上调用 (栈对齐错误 + p 经 rbp 读垃圾) → 三故障。 */
    __asm__ volatile(
        "mov %0, %%rsp\n"
        "mov %1, %%rbp\n"
        : : "r"(efs_saved_rsp), "r"(efs_saved_rbp) : "memory"
    );
    efs_active = 0;
efs_done:
    /* 异常返回路径: rsp 已由 exception_handler 恢复为内核栈
     * (efs_saved_rsp = task 内核栈指针) */
    efs_active = 0;
    kheap_free(p);
    task_exit(0);
}

/* 前向声明: 动态链接 ELF 加载器 (定义在本文件后段) */
static int elf_spawn_dynamic(const char *name);

/* 从磁盘加载并启动一个 .efs 程序作为独立后台任务 (异步, 不等待返回)。
 * 与 run_efs_ex 查找路径一致, 但不抢屏 (窗口系统稍后会接管), 直接创建任务。
 * 返回: >0 = 新任务 PID, <=0 失败 */
static int efs_spawn_async(const char *name) {
    /* 复用 run_efs_ex 的查找 + 加载逻辑的中间步骤:
     * 为了避免复制粘贴, 我们先从 resolve_inode + read_file_range 直接走。 */
    if (!name || !name[0]) return -1;
    int nl = my_strlen(name);
    if (nl > 72) return -1;
    if (nl > 4 && my_strcmp(name + nl - 4, ".efs") == 0) nl -= 4;

    char basename[80], fname[80];
    my_memcpy(basename, name, nl); basename[nl] = 0;
    my_memcpy(fname, name, nl);
    my_memcpy(fname + nl, ".efs", 4); fname[nl + 4] = 0;

    /* 找 inode: /EFMOS -> /Program/<name>/ -> CWD */
    unsigned int efmos_ino = find_in_dir(2, "EFMOS");
    unsigned int ino = 0;
    if (efmos_ino) ino = find_in_dir(efmos_ino, fname);
    if (!ino) {
        char subpath[160] = "/Program/"; int j = 9;
        for(int i=0;i<nl && i<120;i++) subpath[j++]=basename[i];
        subpath[j]=0;
        unsigned int pdir = resolve_inode(subpath, 0);
        if (pdir) ino = find_in_dir(pdir, fname);
    }
    if (!ino) ino = find_in_dir(current_dir_ino, fname);
    if (!ino) {
        serial_write("SM:spawn_err file_not_found name="); serial_write(fname); serial_write("\n");
        return -2;
    }

    unsigned char hd[12];
    if (read_file_content(ino, hd, 12) < 12) {
        serial_write("SM:spawn_err short_header name="); serial_write(fname); serial_write("\n");
        return -3;
    }
    /* [动态链接路由] 若文件头是 ELF 魔数 (0x7F 'E' 'L' 'F'),
     * 说明是 musl/标准 ELF 可执行文件而非 EFS 平二进制 → 交给动态加载器. */
    if (hd[0] == 0x7F && hd[1] == 'E' && hd[2] == 'L' && hd[3] == 'F') {
        return elf_spawn_dynamic(name);
    }
    unsigned char inode_buf[256]; read_inode(ino, inode_buf);
    unsigned int fsize = *(unsigned int*)(inode_buf + 4);
    unsigned int load_addr = *(unsigned int*)(hd + 0);
    unsigned int bin_size = ((unsigned int)hd[8]<<24)|((unsigned int)hd[9]<<16)|
                           ((unsigned int)hd[10]<<8)|(unsigned int)hd[11];
    unsigned int remain = (fsize >= 12) ? fsize - 12 : 0;
    if (load_addr == 0 || load_addr < 0x100000 || load_addr >= 0x10000000) {
        serial_write("SM:spawn_err bad_addr name="); serial_write(basename);
        serial_write(" addr="); serial_write_hex(load_addr); serial_write("\n");
        return -4;
    }
    if (load_addr >= (unsigned int)KHEAP_BASE &&
        load_addr <  (unsigned int)(KHEAP_BASE + KHEAP_SIZE)) {
        serial_write("SM:spawn_err kheap_conflict name="); serial_write(basename); serial_write("\n");
        return -4;
    }
    if (bin_size == 0 || bin_size > remain) bin_size = remain;
    if (bin_size == 0) { serial_write("SM:spawn_err zero_size name="); serial_write(basename); serial_write("\n"); return -5; }

    /* 流式加载 */
    unsigned int loaded = read_file_range(ino, (unsigned char*)(unsigned long)load_addr, 12, bin_size);
    if (loaded < bin_size) { serial_write("SM:spawn_err short_read name="); serial_write(basename); serial_write("\n"); return -6; }
    /* [关键修复] 清零 .bss (同 run_efs_ex): objcopy 不含 .bss, 不清零则全局变量为垃圾值。 */
    {
        unsigned char *bss_start = (unsigned char*)(unsigned long)(load_addr + bin_size);
        unsigned char *bss_end   = (unsigned char*)(unsigned long)(load_addr + 0x100000UL);
        for (unsigned char *p = bss_start; p < bss_end; p++) *p = 0;
    }

    /* 构造 efs_task_param (堆分配, 任务进入后 free) */
    struct efs_task_param *par = kheap_malloc(sizeof(*par));
    if (!par) { serial_write("SM:spawn_err kheap_oom name="); serial_write(basename); serial_write("\n"); return -7; }
    my_memset(par, 0, sizeof(*par));
    par->load_addr = load_addr;
    par->bin_size  = bin_size;
    for (int i = 0; i < nl && i < 31; i++) par->name[i] = basename[i];
    /* 捕获当前 efs_argbuf 中的启动参数 (由 efs_spawn_async_with_args 或 shell 设置) */
    {
        extern char efs_argbuf[1024];
        extern int  efs_arglen;
        int al = efs_arglen;
        if (al > 255) al = 255;
        for (int i = 0; i < al; i++) par->args[i] = efs_argbuf[i];
        par->args[al] = 0;
    }

    /* 创建任务 → 入 runqueue, 立即 TS_READY。下次 sched_yield() 会调度。 */
    struct task_struct *t = task_create(basename, efs_task_entry, par, -1);
    if (!t) { kheap_free(par); serial_write("SM:spawn_err task_create_fail name="); serial_write(basename); serial_write("\n"); return -8; }
    int newpid = task_ptr_get_pid(t);
    serial_write("SM:spawn_ok pid="); serial_write_hex((unsigned long long)newpid);
    serial_write(" name="); serial_write(basename); serial_write("\n");

    /* [WM 模式] 如果窗口管理器已启用, 给新任务创建一个窗口,
     *  大小默认 60 列 × 20 行 (ASCII), 尺寸随字体动态计算。
     * 【例外】 efmcompositor 和 efmAether 是全屏合成器/桌面, 不创建窗口 —
     *   它们直接渲染整个屏幕, 若给它创建窗口, blit_to_window 会被重定向到
     *   该窗口内容区而非全屏。 */
    if (g_wm_enabled && my_strcmp(basename, "efmcompositor") != 0 &&
        my_strcmp(basename, "efmAether") != 0) {
        int ww = 60 * FONT_W + 2 * WM_BORDER + 20;
        int wh = WM_TITLE_H + 20 * FONT_H + 2 * WM_BORDER + 10;
        char ttl[80]; int tn = 0;
        while (basename[tn] && tn < 63) { ttl[tn] = basename[tn]; tn++; }
        ttl[tn] = 0;
        (void)wm_create_window(ttl, newpid, ww, wh, 0);
    }
    return newpid;
}

/* ============================================================
 *  动态链接 ELF 加载器 (内核侧)
 *
 *  目标: 加载 musl libc 编译的 ET_DYN ELF 可执行文件 + 共享库,
 *        替代 glibc 依赖, 为 Mesa 动态库提供运行时.
 *
 *  内存布局 (UEFI 身份映射, 可直接按物理地址访问):
 *    0x20000000 (512MB) 起 → 动态加载区 (共享库 + 可执行映像)
 *    采用 bump allocator 分配, 每个 SO 加载到对齐后的地址.
 *
 *  工作流程:
 *    1. elf_load_so(): 读 ELF 头 → 加载 PT_LOAD 段 → 处理 PT_DYNAMIC 重定位
 *    2. elf_resolve_sym(): 在已加载 SO 列表中按名查找全局符号
 *    3. elf_apply_rela(): 处理 RELA 重定位表
 *       - R_X86_64_RELATIVE: *(base+offset) = base + addend   (PIE 主体)
 *       - R_X86_64_GLOB_DAT / JUMP_SLOT: *(base+offset) = lookup(sym)
 *       - R_X86_64_64: *(base+offset) = lookup(sym) + addend
 *    4. elf_spawn_dynamic(): 加载主程序 + PT_INTERP (ld-musl), 创建 task
 *
 *  支持的 ELF: ET_DYN (PIE 可执行 + 共享库), EM_X86_64, ELFCLASS64.
 * ============================================================ */

/* ---------- ELF 结构 (内核原生类型, 不依赖 efi.h) ---------- */
#define DYN_EI_NIDENT 16
typedef struct {
    unsigned char e_ident[DYN_EI_NIDENT];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned int   e_version;
    unsigned long long e_entry;
    unsigned long long e_phoff;
    unsigned long long e_shoff;
    unsigned int   e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} dyn_Ehdr;

typedef struct {
    unsigned int   p_type;
    unsigned int   p_flags;
    unsigned long long p_offset;
    unsigned long long p_vaddr;
    unsigned long long p_paddr;
    unsigned long long p_filesz;
    unsigned long long p_memsz;
    unsigned long long p_align;
} dyn_Phdr;

typedef struct {
    long long d_tag;
    union { unsigned long long d_val; unsigned long long d_ptr; } d_un;
} dyn_Dyn;

typedef struct {
    unsigned int  st_name;
    unsigned char st_info;
    unsigned char st_other;
    unsigned short st_shndx;
    unsigned long long st_value;
    unsigned long long st_size;
} dyn_Sym;

typedef struct {
    unsigned long long r_offset;
    unsigned long long r_info;
    long long r_addend;
} dyn_Rela;

/* ELF 常量 (局部, 不与 efi elf.h 冲突) */
#define DYN_PT_LOAD     1
#define DYN_PT_DYNAMIC  2
#define DYN_PT_INTERP   3
#define DYN_PT_TLS      7
#define DYN_ET_EXEC     2
#define DYN_ET_DYN      3
#define DYN_EM_X86_64   62
#define DYN_ELFCLASS64  2

#define DYN_DT_NULL         0
#define DYN_DT_NEEDED       1
#define DYN_DT_PLTRELSZ     2
#define DYN_DT_PLTGOT       3
#define DYN_DT_STRTAB       5
#define DYN_DT_SYMTAB       6
#define DYN_DT_RELA         7
#define DYN_DT_RELASZ       8
#define DYN_DT_RELAENT      9
#define DYN_DT_STRSZ        10
#define DYN_DT_SYMENT       11
#define DYN_DT_INIT         12
#define DYN_DT_FINI         13
#define DYN_DT_SONAME       14
#define DYN_DT_RPATH        15
#define DYN_DT_SYMBOLIC     16
#define DYN_DT_REL          17
#define DYN_DT_RELSZ        18
#define DYN_DT_RELENT       19
#define DYN_DT_PLTREL       20
#define DYN_DT_JMPREL       23
#define DYN_DT_INIT_ARRAY   25
#define DYN_DT_FINI_ARRAY   26
#define DYN_DT_INIT_ARRAYSZ 27
#define DYN_DT_FINI_ARRAYSZ 28

#define DYN_R_SYM(i)  ((unsigned long)(i) >> 32)
#define DYN_R_TYPE(i) ((unsigned long)(i) & 0xffffffffL)

#define DYN_R_X86_64_NONE       0
#define DYN_R_X86_64_64         1
#define DYN_R_X86_64_PC32       2
#define DYN_R_X86_64_PLT32      4
#define DYN_R_X86_64_GLOB_DAT   6
#define DYN_R_X86_64_JUMP_SLOT  7
#define DYN_R_X86_64_RELATIVE   8
#define DYN_R_X86_64_32         10
#define DYN_R_X86_64_32S        11
#define DYN_R_X86_64_DTPMOD64   16
#define DYN_R_X86_64_DTPOFF64   17
#define DYN_R_X86_64_TPOFF64    18

#define DYN_ST_BIND(i) ((i) >> 4)
#define DYN_STB_GLOBAL 1
#define DYN_STB_WEAK   2

/* ---------- 动态加载区 bump allocator ----------
 * 0x20000000 (512MB) ~ 0x40000000 (1GB) = 512MB 用于共享库 + 动态可执行映像.
 * 每个 SO 按 4KB 页对齐加载. */
#define DYN_REGION_BASE  0x20000000UL
#define DYN_REGION_END   0x40000000UL
#define DYN_PAGE_SIZE    0x1000UL
static unsigned long g_dyn_brk = DYN_REGION_BASE;

/* 分配 size 字节 (页对齐), 返回基址. 失败返回 0. */
static unsigned long dyn_bump_alloc(unsigned long long size) {
    unsigned long aligned = (g_dyn_brk + DYN_PAGE_SIZE - 1) & ~(DYN_PAGE_SIZE - 1);
    if (aligned + size > DYN_REGION_END) {
        serial_write("DYN: region exhausted\n");
        return 0;
    }
    g_dyn_brk = aligned + size;
    return aligned;
}

/* ---------- 已加载共享对象表 ---------- */
#define DYN_MAX_SO 16
typedef struct {
    int used;
    char name[64];              /* SONAME 或文件名 */
    unsigned long base;         /* 加载基址 */
    unsigned long long entry;   /* 入口点 (仅主程序有意义) */
    /* 程序头信息 (供 auxv AT_PHDR/AT_PHNUM/AT_PHENT 使用) */
    unsigned long long e_phoff; /* ELF 头中 e_phoff */
    unsigned short e_phnum;     /* ELF 头中 e_phnum */
    unsigned short e_phentsize; /* ELF 头中 e_phentsize */
    /* PT_TLS 静态 TLS 段信息 */
    unsigned long long tls_vaddr;   /* TLS 段虚拟地址 (相对基址) */
    unsigned long long tls_filesz;  /* TLS 初始化数据大小 (.tdata) */
    unsigned long long tls_memsz;   /* TLS 总大小 (.tdata + .tbss zero fill) */
    unsigned long long tls_align;   /* TLS 对齐要求 */
    /* 动态段解析结果 (地址均已加上 base) */
    char *strtab;               /* .dynstr */
    dyn_Sym *symtab;            /* .dynsym */
    unsigned long long syment;  /* 符号条目大小 */
    dyn_Rela *rela;             /* DT_RELA 表 */
    unsigned long long relasz;  /* DT_RELASZ */
    dyn_Rela *jmprel;           /* DT_JMPREL (PLT) 表 */
    unsigned long long pltrelsz;
    dyn_Dyn *dyn;               /* 动态段 (用于 DT_NEEDED 扫描) */
    unsigned long long strsz;   /* strtab 大小 */
    int is_exec;                /* 1=主可执行, 0=共享库 */
    int init_done;              /* DT_INIT 是否已调用 (防止重复) */
} dyn_so_t;
static dyn_so_t g_dyn_so[DYN_MAX_SO];
static spinlock_t g_dyn_lock = SPIN_INIT;

/* 在 g_dyn_so 中找空槽并注册 */
static dyn_so_t *dyn_so_register(void) {
    spin_lock(&g_dyn_lock);
    dyn_so_t *so = 0;
    for (int i = 0; i < DYN_MAX_SO; i++) {
        if (!g_dyn_so[i].used) { so = &g_dyn_so[i]; my_memset(so, 0, sizeof(*so)); so->used = 1; break; }
    }
    spin_unlock(&g_dyn_lock);
    return so;
}

/* 按名查找已加载 SO */
static dyn_so_t *dyn_so_find(const char *name) {
    for (int i = 0; i < DYN_MAX_SO; i++) {
        if (g_dyn_so[i].used && my_strcmp(g_dyn_so[i].name, name) == 0) return &g_dyn_so[i];
    }
    return 0;
}

/* ---------- 符号查找 ----------
 * 在所有已加载 SO 的 .dynsym 中按名查找全局/弱符号.
 * 返回符号绝对地址 (base + st_value), 未找到返回 0. */
static unsigned long long dyn_resolve_sym(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < DYN_MAX_SO; i++) {
        dyn_so_t *so = &g_dyn_so[i];
        if (!so->used || !so->symtab || !so->strtab) continue;
        /* 估算符号表最大条目数: 若 DT_SYMENT/DT_STRSZ 可用, 保守估计.
         * 通常 .dynsym 大小 ≈ 距离下一 section / syment, 最多 16384 条足够. */
        unsigned long max_sym = 16384;
        if (so->syment >= 24 && so->strsz > 0) {
            /* 粗上限: 不会比 strsz/syment 更多 (每条符号至少对应一个字符名) */
            unsigned long lim = so->strsz / 2;
            if (lim < max_sym) max_sym = lim + 256;
        }
        unsigned long undef_run = 0;
        for (unsigned long s = 0; s < max_sym; s++) {
            dyn_Sym *sym = &so->symtab[s];
            /* 终止条件: 连续遇到空条目 (st_name=0, st_value=0, st_shndx=0)
             * 超过 8 个 → 认为到了表尾, 避免扫到内存垃圾 */
            if (sym->st_name == 0 && sym->st_value == 0 && sym->st_shndx == 0 && sym->st_size == 0) {
                undef_run++;
                if (undef_run > 8) break;
                continue;
            }
            undef_run = 0;
            /* 越界保护: st_name 不能超出 strtab */
            if (so->strsz > 0 && sym->st_name >= so->strsz) continue;
            unsigned int bind = DYN_ST_BIND(sym->st_info);
            if (bind != DYN_STB_GLOBAL && bind != DYN_STB_WEAK) continue;
            const char *sname = so->strtab + sym->st_name;
            if (!sname || !sname[0]) continue;
            if (my_strcmp(sname, name) == 0) {
                /* 注意: 未定义符号 (st_shndx==0 且 value==0) 不应返回 base+0 误命中 */
                if (sym->st_shndx == 0 && sym->st_value == 0) continue;
                return so->base + sym->st_value;
            }
        }
    }
    return 0;
}

/* ---- WRMSR / RDMSR helper (提前定义, 给 dyn_tls_base 等使用) ---- */
static inline void wrmsr(unsigned int msr, unsigned long long val) {
    unsigned int lo = (unsigned int)(val & 0xFFFFFFFF);
    unsigned int hi = (unsigned int)(val >> 32);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}
static inline unsigned long long rdmsr(unsigned int msr) {
    unsigned int lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((unsigned long long)hi << 32) | lo;
}
#define MSR_STAR     0xC0000081
#define MSR_LSTAR    0xC0000082
#define MSR_CSTAR    0xC0000083
#define MSR_SFMASK   0xC0000084
#define MSR_FS_BASE  0xC0000100
#define MSR_GS_BASE  0xC0000101
#define MSR_IA32_EFER 0xC0000080

/* x86-64 TLS 辅助: 获取当前 FS.base 指向的 TCB, TCB[-1] 是 DTV 指针.
 * 简化: 只有单线程, 返回线程 0 的 TLS block 偏移. 由于内核入口设置好了
 * FS.base 指向 &tcb->self_ptr, 我们只需读取 MSR_FS_BASE 得到 TCB
 * 起点地址 (&tcb->self_ptr - offsetof(tcb,self_ptr)). */
static inline unsigned long dyn_tls_base(void) {
    /* FS.base = &tcb->self_ptr = (unsigned long)tcb + offsetof(musl_tcb_t,self_ptr).
     * tcb start (dtv 之前) = FS.base - 0x30 (pad0*8=32 + dtv=8 + mcanary=8 + pad1*8=16 → self_ptr=64?? 实际 self_ptr 位置:
     * struct { pad0[4]=32, dtv=8, mthread_canary=8, pad1[2]=16, self_ptr=8 } → &self_ptr = &tcb + 64.
     * tls block start 通常 = &tcb. 线程本地变量的 static_tls 偏移是相对于 TPO (TLS Pointer Offset)
     * (dtv[0].val + TLS offset - TLS_TCB_SIZE).
     * musl x86-64 约定: FS:0 (self_ptr) = TCB 末尾 (= &tcb->self_ptr),
     *   TPO = -sizeof(tcb) (即 tcb start), 且 dtv[0].val = TPO.
     * 简化版: 返回 (unsigned long)tcb_start 作为 TLS 基址, R_DTPOFF 加上去就是最终变量地址. */
    unsigned long fs = (unsigned long)rdmsr(MSR_FS_BASE);
    if (!fs) return 0;
    return fs - 64;  /* &tcb = &self_ptr - 64 bytes */
}

/* ---------- 处理 RELA 重定位表 ----------
 * base = 当前 SO 的加载基址. */
static void dyn_apply_rela(dyn_so_t *so, dyn_Rela *rela, unsigned long long relasz) {
    if (!rela || relasz == 0) return;
    unsigned long long n = relasz / sizeof(dyn_Rela);
    for (unsigned long long i = 0; i < n; i++) {
        dyn_Rela *r = &rela[i];
        unsigned long type = DYN_R_TYPE(r->r_info);
        unsigned long symidx = DYN_R_SYM(r->r_info);
        unsigned long place = so->base + r->r_offset;
        switch (type) {
        case DYN_R_X86_64_NONE:
            break;
        case DYN_R_X86_64_RELATIVE:
            /* B + A: 基址 + 加数 (PIE 主体, 无需符号) */
            *(unsigned long long*)place = so->base + (unsigned long long)r->r_addend;
            break;
        case DYN_R_X86_64_64:
        case DYN_R_X86_64_GLOB_DAT:
        case DYN_R_X86_64_JUMP_SLOT: {
            unsigned long long S = 0;
            if (symidx > 0 && so->symtab && so->strtab) {
                dyn_Sym *sym = &so->symtab[symidx];
                const char *sname = so->strtab + sym->st_name;
                if (sname && sname[0]) {
                    S = dyn_resolve_sym(sname);
                    if (!S && sym->st_value != 0 && sym->st_shndx != 0)
                        S = so->base + sym->st_value;
                }
            }
            *(unsigned long long*)place = S + (unsigned long long)r->r_addend;
            break;
        }
        case DYN_R_X86_64_PC32:
        case DYN_R_X86_64_PLT32: {
            /* S + A - P: 32-bit signed relative; 用于代码内部 call/jmp, 通常在 PIE 中 0. */
            unsigned long long S = 0;
            if (symidx > 0 && so->symtab && so->strtab) {
                dyn_Sym *sym = &so->symtab[symidx];
                const char *sname = so->strtab + sym->st_name;
                if (sname && sname[0]) {
                    S = dyn_resolve_sym(sname);
                    if (!S && sym->st_value != 0 && sym->st_shndx != 0)
                        S = so->base + sym->st_value;
                }
            }
            long long val = (long long)S + r->r_addend - (long long)place;
            *(int*)place = (int)val;
            break;
        }
        case DYN_R_X86_64_32: {
            /* S + A zero-extend 32-bit */
            unsigned long long S = 0;
            if (symidx > 0 && so->symtab && so->strtab) {
                dyn_Sym *sym = &so->symtab[symidx];
                const char *sname = so->strtab + sym->st_name;
                if (sname && sname[0]) {
                    S = dyn_resolve_sym(sname);
                    if (!S && sym->st_value != 0 && sym->st_shndx != 0)
                        S = so->base + sym->st_value;
                }
            }
            unsigned int v32 = (unsigned int)(S + (unsigned long long)r->r_addend);
            *(unsigned int*)place = v32;
            break;
        }
        case DYN_R_X86_64_32S: {
            /* S + A sign-extend 32-bit */
            unsigned long long S = 0;
            if (symidx > 0 && so->symtab && so->strtab) {
                dyn_Sym *sym = &so->symtab[symidx];
                const char *sname = so->strtab + sym->st_name;
                if (sname && sname[0]) {
                    S = dyn_resolve_sym(sname);
                    if (!S && sym->st_value != 0 && sym->st_shndx != 0)
                        S = so->base + sym->st_value;
                }
            }
            int v32s = (int)((long long)S + r->r_addend);
            *(int*)place = v32s;
            break;
        }
        case DYN_R_X86_64_DTPMOD64: {
            /* 写当前模块号 (单线程/单模块 = 1) */
            *(unsigned long long*)place = 1ULL;
            break;
        }
        case DYN_R_X86_64_DTPOFF64: {
            /* DTPOFF = 线程本地偏移 = sym st_value + addend.
             * musl 中对 static TLS: TP 指向 dtv[0].val = TPO = tls_start.
             * 所以 DTPOFF = st_value + addend (相对 tls_start 的偏移). */
            unsigned long long symval = 0;
            if (symidx > 0 && so->symtab) symval = so->symtab[symidx].st_value;
            *(unsigned long long*)place = symval + (unsigned long long)r->r_addend;
            break;
        }
        case DYN_R_X86_64_TPOFF64: {
            /* TPOFF64 = st_value + A - TLS_TCB_SIZE.
             * 在 x86-64 musl 中: TP (FS) 指向 TCB 尾部 (self_ptr),
             *   TPO = TP - TCB_SIZE = dtv[0].val.
             * 线程本地变量地址 = TP - TCB_SIZE + @tpoff (即 TPO + @tpoff).
             * 简化: 写成 sym st_value + addend, 运行时 TP 寻址后自然对.
             * 若未启用静态 TLS, 退化为 DTPOFF 语义. */
            unsigned long long symval = 0;
            if (symidx > 0 && so->symtab) symval = so->symtab[symidx].st_value;
            *(unsigned long long*)place = symval + (unsigned long long)r->r_addend;
            break;
        }
        default:
            /* 忽略未支持的重定位类型 (musl 内部 TLSDESC 等复杂类型由 ld-musl 用户态处理,
             * 内核加载器遇到则跳过, 不 crash) */
            break;
        }
    }
}

/* ---------- 加载单个 ET_DYN ELF 文件 ----------
 * ino: 文件 inode. name: 用于注册的名称. is_exec: 是否主程序.
 * 返回 dyn_so_t* (已注册并完成重定位), 失败返回 NULL. */
static dyn_so_t *elf_load_so(unsigned int ino, const char *name, int is_exec) {
    /* 读 ELF 头 (64 字节足够 Ehdr) */
    unsigned char ehdr_buf[128];
    if (read_file_range(ino, ehdr_buf, 0, sizeof(dyn_Ehdr)) < sizeof(dyn_Ehdr)) {
        serial_write("DYN: short read for ehdr\n"); return 0;
    }
    dyn_Ehdr *eh = (dyn_Ehdr*)ehdr_buf;
    /* 验证 ELF 魔数 */
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        serial_write("DYN: not ELF\n"); return 0;
    }
    if (eh->e_ident[4] != DYN_ELFCLASS64 || eh->e_machine != DYN_EM_X86_64) {
        serial_write("DYN: not x86_64 ELF64\n"); return 0;
    }
    if (eh->e_type != DYN_ET_DYN && eh->e_type != DYN_ET_EXEC) {
        serial_write("DYN: not ET_DYN/ET_EXEC\n"); return 0;
    }

    /* 读所有程序头 */
    unsigned short phnum = eh->e_phnum;
    unsigned short phentsize = eh->e_phentsize;
    unsigned long long phsize = (unsigned long long)phnum * phentsize;
    /* 程序头表通常很小 (<4KB), 用 kheap 分配 */
    unsigned char *phbuf = (unsigned char*)kheap_malloc((unsigned long)phsize + 16);
    if (!phbuf) { serial_write("DYN: phdr alloc fail\n"); return 0; }
    if (read_file_range(ino, phbuf, eh->e_phoff, (unsigned int)phsize) < phsize) {
        serial_write("DYN: phdr short read\n"); kheap_free(phbuf); return 0;
    }

    /* 第一遍: 计算所需内存范围 (对 ET_DYN, vaddr 从 0 起; 对 ET_EXEC, 用固定地址) */
    unsigned long long min_vaddr = ~0ULL;
    unsigned long long max_vaddr = 0;
    dyn_Phdr *phs = (dyn_Phdr*)phbuf;
    for (int i = 0; i < phnum; i++) {
        dyn_Phdr *p = (dyn_Phdr*)(phbuf + i * phentsize);
        if (p->p_type != DYN_PT_LOAD) continue;
        if (p->p_vaddr < min_vaddr) min_vaddr = p->p_vaddr;
        if (p->p_vaddr + p->p_memsz > max_vaddr) max_vaddr = p->p_vaddr + p->p_memsz;
    }
    if (max_vaddr == 0) { serial_write("DYN: no PT_LOAD\n"); kheap_free(phbuf); return 0; }

    /* 分配基址 */
    unsigned long base;
    if (eh->e_type == DYN_ET_EXEC) {
        base = (unsigned long)min_vaddr;   /* ET_EXEC: 固定地址加载 */
    } else {
        /* ET_DYN: bump 分配, 对齐到页 */
        unsigned long long span = max_vaddr - min_vaddr;
        base = dyn_bump_alloc(span);
        if (!base) { kheap_free(phbuf); return 0; }
        base -= (unsigned long)min_vaddr;  /* 让 p_vaddr 映射到 base+p_vaddr */
    }

    serial_write("DYN: load "); serial_write(name);
    serial_write(" base="); serial_write_hex(base);
    serial_write(" span="); serial_write_hex(max_vaddr - min_vaddr);
    serial_write("\n");

    /* 第二遍: 加载 PT_LOAD 段 */
    for (int i = 0; i < phnum; i++) {
        dyn_Phdr *p = (dyn_Phdr*)(phbuf + i * phentsize);
        if (p->p_type != DYN_PT_LOAD) continue;
        unsigned char *dest = (unsigned char*)(base + p->p_vaddr);
        /* 清零整个 memsz (覆盖 bss) */
        my_memset(dest, 0, (unsigned int)p->p_memsz);
        /* 读 filesz 字节的文件内容 */
        if (p->p_filesz > 0) {
            unsigned int got = read_file_range(ino, dest, (unsigned int)p->p_offset,
                                               (unsigned int)p->p_filesz);
            (void)got;
        }
    }

    /* 注册 SO */
    dyn_so_t *so = dyn_so_register();
    if (!so) { serial_write("DYN: SO table full\n"); kheap_free(phbuf); return 0; }
    so->base = base;
    so->entry = base + eh->e_entry;
    so->is_exec = is_exec;
    so->e_phoff = eh->e_phoff;
    so->e_phnum = eh->e_phnum;
    so->e_phentsize = eh->e_phentsize;
    {
        int nl = 0; while (name && name[nl] && nl < 63) { so->name[nl] = name[nl]; nl++; }
        so->name[nl] = 0;
    }

    /* 解析 PT_DYNAMIC / PT_TLS: 找动态段, 提取 strtab/symtab/rela/jmprel; 保存 TLS 段信息 */
    dyn_Dyn *dyn = 0;
    for (int i = 0; i < phnum; i++) {
        dyn_Phdr *p = (dyn_Phdr*)(phbuf + i * phentsize);
        if (p->p_type == DYN_PT_DYNAMIC) { dyn = (dyn_Dyn*)(base + p->p_vaddr); }
        else if (p->p_type == DYN_PT_TLS) {
            so->tls_vaddr = p->p_vaddr;
            so->tls_filesz = p->p_filesz;
            so->tls_memsz = p->p_memsz;
            so->tls_align = p->p_align ? p->p_align : 16;
        }
    }
    if (dyn) {
        unsigned long long strtab_addr = 0, symtab_addr = 0, syment = 24;
        unsigned long long rela_addr = 0, relasz = 0;
        unsigned long long jmprel_addr = 0, pltrelsz = 0;
        unsigned long long strsz = 0;
        for (dyn_Dyn *d = dyn; d->d_tag != DYN_DT_NULL; d++) {
            switch (d->d_tag) {
            case DYN_DT_STRTAB:       strtab_addr  = d->d_un.d_ptr; break;
            case DYN_DT_SYMTAB:       symtab_addr  = d->d_un.d_ptr; break;
            case DYN_DT_SYMENT:       syment       = d->d_un.d_val; break;
            case DYN_DT_RELA:         rela_addr    = d->d_un.d_ptr; break;
            case DYN_DT_RELASZ:       relasz       = d->d_un.d_val; break;
            case DYN_DT_RELAENT:      /* relaent = d->d_un.d_val; 默认 24 */ break;
            case DYN_DT_JMPREL:       jmprel_addr  = d->d_un.d_ptr; break;
            case DYN_DT_PLTRELSZ:     pltrelsz     = d->d_un.d_val; break;
            case DYN_DT_PLTREL:       /* pltrel = type (DT_RELA=7) 验证用 */ break;
            case DYN_DT_STRSZ:        strsz        = d->d_un.d_val; break;
            default: break;
            }
        }
        so->dyn = dyn;
        so->strsz = strsz;
        /* d_ptr 是文件内 vaddr, 转为加载后绝对地址 (加 base).
         * 注意: 对 ET_DYN, vaddr 是相对的; 对 ET_EXEC, vaddr 已是绝对.
         * 统一处理: 若地址 < base, 加 base (相对); 否则原样. */
        #define DYN_FIX(addr) ((unsigned long long)(addr) < base ? (base + (unsigned long long)(addr)) : (unsigned long long)(addr))
        if (strtab_addr) so->strtab = (char*)DYN_FIX(strtab_addr);
        if (symtab_addr) so->symtab = (dyn_Sym*)DYN_FIX(symtab_addr);
        so->syment = syment;
        if (rela_addr)   so->rela    = (dyn_Rela*)DYN_FIX(rela_addr);
        so->relasz    = relasz;
        if (jmprel_addr) so->jmprel  = (dyn_Rela*)DYN_FIX(jmprel_addr);
        so->pltrelsz  = pltrelsz;
        #undef DYN_FIX
    }

    kheap_free(phbuf);

    /* 应用重定位 (RELA 主体先于 JMPREL, 确保符号表就绪) */
    dyn_apply_rela(so, so->rela, so->relasz);
    dyn_apply_rela(so, so->jmprel, so->pltrelsz);

    /* 注意: DT_INIT/DT_INIT_ARRAY 不在此调用, 移到 dyn_call_init 中,
     * 在所有库加载 + dyn_relocate_all 完成后才调用, 确保跨库符号已解析. */
    return so;
}

/* ---------- 调用 SO 的 DT_INIT / DT_INIT_ARRAY ----------
 * 必须在所有库加载 + 重定位完成后调用. */
static void dyn_call_init(dyn_so_t *so) {
    if (!so || !so->dyn || so->init_done) return;
    so->init_done = 1;
    unsigned long long base = so->base;
    dyn_Dyn *dyn = so->dyn;
    unsigned long long init_array_addr = 0;
    unsigned long long init_arraysz = 0;
    for (dyn_Dyn *d = dyn; d->d_tag != DYN_DT_NULL; d++) {
        if (d->d_tag == DYN_DT_INIT && d->d_un.d_ptr) {
            unsigned long long faddr = d->d_un.d_ptr;
            if (faddr < base) faddr += base;
            void (*init_fn)(void) = (void(*)(void))faddr;
            init_fn();
        } else if (d->d_tag == DYN_DT_INIT_ARRAY) {
            init_array_addr = d->d_un.d_ptr;
            if (init_array_addr && init_array_addr < base) init_array_addr += base;
        } else if (d->d_tag == DYN_DT_INIT_ARRAYSZ) {
            init_arraysz = d->d_un.d_val;
        }
    }
    if (init_array_addr && init_arraysz) {
        unsigned long n = init_arraysz / sizeof(unsigned long long);
        unsigned long long *arr = (unsigned long long*)init_array_addr;
        for (unsigned long i = 0; i < n; i++) {
            if (!arr[i]) continue;
            void (*fn)(void) = (void(*)(void))arr[i];
            fn();
        }
    }
}

/* ---------- 加载依赖库 (DT_NEEDED) ----------
 * 扫描 so 的 DT_NEEDED, 从 lib_dir_ino 目录加载缺失的共享库.
 * 递归处理 (被依赖库自身也可能有 DT_NEEDED). */
static void elf_load_needed(dyn_so_t *so, unsigned int lib_dir_ino) {
    if (!so || !so->dyn || !so->strtab) return;
    for (dyn_Dyn *d = so->dyn; d->d_tag != DYN_DT_NULL; d++) {
        if (d->d_tag != DYN_DT_NEEDED) continue;
        const char *depname = so->strtab + d->d_un.d_val;
        if (!depname || !depname[0]) continue;
        /* 已加载则跳过 */
        if (dyn_so_find(depname)) continue;
        if (!lib_dir_ino) continue;
        unsigned int dep_ino = find_in_dir(lib_dir_ino, depname);
        if (!dep_ino) {
            serial_write("DYN: missing dep "); serial_write(depname); serial_write("\n");
            continue;
        }
        dyn_so_t *dep = elf_load_so(dep_ino, depname, 0);
        if (dep) {
            /* 递归加载被依赖库的依赖 */
            elf_load_needed(dep, lib_dir_ino);
        }
    }
}

/* ---------- 二次重定位 ----------
 * 加载所有依赖后, 重新应用所有 SO 的重定位, 确保跨库符号引用被解析.
 * (首次加载时被引用库可能尚未加载, 导致 GLOB_DAT/JUMP_SLOT 填 0) */
static void dyn_relocate_all(void) {
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < DYN_MAX_SO; i++) {
            dyn_so_t *so = &g_dyn_so[i];
            if (!so->used) continue;
            dyn_apply_rela(so, so->rela, so->relasz);
            dyn_apply_rela(so, so->jmprel, so->pltrelsz);
        }
    }
}

/* ============================================================
 *  musl syscall shim 层
 * ============================================================
 * musl libc 使用 Linux x86_64 syscall ABI:
 *   系统调用号 in rax, 参数 rdi/rsi/rdx/r10/r8/r9, 返回值 in rax.
 *   触发方式: 支持两种入口
 *     1) int $0x80  (兼容 Linux int80 ABI)
 *     2) syscall 指令 (通过 LSTAR MSR, x86_64 标准 ABI)
 *
 * 实现:
 *   - ASM 包装器保存所有 caller-save + callee-save 寄存器,
 *     调用 C dispatch 函数, 返回值写回 rax, 然后 iret/sysret.
 *   - C 分发器根据 rax 号数翻译到 EFMOS 内核 API.
 *   - 提供 arch_prctl (sys 158) 以设置 x86-64 FS.base (TLS).
 * ============================================================ */
/* ---- 每任务 TLS block ---- */
typedef struct {
    /* x86-64 musl TCB layout: FS 指向 TCB 末尾 (self_ptr).
     * TCB 头在 TCB start:
     *   TCB[-4*8] = self_ptr (指向 TCB 自身, 即 FS:0 = &self_ptr = &tcb+0x70)
     * 我们分配 tls_block, 让 FS.base = (unsigned long)&tcb->self_ptr */
    unsigned long pad0[4];
    unsigned long dtv;        /* dynamic thread vector (libc internal) */
    unsigned long mthread_canary;
    unsigned long pad1[2];
    unsigned long self_ptr;   /* ← FS.base 指向这里 */
} musl_tcb_t;

/* ---- Linux syscall 号数 (x86_64) ---- */
#define LINUX_SYS_read      0
#define LINUX_SYS_write     1
#define LINUX_SYS_close     3
#define LINUX_SYS_fstat     5
#define LINUX_SYS_lseek     8
#define LINUX_SYS_mmap      9
#define LINUX_SYS_mprotect 10
#define LINUX_SYS_munmap   11
#define LINUX_SYS_brk      12
#define LINUX_SYS_ioctl    16
#define LINUX_SYS_sched_yield 24
#define LINUX_SYS_nanosleep  35
#define LINUX_SYS_getpid    39
#define LINUX_SYS_getuid    102
#define LINUX_SYS_geteuid   101
#define LINUX_SYS_getgid    104
#define LINUX_SYS_getegid   103
#define LINUX_SYS_exit      60
#define LINUX_SYS_arch_prctl 158
#define LINUX_SYS_set_tid_address 218
#define LINUX_SYS_futex     202
#define LINUX_SYS_uname     63
#define LINUX_SYS_prctl     157
#define LINUX_SYS_clock_gettime 228
#define LINUX_SYS_getrandom 318
#define LINUX_SYS_rt_sigaction 13
#define LINUX_SYS_rt_sigprocmask 14
#define LINUX_SYS_sigaltstack 131
#define LINUX_SYS_tgkill    234
#define LINUX_SYS_openat    257
#define LINUX_SYS_stat      4
#define LINUX_SYS_lstat     6
#define LINUX_SYS_madvise   28
#define LINUX_SYS_mremap    25
#define LINUX_SYS_writev    20
#define LINUX_SYS_access    21
#define LINUX_SYS_dup       32
#define LINUX_SYS_dup2      33
#define LINUX_SYS_fcntl     72
#define LINUX_SYS_readlink  89
#define LINUX_SYS_getdents64 217
#define LINUX_SYS_set_robust_list 273
#define LINUX_SYS_exit_group 231
#define LINUX_SYS_rseq      334
#define LINUX_SYS_readlinkat 267
#define LINUX_SYS_newfstatat 262
#define LINUX_SYS_open      2

/* arch_prctl 子码 */
#define ARCH_SET_GS    0x1001
#define ARCH_SET_FS    0x1002
#define ARCH_GET_FS    0x1003
#define ARCH_GET_GS    0x1004

/* ---- mmap prot/flags ---- */
#define LX_PROT_NONE    0x0
#define LX_PROT_READ    0x1
#define LX_PROT_WRITE   0x2
#define LX_PROT_EXEC    0x4
#define LX_MAP_PRIVATE  0x02
#define LX_MAP_ANONYMOUS 0x20
#define LX_MAP_FIXED    0x10
#define LX_MAP_FIXED_NOREPLACE 0x100000

/* ---- 每进程的动态区域 mmap brk ---- */
typedef struct {
    unsigned long mmap_brk;  /* 匿名 mmap bump 指针, 位于动态区之上 */
    unsigned long brk_end;   /* 数据段 break (heap) */
    void *ustack_base;
    unsigned long ustack_sz;
    musl_tcb_t *tcb;        /* 已分配的 TLS block (供 free) */
    void *shim_fdbuf;       /* EFMOS file_read/write 的临时 4KB 缓冲 */
    /* 简化 fd 表: fd -> inode + offset */
    int  fd_inode[16];
    long fd_off[16];
} musl_proc_t;
#define MPROC_FD_MAX 16
static musl_proc_t g_mproc[64];   /* 按 PID 索引 (PID<=63) */
static unsigned long g_mmap_brk_next = DYN_REGION_END;   /* 0x40000000 起往上做匿名 mmap */

static inline musl_proc_t *mproc_get(int pid) {
    if (pid < 0 || pid >= 64) return 0;
    return &g_mproc[pid];
}
static int mproc_fd_alloc(int pid, unsigned int inode) {
    musl_proc_t *mp = mproc_get(pid); if (!mp) return -1;
    for (int i = 0; i < MPROC_FD_MAX; i++) {
        if (mp->fd_inode[i] == 0) { mp->fd_inode[i] = (int)inode; mp->fd_off[i] = 0; return i; }
    }
    return -1;
}
static inline void mproc_fd_close(int pid, int fd) {
    musl_proc_t *mp = mproc_get(pid);
    if (mp && fd >= 0 && fd < MPROC_FD_MAX) mp->fd_inode[fd] = 0;
}

/* ---- 文件路径解析 (把 /A/B/C 路径解析到 inode) ---- */
static unsigned int mproc_resolve(const char *path) {
    if (!path || !path[0]) return 0;
    return resolve_inode((char*)path, 0);
}

/* ---- C 级 syscall dispatch ----
 * 返回 syscall 结果 (rax). -errno 作为负数返回. */
/* 可选: lapic "屏蔽中断下" 轮询式 ms 级 sleep. 若未实现则不轮询. */
static void (*g_apic_timer_masked_poll_ms)(unsigned long ms) = 0;
static long long musl_sys_dispatch(long long nr,
                                    long long a1, long long a2, long long a3,
                                    long long a4, long long a5, long long a6) {
    int pid = current_get_pid();
    musl_proc_t *mp = mproc_get(pid);
    (void)a5; (void)a6;
    switch (nr) {
    case LINUX_SYS_exit:
    case LINUX_SYS_exit_group:
        efs_active = 0; task_exit((int)a1); return 0;

    case LINUX_SYS_getpid:  return pid;
    case LINUX_SYS_getuid:
    case LINUX_SYS_geteuid:
    case LINUX_SYS_getgid:
    case LINUX_SYS_getegid: return 0;   /* root 身份 */

    case LINUX_SYS_arch_prctl: {
        int code = (int)a1; unsigned long addr = (unsigned long)a2;
        if (code == ARCH_SET_FS) {
            wrmsr(MSR_FS_BASE, (unsigned long long)addr);
            return 0;
        }
        if (code == ARCH_GET_FS) return (long long)rdmsr(MSR_FS_BASE);
        if (code == ARCH_SET_GS) { wrmsr(MSR_GS_BASE, (unsigned long long)addr); return 0; }
        if (code == ARCH_GET_GS) return (long long)rdmsr(MSR_GS_BASE);
        return -22;   /* EINVAL */
    }

    case LINUX_SYS_set_tid_address: return pid;  /* 简化: 忽略 tidptr */

    case LINUX_SYS_brk: {
        if (!mp) return -1;
        if (a1 == 0) return (long long)mp->brk_end;
        if ((unsigned long)a1 < mp->brk_end) return (long long)mp->brk_end;
        mp->brk_end = (unsigned long)a1; return a1;
    }

    case LINUX_SYS_mmap: {
        /* a1=addr,a2=len,a3=prot,a4=flags,a5=fd,a6=off */
        unsigned long len = (unsigned long)a2;
        unsigned long flags = (unsigned long)a4;
        int fd = (int)a5;
        unsigned long off = (unsigned long)a6;
        if (!mp) return -12;
        unsigned long res;
        if ((flags & LX_MAP_FIXED) || (flags & LX_MAP_FIXED_NOREPLACE)) {
            res = (unsigned long)a1;
        } else {
            unsigned long need = (len + 0xFFFUL) & ~0xFFFUL;
            res = g_mmap_brk_next;
            g_mmap_brk_next += need;
            if (g_mmap_brk_next > 0x50000000UL) return -12;  /* ENOMEM */
        }
        if (flags & LX_MAP_ANONYMOUS) {
            my_memset((void*)res, 0, (unsigned int)len);
        } else if (fd >= 3 && mp && fd < MPROC_FD_MAX && mp->fd_inode[fd]) {
            /* 文件映射: 从 fd 对应的 inode 读取 off 处 len 字节 */
            unsigned int ino = mp->fd_inode[fd];
            unsigned char *dst = (unsigned char*)res;
            unsigned long done = 0;
            while (done < len) {
                unsigned int chunk = (len - done > 65536) ? 65536 : (unsigned int)(len - done);
                unsigned int rd = read_file_range(ino, dst + done, (unsigned int)(off + done), chunk);
                if (rd == 0) break;
                done += rd;
            }
            /* 不足部分清零 */
            if (done < len) my_memset(dst + done, 0, (unsigned int)(len - done));
        } else {
            my_memset((void*)res, 0, (unsigned int)len);
        }
        return (long long)res;
    }
    case LINUX_SYS_munmap: return 0;    /* 简化: 不回收 */
    case LINUX_SYS_mprotect: return 0;  /* 简化: identity map, 所有页 RWX */

    case LINUX_SYS_openat:
    case LINUX_SYS_open: {
        const char *path = (const char*)a1;
        if (nr == 257 /*openat*/) path = (const char*)a2;
        unsigned int ino = mproc_resolve(path);
        if (!ino) return -2;  /* ENOENT */
        int fd = mproc_fd_alloc(pid, ino);
        return fd < 0 ? -24 : (long long)fd;
    }
    case LINUX_SYS_close: {
        int fd = (int)a1;
        if (fd < 3) return 0;   /* stdin/stdout/stderr 空实现 */
        mproc_fd_close(pid, fd); return 0;
    }
    case LINUX_SYS_read: {
        int fd = (int)a1; char *buf = (char*)a2; unsigned long cnt = (unsigned long)a3;
        if (fd == 0) return 0;    /* stdin: 暂不支持 */
        if (!mp || fd < 3 || fd >= MPROC_FD_MAX || mp->fd_inode[fd] == 0) return -9;
        unsigned int ino = (unsigned int)mp->fd_inode[fd];
        long off = mp->fd_off[fd];
        unsigned int got = read_file_range(ino, buf, (unsigned int)off, (unsigned int)cnt);
        mp->fd_off[fd] += got;
        return (long long)got;
    }
    case LINUX_SYS_write: {
        int fd = (int)a1; const char *buf = (const char*)a2; unsigned long cnt = (unsigned long)a3;
        if (fd == 1 || fd == 2) {
            /* stdout/stderr → 串口 + 屏幕 (put_char 支持 UTF-8 累积, CJK 也能正常显示)。
             * [根因] 旧代码只 serial_putc → hello world 输出仅在串口可见,
             * 屏幕无任何输出, 看起来像"程序一运行就退出了"。
             * 修复: 同时通过 put_char 输出到内核屏幕 (back buffer + flush)。 */
            for (unsigned long i = 0; i < cnt; i++) {
                serial_putc(buf[i]);
                put_char(buf[i]);
            }
            drv_gfx_maybe_flush();
            return (long long)cnt;
        }
        if (!mp || fd < 3 || fd >= MPROC_FD_MAX || mp->fd_inode[fd] == 0) return -9;
        unsigned int ino = (unsigned int)mp->fd_inode[fd];
        /* EFMOS 没有逐写的 range API: 先把整个文件读到 shim 缓冲, 修改后回写.
         * 用 kernel_api 中的 file_write 直接覆盖写 (path 未知, 退化为 -EOPNOTSUPP) */
        return -95;   /* EOPNOTSUPP */
    }
    case LINUX_SYS_lseek: {
        int fd = (int)a1; long off = (long)a2; int whence = (int)a3;
        if (!mp || fd < 3 || fd >= MPROC_FD_MAX || mp->fd_inode[fd] == 0) return -9;
        long base = 0;
        unsigned char inode_buf[256]; read_inode((unsigned int)mp->fd_inode[fd], inode_buf);
        long isize = *(unsigned int*)(inode_buf + 4);
        if (whence == 1) base = mp->fd_off[fd]; else if (whence == 2) base = isize;
        long no = base + off;
        if (no < 0) no = 0;
        mp->fd_off[fd] = no;
        return no;
    }
    case LINUX_SYS_fstat: {
        /* x86-64 musl struct stat 布局 (144 字节):
         *   off 0:  st_dev (8B)
         *   off 8:  st_ino (8B)
         *   off 16: st_nlink (8B)
         *   off 24: st_mode (4B) + st_uid (4B)
         *   off 32: st_gid (4B) + __pad0 (4B)
         *   off 40: st_rdev (8B)
         *   off 48: st_size (8B)
         *   off 56: st_blksize (8B) */
        char *stb = (char*)a2;
        if (!stb) return -14;
        for (int i = 0; i < 144; i++) stb[i] = 0;
        unsigned int mode = 0100644;  /* S_IFREG | 0644 */
        unsigned int fsize = 0;
        unsigned long long ino_val = 0;
        if (mp && (int)a1 >= 3 && (int)a1 < MPROC_FD_MAX && mp->fd_inode[(int)a1]) {
            ino_val = (unsigned long long)mp->fd_inode[(int)a1];
            unsigned char ib[256]; read_inode((unsigned int)mp->fd_inode[(int)a1], ib);
            fsize = *(unsigned int*)(ib + 4);
        } else if ((int)a1 == 1 || (int)a1 == 2) {
            mode = 020620;  /* S_IFCHR | 0620 (字符设备, stdout/stderr) */
        }
        *(unsigned long long*)(stb + 0) = 0;           /* st_dev */
        *(unsigned long long*)(stb + 8) = ino_val;     /* st_ino */
        *(unsigned long long*)(stb + 16) = 1;          /* st_nlink */
        *(unsigned int*)(stb + 24) = mode;             /* st_mode */
        *(unsigned int*)(stb + 28) = 0;                /* st_uid */
        *(unsigned int*)(stb + 32) = 0;                /* st_gid */
        *(unsigned long long*)(stb + 40) = 0;          /* st_rdev */
        *(long long*)(stb + 48) = (long long)fsize;    /* st_size */
        *(long long*)(stb + 56) = 4096;                /* st_blksize */
        return 0;
    }
    case LINUX_SYS_stat:
    case LINUX_SYS_lstat:
    case LINUX_SYS_newfstatat: {
        const char *path = (nr == LINUX_SYS_newfstatat) ? (const char*)a2 : (const char*)a1;
        char *stb = (char*)(nr == LINUX_SYS_newfstatat ? a3 : a2);
        if (!stb) return -14;
        for (int i = 0; i < 144; i++) stb[i] = 0;
        unsigned int ino = mproc_resolve(path);
        if (!ino) return -2;   /* ENOENT */
        unsigned char ib[256]; read_inode(ino, ib);
        unsigned int fsize = *(unsigned int*)(ib + 4);
        *(unsigned long long*)(stb + 0) = 0;           /* st_dev */
        *(unsigned long long*)(stb + 8) = ino;         /* st_ino */
        *(unsigned long long*)(stb + 16) = 1;          /* st_nlink */
        *(unsigned int*)(stb + 24) = 0100644;          /* st_mode = S_IFREG|0644 */
        *(long long*)(stb + 48) = (long long)fsize;    /* st_size */
        *(long long*)(stb + 56) = 4096;                /* st_blksize */
        return 0;
    }

    case LINUX_SYS_ioctl: return -25;   /* ENOTTY */
    case LINUX_SYS_sched_yield: sched_yield(); return 0;
    case LINUX_SYS_nanosleep: {
        const long *req = (const long*)a1;
        if (req) {
            unsigned long ms = (unsigned long)req[0] * 1000UL + (unsigned long)req[1] / 1000000UL;
            if (g_apic_timer_masked_poll_ms) g_apic_timer_masked_poll_ms(ms > 1 ? ms - 1 : 1);
        }
        return 0;
    }
    case LINUX_SYS_clock_gettime: {
        /* 用 TSC 近似: 假设 ~2GHz, 返回秒+纳秒.
         * CLOCK_REALTIME(0)=wall, CLOCK_MONOTONIC(1)=uptime. 简化: 都用 TSC. */
        unsigned long long tsc;
        unsigned int lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        tsc = ((unsigned long long)hi << 32) | lo;
        long *ts = (long*)a2;
        if (ts) {
            ts[0] = (long)(tsc / 2000000000ULL);       /* 秒 */
            ts[1] = (long)((tsc % 2000000000ULL) * 5);  /* 纳秒 (1/2GHz = 0.5ns) */
        }
        return 0;
    }
    case LINUX_SYS_uname: {
        char *u = (char*)a1;
        if (!u) return -14;
        /* struct utsname: 6 个 65 字节 char array */
        my_memset(u, 0, 65*6);
        const char *names[6] = {"EFMOS","efmos","0.1","#1 EFMOS","x86_64","(none)"};
        for (int i = 0; i < 6; i++) {
            const char *s = names[i]; int j = 0; while (s[j] && j < 64) { u[i*65+j] = s[j]; j++; }
        }
        return 0;
    }
    case LINUX_SYS_rt_sigaction: {
        /* musl: sigaction(signo, act, oldact). 若 oldact 非空, 清零它. */
        if (a3) { long *old = (long*)a3; for (int i = 0; i < 4; i++) old[i] = 0; }
        return 0;
    }
    case LINUX_SYS_rt_sigprocmask: {
        /* musl: sigprocmask(how, set, oldset). 若 oldset 非空, 清零. */
        if (a3) { long *old = (long*)a3; *old = 0; }
        return 0;
    }
    case LINUX_SYS_futex:
    case LINUX_SYS_prctl:
    case LINUX_SYS_sigaltstack:
    case LINUX_SYS_tgkill:
        return 0;
    case LINUX_SYS_getrandom: {
        /* musl: getrandom(buf, len, flags). 填充伪随机字节 (stack canary 等). */
        unsigned char *buf = (unsigned char*)a1;
        unsigned long len = (unsigned long)a2;
        unsigned int lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        unsigned long seed = ((unsigned long long)hi << 32) | lo;
        for (unsigned long i = 0; i < len; i++) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            buf[i] = (unsigned char)(seed >> 33);
        }
        return (long long)len;
    }
    case LINUX_SYS_set_robust_list: return 0;   /* 简化: 忽略 robust futex list */
    case LINUX_SYS_rseq: return -38;            /* ENOSYS: musl 检测后跳过 */
    case LINUX_SYS_madvise: return 0;           /* 简化: 忽略内存建议 */
    case LINUX_SYS_mremap: {
        /* mremap(old_addr, old_size, new_size, flags). 简化: 原地扩展或分配新区域. */
        unsigned long new_sz = (unsigned long)a3;
        if (new_sz <= (unsigned long)a2) return a1;  /* 缩小: 原地址返回 */
        /* 扩展: 分配新区域并复制 (简化: 返回新 bump 区域) */
        if (!mp) return -12;
        unsigned long need = (new_sz + 0xFFFUL) & ~0xFFFUL;
        unsigned long res = g_mmap_brk_next;
        g_mmap_brk_next += need;
        if (g_mmap_brk_next > 0x50000000UL) return -12;
        my_memcpy((void*)res, (void*)a1, (unsigned int)a2);
        my_memset((void*)(res + a2), 0, (unsigned int)(new_sz - a2));
        return (long long)res;
    }
    case LINUX_SYS_writev: {
        /* writev(fd, iov, iovcnt). 对 stdout/stderr 合并输出到串口+屏幕。 */
        int fd = (int)a1;
        const struct { void *base; unsigned long len; } *iov = (void*)a2;
        int iovcnt = (int)a3;
        if (fd == 1 || fd == 2) {
            unsigned long total = 0;
            for (int i = 0; i < iovcnt; i++) {
                const char *b = (const char*)iov[i].base;
                for (unsigned long j = 0; j < iov[i].len; j++) {
                    serial_putc(b[j]);
                    put_char(b[j]);
                }
                total += iov[i].len;
            }
            drv_gfx_maybe_flush();
            return (long long)total;
        }
        return -9;  /* EBADF */
    }
    case LINUX_SYS_access: {
        /* access(path, mode). 检查文件是否存在. */
        unsigned int ino = mproc_resolve((const char*)a1);
        return ino ? 0 : -2;  /* 0=OK, -2=ENOENT */
    }
    case LINUX_SYS_dup: {
        if (!mp) return -9;
        int fd = (int)a1;
        if (fd < 0 || fd >= MPROC_FD_MAX || mp->fd_inode[fd] == 0) return -9;
        int nfd = mproc_fd_alloc(pid, (unsigned int)mp->fd_inode[fd]);
        return nfd < 0 ? -24 : (long long)nfd;
    }
    case LINUX_SYS_dup2: {
        if (!mp) return -9;
        int oldfd = (int)a1, newfd = (int)a2;
        if (oldfd < 0 || oldfd >= MPROC_FD_MAX || mp->fd_inode[oldfd] == 0) return -9;
        if (newfd < 0 || newfd >= MPROC_FD_MAX) return -9;
        if (newfd == oldfd) return newfd;
        if (mp->fd_inode[newfd]) mp->fd_inode[newfd] = 0;
        mp->fd_inode[newfd] = mp->fd_inode[oldfd];
        mp->fd_off[newfd] = mp->fd_off[oldfd];
        return newfd;
    }
    case LINUX_SYS_fcntl: {
        /* fcntl(fd, cmd, ...). 简化: 返回 0 (FD_CLOEXEC=0). */
        int cmd = (int)a2;
        if (cmd == 0 /*F_DUPFD*/) {
            if (!mp) return -9;
            int fd = (int)a1;
            if (fd < 0 || fd >= MPROC_FD_MAX || mp->fd_inode[fd] == 0) return -9;
            int nfd = mproc_fd_alloc(pid, (unsigned int)mp->fd_inode[fd]);
            return nfd < 0 ? -24 : (long long)nfd;
        }
        return 0;
    }
    case LINUX_SYS_readlink:
    case LINUX_SYS_readlinkat: {
        /* readlink(path, buf, bufsiz). /proc/self/exe → 程序名. */
        const char *path = (nr == LINUX_SYS_readlinkat) ? (const char*)a2 : (const char*)a1;
        char *buf = (char*)(nr == LINUX_SYS_readlinkat ? a3 : a2);
        unsigned long bufsiz = (unsigned long)(nr == LINUX_SYS_readlinkat ? a4 : a3);
        if (!path || !buf || bufsiz == 0) return -14;
        /* 简化: 返回 "/EFMOS/<progname>" */
        const char *fake = "/EFMOS/prog.elf";
        int fl = 0; while (fake[fl]) fl++;
        if ((unsigned long)fl > bufsiz) fl = (int)bufsiz;
        for (int i = 0; i < fl; i++) buf[i] = fake[i];
        return fl;
    }
    case LINUX_SYS_getdents64: {
        /* getdents64(fd, buf, count). 简化: 返回 0 (空目录). */
        return 0;
    }
    default:
        serial_write("SYSCALL: unhandled nr="); serial_write_hex((unsigned int)nr);
        serial_write("\n");
        return -38;  /* ENOSYS */
    }
}

/* ---- syscall ASM 包装器 (syscall 指令 LSTAR 入口) ----
 * 进入内核时: syscall 自动保存 RIP→RCX, RFLAGS→R11
 * 注意: 不能破坏 rcx/r11 (被 syscall 硬件占用), 参数在 rdi/rsi/rdx/r10/r8/r9
 * syscall 约定: 内核栈不变 (RSP 依然是用户栈!)
 * → 我们必须手动切换到当前任务的内核栈. */
extern void musl_syscall_entry(void);   /* 实现在下面的 asm 中 */
extern void musl_int80_entry(void);

/* 定义为全局汇编, 内联到 .text */
__asm__(
".section .text\n"
".global musl_syscall_entry\n"
".type musl_syscall_entry, @function\n"
"musl_syscall_entry:\n"
"  mov  %rsp, %r12\n"
"  mov  g_cur_kstack_top(%rip), %rsp\n"
"  sub $32, %rsp\n"
"  mov %rcx,  0(%rsp)\n"
"  mov %r11,  8(%rsp)\n"
"  mov %r12, 16(%rsp)\n"
"  push %rax\n"
"  push %rbx; push %rcx; push %rdx\n"
"  push %rsi; push %rdi; push %rbp; push %r8\n"
"  push %r9;  push %r10; push %r11; push %r12\n"
"  push %r13; push %r14; push %r15\n"
"  push $0\n"
"  mov 15*8(%rsp), %rdi\n"
"  mov 10*8(%rsp), %rsi\n"
"  mov 11*8(%rsp), %rdx\n"
"  mov 12*8(%rsp), %rcx\n"
"  mov  6*8(%rsp), %r8\n"
"  mov  8*8(%rsp), %r9\n"
"  mov  7*8(%rsp), %rax\n"
"  mov %rax, 0(%rsp)\n"
"  call musl_sys_dispatch\n"
"  add $8, %rsp\n"
"  pop %r15; pop %r14; pop %r13; pop %r12\n"
"  pop %r11; pop %r10; pop %r9;  pop %r8\n"
"  pop %rbp; pop %rdi; pop %rsi; pop %rdx\n"
"  pop %rcx; pop %rbx\n"
"  add $8, %rsp\n"
"  mov  0(%rsp), %rcx\n"
"  mov  8(%rsp), %r11\n"
"  mov 16(%rsp), %rsp\n"
"  sysretq\n"
".size musl_syscall_entry, .-musl_syscall_entry\n"
"\n"
".global musl_int80_entry\n"
".type musl_int80_entry, @function\n"
"musl_int80_entry:\n"
"  cld\n"
"  sub $16, %rsp\n"
"  push $0; push $128\n"
"  push %rax\n"
"  push %rbx; push %rcx; push %rdx\n"
"  push %rsi; push %rdi; push %rbp; push %r8\n"
"  push %r9;  push %r10; push %r11; push %r12\n"
"  push %r13; push %r14; push %r15\n"
"  mov 14*8(%rsp), %rdi\n"
"  mov  9*8(%rsp), %rsi\n"
"  mov 10*8(%rsp), %rdx\n"
"  mov 11*8(%rsp), %rcx\n"
"  mov  5*8(%rsp), %r8\n"
"  mov  7*8(%rsp), %r9\n"
"  mov  6*8(%rsp), %rax\n"
"  push %rax\n"
"  call musl_sys_dispatch\n"
"  add $8, %rsp\n"
"  pop %r15; pop %r14; pop %r13; pop %r12\n"
"  pop %r11; pop %r10; pop %r9;  pop %r8\n"
"  pop %rbp; pop %rdi; pop %rsi; pop %rdx\n"
"  pop %rcx; pop %rbx\n"
"  add $8, %rsp\n"
"  add $32, %rsp\n"
"  iretq\n"
".size musl_int80_entry, .-musl_int80_entry\n"
".previous\n"
);

/* ---- 辅助: 当前任务内核栈顶 ----
 * g_cur_kstack_top 由调度器 switch_to 时更新 (或 task_switch). 这里提供全局变量. */
static unsigned long g_cur_kstack_top = 0;   /* syscall/sysret 换栈用 */

/* ---- syscall shim 初始化 ----
 * 设置 LSTAR MSR + STAR + FMASK + EFER.SCE, 并改写 IDT 0x80 为用户可访问 (DPL=3).
 * 必须在 load_idt() / init_idt() 之后调用. */
static void set_idt_gate(int num, unsigned long long handler, unsigned short sel, unsigned char flags);
static inline void wrmsr_wrapper(unsigned int msr, unsigned long long val) { wrmsr(msr, val); }
static void musl_syscall_setup(void) {
    /* 1. IDT 0x80: 使用 int 80 入口, DPL=3 (用户可调用 int $0x80) */
    set_idt_gate(0x80, (unsigned long long)musl_int80_entry, 0x08, 0xEE);
    /* 2. IA32_EFER.SCE = 1 开启 syscall/sysret */
    unsigned long long efer = rdmsr(MSR_IA32_EFER);
    wrmsr_wrapper(MSR_IA32_EFER, efer | 1ULL);
    /* 3. STAR: 内核 CS=0x08, 用户 CS=0x18(兼容32位, 这里没用)
     *   对 64 位模式: STAR[47:32] → 用户 CS, STAR[63:48] → 内核 CS
     *   约定: kernel CS = 0x08, user CS = 0x1b (但我们环 0 运行, 简化设内核段) */
    unsigned long long star = (0x08ULL << 32) | (0x1bULL << 48);
    wrmsr_wrapper(MSR_STAR, star);
    /* 4. LSTAR = musl_syscall_entry 地址 */
    wrmsr_wrapper(MSR_LSTAR, (unsigned long long)musl_syscall_entry);
    /* 5. SFMASK: 清除 IF 标志位 (bit 9 = 0x200), 进入 syscall 时关中断 */
    wrmsr_wrapper(MSR_SFMASK, 0x200ULL);
    serial_write("SYSCALL: shim installed (int80 + syscall LSTAR)\n");
}

/* ============================================================
 *  musl libc 初始栈 & AUXV 构建 + __libc_start_main 入口
 * ============================================================
 * musl 启动时 `_start` 从栈上读取 argc, argv, envp, auxv,
 * 然后 call __libc_start_main(main, argc, argv, ...).
 *
 * 栈布局 (由高地址向低地址):
 *   [字符串区]  "/path/to/exe" + env vars 字符串
 *   [AT_RANDOM 指向的 16 字节 random 数据]
 *   ...
 *   [auxv[]]  auxv[0]= {AT_PHDR, addr} ... auxv[n] = {AT_NULL, 0}
 *   [envp[]]  envp[0] = "HOME=/root", ..., envp[m] = NULL
 *   [argv[]]  argv[0] = "/path/to/exe", ..., argv[argc] = NULL
 *   [argc]    int
 *
 * AUXV 必须提供的 key: AT_PHDR, AT_PHENT, AT_PHNUM, AT_PAGESZ, AT_BASE,
 * AT_ENTRY, AT_RANDOM, AT_SYSINFO_EHDR (可 0), AT_UID, AT_EUID, AT_GID,
 * AT_EGID, AT_HWCAP, AT_CLKTCK, AT_NULL. */
#define AT_NULL      0
#define AT_PHDR      3
#define AT_PHENT     4
#define AT_PHNUM     5
#define AT_PAGESZ    6
#define AT_BASE      7
#define AT_FLAGS     8
#define AT_ENTRY     9
#define AT_UID       11
#define AT_EUID      12
#define AT_GID       13
#define AT_EGID      14
#define AT_CLKTCK    17
#define AT_RANDOM    25
#define AT_EXECFN    31
#define AT_SYSINFO_EHDR 33
#define AT_HWCAP    16
#define AT_SECURE   23

typedef struct { unsigned long a_type; unsigned long a_val; } auxv_t;

/* 在 sp (用户栈顶) 往下构建栈帧, 返回最终应使用的栈顶. */
static unsigned long musl_build_init_stack(unsigned long sp,
                                           dyn_so_t *exe,
                                           dyn_so_t *interp_or_libc,
                                           const char *progname) {
    /* sp 是栈最高地址 (top+8). 先对齐到 16 字节边界以下.
     * 我们写字符串到高地址, 再写 auxv/envp/argv/argc 到低地址,
     * 最终 rsp = 对齐后的栈指针 (向下生长). */
    unsigned long stack_top = sp & ~0xFUL;

    /* 1. 字符串区: progname (AT_EXECFN), 再 AT_RANDOM 16 字节.
     *    预留 256 字节给字符串区. */
    unsigned long strings = stack_top - 256;
    strings &= ~0xFUL;
    int plen = 0; while (progname && progname[plen]) plen++;
    char *strbuf = (char*)strings;
    for (int i = 0; i < plen && i < 127; i++) strbuf[i] = progname[i];
    strbuf[plen] = 0;
    unsigned long execfn_str = strings;
    /* 随机 16 字节 (固定种子, 不需要真随机) */
    unsigned long rand16 = strings + 128;
    unsigned char *rp = (unsigned char*)rand16;
    for (int i = 0; i < 16; i++) rp[i] = (unsigned char)(0x11 * (i+1));

    /* 2. 计算各表项数量, 分配空间 */
    int envc = 1;
    int argc = 1;
    int auxvc = 18;   /* AT_* 条目数, 包括一个 AT_NULL */

    /* 从 strings 下方继续, 先放 auxv → envp → argv → argc, 保持 16 字节对齐 */
    unsigned long p = strings - 64;
    p &= ~0xFUL;

    /* auxv */
    auxv_t *aux = (auxv_t*)p;
    int ai = 0;
    /* 用 elf_load_so 中保存的 e_phoff/e_phentsize/e_phnum */
    aux[ai++] = (auxv_t){ AT_PHDR,    (unsigned long)(exe->base + exe->e_phoff) };
    aux[ai++] = (auxv_t){ AT_PHENT,   (unsigned long)(exe->e_phentsize ? exe->e_phentsize : 56) };
    aux[ai++] = (auxv_t){ AT_PHNUM,   (unsigned long)(exe->e_phnum ? exe->e_phnum : 4) };
    aux[ai++] = (auxv_t){ AT_PAGESZ,  DYN_PAGE_SIZE };
    aux[ai++] = (auxv_t){ AT_BASE,    interp_or_libc ? interp_or_libc->base : exe->base };
    aux[ai++] = (auxv_t){ AT_FLAGS,   0UL };
    aux[ai++] = (auxv_t){ AT_ENTRY,   exe->entry };
    aux[ai++] = (auxv_t){ AT_UID,     0UL };
    aux[ai++] = (auxv_t){ AT_EUID,    0UL };
    aux[ai++] = (auxv_t){ AT_GID,     0UL };
    aux[ai++] = (auxv_t){ AT_EGID,    0UL };
    aux[ai++] = (auxv_t){ AT_CLKTCK,  100UL };
    aux[ai++] = (auxv_t){ AT_HWCAP,   0x0008000000000000UL };  /* SSE2 标识 */
    aux[ai++] = (auxv_t){ AT_SECURE,  0UL };
    aux[ai++] = (auxv_t){ AT_RANDOM,  rand16 };
    aux[ai++] = (auxv_t){ AT_EXECFN,  execfn_str };
    aux[ai++] = (auxv_t){ AT_SYSINFO_EHDR, 0UL };
    aux[ai++] = (auxv_t){ AT_NULL,    0UL };
    (void)auxvc;

    p -= (envc + 1) * sizeof(unsigned long);  /* envp[] */
    unsigned long *envp = (unsigned long*)p;
    envp[0] = strings + 64;    /* "HOME=/" 64 字节处 */
    my_memcpy((void*)envp[0], "HOME=/\0USER=root\0\0", 20);
    envp[1] = 0;

    p -= (argc + 1) * sizeof(unsigned long);  /* argv[] */
    unsigned long *argv = (unsigned long*)p;
    argv[0] = execfn_str;
    argv[1] = 0;

    p -= sizeof(unsigned long);    /* argc */
    unsigned long *argcp = (unsigned long*)p;
    *argcp = argc;

    /* 最终栈顶: p 再 -8 作为 16 字节对齐 (musl _start 假设进入时栈 16-byte aligned,
     * 然后 push 1×8 给 call main, 所以初始 stack 必须满足 (sp%16==8) 以实现进入 main 后对齐)
     * 我们这里 sp = p 且 p 16-byte 对齐, 所以 p-8 是需要的最终栈. */
    return p - 8;
}

/* ---- 启动 musl 程序的最终入口: 跳转到 _start, 让它做 __libc_start_main(main,...) ----
 * musl libc 的 _start 函数:
 *   mov rsp, rbp
 *   xor rbp, rbp
 *   mov rdi, [rsp]            // argc
 *   lea rsi, [rsp+8]          // argv
 *   lea rdx, [rsp+8+8*(argc+1)]  // envp
 *   lea rcx, auxv             // 位于 envp NULL 之后
 *   call __libc_start_main(main, argc, argv, init, fini, ...)
 * 无需内核特殊处理, 只要栈布局正确即可. */

/* ---------- 动态链接任务入口 (musl 完整版) ---------- */
struct elf_dyn_task_param {
    unsigned long long entry;   /* 主程序 _start 入口绝对地址 */
    unsigned long long base;    /* 主程序加载基址 */
    dyn_so_t *exe_so;           /* 主程序 SO 指针 (用于 auxv) */
    dyn_so_t *libc_so;          /* libc/ld-musl SO 指针 */
    char name[48];
};
static void elf_dyn_task_entry(void *arg) {
    struct elf_dyn_task_param *p = (struct elf_dyn_task_param*)arg;
    if (!p) { task_exit(1); return; }

    /* 分配独立用户栈 (4MB 够用) */
    #define ELF_DYN_STACK (4UL * 1024UL * 1024UL)
    void *ustack = kheap_malloc(ELF_DYN_STACK);
    if (!ustack) { kheap_free(p); task_exit(2); return; }
    unsigned long ustack_bottom = (unsigned long)ustack;
    unsigned long ustack_top = (ustack_bottom + ELF_DYN_STACK) & ~0xFUL;

    int mypid = current_get_pid();
    efs_task_map_register(mypid, ustack, (unsigned int)p->base, ELF_DYN_STACK);

    /* 初始化 musl 进程状态 */
    musl_proc_t *mp = mproc_get(mypid);
    /* ---- 静态 TLS 规划: 扫描所有已加载 SO, 收集 PT_TLS, 按对齐拼接 ----
     * musl x86-64 static TLS 布局:
     *   TPO (TLS Pointer) = dtv[0].val 指向"组合 TLS 数据区起点"
     *   TCB.dtv → 指向 dtv 指针数组 (dtv[0] 被当作 struct{size_t gen; void*val;}[],
     *     但 musl 内部也允许 dtv[-1] 作为生成号, dtv[0].val = TPO.
     *   简化: 我们用一个单元素 dtv: { .val = tls_area_start }, 让 dtv 指针指向 &dtv[0]
     *   FS.base 仍指向 &tcb->self_ptr. */
    unsigned long  tls_offsets[DYN_MAX_SO]; /* 每个 SO 的 TLS 段在组合区中的偏移 */
    unsigned long  tls_total = 0;
    unsigned long  tls_align_max = 16;
    for (int ti = 0; ti < DYN_MAX_SO; ti++) {
        dyn_so_t *so = &g_dyn_so[ti];
        tls_offsets[ti] = 0;
        if (!so->used || so->tls_memsz == 0) continue;
        unsigned long a = so->tls_align ? so->tls_align : 16;
        if (a > tls_align_max) tls_align_max = a;
        unsigned long off = (tls_total + a - 1) & ~(a - 1);
        tls_offsets[ti] = off;
        tls_total = off + so->tls_memsz;
    }
    /* 为 TCB 预留空间 (TCB 放在组合数据之前, 让 dtv[0].val = TPO = tls_area 起点
     * 也即 TCB 起始地址). 这样线程本地变量 tpoff = TPO + @tpoff 就落在 TCB 之后.
     * musl 约定 TCB 末尾 self_ptr = FS.base = &tcb->self_ptr.
     * 我们一次分配: [TCB 64B][dtv ptr 8B][dtv[0] 16B? 简单: TCB 内自带 dtv 字段够用]
     * 实际上 TCB 结构: pad0[4]=32B + dtv=8B + mthread_canary=8B + pad1[2]=16B + self_ptr=8B → 64B 刚好.
     * TCB 内的 tcb->dtv 字段正好是指针, 指向我们的 dtv 数组. */
    unsigned long dtv_area_bytes = 0;
    unsigned long tls_data_bytes = tls_total;
    if (tls_data_bytes) {
        /* 需要 dtv: 至少 dtv[0] 作为 (gen,val). musl dtv 是 struct dtv { size_t gen; void *val; }[]
         * size_t=8B 所以每个元素 16B. dtv[0].gen=1, dtv[0].val = TPO (TLS Pointer Offset) */
        dtv_area_bytes = 32; /* 2 个 dtv 元素空间, 足够 */
    }
    /* 总分配: TCB + dtv_area + tls_data (按最大 align) */
    unsigned long tcb_extra_sz = sizeof(musl_tcb_t) + dtv_area_bytes + tls_data_bytes + tls_align_max;
    if (mp) {
        my_memset(mp, 0, sizeof(*mp));
        mp->brk_end = p->base + 0x200000UL;  /* 数据段 break 给个默认点 */
        mp->mmap_brk = g_mmap_brk_next;
        mp->ustack_base = ustack;
        mp->ustack_sz = ELF_DYN_STACK;
        /* fd 0/1/2 保留 (stdin/stdout/stderr) */
        mp->fd_inode[0] = 1; mp->fd_inode[1] = 1; mp->fd_inode[2] = 1;
        /* 分配 TLS block (TCB + dtv + 数据) + 设置 FS.base */
        musl_tcb_t *tcb = (musl_tcb_t*)kheap_malloc(tcb_extra_sz);
        if (tcb) {
            my_memset(tcb, 0, tcb_extra_sz);
            unsigned long tcb_base = (unsigned long)tcb;
            /* dtv 区域: 紧跟 TCB 之后, 16B 对齐 */
            unsigned long dtv_start = (tcb_base + sizeof(musl_tcb_t) + 15UL) & ~15UL;
            /* tls 数据区: 紧跟 dtv, 最大对齐 */
            unsigned long tls_area = (dtv_start + dtv_area_bytes + tls_align_max - 1) & ~(tls_align_max - 1);
            /* 初始化 dtv: tcb->dtv = &dtv[0] */
            unsigned long long *dtv = (unsigned long long*)dtv_start;
            dtv[0] = 1;                   /* dtv[0].gen = generation 1 */
            dtv[1] = (unsigned long long)tls_area;  /* dtv[0].val = TPO = 组合 TLS 数据起点 */
            tcb->dtv = (unsigned long)dtv;
            /* 逐 SO 复制 .tdata, 清零 .tbss */
            for (int ti = 0; ti < DYN_MAX_SO; ti++) {
                dyn_so_t *so = &g_dyn_so[ti];
                if (!so->used || so->tls_memsz == 0) continue;
                unsigned long dst = tls_area + tls_offsets[ti];
                if (so->tls_filesz > 0) {
                    unsigned long src = so->base + so->tls_vaddr;
                    my_memcpy((void*)dst, (void*)src, (unsigned int)so->tls_filesz);
                }
                /* memsz-filesz 的 .tbss 部分已因 kheap_malloc 清零, 无需再清 */
                (void)dst;
            }
            /* FS.base 指向 TCB 末尾 self_ptr */
            unsigned long fs = (unsigned long)&tcb->self_ptr;
            tcb->self_ptr = fs;
            mp->tcb = tcb;
            wrmsr(MSR_FS_BASE, (unsigned long long)fs);
            serial_write("TLS: tcb="); serial_write_hex(tcb_base);
            serial_write(" dtv="); serial_write_hex(dtv_start);
            serial_write(" area="); serial_write_hex(tls_area);
            serial_write(" total="); serial_write_hex(tls_data_bytes);
            serial_write("\n");
        }
    }

    /* 保存内核栈上下文 (异常处理器/异常恢复用), 同 efs_task_entry */
    unsigned long krsp, krbp;
    __asm__ volatile("mov %%rsp, %0; mov %%rbp, %1" : "=r"(krsp), "=r"(krbp));
    efs_saved_rsp = krsp;
    efs_saved_rbp = krbp;
    efs_load_addr = (unsigned int)p->base;
    efs_bin_size  = 0;
    efs_stack_ptr = ustack;
    efs_return_point = &&elf_dyn_done;
    efs_active = 1;

    efs_setup_api_table();

    /* 复制程序名 → API argbuf */
    int nl = 0; while (p->name[nl] && nl < 47) nl++;
    for (int i = 0; i < nl && i < (int)sizeof(efs_argbuf)-1; i++)
        efs_argbuf[i] = p->name[i];
    efs_arglen = nl; efs_argbuf[nl] = 0;

    serial_write("MUSL[start]: pid="); serial_write_hex(mypid);
    serial_write(" prog="); serial_write(p->name);
    serial_write(" entry="); serial_write_hex(p->entry); serial_write("\n");

    /* ---- 构建 musl 初始栈 ---- */
    unsigned long final_sp = musl_build_init_stack(ustack_top, p->exe_so, p->libc_so, p->name);

    /* 更新 syscall/sysret 换栈的当前任务内核栈顶 (switch_to 会更新, 但当前任务首次运行需手动).
     * krsp 是刚用汇编从 %%rsp 取到的当前内核栈顶近似. */
    g_cur_kstack_top = krsp;

    /* 切到用户栈 + 跳转 _start. 同样小心 rbp 局部变量读取问题 → 用寄存器. */
    register unsigned long r_entry __asm__("r12") = (unsigned long)p->entry;
    register unsigned long r_top   __asm__("r13") = final_sp;
    register unsigned long r_srsp  __asm__("r14") = krsp;
    register unsigned long r_srbp  __asm__("r15") = krbp;
    efs_active = 1;
    __asm__ volatile(
        "mov %%r13, %%rsp\n"
        "xor %%rbp, %%rbp\n"
        "call *%%r12\n"
        : : "r"(r_entry), "r"(r_top), "r"(r_srsp), "r"(r_srbp) : "memory"
    );
    /* 若程序返回 (通常 exit(0) 不返回): 恢复内核栈 */
    __asm__ volatile(
        "mov %0, %%rsp\n"
        "mov %1, %%rbp\n"
        : : "r"(efs_saved_rsp), "r"(efs_saved_rbp) : "memory"
    );
    efs_active = 0;
elf_dyn_done:
    efs_active = 0;
    if (mp && mp->tcb) kheap_free(mp->tcb);
    kheap_free(p);
    task_exit(0);
}

/* ---------- 加载并启动动态链接 ELF 程序 ----------
 * 在 /EFMOS/SYSTEM/LIB/ 预加载 libc + 共享库, 再加载主程序, 创建 task.
 * name: 主程序文件名 (不含路径, 位于 /EFMOS 或 /Program/<name>/).
 * 返回: >0 = 新任务 PID, <=0 失败. */
static int elf_spawn_dynamic(const char *name) {
    if (!name || !name[0]) return -1;
    int nl = my_strlen(name);
    if (nl > 72) return -1;
    if (nl > 4 && my_strcmp(name + nl - 4, ".efs") == 0) nl -= 4;
    char basename[80];
    my_memcpy(basename, name, nl); basename[nl] = 0;

    /* 找主程序 inode (/EFMOS -> /Program/<name>/ -> CWD) */
    char fname[80];
    my_memcpy(fname, name, nl);
    my_memcpy(fname + nl, ".elf", 4); fname[nl + 4] = 0;
    unsigned int efmos_ino = find_in_dir(2, "EFMOS");
    unsigned int ino = 0;
    if (efmos_ino) ino = find_in_dir(efmos_ino, fname);
    if (!ino) {
        char subpath[160] = "/Program/"; int j = 9;
        for (int i = 0; i < nl && i < 120; i++) subpath[j++] = basename[i];
        subpath[j] = 0;
        unsigned int pdir = resolve_inode(subpath, 0);
        if (pdir) ino = find_in_dir(pdir, fname);
    }
    if (!ino) ino = find_in_dir(current_dir_ino, fname);
    if (!ino) {
        /* 回退: 也接受 .efs 扩展名 (可能被误命名) */
        my_memcpy(fname + nl, ".efs", 4); fname[nl + 4] = 0;
        if (efmos_ino) ino = find_in_dir(efmos_ino, fname);
    }
    if (!ino) { serial_write("DYN: main exe not found\n"); return -2; }

    /* 找库目录 /EFMOS/SYSTEM/LIB */
    unsigned int sys_ino = 0, lib_dir_ino = 0;
    if (efmos_ino) sys_ino = find_in_dir(efmos_ino, "SYSTEM");
    if (sys_ino) lib_dir_ino = find_in_dir(sys_ino, "LIB");

    /* 阶段1: 预加载动态链接器 ld-musl (它本身也是 libc) */
    dyn_so_t *libc_so = 0;
    if (lib_dir_ino && !dyn_so_find("libc.musl-x86_64.so.1")) {
        unsigned int ld_ino = find_in_dir(lib_dir_ino, "ld-musl-x86_64.so.1");
        if (ld_ino) {
            libc_so = elf_load_so(ld_ino, "libc.musl-x86_64.so.1", 0);
        } else {
            /* 回退名: libc.so */
            unsigned int lc_ino = find_in_dir(lib_dir_ino, "libc.so");
            if (lc_ino) libc_so = elf_load_so(lc_ino, "libc.so", 0);
        }
    }
    if (!libc_so) libc_so = dyn_so_find("libc.musl-x86_64.so.1");

    /* 阶段2: 预加载 Mesa 共享库 (gbm/egl/drm 等) */
    if (lib_dir_ino) {
        static const char *mesa_libs[] = {
            "libgallium-25.2.so.1", "libgbm.so.1", "libEGL.so.1",
            "libdrm.so.2", "libdrm_efmos.so.1", 0
        };
        for (int i = 0; mesa_libs[i]; i++) {
            if (dyn_so_find(mesa_libs[i])) continue;
            unsigned int mi = find_in_dir(lib_dir_ino, mesa_libs[i]);
            if (mi) {
                dyn_so_t *mso = elf_load_so(mi, mesa_libs[i], 0);
                if (mso) elf_load_needed(mso, lib_dir_ino);
            }
        }
    }

    /* 阶段3: 加载主程序 (is_exec=1) */
    dyn_so_t *exe = elf_load_so(ino, basename, 1);
    if (!exe) { serial_write("DYN: failed to load main exe\n"); return -3; }
    /* 加载主程序的 DT_NEEDED */
    elf_load_needed(exe, lib_dir_ino);

    /* 阶段4: 二次重定位 (解析所有跨库符号) */
    dyn_relocate_all();

    /* 阶段4b: 所有重定位完成后, 按依赖顺序调用 DT_INIT/DT_INIT_ARRAY.
     * 顺序: 先 libc, 再其他库, 最后主程序. g_dyn_so 数组天然按加载顺序排列. */
    for (int i = 0; i < DYN_MAX_SO; i++) {
        if (g_dyn_so[i].used) dyn_call_init(&g_dyn_so[i]);
    }

    /* 阶段5: 创建任务 */
    struct elf_dyn_task_param *par = kheap_malloc(sizeof(*par));
    if (!par) return -4;
    my_memset(par, 0, sizeof(*par));
    par->entry    = exe->entry;
    par->base     = exe->base;
    par->exe_so   = exe;
    par->libc_so  = libc_so;
    for (int i = 0; i < nl && i < 47; i++) par->name[i] = basename[i];

    struct task_struct *t = task_create(basename, elf_dyn_task_entry, par, -1);
    if (!t) { kheap_free(par); return -5; }
    int newpid = task_ptr_get_pid(t);

    /* WM 模式: 创建窗口 (同 efs_spawn_async) */
    if (g_wm_enabled && my_strcmp(basename, "efmcompositor") != 0) {
        int ww = 60 * FONT_W + 2 * WM_BORDER + 20;
        int wh = WM_TITLE_H + 20 * FONT_H + 2 * WM_BORDER + 10;
        char ttl[80]; int tn = 0;
        while (basename[tn] && tn < 63) { ttl[tn] = basename[tn]; tn++; }
        ttl[tn] = 0;
        (void)wm_create_window(ttl, newpid, ww, wh, 0);
    }
    return newpid;
}

/* 覆盖/增强 efs_spawn: 父 efs 调用 spawn(name,args) 时
 *   - 默认仍同步 (保证 gcc 调用 as/ld 语义)
 *   - 但如果全局 WM 模式开启, 则异步启动并在窗口中运行 */
/* [注意] 原 efs_spawn 的同步语义通过 run_efs(name) 保持原样;
 * 窗口模式会被 shell 的 start 命令使用 efs_spawn_async 显式触发。 */

/* 当前 EFS 程序的命令行参数串 (程序名之后的全部参数, 空格分隔)。
 * shell 启动程序前写入; spawn 时由父程序写入供子程序读取。
 * 注意: efs_argbuf/efs_arglen 已在文件头部 (line 610) 定义, 此处不再重复。 */

/* ---------- 新增 EFS API 包装 ----------
 * malloc/free: 暴露内核堆给 .efs 程序 (编译器等需要动态内存)
 * spawn: 运行另一个 .efs 并传入参数串 (gcc 调用 as/ld 的基础)
 * get_args: 读取本程序启动时的命令行参数串 (程序名之后的参数) */
static int run_efs(const char *name);   /* 前向声明 (efs_spawn 在 run_efs 之前定义) */
static void *efs_malloc(unsigned long sz) { return kheap_malloc(sz); }
static void  efs_free(void *p) { kheap_free(p); }
/* spawn: name=程序名(不带.efs), args=参数串(程序名之后的参数,可为NULL=无参数)。
 * 把 args 写入 efs_argbuf 供子程序 get_args 读取, 再运行。返回 0=成功 -1=未找到。 */
static int efs_spawn(const char *name, const char *args) {
    efs_arglen = 0;
    if (args) {
        for (int i = 0; args[i] && efs_arglen < (int)sizeof(efs_argbuf) - 1; i++)
            efs_argbuf[efs_arglen++] = args[i];
        efs_argbuf[efs_arglen] = 0;
    } else {
        efs_argbuf[0] = 0;
    }
    return run_efs(name);
}
static int efs_get_args(char *buf, int max) {
    int n = efs_arglen;
    if (n > max - 1) n = (max > 0) ? max - 1 : 0;
    if (n > 0) my_memcpy(buf, efs_argbuf, n);
    if (max > 0) buf[n] = 0;
    return efs_arglen;   /* 返回原始长度 (即使被截断) */
}

/* ============================================================
 *  驱动子系统 (EFMOS 2026)
 *  - 全局注册表: g_drv_registry[]
 *  - load_driver(path): 读 .drv 文件, 加载到 load_addr, 执行 drv_entry 注册 ops
 *  - 内核 disk_read/disk_write/put_pixel 等会在有驱动时用驱动 ops,
 *    否则回退到内置实现 (保持系统即使无驱动也能启动)
 *  ============================================================ */
#include "drv_common.h"

#define DRV_MAX  16   /* 最多支持 16 个已注册驱动 */
struct drv_reg_entry {
    int  type;                     /* DRV_TYPE_DISK/GFX/NET/INPUT */
    int  used;                     /* 0=空, 1=已用 */
    char name[DRV_NAME_MAX];
    /* 各自 ops 指针 (按 type 选择其中之一使用; 指针指向驱动加载地址内,
     *  驱动程序常驻内存, 因此不需要 free.) */
    union {
        struct drv_disk_ops *disk;
        struct drv_gfx_ops  *gfx;
        const void         *raw;
    } ops;
    unsigned int ops_sz;
};
static struct drv_reg_entry g_drv_registry[DRV_MAX];

/* ---------- 驱动调用接口: 给驱动 drv_entry 用的 iface 回调 ----------
 * 这些函数包装内核原语 (serial_write / kheap_malloc / inb/outb / 注册表插入)。 */
static void drvi_log(const char *s)                 { serial_write(s); }
static void *drvi_kmalloc(unsigned long b)          { return kheap_malloc(b); }
static void  drvi_kfree(void *p)                    { kheap_free(p); }
static unsigned char  drvi_in8(unsigned short p)    { return inb(p); }
static unsigned short drvi_in16(unsigned short p)   { return inw(p); }
static unsigned int   drvi_in32(unsigned short p)   { return inl(p); }
static void drvi_out8(unsigned short p, unsigned char v)  { outb(p, v); }
static void drvi_out16(unsigned short p, unsigned short v){ outw(p, v); }
static void drvi_out32(unsigned short p, unsigned int v)  { outl(p, v); }

static int drvi_register_driver(const char *name, int type, const void *ops, unsigned int ops_sz) {
    if (!name || !name[0]) return -1;
    if (!ops || ops_sz == 0) return -1;
    if (type != DRV_TYPE_DISK && type != DRV_TYPE_GFX &&
        type != DRV_TYPE_NET  && type != DRV_TYPE_INPUT) return -1;
    /* 找空槽 */
    int slot = -1;
    for (int i = 0; i < DRV_MAX; i++) {
        if (!g_drv_registry[i].used) { if (slot < 0) slot = i; continue; }
        /* 重名拒绝 (防止同驱动重复注册) */
        if (my_strcmp(g_drv_registry[i].name, name) == 0) return -2;
    }
    if (slot < 0) return -3;           /* registry 已满 */
    /* 拷贝名字 (最多 DRV_NAME_MAX-1) */
    int n = 0; while (name[n] && n < DRV_NAME_MAX - 1) n++;
    for (int i = 0; i < n; i++) g_drv_registry[slot].name[i] = name[i];
    g_drv_registry[slot].name[n] = 0;
    g_drv_registry[slot].type    = type;
    g_drv_registry[slot].ops.raw = ops;
    g_drv_registry[slot].ops_sz  = ops_sz;
    g_drv_registry[slot].used    = 1;
    serial_write("DRV: registered '"); serial_write(name);
    serial_write("' type=0x"); serial_write_hex((unsigned long)type); serial_write("\n");
    /* disk 类型驱动注册后立刻 attach 到 disk read/write fast cache */
    if (type == DRV_TYPE_DISK) {
        drv_attach_disk_ops(g_drv_registry[slot].ops.disk);
    }
    /* gfx 类型驱动注册后立刻 attach, 使 fill_rect/draw_char/scroll 等立刻切到双缓冲+抗锯齿 */
    if (type == DRV_TYPE_GFX) {
        drv_attach_gfx_ops(g_drv_registry[slot].ops.gfx);
    }
    return 0;
}

/* 构造传给驱动的 iface 表 (函数指针指向内核包装函数, drv_entry 调用) */
static struct drv_kernel_if g_drv_iface_store;
static void drvi_build_iface(void) {
    g_drv_iface_store.magic = DRV_IFACE_MAGIC;
    g_drv_iface_store.register_driver = drvi_register_driver;
    g_drv_iface_store.log = drvi_log;
    g_drv_iface_store.kmalloc = drvi_kmalloc;
    g_drv_iface_store.kfree   = drvi_kfree;
    g_drv_iface_store.in8  = drvi_in8;
    g_drv_iface_store.in16 = drvi_in16;
    g_drv_iface_store.in32 = drvi_in32;
    g_drv_iface_store.out8  = drvi_out8;
    g_drv_iface_store.out16 = drvi_out16;
    g_drv_iface_store.out32 = drvi_out32;
    g_drv_iface_store.file_read = efs_file_read;
}

/* ---------- 驱动加载主函数 (供 EFS API load_driver 调用) ---------- */
static int efs_load_driver(const char *path) {
    if (!path || !path[0]) return -1;
    serial_write("DRV: loading "); serial_write(path); serial_write("\n");
    /* 读 .drv 文件 */
    unsigned int ino = resolve_inode(path, NULL);
    if (!ino) { serial_write("DRV: file not found\n"); return -1; }
    /* 读 12 字节头 */
    unsigned char hdr[16];
    int r = read_file_content(ino, hdr, 12);
    if (r < 12) { serial_write("DRV: header too small\n"); return -2; }
    /* 取文件真实大小 (容错) */
    unsigned char sb_ino[256]; read_inode(ino, sb_ino);
    unsigned int fsize = *(unsigned int*)(sb_ino + 4);
    unsigned int load_addr = *(unsigned int*)(hdr + 0);
    unsigned int bin_size  = ((unsigned int)hdr[8] << 24) | ((unsigned int)hdr[9] << 16) |
                             ((unsigned int)hdr[10] << 8) | (unsigned int)hdr[11];
    unsigned int remain = (fsize >= 12) ? fsize - 12u : 0;
    if (bin_size == 0 || bin_size > remain) bin_size = remain;
    if (bin_size == 0) { serial_write("DRV: empty binary\n"); return -2; }
    /* 加载地址合法性检查 (与 .efs 同一套规则, 但用 256MB~512MB 段给驱动, 避开 EFS 段 1MB~256MB) */
    if (load_addr < 0x10000000u) { /* < 256MB */
        /* 允许在 256MB 以下, 但避免与内核堆 (32~64MB) 冲突 */
        if (load_addr >= (unsigned int)KHEAP_BASE && load_addr < (unsigned int)(KHEAP_BASE + KHEAP_SIZE)) {
            serial_write("DRV: load_addr overlaps kernel heap\n"); return -3;
        }
    }
    if (load_addr >= 0x20000000u) { /* >= 512MB */
        serial_write("DRV: load_addr too high (>=512MB)\n"); return -3;
    }
    /* 流式加载: 跳过 12 字节头, 读到 load_addr */
    unsigned int got = read_file_range(ino, (unsigned char*)(unsigned long)load_addr, 12, bin_size);
    if (got < bin_size) {
        serial_write("DRV: short read got=0x"); serial_write_hex(got);
        serial_write(" want=0x"); serial_write_hex(bin_size); serial_write("\n");
        return -2;
    }
    /* .bss 清零: bin_size 末尾到 load_addr+1MB (与 EFS 同) */
    {
        unsigned char *pb = (unsigned char*)(unsigned long)(load_addr + bin_size);
        unsigned char *pe = (unsigned char*)(unsigned long)(load_addr + 0x100000UL);
        for (unsigned char *p = pb; p < pe; p++) *p = 0;
    }
    /* 构造 iface 表 (懒初始化一次) */
    drvi_build_iface();
    /* 保存 EFS 上下文, 避免驱动触发的异常被误判为 EFS #PF (driver 跑在内核上下文) */
    int saved_efs_active = efs_active;
    unsigned long saved_rsp = efs_saved_rsp, saved_rbp = efs_saved_rbp;
    void *saved_rp = efs_return_point;
    efs_active = 0;   /* 驱动使用内核栈, 异常走通用处理器 */
    /* 调用驱动入口:  drv_entry(iface, 0x1000 的 gop fb) */
    drv_entry_fn drv = (drv_entry_fn)(unsigned long)load_addr;
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    serial_write("DRV: calling entry at 0x"); serial_write_hex((unsigned long)load_addr);
    serial_write(" iface=0x"); serial_write_hex((unsigned long)&g_drv_iface_store);
    serial_write(" fb=0x"); serial_write_hex((unsigned long)fb);
    serial_write(" fb_base=0x"); serial_write_hex(fb->fb_base);
    serial_write("\n");
    (*drv)(&g_drv_iface_store, (struct drv_gop_fb*)fb);
    serial_write("DRV: entry returned\n");
    /* 恢复 EFS 上下文 (给调用者 EFS 环境使用) */
    efs_active = saved_efs_active;
    efs_saved_rsp = saved_rsp; efs_saved_rbp = saved_rbp;
    efs_return_point = saved_rp;
    /* 统计注册成功的驱动数量, 返回非负 = ok */
    int cnt = 0;
    for (int i = 0; i < DRV_MAX; i++) if (g_drv_registry[i].used) cnt++;
    serial_write("DRV: registered count="); serial_write_hex((unsigned long)cnt); serial_write("\n");
    return cnt;
}

/* ---------- 调试 API: 驱动数量 / 驱动列表 ---------- */
static int efs_driver_count(void) {
    int c = 0;
    for (int i = 0; i < DRV_MAX; i++) if (g_drv_registry[i].used) c++;
    return c;
}
static int efs_driver_list(char out_names[][32], int max) {
    if (!out_names || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < DRV_MAX && n < max; i++) {
        if (!g_drv_registry[i].used) continue;
        for (int k = 0; k < 32; k++) {
            out_names[n][k] = g_drv_registry[i].name[k];
            if (g_drv_registry[i].name[k] == 0) break;
        }
        n++;
    }
    return n;
}

/* ---------- 注册表查询辅助 (内核内部用): 按 type 找第一个注册的 ops ---------- */
static const struct drv_disk_ops *drv_find_disk(void) {
    for (int i = 0; i < DRV_MAX; i++)
        if (g_drv_registry[i].used && g_drv_registry[i].type == DRV_TYPE_DISK)
            return g_drv_registry[i].ops.disk;
    return 0;
}
static const struct drv_gfx_ops *drv_find_gfx(void) {
    for (int i = 0; i < DRV_MAX; i++)
        if (g_drv_registry[i].used && g_drv_registry[i].type == DRV_TYPE_GFX)
            return g_drv_registry[i].ops.gfx;
    return 0;
}

/* ---------- 磁盘访问的驱动拦截层: 若有驱动注册则走驱动, 否则原内核实现 ----------
 * (这样即使 ahci.drv 未加载 / 加载失败, 内核内置 AHCI/IDE 仍可启动系统)
 * 额外: g_drv_disk_ptr 是一个 "fast cache" 指针, disk_read_sector 等早期函数使用,
 * 在 drv_attach_disk_ops() 写入后即可在驱动注册瞬间生效。*/
static int drv_disk_read_blocks(unsigned int lba, unsigned int count, void *buf) {
    const struct drv_disk_ops *o = drv_find_disk();
    if (o && o->read_blocks) return o->read_blocks(lba, count, buf);
    return -1;  /* 无驱动, 调用者回退原路径 */
}
static int drv_disk_write_blocks(unsigned int lba, unsigned int count, const void *buf) {
    const struct drv_disk_ops *o = drv_find_disk();
    if (o && o->write_blocks) return o->write_blocks(lba, count, buf);
    return -1;
}

/* ---------- g_drv_disk_ptr 的设置函数 (disk_read_sector 前向声明需要) ----------
 * disk 驱动注册后立即写入此指针 (drvi_register_driver 返回后再写入,
 * 避免驱动在 init 阶段递归读盘造成死循环)。*/
static void drv_attach_disk_ops(const struct drv_disk_ops *ops) {
    g_drv_disk_ptr = ops;
    serial_write("DRV: disk ops attached (");
    if (ops) {
        serial_write("rb="); serial_write_hex(ops->sector_size ? (unsigned long)ops->sector_size : 512UL);
    } else {
        serial_write("NULL");
    }
    serial_write(")\n");
}
/* ---------- g_drv_gfx_ptr 的设置函数 ----------
 * Graphics 驱动注册成功后, 把 fill_rect/draw_char/put_pixel/scroll 等操作全部切到驱动实现,
 * 获得双缓冲+抗锯齿+渐变 等能力。如果注册前有像素写入 front buffer, 注册瞬间切过去,
 * 后续画面会通过 flush 同步, 无撕裂。*/
static void drv_attach_gfx_ops(const struct drv_gfx_ops *ops) {
    g_drv_gfx_ptr = ops;
    serial_write("DRV: gfx ops attached (");
    if (ops) {
        serial_write("pp="); serial_write_hex(ops->put_pixel ? 1UL : 0UL);
        serial_write(" fr="); serial_write_hex(ops->fill_rect ? 1UL : 0UL);
        serial_write(" dc="); serial_write_hex(ops->draw_char_8x16 ? 1UL : 0UL);
        serial_write(" sc="); serial_write_hex(ops->scroll_up ? 1UL : 0UL);
        serial_write(" fl="); serial_write_hex(ops->flush ? 1UL : 0UL);
    } else {
        serial_write("NULL");
    }
    serial_write(")\n");
}

/* 注册辅助: disk 类型驱动注册后立刻 attach 到 g_drv_disk_ptr, 让 disk_read_sector 切过去 */

/* [efmshell 全功能移植] fsop: 文件系统/系统杂项操作统一入口
 * 实现放在 get_mem_size_kb 之后 (需要 task 表 / WM 窗口表 / CPU 探测) */
static int efs_fsop(const char *op, const char *a, const char *b, char *out, int outsz);

/* 在 0x9000 构建内核 API 表 (供 .efs 程序通过 API 表调用内核功能) */
static void efs_setup_api_table(void) {
    struct kernel_api *api = (struct kernel_api*)0x9000;
    api->magic = EFS_API_MAGIC;
    api->_pad = 0;
    api->put_char = efs_put_char;
    api->print = efs_print;
    api->print_utf8 = efs_print_utf8;
    api->clear_screen = efs_clear_screen;
    api->file_read = efs_file_read;
    api->file_write = efs_file_write;
    api->file_exists = efs_file_exists;
    api->mkdir = efs_mkdir;
    api->readline = efs_readline;
    api->reboot = efs_reboot;
    api->get_lang = efs_get_lang;
    api->set_lang = efs_set_lang;
    api->save_settings = efs_save_settings;
    api->mouse_poll = efs_mouse_poll;
    api->mouse_set_cursor = efs_mouse_set_cursor;
    api->file_list = efs_file_list;
    api->file_delete = efs_file_delete;
    api->key_poll = efs_key_poll;
    api->get_current_user = efs_get_current_user;
    api->set_current_user = efs_set_current_user;
    api->user_list = efs_user_list;
    api->user_create = efs_user_create;
    api->user_delete = efs_user_delete;
    api->malloc = efs_malloc;
    api->free = efs_free;
    api->spawn = efs_spawn;
    api->get_args = efs_get_args;
    api->font_w = FONT_W;
    api->font_h = FONT_H;
    /* 2025+: 窗口模式信息. current_pid = 当前运行任务 pid (WM 启用后重定向用) */
    api->wm_enabled = g_wm_enabled ? 1 : 0;
    {
        int p = current_get_pid();
        api->current_pid = (p > 0) ? p : 0;
    }
    /* 2025+: 窗口安全像素级绘制 API (全屏模式 = 全局 FB 绝对坐标, 向后兼容) */
    api->put_pixel  = efs_put_pixel;
    api->fill_rect  = efs_fill_rect_api;
    api->draw_rect  = efs_draw_rect_api;
    api->get_viewport = efs_get_viewport;
    api->get_fb_info  = efs_get_fb_info;
    api->blit_to_window = efs_blit_to_window;
    /* 2026+: 驱动子系统 API (供 efmloader.efs 加载 .drv 文件) */
    api->load_driver   = efs_load_driver;
    api->driver_count  = efs_driver_count;
    api->driver_list   = efs_driver_list;
    /* 2026+: 多线程调度 API: 直接绑定内部实现 (简单不重定向) */
    api->sleep_ms      = task_sleep_ms;
    api->yield         = sched_yield;
    api->get_pid       = current_get_pid;
    api->spawn_async   = efs_spawn_async_with_args;
    api->set_priority  = task_set_priority;
    api->get_wm_snapshot = efs_get_wm_snapshot;
    api->set_compositor_active = efs_set_compositor_active;
    api->dlsym = efs_dlsym;
    api->get_backbuffer = efs_get_backbuffer;
    api->mark_dirty_rect = efs_mark_dirty_rect;
    api->flush_now = efs_flush_now;
    api->draw_char_unicode = efs_draw_char_unicode;
    api->set_gfx_info = efs_set_gfx_info;
    api->chdir = efs_chdir;
    /* [efmshell 全功能移植] 增强行编辑 / 多行文本 / fsop */
    api->readline_ext = efs_readline_ext;
    api->read_text    = efs_read_text;
    api->fsop         = efs_fsop;
}

/* [2026+] spawn_async 的包装: 写 args 到全局 efs_argbuf, 然后调用 efs_spawn_async(name)
 * 实现放在函数外 (C 不允许嵌套函数) */
static int efs_spawn_async_with_args(const char *name, const char *args) {
    extern char efs_argbuf[1024];
    extern int  efs_arglen;
    efs_arglen = 0;
    if (args) {
        while (args[efs_arglen] && efs_arglen < (int)sizeof(efs_argbuf) - 1) {
            efs_argbuf[efs_arglen] = args[efs_arglen];
            efs_arglen++;
        }
        efs_argbuf[efs_arglen] = 0;
    } else {
        efs_argbuf[0] = 0;
    }
    return efs_spawn_async(name);
}

/* 辅助: 检查指定 pid 是否有已绑定窗口 (由 efs_should_redirect 用于防止无窗口进程也进入重定向)
 * 实现: 转发给后面 WM 段定义的 wm_window_exists_for_pid(), 避免此处访问未定义的 g_wm_windows */
static int wm_find_by_pid_exists(int pid) {
    return wm_window_exists_for_pid(pid);
}

/* 加载并执行一个 .efs 可执行文件。
 * 搜索路径 (按顺序):
 *   1. /EFMOS/<name>.efs  — 系统内置程序
 *   2. /Program/<name>/<name>.efs  — Program 文件夹下每个程序独立子目录
 *   3. <当前工作目录>/<name>.efs  — 用户工作目录 (exec 查找会显式优先这里)
 * 若 cwd_first != 0: 先查当前工作目录, 再查系统路径 (exec 命令使用)
 * 若 cwd_first == 0: 只查系统路径 (shell 命令名自动解析时使用)
 * 返回: 0=找到并已执行, -1=未找到/格式错误。 */
static int run_efs_ex(const char *name, int cwd_first) {
    if (!name || !name[0]) return -1;
    int nl = my_strlen(name);
    if (nl <= 0 || nl > 72) return -1;
    /* 去除可能的 .efs 后缀 */
    if (nl > 4 && my_strcmp(name + nl - 4, ".efs") == 0) nl -= 4;

    /* 拼出 basename (<name>) 和 fname (<name>.efs) */
    char basename[80], fname[80];
    my_memcpy(basename, name, nl); basename[nl] = 0;
    my_memcpy(fname, name, nl);
    my_memcpy(fname + nl, ".efs", 4); fname[nl + 4] = 0;

    /* [辅助] 在目录 dir_ino 下找 fname, 找到就执行并返回 0/执行结果 */
    unsigned int tried = 0;
    unsigned int ino = 0;
    #define TRY_IN_DIR(dir_ino, src_tag) do { \
        if (!ino && (dir_ino)) { tried++; serial_write("EFS: lookup [" src_tag "] "); serial_write(fname); serial_write("\n"); \
            ino = find_in_dir((dir_ino), fname); } } while(0)

    if (cwd_first) {
        /* ====== 顺序: CWD -> /EFMOS -> /Program/<name>/ ====== */
        TRY_IN_DIR(current_dir_ino, "CWD");
        if (!ino) {
            unsigned int efmos_ino = find_in_dir(2, "EFMOS");
            TRY_IN_DIR(efmos_ino, "/EFMOS");
        }
        if (!ino) {
            char subpath[160] = "/Program/";
            int j = 9;
            for(int i=0; basename[i] && i<120; i++) subpath[j++]=basename[i];
            subpath[j]=0;
            unsigned int pdir = resolve_inode(subpath, NULL);
            TRY_IN_DIR(pdir, "/Program/<name>/");
        }
    } else {
        /* ====== 顺序: /EFMOS -> /Program/<name>/ -> CWD ====== */
        unsigned int efmos_ino = find_in_dir(2, "EFMOS");
        TRY_IN_DIR(efmos_ino, "/EFMOS");
        if (!ino) {
            char subpath[160] = "/Program/";
            int j = 9;
            for(int i=0;i<nl && i<120;i++) subpath[j++]=basename[i];
            subpath[j]=0;
            unsigned int pdir = resolve_inode(subpath, NULL);
            TRY_IN_DIR(pdir, "/Program/<name>/");
        }
        if (!ino) TRY_IN_DIR(current_dir_ino, "CWD");
    }
    if (!ino) {
        print_string(TR("EFS: file not found: ","EFS: 未找到文件: ")); print_string(fname);
        print_string(TR(" (searched: /EFMOS, /Program/<name>/"," (搜索路径: /EFMOS, /Program/<name>/"));
        if (cwd_first) print_string(", CWD)"); else print_string(", CWD)");
        print_string("\n");
        return -1;
    }
    #undef TRY_IN_DIR

    /* 读取 12 字节头到 fs_load_buf (仅头, 不再受 1MB 缓冲区限制) */
    int sz = read_file_content(ino, fs_load_buf, 12);
    if (sz < 12) {
        print_string(TR("EFS: too small (<12 bytes), invalid header\n","EFS: 文件过小 (<12字节), 头无效\n"));
        return -1;
    }

    /* [动态链接路由] ELF 魔数检测: 若是标准 ELF (musl 编译的动态可执行),
     * 交给动态加载器异步启动 (同步等待动态程序完成较复杂, 桌面场景用异步即可). */
    if (fs_load_buf[0] == 0x7F && fs_load_buf[1] == 'E' &&
        fs_load_buf[2] == 'L' && fs_load_buf[3] == 'F') {
        serial_write("DYN: ELF detected (sync path), launching async\n");
        int pid = elf_spawn_dynamic(name);
        return (pid > 0) ? 0 : -1;
    }

    /* 取文件真实大小 (i_size_lo), 用于 bin_size 容错回退 */
    unsigned char hd_inode[256]; read_inode(ino, hd_inode);
    unsigned int fsize = *(unsigned int*)(hd_inode + 4);

    /* 解析头: 加载地址 (LE, 低32位), 二进制大小 (BE 32位)
     * [关键容错] 头里 bin_size 为 0 或 超过文件实际大小,
     *   都回退到 "全文件剩余字节" (fsize - 12), 避免 xxd 缺失/构建脚本
     *   生成坏大小字段导致 .efs 永远无法执行。 */
    unsigned int load_addr = *(unsigned int*)(fs_load_buf + 0);
    unsigned int bin_size  = ((unsigned int)fs_load_buf[8]  << 24) |
                             ((unsigned int)fs_load_buf[9]  << 16) |
                             ((unsigned int)fs_load_buf[10] << 8)  |
                              (unsigned int)fs_load_buf[11];
    unsigned int remain = (fsize >= 12) ? fsize - 12u : 0;  /* 头后剩余字节数 */

    /* 加载地址合法性检查:
     *   - 必须非零, ≥ 1MB (避开低内存/内核头区)
     *   - < 256MB (512MB RAM 内的安全上限, 预留堆与系统区)
     *   - 不得落入内核堆区 [32MB, 64MB) (会被 malloc 使用) */
    if (load_addr == 0) { print_string(TR("EFS: load_addr=0, invalid\n","EFS: 加载地址=0, 无效\n")); return -1; }
    if (load_addr < 0x100000) {
        print_string(TR("EFS: load_addr too low (","EFS: 加载地址过低 ("));
        char hbuf[16]; char *hex="0123456789ABCDEF"; int j=0;
        unsigned int v=load_addr;
        hbuf[j++]='0'; hbuf[j++]='x';
        for(int i=7;i>=0;i--) hbuf[j++]=hex[(v>>(i*4))&0xF];
        hbuf[j]=0; print_string(hbuf);
        print_string(TR("), < 1MB\n","), 小于 1MB\n")); return -1;
    }
    if (load_addr >= 0x10000000) {
        print_string(TR("EFS: load_addr too high (>=256MB)\n","EFS: 加载地址过高 (>=256MB)\n")); return -1;
    }
    if (load_addr >= (unsigned int)KHEAP_BASE &&
        load_addr <  (unsigned int)(KHEAP_BASE + KHEAP_SIZE)) {
        print_string(TR("EFS: load_addr overlaps kernel heap (32MB~64MB)\n",
                        "EFS: 加载地址与内核堆重叠 (32MB~64MB)\n")); return -1;
    }

    /* 大小容错: bin_size 非法 (0 或 越界) → 按剩余文件大小 */
    if (bin_size == 0 || bin_size > remain) {
        serial_write("EFS: bin_size in header="); serial_write_hex(bin_size);
        serial_write(", fallback to remain="); serial_write_hex(remain); serial_write("\n");
        bin_size = remain;
    }
    if (bin_size == 0) { print_string(TR("EFS: empty binary, nothing to load\n","EFS: 二进制为空, 无可加载内容\n")); return -1; }

    /* [流式加载] 直接把二进制 (跳过 12 字节头) 加载到 load_addr,
     * 逐块读取, 不再经过 fs_load_buf → 突破 1MB 上限, 支持大型 .efs。 */
    unsigned int loaded = read_file_range(ino, (unsigned char*)(unsigned long)load_addr, 12, bin_size);
    if (loaded < bin_size) {
        serial_write("EFS: short load, got="); serial_write_hex(loaded);
        serial_write(" want="); serial_write_hex(bin_size); serial_write("\n");
    }
    /* [关键修复] 清零 .bss 区域: objcopy 只提取 .text/.rodata/.data,
     * 不包含 .bss (NOBITS)。若不清零, 全局/static 变量包含垃圾值 → 卡死。
     * 清零范围: bin_size 结束处到 load_addr+1MB (覆盖典型 .bss)。 */
    {
        unsigned char *bss_start = (unsigned char*)(unsigned long)(load_addr + bin_size);
        unsigned char *bss_end   = (unsigned char*)(unsigned long)(load_addr + 0x100000UL);
        for (unsigned char *p = bss_start; p < bss_end; p++) *p = 0;
    }
    efs_setup_api_table();

    serial_write("EFS exec: "); serial_write(name);
    serial_write(" @ "); serial_write_hex(load_addr);
    serial_write(" size="); serial_write_hex(bin_size); serial_write("\n");
    print_string(TR("Running ","正在运行 ")); print_string(name); print_string(".efs ...\n");

    /* 调用入口 (二进制起始字节 = 入口点)
     * [关键] 调用前先同步关闭光标/闪烁状态, 避免程序返回后光标抖动 */
    int old_cursor_vis = cursor_visible;
    hide_cursor();

    /* [关键修复] 为 EFS 程序分配独立栈 (1MB), 避免消耗内核栈导致 triple fault.
     * 用户程序的 prolog (push rbp, sub rsp, push callee-saved) + printf 调用
     * 需要大量栈空间, 如果共用内核栈会溢出破坏返回地址.
     * 栈从高地址向低地址增长, 所以设置 rsp = stack_top (对齐到 16 字节). */
    #define EFS_STACK_SIZE 0x100000UL   /* 1MB 独立栈 */
    void *efs_stack = kheap_malloc(EFS_STACK_SIZE);
    if (!efs_stack) {
        serial_write("EFS: stack alloc failed\n");
        show_cursor();
        return -1;
    }
    unsigned long efs_stack_top = (unsigned long)efs_stack + EFS_STACK_SIZE;
    /* [关键修复] x86-64 System V ABI: call 指令执行前 rsp 必须为 16k+8 (即 mod 16 == 8)。
     * 之前 (top-8)&~0xF 是错误的: 若 top 末位是 0x0 则 (top-8)&~0xF 末位仍是 0 →
     * 对齐到 0 mod 16, call push 返回地址后变成 8 mod 16 → 违反 ABI。
     * 正确做法: 先 16 字节向下对齐, 再显式减 8 → 结果必然是 8 mod 16。*/
    efs_stack_top = (efs_stack_top & ~0xFUL) - 8UL;

    /* 保存 EFS 上下文到全局变量, 供异常处理器使用 */
    unsigned long kernel_rsp, kernel_rbp;
    __asm__ volatile("mov %%rsp, %0; mov %%rbp, %1" : "=r"(kernel_rsp), "=r"(kernel_rbp));
    efs_saved_rsp = kernel_rsp;
    efs_saved_rbp = kernel_rbp;
    efs_load_addr = load_addr;
    efs_bin_size  = bin_size;
    efs_stack_ptr = efs_stack;
    efs_return_point = &&efs_return_point;

    /* [返回值捕获] 入口调用前先清零 efs_exit_code, 正常/异常路径再填入 */
    efs_exit_code = 0;

    /* 标记 EFS 活动中, 然后切换到 EFS 栈并调用 entry
     * [关键修复] 必须用寄存器传递 load_addr: 切栈后不能再访问 rbp 偏移的局部变量,
     * 因为 rsp 已经指向用户栈而 rbp 仍然指向内核栈帧。
     * 所以 entry_fn 放入寄存器 r12 (callee-saved, 不会被破坏)。 */
    efs_active = 1;
    void (*volatile entry_fn)(void) = (void(*)(void))(unsigned long)load_addr;
    register unsigned long r_entry __asm__("r12") = (unsigned long)entry_fn;
    register unsigned long r_top   __asm__("r13") = efs_stack_top;
    register unsigned long r_rax __asm__("rax");  /* 返回值: _start ret 时 rax 放在这里 */
    (void)r_rax;
    __asm__ volatile(
        "mov %%r13, %%rsp\n"
        "mov %%r13, %%rbp\n"     /* rbp 也设为用户栈顶, 与 rsp 一致, 不保留内核 rbp */
        "call *%%r12\n"
        : "=a"(r_rax)            /* [返回值] _start 的 GCC ret 遵守 System V, rax=返回值 */
        : "r"(r_entry), "r"(r_top)
        : "memory", "rcx", "rdx", "r8", "r9", "r10", "r11", "xmm0", "xmm1", "xmm2", "xmm3",
          "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12",
          "xmm13", "xmm14", "xmm15"
    );
    /* [正常返回路径] 填退出码 (rax 低 32 位, 传统 C int)。
     * 此时 rsp/rbp 仍在用户栈, 立刻恢复内核栈与 rbp。 */
    { int rc = (int)r_rax; efs_exit_code = rc; }

    /* [正常返回路径] EFS 程序通过 _start 的 ret 返回到这里。
     * 此时 rsp/rbp 仍在用户栈 (efs_stack_top) 上! 必须立即恢复内核栈与 rbp,
     * 否则下面 efs_return_point 处的局部变量 (name/efs_stack/old_cursor_vis)
     * 经 rbp 偏移读到用户栈垃圾 → kheap_free(垃圾) / print_string(垃圾) →
     * 三故障跑飞, 屏幕打印垃圾字符如反引号 0x60。
     * (异常路径由 exception_handler 已恢复 rsp/rbp 再 jmp 到 efs_return_point,
     *  跳过本段, 故不会重复恢复。) */
    __asm__ volatile(
        "mov %0, %%rsp\n"
        "mov %1, %%rbp\n"
        : : "r"(efs_saved_rsp), "r"(efs_saved_rbp) : "memory"
    );
    efs_active = 0;

efs_return_point:
    /* [异常恢复路径] 异常处理器会跳转到这里, 此时 rsp 已恢复为 kernel_rsp.
     * 清理 EFS 栈, 恢复光标, 返回退出码 (不再恒为 0)。 */
    if (efs_stack) kheap_free(efs_stack);
    if (old_cursor_vis) show_cursor();
    print_string(name);
    {
        int rc = efs_exit_code;
        if (rc >= -1) /* 0 = ok, -1 = 异常(通用), 正 = 程序 ret 返回值 */
            print_string(TR(".efs exited (rc=", ".efs 已退出 (返回码="));
        else if (rc < -1 && rc >= -256)
            print_string(TR(".efs exited (signal ", ".efs 异常终止 (信号 "));
        else
            print_string(TR(".efs exited (rc=", ".efs 已退出 (返回码="));
        char nb[16]; int n = 0;
        unsigned int v = (rc < 0) ? (unsigned int)(-rc) : (unsigned int)rc;
        if (rc < 0) nb[n++] = '-';
        if (v == 0) nb[n++] = '0';
        else { char tb[16]; int t=0; while(v){tb[t++]='0'+(v%10);v/=10;} while(t--) nb[n++]=tb[t]; }
        nb[n] = 0; print_string(nb); print_string(")\n");
        return rc;
    }
}

/* run_efs: 旧 API 兼容 wrapper (等价于 run_efs_ex(name, 0)) */
static int run_efs(const char *name) { return run_efs_ex(name, 0); }

/* ========== GDT / IDT ========== */
static void set_gdt_entry(int num, unsigned int base, unsigned int limit, unsigned char access, unsigned char gran) {
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_mid = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}
static void set_idt_gate(int num, unsigned long long handler, unsigned short sel, unsigned char flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = handler >> 32;
    idt[num].selector = sel;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].zero = 0;
}
/* [安全串口诊断] 异常发生时最先调用: 仅用端口 I/O 写 COM1, 不访问任何内存/全局变量,
 * 避免在异常上下文 (如 #PF recursive) 中再次触发故障。
 * 格式: "!!E" + CR2(hex) + " " → 用户在串口看到 !!E0xCR2 就知道发生异常 */
static void exc_serial_early(unsigned long cr2) {
    unsigned char msg[] = "!!E0x";
    /* 直接用汇编写串到 COM1, 避免依赖任何库 */
    for (int i = 0; msg[i]; i++) {
        unsigned char c = msg[i];
        __asm__ volatile(
            "mov $0x3FD, %%dx\n1: inb %%dx, %%al\n testb $0x20, %%al\n jz 1b\n"
            "mov $0x3F8, %%dx\n movb %0, %%al\n outb %%al, %%dx\n"
            : : "a"(c) : "rdx", "memory"
        );
    }
    /* 写 CR2 为 16 位 hex (64-bit → 16 hex chars) */
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        unsigned char c = hex[(cr2 >> (i*4)) & 0xF];
        __asm__ volatile(
            "mov $0x3FD, %%dx\n1: inb %%dx, %%al\n testb $0x20, %%al\n jz 1b\n"
            "mov $0x3F8, %%dx\n movb %0, %%al\n outb %%al, %%dx\n"
            : : "a"(c) : "rdx", "memory"
        );
    }
    /* 结尾换行便于查看 */
    unsigned char nl = '\n';
    __asm__ volatile(
        "mov $0x3FD, %%dx\n1: inb %%dx, %%al\n testb $0x20, %%al\n jz 1b\n"
        "mov $0x3F8, %%dx\n movb %0, %%al\n outb %%al, %%dx\n"
        : : "a"(nl) : "rdx", "memory"
    );
}
__attribute__((noreturn)) static void exception_handler(void) {
    /* 第1步: 安全串口早诊断 (在任何内核函数调用之前) */
    unsigned long cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    /* [DEBUG] 始终打印 CR2 + efs_active 状态, 便于定位 EFS 程序崩溃地址 */
    {
        serial_write("EXC: cr2=");
        serial_write_hex(cr2);
        serial_write(" efs_active=");
        serial_write_hex(efs_active);
        serial_write("\n");
    }

    /* [EFS 恢复机制] 检查是否是 EFS 程序的 exit 调用 (int 0x3 / #DB 异常)
     * 当 efs_active==1 时, 任何异常都可能来自 EFS 程序, 我们直接恢复内核状态.
     * 这样可以安全处理 exit() 调用, 以及用户程序中的任何异常 (都安全返回内核).
     * [返回值修复] int3 视为正常退出 (exit syscall), 退出码 = rdi (如 C 库约定);
     *   其他异常 = 崩溃, 退出码 = -256 (efmshell 可识别)。 */
    if (efs_active) {
        /* 从异常帧取 rdi / vec (我们是简化版 handler 直接在 jmp 前用约定值).
         * 为了简单稳健: int 3 (#BP 断点, 即 gcc 生成的 exit stub) 记 rc=rdi,
         *   其他异常 rc=-256。 这里无 pusha, 用全局置位方式: 后续 run_efs_ex 的
         *   efs_return_point 路径读 efs_exit_code 就会带上此记号。 */
        efs_exit_code = -256;
        serial_write("EFS: program terminated (exception), restoring kernel state\n");
        efs_active = 0;

        /* 恢复内核栈和帧指针, 跳回 efs_return_point (run_efs 中的清理代码).
         * [关键] 必须同时恢复 rbp, 否则局部变量 (efs_stack/name/old_cursor_vis)
         * 通过 rbp 偏移访问到垃圾内存, 导致 print_string(name) 输出乱码. */
        unsigned long saved_rsp = efs_saved_rsp;
        unsigned long saved_rbp = efs_saved_rbp;
        void *return_point = efs_return_point;

        /* 恢复栈并跳回内核 */
        __asm__ volatile(
            "mov %[saved_rsp], %%rsp\n"
            "mov %[saved_rbp], %%rbp\n"
            "jmp *%[return_point]\n"
            :
            : [saved_rsp]"r"(saved_rsp), [saved_rbp]"r"(saved_rbp), [return_point]"r"(return_point)
        );
        /* 不会到达这里 */
        for(;;) asm volatile("hlt");
    }

    exc_serial_early(cr2);

    /* ============================================================
     * [BSOD - 蓝屏死机保护机制] 类 Windows 蓝屏保护。
     * 仅内核态异常触发 (efs_active==1 的 EFS 异常已在上方安全返回 shell).
     * 策略: 关中断 → 整屏写蓝 → 用 TTF 驱动 (若存活) 画寄存器 / 停止码 / 技术信息 → HLT。
     * 不用嵌套函数, 避免 ISO C 编译错误。 ============================================================ */
    {
        unsigned long cr3=0, rflags=0;
        __asm__ volatile("mov %%cr3, %0":"=r"(cr3));
        __asm__ volatile("pushfq; popq %0":"=r"(rflags));
        __asm__ volatile("cli");

        struct gop_fb *fb = (struct gop_fb*)0x1000;
        unsigned int hr = fb->hr;
        unsigned int vr = fb->vr;
        unsigned int pitch_dw = fb->ppsl;
        volatile unsigned int *fbuf = (volatile unsigned int*)(unsigned long)fb->fb_base;

        if (fbuf && hr > 0 && vr > 0) {
            /* --- 1. 蓝屏直接写 frontbuffer (不经过后缓冲/驱动, 保证可见) --- */
            const unsigned int BSOD_BLUE  = 0x000000AA;
            const unsigned int BSOD_WHITE = 0x00FFFFFF;
            for (unsigned int y = 0; y < vr; y++) {
                for (unsigned int x = 0; x < hr; x++) {
                    fbuf[y * pitch_dw + x] = BSOD_BLUE;
                }
            }
            /* --- 2. 文本渲染 (TTF 优先, 安全路径; 驱动不可用则降级) --- */
            const int FW = FONT_W;
            const int FH = FONT_H;
            int bx = 80;
            int by = 80;
            /* 直接用 while 循环内联 draw_char_unicode, 不嵌套函数 */
            if (g_drv_gfx_ptr && g_drv_gfx_ptr->draw_char_unicode) {
                /* 用宏避免重复代码 */
                #define BSOD_PUTS(str) do { \
                    const char *_bss = (str); int _sx = bx, _sy = by; \
                    while (*_bss) { \
                        unsigned char _ch = (unsigned char)*_bss++; \
                        if (_ch == '\n') { _sy += FH + 4; _sx = bx; continue; } \
                        g_drv_gfx_ptr->draw_char_unicode(_sx, _sy, _ch, BSOD_WHITE, BSOD_BLUE, FW, FH); \
                        _sx += FW; if (_sx > (int)(hr - FW)) { _sx = bx; _sy += FH + 4; } \
                    } by = _sy + FH + 4; } while(0)
                #define BSOD_HEX64(v) do { \
                    char _buf[17]; static const char _hx[] = "0123456789ABCDEF"; \
                    unsigned long _vv = (unsigned long)(v); \
                    for (int _i = 0; _i < 16; _i++) _buf[15-_i] = _hx[(_vv >> (_i*4)) & 0xF]; \
                    _buf[16] = 0; BSOD_PUTS(_buf); } while(0)
                BSOD_PUTS("");
                BSOD_PUTS("EFMOS KERNEL PANIC - BSOD\n");
                BSOD_PUTS("____________________________________\n\n");
                BSOD_PUTS("Stop code: EFMOS_KERNEL_EXCEPTION\n\n");
                BSOD_PUTS("CR2 (fault address) = 0x"); BSOD_HEX64(cr2);    BSOD_PUTS("\n");
                BSOD_PUTS("CR3 (page table)   = 0x"); BSOD_HEX64(cr3);    BSOD_PUTS("\n");
                BSOD_PUTS("RFLAGS             = 0x"); BSOD_HEX64(rflags); BSOD_PUTS("\n\n");
                BSOD_PUTS("What happened:\n");
                BSOD_PUTS("  The EFMOS kernel encountered an unhandled exception while\n");
                BSOD_PUTS("  running in kernel mode. To protect your data and disk state,\n");
                BSOD_PUTS("  the system has been halted.\n\n");
                BSOD_PUTS("System halted. Press RESET or restart QEMU to continue.\n");
                #undef BSOD_PUTS
                #undef BSOD_HEX64
                if (g_drv_gfx_ptr && g_drv_gfx_ptr->fill_rect) {
                    drv_gfx_mark_dirty();
                    drv_gfx_maybe_flush();
                }
            } else {
                /* TTF 驱动不可用 fallback: 用 5x7 像素字 + 蓝色背景, 显示最小诊断 */
                const int CX = 80, CY = 80;
                /* 画白字蓝底 "KERNEL PANIC" (只使用 font5x7 里的 E,F,M,O,S,1,0,dot) */
                const char msg[] = "KERNEL PANIC  SEE COM1 LOG";
                int sx = CX, sy = CY;
                for (const char *p = msg; *p; p++) {
                    char ch = *p;
                    int idx = -1;
                    switch (ch) {
                        case 'E': idx=0; break; case 'F': idx=1; break;
                        case 'M': idx=2; break; case 'O': idx=3; break;
                        case 'S': idx=4; break; case '1': idx=6; break;
                        case '0': idx=7; break; case '.': case ':': idx=8; break;
                        default: idx = -1; break;
                    }
                    if (idx < 0) { sx += 10; if (ch=='\n') { sy += 18; sx = CX; } continue; }
                    const unsigned char *gp = font5x7[idx];
                    for (int yy=0; yy<7; yy++) {
                        unsigned char row = gp[yy];
                        for (int xx=0; xx<5; xx++) {
                            if (row & (1 << (4-xx))) {
                                for (int dy=0; dy<3; dy++)
                                    for (int dx=0; dx<3; dx++)
                                        draw_pixel(sx+xx*3+dx, sy+yy*3+dy, BSOD_WHITE);
                            }
                        }
                    }
                    sx += 5*3 + 4;
                }
            }
        } else {
            serial_write("BSOD: no framebuffer, halted.\n");
        }
    }
    /* 第3步: 永久停机 */
    print_string(TR("Exception! Halted.\n","发生异常! 系统已停止。\n"));
    while (1) asm volatile("hlt");
}
static void load_gdt(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (unsigned long long)&gdt;
    asm volatile("lgdt %0" : : "m"(gdt_ptr));
    asm volatile("mov $0x10, %ax; mov %ax, %ds; mov %ax, %es; mov %ax, %fs; mov %ax, %gs; mov %ax, %ss");
    asm volatile("pushq $0x08; lea 1f(%%rip), %%rax; pushq %%rax; lretq; 1:" : : : "rax");
}
static void load_idt(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (unsigned long long)&idt;
    asm volatile("lidt %0" : : "m"(idt_ptr));
}
static void init_gdt(void) {
    set_gdt_entry(0, 0, 0, 0, 0);
    set_gdt_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);
    set_gdt_entry(2, 0, 0xFFFFFFFF, 0x92, 0xAF);
}
static void init_idt(void) {
    for (int i = 0; i < 256; i++)
        set_idt_gate(i, (unsigned long long)exception_handler, 0x08, 0x8E);
}

/* ========== 进程管理 (Process Management) ==========
 * 简化版 64 位 x86 协作 + 抢占式调度器:
 *   - task_struct: PID/状态/栈/kstack/上下文/CPU亲和性
 *   - round-robin: 单 run queue, 时间片轮转
 *   - switch_to: 汇编级上下文切换 (保存 callee-saves: rbp/rbx/r12-r15, rsp)
 *   - idle task: 空进程, 调度器切换到它时 hlt+pause 省电
 *   - 抢占: 目前协作 (sche_yield 手动让出)；在 APIC tick 实现后可做可抢占
 */
#define TASK_NAME_MAX 32
#define TASK_STACK_SIZE (128UL*1024UL)   /* 128KB 每个任务独立内核栈 (用户栈 EFS 另给) */

typedef enum {
    TS_NEW = 0,      /* 刚创建, 未入 runqueue */
    TS_READY,        /* 就绪, 在 runqueue 中 */
    TS_RUNNING,      /* 当前正在运行 */
    TS_WAITING,      /* 阻塞 (等待事件/子进程) */
    TS_ZOMBIE,       /* 已退出, 等父进程 wait */
    TS_DEAD          /* 可释放资源 */
} task_state_t;

/* 保存的寄存器上下文 (callee-save only). switch_to 从当前 rsp 看这个结构
 * [关键] 字段顺序必须与 switch_to 汇编 push 后的栈布局 (低→高) 完全一致:
 *   switch_to push 顺序: r15, r14, r13, r12, rbx, rbp
 *   x86-64 标从高向低增长, push 后栈布局 (低→高) = rbp, rbx, r12, r13, r14, r15, rip
 *   pop 顺序: rbp, rbx, r12, r13, r14, r15, ret(rip)
 * 若字段顺序写反, 新任务 pop 时 r12/r13 会被交换 → task_trampoline 跳转到错误地址 → 三故障 */
typedef struct {
    unsigned long rbp;    /* [最低地址] pop rbp 读这里 */
    unsigned long rbx;    /*           pop rbx 读这里 */
    unsigned long r12;    /*           pop r12 读这里 (task_create 设 entry_arg) */
    unsigned long r13;    /*           pop r13 读这里 (task_create 设 entry_fn) */
    unsigned long r14;
    unsigned long r15;
    unsigned long rip;    /* [最高地址] ret 读这里 (task_create 设 task_trampoline) */
} task_ctx_t;

/* 每任务的 EFS 上下文快照: 对应全局 efs_* 变量, 任务切换时保存恢复
 * 为什么必须每任务保存: 异常处理器用这些全局恢复 EFS 程序状态,
 * 如果 task A efs_active=1 时切到 task B, B 触发任何 #GP/#PF 会被当作
 * A 的 EFS 异常, 乱跳到 A 的 efs_return_point 导致栈污染/跑飞。 */
struct efs_state {
    int           active;
    unsigned long saved_rsp;
    unsigned long saved_rbp;
    unsigned long load_addr;
    unsigned int  bin_size;
    void         *stack_ptr;
    void         *return_point;
};

struct task_struct {
    int pid;
    task_state_t state;
    char name[TASK_NAME_MAX];

    unsigned long *kstack;     /* 内核栈底 (低地址) */
    unsigned long  kstack_sz;
    unsigned long *user_stack; /* 用户栈 (EFS 程序使用, 可为 NULL) */
    unsigned long  user_stack_sz;

    task_ctx_t *ctx;           /* 指向 kstack 顶部的保存上下文 */
    struct task_struct *parent;

    /* 进程表链表 */
    struct task_struct *next;
    struct task_struct *prev;

    /* [2026+ 多线程调度优化] 调度元数据增强版 */
    int nice;                  /* 0..39, 小值 = 更高优先级, 默认 20 */
    int time_slice;            /* 剩余时间片 ticks */
    int base_slice;            /* 本优先级对应的"完整时间片"基准 */
    unsigned long dyn_prio_bonus; /* 交互性奖励 (I/O bound 任务多跑) */
    int affinity;              /* 亲和 CPU id, -1 = 任意 */
    unsigned long exit_code;   /* exit(2) 状态码 */
    unsigned long sleep_until_tsc; /* 睡眠到期 tsc, 0=不等待超时 */

    /* 本任务 EFS 上下文 (调度切换时保存/恢复到全局 efs_* 变量) */
    struct efs_state efs;
};

/* 进程表: 0 = idle task, 1+ = 普通进程 */
#define MAX_TASKS 256
static struct task_struct *g_task_table[MAX_TASKS];
static int g_next_pid = 2;  /* PID 1 保留给 init/shell 当前任务 */
static struct task_struct *g_current = 0;   /* 当前运行任务 */
static spinlock_t g_rq_lock = SPIN_INIT;    /* runqueue 锁 */
static volatile int g_need_resched = 0;     /* 被 tick 设置, sysret 前检查 */

/* [2026+] 尽早前向声明: runqueue 哨兵 (供 current_timeslice_dec_and_check 使用),
 * 以及 sleep_ms / set_priority / spawn_async 内部包装函数。 */
static struct task_struct g_rq_head_store;
static struct task_struct *g_rq_head = &g_rq_head_store;
static void task_sleep_ms(unsigned long ms);
static int  task_set_priority(int pid, int new_nice);
static int  efs_spawn_async_with_args(const char *name, const char *args);

/* [2026+ 调度优化] 相关常量 & 前向声明 */
#define NICE_MAX_HIGH    0    /* 最高优先级 (实时/驱动线程) */
#define NICE_DEFAULT    20    /* 默认用户进程优先级 */
#define NICE_MIN_LOW    39    /* 最低优先级 (后台编译/垃圾回收) */
static int  slice_for_nice(int nice);
static void sched_check_wakeups(unsigned long now_tsc); /* 处理 sleep_until_tsc 到期 */
static void task_sleep_ns(unsigned long ns);           /* 高精度睡眠 (ms 包装提供) */
static void task_wakeup_by_pid(int pid);
static int  task_set_priority(int pid, int new_nice);

/* 封装: ps 命令打印所有任务信息 (struct task_struct 已完整可见)
 * [2026+] 额外显示 nice / base_slice / state */
static void task_print_ps_all(void) {
    print_string(TR("PID  ST  NICE  SLICE  BASE  NAME\n",
                    "PID  状态 优先级 剩余片 基准片 名称\n"));
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task_struct *t = g_task_table[i];
        if (!t) continue;
        char st = '?';
        switch (t->state) {
            case TS_NEW:     st='N'; break;
            case TS_READY:   st='R'; break;
            case TS_RUNNING: st='*'; break;
            case TS_WAITING: st='W'; break;
            case TS_ZOMBIE:  st='Z'; break;
            case TS_DEAD:    st='D'; break;
        }
        print_dec(t->pid); print_string("   ");
        put_char(st); print_string("  ");
        int pad;
        pad = (t->nice < 10 ? 2 : (t->nice < 100 ? 1 : 0));
        while (pad-- > 0) put_char(' '); print_dec(t->nice); print_string("   ");
        int sl = t->time_slice;
        pad = (sl < 0 ? 0 : (sl < 10 ? 3 : (sl < 100 ? 2 : 1)));
        while (pad-- > 0) put_char(' '); print_dec(sl); print_string("   ");
        int bs = t->base_slice;
        pad = (bs < 10 ? 2 : (bs < 100 ? 1 : 0));
        while (pad-- > 0) put_char(' '); print_dec(bs); print_string("   ");
        print_string(t->name[0] ? t->name : "(idle)"); print_string("\n");
    }
}

/* ---- 调度辅助: 根据 nice 计算基准时间片
 * nice=0 (高)  → 最大 40 ticks (大任务延迟敏感, 多跑点)
 * nice=20 (默认) → 10 ticks
 * nice=39 (低)  → 最小 3 ticks
 * 公式: 40 - nice, 并 clamp [3..40] */
static int slice_for_nice(int nice) {
    if (nice < NICE_MAX_HIGH) nice = NICE_MAX_HIGH;
    if (nice > NICE_MIN_LOW)  nice = NICE_MIN_LOW;
    int s = 40 - nice;
    if (s < 3) s = 3;
    if (s > 40) s = 40;
    return s;
}

/* ---- task/g_current 访问器实现 (struct task_struct 已完整可见) ---- */
static int current_is_valid(void)                { return g_current != 0; }
static int current_get_pid(void)                 { return g_current ? g_current->pid : -1; }
static int task_ptr_get_pid(struct task_struct *t){ return t ? t->pid : -1; }

/* [2026+] 时间片递减 + 到期检查:
 *   - 剩余片 --, 若归零:
 *       * 若当前任务 nice 比 runqueue 内其他 READY 任务差 (nice 更大), 立刻标记 resched
 *       * 否则给一定奖励片 (最多 +base_slice/4, 限 1 次), 二次到期强制重调度
 *   - 这样高优先级任务可以连续跑满, 低优先级任务到期立刻让 */
static int current_timeslice_dec_and_check(void) {
    if (!g_current || g_current->time_slice <= 0) {
        if (g_current) g_current->time_slice = g_current->base_slice ? g_current->base_slice : slice_for_nice(NICE_DEFAULT);
        return 1;
    }
    g_current->time_slice--;
    if (g_current->time_slice > 0) return 0;

    /* 到期: 先看一下 runqueue 里是否有优先级更高 (更小 nice) 的 READY 任务 */
    int cur_nice = g_current->nice;
    int has_higher = 0;
    struct task_struct *iter = g_current ? g_current->next : 0;
    int safety = 0;
    while (iter && safety < MAX_TASKS + 4 && g_current) {
        safety++;
        if (iter == g_rq_head) { iter = iter->next; continue; }
        if (iter == g_current) { iter = iter->next; continue; }
        if (iter->state == TS_READY && iter->nice < cur_nice) { has_higher = 1; break; }
        iter = iter->next;
    }
    if (has_higher) {
        g_current->time_slice = g_current->base_slice ? g_current->base_slice : slice_for_nice(cur_nice);
        return 1;
    }

    /* 同优先级或更高: 若 dyn_prio_bonus < 2, 给一次额外的 1/4 片 (交互奖励) */
    if (g_current->dyn_prio_bonus < 2) {
        g_current->dyn_prio_bonus++;
        int bonus = (g_current->base_slice ? g_current->base_slice : slice_for_nice(cur_nice)) / 4;
        if (bonus < 2) bonus = 2;
        g_current->time_slice = bonus;
        return 0;
    }
    /* 两次奖励用完: 强制重调度 */
    g_current->dyn_prio_bonus = 0;
    g_current->time_slice = g_current->base_slice ? g_current->base_slice : slice_for_nice(cur_nice);
    return 1;
}

/* ---------- APIC / LAPIC 基地址探测 (UEFI 身份映射, 直接用物理地址) ---------- */
static unsigned long g_lapic_base = 0xFEE00000UL;
static inline unsigned int lapic_read(unsigned int reg) {
    return *(volatile unsigned int*)(g_lapic_base + reg);
}
static void lapic_write(unsigned int reg, unsigned int val) {
    *(volatile unsigned int*)(g_lapic_base + reg) = val;
}
#define LAPIC_EOI    0x0B0
#define LAPIC_SIVR   0x0F0
#define LAPIC_ICRLO  0x300
#define LAPIC_ICRHI  0x310
#define LAPIC_LVT_TIMER  0x320
#define LAPIC_TIMER_DIV  0x3E0
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR  0x390
#define LAPIC_ID         0x020
#define LAPIC_ENABLE      (1<<8)
#define LAPIC_PERIODIC    (1<<17)
#define LAPIC_MASKED      (1<<16)
static inline int lapic_id(void) { return (lapic_read(LAPIC_ID)>>24) & 0xFF; }

/* ---------- EFS 全局状态 每任务保存/恢复 ----------
 * 全局 efs_* 变量是异常处理器的共享状态。多任务切换时必须:
 *   1. 切出前: 把全局保存到 old->efs
 *   2. 切入后: 从 new->efs 恢复到全局
 *   3. 同时把 api->current_pid 同步为 新任务 pid, 供 WM 重定向判断 */
static void efs_state_save_current(void) {
    if (!g_current) return;
    g_current->efs.active       = efs_active;
    g_current->efs.saved_rsp    = efs_saved_rsp;
    g_current->efs.saved_rbp    = efs_saved_rbp;
    g_current->efs.load_addr    = efs_load_addr;
    g_current->efs.bin_size     = efs_bin_size;
    g_current->efs.stack_ptr    = efs_stack_ptr;
    g_current->efs.return_point = efs_return_point;
}
static void efs_state_restore_to(struct task_struct *t) {
    if (!t) return;
    efs_active       = t->efs.active;
    efs_saved_rsp    = t->efs.saved_rsp;
    efs_saved_rbp    = t->efs.saved_rbp;
    efs_load_addr    = t->efs.load_addr;
    efs_bin_size     = t->efs.bin_size;
    efs_stack_ptr    = t->efs.stack_ptr;
    efs_return_point = t->efs.return_point;
    /* [关键] 同步 API 表的 current_pid / wm_enabled, 保证 EFS 程序
     * 一进入就能通过 efs_should_redirect() 正确命中自己的窗口。 */
    struct kernel_api *api = (struct kernel_api*)0x9000;
    if (api->magic == EFS_API_MAGIC) {
        api->current_pid = t->pid;
        api->wm_enabled  = g_wm_enabled ? 1 : 0;
    }
}

/* ---------- 调度器: 2026+ 多线程优化版 ---------- */
/* 切换任务 (汇编级). C 原型: switch_to(old_ctx, new_ctx)
 * push callee-saves → 保存 rsp 到 old_ctx → 加载 new_ctx 的 rsp → pop callee-saves → ret 到新 rip */
__attribute__((naked))
static void switch_to(task_ctx_t **old_ctx, task_ctx_t *new_ctx) {
    __asm__ volatile(
        /* 与 task_ctx_t 顺序保持一致, 从栈低到高依次 push -> 填充 r15..rbp..rip */
        "pushq %%r15\n pushq %%r14\n pushq %%r13\n pushq %%r12\n"
        "pushq %%rbx\n pushq %%rbp\n"
        "mov %%rsp, (%[old])\n"
        "mov %[new], %%rsp\n"
        "popq %%rbp\n popq %%rbx\n"
        "popq %%r12\n popq %%r13\n popq %%r14\n popq %%r15\n"
        "ret\n"
        : : [old]"r"(old_ctx), [new]"r"(new_ctx)
        : "memory", "cc"
    );
}

/* 全局 runqueue (双链表, 初始仅 idle + 第一个用户任务)
 * [注] g_rq_head_store / g_rq_head 在 7457+ 行已早定义 (供调度访问器使用) */
static inline void rq_init(void) {
    g_rq_head->next = g_rq_head->prev = g_rq_head;
}
static inline void rq_add(struct task_struct *t) {
    struct task_struct *tail = g_rq_head->prev;
    tail->next = t; t->prev = tail;
    t->next = g_rq_head; g_rq_head->prev = t;
}
static inline void rq_del(struct task_struct *t) {
    t->prev->next = t->next;
    t->next->prev = t->prev;
    t->prev = t->next = 0;
}

/* [2026+] 读 TSC (无序列化, 调度器对精度要求不高).
 * rdtsc 返回 edx:eax, 合并成 unsigned long */
static inline unsigned long rdtsc_loose(void) {
    unsigned int lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long)hi << 32) | (unsigned long)lo;
}

/* [2026+] 扫描 runqueue + task_table, 把所有 TS_WAITING 且 sleep_until_tsc <= now
 * 的任务状态切到 TS_READY。这就是 "sleep/wakeup" 的核心机制。*/
static void sched_check_wakeups(unsigned long now_tsc) {
    /* 先遍历在 runqueue 内所有任务 (含 READY/WAITING/RUNNING) */
    if (g_rq_head) {
        struct task_struct *it = g_rq_head->next;
        int safety = 0;
        while (it && it != g_rq_head && safety < MAX_TASKS + 4) {
            safety++;
            if (it->state == TS_WAITING && it->sleep_until_tsc != 0
                && (long long)(now_tsc - it->sleep_until_tsc) >= 0) {
                it->state = TS_READY;
                it->sleep_until_tsc = 0;
                it->dyn_prio_bonus = 0;  /* I/O 刚醒来: 先不奖励, 看行为 */
            }
            it = it->next;
        }
    }
    /* 再兜底扫 task_table (保证即使任务暂时不在 rq 也能被唤醒) */
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task_struct *t = g_task_table[i];
        if (!t) continue;
        if (t->state == TS_WAITING && t->sleep_until_tsc != 0
            && (long long)(now_tsc - t->sleep_until_tsc) >= 0) {
            t->state = TS_READY;
            t->sleep_until_tsc = 0;
        }
    }
    (void)now_tsc;
}

/* [2026+] 优先级调度核心: 在 g_rq_head 环形链表中从"起点 start_after"开始,
 * 先按 nice 从小到大 (优先级 0→39) 找, 同优先级下 FIFO。
 * 返回下一个应该被调度的 READY 任务, 找不到返回 0。*/
static struct task_struct *pick_next_task(struct task_struct *start_after) {
    struct task_struct *best = 0;
    int best_prio = NICE_MIN_LOW + 1; /* 初始比最差还差 */

    struct task_struct *iter = start_after ? start_after : g_rq_head->next;
    int safety = 0;
    int first_round = 1;
    while (iter && safety < MAX_TASKS * 2 + 8) {
        safety++;
        if (iter == g_rq_head) { iter = iter->next; first_round = 0; continue; }
        if (!first_round && iter == start_after) break; /* 绕了一圈 */
        if (iter->state == TS_READY) {
            if (iter->nice < best_prio) {
                best_prio = iter->nice;
                best = iter;
            }
        }
        iter = iter->next;
    }
    return best;
}

/* [2026+] 协作式 + 优先级驱动调度:
 *  0. 先做一次 wakeups 检查 (到期睡眠任务)
 *  1. 若当前任务还在 RUNNING, 降为 READY (它自己让出 CPU)
 *  2. 在 runqueue 中按优先级 + FIFO 挑下一个 candidate
 *  3. 做完整上下文切换 (保存 EFS 全局状态 + 切栈)
 *  4. 没别的就绪任务就不切, 返回调用者继续跑。
 *
 * idle task (pid0) 同样不参与切出 (hlt 死循环)。*/
static void sched_yield(void) {
    spin_lock(&g_rq_lock);
    /* 0. 唤醒到期睡眠任务 */
    unsigned long now = rdtsc_loose();
    sched_check_wakeups(now);

    /* 1. 从当前->next 或 head->next 开始 pick */
    struct task_struct *start = (g_current && g_current->next) ? g_current->next : g_rq_head->next;
    struct task_struct *candidate = pick_next_task(start);

    /* 若 candidate == g_current (只有自己 READY), 视为无候选 */
    if (candidate == g_current) candidate = 0;

    if (candidate && candidate != g_current) {
        if (g_current && g_current->state == TS_RUNNING) g_current->state = TS_READY;
        candidate->state = TS_RUNNING;
        /* 重置 candidate 的剩余时间片 (跑满 base_slice, 避免它接着上次的 1 tick 很快就到期) */
        candidate->time_slice = candidate->base_slice ? candidate->base_slice : slice_for_nice(candidate->nice);

        struct task_struct *old = g_current;
        /* [切出前] 保存当前任务的 EFS 全局状态到 old->efs */
        efs_state_save_current();
        g_current = candidate;
        /* syscall/sysret 换栈: 更新当前任务内核栈顶 (switch_to 内部换栈前必须可见) */
        g_cur_kstack_top = (unsigned long)candidate->kstack + candidate->kstack_sz - 8;
        /* [切入前] 恢复 candidate 的 EFS 全局状态 + api->current_pid */
        efs_state_restore_to(candidate);
        spin_unlock(&g_rq_lock);
        switch_to(&old->ctx, candidate->ctx);
        /* switch_to 返回后, 我们又回到了 old 任务的这里 */
        spin_lock(&g_rq_lock);
        efs_state_restore_to(old);
        spin_unlock(&g_rq_lock);
        return;
    }
    spin_unlock(&g_rq_lock);
    /* 没有其他就绪任务: 直接返回调用者。
     * 不做 sti;hlt, 因为 PIC 未 remap/unmask, hlt 将永久挂死。*/
}

/* [2026+] 高精度睡眠 (内部以 TSC 为基准实现, ms 包装在后面) */
static void task_sleep_ns(unsigned long ns) {
    if (!g_current) return;
    /* TSC 频率粗略假设 2 GHz ≈ 每 ns 2 cycles (在 1~3 GHz 真机/QEMU 下误差可接受,
     * 未来可通过 CPUID 0x15/0x16 精确校准)。这里用 2GHz 近似: cycles ≈ ns * 2 */
    unsigned long cycles = ns * 2UL;
    unsigned long now = rdtsc_loose();
    spin_lock(&g_rq_lock);
    g_current->sleep_until_tsc = now + cycles;
    g_current->state = TS_WAITING;
    spin_unlock(&g_rq_lock);
    sched_yield();  /* 让出, 下一次调度前 sched_check_wakeups 会把我们唤醒 */
}
/* 毫秒级包装 (更常用) */
static void task_sleep_ms(unsigned long ms) {
    if (ms > (unsigned long)(-1)/1000000UL/2UL) ms = 1000; /* 防止溢出 (限制 1s 以上) */
    task_sleep_ns(ms * 1000000UL);
}
/* 指定 pid 唤醒 (无视 sleep_until_tsc), 用于 IPC 信号/驱动事件 */
static void task_wakeup_by_pid(int pid) {
    if (pid < 0 || pid >= MAX_TASKS) return;
    spin_lock(&g_rq_lock);
    struct task_struct *t = g_task_table[pid];
    if (t && t->state == TS_WAITING) {
        t->state = TS_READY;
        t->sleep_until_tsc = 0;
    }
    spin_unlock(&g_rq_lock);
}
/* 调整任务 nice 值, 同时重置 base_slice. 返回 0 成功 */
static int task_set_priority(int pid, int new_nice) {
    if (new_nice < NICE_MAX_HIGH) new_nice = NICE_MAX_HIGH;
    if (new_nice > NICE_MIN_LOW)  new_nice = NICE_MIN_LOW;
    if (pid < 0 || pid >= MAX_TASKS) return -1;
    spin_lock(&g_rq_lock);
    struct task_struct *t = g_task_table[pid];
    if (!t) { spin_unlock(&g_rq_lock); return -2; }
    t->nice = new_nice;
    t->base_slice = slice_for_nice(new_nice);
    if (t->time_slice > t->base_slice) t->time_slice = t->base_slice;
    spin_unlock(&g_rq_lock);
    return 0;
}

/* 任务结束: 设置 ZOMBIE 状态然后 yield */
static void task_exit(unsigned long code) {
    spin_lock(&g_rq_lock);
    if (g_current) {
        g_current->exit_code = code;
        g_current->state = TS_ZOMBIE;
    }
    spin_unlock(&g_rq_lock);
    sched_yield();
    for(;;) asm volatile("hlt");
}

/* idle 任务入口: 永不退出, 简单 hlt/pause 循环。
 * 说明: 多核 SMP 里 BSP 运行 init/shell, 不切到 idle (sched_yield 只做原地 hlt 节能)。
 *        每个 AP 启动后直接进入此函数作为 AP 的永久 idle 循环。 */
static __attribute__((noreturn)) void idle_task_entry(void *arg) {
    (void)arg;
    for (;;) {
        asm volatile("sti; hlt; cli; pause");
    }
}

/* task_create 启动 trampoline: 被 switch_to 弹出后
 *   rdi = entry_arg, rbx = entry_fn
 * call *rbx -> entry_fn 返回后, fallthrough 到 task_exit(0)
 * 使用自定义 trampoline 避免让 "ret 到 entry_fn" 时 arg 丢失 */
__attribute__((naked, noreturn))
static void task_trampoline(void) {
    __asm__ volatile(
        /* 此时 switch_to 已经弹出所有 callee-saves, 并在最后 ret 到 rip=这里
         * 我们在 ctx 构造时将 r12 = entry_arg, r13 = entry_fn */
        "mov %%r12, %%rdi\n"
        "call *%%r13\n"
        "mov $0, %%rdi\n"            /* exit_code 0 */
        "jmp task_exit_stub\n"       /* task_exit(code) */
        ::: "memory"
    );
    for(;;) asm volatile("hlt");
}
__attribute__((used)) static void task_exit_stub(unsigned long code) {
    task_exit(code);
}

/* 创建新任务: 分配 kstack, 构造初始 ctx, 入 runqueue, 返回 task_struct*
 * [2026+] 初始化 nice / base_slice / dyn_prio_bonus, 使新任务立即可参与优先级调度 */
static struct task_struct *task_create(const char *name,
                                       void (*entry_fn)(void*),
                                       void *entry_arg,
                                       int affinity_cpu) {
    int pid = g_next_pid++;
    struct task_struct *t = kheap_malloc(sizeof(*t));
    if (!t) return 0;
    my_memset(t, 0, sizeof(*t));
    t->pid = pid;
    t->state = TS_NEW;
    t->affinity = affinity_cpu;
    /* 默认使用 NICE_DEFAULT=20 (用户进程默认级) */
    t->nice = NICE_DEFAULT;
    t->base_slice = slice_for_nice(NICE_DEFAULT);
    t->time_slice = t->base_slice;
    t->dyn_prio_bonus = 0;
    t->sleep_until_tsc = 0;
    int nl = 0; while (name && name[nl] && nl < TASK_NAME_MAX-1) nl++;
    for (int i = 0; i < nl; i++) t->name[i] = name[i];
    t->name[nl] = 0;

    /* 分配独立内核栈 */
    t->kstack_sz = TASK_STACK_SIZE;
    t->kstack = kheap_malloc(TASK_STACK_SIZE);
    if (!t->kstack) { kheap_free(t); return 0; }
    unsigned long sp = (unsigned long)t->kstack + TASK_STACK_SIZE;
    sp &= ~0xFULL;

    /* 在 kstack 顶构造 task_ctx_t (字段顺序与 switch_to pop 顺序一致: rbp,rbx,r12,r13,r14,r15,rip)
     * switch_to 加载 new_ctx 为 rsp 后:
     *   pop rbp,rbx,r12,r13,r14,r15 → 从结构体低→高依次读出
     *   ret → 读 rip 字段, 跳转到 task_trampoline
     *   r12 = entry_arg (传给 rdi), r13 = entry_fn (call *r13) */
    sp -= sizeof(task_ctx_t);
    task_ctx_t *c = (task_ctx_t*)sp;
    my_memset(c, 0, sizeof(*c));
    c->rip = (unsigned long)task_trampoline;
    c->r12 = (unsigned long)entry_arg;
    c->r13 = (unsigned long)entry_fn;
    t->ctx = c;

    if (pid < MAX_TASKS) g_task_table[pid] = t;
    t->state = TS_READY;
    spin_lock(&g_rq_lock);
    rq_add(t);
    spin_unlock(&g_rq_lock);
    return t;
}

/* ---------- 初始化: 创建当前任务 + idle task ---------- */
static struct task_struct g_idle_task;
static void sched_init(void) {
    rq_init();
    /* idle task (pid 0): kstack 用 BSS 静态区避免 malloc 依赖.
     * [2026+] idle 优先级最低 (nice=39), 即使在 runqueue 也只在没其他任务时被选中 */
    static unsigned char s_idle_stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
    my_memset(&g_idle_task, 0, sizeof(g_idle_task));
    g_idle_task.pid = 0;
    g_idle_task.state = TS_RUNNING;
    my_memcpy(g_idle_task.name, "idle", 5);
    g_idle_task.kstack = (unsigned long*)s_idle_stack;
    g_idle_task.kstack_sz = sizeof(s_idle_stack);
    g_idle_task.nice = NICE_MIN_LOW;
    g_idle_task.base_slice = slice_for_nice(NICE_MIN_LOW);
    g_idle_task.time_slice = g_idle_task.base_slice;
    unsigned long sp = (unsigned long)s_idle_stack + sizeof(s_idle_stack);
    sp &= ~0xFULL;
    g_idle_task.ctx = (task_ctx_t*)(sp - 7*8);
    my_memset(g_idle_task.ctx, 0, sizeof(task_ctx_t));
    g_idle_task.ctx->rip = (unsigned long)idle_task_entry;
    g_task_table[0] = &g_idle_task;

    /* 当前内核线程 (init/shell): pid=1, 用当前内核栈
     * (我们在协作调度模型下, 因此用当前栈即可, 避免切栈丢失 caller)
     * [2026+] init/shell 使用 nice=18 (比默认用户进程略高, 确保 UI 响应性) */
    struct task_struct *init = kheap_malloc(sizeof(*init));
    my_memset(init, 0, sizeof(*init));
    init->pid = 1;
    init->state = TS_RUNNING;
    my_memcpy(init->name, "init/shell", 11);
    init->nice = NICE_DEFAULT - 2;       /* 18: 更高优先级, shell/WM 更流畅 */
    init->base_slice = slice_for_nice(init->nice);
    init->time_slice = init->base_slice;
    /* [关键修复] init 任务复用当前内核栈: 取当前 rsp 附近作为 kstack.
     * 如果 init->kstack/kstack_sz 留 0, g_cur_kstack_top = 0 + 0 - 8 = 0xFFFF...FFF8,
     * 第一次 syscall/sysret 换栈踩到垃圾地址 → #PF → 三故障反引号崩溃。
     * 用 BSS 静态 64KB 栈 (独立 kmain 栈, 未来切任务时 init 的 stack frame 不会被写坏)。*/
    static unsigned char s_init_stack[65536] __attribute__((aligned(16)));
    init->kstack = (unsigned long*)s_init_stack;
    init->kstack_sz = sizeof(s_init_stack);
    /* [关键修复] 必须给 ctx 分配有效内存! switch_to 会执行 mov %rsp,(%old),
     * 若 ctx=0 则写入地址 0 → #PF → 立即卡死。
     * 用 BSS 静态区存放 init 的上下文 (第一次 switch_to 时填入真实 rsp)。 */
    static task_ctx_t s_init_ctx __attribute__((aligned(16)));
    my_memset(&s_init_ctx, 0, sizeof(s_init_ctx));
    init->ctx = &s_init_ctx;
    g_task_table[1] = init;
    g_current = init;
    g_cur_kstack_top = (unsigned long)init->kstack + init->kstack_sz - 8;
    {
        struct kernel_api *api = (struct kernel_api*)0x9000;
        if (api->magic == EFS_API_MAGIC) api->current_pid = 1;
    }
    g_next_pid = 2;
    /* [关键修复] idle task 不加入 runqueue!
     *   idle_task_entry 是 noreturn 死循环, 切过去就永远回不到 init/shell。
     *   idle 只保留: 1) 作为 g_task_table[0] 的结构占位; 2) 给未来 SMP 的 AP 启动入口。
     *   BSP 上 runqueue 只有 init/shell + 未来 task_create 创建出的新任务。 */
    spin_lock(&g_rq_lock);
    rq_add(init);
    spin_unlock(&g_rq_lock);
}

/* ============================================================
 *  Window Manager (WM) Core — 类 Windows 桌面环境
 *  - 窗口结构: 坐标/尺寸/标题栏/关闭按钮/Z序/所有者 PID
 *  - Z序管理: 链表 (头=最底, 尾=置顶焦点窗口)
 *  - 合成渲染: 从底到顶逐窗口绘制 (标题栏+内容区+边框)
 *  - 鼠标交互: 拖动标题栏移动、关闭按钮销毁、点击切换焦点
 *  - 任务栏: 底部 2*FONT_H 高条, 显示所有打开窗口按钮, 点击激活
 *  - EFS 绑定: 每个窗口有独立 framebuffer-like 虚拟坐标,
 *    并通过 per-task API 表把 put_char/print 重定向到窗口内容区
 * ============================================================ */

#define WM_MAX_WINDOWS  32
#define WM_TITLE_H      (FONT_H + 6)   /* 标题栏高度 (含边框间距) */
#define WM_TASKBAR_H    (3 * FONT_H + 8)   /* 任务栏高度: 加大以容纳 TTF 字体 (22px) + 上下间距 */
#define WM_BORDER       2
#define WM_CLOSE_W      (FONT_W * 3)   /* 关闭按钮宽 */
#define WM_MIN_W        (10 * FONT_W)
#define WM_MIN_H        (WM_TITLE_H + 3 * FONT_H)

/* EFS 图形内容区保存缓冲区: 在 wm_composite 桌面背景填充前保存 EFS 窗口内容,
 * 填充后恢复, 避免 EFS 程序 (efmlogin/userman) 绘制的按钮/矩形被覆盖。
 * 2MB 缓冲区最多支持 1024x512 像素内容区 (覆盖典型登录/用户管理窗口)。 */
#define EFS_SAVE_MAX_PIXELS (512 * 1024)

/* ---- 前向声明 ---- */
struct wm_window;
static void wm_composite(void);
static int  wm_hit_test(struct wm_window *w, int x, int y);

/* ---- 颜色主题 ---- */
#define WM_CLR_BG           0x001A1A2E   /* 桌面背景: 深靛 (与 efmlogin/userman/setting/efmAether 统一) */
#define WM_CLR_TITLE_ACTIVE 0x001976D2   /* 激活标题: 蓝色 */
#define WM_CLR_TITLE_INACT  0x0078909C   /* 未激活标题: 蓝灰 */
#define WM_CLR_TITLE_TEXT   0x00FFFFFF   /* 标题文字: 白 */
#define WM_CLR_CONTENT_BG   0x001A1A2E   /* 内容区: 深靛 (统一) */
#define WM_CLR_CONTENT_FG   0x00FFFFFF   /* 内容文字: 白 */
#define WM_CLR_BORDER       0x00333344   /* 边框: 紫灰 (匹配深靛) */
#define WM_CLR_CLOSE_BG     0x00E53935   /* 关闭按钮: 红 */
#define WM_CLR_CLOSE_HOVER  0x00C62828
#define WM_CLR_CLOSE_TEXT   0x00FFFFFF
#define WM_CLR_TASKBAR_BG   0x00161626   /* 任务栏: 深靛变种 (比 BG 略暗) */
#define WM_CLR_TASKBAR_BTN  0x0037474F   /* 任务栏按钮: 蓝灰 */
#define WM_CLR_TASKBAR_ACT  0x001976D2   /* 激活窗口按钮: 蓝 */
#define WM_CLR_DESKTOP_TEXT 0x00FFFFFF

/* ---- 窗口结构 ---- */
struct wm_window {
    int wid;                 /* 窗口 ID (>0) */
    int pid;                 /* 所有者 task pid (0=内核窗口) */
    int x, y, w, h;          /* 屏幕坐标 (含标题栏+边框) */
    char title[64];
    int z_order;             /* 渲染顺序 (小=底) */
    int focused;             /* 1=当前顶层焦点 */

    /* 内容区逻辑光标 (EFS 程序 API 重定向使用) */
    int c_cx;                /* 内容区相对列 (像素) */
    int c_cy;                /* 内容区相对行 (像素) */
    unsigned int c_fg;       /* 内容前景色 */
    unsigned int c_bg;       /* 内容背景色 */
    int c_cols;              /* 内容字符列数 (基于 FONT_W) */
    int c_rows;              /* 内容字符行数 */

    /* [文本缓冲区] 存储窗口内所有字符, composite 时从缓冲区重绘。
     * 解决: wm_composite 先填桌面背景会擦除 putc 直接画到 framebuffer 的文字。 */
    char tbuf[WM_TBUF_ROWS][WM_TBUF_COLS];  /* 字符网格 */
    int tbuf_dirty;          /* 1=缓冲区有新内容需重绘 */

    /* [EFS 图形标志] 1=本窗口内容区有 EFS 程序 (efmlogin/userman 等) 通过
     * fill_rect/draw_rect/put_pixel 直接写入 back buffer 的图形元素。
     * wm_composite / 合成器据此决定是否保留 back buffer 像素 (不覆盖背景),
     * 否则传统 shell 窗口仍需用 c_bg 填充背景后画文字。 */
    int has_efm_gfx;
    /* [新架构] EFS 图形信息指针: 由 EFS 程序通过 set_gfx_info API 设置.
     * 非 NULL 时 efmcompositor 从此结构读取矩形+文字, 统一渲染到 canvas/back buffer.
     * EFS 程序不再直接写 back buffer (has_efm_gfx 模式已废弃). */
    struct efm_gfx_info *gfx_info;

    /* 拖动状态 */
    int dragging;            /* 1=正在被鼠标拖动 */
    int drag_dx, drag_dy;    /* 拖动起点相对窗口左上角偏移 */

    int alive;               /* 1=活动 0=待销毁 */

    /* 任务栏按钮命中区域 (wm_composite 写入, wm_handle_mouse 读取) */
    int __taskbar_x1, __taskbar_y1;
    int __taskbar_x2, __taskbar_y2;
};

/* ---- 全局 WM 状态 ---- */
static int  g_wm_enabled = 0;         /* 1=WM 接管屏幕 (登录后) */
static struct wm_window g_wm_windows[WM_MAX_WINDOWS];
static int g_wm_next_wid = 1;
static int g_wm_z_top = 0;            /* 当前最大 z 值 */
static int g_wm_focus_wid = 0;        /* 当前焦点窗口 wid */
static struct wm_window *g_wm_drag_win = 0;  /* 当前正在拖动的窗口 */
static int g_wm_close_hover_wid = 0;  /* 鼠标悬停关闭按钮的窗口 (hover 颜色) */
static spinlock_t g_wm_lock = SPIN_INIT;
static int g_compositor_active = 0;   /* 1=Mesa 合成器已接管渲染, 内核不画 */
static unsigned int g_efs_save_buf[EFS_SAVE_MAX_PIXELS];  /* EFS 图形内容区保存缓冲区 */
static int g_efs_save_cnt = 0;

/* [开始菜单] efmAether @0x6000 的 aether_render_info 镜像 (仅用于读取菜单项).
 * 必须与 efmAether.c / efmcompositor.c 的 struct aether_render_info 布局一致. */
#define AETHER_INFO_MAGIC  0x41455448u   /* "AETH" */
#define AETHER_INFO_ADDR   0x6000
#define AETHER_MAX_TEXT    16
#define AETHER_MAX_MENU   16
struct aether_text_item { int x, y; unsigned int color; char text[96]; };
struct aether_menu_item { char label[32]; char program[32]; };
struct aether_render_info {
    unsigned int magic;
    unsigned int seq;
    int font_w, font_h;
    unsigned int clr_bg, clr_desktop_text, clr_border, clr_title_active,
                 clr_title_inact, clr_title_text, clr_close_bg, clr_close_hover,
                 clr_close_text, clr_taskbar_bg, clr_taskbar_btn, clr_taskbar_act;
    unsigned int clr_menu_bg, clr_menu_item, clr_menu_hover, clr_menu_text, clr_menu_border;
    struct aether_text_item desktop_title;
    struct aether_text_item start_text;
    struct aether_text_item extra[AETHER_MAX_TEXT];
    int extra_count;
    struct aether_menu_item menu_items[AETHER_MAX_MENU];
    int menu_count;
};
/* [开始菜单状态] 由 wm_handle_mouse 维护, efs_get_wm_snapshot 读取给合成器渲染 */
static int g_start_menu_open = 0;       /* 1=菜单已打开 */
static int g_start_menu_hover_idx = -1; /* 鼠标悬停的菜单项索引 */
static int g_start_menu_count = 0;      /* 菜单项总数 (来自 efmAether @0x6000) */

/* ---------- Mesa 合成器 API 实现 (需在 WM 全局变量定义之后) ---------- */
static int efs_get_wm_snapshot(void *out, int max_bytes) {
    if (!out || max_bytes < (int)sizeof(struct efm_wm_snapshot)) return -1;
    struct efm_wm_snapshot *snap = (struct efm_wm_snapshot*)out;
    struct gop_fb *fb = (struct gop_fb*)0x1000;

    spin_lock(&g_wm_lock);
    snap->magic = EFS_API_MAGIC;
    snap->screen_w = fb ? fb->hr : 0;
    snap->screen_h = fb ? fb->vr : 0;
    snap->mouse_x = s_mx;
    snap->mouse_y = s_my;
    snap->mouse_btn = 0;  /* btn 是瞬时的, 合成器从 mouse_poll 获取 */
    snap->mouse_visible = s_mouse_visible;
    snap->close_hover_wid = g_wm_close_hover_wid;
    snap->taskbar_h = WM_TASKBAR_H;
    snap->title_h = WM_TITLE_H;
    snap->font_w = FONT_W;
    snap->font_h = FONT_H;
    /* EFS 图形变化计数器: 单调递增, 合成器对比新旧值检测 fill_rect/draw_rect 等
     * 像素级 API 的修改, 确保 canvas 内容区重绘不会覆盖 EFS 程序绘制的图形. */
    snap->gfx_dirty_seq = g_wm_gfx_dirty_seq;

    int cnt = 0;
    for (int i = 0; i < WM_MAX_WINDOWS && cnt < EFM_WM_MAX_WIN; i++) {
        struct wm_window *w = &g_wm_windows[i];
        if (!w->alive) continue;
        struct efm_wm_win_info *info = &snap->windows[cnt];
        info->wid = w->wid;
        info->x = w->x; info->y = w->y; info->w = w->w; info->h = w->h;
        my_memcpy(info->title, w->title, EFM_WM_TITLE_LEN);
        info->title[EFM_WM_TITLE_LEN - 1] = 0;
        info->z_order = w->z_order;
        info->focused = w->focused;
        info->c_cx = w->c_cx; info->c_cy = w->c_cy;
        info->c_fg = w->c_fg; info->c_bg = w->c_bg;
        info->alive = 1;
        info->close_hover = (g_wm_close_hover_wid == w->wid) ? 1 : 0;
        info->has_efm_gfx = w->has_efm_gfx;
        info->tbuf_cols = WM_TBUF_COLS;
        info->tbuf_rows = WM_TBUF_ROWS;
        info->tbuf_ptr = (char*)w->tbuf;  /* 共享地址空间, 合成器可直接读 */
        info->gfx_info_ptr = w->gfx_info;  /* EFS 图形信息 (可能为 NULL) */
        cnt++;
    }
    snap->window_count = cnt;
    /* [关键修复] 每帧从 efmAether @0x6000 读取 menu_count。
     * 之前只在 wm_handle_mouse 有点击事件时才读取, 合成器收不到正确的菜单项数,
     * 导致 snap->menu_item_count=0, 菜单从不渲染。 */
    {
        struct aether_render_info *ai =
            (struct aether_render_info *)(unsigned long)AETHER_INFO_ADDR;
        if (ai && ai->magic == AETHER_INFO_MAGIC) {
            int mc = ai->menu_count;
            if (mc < 0) mc = 0;
            if (mc > AETHER_MAX_MENU) mc = AETHER_MAX_MENU;
            g_start_menu_count = mc;
        }
    }
    /* 开始菜单状态: 同步给合成器渲染弹出菜单 */
    snap->menu_open = g_start_menu_open;
    snap->menu_hover_idx = g_start_menu_hover_idx;
    snap->menu_item_count = g_start_menu_count;
    spin_unlock(&g_wm_lock);
    return cnt;
}

static void efs_set_compositor_active(int active) {
    g_compositor_active = active ? 1 : 0;
    if (active) {
        /* 合成器接管后, 内核不再直接渲染, 但仍需处理鼠标事件更新窗口状态.
         * 置 g_wm_dirty=0 防止空闲轮询触发 wm_composite. */
        g_wm_dirty = 0;
    }
}

/* dlsym: 从已加载的共享库中解析符号地址 (给合成器调用 Mesa EGL/GL) */
static void *efs_dlsym(const char *name) {
    unsigned long long addr = dyn_resolve_sym(name);
    return addr ? (void*)addr : 0;
}

/* [优化] 获取 Graphics.drv 的 back buffer, 让 Mesa swrast 直接渲染到 back buffer.
 * 消除 BO→back buffer 全屏拷贝 (8MB/帧 @1080p). */
static int efs_get_backbuffer(void **out_ptr, int *out_pitch, int *out_w, int *out_h) {
    if (!g_drv_gfx_ptr || !g_drv_gfx_ptr->get_backbuffer) return -1;
    return g_drv_gfx_ptr->get_backbuffer(out_ptr, out_pitch, out_w, out_h);
}

/* [优化] 标记 back buffer 的脏矩形区域 (inclusive). 直接映射模式下,
 * swrast 直接写 back buffer 不经过 blit_buffer, 需要手动标记脏区让 flush 搬到 front. */
static void efs_mark_dirty_rect(int x1, int y1, int x2, int y2) {
    drv_gfx_mark_dirty_rect(x1, y1, x2, y2);
}

/* [优化] 立即把 back buffer 的脏区 flush 到 front buffer (back→front 拷贝).
 * 合成器每帧渲染完成后调用一次, 确保整帧原子呈现, 避免撕裂/闪烁. */
static void efs_flush_now(void) {
    drv_gfx_force_flush();
}

/* [TTF] Unicode 字符渲染: 转发给 Graphics.drv 的 draw_char_unicode.
 * WM 模式下坐标为窗口内容区相对坐标 (调用者通常为桌面/合成器, 用全局坐标).
 * bg=0xFEEDFACE = 透明模式 (不画背景方块, alpha 混合到现有像素).
 * 返回字符实际像素宽度, 0=失败/不支持. */
static int efs_draw_char_unicode(int x, int y, unsigned int codepoint,
                                 unsigned int fg, unsigned int bg,
                                 int cell_w, int cell_h) {
    if (!g_drv_gfx_ptr || !g_drv_gfx_ptr->draw_char_unicode) return 0;
    /* [关键修复] WM 模式下 EFS 程序传入的坐标是窗口内容区相对坐标 (0,0=内容区左上角),
     * 需要转换为屏幕绝对坐标 (加上窗口偏移), 否则文字画到屏幕左上角而非窗口内,
     * 导致按钮位置正确但文字位置错误 (文字按钮错位).
     * fill_rect/put_pixel 已有同样转换, 此处补齐 draw_char_unicode. */
    if (g_wm_enabled && efs_should_redirect()) {
        struct kernel_api *a = (struct kernel_api*)0x9000;
        struct wm_window *w = wm_find_by_pid(a->current_pid);
        if (w) {
            int ox = wm_content_x(w), oy = wm_content_y(w);
            int cw = wm_content_w(w), ch = wm_content_h(w);
            /* 裁剪到内容区 */
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x + cell_w > cw) cell_w = cw - x;
            if (y + cell_h > ch) cell_h = ch - y;
            if (cell_w <= 0 || cell_h <= 0) return 0;
            int rc = g_drv_gfx_ptr->draw_char_unicode(ox + x, oy + y, codepoint,
                                                      fg, bg, cell_w, cell_h);
            g_wm_dirty = 1;
            g_wm_gfx_dirty_seq++;
            wm_window_set_has_efm_gfx(w, 1);
            return rc;
        }
    }
    return g_drv_gfx_ptr->draw_char_unicode(x, y, codepoint, fg, bg, cell_w, cell_h);
}

/* [新架构] set_gfx_info: EFS 图形程序调用此 API 设置窗口的图形信息指针.
 * 内核把指针存入当前进程的窗口, efmcompositor 通过 get_wm_snapshot 读取.
 * [修复] 同时置 has_efm_gfx=1, 并 bump g_wm_dirty/g_wm_gfx_dirty_seq,
 * 确保合成器立即感知窗口已切换到 GUI 协议渲染, 不会漏过第一帧. */
static void efs_set_gfx_info(void *info) {
    struct kernel_api *a = (struct kernel_api*)0x9000;
    struct wm_window *w = wm_find_by_pid(a->current_pid);
    if (w) {
        w->gfx_info = (struct efm_gfx_info*)info;
        if (info) {
            wm_window_set_has_efm_gfx(w, 1);
            g_wm_dirty = 1;
            g_wm_gfx_dirty_seq++;
        }
    }
}

/* 封装: WM 不完整类型访问器 (给 WM 渲染代码使用) */
static int wm_window_cy(struct wm_window *w) { return w ? w->c_cy : 0; }
static void wm_window_set_cx(struct wm_window *w, int cx) { if (w) w->c_cx = cx; }
static int  wm_window_content_is_cjk_width_at(struct wm_window *w, int cx_px, int cy_px) { (void)w; (void)cx_px; (void)cy_px; return 0; }

/* 封装: wminfo 命令输出 (WM 常量/变量完整可见) */
static void wm_print_status_all(void) {
    print_string(TR("WM status: ", "WM 状态: "));
    print_string(g_wm_enabled ? TR("active","已启用") : TR("inactive","未启用"));
    print_string(TR("  windows: ","  窗口数: "));
    int wc = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) if (g_wm_windows[i].alive) wc++;
    print_dec(wc); print_string(TR("  focus wid=","  焦点窗口wid="));
    print_dec(g_wm_focus_wid); print_string("\n");
}

/* 内容区坐标 = 窗口坐标 + 边框 + 标题栏 */
static inline int wm_content_x(struct wm_window *w) { return w->x + WM_BORDER; }
static inline int wm_content_y(struct wm_window *w) { return w->y + WM_TITLE_H + WM_BORDER; }
static inline int wm_content_w(struct wm_window *w) { return w->w - 2 * WM_BORDER; }
static inline int wm_content_h(struct wm_window *w) { return w->h - WM_TITLE_H - 2 * WM_BORDER; }

/* 找一个空槽位 */
static struct wm_window *wm_alloc_slot(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (!g_wm_windows[i].alive) return &g_wm_windows[i];
    return 0;
}
static struct wm_window *wm_find_by_wid(int wid) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (g_wm_windows[i].alive && g_wm_windows[i].wid == wid) return &g_wm_windows[i];
    return 0;
}
static struct wm_window *wm_find_by_pid(int pid) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (g_wm_windows[i].alive && g_wm_windows[i].pid == pid) return &g_wm_windows[i];
    return 0;
}
/* 仅判断窗口是否存在 (无指针返回) — 给 WM 前部代码通过封装调用 */
static int wm_window_exists_for_pid(int pid) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (g_wm_windows[i].alive && g_wm_windows[i].pid == pid) return 1;
    return 0;
}
/* 返回关闭按钮相对窗口左上角的坐标 (x1,y1,x2,y2 均为相对坐标) */
static inline void wm_close_btn_rect(struct wm_window *w, int *rx1, int *ry1, int *rx2, int *ry2) {
    *rx2 = w->w - WM_BORDER - 2;
    *rx1 = *rx2 - WM_CLOSE_W;
    *ry1 = (WM_TITLE_H - FONT_H) / 2;
    *ry2 = *ry1 + FONT_H;
}
/* 命中测试: 0=内容/其他, 1=标题栏 (可拖), 2=关闭按钮 */
#define WM_HIT_INSIDE   0
#define WM_HIT_TITLE    1
#define WM_HIT_CLOSE    2
static int wm_hit_test(struct wm_window *w, int sx, int sy) {
    if (sx < w->x || sx >= w->x + w->w || sy < w->y || sy >= w->y + w->h) return -1;
    int rx = sx - w->x, ry = sy - w->y;
    if (ry < WM_TITLE_H) {
        int cx1, cy1, cx2, cy2;
        wm_close_btn_rect(w, &cx1, &cy1, &cx2, &cy2);
        if (rx >= cx1 && rx < cx2 && ry >= cy1 && ry < cy2) return WM_HIT_CLOSE;
        return WM_HIT_TITLE;
    }
    return WM_HIT_INSIDE;
}

/* 把窗口推到 Z 序顶部 (设为焦点) */
static void wm_focus_window(struct wm_window *w) {
    if (!w) return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) g_wm_windows[i].focused = 0;
    g_wm_z_top++;
    w->z_order = g_wm_z_top;
    w->focused = 1;
    g_wm_focus_wid = w->wid;
}

/* 创建窗口, 返回 wid (>0 成功) */
static int wm_create_window(const char *title, int pid, int w, int h, int *out_wid) {
    if (!g_wm_enabled) return -1;
    spin_lock(&g_wm_lock);
    struct wm_window *slot = wm_alloc_slot();
    if (!slot) { spin_unlock(&g_wm_lock); return -2; }
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (!fb) { spin_unlock(&g_wm_lock); return -3; }
    int scr_w = (int)fb->hr, scr_h = (int)fb->vr - WM_TASKBAR_H;
    if (w < WM_MIN_W) w = WM_MIN_W;
    if (h < WM_MIN_H) h = WM_MIN_H;
    if (w > scr_w - 40) w = scr_w - 40;
    if (h > scr_h - 40) h = scr_h - 40;
    /* 级联位置: 基于 wid 错位排列, 避免重叠遮挡 */
    static int s_cascade_x = 60, s_cascade_y = 40;
    int wx = s_cascade_x, wy = s_cascade_y;
    s_cascade_x += 30; s_cascade_y += 30;
    if (s_cascade_x + w > scr_w - 20) s_cascade_x = 60;
    if (s_cascade_y + h > scr_h - 20) s_cascade_y = 40;
    if (wx + w > scr_w) wx = scr_w - w - 10;
    if (wy + h > scr_h) wy = scr_h - h - 10;

    my_memset(slot, 0, sizeof(*slot));
    slot->alive = 1;
    slot->wid = g_wm_next_wid++;
    slot->pid = pid;
    slot->x = wx; slot->y = wy; slot->w = w; slot->h = h;
    int tl = 0; while (title && title[tl] && tl < 63) { slot->title[tl] = title[tl]; tl++; }
    slot->title[tl] = 0;
    slot->c_fg = WM_CLR_CONTENT_FG;
    slot->c_bg = WM_CLR_CONTENT_BG;
    slot->c_cx = 0; slot->c_cy = 0;
    slot->c_cols = wm_content_w(slot) / FONT_W;
    slot->c_rows = wm_content_h(slot) / FONT_H;
    if (slot->c_cols < 1) slot->c_cols = 1;
    if (slot->c_rows < 1) slot->c_rows = 1;
    wm_focus_window(slot);
    int wid = slot->wid;
    spin_unlock(&g_wm_lock);
    if (out_wid) *out_wid = wid;
    wm_composite();
    return 0;
}
/* 销毁窗口 */
static void wm_destroy_window(int wid) {
    spin_lock(&g_wm_lock);
    struct wm_window *w = wm_find_by_wid(wid);
    if (!w) { spin_unlock(&g_wm_lock); return; }
    w->alive = 0;
    if (g_wm_focus_wid == wid) g_wm_focus_wid = 0;
    /* 找下一个最高 z 的窗口做焦点 */
    struct wm_window *next = 0; int max_z = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_wm_windows[i].alive && g_wm_windows[i].z_order > max_z) {
            max_z = g_wm_windows[i].z_order;
            next = &g_wm_windows[i];
        }
    }
    spin_unlock(&g_wm_lock);
    if (next) wm_focus_window(next);
    wm_composite();
}

/* 滚动内容区 (当换行到底部时): 整体内容上移一行 (FONT_H 像素) */
static void wm_content_scroll_up(struct wm_window *w) {
    /* [文本缓冲区版] 上移一行: 行 1..N-1 → 行 0..N-2, 末行清空 */
    for (int r = 0; r < WM_TBUF_ROWS - 1; r++) {
        for (int c = 0; c < WM_TBUF_COLS; c++)
            w->tbuf[r][c] = w->tbuf[r + 1][c];
    }
    for (int c = 0; c < WM_TBUF_COLS; c++)
        w->tbuf[WM_TBUF_ROWS - 1][c] = 0;
    w->tbuf_dirty = 1;
}

/* 在窗口内容区当前光标位置写一个字符 (不处理 CJK 的复杂情况)
 * [文本缓冲区版] 只写入 tbuf, 不直接画 framebuffer; 由 wm_composite 统一重绘。 */
static void wm_window_putc(struct wm_window *w, char ch) {
    if (!w || !w->alive) return;
    int cw = wm_content_w(w), ch2 = wm_content_h(w);
    /* [\r 回车] 回到当前行行首 (列 0) */
    if (ch == '\r') {
        w->c_cx = 0;
        return;
    }
    if (ch == '\n') {
        w->c_cx = 0;
        w->c_cy += FONT_H;
        if (w->c_cy + FONT_H > ch2) {
            wm_content_scroll_up(w);
            w->c_cy = ch2 - FONT_H;
        }
        return;
    }
    if (w->c_cx + FONT_W > cw) {
        w->c_cx = 0;
        w->c_cy += FONT_H;
        if (w->c_cy + FONT_H > ch2) {
            wm_content_scroll_up(w);
            w->c_cy = ch2 - FONT_H;
        }
    }
    /* 写入文本缓冲区 (网格坐标 = 像素坐标 / 字体尺寸) */
    int col = w->c_cx / FONT_W;
    int row = w->c_cy / FONT_H;
    if (row >= 0 && row < WM_TBUF_ROWS && col >= 0 && col < WM_TBUF_COLS) {
        w->tbuf[row][col] = ch;
        w->tbuf_dirty = 1;
    }
    w->c_cx += FONT_W;
}
/* 清空窗口内容区中指定行 (row 单位=像素 y, 即 c_cy 值)
 * [文本缓冲区版] 清空 tbuf 中对应行 */
static void wm_window_clear_row(struct wm_window *w, int row) {
    if (!w || !w->alive) return;
    int trow = row / FONT_H;
    if (trow >= 0 && trow < WM_TBUF_ROWS) {
        for (int c = 0; c < WM_TBUF_COLS; c++)
            w->tbuf[trow][c] = 0;
        w->tbuf_dirty = 1;
    }
}
/* 光标前进 n 个字符宽度 (不绘制字符, 只更新 c_cx/c_cy), 处理换行 */
static void wm_window_advance(struct wm_window *w, int n) {
    if (!w || !w->alive || n <= 0) return;
    int cw = wm_content_w(w), ch2 = wm_content_h(w);
    int pixels = n * FONT_W;
    w->c_cx += pixels;
    while (w->c_cx >= cw) {
        w->c_cx -= cw;
        w->c_cy += FONT_H;
        if (w->c_cy + FONT_H > ch2) {
            wm_content_scroll_up(w);
            w->c_cy = ch2 - FONT_H;
        }
    }
}
/* 设置窗口 EFS 图形标志 (供前半部分 EFS API 使用, 因为 struct wm_window
 * 完整定义在文件后面, 不能直接访问 w->has_efm_gfx). */
static void wm_window_set_has_efm_gfx(struct wm_window *w, int v) {
    if (w) w->has_efm_gfx = v ? 1 : 0;
}

/* 清空窗口内容区 (填 c_bg) + 清空文本缓冲区 */
static void wm_window_clear(struct wm_window *w) {
    if (!w || !w->alive) return;
    for (int r = 0; r < WM_TBUF_ROWS; r++)
        for (int c = 0; c < WM_TBUF_COLS; c++)
            w->tbuf[r][c] = 0;
    w->c_cx = 0; w->c_cy = 0;
    w->tbuf_dirty = 1;
    /* [修复] 不再清除 has_efm_gfx: clear_screen 后 EFS 程序 (efmlogin/userman)
     * 会立即重绘按钮/矩形到 back buffer。如果在此清零, wm_redirect_clear → wm_composite
     * 会把内容区填成 c_bg, 然后 EFS 程序重绘的图形可能在下一帧合成前不可见,
     * 造成"界面闪烁/按钮消失"。保留 has_efm_gfx=1 让 wm_composite 的 save/restore
     * 机制保存旧图形, 直到 EFS 程序重绘覆盖。 */
    /* [合成器接管] clear_screen 必须同时清空 back buffer 的窗口内容区,
     * 否则 EFS 程序 (userman) 下一帧重绘时旧矩形/按钮残留在 back buffer 中,
     * 合成器复制 back buffer → canvas 会把残留也显示出来, 导致画面错乱。
     * 这里用 w->c_bg 填充内容区, 并递增 gfx_dirty_seq 通知合成器重绘。 */
    if (g_compositor_active && g_drv_gfx_ptr && g_drv_gfx_ptr->fill_rect) {
        int cx = wm_content_x(w);
        int cy = wm_content_y(w);
        int cw = wm_content_w(w);
        int ch = wm_content_h(w);
        if (cw > 0 && ch > 0) {
            fill_rect(cx, cy, cw, ch, w->c_bg);
            g_wm_gfx_dirty_seq++;
        }
    }
}

/* 合成渲染: 先画桌面背景, 再按 Z 序从小到大画窗口, 最后画任务栏.
 * [渲染模型 2026+ 最终版] 标准 compositor "合成→flush→画鼠标" 三段式:
 *   1. frame_begin: 所有绘制 (桌面/窗口/任务栏/文本光标) 全写 back buffer, 中途不 flush
 *   2. frame_end: 一次性把 back buffer 脏矩形 flush 到 front buffer (无撕裂)
 *   3. flush 完成后: 直接在 front buffer 上画鼠标箭头 (不 save/restore, 下帧合成会覆盖)
 * 这彻底消除了旧 "save/restore 鼠标背景 → 写 front → 被 flush 覆盖" 的闪烁链. */
static void wm_composite(void) {
    /* [桌面脱离内核] 桌面渲染已移至 efmAether.efs (TTF 字体).
     * 内核仅保留 WM 状态管理 (窗口创建/位置/标题/tbuf/focus/drag),
     * 通过 get_wm_snapshot 暴露给 efmAether 读取.
     * 此函数保留签名 (9 处调用点), 但不再执行任何渲染. */
    if (!g_wm_enabled) return;
    g_wm_dirty = 0;  /* 清脏标记, 避免调用者反复触发 */
}

/* WM 鼠标事件分发: 在空闲轮询路径被调用。
 * 返回 1=事件被 WM 消费 (已合成/移动/关闭/焦点切换), 0=事件留给 EFS/键盘 */
/* [开始菜单点击边缘检测] 上一帧左键状态, 仅 btn 从 0→1 跳变时才算一次点击,
     * 防止 PS/2 鼠标按住拖动时每个数据包重复触发 (原日志中 ~100 个 SM:click 行) */
static unsigned char g_last_btn = 0;
/* [防抖] 开始按钮切换后, 需等待若干次轮询才允许再次切换,
 * 防止 PS/2 鼠标快速连发导致菜单开→关瞬间抖动 (用户看不到菜单). */
static int g_start_toggle_guard = 0;
static int wm_handle_mouse(void) {
    if (!g_wm_enabled) return 0;
    struct mouse_event ev;
    if (!mouse_poll(&ev)) return 0;
    int x = ev.x, y = ev.y;
    int btn = ev.btn;
    /* btn_press: 左键按下瞬间 (0→1 跳变), 用于开始按钮/菜单项点击.
     * 已有的窗口点击/拖动逻辑继续用 btn & 1 (保持按住拖动语义). */
    int btn_press = ((g_last_btn & 1) == 0) && (btn & 1);
    g_last_btn = (unsigned char)btn;
    int consumed = 0;
    spin_lock(&g_wm_lock);
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    int taskbar_y0 = fb ? ((int)fb->vr - WM_TASKBAR_H) : 0;

    /* 关闭按钮 hover 检查 (仅影响颜色, 不消费事件) */
    int new_hover = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        struct wm_window *w = &g_wm_windows[i];
        if (!w->alive) continue;
        if (wm_hit_test(w, x, y) == WM_HIT_CLOSE) { new_hover = w->wid; break; }
    }
    if (new_hover != g_wm_close_hover_wid) { g_wm_close_hover_wid = new_hover; consumed = 1; }

    /* ===== 开始菜单: 读取 efmAether @0x6000 的菜单项数量 + 字体高度 =====
     * [关键] menu_item_h 必须用 efmAether 的 font_h (= TTF_FONT_H=22), 不能用
     * 内核 FONT_H (=18), 否则 hit-testing 与 compositor 渲染的项高度不一致. */
    int aether_font_h = FONT_H;
    {
        struct aether_render_info *ai =
            (struct aether_render_info *)(unsigned long)AETHER_INFO_ADDR;
        if (ai && ai->magic == AETHER_INFO_MAGIC) {
            g_start_menu_count = ai->menu_count;
            if (g_start_menu_count < 0) g_start_menu_count = 0;
            if (g_start_menu_count > AETHER_MAX_MENU) g_start_menu_count = AETHER_MAX_MENU;
            if (ai->font_h > 0) aether_font_h = ai->font_h;
            /* [DEBUG] 开始菜单点击状态 (仅按下瞬间触发, 按住拖动不重复) */
            if (btn_press) {
                serial_write("SM:click x="); serial_put_dec(x);
                serial_write(" y="); serial_put_dec(y);
                serial_write(" mc="); serial_put_dec(g_start_menu_count);
                serial_write(" fh="); serial_put_dec(aether_font_h);
                serial_write(" op="); serial_put_dec(g_start_menu_open);
                serial_write("\n");
            }
        } else {
            if (btn_press) serial_write("SM:no_aether\n");
        }
    }
    /* 开始按钮几何 (与 efmcompositor render_scene 一致) */
    int start_sz = WM_TASKBAR_H - 6;
    if (start_sz < 20) start_sz = 20;
    int sb_x1 = 4, sb_y1 = taskbar_y0 + 3;
    int sb_x2 = sb_x1 + start_sz, sb_y2 = sb_y1 + start_sz;
    /* [DEBUG] 开始按钮几何 (仅按下瞬间输出一次) */
    if (btn_press) {
        serial_write("SM:sb geo x="); serial_put_dec(sb_x1);
        serial_write("-"); serial_put_dec(sb_x2);
        serial_write(" y="); serial_put_dec(sb_y1);
        serial_write("-"); serial_put_dec(sb_y2);
        serial_write(" sz="); serial_put_dec(start_sz);
        serial_write(" ty0="); serial_put_dec(taskbar_y0);
        serial_write("\n");
    }
    /* 开始菜单弹出几何 (与 efmcompositor render_scene 一致: item_h = TTF_FONT_H + 8) */
    int menu_w = 160;
    int menu_item_h = aether_font_h + 8;
    int menu_h = g_start_menu_count * menu_item_h + 6;
    int menu_x1 = 4, menu_x2 = menu_x1 + menu_w;
    int menu_y2 = taskbar_y0;            /* 紧贴任务栏上方 */
    int menu_y1 = menu_y2 - menu_h;      /* 菜单顶部 */
    if (menu_y1 < 0) menu_y1 = 0;

    /* ===== 开始菜单 hover 更新 (鼠标移动时, 不需点击) ===== */
    if (g_start_menu_open && g_start_menu_count > 0) {
        int new_hov = -1;
        if (x >= menu_x1 && x < menu_x2 && y >= menu_y1 && y < menu_y2) {
            int idx = (y - menu_y1 - 3) / menu_item_h;
            if (idx >= 0 && idx < g_start_menu_count) new_hov = idx;
        }
        if (new_hov != g_start_menu_hover_idx) {
            g_start_menu_hover_idx = new_hov;
            consumed = 1;
        }
    }

    /* ===== 左键点击: 开始按钮 / 菜单项 / 外部关闭 =====
     * menu_was_open: 快照菜单状态 (case 3 会清零 g_start_menu_open, 用快照守卫窗口点击) */
    int menu_was_open = g_start_menu_open;
    /* [关键修复] 用 btn_press (边缘检测) 而非 btn & 1, 防止按住拖动时每帧切换菜单 */
    if (g_start_toggle_guard > 0) g_start_toggle_guard--;
    if (btn_press) {
        /* 1. 点击开始按钮 → 切换菜单开关 (带防抖, 防止快速连发) */
        if (x >= sb_x1 && x < sb_x2 && y >= sb_y1 && y < sb_y2) {
            if (g_start_toggle_guard == 0) {
                g_start_menu_open = !g_start_menu_open;
                g_start_menu_hover_idx = -1;
                g_start_toggle_guard = 8;  /* 防抖: 等待 ~8 次轮询 (~250ms) */
                consumed = 1;
                serial_write("SM:start_btn_hit\n");
            }
        }
        /* 2. 菜单打开时点击菜单项 → 启动对应程序 */
        else if (g_start_menu_open && g_start_menu_count > 0 &&
                 x >= menu_x1 && x < menu_x2 &&
                 y >= menu_y1 && y < menu_y2) {
            int idx = (y - menu_y1 - 3) / menu_item_h;
            if (idx >= 0 && idx < g_start_menu_count) {
                struct aether_render_info *ai =
                    (struct aether_render_info *)(unsigned long)AETHER_INFO_ADDR;
                if (ai && ai->magic == AETHER_INFO_MAGIC &&
                    ai->menu_items[idx].program[0]) {
                    /* 复制程序名到栈 (释放锁后调用 efs_spawn_async, 避免持锁阻塞) */
                    char prog[32];
                    for (int i = 0; i < 31; i++) {
                        prog[i] = ai->menu_items[idx].program[i];
                        if (prog[i] == 0) break;
                    }
                    prog[31] = 0;
                    g_start_menu_open = 0;
                    g_start_menu_hover_idx = -1;
                    /* 清空 efs_argbuf 防止上一条 shell 命令的残留参数传入菜单启动的程序 */
                    {
                        extern char efs_argbuf[1024];
                        extern int  efs_arglen;
                        efs_argbuf[0] = 0;
                        efs_arglen = 0;
                    }
                    spin_unlock(&g_wm_lock);
                    (void)efs_spawn_async(prog);  /* 新窗口异步启动 */
                    spin_lock(&g_wm_lock);
                    consumed = 1;
                    serial_write("SM:spawn "); serial_write(prog); serial_write("\n");
                }
            }
        }
        /* 3. 菜单打开时点击其他区域 → 关闭菜单 */
        else if (g_start_menu_open) {
            g_start_menu_open = 0;
            g_start_menu_hover_idx = -1;
            consumed = 1;
        }
    }

    /* 拖动中: 更新位置 */
    if (g_wm_drag_win && g_wm_drag_win->alive && g_wm_drag_win->dragging) {
        int nx = x - g_wm_drag_win->drag_dx;
        int ny = y - g_wm_drag_win->drag_dy;
        /* 限制在桌面范围内 */
        int max_y = fb ? ((int)fb->vr - WM_TASKBAR_H - 10) : 600;
        if (nx < -g_wm_drag_win->w + 30) nx = -g_wm_drag_win->w + 30;
        if (fb && nx > (int)fb->hr - 30) nx = (int)fb->hr - 30;
        if (ny < 0) ny = 0;
        if (ny > max_y) ny = max_y;
        g_wm_drag_win->x = nx; g_wm_drag_win->y = ny;
        consumed = 1;
    }

    /* 左键按下瞬间 (btn_press = 0→1 跳变).
     * [开始菜单] 菜单打开时, 若开始按钮/菜单项/外部关闭已消费事件, 跳过窗口点击处理.
     * 菜单关闭时, 不加守卫 (保持原有行为: hover 变化设 consumed=1 不影响点击). */
    if (btn_press && !(consumed && menu_was_open)) {
        /* 先查任务栏点击 */
        if (fb && y >= taskbar_y0) {
            for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                struct wm_window *w = &g_wm_windows[i];
                if (!w->alive) continue;
                if (x >= w->__taskbar_x1 && x < w->__taskbar_x2 &&
                    y >= w->__taskbar_y1 && y < w->__taskbar_y2) {
                    wm_focus_window(w);
                    consumed = 1;
                    break;
                }
            }
        } else {
            /* 查找顶层窗口 (从最高 z 开始) */
            struct wm_window *top = 0; int top_z = -1;
            for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                struct wm_window *w = &g_wm_windows[i];
                if (!w->alive) continue;
                if (wm_hit_test(w, x, y) >= 0 && w->z_order > top_z) {
                    top_z = w->z_order; top = w;
                }
            }
            if (top) {
                int ht = wm_hit_test(top, x, y);
                if (ht == WM_HIT_CLOSE) {
                    int wid = top->wid;
                    spin_unlock(&g_wm_lock);
                    wm_destroy_window(wid);
                    spin_lock(&g_wm_lock);
                    consumed = 1;
                } else if (ht == WM_HIT_TITLE) {
                    /* 开始拖动 */
                    top->dragging = 1;
                    top->drag_dx = x - top->x;
                    top->drag_dy = y - top->y;
                    g_wm_drag_win = top;
                    wm_focus_window(top);
                    consumed = 1;
                } else {
                    /* 点击内容区: 切换焦点, 并把鼠标事件放回队列让前台 EFS 程序
                     * 通过 mouse_poll 获取点击坐标.
                     * [坐标转换修复] EFS 程序 (efmlogin/userman) 的按钮坐标是
                     * 内容区相对坐标 (通过 btn_add 注册), 但 mouse_poll 返回的是
                     * 屏幕绝对坐标。不转换会导致 btn_hit 永远匹配不上 → 按钮无法点击。
                     * 这里把屏幕坐标 (x,y) 减去窗口内容区左上角偏移, 得到内容区相对坐标。 */
                    wm_focus_window(top);
                    struct mouse_event ev2 = ev;
                    ev2.x = x - wm_content_x(top);
                    ev2.y = y - wm_content_y(top);
                    mouseq_push(&ev2);
                    consumed = 0;
                }
            }
        }
    }
    /* 左键释放: 结束拖动 */
    if (!(btn & 1) && g_wm_drag_win) {
        g_wm_drag_win->dragging = 0;
        g_wm_drag_win = 0;
        consumed = 1;
    }
    spin_unlock(&g_wm_lock);
    /* [窗口拖动优化 2026+] 旧: 每个鼠标事件 consumed=1 就 wm_composite() 一次
     *    → 拖动时每秒几百次合成，CPU 占用高 + 画面抖动。
     * 新: 区分两种 consumed:
     *   - "状态切换类" (按下/释放/关闭点击/hover 变化/焦点切换立即生效): 必须立即合成 (无外层 idle 循环等待)
     *   - "拖动位置类" (dragging=1 且仅 x/y 变化): 只置 g_wm_dirty=1, 不合成。
     *     外层 idle 轮询每帧会 poll dirty → wm_composite，最高约 60fps，视觉流畅。
     * 判定: g_wm_drag_win && g_wm_drag_win->dragging (下一个按键释放之前拖动持续)
     *       且本次事件不是 "鼠标按下刚起拖" (否则首帧位置不更新会有 1 帧滞后)
     *       → 仅置 dirty，不立即 composite。 */
    {
        int drag_only = 0;
        if (g_wm_drag_win && g_wm_drag_win->dragging && (btn & 1)) {
            /* 当前正拖着 + 左键仍按住 — 纯位置更新. */
            drag_only = 1;
        }
        if (consumed) {
            if (drag_only) {
                /* 只置脏, 不立即合. 下一空闲循环 poll g_wm_dirty → 合 + 一次 flush. */
                g_wm_dirty = 1;
            } else {
                /* 非拖动类的交互: 立即合成 (点击标题栏/关闭按钮/hover 切换等). */
                wm_composite();
            }
        }
    }
    return consumed;
}

/* 给指定 PID 的窗口重定向 putc/put_string (供 EFS 进程私有 API 表使用) */
static void wm_redirect_putc(int pid, char ch) {
    struct wm_window *w = wm_find_by_pid(pid);
    if (w) wm_window_putc(w, ch);
}
static void wm_redirect_print(int pid, const char *s) {
    struct wm_window *w = wm_find_by_pid(pid);
    if (!w) return;
    while (*s) wm_window_putc(w, *s++);
}
static void wm_redirect_clear(int pid) {
    struct wm_window *w = wm_find_by_pid(pid);
    if (w) wm_window_clear(w);
    wm_composite();
}

/* WM 初始化 (登录成功后调用) */
static void wm_init(void) {
    spin_lock(&g_wm_lock);
    for (int i = 0; i < WM_MAX_WINDOWS; i++) my_memset(&g_wm_windows[i], 0, sizeof(struct wm_window));
    g_wm_next_wid = 1;
    g_wm_z_top = 0;
    g_wm_focus_wid = 0;
    g_wm_drag_win = 0;
    g_wm_close_hover_wid = 0;
    g_wm_enabled = 1;
    {
        struct kernel_api *api = (struct kernel_api*)0x9000;
        if (api->magic == EFS_API_MAGIC) api->wm_enabled = 1;
    }
    spin_unlock(&g_wm_lock);
    struct gop_fb *fb = (struct gop_fb*)0x1000;
    if (fb && fb->fb_base) {
        /* WM 初始化: 清屏 + 首次合成. 鼠标由 wm_composite Phase 3 统一画. */
        fill_rect(0, 0, fb->hr, fb->vr, WM_CLR_BG);
    }
    wm_composite();
}

/* ========== Main ========== */

/* 获取逻辑 CPU 核心数。
 * 优先使用 CPUID 1 EBX[23:16] (LogicalProcessorCount) — 在 QEMU/OVMF 下最可靠。
 * CPUID 0x0B (Extended Topology) 在 QEMU/OVMF 下 ECX=1 可能返回 0,
 * 回退到 ECX=0 只给出每核心线程数 (1), 而非封装内总逻辑处理器数。 */
static unsigned int get_cpu_cores(void) {
    unsigned int a, b, c, d;
    /* [主] CPUID 1: EBX[23:16] = 每封装逻辑处理器数 */
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    unsigned int n = (b >> 16) & 0xFF;
    if (n > 1) return n;

    /* [辅] CPUID 0x0B: Extended Topology Enumeration */
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    if (a >= 0x0B) {
        /* ECX=1 (Core 级): EBX[15:0] = 封装内逻辑处理器总数 */
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x0B), "c"(1));
        unsigned int n2 = b & 0xFFFF;
        if (n2 > 1) return n2;
        /* ECX=0 (Thread 级) */
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x0B), "c"(0));
        n2 = b & 0xFFFF;
        if (n2 > 1) return n2;
    }
    return n ? n : 1;
}

/* 获取系统总物理内存 (KB)。
 * 优先使用 bootloader 从 UEFI 内存映射计算的值 (存于 0x1510),
 * 回退到 CMOS RTC (仅 SeaBIOS 下可靠, OVMF/UEFI 下返回垃圾值)。 */
static unsigned long long get_mem_size_kb(void) {
    /* [优先] bootloader 传递的 UEFI 内存映射汇总 */
    unsigned long long boot_mem = *(unsigned long long*)0x1510UL;
    if (boot_mem > 0x100000ULL) {   /* > 1MB 才可信 */
        return boot_mem / 1024;
    }
    /* [回退] CMOS RTC (SeaBIOS 兼容) */
    outb(0x70, 0x15); unsigned char el = inb(0x71);
    outb(0x70, 0x16); unsigned char eh = inb(0x71);
    unsigned int ext16 = ((unsigned int)eh << 8) | el;
    outb(0x70, 0x17); unsigned char h7l = inb(0x71);
    outb(0x70, 0x18); unsigned char h7h = inb(0x71);
    unsigned int above_17 = ((unsigned int)h7h << 8) | h7l;
    outb(0x70, 0x34); unsigned char h4l = inb(0x71);
    outb(0x70, 0x35); unsigned char h4h = inb(0x71);
    unsigned int above_34 = ((unsigned int)h4h << 8) | h4l;
    unsigned int above = above_17 > above_34 ? above_17 : above_34;
    unsigned long long total = 1024ULL + (unsigned long long)ext16 + (unsigned long long)above * 64;
    if (total < 1024) total = 1024;
    return total;
}

/* ========== [efmshell] fsop: 文件系统/系统杂项操作 ==========
 * 统一入口: fsop(op, a, b, out, outsz)
 * 文本类操作把结果写入 out (NUL 结尾), shell 在控制台/WM 窗口两种模式下
 * 统一经 api->print 输出; 数值类操作用返回值传递 (ino/字节数/0=ok/-1=err)。
 * 支持的 op:
 *   ln <target> <linkname>   建符号链接, 返回新 ino (0=失败)
 *   readlink <link>          out=目标路径, 返回长度 (-1=非链接/未找到)
 *   truncate <file> <size>   截断/扩展, 0=成功
 *   rmdir <name>             删空目录, 0=成功
 *   unlink <name>            删文件 (mv 用), 0=成功
 *   sync                     刷新磁盘缓存, 0=成功
 *   append <file> <text>     追加文本, 返回追加后总大小 (-1=失败)
 *   shutdown                 ACPI 关机 (成功不返回)
 *   pwd / date / sysinfo / memfree / df / stat / tree / find / du / ps / wminfo
 *                            → 文本写入 out, 返回写入长度 (或 ino/字节数) */

/* --- 缓冲区追加小工具 (截断安全, 始终保 NUL 结尾) --- */
static int fsop_cat(char *out, int outsz, int *len, const char *s) {
    if (!out || outsz <= 1 || !s) return -1;
    while (*s && *len < outsz - 1) out[(*len)++] = *s++;
    out[*len] = 0;
    return 0;
}
static void fsop_cat_dec(char *out, int outsz, int *len, long v) {
    char tmp[24]; int i = 0, j;
    unsigned long u = (v < 0) ? (unsigned long)(-v) : (unsigned long)v;
    if (v < 0 && *len < outsz - 1) out[(*len)++] = '-';
    if (u == 0) tmp[i++] = '0';
    while (u > 0 && i < 22) { tmp[i++] = (char)('0' + (u % 10)); u /= 10; }
    for (j = i - 1; j >= 0 && *len < outsz - 1; j--) out[(*len)++] = tmp[j];
    out[*len] = 0;
}
static void fsop_cat_dec2(char *out, int outsz, int *len, int v) {  /* 2 位补零 */
    if (v < 10) fsop_cat(out, outsz, len, "0");
    fsop_cat_dec(out, outsz, len, v);
}
static void fsop_cat_hex(char *out, int outsz, int *len, unsigned long v) {
    char tmp[20]; int i = 0, j;
    if (v == 0) tmp[i++] = '0';
    while (v > 0 && i < 18) {
        unsigned d = (unsigned)(v & 0xF);
        tmp[i++] = (d < 10) ? (char)('0' + d) : (char)('A' + d - 10);
        v >>= 4;
    }
    for (j = i - 1; j >= 0 && *len < outsz - 1; j--) out[(*len)++] = tmp[j];
    out[*len] = 0;
}

/* --- tree 递归 (输出到缓冲区版本, 与原 ext4_tree_recursive 格式一致) --- */
static void fsop_tree_rec(unsigned int dir_ino, int depth, int *count,
                          char *out, int outsz, int *len) {
    unsigned char inode[256]; read_inode(dir_ino, inode);
    unsigned short mode = *(unsigned short*)(inode);
    if (!(mode & 0x4000)) return;
    unsigned char *dir_buf = dir_scan_buf;
    unsigned int fsize = read_inode_data(inode, dir_buf, 56 * 1024);
    unsigned char *ptr = dir_buf;
    while (ptr + 8 <= dir_buf + fsize) {
        unsigned int ino = *(unsigned int*)ptr;
        unsigned short rec = *(unsigned short*)(ptr + 4);
        unsigned char nlen = *(unsigned char*)(ptr + 6);
        unsigned char ftype = *(unsigned char*)(ptr + 7);
        if (rec < 8 || ptr + rec > dir_buf + fsize) break;
        if (ino && nlen) {
            char name[256]; my_strncpy(name, (char*)ptr + 8, nlen); name[nlen] = 0;
            if (!(my_strcmp(name, ".") == 0 || my_strcmp(name, "..") == 0)) {
                for (int i = 0; i < depth; i++) fsop_cat(out, outsz, len, "  ");
                if (ftype == 2) { fsop_cat(out, outsz, len, "["); fsop_cat(out, outsz, len, name); fsop_cat(out, outsz, len, "]\n"); }
                else            { fsop_cat(out, outsz, len, "  "); fsop_cat(out, outsz, len, name); fsop_cat(out, outsz, len, "\n"); }
                (*count)++;
                if (ftype == 2) {
                    fsop_tree_rec(ino, depth + 1, count, out, outsz, len);
                    /* 递归覆盖了 dir_scan_buf, 重新读取本目录后继续 */
                    fsize = read_inode_data(inode, dir_buf, 56 * 1024);
                }
            }
        }
        ptr += rec;
    }
}

static int efs_fsop(const char *op, const char *a, const char *b, char *out, int outsz) {
    int len = 0;
    if (!op) return -1;
    if (out && outsz > 0) out[0] = 0;

    /* ---------- ext4 修改类 ---------- */
    if (my_strcmp(op, "ln") == 0) {              /* ln <target> <linkname> */
        if (!current_dir_ino || !a || !b) return 0;
        return (int)ext4_symlink(current_dir_ino, b, a);
    }
    if (my_strcmp(op, "readlink") == 0) {
        if (!current_dir_ino || !a || !out || outsz <= 0) return -1;
        unsigned int ino = find_in_dir(current_dir_ino, a);
        if (!ino) return -1;
        return ext4_readlink(ino, out, (unsigned int)outsz);
    }
    if (my_strcmp(op, "truncate") == 0) {
        if (!current_dir_ino || !a) return -1;
        unsigned int ino = find_in_dir(current_dir_ino, a);
        if (!ino) return -1;
        unsigned int nsz = (unsigned int)my_atoi(b ? b : "0");
        return ext4_truncate(ino, nsz) == 0 ? 0 : -1;
    }
    if (my_strcmp(op, "rmdir") == 0) {
        if (!current_dir_ino || !a) return -1;
        return ext4_rmdir(current_dir_ino, a);
    }
    if (my_strcmp(op, "unlink") == 0) {
        if (!current_dir_ino || !a) return -1;
        return ext4_unlink(current_dir_ino, a);
    }
    if (my_strcmp(op, "sync") == 0) {
        return disk_flush();
    }
    if (my_strcmp(op, "append") == 0) {          /* a=file b=text -> 返回总大小 */
        if (!current_dir_ino || !a || !b) return -1;
        unsigned int ino = find_in_dir(current_dir_ino, a);
        if (!ino) return -1;
        return ext4_append_file(ino, (const unsigned char*)b, (unsigned int)my_strlen(b));
    }
    if (my_strcmp(op, "shutdown") == 0) {
        /* ACPI shutdown: PM1a_CNT 0x604 + 旧版 QEMU 0xB004 回退 */
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);
        return -1;   /* 若执行到这里说明关机未生效 */
    }

    /* ---------- 文本信息类 ---------- */
    if (my_strcmp(op, "pwd") == 0) {
        fsop_cat(out, outsz, &len, current_path);
        return len;
    }
    if (my_strcmp(op, "date") == 0) {            /* CMOS RTC (BCD) */
        outb(0x70, 0x00); unsigned char s = inb(0x71);
        outb(0x70, 0x02); unsigned char m = inb(0x71);
        outb(0x70, 0x04); unsigned char h = inb(0x71);
        outb(0x70, 0x07); unsigned char d = inb(0x71);
        outb(0x70, 0x08); unsigned char mo = inb(0x71);
        outb(0x70, 0x09); unsigned char y = inb(0x71);
        fsop_cat_dec2(out, outsz, &len, ((h >> 4) & 0xF) * 10 + (h & 0xF));
        fsop_cat(out, outsz, &len, ":");
        fsop_cat_dec2(out, outsz, &len, ((m >> 4) & 0xF) * 10 + (m & 0xF));
        fsop_cat(out, outsz, &len, ":");
        fsop_cat_dec2(out, outsz, &len, ((s >> 4) & 0xF) * 10 + (s & 0xF));
        fsop_cat(out, outsz, &len, " ");
        fsop_cat_dec2(out, outsz, &len, ((mo >> 4) & 0xF) * 10 + (mo & 0xF));
        fsop_cat(out, outsz, &len, "/");
        fsop_cat_dec2(out, outsz, &len, ((d >> 4) & 0xF) * 10 + (d & 0xF));
        fsop_cat(out, outsz, &len, "/20");
        fsop_cat_dec2(out, outsz, &len, ((y >> 4) & 0xF) * 10 + (y & 0xF));
        return len;
    }
    if (my_strcmp(op, "sysinfo") == 0) {
        fsop_cat(out, outsz, &len, TR("== System Info ==\n",           "== 系统信息 ==\n"));
        fsop_cat(out, outsz, &len, TR("Kernel: EFMOS v1.0.0\n",       "内核版本: EFMOS v1.0.0\n"));
        fsop_cat(out, outsz, &len, TR("CPU cores: ",                  "CPU 核心数: "));
        fsop_cat_dec(out, outsz, &len, (long)get_cpu_cores());
        fsop_cat(out, outsz, &len, "\n");
        unsigned long long mem_kb = get_mem_size_kb();
        fsop_cat(out, outsz, &len, TR("Memory:   ",                   "内存:     "));
        fsop_cat_dec(out, outsz, &len, (long)(mem_kb / 1024));
        fsop_cat(out, outsz, &len, TR(" MB (",                        " MB (合 "));
        fsop_cat_dec(out, outsz, &len, (long)mem_kb);
        fsop_cat(out, outsz, &len, TR(" KB)\n",                       " KB)\n"));
        fsop_cat(out, outsz, &len, TR("Disk:     ",                   "磁盘:     "));
        fsop_cat(out, outsz, &len, disk_type == 2 ? "AHCI" : (disk_type == 1 ? "IDE" : TR("none", "无")));
        fsop_cat(out, outsz, &len, "\n");
        fsop_cat(out, outsz, &len, TR("ext4:     start_lba=0x",       "ext4:     起始 LBA = 0x"));
        fsop_cat_hex(out, outsz, &len, ext4_start_lba);
        fsop_cat(out, outsz, &len, TR(" bs=",                        " 块大小 = "));
        fsop_cat_dec(out, outsz, &len, ext4_bs);
        fsop_cat(out, outsz, &len, TR(" (read-write)\n",              " (读写模式)\n"));
        fsop_cat(out, outsz, &len, TR("ext4 inodes/group: ",          "ext4 每块组 inode: "));
        fsop_cat_dec(out, outsz, &len, inodes_per_group);
        fsop_cat(out, outsz, &len, TR("  inode_size: ",              "  inode 大小: "));
        fsop_cat_dec(out, outsz, &len, inode_size);
        fsop_cat(out, outsz, &len, "\n");
        fsop_cat(out, outsz, &len, TR("CWD:      ",                   "当前目录: "));
        fsop_cat(out, outsz, &len, current_path);
        fsop_cat(out, outsz, &len, "\n");
        fsop_cat(out, outsz, &len, TR("CWD ino:  ",                   "目录 ino: "));
        fsop_cat_dec(out, outsz, &len, current_dir_ino);
        fsop_cat(out, outsz, &len, "\n");
        return len;
    }
    if (my_strcmp(op, "memfree") == 0) {
        unsigned int count = *get_file_count_ptr();
        struct file_entry *ft = get_file_table();
        unsigned int used = 0;
        for (unsigned int i = 0; i < count; i++) used += ft[i].size;
        fsop_cat(out, outsz, &len, TR("Memory FS: ",                  "内存文件系统: "));
        fsop_cat_dec(out, outsz, &len, used);
        fsop_cat(out, outsz, &len, TR(" / 262144 bytes used (",       " / 262144 字节 已用 ("));
        fsop_cat_dec(out, outsz, &len, used * 100 / 262144);
        fsop_cat(out, outsz, &len, TR("%)\nEntries:   ",              "%)\n条目数:   "));
        fsop_cat_dec(out, outsz, &len, count);
        fsop_cat(out, outsz, &len, TR(" / 512\n",                    " / 512\n"));
        return len;
    }
    if (my_strcmp(op, "df") == 0) {
        fsop_cat(out, outsz, &len,
            TR("Filesystem    Start LBA    BS      FreeBlocks  FreeInodes  Mode\n",
               "文件系统       起始LBA      块大小   空闲块       空闲Inode   模式\n"));
        if (ext4_start_lba) {
            unsigned int fb = *(unsigned int*)(sb_buf + 0x0C);
            unsigned int fi = *(unsigned int*)(sb_buf + 0x10);
            fsop_cat(out, outsz, &len, "ext4          0x");
            fsop_cat_hex(out, outsz, &len, ext4_start_lba);
            fsop_cat(out, outsz, &len, "      ");
            fsop_cat_dec(out, outsz, &len, ext4_bs);
            fsop_cat(out, outsz, &len, "   ");
            fsop_cat_dec(out, outsz, &len, fb);
            fsop_cat(out, outsz, &len, "       ");
            fsop_cat_dec(out, outsz, &len, fi);
            fsop_cat(out, outsz, &len, TR("    read-write\n", "    读写\n"));
        }
        fsop_cat(out, outsz, &len,
            TR("memfs         -            256KB   -           -           read-write\n",
               "内存文件系统   -            256KB   -           -           读写\n"));
        return len;
    }
    if (my_strcmp(op, "stat") == 0) {
        if (!current_dir_ino || !a) return -1;
        unsigned int ino = find_in_dir(current_dir_ino, a);
        if (!ino) { fsop_cat(out, outsz, &len, TR("File not found\n","找不到文件\n")); return len; }
        unsigned char inode[256]; read_inode(ino, inode);
        unsigned short mode = *(unsigned short*)(inode);
        unsigned int size = *(unsigned int*)(inode + 4);
        fsop_cat(out, outsz, &len, TR("File:  ", "文件:  ")); fsop_cat(out, outsz, &len, a); fsop_cat(out, outsz, &len, "\n");
        fsop_cat(out, outsz, &len, TR("FS:    ext4\n", "文件系统: ext4\n"));
        fsop_cat(out, outsz, &len, TR("Inode: ", "Inode: ")); fsop_cat_dec(out, outsz, &len, ino); fsop_cat(out, outsz, &len, "\n");
        fsop_cat(out, outsz, &len, TR("Type:  ", "类型:  "));
        if (mode & 0x4000) fsop_cat(out, outsz, &len, TR("directory\n","目录\n"));
        else if ((mode & 0xF000) == 0xA000) {
            char target[256];
            fsop_cat(out, outsz, &len, TR("symlink -> ","符号链接 -> "));
            if (ext4_readlink(ino, target, sizeof(target)) >= 0) fsop_cat(out, outsz, &len, target);
            fsop_cat(out, outsz, &len, "\n");
        }
        else if (mode & 0x8000) fsop_cat(out, outsz, &len, TR("regular file\n","普通文件\n"));
        else fsop_cat(out, outsz, &len, TR("other\n","其他\n"));
        fsop_cat(out, outsz, &len, TR("Mode:  0x","权限:  0x")); fsop_cat_hex(out, outsz, &len, mode); fsop_cat(out, outsz, &len, "\n");
        fsop_cat(out, outsz, &len, TR("Size:  ","大小:  ")); fsop_cat_dec(out, outsz, &len, size); fsop_cat(out, outsz, &len, TR(" bytes\n"," 字节\n"));
        return len;
    }
    if (my_strcmp(op, "tree") == 0) {
        if (!current_dir_ino) return -1;
        int count = 0;
        fsop_cat(out, outsz, &len, current_path); fsop_cat(out, outsz, &len, "\n");
        fsop_tree_rec(current_dir_ino, 1, &count, out, outsz, &len);
        fsop_cat(out, outsz, &len, "\n");
        fsop_cat_dec(out, outsz, &len, count);
        fsop_cat(out, outsz, &len, TR(" entries\n"," 个条目\n"));
        return count;
    }
    if (my_strcmp(op, "find") == 0) {
        if (!current_dir_ino || !a) return -1;
        unsigned int ino = ext4_find_recursive(current_dir_ino, a, 16);
        if (ino) {
            unsigned char inode[256]; read_inode(ino, inode);
            unsigned short mode = *(unsigned short*)(inode);
            unsigned int size = *(unsigned int*)(inode + 4);
            fsop_cat(out, outsz, &len, TR("Found: inode=","已找到: inode="));
            fsop_cat_dec(out, outsz, &len, ino); fsop_cat(out, outsz, &len, "\n");
            fsop_cat(out, outsz, &len, TR("Type: ","类型: "));
            fsop_cat(out, outsz, &len, (mode & 0x4000) ? TR("directory","目录") : TR("file","文件"));
            fsop_cat(out, outsz, &len, TR("  Size: ","  大小: ")); fsop_cat_dec(out, outsz, &len, size); fsop_cat(out, outsz, &len, TR(" bytes\n"," 字节\n"));
        } else {
            fsop_cat(out, outsz, &len, TR("Not found\n","未找到\n"));
        }
        return (int)ino;
    }
    if (my_strcmp(op, "du") == 0) {
        if (!current_dir_ino) return -1;
        unsigned long long total = ext4_du_recursive(current_dir_ino, 16);
        fsop_cat(out, outsz, &len, current_path); fsop_cat(out, outsz, &len, ": ");
        fsop_cat_dec(out, outsz, &len, (long)total); fsop_cat(out, outsz, &len, TR(" bytes\n"," 字节\n"));
        return (int)total;
    }
    if (my_strcmp(op, "ps") == 0) {
        fsop_cat(out, outsz, &len,
            TR("  PID  STATE      NICE  NAME\n",
               "  PID  状态       优级  名称\n"));
        int n = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            struct task_struct *t = g_task_table[i];
            if (!t) continue;
            const char *st;
            if (t->state == TS_NEW)       st = TR("NEW    ","新建    ");
            else if (t->state == TS_READY)   st = TR("READY  ","就绪  ");
            else if (t->state == TS_RUNNING) st = TR("RUNNING","运行中");
            else if (t->state == TS_WAITING) st = TR("WAIT   ","等待  ");
            else if (t->state == TS_ZOMBIE)  st = TR("ZOMBIE ","僵尸   ");
            else                              st = TR("DEAD   ","消亡   ");
            if (t->state == TS_DEAD) continue;
            fsop_cat_dec(out, outsz, &len, t->pid);
            fsop_cat(out, outsz, &len, "  ");
            fsop_cat(out, outsz, &len, st);
            fsop_cat(out, outsz, &len, "  ");
            fsop_cat_dec(out, outsz, &len, t->nice);
            fsop_cat(out, outsz, &len, "  ");
            fsop_cat(out, outsz, &len, t->name);
            fsop_cat(out, outsz, &len, "\n");
            n++;
        }
        fsop_cat(out, outsz, &len, "\n");
        fsop_cat_dec(out, outsz, &len, n);
        fsop_cat(out, outsz, &len, TR(" tasks\n"," 个进程\n"));
        return n;
    }
    if (my_strcmp(op, "memls") == 0) {
        unsigned int count = *get_file_count_ptr();
        struct file_entry *ft = get_file_table();
        for (unsigned int i = 0; i < count; i++) {
            if (ft[i].type == 1) { fsop_cat(out, outsz, &len, "["); fsop_cat(out, outsz, &len, ft[i].path); fsop_cat(out, outsz, &len, "]\n"); }
            else {
                fsop_cat(out, outsz, &len, ft[i].path);
                fsop_cat(out, outsz, &len, "  ");
                fsop_cat_dec(out, outsz, &len, ft[i].size);
                fsop_cat(out, outsz, &len, "\n");
            }
        }
        fsop_cat(out, outsz, &len, "\n");
        fsop_cat_dec(out, outsz, &len, count);
        fsop_cat(out, outsz, &len, TR(" entries (memory FS)\n"," 个条目 (内存文件系统)\n"));
        return (int)count;
    }
    if (my_strcmp(op, "memread") == 0) {
        if (!a || !out || outsz <= 0) return -1;
        char full[256]; resolve_path(a, full);
        int idx = find_entry(full);
        if (idx < 0) return -1;
        struct file_entry *ft = get_file_table();
        if (ft[idx].type != 0) return -2;
        unsigned int sz = ft[idx].size;
        if ((int)sz > outsz) sz = (unsigned int)outsz;
        unsigned char *data = get_file_content_base() + ft[idx].offset;
        for (unsigned int i = 0; i < sz; i++) out[i] = (char)data[i];
        return (int)sz;
    }
    if (my_strcmp(op, "wminfo") == 0) {
        fsop_cat(out, outsz, &len, TR("WM: ","窗口管理器: "));
        fsop_cat_dec(out, outsz, &len, g_wm_enabled ? 1 : 0);
        fsop_cat(out, outsz, &len, TR("  Compositor: ","  合成器: "));
        fsop_cat_dec(out, outsz, &len, g_compositor_active ? 1 : 0);
        int wc = 0;
        for (int i = 0; i < WM_MAX_WINDOWS; i++) if (g_wm_windows[i].alive) wc++;
        fsop_cat(out, outsz, &len, TR("  Windows: ","  窗口数: "));
        fsop_cat_dec(out, outsz, &len, wc);
        fsop_cat(out, outsz, &len, TR("  Focus wid: ","  焦点窗口: "));
        fsop_cat_dec(out, outsz, &len, g_wm_focus_wid);
        fsop_cat(out, outsz, &len, "\n");
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            struct wm_window *w = &g_wm_windows[i];
            if (!w->alive) continue;
            fsop_cat(out, outsz, &len, TR("  wid=","  窗口ID="));
            fsop_cat_dec(out, outsz, &len, w->wid);
            fsop_cat(out, outsz, &len, TR(" pid="," PID="));
            fsop_cat_dec(out, outsz, &len, w->pid);
            fsop_cat(out, outsz, &len, TR(" pos="," 坐标="));
            fsop_cat_dec(out, outsz, &len, w->x); fsop_cat(out, outsz, &len, ",");
            fsop_cat_dec(out, outsz, &len, w->y); fsop_cat(out, outsz, &len, TR(" size="," 尺寸="));
            fsop_cat_dec(out, outsz, &len, w->w); fsop_cat(out, outsz, &len, "x");
            fsop_cat_dec(out, outsz, &len, w->h); fsop_cat(out, outsz, &len, TR(" z="," 叠层="));
            fsop_cat_dec(out, outsz, &len, w->z_order);
            fsop_cat(out, outsz, &len, " '");
            fsop_cat(out, outsz, &len, w->title);
            fsop_cat(out, outsz, &len, "'\n");
        }
        return wc;
    }
    return -1;
}

/* ========== 多核 (SMP) 启动 ==========
 * 步骤:
 *   1. 解析 RSDP → XSDT/RSDT → MADT (APIC)
 *   2. 遍历 MADT: 记录 Local APIC ID (Processor Local APIC 条目 type=0),
 *      收集 g_smp_cpus[].
 *   3. 初始化 BSP LAPIC (软件 enable, 设置 SVR spurious vector)
 *   4. 准备 AP 启动码 (AP trampoline): 复制到 0x8000 (实模式可用内存低位).
 *      切换到 64 位, 设栈, lgdt, 跳到 smp_ap_entry() C 函数.
 *   5. 对每个非 BSP CPU 发送 INIT-SIPI-SIPI IPI.
 *   6. AP 启动后: 设置自己的 LAPIC, 进入 per-CPU idle loop.
 */
#define MAX_CPUS 16
static int g_smp_cpu_count = 1;           /* 至少 1 (BSP) */
static unsigned char g_smp_apic_ids[MAX_CPUS];
static volatile int g_smp_aps_started = 0; /* AP 每启动一个 +1, BSP 轮询等待 */
static unsigned long g_smp_ap_stacks[MAX_CPUS];  /* 每个 AP 的栈顶 (高地址) */

/* RSDP (Root System Description Pointer). 2.0+ 版本用 XSDT, 否则 RSDT */
typedef struct {
    char signature[8];     /* "RSD PTR " */
    unsigned char chksum;
    char oemid[6];
    unsigned char revision;
    unsigned int rsdt_addr;
    unsigned int length;
    unsigned long xsdt_addr;
    unsigned char ext_chksum;
    unsigned char reserved[3];
} rsdp_t;
typedef struct {
    char signature[4];     /* "XSDT" / "RSDT" / "APIC" */
    unsigned int length;
    unsigned char revision, chksum, oemid[6], oemtblid[8];
    unsigned int oemrev;
    unsigned int creator_id;
    unsigned int creator_rev;
} acpi_hdr_t;
typedef struct {
    acpi_hdr_t hdr;
    unsigned int lapic_addr;
    unsigned int flags;
} madt_t;

/* RSDP 查找:
 * 1. 优先读 bootloader 从 UEFI 配置表传递的 RSDP 指针 (存于 0x1518)
 * 2. 回退到扫描 BIOS ROM 区 0xE0000~0xFFFFF (SeaBIOS 兼容) */
static rsdp_t *find_rsdp(void) {
    /* [优先] bootloader 传递的 UEFI 配置表 RSDP 地址 */
    unsigned long long boot_rsdp = *(unsigned long long*)0x1518UL;
    if (boot_rsdp) {
        rsdp_t *r = (rsdp_t*)(unsigned long)boot_rsdp;
        if (r->signature[0]=='R' && r->signature[1]=='S' && r->signature[2]=='D'
        &&  r->signature[3]==' ' && r->signature[4]=='P' && r->signature[5]=='T'
        &&  r->signature[6]=='R' && r->signature[7]==' ') {
            unsigned char s = 0;
            const unsigned char *p = (const unsigned char*)r;
            for (int i = 0; i < 20; i++) s += p[i];
            if (s == 0) return r;
        }
    }
    /* [回退] 扫描 BIOS ROM 区 (每 16 字节对齐) */
    unsigned char *p = (unsigned char*)0xE0000UL;
    for (; (unsigned long)p < 0xFFFFFUL; p += 16) {
        if (p[0]=='R' && p[1]=='S' && p[2]=='D' && p[3]==' '
        &&  p[4]=='P' && p[5]=='T' && p[6]=='R' && p[7]==' ') {
            rsdp_t *r = (rsdp_t*)p;
            /* 校验和 */
            unsigned char s = 0;
            for (int i = 0; i < 20; i++) s += p[i];
            if (s) continue;
            return r;
        }
    }
    return 0;
}

/* 验证 ACPI table header checksum */
static int acpi_chksum_ok(const acpi_hdr_t *h) {
    unsigned char s = 0;
    const unsigned char *p = (const unsigned char*)h;
    for (unsigned int i = 0; i < h->length; i++) s += p[i];
    return s == 0;
}

/* 在 XSDT/RSDT 中按 signature 找一张子表 */
static void *find_acpi_table(rsdp_t *r, const char sig[4]) {
    if (!r) return 0;
    acpi_hdr_t *root;
    int use_xsdt = 0;
    if (r->revision >= 2 && r->xsdt_addr) {
        root = (acpi_hdr_t*)(unsigned long)r->xsdt_addr;
        use_xsdt = 1;
    } else if (r->rsdt_addr) {
        root = (acpi_hdr_t*)(unsigned long)r->rsdt_addr;
    } else return 0;
    if (!acpi_chksum_ok(root)) return 0;
    /* 比对签名本身是否 RSDT/XSDT */
    if (use_xsdt && !(root->signature[0]=='X'&&root->signature[1]=='S'
                    &&root->signature[2]=='D'&&root->signature[3]=='T')) return 0;
    if (!use_xsdt && !(root->signature[0]=='R'&&root->signature[1]=='S'
                    &&root->signature[2]=='D'&&root->signature[3]=='T')) return 0;

    unsigned int entry_sz = use_xsdt ? 8 : 4;
    unsigned long entries_off = sizeof(acpi_hdr_t);
    unsigned long n = (root->length - entries_off) / entry_sz;
    for (unsigned long i = 0; i < n; i++) {
        unsigned long tbl_addr;
        if (use_xsdt) tbl_addr = *(unsigned long*)((unsigned char*)root + entries_off + i*8);
        else          tbl_addr = *(unsigned int*) ((unsigned char*)root + entries_off + i*4);
        acpi_hdr_t *t = (acpi_hdr_t*)tbl_addr;
        if (t->signature[0]==sig[0] && t->signature[1]==sig[1]
        &&  t->signature[2]==sig[2] && t->signature[3]==sig[3]
        &&  acpi_chksum_ok(t)) return t;
    }
    return 0;
}

/* 解析 MADT: 枚举所有 CPU 本地 APIC ID, 返回发现的 CPU 数 */
static int smp_enumerate_cpus(void) {
    rsdp_t *rsdp = find_rsdp();
    if (!rsdp) { serial_write("SMP: RSDP not found\n"); return 1; }
    madt_t *madt = (madt_t*)find_acpi_table(rsdp, "APIC");
    if (!madt) { serial_write("SMP: MADT/APIC not found\n"); return 1; }
    /* 更新 LAPIC 基地址 (MADT 会给出实际映射, 通常还是 0xFEE00000) */
    if (madt->lapic_addr) g_lapic_base = (unsigned long)madt->lapic_addr;

    g_smp_apic_ids[0] = (unsigned char)lapic_id();
    int count = 1;
    unsigned char *entries = (unsigned char*)(madt + 1);
    unsigned int length = madt->hdr.length - sizeof(madt_t);
    unsigned int i = 0;
    while (i < length && count < MAX_CPUS) {
        unsigned char type = entries[i];
        unsigned char rec_len = entries[i+1];
        if (rec_len < 2) break;
        if (type == 0) { /* Processor Local APIC */
            /* offset 2 = acpi_processor_id, 3 = apic_id, 4..7 = flags (bit0 = enabled) */
            unsigned char apic_id = entries[i + 3];
            unsigned int flags = *(unsigned int*)(entries + i + 4);
            if (flags & 1) {
                /* 避免重复添加 BSP */
                int dup = 0;
                for (int k = 0; k < count; k++) if (g_smp_apic_ids[k] == apic_id) { dup = 1; break; }
                if (!dup) { g_smp_apic_ids[count++] = apic_id; }
            }
        }
        i += rec_len;
    }
    g_smp_cpu_count = count;
    return count;
}

/* AP trampoline: 位置 0x8000 (低内存, 实模式可达).
 * 功能: AP 在收到 SIPI 后 从 0x8000:0x0000 (0x80000 物理) 以实模式启动,
 *   - 关中断, 加载 GDT
 *   - 开启 PE (CR0.PE=1 + PAE CR4.PAE=1)
 *   - 用 64-bit long mode: 设置 EFER MSR.LME=1, 打开 CR0.PG
 *   - 进入 CPL0 64 位, lgdt, 设 rsp (从 per-CPU 数组), jmp smp_ap_main()
 *
 * 为简化实现, 我们直接在下面用 __asm__ 写入一段小汇编到 0x80000.
 * 注: 0x80000~0x8FFFF 是 64KB, 足够放 trampoline + 1 个临时 GDT. */

/* AP C 入口 (在 trampoline 切换到 64 位后 call) */
static void smp_ap_main(int cpu_idx) {
    serial_write("SMP: AP "); serial_write_hex(cpu_idx);
    serial_write(" booted (LAPIC ID="); serial_write_hex(lapic_id());
    serial_write(")\n");
    /* AP LAPIC enable */
    lapic_write(LAPIC_SIVR, 0xFF | LAPIC_ENABLE);
    /* 设置好 gdt + ds/es/fs/gs/ss 选择子 (trampoline 已设置, 保险重设) */
    load_gdt();
    /* 标记已启动 */
    __atomic_add_fetch(&g_smp_aps_started, 1, __ATOMIC_RELEASE);
    /* AP idle loop: hlt forever (等待以后调度器迁移任务过来). */
    for (;;) {
        asm volatile("cli; hlt; sti; pause");
    }
}

/* [简化 SIPI 方案] 我们让 trampoline 很简单: 只做最小初始化.
 * 实际实现: 直接写机器码到 0x80000 (物理地址 = 线性地址, UEFI 身份映射).
 * 这是一段 32 位保护模式 + 64 位长模式切换的 trampoline 模板,
 * 末尾用 C 参数填充:  gdt 地址 / ap_main 地址 / cpu_id 号 / rsp 栈顶 */
#define TRAMPOLINE_BASE 0x80000UL

/* BSP 通过 ICR 发送 IPI 给指定 APIC ID */
static void lapic_send_ipi(unsigned char apic_id, unsigned int icrlo) {
    /* 先等上次发送完成 */
    while (lapic_read(LAPIC_ICRLO) & (1<<12)) { asm volatile("pause"); }
    lapic_write(LAPIC_ICRHI, ((unsigned int)apic_id) << 24);
    lapic_write(LAPIC_ICRLO, icrlo);
    while (lapic_read(LAPIC_ICRLO) & (1<<12)) { asm volatile("pause"); }
}

/* 初始化 BSP LAPIC + 枚举 CPU + 启动所有 AP */
static void smp_init(void) {
    /* 1) 使能 BSP APIC: SVR[8]=Enable, 虚假中断向量 0xFF */
    lapic_write(LAPIC_SIVR, 0xFF | LAPIC_ENABLE);
    serial_write("SMP: BSP LAPIC enabled, base=");
    serial_write_hex(g_lapic_base); serial_write("\n");

    /* 2) 枚举 CPU */
    int n = smp_enumerate_cpus();
    serial_write("SMP: found "); serial_write_hex(n); serial_write(" CPUs\n");
    if (n <= 1) return;   /* Uniprocessor */

    /* 3) 为每个 AP 分配独立栈 (16KB 足够运行简单 idle loop) */
    for (int i = 1; i < n; i++) {
        unsigned long stk = (unsigned long)kheap_malloc(16UL*1024UL);
        if (!stk) { serial_write("SMP: AP stack alloc failed\n"); break; }
        g_smp_ap_stacks[i] = (stk + 16UL*1024UL) & ~0xFUL;
    }

    /* 4) 构造 trampoline 二进制: 写在 0x80000.
     *   布局:
     *    [0x000] 16位实模式段: 关中断, A20, lgdt, 转保护
     *    [0x020] 32位: 设 cr4.PAE, 设 cr3=0 (身份映射 1GB 就用 UEFI 的), efer.LME, cr0.PG, lgdt
     *           远跳到 64-bit 代码段
     *    [0x060] 64位: load gs/fs, mov cpu_idx->edi, mov rsp=stack, call smp_ap_main
     *    [0x100] GDT: 4 个描述符 (null, 64-bit code, 64-bit data, tss 占位)
     *    [0x200] gdtr + 变量 (cpu_idx 数, ap_main 地址, stack 地址 每核一个)
     *
     *   [简化实现] 我们使用一个已"准备好"的最小机器码序列,
     *   利用 UEFI 在进入内核前已经关闭分页 identity map 的事实,
     *   直接跳到 64 位 C 入口 (不需要重设页表).
     */
    unsigned char *t = (unsigned char*)TRAMPOLINE_BASE;
    /* 清 1KB 作为整个 trampoline 区 */
    for (int i = 0; i < 1024; i++) t[i] = 0x90; /* NOP; 避免执行旧数据 */

    /* 变量槽: 写在 trampoline 尾部 (相对偏移固定) */
    /* 0x90000 里也能写, 但为整洁, 用 trampoline 末尾 + 0x300 = 0x80300 起 */
    volatile unsigned char *vars = (volatile unsigned char*)0x80300UL;
    /* 0x300: 32 位 gdtr (limit:31-16, base:0-31, 分两槽) */
    /* 0x310: 64 位 gdtr (limit: 2B, base: 8B) */
    /* 0x320: gdt 拷贝 */

    /* 构造一个最小 GDT 在 0x80320 (使用与内核 struct gdt_entry 相同的字段名) */
    struct gdt_entry *mini_gdt = (struct gdt_entry*)0x80320UL;
    for (int i = 0; i < 4; i++) my_memset(&mini_gdt[i], 0, sizeof(struct gdt_entry));
    /* index 1: 64 位 code, 非一致, DPL0, 存在, L=1 (长模式 64), G=1 粒度4KB */
    mini_gdt[1].limit_low    = 0xFFFF;
    mini_gdt[1].base_low     = 0;
    mini_gdt[1].base_mid     = 0;
    mini_gdt[1].access       = 0x9A;   /* P=1,DPL=0,Executable,Read */
    mini_gdt[1].granularity  = 0xAF;   /* G=1, D=0(长模式下=0),L=1,AVL=0 limit hi=0xF */
    mini_gdt[1].base_high    = 0;
    /* index 2: 64 位 data */
    mini_gdt[2].limit_low    = 0xFFFF;
    mini_gdt[2].access       = 0x92;
    mini_gdt[2].granularity  = 0xAF;
    /* 64-bit GDTR (放在 0x80310, limit:2B, base:8B) */
    unsigned short *gdt_limit = (unsigned short*)0x80310UL;
    unsigned long  *gdt_base  = (unsigned long *)0x80312UL;
    *gdt_limit = (unsigned short)(4 * 8 - 1);
    *gdt_base  = (unsigned long)mini_gdt;

    /* 把 per-CPU 入口地址写到 0x80400, 供以后真正 SIPI handler 取 */
    volatile unsigned long *ap_entry_ptr = (volatile unsigned long*)0x80400UL;
    *ap_entry_ptr = (unsigned long)smp_ap_main;
    for (int i = 1; i < n; i++) {
        volatile unsigned long *apstk = (volatile unsigned long*)(0x80410UL + i*8);
        *apstk = g_smp_ap_stacks[i];
    }

    /* [安全] 不发送 INIT-SIPI-SIPI。
     * 原因: SIPI 让 AP 以实模式跳转到 0x80000, 但该处只有 NOP 填充,
     *   没有完整的 16位→32位→64位 切换代码。AP 执行 NOP 后跑进垃圾数据,
     *   在 VMware 上导致 "vcpu-1 tried to execute invalid part of memory" 崩溃。
     *   QEMU 对此较宽容 (AP 停在 HLT 状态), 但真实/VMware 环境会硬崩溃。
     * 当前保留: CPU 枚举 + LAPIC 初始化 + per-CPU 栈预留,
     *   为将来补全实模式 trampoline 后直接启用 AP 做准备。 */
    serial_write("SMP: AP cores detected but not started (trampoline incomplete)\n");
}

/* 启动 banner: 大号像素级 EFMOS LOGO + 版本 + CPU 核数 + 内存大小 */
static void print_efmos_banner(void) {
    /* [像素级大字] 在屏幕上方居中绘制渐变色 EFMOS,
     * scale=4 每字母 32x64 像素, 5 字母总宽约 200 像素,
     * 对 800x600 / 1024x768 等常见分辨率都友好。 */
    int logo_h = draw_efmos_logo(4);
    /* 把文本光标放到 LOGO 下方 (向下取整到 FONT_H 像素字符行高),
     * 额外留 2 个字符行的空白做边距。 */
    cursor_x = 0;
    cursor_y = ((logo_h + FONT_H - 1) / FONT_H + 2) * FONT_H;
    /* 信息文本 (双语) */
    print_string(TR("EFMOS Operating System   v1.0.0\n",
                    "EFMOS 操作系统   v1.0.0\n"));
    print_string(TR("CPU cores: ", "CPU 核心数: "));
    print_dec(get_cpu_cores());
    print_string(TR("    Memory: ", "    内存: "));
    unsigned long long mem_kb = get_mem_size_kb();
    print_dec((unsigned int)(mem_kb / 1024));
    print_string(TR(" MB (", " MB ("));
    print_dec((unsigned int)mem_kb);
    print_string(TR(" KB)\n", " KB)\n"));
    print_string(TR("Disk: ", "磁盘: "));
    print_string(disk_type==2 ? "AHCI" : (disk_type==1 ? "IDE" : "none"));
    print_string(TR("    Type 'help' for command list.\n",
                    "    输入 'help' 查看命令列表。\n"));
    print_string("\n");
}

/* 配置解析辅助: 比较键名 (长度受限, 避免依赖 null 终止) */
static int key_eq(const char *a, int al, const char *b, int bl) {
    if (al != bl) return 0;
    for (int i = 0; i < al; i++)
        if ((unsigned char)a[i] != (unsigned char)b[i]) return 0;
    return 1;
}

void kmain(void) {
    serial_write("\nEFMOS Kernel\n");
    init_gdt(); load_gdt(); serial_write("GDT OK\n");
    init_idt(); load_idt(); serial_write("IDT OK\n");

    /* [关键修复] 启用 SSE/SSE2: 设置 CR4.OSFXSR (bit9) + CR4.OSXMMEXCPT (bit10),
     * 并确保 CR0.MP=1 (bit1), CR0.EM=0 (bit2 清除).
     * UEFI 固件可能未设置 OSFXSR, 导致 GCC 生成的 SSE 指令 (float 运算/SSE2 memset)
     * 在真实硬件上触发 #UD 异常. QEMU/OVMF 通常默认开启, 但真实 BIOS 不一定. */
    {
        unsigned long cr0, cr4;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= (1UL << 1);    /* MP: Monitor coProcessor */
        cr0 &= ~(1UL << 2);   /* EM: 清除 Emulation (让 SSE/x87 直接执行) */
        cr0 &= ~(1UL << 3);   /* TS: 清除 Task Switched */
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1UL << 9);    /* OSFXSR: Operating System supports FXSAVE/FXRSTOR */
        cr4 |= (1UL << 10);   /* OSXMMEXCPT: OS supports SSE exception (#XM) */
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
        /* 执行一次 fninit 初始化 x87 FPU 状态 */
        __asm__ volatile("fninit");
        serial_write("SSE: CR4.OSFXSR enabled\n");
    }

    musl_syscall_setup();

    /* [关键修复] 屏蔽 8259 PIC 所有中断:
     * 内核使用轮询模式处理所有 I/O (PS2 kbd/mouse, AHCI disk, LAPIC timer),
     * 不需要硬件中断。但 sti 后若 8259 未屏蔽, PS2 键盘 IRQ1 会触发
     * exception_handler (IDT 所有向量都指向它), 当 efs_active==1 时
     * 合成器被误判为"异常退出"导致崩溃。必须在 sti 前屏蔽。 */
    outb(0x21, 0xFF);   /* master PIC mask all */
    outb(0xA1, 0xFF);   /* slave PIC mask all */
    asm volatile("sti");

    /* [关键修复] PS/2 键盘+鼠标控制器初始化必须在磁盘控制器初始化之前!
     *   - AHCI/IDE 初始化会做 PCI 配置周期, 在某些 BIOS/QEMU 模式下
     *     会干扰 8042 AUX 状态位, 造成"键盘字节被标成鼠标字节"或反之。
     *   - 先把 ps2_init 做了, 8042 配置字节锁死 Set2→Set1 翻译后,
     *     后面 PCI 配置周期就不能再干扰扫描码了 */
    ps2_init();
    serial_write("PS2 init OK (kbd+mouse streaming)\n");
    mouse_set_cursor(1);   /* 显示系统级箭头光标 */

    if (ahci_init_driver() == 0) {
        disk_type = 2;
        serial_write("Using AHCI disk.\n");
    } else {
        serial_write("AHCI failed, trying IDE...\n");
        if (ide_init_driver() == 0) {
            disk_type = 1;
            serial_write("Using IDE disk.\n");
        } else {
            serial_write("Disk init failed, halt.\n");
            while(1) asm volatile("hlt");
        }
    }

    ext4_init();

    /* [多核 SMP 初始化] (在 sched_init 之前, 确保 kheap 还没被大量使用前给 AP 预留栈)
     * 会解析 MADT -> 枚举 CPU id -> 尝试 SIPI 唤醒 AP.
     * 失败 (实模式 trampoline 不完整时) 也不影响 BSP 单核心启动. */
    smp_init();

    /* [进程/调度初始化] 创建 idle + init 任务, 建立 runqueue. 必须在使用 sched_yield 前完成 */
    sched_init();
    serial_write("Scheduler OK (idle pid0 + init pid1)\n");

    /* [用户系统初始化] — 不创建 users.conf, 不创建默认 admin
     *   仅: 1) 确保 /users 目录存在 (用户文件夹)
     *       2) 确保 /users/data 目录存在 (用户个性化数据: setting.conf, passwd)
     *   用户列表 = /users 下的子目录 (排除 data)
     *   开机时总是启动 userman 让用户登录或新建用户 */
    {
        unsigned int root = 2;
        unsigned int users_ino = find_in_dir(root, "users");
        if (!users_ino) {
            users_ino = ext4_mkdir_inode(root, "users");
            serial_write("boot: created /users\n");
        }
        if (users_ino) {
            unsigned int data_ino = find_in_dir(users_ino, "data");
            if (!data_ino) {
                data_ino = ext4_mkdir_inode(users_ino, "data");
                serial_write("boot: created /users/data\n");
            }
        }
        /* [新增] /Program 目录: 非系统程序, 每个程序独立子目录, 如 /Program/gcc/gcc.efs */
        unsigned int prog_ino = find_in_dir(root, "Program");
        if (!prog_ino) {
            prog_ino = ext4_mkdir_inode(root, "Program");
            serial_write("boot: created /Program\n");
        }
        /* [新增 2026] /EFMOS/DRIVERS 目录: 存放所有 .drv 驱动程序,
         * 启动时 efmloader.efs 会依次扫描并加载。EFMOS/ 之前已存在 (放 kernel.efs 等),
         * 先找到 /EFMOS inode, 再在其下建 DRIVERS 子目录。*/
        unsigned int efmos_ino = find_in_dir(root, "EFMOS");
        if (efmos_ino) {
            unsigned int drv_ino = find_in_dir(efmos_ino, "DRIVERS");
            if (!drv_ino) {
                drv_ino = ext4_mkdir_inode(efmos_ino, "DRIVERS");
                serial_write("boot: created /EFMOS/DRIVERS\n");
            }
        }
    }

    /* [系统语言配置] 登录前尚无当前用户, 从 /users/sys.conf 读取系统级语言设置,
     * 使登录界面使用上次语言。文件格式: 单行 "lang=0" 或 "lang=1"。
     * 首次运行 (sys.conf 不存在) 时创建默认 sys.conf (lang=0), 后续不再由内核写入,
     * 仅 efmlogin 语言切换时修改此文件。 */
    {
        char sbuf[64];
        int sl = efs_file_read("/users/sys.conf", sbuf, sizeof(sbuf) - 1);
        if (sl > 0) {
            sbuf[sl] = 0;
            /* 简单解析 "lang=" */
            const char *p = sbuf;
            while (*p) {
                if (p[0]=='l' && p[1]=='a' && p[2]=='n' && p[3]=='g' && p[4]=='=') {
                    efm_lang = (p[5] == '1') ? 1 : 0;
                    break;
                }
                while (*p && *p != '\n') p++;
                if (*p) p++;
            }
            serial_write("boot: loaded system lang from /users/sys.conf\n");
        } else {
            /* 首次运行: 生成默认 sys.conf (lang=0) */
            const char *def = "lang=0\n";
            int dl = 0; while (def[dl]) dl++;
            (void)efs_file_write("/users/sys.conf", def, dl);
            serial_write("boot: first run, created default /users/sys.conf\n");
        }
    }

    /* [2026+] 驱动引导: 先由 kmain 同步执行 efmloader.efs 完成初始驱动加载,
     * 避免 kmain 阶段直接调调度器 yield 造成不稳定 (调度器此时还未跑过一次上下文切换)。
     * efmloader.efs 内部流程 (两阶段):
     *   1) 同步: 扫描 /EFMOS/DRIVERS/*.drv, 逐个 load_driver 注册; 打印统计;
     *            再调用 spawn_async("efmloader", "--background") 把自己以
     *            --background 参数启动为独立后台线程 (nice=0, 最高优先级, 不退出,
     *            持续监控热加载)。
     *   2) 异步后台: 每 3 秒扫描 /EFMOS/DRIVERS/, 自动热加载新增 .drv。
     * 这样 kmain 仅用稳定的 run_efs_ex (同步, 不改调度状态), 真正的后台常驻进程
     * 由 efmloader 在用户 API 调用 spawn_async 时创建, 此时调度器已经经历过若干次
     * 正常的 EFS 生命周期切换 (创建→运行→退出), 更稳定。 */
    {
        print_string(TR("Loading driver manager...\n",
                        "正在加载驱动管理器...\n"));
        int rc = run_efs_ex("efmloader", 0);
        if (rc < 0) {
            print_string(TR("efmloader.efs not found, skipping driver loading.\n",
                            "未找到 efmloader.efs, 跳过驱动加载。\n"));
        } else {
            int nd = 0;
            for (int i = 0; i < 16; i++) if (g_drv_registry[i].used) nd++;
            print_string(TR("Drivers loaded: ", "已加载驱动数: "));
            if (nd == 0) { put_char('0'); } else {
                int tmp = nd, p = 0; char dbuf[12];
                while (tmp) { dbuf[p++] = '0' + (tmp % 10); tmp /= 10; }
                while (p--) put_char(dbuf[p]);
            }
            print_string(TR(" (background monitor started)\n",
                            " (后台监控线程已启动)\n"));
        }
    }

    /* [关键修复] 内核 fallback: 如果 efmloader 没有成功加载 Graphics.drv,
     * 直接由内核加载。这确保双缓冲 (blit/flush/copy_back_rect_to_front) 始终可用,
     * 消除闪屏和鼠标拖尾。efmloader 可能因各种原因失败 (目录扫描结果为空,
     * struct 不匹配, 崩溃等), 内核直接加载是最可靠的兜底方案。 */
    if (!g_drv_gfx_ptr) {
        serial_write("boot: efmloader did not load Graphics.drv, trying direct load\n");
        /* 尝试多种可能的文件名 (make_disk.sh 用数字前缀, 也可能无前缀) */
        int grc = efs_load_driver("/EFMOS/DRIVERS/02-Graphics.drv");
        if (grc < 0) grc = efs_load_driver("/EFMOS/DRIVERS/Graphics.drv");
        if (grc < 0) grc = efs_load_driver("/EFMOS/DRIVERS/03-Graphics.drv");
        if (g_drv_gfx_ptr) {
            serial_write("boot: Graphics.drv loaded via kernel fallback\n");
        } else {
            serial_write("boot: WARNING - Graphics.drv fallback failed (rc=");
            serial_write_hex((unsigned long)(unsigned int)grc);
            serial_write("), dual-buffer disabled\n");
        }
    }
    /* 同样为 ahci.drv 做 fallback (虽然内核有内置 AHCI, 但驱动版本更优) */
    if (!g_drv_disk_ptr) {
        int arc = efs_load_driver("/EFMOS/DRIVERS/01-ahci.drv");
        if (arc < 0) efs_load_driver("/EFMOS/DRIVERS/ahci.drv");
    }

    /* 使用默认设置 (白字黑底, 光标闪烁) 填充屏幕 */
    fill_screen(bg);
    cursor_x = 0; cursor_y = 0;
    print_efmos_banner();

    /* [用户登录门禁] 以 /users/users.conf 内容为准:
     *   - users.conf 不存在或为空 (无账户) → 运行 userman (创建账户)
     *   - users.conf 有 ≥1 个用户名       → 运行 efmlogin (登录)
     * 循环直到有用户成功登录才放行进入 Shell。 */
    int login_attempts = 0;
    while (!current_username[0]) {
        /* 检查 users.conf 是否有实际用户条目 */
        int has_accounts = 0;
        {
            char chk[2048];
            int cl = efs_file_read("/users/users.conf", chk, sizeof(chk) - 1);
            if (cl > 0) {
                chk[cl] = 0;
                for (int i = 0; i < cl; i++) {
                    char c = chk[i];
                    /* 存在非空白/非换行字符 → 有内容 */
                    if (c != '\n' && c != '\r' && c != ' ' && c != '\t' && c != 0) {
                        has_accounts = 1;
                        break;
                    }
                }
            }
        }
        if (has_accounts) {
            print_string(TR("Launching login...\n","正在启动登录程序...\n"));
            (void)run_efs("efmlogin");
        } else {
            print_string(TR("Launching user manager (no accounts yet)...\n",
                            "正在启动用户管理器 (尚无账户)...\n"));
            (void)run_efs("userman");
        }
        login_attempts++;
        if (!current_username[0]) {
            /* 仍未登录: 延时 + 重新启动对应程序, 提示用户必须先登录 */
            for (volatile int k = 0; k < 8000000; k++) asm volatile("pause");
            print_string(TR("WARNING: No user logged in! Restarting.\n",
                            "警告: 尚未登录任何用户! 重新启动。\n"));
            if (login_attempts > 5) {
                print_string(TR("Login loop detected - please login or create a user first.\n",
                                "检测到登录循环 - 请登录或先创建新用户。\n"));
            }
        }
    }

    /* 登录成功后, 加载当前用户的个性化设置 (/users/data/<username>/setting.conf) */
    if (current_username[0]) {
        char spath[160];
        const char *prefix = "/users/data/";
        int pl = my_strlen(prefix);
        int ul = my_strlen(current_username);
        my_memcpy(spath, prefix, pl);
        my_memcpy(spath + pl, current_username, ul);
        my_memcpy(spath + pl + ul, "/setting.conf", 14);  /* 含 \0 */

        unsigned char conf_buf[512];
        int cs = efs_file_read(spath, (char*)conf_buf, sizeof(conf_buf) - 1);
        if (cs > 0) {
            conf_buf[cs] = 0;
            const char *p = (const char*)conf_buf;
            while (*p) {
                const char *eq = p;
                while (*eq && *eq != '=' && *eq != '\n') eq++;
                if (*eq != '=') { while (*p && *p != '\n') p++; if (*p) p++; continue; }
                int klen = (int)(eq - p);
                const char *val = eq + 1;
                const char *nl = val;
                while (*nl && *nl != '\n') nl++;
                int vlen = (int)(nl - val);
                if (key_eq(p, klen, "lang", 4)) {
                    efm_lang = (vlen > 0 && val[0] == '1') ? 1 : 0;
                } else if (key_eq(p, klen, "cursor_blink", 12)) {
                    cursor_visible = (vlen > 0 && val[0] == '1') ? 1 : 0;
                } else if (key_eq(p, klen, "fg", 2)) {
                    unsigned int v = 0; int i = 0;
                    if (val[0]=='0' && (val[1]=='x'||val[1]=='X')) i=2;
                    for (; i < 8 && i < vlen; i++) {
                        char c = val[i]; v <<= 4;
                        if      (c >= '0' && c <= '9') v |= c-'0';
                        else if (c >= 'a' && c <= 'f') v |= c-'a'+10;
                        else if (c >= 'A' && c <= 'F') v |= c-'A'+10;
                        else { v >>= 4; break; }
                    }
                    fg = v;
                } else if (key_eq(p, klen, "bg", 2)) {
                    unsigned int v = 0; int i = 0;
                    if (val[0]=='0' && (val[1]=='x'||val[1]=='X')) i=2;
                    for (; i < 8 && i < vlen; i++) {
                        char c = val[i]; v <<= 4;
                        if      (c >= '0' && c <= '9') v |= c-'0';
                        else if (c >= 'a' && c <= 'f') v |= c-'a'+10;
                        else if (c >= 'A' && c <= 'F') v |= c-'A'+10;
                        else { v >>= 4; break; }
                    }
                    bg = v;
                }
                p = nl; if (*p) p++;
            }
            serial_write("boot: loaded user settings for "); serial_write(current_username); serial_write("\n");
        }
        /* 注: /users/sys.conf 仅在首次运行时由 boot 生成, 不再在登录后同步写入。
         * efmlogin 的语言切换会直接修改 sys.conf; 登录后语言以当前用户 setting.conf 为准。 */
    }

    /* [登录后 · 纯 Shell] 取消桌面设计。登录后不启用 WM/合成器/efmAether,
     * 直接进入传统全屏 Shell, 保持与引导阶段相同的图形文字终端。
     * efmlogin/userman (登录前) 仍走原本的图形化界面。 */
    {
        struct gop_fb *fb = (struct gop_fb*)0x1000;
        if (fb && fb->fb_base) {
            /* 清屏: 清除 efmlogin 残留画面.
             * [关键] 用 fill_rect 逐行填整块屏幕 (宽×高), 而不是用 put_char 走
             * Graphics.drv 字符路径清屏 — 字符路径会触发 "空格字形宽 0 +
             * bearing_x 算错 → draw_x clamp 导致遗留 D 字形残像素" 的 bug。
             * fill_screen 填整个 screen_region 的 BGRx bg 色到 back buffer,
             * 然后立即 flush 到 front buffer, 保证 efmlogin 所有字形残像素
             * 被彻底覆盖, 不会留在 back buffer 里。 */
            fill_screen(bg);
            /* [关键修复] fill_screen 内部调用 fill_rect,
             * drv_gfx_frame_begin/end 在非帧内不会自动 flush.
             * 这里显式 flush, 确保清屏结果立即出现在屏幕上,
             * 也保证 back buffer 已经被 bg 完全写过, 不是未初始化内存。*/
            if (!g_compositor_active) {
                /* 强制立即 flush: 先标脏整个屏幕, 再调 maybe_flush / 驱动 flush (无视 depth).
                 * [注] drv_gfx_ops 表只有 flush = gfx_flush_now, 没有独立 flush_now
                 * 字段 (flush_now 是 kernel_api 给 EFS 程序用的 efs_flush_now)。
                 * drv_gfx_force_flush() 内部直接调 g_drv_gfx_ptr->flush(), 无视
                 * g_gfx_frame_depth, 语义等价于 "立即刷"。 */
                if (g_drv_gfx_ptr && g_drv_gfx_ptr->mark_dirty_rect) {
                    g_drv_gfx_ptr->mark_dirty_rect(0, 0, (int)fb->hr - 1, (int)fb->vr - 1);
                }
                drv_gfx_maybe_flush();
                drv_gfx_force_flush();
            }
            cursor_x = 0;
            cursor_y = 0;
        }
        /* [关键修复] UTF-8 状态机清零: efmlogin/userman 输出中文会在 welcome
         * banner / 菜单字符串 (如 "输入 'help' 查看可用命令列表。\n\n") 里触发
         * utf8_pos = 1 (首字节 0xE0-0xEF) 状态, 然后 Shell 首个用户 ASCII 输入
         * 字节不符合 "续字节 0x80-0xBF" 被丢, 但更关键的是 — 如果进入 shell 后
         * welcome 先输出, 在 "查看可用命令列表。" 的最后一个中文字节之前如果
         * 提前被某个调用处 (如 show_cursor 或 yield) 打断, utf8_pos 会留在 1 或
         * 2, 然后用户输入 'l'/'s' 会被当成续字节。清零是最直接的保证。 */
        utf8_pos = 0;
        utf8_buf[0] = utf8_buf[1] = utf8_buf[2] = 0;
    }
    cursor_visible = 1;
    show_cursor();
    /* [flush] show_cursor 写了 front buffer 的光标像素, 但 welcome 字符串写的是
     * back buffer (通过 TTF draw_char_unicode), 必须显式 flush back buffer 到
     * front, 不然 welcome 文字停留在 back buffer, 用户看到"光标在空屏幕闪" —
     * 然后第一次按键重绘才把 back buffer 刷出来, 同时看到
     * "DDDDDD 异常输出" (之前 back buffer 里残留的 D 字形残像素突然被刷到 front)。
     * 这里在所有 print_string 之前先强制刷一次空的 back buffer, 保持和 front
     * 一致 (都是深靛色清屏)。 */
    if (!g_wm_enabled && !g_compositor_active) drv_gfx_maybe_flush();
    print_string(TR("EFMOS Shell v1.0 (Text Mode)\n",
                    "EFMOS 终端 Shell v1.0 (纯文字模式)\n"));
    print_string(TR("--------------------------------\n",
                    "--------------------------------\n"));
    print_string(TR("Type 'help' for the command list.\n\n",
                    "输入 'help' 查看可用命令列表。\n\n"));

    /* [纯 Shell] 跳过 efmcompositor + efmAether + Mesa 预加载等桌面相关进程启动。
     * 这些只在桌面模式下使用, 既然取消了桌面, 就不需要启动 (节省内存 + 避免 CPU 占用)。 */
    serial_write("boot: entering pure-shell mode (desktop disabled)\n");

    /* [再次 drain 键盘] 在 efmlogin 退出 → 4000000 pause 延迟 → welcome banner 打印
     * 期间, 用户的按键动作 / PS/2 控制器残余字节 (包括 typematic 连续 MAKE 码)
     * 会攒到 rawq/kbdq 里, 启动 drain 虽然会清一次队列, 但 classify_rawq
     * 在清完队列后又会把 8042 OBF 里的字节 push 到 rawq, 下一次 kb_has_data()
     * 又读出来。这里再 drain 一次 + classify 一次 + 再清一次, 确保队列真的空。 */
    {
        for (int i = 0; i < 32; i++) {
            unsigned char st = inb(0x64);
            if (!(st & 0x01)) break;
            if (st & 0xC0) { (void)inb(0x60); continue; }
            (void)inb(0x60);
        }
        classify_rawq();
        rawq_head = rawq_tail = 0;
        kbdq_head = kbdq_tail = 0;
        /* mouseq 也清一下 (efmlogin 用了鼠标) */
        mouseq_head = mouseq_tail = 0;
        s_midx = 0;
        s_mwatchdog = 0;
    }

    /* [shell 独立化] 内核不再运行内建 shell, 改为异步启动 efmshell.efs (独立 shell 进程).
     * 内核侧只保留 idle 循环: 鼠标箭头绘制 + 抢占调度检查 (efmshell 的 readline 通过
     * API 在 EFS 任务上下文运行, 内核 efs_readline 内部已处理键盘队列/光标/flush). */
    serial_write("boot: launching efmshell (kernel shell detached)\n");
    {
        int sh_pid = efs_spawn_async("efmshell");
        if (sh_pid <= 0) {
            serial_write("boot: efmshell spawn FAILED, no shell available\n");
            print_string("FATAL: efmshell.efs missing from /EFMOS - system halted\n");
            for (;;) { __asm__("hlt"); }
        }
    }
    /* 内核 idle: efmshell 在独立任务中运行, 这里只维护鼠标箭头 + 让出 CPU.
     * (纯 shell 模式无合成器, 鼠标由内核绘制; efs_readline 空闲时也会画,
     *  这里在命令执行等非 readline 阶段保持箭头更新) */
    {
        unsigned int idle_cnt = 0;
        for (;;) {
            idle_cnt++;
            lapic_timer_check();
            if (!g_wm_enabled && !g_compositor_active) {
                if (idle_cnt > 2000u) { idle_cnt = 0; if (s_mouse_visible) draw_mouse_cursor(); }
            }
            sched_yield();
            asm volatile("pause");
        }
    }
}
