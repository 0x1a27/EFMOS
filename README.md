# EFMOS

**EFMOS** (EFM Operating System) 是一个从零开始编写的 64 位 x86 UEFI 可启动操作系统，使用 C 语言实现内核、引导加载器、驱动程序、文件系统和用户程序。

![EFMOS Logo](recourses/EFMOS-logo-w.png)

## 特性

### 内核
- **单文件内核** (`kernel/kernel.c`)，约 11,000+ 行 C 代码
- UEFI 引导，ELF 内核加载
- GDT/IDT/SSE/SYSCALL 指令支持
- LAPIC 定时器（MASKED 轮询模式，无 IRQ 抢占）
- SMP 多核初始化（MADT 解析 + SIPI 唤醒）
- 抢占式调度器（优先级 + 时间片轮转）

### 文件系统
- **ext4** 读写驱动（支持 inode、块组、目录树）
- **EFS**（EFMOS File System）— 扁平二进制程序格式，12 字节头 `[load_addr][reserved][size]`
- 内核 API 表通过 `0x9000` 地址暴露给 EFS 程序

### 驱动
- **AHCI** SATA 磁盘驱动（多端口、LBA48、DMA）
- **IDE** ATA/ATAPI 驱动（硬盘 + CD-ROM 2048 字节扇区翻译）
- **Graphics.drv** — 双缓冲 + 脏矩形刷新 + TTF 抗锯齿字体渲染
- **PS/2** 键盘和鼠标驱动（流式模式）

### 桌面环境
- **efmcompositor** — 窗口合成器（pid=3）
- **efmAether** — 桌面环境（pid=4），渲染信息写入 `0x6000`
- **efmshell** — 命令行 Shell（支持历史、Tab 补全、CJK 文本对齐）
- **fileman** — 文件管理器
- **setting** — 系统设置
- **userman** — 用户管理
- **efmlogin** — 登录程序

### 图形栈
- TTF 字体引擎（Sarasa Gothic，支持 ASCII + CJK 混排）
- Mesa/OpenGL 兼容层（`lib/` 目录，libc + libdrm + EGL + GBM 适配层）
- GCC 编译器移植（`Program/gcc/`）

## 目录结构

```
EFMOS/
├── bootloader/        # UEFI 引导加载器 (bootloader.c → BOOTX64.EFI)
├── kernel/            # 操作系统内核 (kernel.c, link.ld)
├── drivers/            # 内核驱动 (.drv 格式, 热加载)
│   ├── ahci_drv.c      #   AHCI SATA 磁盘驱动
│   ├── graphics_drv.c  #   图形驱动 (TTF + 双缓冲)
│   ├── vga_drv.c       #   VGA 回退驱动
│   └── drv_common.h    #   驱动公共头文件
├── efmsfile/          # EFS 用户程序
│   ├── efmshell.c      #   命令行 Shell
│   ├── efmloader.c     #   驱动加载器 (监控 /EFMOS/DRIVERS)
│   ├── efmcompositor.c #   窗口合成器
│   ├── efmAether.c     #   桌面环境
│   ├── fileman.c       #   文件管理器
│   ├── setting.c       #   系统设置
│   ├── userman.c       #   用户管理
│   ├── efmlogin.c      #   登录程序
│   ├── ttf_font.c/h    #   TTF 字体引擎
│   └── logo.h          #   启动 Logo 位图
├── Program/gcc/       # GCC 编译器移植
├── lib/               # Mesa 兼容层 (libc/libdrm/EGL/GBM)
│   ├── include/        #   系统头文件
│   ├── pkgconfig/      #   pkg-config 模板
│   ├── efmlibc.c       #   libc 兼容层
│   ├── efmlibdrm.c     #   libdrm 兼容层
│   ├── efmlibegl.c     #   EGL 兼容层
│   ├── efmlibgbm.c     #   GBM 兼容层
│   └── efmlibfw.c      #   固件加载兼容层
├── fonts/             # 字体文件 (Sarasa Gothic)
├── recourses/         # 资源文件 (Logo)
├── Makefile           # 构建系统
├── build_disk.py      # 磁盘镜像构建 (GPT + FAT ESP + ext4)
├── gen_cjk_font.py    # CJK 点阵字体生成器
└── efm_flat.ld        # EFS 程序链接脚本
```

## 构建要求

| 工具 | 用途 |
|------|------|
| `gcc` | 编译内核、EFS 程序、驱动 |
| `x86_64-w64-mingw32-gcc` | 编译 UEFI 引导加载器 |
| `ld` | 链接内核 ELF |
| `objcopy` | 生成扁平二进制 |
| `python3` | 构建脚本 |
| `mkfs.ext4` / `debugfs` | 创建 ext4 分区 |
| `qemu-system-x86_64` | 测试运行 |
| OVMF 固件 (`OVMF.fd`) | UEFI 引导 (需单独下载) |

