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

all: disk.img disk.vmdk

bootloader/BOOTX64.EFI: bootloader/bootloader.c bootloader/bootlogo.h
	x86_64-w64-mingw32-gcc -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone -mgeneral-regs-only -I/usr/include/efi -I/usr/include/efi/x86_64 -Ibootloader -c bootloader/bootloader.c -o bootloader/bootloader.o
	x86_64-w64-mingw32-gcc -nostdlib -Wl,--subsystem,10 -Wl,-e,efi_main -o bootloader/BOOTX64.EFI bootloader/bootloader.o -lmingw32

# kernel: 字符渲染统一使用 TTF (Graphics.drv), 不再依赖 cjk_font_data.h
# [修复] -mcmodel=kernel 假设代码/数据在 0xFFFFFFFF80000000+ (高半核),
#   但内核实际链接/加载在低地址 (0x100000)。kmain 一访问全局变量就
#   解引用高规范地址 -> #PF (CR2=0xFFFFFFFFxxxxxxxx)。
#   改用 -mcmodel=large (与 fileman.efs/setting.efs 一致), 用 64 位绝对寻址,
#   符号地址按真实低地址加载, 不再产生高地址访问。
kernel/kernel.elf: kernel/kernel.c kernel/link.ld drivers/drv_common.h bootloader/bootlogo.h
	gcc -ffreestanding -nostdinc -nostdlib -Ikernel -Idrivers -Ibootloader -mno-red-zone -mcmodel=large -fno-pic -c kernel/kernel.c -o kernel/kernel.o
	ld -nostdlib -T kernel/link.ld -o kernel/kernel.elf kernel/kernel.o

# [关键修复] 构建 .efs 文件头
# POSIX sh 的 printf 不保证支持 \xNN (bash 才支持; dash 只支持 \0NNN 八进制)。
# 用 /usr/bin/printf (如果是 bash-builtin) 配合 bash -c 最可靠, 或者直接
# 用 dd 构造 12 字节头。这里用 bash -c 保证 \xNN 解析为真实二进制字节。
# fileman.efs: 文件管理器, 加载地址 0x300000 (3MB)
# [efmsfile] userman/fileman/setting 源文件与产物统一放 efmsfile/ 目录
efmsfile/fileman.efs: efmsfile/fileman.c
	@mkdir -p efmsfile
	gcc -ffreestanding -nostdlib -fno-pic -no-pie -mno-red-zone -mcmodel=large -Wl,--build-id=none -Ttext=0x300000 -o efmsfile/fileman.elf efmsfile/fileman.c
	objcopy -O binary -j .text -j .rodata -j .data efmsfile/fileman.elf efmsfile/fileman.bin
	SIZE=$$(stat -c%s efmsfile/fileman.bin); \
	bash -c "printf '\x00\x00\x30\x00\x00\x00\x00\x00' > efmsfile/fileman.efs"; \
	if command -v xxd >/dev/null 2>&1; then \
	  printf '%08x' $$SIZE | xxd -r -p >> efmsfile/fileman.efs; \
	else \
	  b0=$$(( (SIZE >> 24) & 0xFF )); b1=$$(( (SIZE >> 16) & 0xFF )); \
	  b2=$$(( (SIZE >> 8)  & 0xFF )); b3=$$(( SIZE & 0xFF )); \
	  bash -c "printf '\x'$$(printf '%02x' $$b0)'\x'$$(printf '%02x' $$b1)'\x'$$(printf '%02x' $$b2)'\x'$$(printf '%02x' $$b3)" >> efmsfile/fileman.efs; \
	fi; \
	cat efmsfile/fileman.bin >> efmsfile/fileman.efs

