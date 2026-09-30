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

/*
 * drv_common.h - EFMOS 驱动子系统公共头 (内核 / efmloader.efs / *.drv 都 include 此文件)
 *
 * Include guard: 防止重复引入
 */
#ifndef DRV_COMMON_H
#define DRV_COMMON_H

/*
 * .drv 文件格式:
 *   .drv 是 flat-bin, 编译方式与 .efs 相同 (gcc + objcopy -j .text/.rodata/.data)。
 *   文件布局:
 *     头部 12 字节 (与 .efs 完全一致):
 *       offset  0:  u32 load_addr  (小端, 加载内存地址)
 *       offset  4:  u32 保留 (0)
 *       offset  8:  u32 bin_size   (大端, 紧跟头的程序二进制大小)
 *     随后是二进制代码, 入口 = load_addr 首字节 (同 .efs)。
 *   驱动入口签名: void drv_entry(struct drv_kernel_if *iface, struct gop_fb *fb)
 *     - iface: 内核传给驱动的回调注册接口表 (魔法 0x44525630 = "DRV0")
 *     - fb:    0x1000 的 GOP 帧缓冲信息 (直接给显卡驱动用)
 *
 * 驱动类型 & ops 表 (由驱动通过 iface->register_driver() 注册):
 *   DRV_TYPE_DISK (1): struct drv_disk_ops   (read_blocks / write_blocks / flush / size)
 *   DRV_TYPE_GFX  (2): struct drv_gfx_ops    (put_pixel / fill_rect / mode_info)
 *   DRV_TYPE_NET  (3): (预留)
 *   DRV_TYPE_INPUT(4): (预留)
 *
 * 驱动加载流程:
 *   1. 内核启动到 kmain, ext4_init() 之后, 用户登录循环之前
 *      -> 运行 run_efs("efmloader")
 *   2. efmloader.efs 扫描 /EFMOS/DRIVERS/*.drv, 按文件名顺序加载
 *      -> 调用内核新 API load_driver(path) 把驱动映射到内存并执行其入口
 *   3. 驱动 drv_entry() 通过 iface->register_driver(...) 把 ops 表挂到内核
 *   4. 驱动可用: disk_read/write 变成"若有注册驱动就用驱动, 否则回退原 AHCI/IDE 内核路径"
 *      (内核内置 AHCI/IDE 保留为 fallback, 保证即使驱动分离也能正常启动.)
 *
 * 内核侧额外 API (加入 0x9000 的 struct kernel_api, 供 efmloader 使用):
 *   int  (*load_driver)(const char *path);         读 .drv 文件, 执行其入口, 注册成功返回非负
 *   int  (*driver_count)(void);                    当前已注册驱动数 (调试用)
 *   int  (*driver_list)(char out_names[][32], int max);  写驱动名, 返回条目数
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- GOP FB 结构 (与 kernel 0x1000 的 struct gop_fb 完全一致, 驱动直接用) ----------
 * 内核 struct gop_fb (kernel.c):
 *   struct gop_fb { unsigned long long fb_base; unsigned int hr, vr, ppsl; } __attribute__((packed));
 * 驱动必须用完全相同的布局, 否则读到的字段全错. */
#ifndef DRV_GOP_FB_DEFINED
#define DRV_GOP_FB_DEFINED
struct drv_gop_fb {
    unsigned long long fb_base;   /* offset 0: 帧缓冲物理地址 */
    unsigned int hr;              /* offset 8: 水平分辨率 */
    unsigned int vr;              /* offset 12: 垂直分辨率 */
    unsigned int ppsl;            /* offset 16: 每扫描行像素数 (pitch/4) */
} __attribute__((packed));
#endif

/* ---------- 驱动类型 ---------- */
#define DRV_TYPE_DISK  1
#define DRV_TYPE_GFX   2
#define DRV_TYPE_NET   3
#define DRV_TYPE_INPUT 4

#define DRV_NAME_MAX   32

/* ---------- 磁盘驱动 ops (注册后内核 disk_read/disk_write 会调用) ---------- */
struct drv_disk_ops {
    int  (*read_blocks)(unsigned int lba, unsigned int count, void *buf);
    int  (*write_blocks)(unsigned int lba, unsigned int count, const void *buf);
    int  (*flush)(void);
    unsigned long long (*size_sectors)(void);
    /* 设置 sector size (512/4096), 0=默认 512 */
    unsigned int sector_size;
};