### 安装依赖 (Ubuntu/Debian)

```bash
sudo apt install gcc binutils-mingw-w64-x86-64 python3 e2fsprogs qemu-system-x86
# OVMF 固件:
sudo apt install ovmf
cp /usr/share/OVMF/OVMF_CODE.fd ./OVMF.fd
```

## 构建与运行

### 1. 构建完整系统磁盘

```bash
make
```

生成 `disk.img`（150MB GPT 磁盘：FAT ESP + ext4 系统分区）。

### 2. 在 QEMU 中运行

```bash
make run
# 等价于:
qemu-system-x86_64 -m 1024M -bios OVMF.fd -serial stdio -M q35 \
  -vga std -global VGA.vgamem_mb=64 \
  -drive file=disk.img,format=raw,if=none,id=disk0 \
  -device ahci,id=ahci \
  -device ide-hd,drive=disk0,bus=ahci.0
```

### 3. 单独构建组件

```bash
make kernel/kernel.elf     # 仅内核
make bootloader/BOOTX64.EFI  # 仅引导加载器
make efmsfile/efmshell.efs   # 仅 Shell
make drivers/Graphics.drv    # 仅图形驱动
make lib                     # 仅 Mesa 兼容库
```

## 内存布局

| 地址 | 用途 |
|------|------|
| `0x200000` | 内核 (VMA = 2MB) |
| `0x300000` | fileman.efs (3MB) |
| `0x400000` | efmloader.efs (4MB) |
| `0x500000` | userman.efs (5MB) |
| `0x600000` | efmlogin.efs (6MB) |
| `0x6000` | efmAether 渲染信息 (固定地址) |
| `0x9000` | 内核 API 表 (EFS 程序访问入口) |
| `0x2400000` | ahci.drv (36MB) |
| `0x2800000` | Graphics.drv (40MB) |

## EFS 程序格式

EFS 程序是扁平二进制文件，前 12 字节为头：

```
[4 bytes: load_addr (小端)] [4 bytes: 保留 0] [4 bytes: bin_size (大端)]
```

入口函数 `_start` 必须放在 `.text.start` 段（通过 `__attribute__((section(".text.start")))`），并使用 `efm_flat.ld` 链接脚本确保入口位于首字节。

## 系统截图

系统启动后显示桌面环境，包含：
- 任务栏（开始按钮 + 时钟）
- 窗口管理（拖动、调整大小、关闭）
- 文件管理器
- 终端 Shell
- 系统设置

## 许可证

EFMOS 自研代码（内核、引导加载器、驱动、EFS 程序、Mesa 兼容层 `lib/`、内置编译器 `Program/gcc/`、构建脚本等）采用 **GNU General Public License v3.0**（GPLv3）发布，完整许可证文本见 [LICENSE](LICENSE)。

Copyright (C) 2026 0x1a27

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

各源码文件的许可证声明见文件头的 `SPDX-License-Identifier` 注释。

### 第三方组件及其许可证

本项目包含或引用以下第三方软件，各自按其原有许可证使用（详见 [NOTICE](NOTICE)）：

| 组件 | 位置 | 许可证 | 说明 |
|------|------|--------|------|
| Mesa 3D 24.2.5 | `mesa-src/`（未入库，单独检出） | MIT（核心库；GLX 为 SGI Free Software License B 等，逐组件见 `mesa-src/docs/license.rst`） | OpenGL/Gallium 实现，用于构建 EFMOS 图形栈（swrast 软件光栅化 + EGL/GBM） |
| Sarasa Gothic 字体 | `fonts/` | SIL Open Font License 1.1（OFL-1.1，见 `fonts/OFL.txt`） | 基于 Iosevka + 思源黑体的 CJK 等宽字体；`gen_cjk_font.py` 生成的 CJK 点阵数据同属 OFL-1.1 |
| OVMF (edk2) | `OVMF*.fd`（二进制，未入库） | GPLv2+ | UEFI 固件，需从 [edk2](https://github.com/tianocore/edk2) 项目单独构建/获取 |
| Khronos EGL / Linux DRM uapi / libdrm | `lib/include/` | 接口规范分别属 Khronos / GPL-2.0-or-later + BSD-2-Clause / MIT | EFMOS 编写的最小兼容适配声明 |

### 分发说明

以二进制形式（如 `disk.img`、`efmos-install.iso`）分发 EFMOS 时，按 GPLv3 要求须同时提供对应的完整源码（或提供至少三年、以可接受费用提供的书面要约）；第三方组件须按其各自许可证保留版权声明，并随附相应许可证文本与源码获取途径。

## 致谢

- **Sarasa Gothic** 字体（作者 Naoto T. Ito / be5invis）及思源黑体贡献者
- **Mesa 3D** 社区
- **OVMF / edk2** 项目