# setting.efs: 设置程序, 加载地址 0x100000 (1MB, 不与内核 0x300000 冲突)
efmsfile/setting.efs: efmsfile/setting.c
	@mkdir -p efmsfile
	gcc -ffreestanding -nostdlib -fno-pic -no-pie -mno-red-zone -mcmodel=large -Wl,--build-id=none -Ttext=0x100000 -o efmsfile/setting.elf efmsfile/setting.c
	objcopy -O binary -j .text -j .rodata -j .data efmsfile/setting.elf efmsfile/setting.bin
	SIZE=$$(stat -c%s efmsfile/setting.bin); \
	bash -c "printf '\x00\x00\x10\x00\x00\x00\x00\x00' > efmsfile/setting.efs"; \
	if command -v xxd >/dev/null 2>&1; then \
	  printf '%08x' $$SIZE | xxd -r -p >> efmsfile/setting.efs; \
	else \
	  b0=$$(( (SIZE >> 24) & 0xFF )); b1=$$(( (SIZE >> 16) & 0xFF )); \
	  b2=$$(( (SIZE >> 8)  & 0xFF )); b3=$$(( SIZE & 0xFF )); \
	  bash -c "printf '\x'$$(printf '%02x' $$b0)'\x'$$(printf '%02x' $$b1)'\x'$$(printf '%02x' $$b2)'\x'$$(printf '%02x' $$b3)" >> efmsfile/setting.efs; \
	fi; \
	cat efmsfile/setting.bin >> efmsfile/setting.efs

# efmshell.efs: 独立 Shell (从 kernel shell_loop 迁出), 加载地址 0xA00000 (10MB)
# [efmsfile] efm* 源文件与产物统一放 efmsfile/ 目录
# [关键] 必须用 efm_flat.ld + section(".text.start") 保证 _start 在文件首,
#   普通 gcc -Ttext 链接按函数定义顺序排放, 入口错位 → 内核跳入执行到垃圾函数.
efmsfile/efmshell.efs: efmsfile/efmshell.c
	@mkdir -p efmsfile
	$(DRV_CC) -I. -o efmsfile/efmshell.o efmsfile/efmshell.c
	$(DRV_OBJCOPY_STRIP) efmsfile/efmshell.o efmsfile/efmshell.stripped.o
	$(DRV_LD_EFS) efmsfile/efmshell.elf -Ttext=0xA00000 efmsfile/efmshell.stripped.o
	objcopy -O binary -j .text -j .rodata -j .data efmsfile/efmshell.elf efmsfile/efmshell.bin
	$(call bin_package,efmsfile/efmshell,\x00\x00\xA0\x00\x00\x00\x00\x00,efs)

# userman.efs: 用户管理程序, 加载地址 0x500000 (5MB)
efmsfile/userman.efs: efmsfile/userman.c
	@mkdir -p efmsfile
	gcc -ffreestanding -nostdlib -fno-pic -no-pie -mno-red-zone -mcmodel=large -Wl,--build-id=none -Ttext=0x500000 -o efmsfile/userman.elf efmsfile/userman.c
	objcopy -O binary -j .text -j .rodata -j .data efmsfile/userman.elf efmsfile/userman.bin
	SIZE=$$(stat -c%s efmsfile/userman.bin); \
	bash -c "printf '\x00\x00\x50\x00\x00\x00\x00\x00' > efmsfile/userman.efs"; \
	if command -v xxd >/dev/null 2>&1; then \
	  printf '%08x' $$SIZE | xxd -r -p >> efmsfile/userman.efs; \
	else \
	  b0=$$(( (SIZE >> 24) & 0xFF )); b1=$$(( (SIZE >> 16) & 0xFF )); \
	  b2=$$(( (SIZE >> 8)  & 0xFF )); b3=$$(( SIZE & 0xFF )); \
	  bash -c "printf '\x'$$(printf '%02x' $$b0)'\x'$$(printf '%02x' $$b1)'\x'$$(printf '%02x' $$b2)'\x'$$(printf '%02x' $$b3)" >> efmsfile/userman.efs; \
	fi; \
	cat efmsfile/userman.bin >> efmsfile/userman.efs

