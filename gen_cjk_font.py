#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# EFMOS - a 64-bit x86_64 UEFI operating system written in C.
#
# Copyright (C) 2026 0x1a27
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Generate 16x16 bitmap font data for Chinese UI characters.

优先使用 PIL + simhei.ttf 实时生成位图字体。
若 PIL 不可用或字体缺失, fallback 到预存的 cjk_font_preset.h
(包含 EFMOS UI 所需全部 339 个汉字, 由本脚本在 PIL 环境下预先生成)。
这样构建环境无需安装 Pillow/字体即可成功编译内核。
"""
import sys
import os

try:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
except NameError:
    # 当通过 exec(open(...).read()) 等方式运行时 __file__ 未定义
    SCRIPT_DIR = os.getcwd()
PRESET_PATH = os.path.join(SCRIPT_DIR, "cjk_font_preset.h")

# 常见 CJK 字体路径 (按优先级排序)。SimHei 是微软专有字体, Linux 上通常没有;
# fonts-wqy-zenhei / fonts-noto-cjk 是 Debian/Ubuntu 自带的开源 CJK 字体,
# 用 `sudo apt install fonts-wqy-zenhei` 或 `fonts-noto-cjk` 即可获得。
# /tmp/unifont.ttf: GNU Unifont (全 BMP 覆盖, 16px 位图风格, 适合 OS UI),
#   可由 build 环境预先放置 (例如从 fontsource woff 转换而来)。
CJK_FONT_CANDIDATES = [
    "/tmp/simhei.ttf",                                     # 用户手动放置
    "/tmp/unifont.ttf",                                    # GNU Unifont (全 BMP 覆盖)
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",        # fonts-wqy-zenhei
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",      # fonts-wqy-microhei
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",  # fonts-noto-cjk
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
    "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf",
    "/usr/share/fonts/truetype/arphic/uming.ttc",          # fonts-arphic-uming
    "/usr/share/fonts/truetype/arphic/ukai.ttc",           # fonts-arphic-ukai
]


def find_cjk_font():
    """返回第一个存在的 CJK 字体路径, 都没有则返回 None"""
    for p in CJK_FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    return None


def generate_with_pil():
    """用 PIL + 系统字体实时生成 (需要 python3-pil 和任一 CJK 字体包)"""
    from PIL import Image, ImageDraw, ImageFont

    # 扫描所有 EFMOS 源码文件, 收集实际使用的非 ASCII 字符
    # (覆盖内核 + 所有 .efs 程序的 print_utf8 字符串), 确保字体
    # 覆盖每一个可能显示的字符 (如 "登录" 的 "登" 等)。
    SOURCE_FILES = [
        "kernel/kernel.c",
        "efmsfile/userman.c",
        "efmsfile/efmlogin.c",
        "efmsfile/setting.c",
        "efmsfile/fileman.c",
        "efmsfile/efmshell.c",
        "efmsfile/efmAether.c",
        "efmsfile/efmcompositor.c",
    ]
    chars = set()
    for sf in SOURCE_FILES:
        path = os.path.join(SCRIPT_DIR, sf)
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as fh:
                for ch in fh.read():
                    if ord(ch) > 127:
                        chars.add(ch)
        except FileNotFoundError:
            sys.stderr.write("WARNING: 源文件不存在, 跳过: " + path + "\n")

    chars = sorted(chars)
    print(f"Total unique CJK characters: {len(chars)}", file=sys.stderr)

    # Load font: 优先用户指定的 /tmp/simhei.ttf, 否则搜索系统已安装的 CJK 字体
    font_path = find_cjk_font()
    if font_path is None:
        raise RuntimeError(
            "未找到任何 CJK 字体。请执行: sudo apt install fonts-wqy-zenhei  "
            "(或 fonts-noto-cjk), 或把 SimHei 字体放到 /tmp/simhei.ttf"
        )
    print(f"Using font: {font_path}", file=sys.stderr)
    font = ImageFont.truetype(font_path, 16)

    # Generate 16x16 bitmaps
    font_data = {}
    for ch in chars:
        img = Image.new('1', (16, 16), 0)
        draw = ImageDraw.Draw(img)
        bbox = draw.textbbox((0, 0), ch, font=font)
        w = bbox[2] - bbox[0]
        h = bbox[3] - bbox[1]
        x = (16 - w) // 2 - bbox[0]
        y = (16 - h) // 2 - bbox[1]
        draw.text((x, y), ch, fill=1, font=font)

        data = bytearray(32)
        for row in range(16):
            for col in range(16):
                if img.getpixel((col, row)):
                    data[row * 2 + (col // 8)] |= (0x80 >> (col % 8))
        font_data[ch] = bytes(data)

    # Output as C code
    print("/* Auto-generated 16x16 CJK bitmap font for EFMOS UI */")
    print("/* Each entry: UTF-8 (4 bytes padded) + 32 bytes bitmap = 36 bytes */")
    print("")
    print(f"#define CJK_FONT_COUNT {len(chars)}")
    print("")
    print("static const unsigned char cjk_font[][36] = {")

    for ch in chars:
        utf8 = ch.encode('utf-8')
        u = utf8 + b'\x00' * (4 - len(utf8))
        bm = font_data[ch]
        hex_bytes = ','.join(f'0x{b:02X}' for b in (u + bm))
        print(f"    {{{hex_bytes}}},  /* {ch} U+{ord(ch):04X} */")

    print("};")


def fallback_to_preset():
    """PIL 不可用时, 直接输出预存的字体数据文件内容"""
    if not os.path.exists(PRESET_PATH):
        sys.stderr.write(
            "ERROR: PIL 不可用且预存字体文件缺失: " + PRESET_PATH + "\n"
            "请执行以下任一方案:\n"
            "  1) pip3 install Pillow  并把 SimHei 字体放到 /tmp/simhei.ttf\n"
            "  2) 从其他机器拷贝 cjk_font_preset.h 到本目录\n"
        )
        sys.exit(1)
    sys.stderr.write("PIL 不可用, 使用预存字体数据: " + PRESET_PATH + "\n")
    with open(PRESET_PATH, "r", encoding="utf-8") as f:
        sys.stdout.write(f.read())


if __name__ == "__main__":
    try:
        generate_with_pil()
    except Exception as e:
        sys.stderr.write(f"PIL 生成失败 ({e}), 切换到预存数据\n")
        fallback_to_preset()
