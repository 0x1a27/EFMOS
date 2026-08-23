#include <efi.h>
#include <efilib.h>
#include "bootlogo.h"
EFI_BOOT_SERVICES *BS; EFI_RUNTIME_SERVICES *RT; EFI_SYSTEM_TABLE *ST;
EFI_GUID gEfiBlockIoProtocolGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

/* ---------- Boot Graphics (LOGO + Ring Spinner) ----------
 * 在 boot 阶段用 GOP 画 EFMOS 大字 LOGO + 圆形加载动画,
 * 缓解内核/磁盘加载慢时用户的"黑屏死机"焦虑感。
 * 使用 EFI_GRAPHICS_OUTPUT_PROTOCOL.Blt() 绘制到 BltBuffer,
 * 然后一次性 Blt 到 FB, 兼容性好于直接写 FB。
 * 字体: 8x16 ASCII (与内核 font 表保持一致位图),
 *       放大 5 倍 (每字符 40x80 像素) 显示 E F M O S 大字。 */

/* 与内核 font 相同的 8x16 ASCII 位图 (仅使用字母 E,F,M,O,S 的索引) */
static const UINT8 boot_font[95][16] = {
/*  32 SPACE  */{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/*  48 '0'    */{0,0,0x7C,0xC6,0xC6,0xD6,0xD6,0xC6,0xC6,0x7C,0,0,0,0,0,0},
/*  49 '1'    */{0,0,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x7E,0,0,0,0,0,0},
/*  50 '2'    */{0,0,0x7C,0xC6,0x06,0x0C,0x18,0x30,0xC6,0xFE,0,0,0,0,0,0},
/*  51 '3'    */{0,0,0x7C,0xC6,0x06,0x3C,0x06,0x06,0xC6,0x7C,0,0,0,0,0,0},
/*  52 '4'    */{0,0,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0,0,0,0,0,0},
/*  53 '5'    */{0,0,0xFE,0xC0,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0,0,0,0,0,0},
/*  54 '6'    */{0,0,0x3C,0x60,0xC0,0xFC,0xC6,0xC6,0xC6,0x7C,0,0,0,0,0,0},
/*  55 '7'    */{0,0,0xFE,0xC6,0x06,0x0C,0x18,0x30,0x30,0x30,0,0,0,0,0,0},
/*  56 '8'    */{0,0,0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0x7C,0,0,0,0,0,0},
/*  57 '9'    */{0,0,0x7C,0xC6,0xC6,0x7E,0x06,0x06,0x0C,0x78,0,0,0,0,0,0},
/*  58 ':'    */{0,0,0,0,0,0,0x18,0x18,0,0,0,0,0,0,0,0},
/*  59 ';'    */{0,0,0,0,0,0,0x18,0x18,0x30,0,0,0,0,0,0,0},
/*  60 '<'    */{0,0,0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0,0,0,0,0,0,0},
/*  61 '='    */{0,0,0,0,0x7E,0,0,0x7E,0,0,0,0,0,0,0,0},
/*  62 '>'    */{0,0,0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0,0,0,0,0,0,0},
/*  63 '?'    */{0,0,0x7C,0xC6,0x06,0x0C,0x18,0x18,0,0x18,0,0,0,0,0,0},
/*  64 '@'    */{0,0,0x7C,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0,0,0,0,0,0},
/*  65 'A'    */{0,0,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0,0,0,0,0,0},
/*  66 'B'    */{0,0,0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xC6,0xFC,0,0,0,0,0,0},
/*  67 'C'    */{0,0,0x3C,0x66,0xC0,0xC0,0xC0,0xC0,0x66,0x3C,0,0,0,0,0,0},
/*  68 'D'    */{0,0,0xF8,0xCC,0xC6,0xC6,0xC6,0xC6,0xCC,0xF8,0,0,0,0,0,0},
/*  69 'E'    */{0,0,0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0xFE,0,0,0,0,0,0},
/*  70 'F'    */{0,0,0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0xC0,0,0,0,0,0,0},
/*  71 'G'    */{0,0,0x3C,0x66,0xC0,0xC0,0xDE,0xC6,0x66,0x3C,0,0,0,0,0,0},
/*  72 'H'    */{0,0,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0,0,0,0,0,0},
/*  73 'I'    */{0,0,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0,0,0,0,0,0},
/*  74 'J'    */{0,0,0x06,0x06,0x06,0x06,0x06,0xC6,0xC6,0x7C,0,0,0,0,0,0},
/*  75 'K'    */{0,0,0xC6,0xCC,0xD8,0xF0,0xF0,0xD8,0xCC,0xC6,0,0,0,0,0,0},
/*  76 'L'    */{0,0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFE,0,0,0,0,0,0},
/*  77 'M'    */{0,0,0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0,0,0,0,0,0},
/*  78 'N'    */{0,0,0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0xC6,0,0,0,0,0,0},
/*  79 'O'    */{0,0,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0,0,0,0,0,0},
/*  80 'P'    */{0,0,0xFC,0xC6,0xC6,0xFC,0xC0,0xC0,0xC0,0xC0,0,0,0,0,0,0},
/*  81 'Q'    */{0,0,0x7C,0xC6,0xC6,0xC6,0xD6,0xCE,0xC6,0x7C,0,0,0,0,0,0},
/*  82 'R'    */{0,0,0xFC,0xC6,0xC6,0xFC,0xD8,0xCC,0xC6,0xC6,0,0,0,0,0,0},
/*  83 'S'    */{0,0,0x7C,0xC6,0xC0,0x7C,0x06,0x06,0xC6,0x7C,0,0,0,0,0,0},
/*  84 'T'    */{0,0,0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0,0},
/*  85 'U'    */{0,0,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0,0,0,0,0,0},
/*  86 'V'    */{0,0,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0,0,0,0,0,0},
/*  87 'W'    */{0,0,0xC6,0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0x44,0,0,0,0,0,0},
/*  88 'X'    */{0,0,0xC6,0xC6,0x6C,0x38,0x38,0x6C,0xC6,0xC6,0,0,0,0,0,0},
/*  89 'Y'    */{0,0,0xC6,0xC6,0x6C,0x38,0x18,0x18,0x18,0x18,0,0,0,0,0,0},
/*  90 'Z'    */{0,0,0xFE,0x06,0x0C,0x18,0x30,0x60,0xC0,0xFE,0,0,0,0,0,0}
};

/* 全局 GOP (如果能成功获取) */
static EFI_GRAPHICS_OUTPUT_PROTOCOL *s_gop = NULL;
static EFI_GRAPHICS_OUTPUT_BLT_PIXEL *s_blt = NULL;  /* 暂存 blt buffer (全屏) */
static UINT32 s_hr, s_vr;

/* ---------- UEFI 辅助函数 (需在 boot_progress 之前定义, 消除隐式声明) ---------- */
void print(CHAR16 *s) { if (ST && ST->ConOut) ST->ConOut->OutputString(ST->ConOut, s); }
void print_hex(UINT64 v) { CHAR16 h[]=L"0123456789ABCDEF", b[19]=L"0x0000000000000000"; for(int i=17;i>=2;i--){b[i]=h[v&0xF];v>>=4;} print(b); }
int my_memcmp(const void *a, const void *b, UINTN n) { const UINT8 *p1=a,*p2=b; while(n--){if(*p1!=*p2)return *p1-*p2;p1++;p2++;} return 0; }
void *my_memcpy(void *d, const void *s, UINTN n) { UINT8 *dd=d,*ss=(UINT8*)s; while(n--)*dd++=*ss++; return d; }
static UINTN my_strlen(const CHAR8 *s) { UINTN l=0; while(*s++) l++; return l; }

/* 尝试启用 GOP 图形模式: 从显示器支持的 GOP 模式中选分辨率最高的
 * (UEFI GOP 已枚举显示器/EDID 支持的模式, 遍历取最大即"从显示器读取").
 * 限制: 不超过 1920x1080 (QEMU 默认 vgamem 16MB 够用, 更高可能显存不足).
 * 回退: 无匹配则保持当前模式. */
static EFI_STATUS boot_init_gfx(void) {
    EFI_STATUS st = BS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&s_gop);
    if (EFI_ERROR(st) || !s_gop || !s_gop->Mode) { s_gop = NULL; return st; }

    /* 遍历所有 GOP 模式, 选像素总数最多且 <= 1920x1080 的 */
    UINT32 target = 0xFFFFFFFF;
    UINT32 best_pixels = 0;
    for (UINT32 m = 0; m < s_gop->Mode->MaxMode; m++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN sz = 0;
        if (EFI_ERROR(s_gop->QueryMode(s_gop, m, &sz, &info))) continue;
        UINT32 hr = info->HorizontalResolution;
        UINT32 vr = info->VerticalResolution;
        /* 只接受 32-bit 像素格式 (BGRX/RBGX/BitMask) */
        int ok_fmt = (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
                      info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
                      info->PixelFormat == PixelBitMask);
        /* 限制上限: 1920x1080 (约 8MB framebuffer, QEMU vgamem 16MB 够用) */
        if (ok_fmt && hr >= 640 && vr >= 480 && hr <= 1920 && vr <= 1200) {
            UINT32 pixels = hr * vr;
            if (pixels > best_pixels) {
                best_pixels = pixels;
                target = m;
            }
        }
        BS->FreePool(info);
    }

    if (target != 0xFFFFFFFF && target != s_gop->Mode->Mode)
        s_gop->SetMode(s_gop, target);
    s_hr = s_gop->Mode->Info->HorizontalResolution;
    s_vr = s_gop->Mode->Info->VerticalResolution;
    /* 分配全屏 BltBuffer (BGRx 32bit 像素) */
    BS->AllocatePool(EfiLoaderData, (UINTN)s_hr * s_vr * 4, (VOID**)&s_blt);
    return EFI_SUCCESS;
}
/* 将 s_blt 一次性刷新到屏幕 */
static void boot_flush(void) {
    if (!s_gop || !s_blt) return;
    s_gop->Blt(s_gop, s_blt, EfiBltBufferToVideo, 0, 0, 0, 0, s_hr, s_vr, 0);
}
/* 清屏填充一色 (或直接写 FB 更快, 用 blt 填充矩形也可) */
static void boot_fill(UINT32 rgb) {
    if (!s_gop || !s_blt) return;
    UINT8 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL px;
    px.Red = r; px.Green = g; px.Blue = b; px.Reserved = 0;
    s_gop->Blt(s_gop, &px, EfiBltVideoFill, 0, 0, 0, 0, s_hr, s_vr, 0);
    /* 同时填 s_blt, 以便后续增量叠加 */
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p = s_blt;
    for (UINTN i = 0; i < (UINTN)s_hr * s_vr; i++, p++) { p->Red = r; p->Green = g; p->Blue = b; p->Reserved = 0; }
}
/* 在 blt buffer (x,y) 画一个点 */
static inline void boot_px(UINT32 x, UINT32 y, UINT32 rgb) {
    if (!s_blt || x >= s_hr || y >= s_vr) return;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *p = &s_blt[(UINTN)y * s_hr + x];
    p->Red   = (UINT8)((rgb >> 16) & 0xFF);
    p->Green = (UINT8)((rgb >>  8) & 0xFF);
    p->Blue  = (UINT8)( rgb        & 0xFF);
}
/* 画放大 scale 倍的单字符 */
static void boot_draw_char(UINT32 x, UINT32 y, char ch, UINT32 rgb, UINT32 scale) {
    if (!s_blt) return;
    if (ch < 32 || ch > 90) return;
    const UINT8 *g = boot_font[ch - 32];
    for (UINT32 r = 0; r < 16; r++) {
        UINT8 line = g[r];
        for (UINT32 c = 0; c < 8; c++) {
            if (line & (0x80 >> c)) {
                for (UINT32 sy = 0; sy < scale; sy++)
                    for (UINT32 sx = 0; sx < scale; sx++)
                        boot_px(x + c*scale + sx, y + r*scale + sy, rgb);
            }
        }
    }
}
/* 在屏幕中上方居中绘制 EFMOS LOGO 图片 (来自 bootlogo.h, 8-bit alpha 灰度)
 * 抗锯齿: 双线性插值 (bilinear interpolation), 放大时边缘灰度平滑过渡,
 * 消除锯齿. 背景=黑, 前景=白, out = alpha (灰度值). */