# efmlogin.efs: 登录程序, 加载地址 0x600000 (6MB, 不与 userman 0x500000 冲突)
efmsfile/efmlogin.efs: efmsfile/efmlogin.c
	@mkdir -p efmsfile
	gcc -ffreestanding -nostdlib -fno-pic -no-pie -mno-red-zone -mcmodel=large -Wl,--build-id=none -Ttext=0x600000 -o efmsfile/efmlogin.elf efmsfile/efmlogin.c
	objcopy -O binary -j .text -j .rodata -j .data efmsfile/efmlogin.elf efmsfile/efmlogin.bin
	SIZE=$$(stat -c%s efmsfile/efmlogin.bin); \
	bash -c "printf '\x00\x00\x60\x00\x00\x00\x00\x00' > efmsfile/efmlogin.efs"; \
	if command -v xxd >/dev/null 2>&1; then \
	  printf '%08x' $$SIZE | xxd -r -p >> efmsfile/efmlogin.efs; \
	else \
	  b0=$$(( (SIZE >> 24) & 0xFF )); b1=$$(( (SIZE >> 16) & 0xFF )); \
	  b2=$$(( (SIZE >> 8)  & 0xFF )); b3=$$(( SIZE & 0xFF )); \
	  bash -c "printf '\x'$$(printf '%02x' $$b0)'\x'$$(printf '%02x' $$b1)'\x'$$(printf '%02x' $$b2)'\x'$$(printf '%02x' $$b3)" >> efmsfile/efmlogin.efs; \
	fi; \
	cat efmsfile/efmlogin.bin >> efmsfile/efmlogin.efs

# ========== 2026+: efmloader.efs + .drv 驱动 ==========
# 通用 .efs / .drv 构建方式完全相同 (flat-bin + 12 字节头),
# 仅加载地址不同: .efs /EFMOS 加载, .drv 由内核 load_driver 加载。
# 头 12 字节: [u32 load_addr (小端)] [u32 保留 0] [u32 bin_size (大端)]

# GCC 在高版本会默认生成 .note.gnu.property, 与 -Ttext 指定的 .text 起始地址冲突.
# 解决方案: 先 gcc -c 编译成 .o, 再 objcopy 从 .o 中删除不需要的 section, 最后 ld 链接.
# -O2 对驱动合适, 但 -fno-function-sections (不把函数拆到独立 section, 避免 GC 误删)
# 同时用 -fno-data-sections 保留全局变量
DRV_CC = gcc -c -ffreestanding -fno-pic -mno-red-zone -mcmodel=large \
            -fno-asynchronous-unwind-tables -fno-stack-protector \
            -fomit-frame-pointer -O2 -mno-sse -mno-sse2 -mno-mmx
DRV_OBJCOPY_STRIP = objcopy -R .note.gnu.property -R .note.ABI-tag \
                             -R .eh_frame -R .eh_frame_hdr \
                             -R .comment
# ld 链接参数:
#   --nmagic (-N): text/data 合并, text 可写 (flat-bin 友好)
#   --no-dynamic-linker: 禁用 .interp
#   --no-warn-rwx-segments: 忽略 RWX 段告警
#   -T efm_flat.ld: 自定义链接脚本, 把 .text.start (入口函数 _start/drv_entry)
#     放在 .text 最前, 确保入口位于 load_addr (内核直接跳到 load_addr 执行).
#     默认脚本 *(.text .text.*) 会把静态函数排在 .text.start 前, 导致入口错位崩溃.
#   不能用 --discard-all / --gc-sections: 驱动入口 drv_entry 通过 asm "call drv_main"
#   引用, 对 ld 来说是"不可见"引用会被 GC/discard 掉. 所以保留所有 symbol.
# efmloader.efs 入口 = _start; .drv 入口 = drv_entry (均标 section(".text.start"))
DRV_LD_EFS = ld --nmagic --no-dynamic-linker --no-warn-rwx-segments -T efm_flat.ld -e _start -o
DRV_LD_DRV = ld --nmagic --no-dynamic-linker --no-warn-rwx-segments -T efm_flat.ld -o

