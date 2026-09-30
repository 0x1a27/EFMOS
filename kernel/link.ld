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

/* kernel/link.ld - EFMOS 内核链接脚本
 *
 * [VMA 说明] 必须选 UEFI 可用的低物理地址 (1MB ~ 4GB 以下),
 * 且不能与 .efs 应用的加载地址冲突:
 *   - setting.efs  内核加载到 0x100000 (1MB)
 *   - fileman.efs  内核加载到 0x300000 (3MB)
 * 所以内核 VMA 选 0x200000 (2MB), 落在两者之间,
 * 2MB 也是大页对齐, 内核若启用大页映射正好合用。
 *
 * 关键: _start 必须是 .text 第一个符号 (bootloader 跳转到 entry = ehdr.e_entry,
 * 而 ehdr.e_entry 指向链接后的 VMA 起始处 _start)。 */

ENTRY(_start)

SECTIONS {
    . = 0x200000;           /* VMA = 2MB, UEFI 可用低地址, 避开 1MB/3MB 的 .efs 加载区 */

    /* .text 必须放最前: 保证 _start VMA = 0x200000 = entry point */
    .text ALIGN(4K) : {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : {
        *(.eh_frame .eh_frame_hdr)
        *(.comment .note.*)
        *(.rel .rela.*)
    }
}
