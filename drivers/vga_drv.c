/* vga_drv.c - EFMOS 通用 VESA GOP 帧缓冲显卡驱动 (编译为 vga.drv)
 * 加载地址: 0x2000000 (32MB, 不与任何内核/EFS 区域冲突)
 * 入口: drv_entry
 *
 * 功能:
 *   - 使用内核通过 drv_entry 第二个参数传入的 drv_gop_fb (来自 0x1000 的 UEFI GOP info)
 *   - 实现通用的 put_pixel / fill_rect / draw_hline / draw_vline / mode_info 操作
 *   - 这些是最基本的 GPU 操作, 相当于 "软件渲染驱动";
 *     未来如果有 QXL/Virtio-GPU/VBoxVGA/Intel GVT-d 等硬件加速,
 *     可以再写独立驱动 (qxl.drv / virtio-gpu.drv) 覆盖 vga 注册。
 *
 * 驱动的生命周期:
 *   drv_entry(iface, fb)
 *     ├─ 缓存 fb 指针 + 像素格式 (fb_base, hr, vr, ppsl)
 *     ├─ 构造 drv_gfx_ops 静态表
 *     └─ 调 iface->register_driver("vga", DRV_TYPE_GFX, &ops, sizeof ops)
 *
 * [注意] 本驱动的 put_pixel/fill_rect 逻辑直接写入 UEFI 设置的线性帧缓冲,
 * 与 bootloader 设置的 pixel format 强相关 (默认 QEMU std VGA + OVMF = BGRx8888, 32bpp)。
 * color 参数格式使用 0x00RRGGBB, 写 FB 时按 BGR byte 转换。
 */

#include "drv_common.h"

/* ========== 全局静态状态 (驱动加载在固定地址, 全局变量安全) ========== */
static struct drv_gop_fb g_fb;        /* 帧缓冲信息拷贝 (驱动加载时填入) */
static int g_initialized = 0;         /* 是否 init 完成 (0/1) */

/* 像素格式: 按 UEFI GOP PixelBlueGreenRedReserved8BitPerColor,
 * 每个像素 4 字节, 字节序为 [B, G, R, Rsvd] (小端 uint32 视角为 0x00RRGGBB).
 * 当 pixel_format 不匹配时, 驱动通过 "blind copy 0x00RRGGBB to u32" 兼容最常见格式。*/
static inline void vga_write_pixel(unsigned int x, unsigned int y, unsigned int color_888) {
    if (!g_initialized || !g_fb.fb_base) return;
    if (x >= g_fb.hr || y >= g_fb.vr) return;
    /* pitch in bytes */
    unsigned int pitch = g_fb.ppsl ? g_fb.ppsl : (g_fb.hr * 4);
    unsigned char *fb_bytes = (unsigned char*)g_fb.fb_base;
    unsigned long offset = (unsigned long)y * (unsigned long)pitch + (unsigned long)x * 4UL;
    unsigned char *p = fb_bytes + offset;
    /* color_888 = 0x00RRGGBB  (bits 23..16 = R, 15..8 = G, 7..0 = B) */
    unsigned char r = (unsigned char)((color_888 >> 16) & 0xFF);
    unsigned char g = (unsigned char)((color_888 >>  8) & 0xFF);
    unsigned char b = (unsigned char)((color_888      ) & 0xFF);
    /* BGRx 顺序 (最常见 UEFI GOP 格式) */
    p[0] = b; p[1] = g; p[2] = r; p[3] = 0xFF;
}

/* ========== ops: put_pixel ========== */
static void vga_put_pixel(int x, int y, unsigned int c) {
    if (x < 0 || y < 0) return;
    vga_write_pixel((unsigned int)x, (unsigned int)y, c);
}

/* ========== ops: fill_rect (逐行填像素, 或整 32 位写提升性能)
 * 用 uint32 写时, 需把 color_888 编成 BGRx 小端 u32 = 0xFFRRGGBB (同 pixel_format) */
static void vga_fill_rect(int x1, int y1, int x2, int y2, unsigned int c) {
    if (!g_initialized || !g_fb.fb_base) return;
    /* normalize */
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if ((unsigned int)x2 >= g_fb.hr) x2 = (int)g_fb.hr - 1;
    if ((unsigned int)y2 >= g_fb.vr) y2 = (int)g_fb.vr - 1;
    if (x1 > x2 || y1 > y2) return;

    unsigned int r = (unsigned char)((c >> 16) & 0xFF);
    unsigned int g = (unsigned char)((c >>  8) & 0xFF);
    unsigned int b = (unsigned char)((c      ) & 0xFF);
    unsigned int u32_bgrx = (0xFFu << 24) | (r << 16) | (g << 8) | b;

    unsigned int pitch = g_fb.ppsl ? g_fb.ppsl : (g_fb.hr * 4);
    unsigned char *fb_bytes = (unsigned char*)g_fb.fb_base;
    int w = x2 - x1 + 1;
    for (int y = y1; y <= y2; y++) {
        unsigned long row_off = (unsigned long)y * (unsigned long)pitch + (unsigned long)x1 * 4UL;
        unsigned int *row = (unsigned int*)(fb_bytes + row_off);
        /* unrolled: 逐像素写 32bit */
        for (int x = 0; x < w; x++) row[x] = u32_bgrx;
    }
}

/* ========== ops: draw_char_8x16 (stub, 内核用自己 draw.c 实现即可; 这里填 NULL 也能用) */
static void vga_draw_char_8x16(int x, int y, char c, unsigned int fg, unsigned int bg) {
    /* 驱动层不提供 8x16 字体绘制 (字体数据放内核更合适), 留空即可。
     * 内核 draw.c 有内置 fallback 实现, 会通过 put_pixel 间接调用我们。 */
    (void)x; (void)y; (void)c; (void)fg; (void)bg;
}