define bin_package =
	@SIZE=$$(stat -c%s $(1).bin); \
	bash -c "printf '$(2)' > $(1).tmp_head"; \
	if command -v xxd >/dev/null 2>&1; then \
	  printf '%08x' $$SIZE | xxd -r -p >> $(1).tmp_head; \
	else \
	  b0=$$(( (SIZE >> 24) & 0xFF )); b1=$$(( (SIZE >> 16) & 0xFF )); \
	  b2=$$(( (SIZE >> 8)  & 0xFF )); b3=$$(( SIZE & 0xFF )); \
	  bash -c "printf '\x'$$(printf '%02x' $$b0)'\x'$$(printf '%02x' $$b1)'\x'$$(printf '%02x' $$b2)'\x'$$(printf '%02x' $$b3)" >> $(1).tmp_head; \
	fi; \
	cat $(1).tmp_head $(1).bin > $(1).$(3); \
	rm -f $(1).tmp_head
endef

# efmloader.efs: 驱动管理器, 加载地址 0x400000 (4MB)
# 12字节头: load_addr=0x00400000 小端 => \x00\x00\x40\x00, 保留4字节0
efmsfile/efmloader.efs: efmsfile/efmloader.c drivers/drv_common.h
	@mkdir -p efmsfile
	$(DRV_CC) -I. -o efmsfile/efmloader.o efmsfile/efmloader.c
	$(DRV_OBJCOPY_STRIP) efmsfile/efmloader.o efmsfile/efmloader.stripped.o
	$(DRV_LD_EFS) efmsfile/efmloader.elf -Ttext=0x400000 efmsfile/efmloader.stripped.o
	objcopy -O binary -j .text -j .rodata -j .data efmsfile/efmloader.elf efmsfile/efmloader.bin
	$(call bin_package,efmsfile/efmloader,\x00\x00\x40\x00\x00\x00\x00\x00,efs)

# efmcompositor.efs: Mesa 合成器, 加载地址 0x800000 (8MB, 不与 efmlogin 0x600000 / gcc 0x700000 冲突)
# 登录后由内核 spawn_async 启动, 接管桌面渲染 (Mesa 渲染重构).
# 12字节头: load_addr=0x00800000 小端 => \x00\x00\x80\x00, 保留4字节0
efmsfile/efmcompositor.efs: efmsfile/efmcompositor.c bootloader/bootlogo.h
	@mkdir -p efmsfile
	$(DRV_CC) -Ibootloader -o efmsfile/efmcompositor.o efmsfile/efmcompositor.c
	$(DRV_OBJCOPY_STRIP) efmsfile/efmcompositor.o efmsfile/efmcompositor.stripped.o
	$(DRV_LD_EFS) efmsfile/efmcompositor.elf -Ttext=0x800000 efmsfile/efmcompositor.stripped.o
	objcopy -O binary -j .text -j .rodata -j .data efmsfile/efmcompositor.elf efmsfile/efmcompositor.bin
	$(call bin_package,efmsfile/efmcompositor,\x00\x00\x80\x00\x00\x00\x00\x00,efs)

# efmAether.efs: 桌面环境 (TTF 字体), 加载地址 0x900000 (9MB)
# 登录后由内核 spawn_async 启动, 接管桌面背景/任务栏/窗口框架渲染.
# 使用 Sarasa Gothic TTF 字体 (通过 API->draw_char_unicode), 透明 alpha 混合.
# 12字节头: load_addr=0x00900000 小端 => \x00\x00\x90\x00, 保留4字节0
efmsfile/efmAether.efs: efmsfile/efmAether.c
	@mkdir -p efmsfile
	$(DRV_CC) -I. -o efmsfile/efmAether.o efmsfile/efmAether.c
	$(DRV_OBJCOPY_STRIP) efmsfile/efmAether.o efmsfile/efmAether.stripped.o
	$(DRV_LD_EFS) efmsfile/efmAether.elf -Ttext=0x900000 efmsfile/efmAether.stripped.o
	objcopy -O binary -j .text -j .rodata -j .data efmsfile/efmAether.elf efmsfile/efmAether.bin
	$(call bin_package,efmsfile/efmAether,\x00\x00\x90\x00\x00\x00\x00\x00,efs)