static void boot_draw_logo(void) {
    if (!s_blt) return;
    UINT32 iw = LOGO_W, ih = LOGO_H;
    /* 计算缩放: 适应屏幕宽度的 40%, 不超过 400px */
    UINT32 max_w = s_hr * 40 / 100;
    if (max_w > 400) max_w = 400;
    UINT32 scale = 1;
    if (iw * 2 <= max_w) scale = max_w / iw;
    if (scale < 1) scale = 1;
    UINT32 dw = iw * scale, dh = ih * scale;
    /* 居中: 水平居中, 垂直在屏幕上方约 18% 处 */
    UINT32 start_x = (s_hr > dw) ? (s_hr - dw) / 2 : 0;
    UINT32 start_y = s_vr * 18 / 100;
    /* 双线性插值: 对每个目标像素, 在源 alpha 图上做 2D 线性插值.
     * 定点运算 (16.16 格式), 避免浮点 (UEFI 无 FPU 初始化).
     * fx = x * (iw-1) / (dw-1), fy = y * (ih-1) / (dh-1)
     * 取 4 个相邻源像素 a00,a10,a01,a11, 按 wx,wy 权重插值. */
    for (UINT32 y = 0; y < dh; y++) {
        UINT32 fy_q16 = (dh > 1) ? (y * (ih - 1) * 65536U) / (dh - 1) : 0;
        UINT32 sy0 = fy_q16 >> 16;
        UINT32 sy1 = (sy0 + 1 < ih) ? sy0 + 1 : sy0;
        UINT32 wy  = (fy_q16 & 0xFFFF) >> 8;  /* 权重 0..255 */
        for (UINT32 x = 0; x < dw; x++) {
            UINT32 fx_q16 = (dw > 1) ? (x * (iw - 1) * 65536U) / (dw - 1) : 0;
            UINT32 sx0 = fx_q16 >> 16;
            UINT32 sx1 = (sx0 + 1 < iw) ? sx0 + 1 : sx0;
            UINT32 wx  = (fx_q16 & 0xFFFF) >> 8;  /* 权重 0..255 */
            /* 4 个源像素 alpha */
            UINT32 a00 = boot_logo_alpha[sy0 * LOGO_W + sx0];
            UINT32 a10 = boot_logo_alpha[sy0 * LOGO_W + sx1];
            UINT32 a01 = boot_logo_alpha[sy1 * LOGO_W + sx0];
            UINT32 a11 = boot_logo_alpha[sy1 * LOGO_W + sx1];
            /* 双线性: 先 x 方向插值 (×256), 再 y 方向 (÷65536) */
            UINT32 top = a00 * (256 - wx) + a10 * wx;
            UINT32 bot = a01 * (256 - wx) + a11 * wx;
            UINT32 val = (top * (256 - wy) + bot * wy) / 65536U;
            if (val == 0) continue;
            UINT8 v = (UINT8)val;
            boot_px(start_x + x, start_y + y, ((UINT32)v << 16) | ((UINT32)v << 8) | v);
        }
    }
    boot_flush();
}
/* ---------- 启动加载界面: 圆形 ring spinner (无进度条/无文字) ----------
 * 经典圆形加载动画: 72 个点构成连续圆环, 一段亮弧顺时针旋转。
 * 点足够密 (每 5°一个) 且相邻点重叠, 圆环边缘平滑无锯齿。
 * boot_progress 每次调用前进一帧; boot_spinner 连续动画。 */