/* ---------- 显卡驱动 ops (注册后 put_char/draw 等底层像素操作可替换) ---------- */
struct drv_gfx_ops {
    void (*put_pixel)(int x, int y, unsigned int color);
    void (*fill_rect)(int x1, int y1, int x2, int y2, unsigned int color);
    void (*draw_char_8x16)(int x, int y, char c, unsigned int fg, unsigned int bg);
    void (*scroll_up)(int px_lines);
    /* 返回当前分辨率 (驱动切换模式时可改) */
    void (*get_mode)(unsigned int *out_hr, unsigned int *out_vr,
                     unsigned int *out_ppsl, unsigned int ***out_fb_base);
    /* [2026+] 双缓冲/延迟刷新: 把后缓冲中积累的脏矩形一次性刷到前端.
     * 若驱动不支持双缓冲 (如老 vga.drv), 此字段为 NULL, 调用者应当跳过. */
    void (*flush)(void);
    /* [2026+] 局部 back→front 拷贝: 把 back buffer (x,y,w,h) 矩形复制到 front.
     * 用途: 鼠标在 flush 之后单独画到 front 时, 先用此函数擦除 front 上旧位置的鼠标箭头
     * (把 "下层内容" 从 back 拷回来覆盖旧像素), 再画新箭头到 front → 零拖尾.
     * 无 back buffer 时为 NULL. */
    void (*copy_back_rect_to_front)(int x, int y, int w, int h);
    /* [2026+] 批量像素 blit: 把外部像素缓冲区 src 拷贝到 back buffer 的 (dst_x,dst_y) 位置.
     * 用途: Mesa swrast 渲染完一帧后, 通过此接口把结果写入后缓冲 (而非直接写 front),
     * 保持与双缓冲/脏矩形机制一致. src_pitch 为源每行字节数.
     * 无 back buffer 时为 NULL (调用者应回退到 put_pixel 逐像素). */
    void (*blit_buffer)(int dst_x, int dst_y, int w, int h, const void *src, int src_pitch);
    /* [2026+] 手动标记后缓冲的 (x1,y1)-(x2,y2) 矩形为脏区 (inclusive).
     * 用途: 当外部写入修改了 back buffer (blit_buffer/memcpy 等) 但没经过标准 put_pixel/fill_rect 接口时,
     * 需要调用此函数把被改区域加进脏矩形, 确保下一次 flush 拷贝到 front.
     * Mesa blit 后用于强制把鼠标前后位置 16x16 区域加进脏区. */
    void (*mark_dirty_rect)(int x1, int y1, int x2, int y2);
    /* [优化] 获取 back buffer 指针和 pitch, 让 Mesa swrast 直接渲染到 back buffer,
     * 消除 BO→back buffer 的全屏拷贝. 返回 0=成功, -1=不支持. */
    int  (*get_backbuffer)(void **out_ptr, int *out_pitch, int *out_w, int *out_h);
    /* [TTF] Unicode 字符渲染: 使用 TTF 字体栅格化任意 Unicode 码点.
     * 返回字符的实际像素宽度 (0=不支持或失败, 调用者应回退到 draw_char_8x16).
     * codepoint: Unicode 码点 (如 'A'=0x41, '中'=0x4E2D)
     * cell_w: 字符单元宽度 (ASCII=FONT_W, CJK=2*FONT_W), 用于背景填充
     * cell_h: 字符单元高度 (FONT_H), 用于背景填充 */
    int  (*draw_char_unicode)(int x, int y, unsigned int codepoint,
                              unsigned int fg, unsigned int bg, int cell_w, int cell_h);
};

/* ---------- 内核提供给驱动的注册接口表 (drv_entry 的第一个参数) ---------- */
#define DRV_IFACE_MAGIC  0x44525630UL   /* "DRV0" */
struct drv_kernel_if {
    unsigned long magic;   /* == DRV_IFACE_MAGIC */

    /* 驱动名写入 driver registry, 返回 0=ok, -1=已满, -2=重名 */
    int  (*register_driver)(const char *name, int type, const void *ops, unsigned int ops_sz);

    /* 驱动可调用: 串口调试输出 */
    void (*log)(const char *s);

    /* 驱动可调用: 分配/释放内核内存 (同 EFS malloc/free, 用 kheap) */
    void *(*kmalloc)(unsigned long bytes);
    void  (*kfree)(void *p);

    /* 驱动可调用: 端口 I/O (IN/OUT 系列) */
    unsigned char  (*in8)(unsigned short port);
    unsigned short (*in16)(unsigned short port);
    unsigned int   (*in32)(unsigned short port);
    void           (*out8)(unsigned short port, unsigned char v);
    void           (*out16)(unsigned short port, unsigned short v);
    void           (*out32)(unsigned short port, unsigned int v);

    /* [TTF] 文件读取: 从 ext4 文件系统读取文件到 buf, 最多 max 字节.
     * 返回实际读取的字节数, -1=失败. 路径格式: "/EFMOS/fonts/sarasa.ttf" */
    int  (*file_read)(const char *path, char *buf, int max);
};

/* ---------- .drv 入口 (必须为 .text 第一个函数) ---------- */
typedef void (*drv_entry_fn)(struct drv_kernel_if *iface, struct drv_gop_fb *fb);

#ifdef __cplusplus
}
#endif

#endif /* DRV_COMMON_H */