# vga.drv: 通用 VESA GOP 帧缓冲显卡驱动, 加载地址 0x2000000 (32MB)
# 12字节头: load_addr=0x02000000 小端 => \x00\x00\x00\x02, 保留4字节0
drivers/vga.drv: drivers/vga_drv.c drivers/drv_common.h
	@mkdir -p drivers
	$(DRV_CC) -Idrivers -o drivers/vga.o drivers/vga_drv.c
	$(DRV_OBJCOPY_STRIP) drivers/vga.o drivers/vga.stripped.o
	$(DRV_LD_DRV) drivers/vga.elf -Ttext=0x2000000 drivers/vga.stripped.o
	objcopy -O binary -j .text -j .rodata -j .data drivers/vga.elf drivers/vga.bin
	$(call bin_package,drivers/vga,\x00\x00\x00\x02\x00\x00\x00\x00,drv)

# ahci.drv: AHCI/SATA 磁盘驱动, 加载地址 0x2400000 (36MB, 紧跟 vga)
# 12字节头: load_addr=0x02400000 小端 => \x00\x00\x40\x02, 保留4字节0
drivers/ahci.drv: drivers/ahci_drv.c drivers/drv_common.h
	@mkdir -p drivers
	$(DRV_CC) -Idrivers -o drivers/ahci.o drivers/ahci_drv.c
	$(DRV_OBJCOPY_STRIP) drivers/ahci.o drivers/ahci.stripped.o
	$(DRV_LD_DRV) drivers/ahci.elf -Ttext=0x2400000 drivers/ahci.stripped.o
	objcopy -O binary -j .text -j .rodata -j .data drivers/ahci.elf drivers/ahci.bin
	$(call bin_package,drivers/ahci,\x00\x00\x40\x02\x00\x00\x00\x00,drv)

# Graphics.drv: 升级版 GPU 驱动 (双缓冲+抗锯齿+渐变), 加载地址 0x2800000 (40MB)
# [注] 字母序 Graphics < ahci < vga, 因此 efmloader 会在 ahci.drv 之前加载 Graphics.drv
#      符合 "先显卡后磁盘" 的一般顺序. 但为了磁盘驱动优先, make_disk.sh 输出时可调整.
#      实际上 ahci=AHCI 仅影响磁盘读(ext4之前已初始化), 所以显卡优先也 OK.
drivers/Graphics.drv: drivers/graphics_drv.c drivers/drv_common.h efmsfile/ttf_font.h efmsfile/ttf_font.c
	@mkdir -p drivers
	$(DRV_CC) -Idrivers -Iefmsfile -o drivers/Graphics.o drivers/graphics_drv.c
	$(DRV_OBJCOPY_STRIP) drivers/Graphics.o drivers/Graphics.stripped.o
	$(DRV_LD_DRV) drivers/Graphics.elf -Ttext=0x2800000 drivers/Graphics.stripped.o
	objcopy -O binary -j .text -j .rodata -j .data drivers/Graphics.elf drivers/Graphics.bin
	$(call bin_package,drivers/Graphics,\x00\x00\x80\x02\x00\x00\x00\x00,drv)