static int s_spin_frame = 0;  /* spinner 旋转帧计数 (跨函数共享, 持续递增) */

/* 72 点单位圆坐标 (×1000, 整数运算避免浮点), 每 5° 一个点 */
static const int RING_SX[72] = {
    1000, 996, 985, 966, 940, 906, 866, 819, 766, 707,
    643, 574, 500, 423, 342, 259, 174, 87, 0, -87,
    -174, -259, -342, -423, -500, -574, -643, -707, -766, -819,
    -866, -906, -940, -966, -985, -996, -1000, -996, -985, -966,
    -940, -906, -866, -819, -766, -707, -643, -574, -500, -423,
    -342, -259, -174, -87, 0, 87, 174, 259, 342, 423,
    500, 574, 643, 707, 766, 819, 866, 906, 940, 966,
    985, 996
};
static const int RING_SY[72] = {
    0, 87, 174, 259, 342, 423, 500, 574, 643, 707,
    766, 819, 866, 906, 940, 966, 985, 996, 1000, 996,
    985, 966, 940, 906, 866, 819, 766, 707, 643, 574,
    500, 423, 342, 259, 174, 87, 0, -87, -174, -259,
    -342, -423, -500, -574, -643, -707, -766, -819, -866, -906,
    -940, -966, -985, -996, -1000, -996, -985, -966, -940, -906,
    -866, -819, -766, -707, -643, -574, -500, -423, -342, -259,
    -174, -87
};

/* 画一帧圆形 ring spinner: 圆环 (暗) + 旋转亮弧 (头亮尾暗渐变)
 * cx,cy=圆心  r=半径  frame=旋转帧
 * 点以坐标为中心绘制 (dot×dot 居中), 相邻点重叠 → 圆环连续无空隙 */
static void draw_ring_spinner(UINT32 cx, UINT32 cy, UINT32 r, int frame) {
    if (!s_gop || !s_blt || r < 6) return;
    const UINT32 dim = 0x333333;          /* 暗环底色 (深灰) */
    /* 亮弧: 18 个点 (90°, 每 5°一点), 纯白色渐变 (头亮→尾暗) */
    static const UINT32 bright[18] = {
        0xFFFFFF, 0xF0F0F0, 0xE0E0E0, 0xD0D0D0, 0xC0C0C0, 0xB0B0B0,
        0xA0A0A0, 0x909090, 0x808080, 0x707070, 0x606060, 0x505050,
        0x484848, 0x404040, 0x383838, 0x303030, 0x2A2A2A, 0x242424
    };
    UINT32 dot = (r >= 20) ? 3 : 2;       /* 点像素大小 (大屏 3px 让点重叠) */
    int half = (int)(dot / 2);
    int start = frame % 72;               /* 亮弧起点 */
    for (int i = 0; i < 72; i++) {
        int px = (int)cx + (RING_SX[i] * (int)r) / 1000;
        int py = (int)cy + (RING_SY[i] * (int)r) / 1000;
        UINT32 color = dim;
        int rel = (i - start + 72) % 72;
        if (rel < 18) color = bright[rel];
        for (UINT32 dy = 0; dy < dot; dy++)
            for (UINT32 dx = 0; dx < dot; dx++)
                boot_px((UINT32)(px - half + (int)dx),
                        (UINT32)(py - half + (int)dy), color);
    }
}

