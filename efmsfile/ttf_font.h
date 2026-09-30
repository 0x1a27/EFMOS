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

/* ttf_font.h - TTF 字体加载器与栅格化器 (Sarasa Gothic 等 TrueType 字体)
 *
 * 集成方式: 在 graphics_drv.c 中 #include "ttf_font.c"
 * 内存分配: 通过 ttf_init() 注入 kmalloc/kfree/file_read 回调
 *
 * 功能:
 *   - 解析 TTF 文件 (cmap/glyf/loca/head/maxp/hmtx/hhea 表)
 *   - Unicode 码点到字形索引映射 (cmap format 4 + 12)
 *   - 字形轮廓提取 (简单字形 + 复合字形)
 *   - 二次贝塞尔曲线展平
 *   - 扫描线栅格化 (2x2 超采样抗锯齿)
 *   - 字形位图缓存 (512 条目, LRU)
 */
#ifndef TTF_FONT_H
#define TTF_FONT_H

/* 字形位图: 灰度图, 每像素 0-255 */
struct ttf_bitmap {
    int width;
    int height;
    int advance;    /* 推进宽度 (像素) */
    int bearing_x;  /* 左侧间距 (像素) */
    int bearing_y;  /* 顶部间距 (像素, 从基线到顶部) */
    unsigned char *data;  /* width * height 字节, 灰度 0-255 */
};

/* TTF 字体上下文 (不透明指针) */
struct ttf_font;

/* 初始化: 注入内存分配和文件读取回调 (由 graphics_drv.c 调用) */
void ttf_set_alloc(void *(*malloc_fn)(unsigned long),
                   void  (*free_fn)(void *),
                   int   (*read_fn)(const char *, char *, int),
                   void  (*log_fn)(const char *));

/* 从文件加载 TTF 字体. 返回 NULL = 失败 */
struct ttf_font *ttf_load(const char *path);

/* 获取字符的栅格化位图 (带缓存). pixel_size = 目标像素高度 */
struct ttf_bitmap *ttf_get_bitmap(struct ttf_font *font,
                                  unsigned int codepoint, int pixel_size);

/* 获取字体的 ascent (像素), 用于基线定位. pixel_size = 目标像素高度 */
int ttf_get_ascent_px(struct ttf_font *font, int pixel_size);

/* 获取字符的推进宽度 (像素). 用于等宽字体校准 cell 宽度 */
int ttf_get_advance_px(struct ttf_font *font, unsigned int codepoint, int pixel_size);

/* 释放字体 */
void ttf_free(struct ttf_font *font);

#endif /* TTF_FONT_H */