# gcc.efs: 自包含 C/C++ 子集编译器, 加载地址 0x700000 (7MB, 不与 efmlogin 0x600000 冲突)
# [入口对齐修复] 必须走 DRV_CC -> stripped.o -> DRV_LD_EFS (efm_flat.ld),
#   与 efmshell/efmloader/efmAether 同流程, 保证 _start section(".text.start")
#   排在 flat binary 首字节, 避免普通 gcc -Ttext 时静态函数插队导致内核跳入垃圾。
# [SSE/FPU 例外] gcc.c 内部词法器用 double (L.fval, g_strtod), 编译器会用 XMM 寄存器
#   做 double 赋值返回; 若带 DRV_CC 的 -mno-sse -mno-sse2 -mno-mmx 会报错
#   "SSE register return with SSE disabled". 因此单独用 EFS_CC (保留硬件浮点 + SSE2).
#   内核 EFS 沙盒进入前已经 fxrstor 初始化 x87 + SSE 状态, 用户态可安全用浮点。
# [目录结构] 非系统程序存放于 /Program/<name>/ 下, 产物对应输出到 Program/gcc/gcc.efs
EFS_CC = gcc -c -ffreestanding -fno-pic -mno-red-zone -mcmodel=large \
            -fno-asynchronous-unwind-tables -fno-stack-protector \
            -fomit-frame-pointer -O2
Program/gcc/gcc.efs: Program/gcc/gcc.c
	@mkdir -p Program/gcc
	$(EFS_CC) -I. -o Program/gcc/gcc.o Program/gcc/gcc.c
	$(DRV_OBJCOPY_STRIP) Program/gcc/gcc.o Program/gcc/gcc.stripped.o
	$(DRV_LD_EFS) Program/gcc/gcc.elf -Ttext=0x700000 Program/gcc/gcc.stripped.o
	objcopy -O binary -j .text -j .rodata -j .data Program/gcc/gcc.elf Program/gcc/gcc.bin
	SIZE=$$(stat -c%s Program/gcc/gcc.bin); \
	bash -c "printf '\x00\x00\x70\x00\x00\x00\x00\x00' > Program/gcc/gcc.efs"; \
	if command -v xxd >/dev/null 2>&1; then \
	  printf '%08x' $$SIZE | xxd -r -p >> Program/gcc/gcc.efs; \
	else \
	  b0=$$(( (SIZE >> 24) & 0xFF )); b1=$$(( (SIZE >> 16) & 0xFF )); \
	  b2=$$(( (SIZE >> 8)  & 0xFF )); b3=$$(( SIZE & 0xFF )); \
	  bash -c "printf '\x'$$(printf '%02x' $$b0)'\x'$$(printf '%02x' $$b1)'\x'$$(printf '%02x' $$b2)'\x'$$(printf '%02x' $$b3)" >> Program/gcc/gcc.efs; \
	fi; \
	cat Program/gcc/gcc.bin >> Program/gcc/gcc.efs
# 兼容别名: make gcc.efs 等价于 Program/gcc/gcc.efs
gcc.efs: Program/gcc/gcc.efs
	@cp -f Program/gcc/gcc.efs gcc.efs 2>/dev/null || true

disk.img: bootloader/BOOTX64.EFI kernel/kernel.elf \
          efmsfile/efmlogin.efs efmsfile/efmloader.efs efmsfile/efmshell.efs \
          efmsfile/efmcompositor.efs efmsfile/efmAether.efs \
          efmsfile/fileman.efs efmsfile/setting.efs efmsfile/userman.efs \
          drivers/Graphics.drv drivers/ahci.drv Program/gcc/gcc.efs build_disk.py
	python3 build_disk.py

disk.vmdk: disk.img
	qemu-img convert -f raw -O vmdk disk.img disk.vmdk

# ========== Mesa 移植: EFMOS libc + libdrm 兼容层 (静态库) ==========
# 用 -fPIC 编译以便链接进 Mesa 共享库 (.so), 用 lib/include 为系统头目录.
EFM_LIB_CC = gcc -c -ffreestanding -fPIC -mno-red-zone \
                  -fno-asynchronous-unwind-tables -fno-stack-protector \
                  -fomit-frame-pointer -O1 \
                  -Ilib/include -nostdinc -Ilib/include -I. \
                  -I$(shell gcc -print-file-name=include)
EFM_LIB_AR = ar rcs