/* boot_progress: 只画圆形 spinner (无进度条/无屏幕文字) */
static void boot_progress(int pct, CHAR16 *msg) {
    (void)pct;  /* 百分比不再用于绘制, 仅保留参数兼容调用点 */
    if (s_gop && s_blt) {
        /* spinner 圆心: 水平居中, 垂直 65% 处 */
        UINT32 cx = s_hr / 2;
        UINT32 cy = s_vr * 65 / 100;
        UINT32 r  = (s_hr >= 1024) ? 20 : 14;
        UINT32 dot = (s_hr >= 1024) ? 2 : 2;
        UINT32 pad = dot + 1;             /* 擦除留 1px 余量 */

        /* 擦除 spinner 圆环区域 (仅圆环, 不含文字) */
        UINT32 ey0 = (cy > r + pad) ? cy - r - pad : 0;
        UINT32 ey1 = cy + r + pad;
        UINT32 ex0 = (cx > r + pad) ? cx - r - pad : 0;
        UINT32 ex1 = (cx + r + pad + 2 < s_hr) ? cx + r + pad + 2 : s_hr;
        for (UINT32 yy = ey0; yy < ey1; yy++)
            for (UINT32 xx = ex0; xx < ex1; xx++)
                boot_px(xx, yy, 0x000000);

        /* 画圆形 spinner (前进一帧) */
        draw_ring_spinner(cx, cy, r, s_spin_frame);
        s_spin_frame++;
        boot_flush();
    }
    /* 串口仍输出文字状态 (便于调试), 但屏幕上不显示 */
    if (msg) { print(msg); }
    print(L"\r\n");
}

/* ---------- 转圈加载提示 (boot 完成后短暂展示) ----------
 * 圆形 ring spinner 连续旋转 total_ms 毫秒, 随后进入内核。 */
static void boot_spinner(UINT32 total_ms) {
    if (!s_gop || !s_blt) { BS->Stall((UINTN)total_ms * 1000); return; }

    UINT32 cx = s_hr / 2;
    UINT32 cy = s_vr * 65 / 100;
    UINT32 r  = (s_hr >= 1024) ? 20 : 14;
    UINT32 dot = (s_hr >= 1024) ? 2 : 2;
    UINT32 pad = dot + 1;

    UINT32 ey0 = (cy > r + pad) ? cy - r - pad : 0;
    UINT32 ey1 = cy + r + pad;
    UINT32 ex0 = (cx > r + pad) ? cx - r - pad : 0;
    UINT32 ex1 = (cx + r + pad + 2 < s_hr) ? cx + r + pad + 2 : s_hr;

    UINT32 frame_ms = 60;
    UINT32 frames = total_ms / frame_ms;
    if (frames < 72) frames = 72;  /* 至少转完一圈 (72 帧) */

    for (UINT32 f = 0; f < frames; f++) {
        /* 擦除 spinner 圆环区域 */
        for (UINT32 yy = ey0; yy < ey1; yy++)
            for (UINT32 xx = ex0; xx < ex1; xx++)
                boot_px(xx, yy, 0x000000);

        draw_ring_spinner(cx, cy, r, s_spin_frame);
        s_spin_frame++;

        boot_flush();
        BS->Stall((UINTN)frame_ms * 1000);
    }
}

static EFI_BLOCK_IO *bio; static UINT64 part_start;
static UINT32 block_size, inodes_per_group, inode_size; static VOID *gdt;

EFI_STATUS read_sectors(UINT64 lba, UINTN bytes, VOID *buf) { return bio->ReadBlocks(bio, bio->Media->MediaId, lba, bytes, buf); }
EFI_STATUS read_block(UINT32 blk, VOID *buf) { return read_sectors(part_start + (UINT64)blk * block_size / 512, block_size, buf); }

EFI_STATUS ext4_init(EFI_BLOCK_IO *b, UINT64 pstart) {
    bio = b; part_start = pstart;
    UINT8 sb[1024]; EFI_STATUS s = read_sectors(pstart + 2, 1024, sb);
    if(EFI_ERROR(s)) return s;
    if(*(UINT16*)(sb + 0x38) != 0xEF53) return EFI_UNSUPPORTED;
    block_size = 1024 << *(UINT32*)(sb + 0x18);
    inodes_per_group = *(UINT32*)(sb + 0x28);
    inode_size = *(UINT16*)(sb + 0x58); if(!inode_size) inode_size = 128;
    UINT32 inodes_count = *(UINT32*)sb;
    UINTN gdt_bytes = ((inodes_count / inodes_per_group + 1) * 32 + block_size - 1) / block_size * block_size;
    s = BS->AllocatePool(EfiLoaderData, gdt_bytes, &gdt); if(EFI_ERROR(s)) return s;
    for(UINTN i=0; i<gdt_bytes/block_size; i++) { s=read_block(1+i, (UINT8*)gdt+i*block_size); if(EFI_ERROR(s)){BS->FreePool(gdt); return s;} }
    return EFI_SUCCESS;
}
static UINT32 get_itable(UINT32 group) { return *(UINT32*)((UINT8*)gdt + group*32 + 0x08); }

EFI_STATUS read_inode(UINT32 ino, VOID *inode_buf) {
    UINT32 grp=(ino-1)/inodes_per_group, idx=(ino-1)%inodes_per_group;
    UINT32 itable_blk = get_itable(grp);
    UINT32 off = (idx*inode_size) % block_size;
    UINT32 target = itable_blk + (idx*inode_size)/block_size;
    UINT8 *buf; EFI_STATUS s = BS->AllocatePool(EfiLoaderData, block_size, (VOID**)&buf);
    if(EFI_ERROR(s)) return s; s = read_block(target, buf);
    if(EFI_ERROR(s)){BS->FreePool(buf); return s;}
    my_memcpy(inode_buf, buf+off, inode_size); BS->FreePool(buf); return EFI_SUCCESS;
}

typedef struct { UINT32 inode; CHAR8 name[256]; UINT8 type; } DirEntry;

/* 前向声明: read_inode_data 定义在 list_directory 之后 */
static EFI_STATUS read_inode_data(const UINT8 *inode, UINT8 *buf);

