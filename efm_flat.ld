/* efm_flat.ld - flat binary 链接脚本 (.efs / .drv 通用)
 *
 * [入口点修复] 内核加载 .efs/.drv 后直接跳转到 load_addr 执行, 因此入口函数
 * (_start / drv_entry) 必须位于 .text 段最前面 (即 load_addr 处).
 *
 * 默认 ld 脚本里 *(.text .text.*) 会把普通 .text (静态函数) 排在 .text.start
 * 之前, 导致入口函数不在 load_addr → 内核跳到错误地址立即崩溃.
 *
 * 本脚本显式把 .text.start 放在最前, 确保入口函数位于 load_addr.
 * 地址由 -Ttext=0xXXXXXX 指定 (不同目标不同), 脚本本身不硬编码地址.
 * [BSS 符号] 定义 __bss_start / _end, 供 _start 内联汇编清零 BSS。
 *   否则 stripped.o 里 "lea __bss_start(%rip)" 会 ld undefined reference。 */

ENTRY(_start)

SECTIONS {
    .text : {
        /* 入口函数优先 (compositor/loader 的 _start, 驱动的 drv_entry) */
        KEEP(*(.text.start))
        /* 其余代码 */
        *(.text.unlikely .text.unlikely.*)
        *(.text.startup .text.startup.*)
        *(.text.hot .text.hot.*)
        *(.text .text.* .gnu.linkonce.t.*)
        /* 只读数据 + 数据 (flat binary, 全部连续) */
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        *(.data .data.* .gnu.linkonce.d.*)
    }
    __bss_start = .;
    .bss : {
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
    }
    _end = .;
    /DISCARD/ : {
        *(.note.*) *(.comment) *(.eh_frame*) *(.eh_frame_hdr)
    }
}