lib: lib/libefmlibc.a lib/libefmlibdrm.a lib/libefmgbm.a lib/libefmegl.a lib/libefmgem.a \
     lib/libefmfw.a lib/libefmdeps.a

lib/libefmlibc.a: lib/efmlibc.c lib/efm_math_stubs.c
	@mkdir -p lib
	$(EFM_LIB_CC) -o lib/efmlibc.o           lib/efmlibc.c
	$(EFM_LIB_CC) -o lib/efm_math_stubs.o    lib/efm_math_stubs.c
	$(EFM_LIB_AR) $@ lib/efmlibc.o lib/efm_math_stubs.o

lib/libefmlibdrm.a: lib/efmlibdrm.c
	@mkdir -p lib
	$(EFM_LIB_CC) -o lib/efmlibdrm.o  lib/efmlibdrm.c
	$(EFM_LIB_AR) $@ lib/efmlibdrm.o

lib/libefmgbm.a: lib/efmlibgbm.c
	@mkdir -p lib
	$(EFM_LIB_CC) -o lib/efmlibgbm.o  lib/efmlibgbm.c
	$(EFM_LIB_AR) $@ lib/efmlibgbm.o

lib/libefmegl.a: lib/efmlibegl.c
	@mkdir -p lib
	$(EFM_LIB_CC) -o lib/efmlibegl.o  lib/efmlibegl.c
	$(EFM_LIB_AR) $@ lib/efmlibegl.o

lib/libefmgem.a: lib/efmlibgem.c
	@mkdir -p lib
	$(EFM_LIB_CC) -o lib/efmlibgem.o  lib/efmlibgem.c
	$(EFM_LIB_AR) $@ lib/efmlibgem.o

# ---------- GPU 固件加载兼容层 (request_firmware 系列) ----------
# Mesa Gallium3D 硬件 winsys 编译时引用 request_firmware, 但 swrast 运行时
# 不需要固件. 此库从 /EFMOS/FIRMWARE/ 读取 .bin, 不存在则返回空 blob.
lib/libefmfw.a: lib/efmlibfw.c
	@mkdir -p lib
	$(EFM_LIB_CC) -o lib/efmlibfw.o  lib/efmlibfw.c
	$(EFM_LIB_AR) $@ lib/efmlibfw.o

# ---------- Mesa 额外依赖 stub (expat/zlib/libelf/sha1) ----------
# 让 Mesa util/xmlconfig.c, disk_cache, sha1, spir-v 等 编译通过.
lib/libefmdeps.a: lib/efmlibdeps.c
	@mkdir -p lib
	$(EFM_LIB_CC) -o lib/efmlibdeps.o  lib/efmlibdeps.c
	$(EFM_LIB_AR) $@ lib/efmlibdeps.o

# ---------- pkg-config (.pc) 文件: 让 Mesa meson 通过 dependency('xxx') 找到 ----------
# 输出到 lib/pkgconfig/, 内容引用 $(CURDIR)/lib 作 libdir, $(CURDIR)/lib/include 作 includedir.
lib/pkgconfig/%.pc: lib/pkgconfig/%.pc.in
	@mkdir -p lib/pkgconfig
	@sed -e 's|@PREFIX@|$(CURDIR)|g' \
	     -e 's|@LIBDIR@|$(CURDIR)/lib|g' \
	     -e 's|@INCLUDEDIR@|$(CURDIR)/lib/include|g' $< > $@
	@echo "--- generated: $@ ---"

pkgconfig: lib/pkgconfig/efm-libc.pc lib/pkgconfig/efm-libdrm.pc \
           lib/pkgconfig/efm-gbm.pc lib/pkgconfig/efm-egl.pc \
           lib/pkgconfig/efm-gem.pc lib/pkgconfig/efm-fw.pc lib/pkgconfig/efm-deps.pc \
           lib/pkgconfig/libdrm.pc lib/pkgconfig/gbm.pc lib/pkgconfig/egl.pc lib/pkgconfig/zlib.pc \
           lib/pkgconfig/libelf.pc lib/pkgconfig/libudev.pc