EFI_STATUS list_directory(UINT32 dir_ino, DirEntry **entries, UINTN *count) {
    UINT8 inode[256]; EFI_STATUS s = read_inode(dir_ino, inode); if(EFI_ERROR(s)) return s;
    UINT32 fsize = *(UINT32*)(inode+4);
    UINT8 *dir_buf; s = BS->AllocatePool(EfiLoaderData, fsize, (VOID**)&dir_buf); if(EFI_ERROR(s)) return s;
    read_inode_data(inode, dir_buf);
    UINTN cnt=0; UINT8 *ptr=dir_buf, *end=dir_buf+fsize;
    while(ptr<end) { if(*(UINT32*)ptr) cnt++; ptr += *(UINT16*)(ptr+4); }
    if(!cnt) { BS->FreePool(dir_buf); *entries=NULL; *count=0; return EFI_SUCCESS; }
    DirEntry *e; s = BS->AllocatePool(EfiLoaderData, cnt*sizeof(DirEntry), (VOID**)&e);
    if(EFI_ERROR(s)){BS->FreePool(dir_buf); return s;}
    ptr=dir_buf; UINTN idx=0;
    while(ptr<end) {
        UINT32 ino = *(UINT32*)ptr; UINT16 rec = *(UINT16*)(ptr+4);
        UINT8 nlen = *(UINT8*)(ptr+6); UINT8 ftype = *(UINT8*)(ptr+7);
        if(ino) { e[idx].inode=ino; my_memcpy(e[idx].name, ptr+8, nlen); e[idx].name[nlen]=0; e[idx].type=ftype; idx++; }
        ptr += rec;
    }
    BS->FreePool(dir_buf); *entries=e; *count=cnt; return EFI_SUCCESS;
}

EFI_STATUS find_in_dir(UINT32 dir_ino, const CHAR8 *name, UINT32 *ino_out) {
    DirEntry *e; UINTN cnt; EFI_STATUS s = list_directory(dir_ino, &e, &cnt);
    if(EFI_ERROR(s)) return s;
    for(UINTN i=0; i<cnt; i++) if(my_memcmp(e[i].name, name, my_strlen(name)+1)==0) { *ino_out = e[i].inode; BS->FreePool(e); return EFI_SUCCESS; }
    BS->FreePool(e); return EFI_NOT_FOUND;
}

typedef struct { UINT8 id[16]; UINT16 type, machine; UINT32 ver; UINT64 entry, phoff, shoff; UINT32 flags; UINT16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } Elf64_Ehdr;
typedef struct { UINT32 type, flags; UINT64 offset, vaddr, paddr, filesz, memsz, align; } Elf64_Phdr;
#define PT_LOAD 1
/* [修复] 完整读取 inode 指向的文件数据到 buf。
 * 原 bootloader 仅读取 i_block[0..11] 直接块 (4K 块时仅 48KB), kernel.elf
 * 超出后读取的是间接指针/磁盘垃圾, 导致加载的内核高位代码 (含 kmain) 残缺,
 * 跳转到 entry(kmain) 时执行垃圾指令 -> #UD/#PF 崩溃。
 * 本函数按逻辑块号正确映射物理块, 支持:
 *   - ext4 extent (depth 0 内联 / depth 1 索引块), 覆盖默认 ext4
 *   - ext2 风格 直接块 + 一级/二级间接块, 覆盖 ^extent 文件系统
 * 三级间接省略 (内核 < 4MB 用不到)。 */
static EFI_STATUS read_inode_data(const UINT8 *inode, UINT8 *buf) {
    UINT32 fsize = *(UINT32*)(inode + 4);
    UINT32 lblks = (fsize + block_size - 1) / block_size;
    const UINT32 *ib = (const UINT32*)(inode + 40);
    UINT32 ptrs = block_size / 4;

    /* ext4 extent: i_block 起始 2 字节为 magic 0xF30A */
    if ((ib[0] & 0xFFFF) == 0xF30A) {
        UINT16 entries = (UINT16)(ib[0] >> 16);   /* eh_entries @+2 */
        UINT16 depth   = (UINT16)(ib[1] >> 16);   /* eh_depth   @+6 */
        UINT8 *node = NULL;
        BS->AllocatePool(EfiLoaderData, block_size, (VOID**)&node);
        const UINT8 *root = inode + 52;           /* 根 extent 项起始于 i_block+12 */
        for (UINT16 e = 0; e < entries; e++) {
            const UINT8 *item = root + (UINTN)e * 12;
            if (depth == 0) {
                UINT32 lblk0 = *(UINT32*)(item + 0);
                UINT16 len   = *(UINT16*)(item + 4);
                UINT16 hi    = *(UINT16*)(item + 6);
                UINT32 lo    = *(UINT32*)(item + 8);
                UINT64 phys  = ((UINT64)hi << 32) | lo;
                for (UINT16 k = 0; k < len; k++) {
                    UINT32 l = lblk0 + k; if (l >= lblks) break;
                    read_block((UINT32)(phys + k), buf + (UINTN)l * block_size);
                }
            } else {
                /* depth>=1: 索引项 -> 叶节点 extent 块 */
                UINT32 leaf_phys = *(UINT32*)(item + 4);   /* ei_leaf_lo @+4 */
                if (EFI_ERROR(read_block(leaf_phys, node))) continue;
                UINT16 leaf_entries = *(UINT16*)(node + 2);
                const UINT8 *le = node + 12;
                for (UINT16 le_n = 0; le_n < leaf_entries; le_n++) {
                    const UINT8 *lx = le + (UINTN)le_n * 12;
                    UINT32 lblk0 = *(UINT32*)(lx + 0);
                    UINT16 len   = *(UINT16*)(lx + 4);
                    UINT16 hi    = *(UINT16*)(lx + 6);
                    UINT32 lo    = *(UINT32*)(lx + 8);
                    UINT64 phys  = ((UINT64)hi << 32) | lo;
                    for (UINT16 k = 0; k < len; k++) {
                        UINT32 l = lblk0 + k; if (l >= lblks) break;
                        read_block((UINT32)(phys + k), buf + (UINTN)l * block_size);
                    }
                }
            }
        }
        BS->FreePool(node);
        return EFI_SUCCESS;
    }

    /* ext2 风格: 直接块[0..11] + 一级间接[12] + 二级间接[13] */
    UINT8 *ind = NULL, *dind = NULL;
    BS->AllocatePool(EfiLoaderData, block_size, (VOID**)&ind);
    BS->AllocatePool(EfiLoaderData, block_size, (VOID**)&dind);
    UINT32 last_ind = 0xFFFFFFFF, last_dind = 0xFFFFFFFF;
    for (UINT32 l = 0; l < lblks; l++) {
        UINT32 phys = 0;
        if (l < 12) {
            phys = ib[l];
        } else if (l < 12 + ptrs) {
            if (last_ind != ib[12]) { read_block(ib[12], ind); last_ind = ib[12]; }
            phys = ((UINT32*)ind)[l - 12];
        } else {
            UINT32 off = l - 12 - ptrs;
            UINT32 i1 = off / ptrs, i2 = off % ptrs;
            if (last_dind != ib[13]) { read_block(ib[13], dind); last_dind = ib[13]; }
            UINT32 ind_phys = ((UINT32*)dind)[i1];
            if (ind_phys) { read_block(ind_phys, ind); phys = ((UINT32*)ind)[i2]; }
        }
        if (phys) read_block(phys, buf + (UINTN)l * block_size);
    }
    BS->FreePool(ind); BS->FreePool(dind);
    return EFI_SUCCESS;
}

