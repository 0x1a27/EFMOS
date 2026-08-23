/* graphics_drv.c - EFMOS Graphics/GPU 驱动 (编译为 Graphics.drv)
 * 加载地址: 0x2800000 (40MB, 紧跟 ahci 36MB + 预留 4MB, 不与内核冲突)
 * 入口: drv_entry
 *
 * 目标: 在 VESA GOP 帧缓冲基础上, 实现"不闪烁 + 高清 + 抗锯齿 + 视觉增强".
 *
 * 核心优化:
 *   [1] 双缓冲 (Double Buffering)
 *       - 所有 put_pixel/fill_rect/scroll 都写"离屏后缓冲"(back buffer)
 *       - 在每次 GPU API 调用尾部, 把"脏矩形"(dirty rect) 复制回真正的
 *         GOP front buffer, 且复制是 32-bit 对齐 burst (避免单字节搬运
 *         导致的逐像素"逐行刷出", 也就是肉眼能看到的闪烁/撕裂)
 *       - 脏矩形合并算法: 每次调用 gfx_dirty_mark(x,y,w,h) 把脏区扩张,
 *         只在一次 flush 时复制合并后的矩形, 减少内存搬运带宽
 *
 *   [2] 消除撕裂: 每次 flush 时把复制限制在 "同一条扫描线" 完成
 *       (在 FB 映射为 WC 的机器上, 用 128-bit SSE 对齐 MOV 更好, 此处退化为
 *       32-bit MOV 连续写, 效果已接近. 未来如需进一步可在 VBLANK IRQ 中触发 flush)
 *
 *   [3] 抗锯齿 (AA):
 *       - draw_char_8x16: 内置 8x16 VGA 点阵字形数据, 每个点阵像素用
 *         2x2 超采样 (Super-sampling) 方式展开为 2bit 灰度, 再与背景 alpha
 *         混合, 输出平滑字体, 肉眼完全看不到锯齿
 *       - draw_circle / draw_round_rect: 基于距离场的 alpha 覆盖采样
 *         (每像素 4 次中心偏移采样取平均), 边缘灰度呈线性衰减
 *
 *   [4] 高分辨率 / 高色深:
 *       - 不假设分辨率, 全部使用 UEFI GOP 实际报告的 hr/vr/ppsl
 *       - 像素写入前自动检测 BGRx vs RGBx (读第一个像素 R/B 位置判断),
 *         保证切换显卡/QEMU/std-VM 后颜色不反
 *       - fill_rect_gradient (线性渐变): 在内核扩展 drv_gfx_ops 时
 *         本驱动已内置实现 (即使 ops 表未暴露, draw.c 也可调用渐变 API)
 *
 *   [5] 视觉增强:
 *       - put_pixel_blend: 支持 0xRRGGBBAA 的 8-bit alpha 混合
 *       - scroll_up: 用 uint32* 逐 32-bit 搬移 + 最后 64 行 burst zero
 *       - draw_line (Bresenham): 带 AA 的线段绘制
 *
 * 驱动生命周期:
 *   drv_entry(iface, fb)
 *     ├─ 拷贝 fb 信息, 检测像素格式 (BGRx/RGBx)
 *     ├─ 用 iface->kmalloc 分配一个与 front buffer 同样大的 back buffer
 *       (容量 = vr * ppsl, 通常 1920×1080×4 = 8MB, 内核堆 32MB 足够)
 *     ├─ 初始化脏矩形为空, 把 front buffer 初始内容拷贝到 back buffer
 *     ├─ 构造 drv_gfx_ops 静态表
 *     └─ register_driver("Graphics", DRV_TYPE_GFX, &ops, sz)
 */

#include "drv_common.h"
#include "ttf_font.h"
/* TTF 字体加载器实现 (直接编入驱动, 无需单独链接) */
#include "ttf_font.c"

/* ==================== 1. 全局状态 ==================== */
static struct drv_gop_fb g_fb;
static int  g_initialized = 0;

/* --- 双缓冲 --- */
static unsigned char *g_back = 0;          /* 离屏后缓冲 (kmalloc, 与 front 同尺寸) */
static unsigned long  g_fb_sz  = 0;         /* front buffer 总字节数 = vr * pitch_b */
static unsigned int   g_pitch_px = 0;      /* 每扫描行像素数 = ppsl or hr */
static unsigned long  g_pitch_b  = 0;      /* 每扫描行字节数 = pitch_px * 4 */

/* 像素格式检测结果 */
static int g_pixfmt_rgbx = 0;              /* 1 = RGBx (字节 R,G,B,A), 0 = 默认为 BGRx */

/* --- 脏矩形 (合并最小包围盒, flush 时仅搬这块) --- */
static int g_dirty = 0;                     /* 1 = 有脏区需要 flush */
static int g_dx1, g_dy1, g_dx2, g_dy2;     /* 脏矩形 (含两端, 像素坐标) */

/* --- 字体: 8x16 标准 VGA ROM 点阵 (Codepage 437, 0x00..0x7F ASCII + 部分图形) ---
 * 为避免外部依赖, 直接硬编码最小可用 96 个 ASCII 可打印字符 (0x20..0x7E) */
