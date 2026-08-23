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