EFI_STATUS load_elf(UINT8 *data, UINT64 *entry, UINT64 *stack) {
    Elf64_Ehdr *eh = (Elf64_Ehdr*)data;
    if(my_memcmp(eh->id, "\x7F""ELF", 4)) return EFI_UNSUPPORTED;
    Elf64_Phdr *ph = (Elf64_Phdr*)(data + eh->phoff);

    /* [诊断] 打印 ELF 头信息, 确认 entry 和加载地址 */
    print(L"ELF: entry=0x"); print_hex(eh->entry);
    print(L" phnum="); print_hex(eh->phnum); print(L"\r\n");

    /* [修复] 内核用 -mcmodel=large -fno-pic 编译, 绝对地址在链接时固化,
     * 不可重定位。必须在 ELF 指定的 paddr 加载, 否则跳转到 entry 时
     * 该地址没有代码 -> 执行垃圾 -> #PF。
     * 旧代码在 AllocateAddress(paddr) 失败时静默 fallback 到 AllocateAnyPages
     * (随机地址), 但 entry 仍指向链接地址 -> 必崩。
     * 现在改为: 必须在 paddr 加载; 失败则打印诊断信息并返回错误,
     * 让用户知道 link.ld 的链接地址 UEFI 无法分配。
     * (paddr=0 的 ELF 视为可任意放置, 用 AllocateAnyPages) */
    for(UINTN i=0; i<eh->phnum; i++) {
        if(ph[i].type != PT_LOAD) continue;
        UINTN memsz = (UINTN)ph[i].memsz, filesz = (UINTN)ph[i].filesz;
        if(memsz == 0) continue;
        UINTN pages = (memsz+0xFFF)>>12;
        EFI_PHYSICAL_ADDRESS addr = ph[i].paddr;
        EFI_STATUS st;

        print(L"ELF PT_LOAD["); print_hex(i); print(L"] paddr=0x");
        print_hex(ph[i].paddr); print(L" memsz=0x"); print_hex(memsz); print(L"\r\n");

        if(ph[i].paddr == 0) {
            st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
        } else {
            st = BS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &addr);
            if(EFI_ERROR(st)) {
                print(L"ELF: cannot load at link addr 0x");
                print_hex(ph[i].paddr);
                print(L"\r\nAdjust kernel/link.ld to a UEFI-allocatable low addr (e.g. 0x100000)\r\n");
                return st;
            }
        }
        BS->SetMem((VOID*)addr, memsz, 0);
        my_memcpy((VOID*)addr, data+ph[i].offset, filesz);
    }
    *entry = eh->entry; EFI_PHYSICAL_ADDRESS stk;
    EFI_STATUS st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 2, &stk); if(EFI_ERROR(st)) return st;
    *stack = stk + 0x2000; return EFI_SUCCESS;
}

/* 搜索 GPT 表中 type == linuxGuid 的分区, 返回其起始 LBA; 未找到返回 0 */
static UINT64 scan_gpt_for_ext4(EFI_BLOCK_IO *b, UINT64 *start_out) {
    UINT8 gpt[512];
    EFI_STATUS s = b->ReadBlocks(b, b->Media->MediaId, 1, 512, gpt);
    if(EFI_ERROR(s) || my_memcmp(gpt, "EFI PART", 8)) return 0;
    UINT32 esz = *(UINT32*)(gpt+0x54); UINT64 elba = *(UINT64*)(gpt+0x48);
    if(esz == 0 || esz > 1024) return 0;
    UINTN tableBytes = 128 * esz;
    UINT8 *ents; s = BS->AllocatePool(EfiLoaderData, tableBytes, (VOID**)&ents);
    if(EFI_ERROR(s)) return 0;
    s = b->ReadBlocks(b, b->Media->MediaId, elba, tableBytes, ents);
    if(EFI_ERROR(s)) { BS->FreePool(ents); return 0; }
    EFI_GUID linuxGuid = {0x0FC63DAF,0x8483,0x4772,{0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4}};
    UINT64 start = 0;
    for(UINTN j=0; j<128; j++) {
        EFI_GUID *type = (EFI_GUID*)(ents + j*esz);
        if(type->Data1==0) continue;
        if(my_memcmp(type, &linuxGuid, sizeof(EFI_GUID))==0) {
            start = *(UINT64*)(ents + j*esz + 32);  /* 分区起始 LBA */
            break;
        }
    }
    BS->FreePool(ents);
    if(start_out) *start_out = start;
    return start;
}

/* 通过 Device Path Protocol 获取分区的整盘绝对起始 LBA。
 * gnu-efi 的 EFI_BLOCK_IO_MEDIA 没有 LowestLBA 字段 (那是 EDK2 专有),
 * 所以读 HARDDRIVE_DEVICE_PATH 节点的 PartitionStart 字段 (UEFI 规范标准)。
 * 返回 0 表示无法获取 (调用方需 fallback 到 GPT 扫描)。 */
/* UEFI Device Path 常量 (规范定义, 不依赖 gnu-efi 头文件命名) */
#define DP_TYPE_MEDIA       0x04
#define DP_SUBTYPE_HARDDRIVE 0x01
#define DP_TYPE_END         0x7F
typedef struct { UINT8 Type; UINT8 SubType; UINT8 Length[2]; } DP_NODE;
typedef struct { DP_NODE Header; UINT32 PartitionNumber; UINT64 PartitionStart, PartitionSize; UINT8 Signature[16]; UINT8 MBRType, SignatureType; } DP_HARDDRIVE;

static UINT64 get_partition_abs_lba(EFI_HANDLE h) {
    EFI_GUID dpGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    DP_NODE *dp;
    if(EFI_ERROR(BS->HandleProtocol(h, &dpGuid, (VOID**)&dp)) || !dp) return 0;
    while(!(dp->Type == DP_TYPE_END && dp->SubType == 0xFF)) {
        if(dp->Type == DP_TYPE_MEDIA && dp->SubType == DP_SUBTYPE_HARDDRIVE) {
            return ((DP_HARDDRIVE*)dp)->PartitionStart;
        }
        UINT16 len = dp->Length[0] | (dp->Length[1] << 8);
        if(len < 4) break;  /* 防止损坏的 device path 死循环 */
        dp = (DP_NODE*)((UINT8*)dp + len);
    }
    return 0;
}