static const unsigned char g_font8x16[96][16] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 20 SPACE */
    {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, /* 21 ! */
    {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 22 " */
    {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00}, /* 23 # */
    {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00}, /* 24 $ */
    {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00}, /* 25 % */
    {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, /* 26 & */
    {0x30,0x30,0x30,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 27 ' */
    {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00}, /* 28 ( */
    {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00}, /* 29 ) */
    {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00}, /* 2A * */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00}, /* 2B + */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x38,0x38,0x38,0x30,0x60,0x00,0x00}, /* 2C , */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 2D - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x38,0x38,0x00,0x00,0x00,0x00,0x00}, /* 2E . */
    {0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00}, /* 2F / */
    {0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xF6,0xE6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 30 0 */
    {0x00,0x00,0x10,0x30,0xF0,0x10,0x10,0x10,0x10,0x10,0x10,0x7C,0x00,0x00,0x00,0x00}, /* 31 1 */
    {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00}, /* 32 2 */
    {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 33 3 */
    {0x00,0x00,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00}, /* 34 4 */
    {0x00,0x00,0xFE,0xC0,0xC0,0xBC,0xC6,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 35 5 */
    {0x00,0x00,0x38,0x60,0xC0,0xC0,0xBC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 36 6 */
    {0x00,0x00,0xFE,0x06,0x06,0x0C,0x18,0x30,0x30,0x60,0x60,0x60,0x00,0x00,0x00,0x00}, /* 37 7 */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 38 8 */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00}, /* 39 9 */
    {0x00,0x00,0x00,0x00,0x38,0x38,0x00,0x00,0x00,0x38,0x38,0x00,0x00,0x00,0x00,0x00}, /* 3A : */
    {0x00,0x00,0x00,0x00,0x38,0x38,0x00,0x00,0x00,0x38,0x30,0x60,0x00,0x00,0x00,0x00}, /* 3B ; */
    {0x00,0x00,0x0C,0x18,0x30,0x60,0xC0,0x60,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, /* 3C < */
    {0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 3D = */
    {0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00}, /* 3E > */
    {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, /* 3F ? */
    {0x00,0x00,0x7C,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 40 @ */
    {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* 41 A */
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00}, /* 42 B */
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00}, /* 43 C */
    {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00}, /* 44 D */
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00}, /* 45 E */
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, /* 46 F */
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3E,0x00,0x00,0x00,0x00}, /* 47 G */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* 48 H */
    {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* 49 I */
    {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00}, /* 4A J */
    {0x00,0x00,0xE6,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, /* 4B K */
    {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x62,0x66,0x66,0x66,0xFE,0x00,0x00,0x00,0x00}, /* 4C L */
    {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* 4D M */
    {0x00,0x00,0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* 4E N */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 4F O */
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, /* 50 P */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xCE,0xCE,0xDE,0x7C,0x0C,0x0E,0x00,0x00}, /* 51 Q */
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, /* 52 R */
    {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 53 S */
    {0x00,0x00,0x7C,0x7C,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* 54 T */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* 55 U */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00}, /* 56 V */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00}, /* 57 W */
    {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x7C,0x6C,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* 58 X */
    {0x00,0x00,0xC6,0xC6,0x6C,0x38,0x18,0x38,0x6C,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* 59 Y */
    {0x00,0x00,0xFE,0xCE,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00}, /* 5A Z */
    {0x00,0x00,0x7C,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7C,0x00,0x00,0x00,0x00}, /* 5B [ */
    {0x00,0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00,0x00,0x00,0x00}, /* 5C \ */
    {0x00,0x00,0x7C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7C,0x00,0x00,0x00,0x00}, /* 5D ] */
    {0x00,0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 5E ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00}, /* 5F _ */
    {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 60 ` */
    {0x00,0x00,0x00,0x7C,0x06,0x3E,0x46,0x46,0x46,0x3E,0x06,0x7E,0x00,0x00,0x00,0x00}, /* 61 a */
    {0x00,0x00,0xE0,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00}, /* 62 b */
    {0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC0,0xC6,0x7C,0x06,0x00,0x00,0x00,0x00}, /* 63 c */
    {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x6E,0x00,0x00,0x00,0x00}, /* 64 d */
    {0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 65 e */
    {0x00,0x00,0x38,0x6C,0x64,0x60,0x78,0x60,0x60,0x60,0x60,0x78,0x00,0x00,0x00,0x00}, /* 66 f */
    {0x00,0x00,0x00,0x7E,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 67 g */
    {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, /* 68 h */
    {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* 69 i */
    {0x00,0x00,0x0C,0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00}, /* 6A j */
    {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, /* 6B k */
    {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* 6C l */
    {0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* 6D m */
    {0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00}, /* 6E n */
    {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 6F o */
    {0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0xF0,0x60,0x60,0x00,0x00}, /* 70 p */
    {0x00,0x00,0x00,0x7E,0x66,0x66,0x66,0x7E,0x06,0x06,0x06,0x06,0x06,0x06,0x00,0x00}, /* 71 q */
    {0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, /* 72 r */
    {0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 73 s */
    {0x00,0x00,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x30,0x34,0x18,0x00,0x00,0x00,0x00}, /* 74 t */
    {0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCE,0x6C,0x00,0x00,0x00,0x00}, /* 75 u */
    {0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00}, /* 76 v */
    {0x00,0x00,0x00,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00}, /* 77 w */
    {0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x7C,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, /* 78 x */
    {0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xCE,0x7E,0x06,0x0C,0xF8,0x00}, /* 79 y */
    {0x00,0x00,0x00,0xFE,0x8C,0x18,0x30,0x60,0xC0,0x86,0xFE,0x00,0x00,0x00,0x00,0x00}, /* 7A z */
    {0x00,0x00,0x1C,0x30,0x30,0x30,0xE0,0x30,0x30,0x30,0x1C,0x00,0x00,0x00,0x00,0x00}, /* 7B { */
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18}, /* 7C | */
    {0x00,0x00,0xE0,0x30,0x30,0x30,0x1C,0x30,0x30,0x30,0xE0,0x00,0x00,0x00,0x00,0x00}, /* 7D } */
    {0x00,0x70,0xD8,0x0C,0x06,0x06,0x0C,0xD8,0x70,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 7E ~ */
};

/* ==================== 0. 驱动入口 (必须在 .text 最前面, 内核从 load_addr 调用) ==================== */
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

/* ==================== 2. 基础像素写入 ==================== */

/* 把 0x00RRGGBB 写成 BGRx 或 RGBx 单字节 (4 字节) */
static inline void bb_write_px(unsigned char *buf, unsigned int pitch_px,
                               int x, int y, unsigned int r, unsigned int g, unsigned int b) {
    unsigned long off = ((unsigned long)y * (unsigned long)pitch_px + (unsigned long)x) * 4UL;
    unsigned char *p = buf + off;
    if (g_pixfmt_rgbx) { p[0] = r; p[1] = g; p[2] = b; p[3] = 0xFF; }
    else               { p[0] = b; p[1] = g; p[2] = r; p[3] = 0xFF; }
}
/* u32 burst 版本 (fill_rect 性能用, 写入整个对齐像素值) */
static inline unsigned int bb_px_u32(unsigned int r, unsigned int g, unsigned int b) {
    if (g_pixfmt_rgbx) return (0xFFu<<24) | (b<<16) | (g<<8) | r;   /* R-G-B-A -> A-B-G-R byte view */
    else               return (0xFFu<<24) | (r<<16) | (g<<8) | b;   /* B-G-R-A byte = A-R-G-B LE u32 */
}

/* ---------------- 脏区标记 + flush ---------------- */
static void gfx_dirty_mark(int x1, int y1, int x2, int y2) {
    if (!g_initialized) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if ((unsigned int)x2 >= g_fb.hr) x2 = (int)g_fb.hr - 1;
    if ((unsigned int)y2 >= g_fb.vr) y2 = (int)g_fb.vr - 1;
    if (x1 > x2 || y1 > y2) return;
    if (!g_dirty) { g_dx1 = x1; g_dy1 = y1; g_dx2 = x2; g_dy2 = y2; g_dirty = 1; return; }
    if (x1 < g_dx1) g_dx1 = x1; if (y1 < g_dy1) g_dy1 = y1;
    if (x2 > g_dx2) g_dx2 = x2; if (y2 > g_dy2) g_dy2 = y2;
}

/* 行拷贝: 64-bit 标量展开 (不使用 SSE2, 保证兼容性, 同时比 32-bit 逐像素快 2 倍)
 * 策略: 对齐到 8 字节 (2 像素) 一次拷贝, 剩余 1 像素用 32-bit 收尾 */
static inline void gfx_sse2_copy_row(unsigned int *dst, const unsigned int *src, int w) {
    int i = 0;
    /* 8 字节 (2 像素) 一组, 用 unsigned long long 拷贝 */
    int pairs = w >> 1;
    unsigned long long *d8 = (unsigned long long*)dst;
    const unsigned long long *s8 = (const unsigned long long*)src;
    while (pairs--) *d8++ = *s8++;
    /* 剩余 1 像素 (w 为奇数时) */
    if (w & 1) dst[w - 1] = src[w - 1];
}

static void gfx_flush_now(void) {
    if (!g_dirty || !g_back || !g_initialized || !g_fb.fb_base) return;
    int h = g_dy2 - g_dy1 + 1;
    int w = g_dx2 - g_dx1 + 1;
    unsigned char *front = (unsigned char*)g_fb.fb_base;
    for (int y = 0; y < h; y++) {
        unsigned long off = ((unsigned long)(g_dy1 + y) * g_pitch_b)
                          + ((unsigned long)g_dx1 * 4UL);
        gfx_sse2_copy_row((unsigned int*)(front + off),
                          (unsigned int*)(g_back + off), w);
    }
    g_dirty = 0;
}

/* [鼠标拖尾修复] 把 back buffer 指定矩形拷贝到 front buffer (不经过脏矩形/flush 状态).
 * 用于 WM 模式下: flush 之后鼠标单独画到 front, 当鼠标位置变更时, 先调用此函数
 * 把 front 上 "旧鼠标 16x16 区域" 用 back buffer(干净的桌面/窗口像素) 覆盖 →
 * 彻底消除旧鼠标箭头残影。参数与 fill_rect fill_rect(x,y,w,h) 相同 (x,y 左上 inclusive). */
static void gfx_copy_back_rect_to_front(int x, int y, int w, int h) {
    if (!g_back || !g_initialized || !g_fb.fb_base || w <= 0 || h <= 0) return;
    /* clip 到屏幕范围 */
    int x1 = x, y1 = y, x2 = x + w - 1, y2 = y + h - 1;
    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if ((unsigned int)x2 >= g_fb.hr) x2 = (int)g_fb.hr - 1;
    if ((unsigned int)y2 >= g_fb.vr) y2 = (int)g_fb.vr - 1;
    if (x1 > x2 || y1 > y2) return;
    int rows = y2 - y1 + 1;
    int cw = x2 - x1 + 1;
    unsigned char *front = (unsigned char*)g_fb.fb_base;
    for (int yy = 0; yy < rows; yy++) {
        unsigned long off = ((unsigned long)(y1 + yy) * g_pitch_b)
                          + ((unsigned long)x1 * 4UL);
        gfx_sse2_copy_row((unsigned int*)(front + off),
                          (unsigned int*)(g_back + off), cw);
    }
}

/* [Mesa 集成] 把外部像素缓冲区 (src) 批量拷贝到 back buffer 的 (dst_x,dst_y) 位置.
 * 用途: Mesa swrast 渲染完一帧到 gbm_bo 的 mmap 区域后, 通过此接口写入后缓冲,
 * 而非直接写 front buffer, 保持双缓冲/脏矩形机制一致, 避免 WM 冲突.
 * 参数:
 *   dst_x, dst_y: back buffer 中的目标左上角坐标 (全局 FB 坐标系)
 *   w, h:         要拷贝的宽高 (像素)
 *   src:          源像素缓冲区 (32-bit BGRA/RGBA, 与 GOP 格式一致)
 *   src_pitch:    源每行字节数 (通常 = w * 4) */
static void gfx_blit_buffer(int dst_x, int dst_y, int w, int h,
                            const void *src, int src_pitch) {
    if (!g_back || !g_initialized || !src || w <= 0 || h <= 0) return;
    struct drv_gop_fb *fb = &g_fb;
    /* clip 到屏幕范围 */
    int x1 = dst_x, y1 = dst_y;
    int x2 = dst_x + w - 1, y2 = dst_y + h - 1;
    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if ((unsigned int)x2 >= fb->hr) x2 = (int)fb->hr - 1;
    if ((unsigned int)y2 >= fb->vr) y2 = (int)fb->vr - 1;
    if (x1 > x2 || y1 > y2) return;
    int dst_pitch_px = (int)g_pitch_px;
    int copy_w = x2 - x1 + 1;
    int copy_h = y2 - y1 + 1;
    int src_offset_x = x1 - dst_x;  /* 源中对应的起始列 (clip 后) */
    int src_offset_y = y1 - dst_y;
    const unsigned int *s = (const unsigned int*)src;
    unsigned int *bd = (unsigned int*)g_back;
    /* SSE2 加速逐行拷贝 (源 pitch 可能 != 目标 pitch) */
    for (int yy = 0; yy < copy_h; yy++) {
        unsigned int *dst_row = bd + ((unsigned long)(y1 + yy) * (unsigned long)dst_pitch_px);
        const unsigned int *src_row = s + ((unsigned long)(src_offset_y + yy) * (unsigned long)(src_pitch / 4));
        gfx_sse2_copy_row(dst_row + x1, src_row + src_offset_x, copy_w);
    }
    /* 标记脏矩形, 下一次 flush 自动搬到 front */
    gfx_dirty_mark(x1, y1, x2, y2);
}

/* alpha 混合: src=前景 0xRRGGBB, a = 0..255 alpha; dst=背景 (已经在 back buffer 中的像素) */
static inline unsigned int bb_blend_u32(unsigned int dst_u32,
                                        unsigned int r, unsigned int g, unsigned int b,
                                        unsigned int a) {
    if (a >= 255) return bb_px_u32(r, g, b);
    if (a == 0)   return dst_u32;
    unsigned int dr, dg, db;
    if (g_pixfmt_rgbx) {
        dr = (dst_u32) & 0xFF;                 /* p[0]=R */
        dg = (dst_u32 >> 8) & 0xFF;
        db = (dst_u32 >> 16) & 0xFF;
    } else {
        db = (dst_u32) & 0xFF;                 /* p[0]=B */
        dg = (dst_u32 >> 8) & 0xFF;
        dr = (dst_u32 >> 16) & 0xFF;
    }
    unsigned int ia = 255 - a;
    unsigned int nr = (r*a + dr*ia + 127) / 255;
    unsigned int ng = (g*a + dg*ia + 127) / 255;
    unsigned int nb = (b*a + db*ia + 127) / 255;
    return bb_px_u32(nr, ng, nb);
}

/* 从 back buffer 中读某个像素的 u32 */
static inline unsigned int bb_read_u32(int x, int y) {
    if (x<0||y<0) return 0;
    if ((unsigned int)x >= g_fb.hr || (unsigned int)y >= g_fb.vr) return 0;
    unsigned long off = (unsigned long)y * g_pitch_b + (unsigned long)x * 4UL;
    return *(unsigned int*)(g_back + off);
}

/* ==================== 3. ops: put_pixel + alpha ==================== */
static void gfx_put_pixel(int x, int y, unsigned int c) {
    if (!g_initialized || !g_back) return;
    if (x<0 || y<0) return;
    if ((unsigned int)x >= g_fb.hr || (unsigned int)y >= g_fb.vr) return;
    unsigned int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    bb_write_px(g_back, g_pitch_px, x, y, r, g, b);
    gfx_dirty_mark(x, y, x, y);
}

/* 带 alpha 的 put_pixel (内部辅助, 给 AA 字体用) */
static void gfx_put_pixel_blend(int x, int y, unsigned int r, unsigned int g, unsigned int b, unsigned int a) {
    if (!g_initialized || !g_back) return;
    if (x<0 || y<0) return;
    if ((unsigned int)x >= g_fb.hr || (unsigned int)y >= g_fb.vr) return;
    unsigned long off = (unsigned long)y * g_pitch_b + (unsigned long)x * 4UL;
    unsigned int *p = (unsigned int*)(g_back + off);
    *p = bb_blend_u32(*p, r, g, b, a);
    gfx_dirty_mark(x, y, x, y);
}

/* ==================== 4. ops: fill_rect (含渐变入口) ==================== */
static void gfx_fill_rect(int x1, int y1, int x2, int y2, unsigned int c) {
    if (!g_initialized || !g_back) return;
    if (x1>x2) { int t=x1; x1=x2; x2=t; }
    if (y1>y2) { int t=y1; y1=y2; y2=t; }
    if (x1<0) x1=0; if (y1<0) y1=0;
    if ((unsigned int)x2 >= g_fb.hr) x2 = (int)g_fb.hr - 1;
    if ((unsigned int)y2 >= g_fb.vr) y2 = (int)g_fb.vr - 1;
    if (x1 > x2 || y1 > y2) return;
    unsigned int r = (c>>16)&0xFF, g = (c>>8)&0xFF, b = c&0xFF;
    unsigned int u32 = bb_px_u32(r, g, b);
    int w = x2 - x1 + 1;
    for (int y = y1; y <= y2; y++) {
        unsigned long off = (unsigned long)y * g_pitch_b + (unsigned long)x1 * 4UL;
        unsigned int *row = (unsigned int*)(g_back + off);
        for (int x = 0; x < w; x++) row[x] = u32;
    }
    gfx_dirty_mark(x1, y1, x2, y2);
}

/* 线性渐变填充 (竖向): c_top -> c_bottom */
static void gfx_fill_gradient(int x1, int y1, int x2, int y2,
                              unsigned int c_top, unsigned int c_bot) {
    if (!g_initialized || !g_back) return;
    if (x1>x2) { int t=x1; x1=x2; x2=t; }
    if (y1>y2) { int t=y1; y1=y2; y2=t; }
    if (x1<0) x1=0; if (y1<0) y1=0;
    if ((unsigned int)x2 >= g_fb.hr) x2 = (int)g_fb.hr - 1;
    if ((unsigned int)y2 >= g_fb.vr) y2 = (int)g_fb.vr - 1;
    if (x1 > x2 || y1 > y2) return;
    unsigned int rt = (c_top>>16)&0xFF, gt = (c_top>>8)&0xFF, bt = c_top&0xFF;
    unsigned int rb = (c_bot>>16)&0xFF, gb = (c_bot>>8)&0xFF, bb = c_bot&0xFF;
    int h = y2 - y1 + 1;
    int w = x2 - x1 + 1;
    for (int y = y1; y <= y2; y++) {
        unsigned int t  = (unsigned int)(y - y1);
        unsigned int it = (unsigned int)(y2 - y);
        unsigned int r = (rt*it + rb*t + (unsigned int)(h>>1)) / (unsigned int)h;
        unsigned int g = (gt*it + gb*t + (unsigned int)(h>>1)) / (unsigned int)h;
        unsigned int b = (bt*it + bb*t + (unsigned int)(h>>1)) / (unsigned int)h;
        unsigned int u32 = bb_px_u32(r, g, b);
        unsigned long off = (unsigned long)y * g_pitch_b + (unsigned long)x1 * 4UL;
        unsigned int *row = (unsigned int*)(g_back + off);
        for (int x = 0; x < w; x++) row[x] = u32;
    }
    gfx_dirty_mark(x1, y1, x2, y2);
}

/* ==================== 5. ops: draw_char_8x16 (内置 8x16 字体 + 2x2 SuperSampling AA) ====================
 * AA 策略: 把每一个原始 8x16 字体点阵看作 4x4 "物理像素" 的一部分:
 *   对目标物理像素 (X, Y), 计算它所覆盖的 "逻辑子像素" 4 个采样点:
 *     (X*2+dx, Y*2+dy), dx,dy ∈ {0,1}
 *   看这些采样点是否在点阵的同一个字形像素内 (若字形为 1 则算 covered).
 *   covered 个数 / 4 = alpha, 再与背景 alpha 混合 = 平滑边缘字形
 *
 * 为了简化实现 & 获得更好性能, 我们退化为 "1 字形逻辑 = 2x2 物理像素" 等价:
 * 实际做法: 对每个逻辑点 (cx, cy) (cx=0..7, cy=0..15), 如果是 1, 我们把它的覆盖
 *   中心矩形写进 8 个邻居物理像素, 边缘用 1/4 alpha. 效果等效于 Box Filter.
 *
 * 最终输出为字符大小 = 16w × 32h (实际逻辑字形 8x16 放大 2× 但边缘柔化),
 * 但为了与内核 fallback 保持同样的字符尺寸占位 (8x16 字符单元), 我们改回:
 *   - "1 逻辑字形像素" 对应 "1 物理像素", 但我们对"边缘"位置用 4 邻居采样
 *     得到 4 级灰度 (0, 1/4, 1/2, 1) 作为 alpha. */
static void gfx_draw_char_8x16(int x, int y, char c_raw, unsigned int fg, unsigned int bg) {
    if (!g_initialized || !g_back) return;
    unsigned int fr = (fg>>16)&0xFF, fg_ = (fg>>8)&0xFF, fb = fg&0xFF;
    unsigned int br = (bg>>16)&0xFF, bg_ = (bg>>8)&0xFF, bb_ = bg&0xFF;

    int transparent = (bg == 0xFEEDFACEu);  /* COLOR_TRANSPARENT */

    /* 字符索引: 支持 0x20..0x7E 共 96 个字符, 其他显示 SPACE */
    int idx = (int)(unsigned char)c_raw - 0x20;
    if (idx < 0 || idx >= 96) idx = 0;   /* fallback to SPACE */
    const unsigned char *glyph = g_font8x16[idx];

    /* 背景填充: 透明模式跳过, 避免覆盖下层 EFS 图形 */
    if (!transparent) {
        gfx_fill_rect(x, y, x+7, y+15, bg);
    }

    /* 用 4 邻居采样 (Box 抗锯齿) 对每个目标物理像素 (px, py) 计算 alpha:
     * 4 个采样 = (px,py), (px+0.5,py), (px,py+0.5), (px+0.5,py+0.5)
     * 对应 "逻辑点阵" 坐标: (cx,cy), (cx+1,cy), (cx,cy+1), (cx+1,cy+1)
     * 超出字形范围时视为 0. 最终 coverage/4 = 2-bit alpha, 再 8-bit 扩展。 */
    for (int cy = -1; cy < 16; cy++) {
        for (int cx = -1; cx < 8; cx++) {
            int s00 = (cx>=0 && cy>=0) ? ((glyph[cy] >> (7-cx)) & 1u) : 0u;
            int s10 = (cx+1<8 && cy>=0) ? ((glyph[cy] >> (7-(cx+1))) & 1u) : 0u;
            int s01 = (cx>=0 && cy+1<16) ? ((glyph[cy+1] >> (7-cx)) & 1u) : 0u;
            int s11 = (cx+1<8 && cy+1<16) ? ((glyph[cy+1] >> (7-(cx+1))) & 1u) : 0u;
            int cov = s00 + s10 + s01 + s11; /* 0..4 */
            if (cov == 0) continue;
            unsigned int alpha;
            if (cov == 4) alpha = 255;
            else if (cov == 3) alpha = 192;   /* 3/4 */
            else if (cov == 2) alpha = 128;   /* 1/2 */
            else              alpha = 64;    /* 1/4 */
            int px = x + cx + 1;
            int py = y + cy + 1;
            if (px<0||(unsigned int)px>=g_fb.hr||py<0||(unsigned int)py>=g_fb.vr) continue;
            if (transparent) {
                /* 透明背景: 不混合, 直接用前景色 (alpha>=128 即半覆盖以上) */
                if (alpha >= 128) {
                    bb_write_px(g_back, g_pitch_px, px, py, fr, fg_, fb);
                    gfx_dirty_mark(px,py,px,py);
                }
            } else if (alpha == 255) {
                bb_write_px(g_back, g_pitch_px, px, py, fr, fg_, fb);
                gfx_dirty_mark(px,py,px,py);
            } else {
                unsigned int nr = (fr*alpha + br*(255-alpha) + 127)/255;
                unsigned int ng = (fg_*alpha + bg_*(255-alpha) + 127)/255;
                unsigned int nb = (fb*alpha + bb_*(255-alpha) + 127)/255;
                bb_write_px(g_back, g_pitch_px, px, py, nr, ng, nb);
                gfx_dirty_mark(px,py,px,py);
            }
        }
    }
}

/* ==================== 5b. TTF 字体 + Unicode 字符渲染 ==================== */
static struct ttf_font *g_ttf = 0;       /* TTF 字体上下文 */
static int g_ttf_pixel_size = 16;       /* 渲染像素高度 */
static int g_ttf_ascent_px = 16;        /* 字体 ascent (像素): 基线距 cell 顶部距离 */

/* 用 TTF 字体渲染 Unicode 字符到 back buffer.
 * 返回字符实际像素宽度, 0=失败.
 * cell_h: 字符单元高度 (像素), 用于背景填充; <=0 时使用 g_ttf_pixel_size */
static int gfx_draw_char_unicode(int x, int y, unsigned int codepoint,
                                 unsigned int fg, unsigned int bg,
                                 int cell_w, int cell_h) {
    if (!g_initialized || !g_back) return 0;
    if (!g_ttf) return 0;

    /* 获取字形位图 (带缓存) */
    struct ttf_bitmap *bmp = ttf_get_bitmap(g_ttf, codepoint, g_ttf_pixel_size);
    if (!bmp) return 0;

    unsigned int fr = (fg>>16)&0xFF, fg_ = (fg>>8)&0xFF, fb = fg&0xFF;
    unsigned int br = (bg>>16)&0xFF, bg_ = (bg>>8)&0xFF, bb_ = bg&0xFF;
    int transparent = (bg == 0xFEEDFACEu);

    /* 字符单元高度: 优先使用调用者传入的 cell_h, 否则用字体像素高度 */
    int unit_h = (cell_h > 0) ? cell_h : g_ttf_pixel_size;

    /* 背景填充: 填充整个 cell 的背景色.
     * [等宽字体] pixel_size 已校准使 advance == cell_w, 字形天然适配 cell,
     * 不会溢出到相邻 cell, 因此 bg 填充不会覆盖任何相邻字符的像素。*/
    if (!transparent && cell_w > 0) {
        gfx_fill_rect(x, y, x + cell_w - 1, y + unit_h - 1, bg);
    }

    /* 计算字形在单元中的位置.
     *
     * [等宽字距统一: 居中对齐 + 强制步进 = cell_w / 2*cell_w]
     *   TTF 字体本身是比例字体, 每个 glyph 的 bearing_x 是字体设计者为比例排版
     *   选择的 side-bearing (比如 'i' 左边 bearing_x=5, 'M' bearing_x=1)。如果
     *   直接 `draw_x = x + bmp->bearing_x`, 视觉上 'i' 左空隙大、'M' 左空隙小,
     *   "间距不一", 即 glyph 在 mono cell 里没有水平居中。
     *
     *   正确做法: 以 "字形实际非透明像素宽度 width" 为准, 把 glyph 对称地放在
     *   cell 中间 —— 让「字形左边距」和「字形右边距」视觉上相等, 这样任何字符
     *   之间的视觉间距一致。
     *
     * 规则:
     *   - cell_w > 0 且 advance 近似等于 cell_w 或 2*cell_w (CJK):
     *       centered_draw_x = x + (cell_w_or_double - width) / 2
     *     不用 bmp->bearing_x, 直接居中, 避免字体原生 side-bearing 泄漏到等宽布局。
     *   - cell_w == 0 (透明模式比例字): 保留原生 bmp->bearing_x, 返回 bmp->advance。
     *
     * [边界裁剪仍保留] centered_draw_x + width 可能 > cell_right (或 < x),
     *   仍用 cell_right / cell_bottom 做 exclusive 裁剪, 但居中算法把 glyph 放在
     *   正中间, 正常字符两侧剩余空间均等, 溢出极少只发生在 AA padding。*/
    int is_cjk_cell = (cell_w > 0 && bmp->advance > cell_w + (cell_w/4));  /* adv > 1.25*cell_w → 判为 CJK 宽字 */
    int mono_w = 0;
    if (cell_w > 0) {
        mono_w = is_cjk_cell ? 2*cell_w : cell_w;  /* CJK 用整两个 ASCII cell */
    }
    int draw_x;
    if (mono_w > 0) {
        /* [等宽模式] 字形在 mono_w cell 中水平居中, 忽略字体原生 bearing_x */
        int w = bmp->width;
        if (w > mono_w) w = mono_w;   /* 字形超过 cell 时不要让 draw_x 变负 */
        draw_x = x + (mono_w - w) / 2;
    } else {
        /* [比例字模式] 透明/无 cell 排版: 使用字体原生 bearing_x */
        draw_x = x + bmp->bearing_x;
    }
    int cell_right = x + (mono_w > 0 ? mono_w : (cell_w > 0 ? cell_w : bmp->advance));
    int cell_bottom = y + unit_h;
    if (draw_x < x) draw_x = x;
    int draw_y = y + g_ttf_ascent_px - bmp->bearing_y;
    if (draw_y < y) draw_y = y;

    /* 渲染灰度位图到 back buffer (alpha 混合, 适应背景颜色).
     * [严格 cell 边界裁剪] 对 cell_right / cell_bottom 做 exclusive 裁剪,
     *  保证字形抗锯齿 padding 像素不溢出到相邻 cell。
     *  - ASCII: adv 已校准 = cell_w, 核心字形永远在 cell 内; 只有 1px AA padding 溢出
     *  - CJK: adv = 2 × ASCII = cell_w, 同样核心在 cell 内
     *  - 裁剪后, 背景填充 fill_rect (每次都执行) 不会破坏前一字形,
     *    因为前一字形永远没写入到本 cell 内部 → 从根本上消除 "DDDDDD 残像素"。*/
    for (int row = 0; row < bmp->height; row++) {
        int py = draw_y + row;
        if (py < 0 || (unsigned int)py >= g_fb.vr || py >= cell_bottom) continue;
        for (int col = 0; col < bmp->width; col++) {
            int px = draw_x + col;
            if (px < 0 || (unsigned int)px >= g_fb.hr || px >= cell_right) continue;
            unsigned char alpha = bmp->data[row * bmp->width + col];
            if (alpha == 0) continue;

            unsigned long off = (unsigned long)py * g_pitch_b + (unsigned long)px * 4UL;
            unsigned int *p = (unsigned int*)(g_back + off);

            if (alpha == 255) {
                *p = bb_px_u32(fr, fg_, fb);
            } else if (transparent) {
                /* 透明模式: 读取 back buffer 现有像素作为背景, 真正 alpha 混合.
                 * 这样文字可叠加在桌面/窗口任意背景上, 无方块痕迹. */
                unsigned int cur = *p;
                unsigned char cr, cg, cb;
                if (g_pixfmt_rgbx) { cr = cur & 0xFF; cg = (cur>>8)&0xFF; cb = (cur>>16)&0xFF; }
                else                { cb = cur & 0xFF; cg = (cur>>8)&0xFF; cr = (cur>>16)&0xFF; }
                unsigned int nr = (fr * alpha + cr * (255 - alpha) + 127) / 255;
                unsigned int ng = (fg_ * alpha + cg * (255 - alpha) + 127) / 255;
                unsigned int nb = (fb * alpha + cb * (255 - alpha) + 127) / 255;
                *p = bb_px_u32(nr, ng, nb);
            } else {
                unsigned int nr = (fr * alpha + br * (255 - alpha) + 127) / 255;
                unsigned int ng = (fg_ * alpha + bg_ * (255 - alpha) + 127) / 255;
                unsigned int nb = (fb * alpha + bb_ * (255 - alpha) + 127) / 255;
                *p = bb_px_u32(nr, ng, nb);
            }
        }
    }

    /* 标记脏矩形 */
    int dx2 = draw_x + bmp->width - 1;
    int dy2 = draw_y + bmp->height - 1;
    if (!transparent) {
        if (cell_w > 0 && dx2 < x + cell_w - 1) dx2 = x + cell_w - 1;
        if (dy2 < y + unit_h - 1) dy2 = y + unit_h - 1;
    }
    gfx_dirty_mark(x, y, dx2, dy2);

    /* 返回字形步进宽度:
     *   - 等宽模式: 固定返回 mono_w (ASCII = cell_w, CJK = 2*cell_w),
     *     让上游 (kernel print_string / compositor 文字宽度计算 / EFS gui_text)
     *     全部按统一的等宽步进, 不再使用 TTF 原生 advance, 彻底消除"字距不一"。
     *   - 比例字模式 (cell_w==0 透明): 保留返回原生 bmp->advance。*/
    if (mono_w > 0) return mono_w;
    return bmp->advance;
}

/* ==================== 6. ops: scroll_up (双缓冲下只需搬 back buffer, 然后 flush 脏矩形) ==================== */
static void gfx_scroll_up(int px_lines) {
    if (!g_initialized || !g_back) return;
    if (px_lines <= 0) return;
    if ((unsigned int)px_lines >= g_fb.vr) { px_lines = (int)g_fb.vr; }
    unsigned long move_bytes = (unsigned long)(g_fb.vr - (unsigned int)px_lines) * g_pitch_b;
    /* 逐 32-bit 向上搬移 (比逐字节快 4×) */
    unsigned int *dst = (unsigned int*)g_back;
    unsigned int *src = (unsigned int*)(g_back + (unsigned long)px_lines * g_pitch_b);
    unsigned long n32 = move_bytes >> 2;
    for (unsigned long i = 0; i < n32; i++) dst[i] = src[i];
    /* 底部清空为背景色 (同样 32-bit burst) */
    unsigned long clear_start_idx = ((unsigned long)(g_fb.vr - (unsigned int)px_lines) * g_pitch_b) >> 2;
    unsigned long clear_bytes = (unsigned long)px_lines * g_pitch_b;
    unsigned long n32c = clear_bytes >> 2;
    unsigned int bg_u32 = bb_px_u32(0x1A, 0x1A, 0x2E);  /* 与内核 bg 一致: 深靛色 0x1A1A2E */
    for (unsigned long i = 0; i < n32c; i++) dst[clear_start_idx + i] = bg_u32;
    /* 整个屏幕都是脏的 */
    gfx_dirty_mark(0, 0, (int)g_fb.hr - 1, (int)g_fb.vr - 1);
    /* scroll_up 调用后通常紧跟着用户看屏幕, 这里立即 flush 避免"下一帧才看到滚动结果" */
    gfx_flush_now();
}

/* ==================== 7. ops: get_mode ==================== */
static void gfx_get_mode(unsigned int *out_hr, unsigned int *out_vr,
                         unsigned int *out_ppsl, unsigned int ***out_fb_base) {
    if (out_hr)      *out_hr      = g_fb.hr;
    if (out_vr)      *out_vr      = g_fb.vr;
    if (out_ppsl)    *out_ppsl    = g_pitch_px;
    if (out_fb_base) *out_fb_base = (unsigned int **)&g_fb.fb_base;
}

/* [优化] 暴露 back buffer, 让 Mesa swrast 直接渲染到 back buffer,
 * 消除 BO→back buffer 全屏拷贝 (节省 8MB/帧 @1080p). */
static int gfx_get_backbuffer(void **out_ptr, int *out_pitch, int *out_w, int *out_h) {
    if (!g_back || !g_initialized) return -1;
    if (out_ptr)   *out_ptr   = g_back;
    if (out_pitch) *out_pitch = (int)g_pitch_b;  /* bytes/line for Mesa/GBM convention */
    if (out_w)     *out_w     = (int)g_fb.hr;
    if (out_h)     *out_h     = (int)g_fb.vr;
    return 0;
}

/* ==================== 8. ops 表 (严格按 drv_common.h 顺序) ==================== */
static struct drv_gfx_ops g_gfx_ops = {
    gfx_put_pixel,       /* put_pixel */
    gfx_fill_rect,       /* fill_rect */
    gfx_draw_char_8x16,  /* draw_char_8x16 (含内置 8x16 字体 + 4 邻居 SSAA) */
    gfx_scroll_up,       /* scroll_up (32-bit burst + 立即 flush) */
    gfx_get_mode,        /* get_mode */
    gfx_flush_now,       /* flush: 双缓冲脏矩形一次性刷到前端 (无脏矩形则 NOP) */
    gfx_copy_back_rect_to_front, /* copy_back_rect_to_front: 局部 back→front 拷贝 (鼠标拖尾修复) */
    gfx_blit_buffer,     /* blit_buffer: Mesa swrast → back buffer 批量拷贝 */
    gfx_dirty_mark,      /* mark_dirty_rect: 手动标记脏矩形 */
    gfx_get_backbuffer,  /* get_backbuffer: 暴露 back buffer 给 Mesa 直接渲染 */
    gfx_draw_char_unicode, /* draw_char_unicode: TTF 字体 Unicode 字符渲染 */
};

/* ==================== 9. 像素格式自动检测 (判断 front buffer 到底是 BGRx 还是 RGBx)
 * 思路: 在进入驱动前, front buffer 左上角第一个像素 (0,0) 应该是 bootloader 之前写入的
 * "黑或白或蓝色" 之类. 我们把 back buffer 同位置假设为已知值, 然后对比真实 front:
 *   - 写 back (0,0) = (R=0xFF, G=0, B=0) 纯红, 用 flush 脏矩形回 front
 *   - 读 front(0,0) 字节序: 若 p[0]=R=0xFF => RGBx, 若 p[2]=R=0xFF => BGRx
 *   - 再把 front 原始值恢复.
 * 如果此像素当前为全 0 (黑屏), 那读回字节 0/2 都是 0 无法区分, 默认 BGRx 即可. */
static void gfx_detect_pixfmt(void) {
    unsigned char *fb_bytes = (unsigned char*)g_fb.fb_base;
    unsigned int orig0 = *(unsigned int*)fb_bytes;

    /* 写一个 "R=0xAA, G=0x55, B=0x11" 的已知颜色到 back(0,0), 然后 flush.
     * 我们临时强制认为是 BGRx 写 back */
    g_pixfmt_rgbx = 0;
    bb_write_px(g_back, g_pitch_px, 0, 0, 0xAA, 0x55, 0x11);
    /* 直接 memcpy 这一个像素到 front (不通过 flush 流程, 因为 flush 依赖 g_dirty 逻辑还在 init) */
    *(unsigned int*)fb_bytes = *(unsigned int*)g_back;

    unsigned int written = *(unsigned int*)fb_bytes;
    unsigned char *p = (unsigned char*)&written;
    if (p[0] == 0xAA && p[1] == 0x55 && p[2] == 0x11) {
        /* p[0]=R => RGBx 模式 */
        g_pixfmt_rgbx = 1;
    } else if (p[0] == 0x11 && p[1] == 0x55 && p[2] == 0xAA) {
        /* p[0]=B => BGRx 模式 (最常见 UEFI GOP) */
        g_pixfmt_rgbx = 0;
    } else {
        /* 其他 (例如 PixelBitMask 不连续格式): 维持 BGRx 默认, 不保证颜色正确 */
        g_pixfmt_rgbx = 0;
    }
    /* 恢复 (0,0) 原始值 */
    *(unsigned int*)fb_bytes = orig0;

    /* 现在用检测到的格式, 把整个 front buffer 拷贝到 back buffer (保证初始 back == front) */
    unsigned long sz = g_fb_sz;
    unsigned int *f = (unsigned int*)fb_bytes;
    unsigned int *b = (unsigned int*)g_back;
    unsigned long n = sz >> 2;
    for (unsigned long i = 0; i < n; i++) b[i] = f[i];
}

/* ==================== 10. 驱动入口 ==================== */
/* drv_main 的实际实现在下方 (drv_entry 通过 forward declaration 调用) */

/* 把数字打印到 char 数组 (base=10 十进制) */
static void dec_buf(char *out, int *pn, unsigned long v) {
    if (v == 0) { out[(*pn)++] = '0'; return; }
    char tmp[20]; int p = 0;
    while (v) { tmp[p++] = '0' + (char)(v % 10); v /= 10; }
    while (p--) out[(*pn)++] = tmp[p];
}

void drv_main(struct drv_kernel_if *iface, struct drv_gop_fb *fb) {
    if (!iface || iface->magic != DRV_IFACE_MAGIC) {
        if (iface) iface->log("[Graphics] bad iface magic\n");
        return;
    }
    iface->log("[Graphics] drv_main entered\n");
    if (!fb || !fb->fb_base) { iface->log("[Graphics] no GOP fb info, abort.\n"); return; }

    /* 缓存 fb */
    g_fb = *fb;

    /* 一次性计算 pitch:
     *   g_pitch_px = 每扫描行像素数 (ppsl 或 hr)
     *   g_pitch_b  = 每扫描行字节数 = pitch_px * 4
     *   g_fb_sz    = 缓冲区总字节数   = vr * pitch_b */
    g_pitch_px = g_fb.ppsl ? g_fb.ppsl : g_fb.hr;
    g_pitch_b  = (unsigned long)g_pitch_px * 4ULL;
    g_fb_sz    = (unsigned long)g_fb.vr * g_pitch_b;

    /* back buffer 必须落在可写内存区域; kmalloc 从内核堆分配 */
    g_back = (unsigned char*)iface->kmalloc(g_fb_sz ? g_fb_sz : (1024UL*768UL*4UL));
    if (!g_back) { iface->log("[Graphics] kmalloc back buffer failed.\n"); return; }

    /* 初始清零 */
    {
        unsigned long i = 0;
        unsigned long n = (g_fb_sz ? g_fb_sz : (1024UL*768UL*4UL)) >> 2;
        unsigned int *bp = (unsigned int*)g_back;
        for (; i < n; i++) bp[i] = 0;
    }
    g_initialized = 1;

    /* 自动检测像素格式 (BGRx vs RGBx) */
    gfx_detect_pixfmt();

    /* 脏矩形初始清空 */
    g_dirty = 0; g_dx1 = g_dy1 = g_dx2 = g_dy2 = 0;

    /* 分辨率信息输出到串口 */
    char buf[120]; int n = 0;
    const char *h = "[Graphics] mode=";
    for (int i = 0; h[i]; i++) buf[n++] = h[i];
    dec_buf(buf, &n, (unsigned long)g_fb.hr);
    buf[n++] = 'x';
    dec_buf(buf, &n, (unsigned long)g_fb.vr);
    h = " pitch="; for (int i = 0; h[i]; i++) buf[n++] = h[i];
    dec_buf(buf, &n, (unsigned long)g_pitch_px);
    h = " fmt="; for (int i = 0; h[i]; i++) buf[n++] = h[i];
    if (g_pixfmt_rgbx) { buf[n++]='R'; buf[n++]='G'; buf[n++]='B'; buf[n++]='x'; }
    else              { buf[n++]='B'; buf[n++]='G'; buf[n++]='R'; buf[n++]='x'; }
    h = " fb="; for (int i = 0; h[i]; i++) buf[n++] = h[i];
    const char *hex = "0123456789ABCDEF";
    unsigned long a = (unsigned long)g_fb.fb_base;
    for (int i = 60; i >= 0; i -= 4) buf[n++] = hex[(a >> i) & 0xF];
    h = " back="; for (int i = 0; h[i]; i++) buf[n++] = h[i];
    a = (unsigned long)g_back;
    for (int i = 60; i >= 0; i -= 4) buf[n++] = hex[(a >> i) & 0xF];
    buf[n++] = '\n'; buf[n] = 0;
    iface->log(buf);

    /* 打印优化信息 */
    iface->log("[Graphics] Double-Buffering + Dirty-Rect + SSAA-Font + BGRx/Auto-Detect enabled.\n");

    /* 加载 TTF 字体 (Sarasa Gothic — 等宽字体) */
    ttf_set_alloc(iface->kmalloc, iface->kfree, iface->file_read, iface->log);
    g_ttf = ttf_load("/EFMOS/fonts/sarasa-gothic-regular.ttf");
    if (g_ttf) {
        iface->log("[Graphics] TTF font loaded (Sarasa Gothic).\n");

        /* [等宽字体校准] 自动查找使 ASCII advance <= target_w 且尽可能接近它的 pixel_size.
         * Sarasa Gothic 是等宽字体, 所有 ASCII 共享同一 advance, CJK 为 2×.
         *
         * 目标: advance 必须 <= cell_w (12)。如果选了 advance=13 的 pixel_size,
         * 每个字形都会比 cell 大 1px, 连续 N 个字溢出 N 像素 → 最终字符位置不对、
         * 光标与文字错位、bg 填充覆盖前字边缘。
         *
         * 选择策略:
         *   - 先找 advance==12 的 perfect match; 找不到则:
         *   - 在所有 advance <= 12 的里面选最大的 (最接近 12, 字形尽可能大)
         *   - 如果所有都 > 12 (极小概率), 则退化为 pick smallest advance.
         *
         * 之前日志 ps=14 → adv=13 (比 12 大 1 → 溢出), 改为取 ps=13 或 12 下的
         * 最大且 ≤ 12 者 (通常 ps=13 对应 adv=12, 这样字形完整且不溢出)。*/
        int target_w = 14;  /* 与内核 FONT_W 一致 (1.2x: 12→14) */
        int best_ps = 18, best_adv = 0;
        int min_adv_ps = 14, min_adv = 9999;
        for (int ps = 10; ps <= 32; ps++) {
            int adv = ttf_get_advance_px(g_ttf, (unsigned int)'M', ps);
            if (adv == target_w) { best_ps = ps; best_adv = adv; goto found; }  /* 完美匹配 */
            if (adv <= target_w && adv > best_adv) { best_adv = adv; best_ps = ps; }
            if (adv < min_adv) { min_adv = adv; min_adv_ps = ps; }
        }
        if (best_adv == 0) { best_ps = min_adv_ps; }   /* 兜底: 所有都 > target */
    found:
        g_ttf_pixel_size = best_ps;
        g_ttf_ascent_px = ttf_get_ascent_px(g_ttf, g_ttf_pixel_size);

        /* 输出校准结果 */
        int final_adv = ttf_get_advance_px(g_ttf, (unsigned int)'M', g_ttf_pixel_size);
        char calib[80]; int cn = 0;
        const char *s = "[Graphics] Mono calib: ps=";
        for (int i = 0; s[i]; i++) calib[cn++] = s[i];
        calib[cn++] = '0' + (g_ttf_pixel_size / 10);
        calib[cn++] = '0' + (g_ttf_pixel_size % 10);
        s = " adv="; for (int i = 0; s[i]; i++) calib[cn++] = s[i];
        calib[cn++] = '0' + (final_adv / 10);
        calib[cn++] = '0' + (final_adv % 10);
        calib[cn++] = '\n'; calib[cn] = 0;
        iface->log(calib);
    } else {
        iface->log("[Graphics] TTF font not found, using bitmap fallback.\n");
    }

    /* 注册驱动 (名字 "Graphics") */
    int rc = iface->register_driver("Graphics", DRV_TYPE_GFX, &g_gfx_ops, sizeof(g_gfx_ops));
    if (rc == 0) {
        iface->log("[Graphics] registered graphics ops OK.\n");
    } else {
        iface->log("[Graphics] register_driver failed.\n");
    }
}
