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

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。

## 致谢

- **Sarasa Gothic** 字体 — 基于 Iosevka + 思源黑体的 CJK 等宽字体
- **Mesa 3D** — OpenGL 实现参考
- **OVMF** — UEFI 固件实现