/* 测试一个 BlockIo 是否为 ext4 分区 (或包含 ext4 分区)。
 * - 分区句柄 (LogicalPartition=TRUE): ReadBlocks 用分区相对 LBA,
 *   part_start_out=0 (ext4_init 内部用相对 LBA), abs_lba_out 从 DevicePath 取
 * - 整盘句柄 (LogicalPartition=FALSE): 扫 GPT 找 Linux 分区,
 *   part_start_out=abs_lba_out=GPT 分区起始绝对 LBA
 * 返回 1=找到 ext4, 0=没找到 */
static int try_blockio_for_ext4(EFI_BLOCK_IO *b, EFI_HANDLE h,
                                 UINT64 *part_start_out, UINT64 *abs_lba_out) {
    if(!b || !b->Media) return 0;
    if(b->Media->LogicalPartition) {
        UINT8 sb[1024];
        if(EFI_ERROR(b->ReadBlocks(b, b->Media->MediaId, 2, 1024, sb))) return 0;
        if(*(UINT16*)(sb + 0x38) != 0xEF53) return 0;
        /* ext4 分区: ext4_init 用分区相对 LBA (part_start=0);
         * 内核需要整盘绝对 LBA, 从 DevicePath 取 */
        *part_start_out = 0;
        *abs_lba_out = get_partition_abs_lba(h);
        return 1;
    } else {
        UINT64 s = scan_gpt_for_ext4(b, NULL);
        if(!s) return 0;
        *part_start_out = s;
        *abs_lba_out = s;
        return 1;
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE *sys) {
    BS = sys->BootServices; RT = sys->RuntimeServices; ST = sys;
    print(L"\r\nEFMOS Boot\r\n");

    /* ---------- Step 1: 启动图形界面, 画 LOGO + 圆形 spinner ---------- */
    boot_init_gfx();
    boot_fill(0x000000);   /* 全黑背景 */
    boot_draw_logo();      /* 顶部 EFMOS 渐变大字 */
    boot_progress(5, L"Initializing boot services");

    EFI_STATUS status;
    EFI_BLOCK_IO *disk = NULL;
    UINT64 ext4_start = 0;
    UINT64 ext4_abs_lba = 0;

    /* ---------- Step 2: 查找 ext4 磁盘/分区 ---------- */
    boot_progress(15, L"Probing storage devices");
    EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID *probe_guids[3] = { &gEfiBlockIoProtocolGuid, &fsGuid, NULL };
    int probe_all[3] = { 0, 0, 1 };

    for(int strat = 0; strat < 3 && !disk; strat++) {
        UINTN hcnt = 0; EFI_HANDLE *handles = NULL;
        if(probe_all[strat])
            status = BS->LocateHandleBuffer(AllHandles, NULL, NULL, &hcnt, &handles);
        else
            status = BS->LocateHandleBuffer(ByProtocol, probe_guids[strat], NULL, &hcnt, &handles);
        if(EFI_ERROR(status) || !handles || !hcnt) continue;

        for(UINTN i=0; i<hcnt && !disk; i++) {
            EFI_BLOCK_IO *b = NULL;
            status = BS->HandleProtocol(handles[i], &gEfiBlockIoProtocolGuid, (VOID**)&b);
            if(EFI_ERROR(status) || !b) continue;
            UINT64 ps=0, al=0;
            if(try_blockio_for_ext4(b, handles[i], &ps, &al)) {
                disk = b; ext4_start = ps; ext4_abs_lba = al;
            }
        }
        BS->FreePool(handles);
    }
    if(!disk) {
        boot_progress(100, L"ERROR: No ext4 disk found");
        print(L"No disk found (no ext4 partition)\r\n");
        return EFI_NOT_FOUND;
    }

    /* ---------- Step 3: ext4 初始化 ---------- */
    boot_progress(30, L"Mounting ext4 filesystem");
    if(EFI_ERROR(ext4_init(disk, ext4_start))) {
        boot_progress(100, L"ERROR: ext4 init fail");
        print(L"ext4 init fail\r\n");
        return EFI_UNSUPPORTED;
    }

    /* ---------- Step 4: 定位 /EFMOS/kernel.elf ---------- */
    boot_progress(45, L"Locating kernel image");
    UINT32 efinode=0, kerninode=0;
    status = find_in_dir(2, "EFMOS", &efinode);
    if(EFI_ERROR(status)) {
        boot_progress(100, L"ERROR: No /EFMOS directory");
        print(L"No /EFMOS\r\n"); return EFI_NOT_FOUND;
    }
    status = find_in_dir(efinode, "kernel.elf", &kerninode);
    if(EFI_ERROR(status)) {
        boot_progress(100, L"ERROR: kernel.elf missing");
        print(L"No kernel.elf\r\n"); return EFI_NOT_FOUND;
    }

    /* ---------- Step 5: 读取内核文件并解析 ELF ---------- */
    boot_progress(60, L"Loading kernel.elf");
    UINT8 kern_inode[256]; read_inode(kerninode, kern_inode);
    UINT32 fsize = *(UINT32*)(kern_inode+4);
    UINT8 *kern_buf; BS->AllocatePool(EfiLoaderData, fsize, (VOID**)&kern_buf);
    read_inode_data(kern_inode, kern_buf);
    boot_progress(75, L"Linking ELF segments");
    UINT64 entry, stack; status = load_elf(kern_buf, &entry, &stack);
    BS->FreePool(kern_buf);
    if(EFI_ERROR(status)) {
        boot_progress(100, L"ERROR: ELF load failed");
        print(L"ELF fail\r\n"); return status;
    }

    /* ---------- Step 6: 传递参数给内核 + 设置 FB 信息 ---------- */
    boot_progress(88, L"Preparing kernel handoff");
    *(UINT64*)0x1500 = ext4_abs_lba;
    *(UINT32*)0x1508 = block_size;

    struct { UINT64 fb; UINT32 hr, vr, ppsl; } *gop = (void*)0x1000;
    if (s_gop && s_gop->Mode) {
        gop->fb   = s_gop->Mode->FrameBufferBase;
        gop->hr   = s_gop->Mode->Info->HorizontalResolution;
        gop->vr   = s_gop->Mode->Info->VerticalResolution;
        gop->ppsl = s_gop->Mode->Info->PixelsPerScanLine;
    } else {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP = NULL;
        status = BS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&GOP);
        if(!EFI_ERROR(status) && GOP && GOP->Mode) {
            gop->fb=GOP->Mode->FrameBufferBase;
            gop->hr=GOP->Mode->Info->HorizontalResolution;
            gop->vr=GOP->Mode->Info->VerticalResolution;
            gop->ppsl=GOP->Mode->Info->PixelsPerScanLine;
        } else gop->fb=0;
    }

    /* ---------- Step 7: ExitBootServices 并跳入内核 ---------- */
    boot_progress(98, L"Exiting UEFI boot services");
    print(L"Exiting boot services...\r\n");

    /* [修复 · 首次启动卡死 · ExitBootServices 竞态]
     *
     * UEFI 规范严格要求: 传给 ExitBootServices 的 mapKey 必须是最后一次调用
     * GetMemoryMap 返回的那个; 且两次 GetMemoryMap / ExitBootServices 之间
     * 绝不能有任何分配 (AllocatePool/FreePool/LoadImage)。
     * 旧代码: GetMemoryMap(NULL) → AllocatePool → GetMemoryMap(mmap) → 多次
     *   AllocatePool(RSDP计算等) → ExitBootServices(mapKey 已过期) → 返回
     *   EFI_INVALID_PARAMETER 但代码不检查 → UEFI 未切到 Runtime, 跳到内核后
     *   仍有 BS 定时器/SMI 后台运行, 踩内核页表/GDT → "第一次bootloader有概率卡死
     *   (只能重启)"。概率出现是因为 OVMF/真机在 RAM 计算时是否恰好产生新内存描述符。
     *
     * 修复:
     *   ① 先完成 RAM 总量 / ACPI RSDP 计算 (可随意 AllocatePool);
     *   ② 然后才进入 retry 循环: GetMemoryMap → ExitBootServices, 失败则重来;
     *   ③ retry 循环内不调任何 BS 分配/协议 API, 确保 mapKey 始终新鲜。 */
    {
        UINT64 total_ram = 0;
        UINT64 rsdp_addr  = 0;
        {
            /* --- RAM 总量 (临时 mmap, 允许分配) --- */
            UINTN ms = 0, mk, ds; UINT32 dv;
            EFI_STATUS s1 = BS->GetMemoryMap(&ms, NULL, &mk, &ds, &dv);
            if (s1 == EFI_BUFFER_TOO_SMALL) {
                VOID *mm; BS->AllocatePool(EfiLoaderData, ms + 2*ds, &mm);
                UINTN msz = ms + 2*ds;
                if (!EFI_ERROR(BS->GetMemoryMap(&msz, mm, &mk, &ds, &dv))) {
                    UINT8 *p = (UINT8*)mm;
                    UINTN entries = msz / ds;
                    for (UINTN i = 0; i < entries; i++) {
                        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)(p + i*ds);
                        if (d->Type <= 10) total_ram += d->NumberOfPages * 4096ULL;
                    }
                }
                BS->FreePool(mm);
            }
            *(UINT64*)0x1510 = total_ram;

            /* --- ACPI RSDP (只读系统配置表, 不分配) --- */
            UINT8 acpi20_guid[16] = {
                0x71,0xE8,0x68,0x88, 0xF1,0xE4, 0xD3,0x11,
                0xBC,0x22, 0x00,0x80,0xC7,0x3C,0x88,0x81
            };
            for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
                if (my_memcmp(&ST->ConfigurationTable[i].VendorGuid, acpi20_guid, 16) == 0) {
                    rsdp_addr = (UINT64)(UINTN)ST->ConfigurationTable[i].VendorTable;
                    break;
                }
            }
            *(UINT64*)0x1518 = rsdp_addr;
        }
        /* --- 最后一步: 进度到 100% + spinner (在 循环外, 避免循环内重复刷屏) --- */
        boot_progress(100, L"Boot complete, entering kernel");
        boot_spinner(1500);

        /* --- [Final Path] GetMemoryMap → ExitBootServices 重试循环 ---
         * 规则: 循环内 **绝对不能** 调任何会改变内存布局的 BS API。
         *  ① 先在循环外预分配一个"足够大"的 mmap buffer (比初始 2*descSize 再多加 16KB,
         *     覆盖 OVMF/真机 GetMemoryMap 两次返回尺寸差异的极端情况)。
         *  ② 每次重试时, 用同一个 pre-alloc buffer, 不再调 AllocatePool/FreePool。
         *  ③ 若 ExitBootServices 返回 EFI_INVALID_PARAMETER (mapKey 过期), 用
         *     BS->GetMemoryMap(NULL) 获得新尺寸+新 mapKey, 再用预分配 buffer 重取。
         * 最大重试 64 次。超过则打印明确错误并停, 不再静默跳进内核。 */
        UINTN fMapSize0 = 0, fMapKey0 = 0, fDescSize0 = 0;
        UINT32 fDescVer0 = 0;
        EFI_STATUS sp = BS->GetMemoryMap(&fMapSize0, NULL, &fMapKey0, &fDescSize0, &fDescVer0);
        (void)sp;
        /* 预分配比第一次所需大 16KB + 32 个描述符, 避免后续 GetMemoryMap 超出 (此分配
         * 发生在 "最后一次 GetMemoryMap" 之前, 符合规范)。 */
        UINTN fAlloc = fMapSize0 + 32*fDescSize0 + 16384;
        VOID *fMmap = NULL;
        BS->AllocatePool(EfiLoaderData, fAlloc, &fMmap);

        EFI_STATUS ebs = EFI_LOAD_ERROR;
        for (int retries = 0; retries < 64; retries++) {
            UINTN real_sz = fAlloc;
            UINTN mk = 0, ds = 0; UINT32 dv = 0;
            /* --- 取最新 mmap + mapKey --- */
            EFI_STATUS s2 = BS->GetMemoryMap(&real_sz, fMmap, &mk, &ds, &dv);
            if (EFI_ERROR(s2)) continue;
            /* --- 立刻用同一个 mapKey 调 ExitBootServices (中间没有任何分配) --- */
            ebs = BS->ExitBootServices(img, mk);
            if (!EFI_ERROR(ebs)) break;
        }
        if (EFI_ERROR(ebs)) {
            /* 无法退出 BS: 打印明确错误, 停止 — 总比静默跳进内核后卡死好 */
            print(L"ExitBootServices FAILED after retries; halting.\r\n");
            for(;;) BS->Stall(1000000);
        }
    }

    asm volatile("mov %0, %%rsp; push $0; jmp *%1" : : "r"(stack), "r"(entry) : "memory");
    return EFI_SUCCESS;
}