# ---------- Mesa 构建工具链元数据 (meson cross-file 参考) ----------
mesa-toolchain: lib pkgconfig
	@GCC_INCLUDE_DIR=$$(gcc -print-file-name=include); \
	EFM_INC='-I$(CURDIR)/lib/include'; \
	GCC_INC="-I$${GCC_INCLUDE_DIR}"; \
	C_ARGS_ARR="['-ffreestanding','-mno-red-zone','-fno-stack-protector','-fno-pic','-nostdinc','$${EFM_INC}','$${GCC_INC}']"; \
	{ \
	echo "[binaries]"; \
	echo "c = 'gcc'"; \
	echo "cpp = 'g++'"; \
	echo "ar = 'ar'"; \
	echo "strip = 'strip'"; \
	echo "pkg-config = '$(shell which pkg-config)'"; \
	echo ""; \
	echo "[host_machine]"; \
	echo "system = 'linux'"; \
	echo "cpu_family = 'x86_64'"; \
	echo "cpu = 'x86_64'"; \
	echo "endian = 'little'"; \
	echo ""; \
	echo "[properties]"; \
	echo "c_args = $${C_ARGS_ARR}"; \
	echo "cpp_args = ['-fno-stack-protector','-fno-pic','-I$(CURDIR)/lib/include','-fpermissive']"; \
	echo "c_link_args = ['-nostdlib','-no-pie','-L$(CURDIR)/lib','-lefmegl','-lefmgbm','-lefmgem','-lefmlibdrm','-lefmfw','-lefmdeps','-lefmlibc']"; \
	echo "cpp_link_args = ['-L$(CURDIR)/lib','-lefmegl','-lefmgbm','-lefmgem','-lefmlibdrm','-lefmfw','-lefmdeps','-lefmlibc','-lstdc++']"; \
	echo "pkg_config_path = '$(CURDIR)/lib/pkgconfig'"; \
	echo "sys_root = '$(CURDIR)'"; \
	} > mesa-cross.ini
	@echo "--- generated: mesa-cross.ini (给 meson --cross-file=mesa-cross.ini 使用) ---"

run: disk.img
	qemu-img info disk.img
	qemu-system-x86_64 -m 1024M -bios OVMF.fd -serial stdio -M q35 \
  -vga std -global VGA.vgamem_mb=64 \
  -drive file=disk.img,format=raw,if=none,id=disk0 \
  -device ahci,id=ahci \
  -device ide-hd,drive=disk0,bus=ahci.0

clean:
	rm -f bootloader/*.o bootloader/BOOTX64.EFI kernel/*.o kernel/kernel.elf disk.img disk.vmdk \
	      efmsfile/*.elf efmsfile/*.bin efmsfile/*.efs efmsfile/*.o efmsfile/*.tmp_head \
	      gcc.elf gcc.bin gcc.efs gcc.tmp_head \
	      Program/gcc/*.elf Program/gcc/*.bin Program/gcc/*.efs Program/gcc/*.tmp_head \
	      drivers/*.o drivers/*.stripped.o drivers/*.elf drivers/*.bin drivers/*.drv drivers/*.tmp_head \
	      drivers/Graphics.o drivers/Graphics.stripped.o drivers/Graphics.elf \
	      drivers/Graphics.bin drivers/Graphics.drv \
	      lib/*.o lib/libefmlibc.a lib/libefmlibdrm.a lib/libefmgbm.a lib/libefmegl.a \
	      lib/libefmgem.a lib/libefmfw.a lib/libefmdeps.a \
	      lib/pkgconfig/*.pc mesa-cross.ini \
	      *.tmp_*
# *.tmp_* 兜底: bin_package 宏中途失败可能残留 <target>.tmp_head, 以及未来新增的临时文件.
#   上面每个产物显式列一份, 防止 "*" 匹配太宽删到源码.

.PHONY: all clean run lib mesa-toolchain pkgconfig