/* ========== ops: scroll_up (向上滚动 N 像素行) ========== */
static void vga_scroll_up(int px_lines) {
    if (!g_initialized || !g_fb.fb_base) return;
    if (px_lines <= 0) return;
    unsigned int pitch = g_fb.ppsl ? g_fb.ppsl : (g_fb.hr * 4);
    if ((unsigned int)px_lines >= g_fb.vr) { px_lines = (int)g_fb.vr; }
    unsigned char *fb_bytes = (unsigned char*)g_fb.fb_base;
    unsigned long move_bytes = (unsigned long)(g_fb.vr - (unsigned int)px_lines) * (unsigned long)pitch;
    unsigned long clear_start = (unsigned long)(g_fb.vr - (unsigned int)px_lines) * (unsigned long)pitch;
    unsigned long clear_bytes = (unsigned long)px_lines * (unsigned long)pitch;
    /* 向上搬移: 用指针逐字节 (无 string.h) */
    unsigned char *dst = fb_bytes;
    unsigned char *src = fb_bytes + (unsigned long)px_lines * (unsigned long)pitch;
    for (unsigned long i = 0; i < move_bytes; i++) dst[i] = src[i];
    /* 底部清空为背景色 (深靛色 0x1A1A2E, 与内核 Graphics.drv 一致) */
    {
        unsigned int bg_r = 0x1A, bg_g = 0x1A, bg_b = 0x2E;
        unsigned int bg_u32 = (0xFFu << 24) | (bg_r << 16) | (bg_g << 8) | bg_b;
        unsigned int *dst32 = (unsigned int*)(fb_bytes + clear_start);
        unsigned long n32 = clear_bytes >> 2;
        for (unsigned long i = 0; i < n32; i++) dst32[i] = bg_u32;
    }
}

/* ========== ops: get_mode ========== */
static void vga_get_mode(unsigned int *out_hr, unsigned int *out_vr,
                         unsigned int *out_ppsl, unsigned int ***out_fb_base) {
    if (out_hr)      *out_hr      = g_fb.hr;
    if (out_vr)      *out_vr      = g_fb.vr;
    if (out_ppsl)    *out_ppsl    = g_fb.ppsl ? g_fb.ppsl : (g_fb.hr * 4);
    if (out_fb_base) *out_fb_base = (unsigned int **)&g_fb.fb_base;
}

/* ========== ops 表 (严格按 drv_common.h drv_gfx_ops 顺序) ========== */
static struct drv_gfx_ops g_vga_ops = {
    vga_put_pixel,      /* put_pixel */
    vga_fill_rect,      /* fill_rect */
    vga_draw_char_8x16, /* draw_char_8x16 (stub) */
    vga_scroll_up,      /* scroll_up */
    vga_get_mode,       /* get_mode */
};

/* ========== 驱动入口 (必须为 .text 第一个函数!) ==========
 * efmloader 通过 api->load_driver("/EFMOS/DRIVERS/vga.drv") 加载本 .drv 后,
 * 内核按头部 load_addr + 12 字节把本文件拷贝, 然后跳转到 load_addr,
 * 即 _start / drv_entry (用 naked 属性强制排在 .text 开头). */
void drv_main(struct drv_kernel_if *iface, struct drv_gop_fb *fb);

__attribute__((naked, section(".text.start")))
void drv_entry(struct drv_kernel_if *iface, struct drv_gop_fb *fb) {
    __asm__ volatile(
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"
        "call drv_main\n\t"
        "leave\n\t"
        "ret\n\t"
    );
}

void drv_main(struct drv_kernel_if *iface, struct drv_gop_fb *fb) {
    if (!iface || iface->magic != DRV_IFACE_MAGIC) return;
    if (!fb || !fb->fb_base) { iface->log("[vga] no gop fb info, abort.\n"); return; }

    /* 缓存 fb 信息到本驱动 BSS */
    g_fb = *fb;
    g_initialized = 1;

    /* 打印分辨率信息到串口 (通过 iface->log) */
    char buf[80];
    int n = 0;
    const char *h = "[vga] GOP fb=";
    for (int i = 0; h[i]; i++) buf[n++] = h[i];
    /* 手写 hex of fb_base (8 位十六进制) */
    unsigned long a = (unsigned long)g_fb.fb_base;
    const char *hex = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) buf[n++] = hex[(a >> i) & 0xF];
    buf[n++] = ' '; buf[n++] = 'x'; buf[n++] = ' ';
    /* hr */
    unsigned int v = g_fb.hr;
    if (v == 0) buf[n++] = '0';
    else {
        char tmp[10]; int p = 0;
        while (v) { tmp[p++] = '0' + (v % 10); v /= 10; }
        while (p--) buf[n++] = tmp[p];
    }
    buf[n++] = 'x';
    v = g_fb.vr;
    if (v == 0) buf[n++] = '0';
    else {
        char tmp[10]; int p = 0;
        while (v) { tmp[p++] = '0' + (v % 10); v /= 10; }
        while (p--) buf[n++] = tmp[p];
    }
    buf[n++] = '\n'; buf[n] = 0;
    iface->log(buf);

    /* 注册显卡驱动 (成功后, 内核的图形拦截层会优先调用我们) */
    int rc = iface->register_driver("vga", DRV_TYPE_GFX, &g_vga_ops, sizeof(g_vga_ops));
    if (rc == 0) {
        iface->log("[vga] registered graphics ops OK.\n");
    } else {
        iface->log("[vga] register_driver failed.\n");
    }
}
