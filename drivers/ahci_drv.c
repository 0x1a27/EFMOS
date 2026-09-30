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

/* ahci_drv.c - EFMOS AHCI/SATA 磁盘驱动 (编译为 ahci.drv)
 * 加载地址: 0x2400000 (36MB, 紧跟 vga.drv 之后, 缓冲区 0x9xxxx 不与其重叠)
 * 入口: drv_entry
 *
 * 功能:
 *   - 使用 PCI 枚举查找 AHCI 控制器 (class 0x0106)
 *   - 执行完整 HBA/端口初始化 (与内核内置 AHCI 代码一致)
 *   - 暴露 drv_disk_ops: read_blocks / write_blocks / flush / size_sectors
 *   - 注册成功后, 内核 disk_read_sector / disk_read_n 等函数会优先使用本驱动
 *
 * [注] 本驱动使用 0x90000 / 0x91000 / 0x92000 作为 AHCI 命令列表 / FIS 区 / 命令表,
 *      与内核内置 AHCI 实现共用相同区域, 不会冲突 (驱动加载时会重新初始化这些区域,
 *      此时内核已完成 ext4 初始化, 命令列表内容不再需要)。
 */

#include "drv_common.h"

/* ========== 驱动入口 (必须在 .text 最前面, 内核从 load_addr 调用) ========== */
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

/* ========== 小工具宏/函数 (最小化依赖, 不使用 C 库) ========== */
#define NULL ((void*)0)
static void my_memset(void *s, unsigned char c, unsigned long n) {
    unsigned char *p = s; while (n--) *p++ = c;
}
static void my_memcpy(void *d, const void *s, unsigned long n) {
    unsigned char *a = d, *b = (unsigned char*)s; while (n--) *a++ = *b++;
}

/* I/O Port: 通过内核 iface->in8/out8/... 访问 */
static struct drv_kernel_if *g_iface = NULL;
#define inb(p)   (g_iface->in8((unsigned short)(p)))
#define inw(p)   (g_iface->in16((unsigned short)(p)))
#define inl(p)   (g_iface->in32((unsigned short)(p)))
#define outb(p,v) g_iface->out8((unsigned short)(p),(unsigned char)(v))
#define outw(p,v) g_iface->out16((unsigned short)(p),(unsigned short)(v))
#define outl(p,v) g_iface->out32((unsigned short)(p),(unsigned int)(v))
#define LOG(s)    do { if (g_iface) g_iface->log(s); } while (0)

/* hex print 辅助: 把 unsigned long 打成 "0x..." 到 buf, 返回 buf */
static char *hex_str(char *buf, unsigned long v) {
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    int p = 18; buf[p--] = 0;
    for (int i = 0; i < 16; i++) { buf[p--] = hex[v & 0xF]; v >>= 4; }
    return buf;
}
static void log_hex(const char *prefix, unsigned long v, const char *suffix) {
    char b[32];
    char b2[32];
    int n = 0;
    for (int i = 0; prefix[i]; i++) b[n++] = prefix[i];
    char *h = hex_str(b2, v);
    for (int i = 0; h[i]; i++) b[n++] = h[i];
    for (int i = 0; suffix[i]; i++) b[n++] = suffix[i];
    b[n] = 0;
    LOG(b);
}

/* ========== AHCI 内存布局常量 (与内核内置 AHCI 完全一致) ========== */
#define AHCI_CMD_LIST_BASE   0x90000
#define AHCI_FIS_RECV_BASE   0x91000
#define AHCI_CMD_TABLE_BASE  0x92000

static volatile unsigned int *ahci_abar = NULL;
static int ahci_port = -1;

/* ========== PCI 配置访问 ========== */
static unsigned int pci_config_read(unsigned int bus, unsigned int dev, unsigned int func, unsigned int offset) {
    outl(0xCF8, 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC));
    return inl(0xCFC);
}
static void pci_config_write(unsigned int bus, unsigned int dev, unsigned int func, unsigned int offset, unsigned int val) {
    outl(0xCF8, 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC));
    outl(0xCFC, val);
}

/* ========== 错误恢复 / 端口就绪检查 / 命令结果 ========== */
static int ahci_port_error_recovery(void);
static int ahci_wait_port_ready(void);
static int ahci_check_cmd_result(void);
static int ahci_build_prdt(unsigned char *cmd_table_base, const void *buf, unsigned int byte_count);

static int ahci_find_controller(void) {
    int found = 0;
    for (int pass = 0; pass < 2 && !found; pass++) {
        int bus_hi = (pass == 0) ? 1 : 256;
        for (int bus = 0; bus < bus_hi && !found; bus++) {
        for (int dev = 0; dev < 32 && !found; dev++) {
            for (int func = 0; func < 8 && !found; func++) {
                unsigned int vendor = pci_config_read(bus, dev, func, 0);
                if (vendor == 0xFFFF) continue;
                if (((pci_config_read(bus, dev, func, 0x08) >> 16) & 0xFFFF) == 0x0106) {
                    unsigned int bar5_low = pci_config_read(bus, dev, func, 0x24);
                    unsigned int bar5_high = pci_config_read(bus, dev, func, 0x28);
                    unsigned long long bar5 = ((unsigned long long)bar5_high << 32) | bar5_low;
                    bar5 &= ~0xF;
                    ahci_abar = (volatile unsigned int*)(unsigned long)bar5;
                    log_hex("[ahci] AHCI BAR5 = ", (unsigned long)bar5, "\n");

                    unsigned int cmd = pci_config_read(bus, dev, func, 0x04);
                    cmd |= (1 << 1) | (1 << 2);
                    pci_config_write(bus, dev, func, 0x04, cmd);

                    ahci_abar[0x04/4] |= (1 << 31);
                    int timeout = 0;
                    while (!(ahci_abar[0x04/4] & (1 << 31))) {
                        if (++timeout > 10000) { found = -1; break; }
                        asm volatile("pause");
                    }
                    if (found == -1) break;

                    unsigned int cap = ahci_abar[0x00/4];
                    log_hex("[ahci] AHCI CAP = ", cap, "\n");
                    if (cap == 0 || cap == 0xFFFFFFFF) { found = -1; break; }
                    found = 1;
                }
            }
        }
        }
    }
    return (found == 1) ? 1 : 0;
}

static int ahci_port_init(void) {
    unsigned int pi = ahci_abar[0x0C / 4];
    log_hex("[ahci] PI = ", pi, "\n");
    int timeout;
    int found_port = -1;

    for (int p = 0; p < 32; p++) {
        if (!(pi & (1 << p))) continue;
        log_hex("[ahci] Trying port ", p, "\n");

        volatile unsigned int *port_regs = ahci_abar + 0x40 + p * 0x20;
        port_regs[0x18/4] &= ~((1 << 0) | (1 << 4));
        timeout = 0;
        while ((port_regs[0x18/4] & ((1 << 15) | (1 << 14))) && timeout < 100000) {
            asm volatile("pause"); timeout++;
        }
        if (timeout >= 100000) continue;

        port_regs[0x10/4] = port_regs[0x10/4];
        port_regs[0x30/4] = port_regs[0x30/4];

        port_regs[0x2C/4] = (port_regs[0x2C/4] & ~0xF) | 1;
        port_regs[0x2C/4] &= ~(0xF << 8);
        for (volatile int i = 0; i < 200000; i++) asm volatile("pause");
        port_regs[0x2C/4] = (port_regs[0x2C/4] & ~0xF) | 0;
        timeout = 0;
        while ((port_regs[0x2C/4] & 0xF) && timeout < 100000) { asm volatile("pause"); timeout++; }

        unsigned int ssts;
        timeout = 0;
        do {
            ssts = port_regs[0x28/4];
            if ((ssts & 0xF) == 0x3) break;
            for (volatile int i = 0; i < 100000; i++) asm volatile("pause");
        } while (++timeout < 500);

        log_hex("[ahci] Port ", p, ": ");
        log_hex("PxSSTS = ", ssts, "\n");
        port_regs[0x30/4] = port_regs[0x30/4];
        if ((ssts & 0xF) == 0x3) { found_port = p; break; }
    }

    if (found_port < 0) return -1;
    ahci_port = found_port;

    volatile unsigned int *port_regs = ahci_abar + 0x40 + found_port * 0x20;
    port_regs[0x18/4] &= ~((1 << 0) | (1 << 4));
    timeout = 0;
    while ((port_regs[0x18/4] & ((1 << 15) | (1 << 14))) && timeout < 100000) { asm volatile("pause"); timeout++; }

    my_memset((void*)AHCI_CMD_LIST_BASE, 0, 1024);
    my_memset((void*)AHCI_FIS_RECV_BASE, 0, 256);
    port_regs[0x00/4] = AHCI_CMD_LIST_BASE;
    port_regs[0x04/4] = 0;
    port_regs[0x08/4] = AHCI_FIS_RECV_BASE;
    port_regs[0x0C/4] = 0;

    port_regs[0x18/4] |= (1 << 4);
    for (volatile int i = 0; i < 1000; i++) asm volatile("pause");
    port_regs[0x18/4] |= (1 << 0);
    timeout = 0;
    while (!(port_regs[0x18/4] & (1 << 15)) || !(port_regs[0x18/4] & (1 << 14))) {
        if (++timeout > 100000) { LOG("[ahci] engine start timeout\n"); return -1; }
        asm volatile("pause");
    }
    LOG("[ahci] port ready.\n");
    return 0;
}

static int ahci_port_error_recovery(void) {
    if (ahci_port < 0) return -1;
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    LOG("[ahci] error recovery\n");
    port_regs[0x18/4] &= ~((1 << 0) | (1 << 4));
    int timeout = 0;
    while ((port_regs[0x18/4] & ((1 << 15) | (1 << 14))) && timeout < 100000) { asm volatile("pause"); timeout++; }
    port_regs[0x10/4] = port_regs[0x10/4];
    port_regs[0x30/4] = port_regs[0x30/4];
    port_regs[0x2C/4] = (port_regs[0x2C/4] & ~0xF) | 1;
    for (volatile int i = 0; i < 200000; i++) asm volatile("pause");
    port_regs[0x2C/4] &= ~0xF;
    timeout = 0;
    unsigned int ssts;
    do {
        ssts = port_regs[0x28/4];
        if ((ssts & 0xF) == 0x3) break;
        for (volatile int i = 0; i < 100000; i++) asm volatile("pause");
    } while (++timeout < 300);
    if ((ssts & 0xF) != 0x3) { LOG("[ahci] recovery failed, no device\n"); return -1; }
    port_regs[0x18/4] |= (1 << 4);
    for (volatile int i = 0; i < 1000; i++) asm volatile("pause");
    port_regs[0x18/4] |= (1 << 0);
    timeout = 0;
    while (!(port_regs[0x18/4] & (1 << 15)) || !(port_regs[0x18/4] & (1 << 14))) {
        if (++timeout > 100000) { LOG("[ahci] recovery: engine start failed\n"); return -1; }
        asm volatile("pause");
    }
    LOG("[ahci] recovery complete\n");
    return 0;
}

static int ahci_wait_port_ready(void) {
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    int timeout = 0;
    while ((port_regs[0x20/4] & 0x88) && timeout < 500000) {
        if (++timeout % 50000 == 0) {
            if (port_regs[0x30/4] & 0xFFFFFFFF) {
                log_hex("[ahci] PxSERR before cmd: ", port_regs[0x30/4], "\n");
                ahci_port_error_recovery();
                return -1;
            }
        }
        asm volatile("pause");
    }
    if (timeout >= 500000) { LOG("[ahci] port busy timeout\n"); ahci_port_error_recovery(); return -1; }
    timeout = 0;
    while ((port_regs[0x38/4] & 1) && timeout < 500000) {
        if (++timeout % 100000 == 0) {
            unsigned int tfd = port_regs[0x20/4];
            if (tfd & 0x01) { ahci_port_error_recovery(); return -1; }
        }
        asm volatile("pause");
    }
    if (timeout >= 500000) { LOG("[ahci] PxCI stuck, recovering\n"); ahci_port_error_recovery(); return -1; }
    port_regs[0x10/4] = port_regs[0x10/4];
    port_regs[0x30/4] = port_regs[0x30/4];
    return 0;
}

static int ahci_check_cmd_result(void) {
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    unsigned int tfd = port_regs[0x20/4];
    unsigned int serr = port_regs[0x30/4];
    unsigned int is = port_regs[0x10/4];
    port_regs[0x10/4] = is;
    port_regs[0x30/4] = serr;
    if (tfd & 0x01) {
        log_hex("[ahci] cmd error PxTFD=", tfd, " ");
        log_hex("PxSERR=", serr, "\n");
        ahci_port_error_recovery();
        return -1;
    }
    if (is & 0xBC000) {
        log_hex("[ahci] fatal irq PxIS=", is, "\n");
        ahci_port_error_recovery();
        return -1;
    }
    return 0;
}

static int ahci_build_prdt(unsigned char *cmd_table_base, const void *buf, unsigned int byte_count) {
    volatile unsigned int *prd = (volatile unsigned int*)(cmd_table_base + 0x80);
    unsigned int remaining = byte_count;
    unsigned long long addr = (unsigned long long)(unsigned long)buf;
    int n = 0;
    while (remaining > 0 && n < 248) {
        unsigned int chunk = remaining;
        if (chunk > 0x400000) chunk = 0x400000;
        prd[n * 4 + 0] = (unsigned int)(addr & 0xFFFFFFFF);
        prd[n * 4 + 1] = (unsigned int)(addr >> 32);
        prd[n * 4 + 2] = 0;
        prd[n * 4 + 3] = chunk - 1;
        addr += chunk;
        remaining -= chunk;
        n++;
    }
    if (n > 0) prd[(n - 1) * 4 + 3] |= 0x80000000;
    return n;
}

/* ========== 读/写/刷/容量 ========== */
static int ahci_read_blocks(unsigned int lba, unsigned int count, void *buf) {
    if (ahci_port < 0 || count == 0) return -1;
    if (count > 256) count = 256;
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    if (ahci_wait_port_ready() != 0) return -1;

    volatile unsigned int *cmd_header = (volatile unsigned int*)AHCI_CMD_LIST_BASE;
    for (int i = 0; i < 8; i++) cmd_header[i] = 0;
    cmd_header[0] = 5;
    cmd_header[1] = 0;
    cmd_header[2] = AHCI_CMD_TABLE_BASE;
    cmd_header[3] = 0;

    volatile unsigned int *cmd_table = (volatile unsigned int*)AHCI_CMD_TABLE_BASE;
    for (int i = 0; i < 128; i++) cmd_table[i] = 0;
    volatile unsigned int *fis = cmd_table;
    fis[0] = 0x27 | (0x80 << 8) | (0x25 << 16);
    fis[1] = (lba & 0xFF)
           | (((lba >> 8) & 0xFF) << 8)
           | (((lba >> 16) & 0xFF) << 16)
           | (0xE0 << 24);
    fis[2] = ((lba >> 24) & 0xFF);
    fis[3] = (count & 0xFF) | (((count >> 8) & 0xFF) << 8);
    fis[4] = 0;

    int prdt_n = ahci_build_prdt((unsigned char*)AHCI_CMD_TABLE_BASE, buf, count * 512);
    if (prdt_n <= 0) { LOG("[ahci] read build_prdt failed\n"); return -1; }
    cmd_header[0] |= ((unsigned int)prdt_n << 16);

    port_regs[0x38/4] = 1;
    int timeout = 0;
    while (port_regs[0x38/4] & 1) {
        if (++timeout > 5000000) {
            log_hex("[ahci] read timeout, PxCI=", port_regs[0x38/4], " ");
            log_hex("PxTFD=", port_regs[0x20/4], "\n");
            ahci_port_error_recovery();
            return -1;
        }
        asm volatile("pause");
    }
    return ahci_check_cmd_result();
}

static int ahci_write_blocks(unsigned int lba, unsigned int count, const void *buf) {
    if (ahci_port < 0 || count == 0) return -1;
    if (count > 256) count = 256;
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    if (ahci_wait_port_ready() != 0) return -1;

    volatile unsigned int *cmd_header = (volatile unsigned int*)AHCI_CMD_LIST_BASE;
    for (int i = 0; i < 8; i++) cmd_header[i] = 0;
    cmd_header[0] = (1 << 6) | 5;
    cmd_header[1] = 0;
    cmd_header[2] = AHCI_CMD_TABLE_BASE;
    cmd_header[3] = 0;

    volatile unsigned int *cmd_table = (volatile unsigned int*)AHCI_CMD_TABLE_BASE;
    for (int i = 0; i < 128; i++) cmd_table[i] = 0;
    volatile unsigned int *fis = cmd_table;
    fis[0] = 0x27 | (0x80 << 8) | (0x35 << 16);
    fis[1] = (lba & 0xFF)
           | (((lba >> 8) & 0xFF) << 8)
           | (((lba >> 16) & 0xFF) << 16)
           | (0xE0 << 24);
    fis[2] = ((lba >> 24) & 0xFF);
    fis[3] = (count & 0xFF) | (((count >> 8) & 0xFF) << 8);

    int prdt_n = ahci_build_prdt((unsigned char*)AHCI_CMD_TABLE_BASE, buf, count * 512);
    if (prdt_n <= 0) { LOG("[ahci] write build_prdt failed\n"); return -1; }
    cmd_header[0] |= ((unsigned int)prdt_n << 16);

    port_regs[0x38/4] = 1;
    int timeout = 0;
    while (port_regs[0x38/4] & 1) {
        if (++timeout > 5000000) { LOG("[ahci] write timeout\n"); ahci_port_error_recovery(); return -1; }
        asm volatile("pause");
    }
    return ahci_check_cmd_result();
}

static int ahci_identify_device(unsigned char *buf) {
    if (ahci_port < 0) return -1;
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    if (ahci_wait_port_ready() != 0) return -1;

    volatile unsigned int *cmd_header = (volatile unsigned int*)AHCI_CMD_LIST_BASE;
    for (int i = 0; i < 8; i++) cmd_header[i] = 0;
    cmd_header[0] = (1 << 16) | 5;
    cmd_header[2] = AHCI_CMD_TABLE_BASE;
    cmd_header[3] = 0;

    volatile unsigned int *cmd_table = (volatile unsigned int*)AHCI_CMD_TABLE_BASE;
    for (int i = 0; i < 128; i++) cmd_table[i] = 0;
    volatile unsigned int *fis = cmd_table;
    fis[0] = 0x27 | (0x80 << 8) | (0xEC << 16);
    fis[1] = 0; fis[2] = 0; fis[3] = 0;

    volatile unsigned int *prd = cmd_table + 0x80/4;
    prd[0] = (unsigned int)(unsigned long)buf;
    prd[1] = 0;
    prd[2] = 0;
    prd[3] = 0x80000000 | (512 - 1);

    port_regs[0x38/4] = 1;
    int timeout = 0;
    while (port_regs[0x38/4] & 1) {
        if (++timeout > 2000000) { LOG("[ahci] identify timeout\n"); return -1; }
        asm volatile("pause");
    }
    if (ahci_check_cmd_result() != 0) return -1;
    return 0;
}

static int ahci_flush_cache(void) {
    if (ahci_port < 0) return -1;
    volatile unsigned int *port_regs = ahci_abar + 0x40 + ahci_port * 0x20;
    if (ahci_wait_port_ready() != 0) return -1;

    volatile unsigned int *cmd_header = (volatile unsigned int*)AHCI_CMD_LIST_BASE;
    for (int i = 0; i < 8; i++) cmd_header[i] = 0;
    cmd_header[0] = (0 << 16) | 5;
    cmd_header[0] |= (1 << 6);
    cmd_header[2] = AHCI_CMD_TABLE_BASE;
    cmd_header[3] = 0;

    volatile unsigned int *cmd_table = (volatile unsigned int*)AHCI_CMD_TABLE_BASE;
    for (int i = 0; i < 128; i++) cmd_table[i] = 0;
    volatile unsigned int *fis = cmd_table;
    fis[0] = 0x27 | (0x80 << 8) | (0xEA << 16);
    fis[1] = 0; fis[2] = 0; fis[3] = 0;

    port_regs[0x38/4] = 1;
    int timeout = 0;
    while (port_regs[0x38/4] & 1) {
        if (++timeout > 3000000) { LOG("[ahci] flush timeout\n"); return -1; }
        asm volatile("pause");
    }
    return ahci_check_cmd_result();
}

static unsigned long long ahci_get_sector_count(void) {
    unsigned char id_buf[512];
    if (ahci_identify_device(id_buf) != 0) return 0;
    unsigned long long sectors = *(unsigned int*)(id_buf + 200);
    sectors |= ((unsigned long long)*(unsigned int*)(id_buf + 204) << 32);
    unsigned short word83 = *(unsigned short*)(id_buf + 83 * 2);
    if (!(word83 & 0x400)) {
        sectors = *(unsigned int*)(id_buf + 60 * 2);
    }
    return sectors;
}

/* ========== 内核驱动 ops 表 ========== */
/* [注意] ops 字段顺序必须与 drv_common.h 中 struct drv_disk_ops 完全一致!
 * struct drv_disk_ops {
 *   int  (*read_blocks)(unsigned int lba, unsigned int count, void *buf);
 *   int  (*write_blocks)(unsigned int lba, unsigned int count, const void *buf);
 *   int  (*flush)(void);
 *   unsigned long long (*size_sectors)(void);
 *   unsigned int sector_size;
 * };
 */
static struct drv_disk_ops g_ahci_ops = {
    ahci_read_blocks,
    ahci_write_blocks,
    ahci_flush_cache,
    ahci_get_sector_count,
    512,  /* sector_size (字段, 非函数指针) */
};

/* ========== 驱动入口 ========== */
/* drv_main 的实现在下方, drv_entry 通过 forward declaration 调用 */

void drv_main(struct drv_kernel_if *iface, struct drv_gop_fb *fb) {
    (void)fb;
    if (!iface || iface->magic != DRV_IFACE_MAGIC) return;
    g_iface = iface;

    LOG("[ahci] starting (ahci.drv)\n");
    if (ahci_find_controller() != 1) {
        LOG("[ahci] AHCI controller not found, abort.\n");
        return;
    }
    if (ahci_port_init() != 0) {
        LOG("[ahci] AHCI port init failed, abort.\n");
        return;
    }
    unsigned long long sectors = ahci_get_sector_count();
    if (sectors) {
        char b[64];
        char b2[64];
        int n = 0;
        const char *p = "[ahci] disk size: ";
        for (int i = 0; p[i]; i++) b[n++] = p[i];
        char *h = hex_str(b2, (unsigned long)sectors);
        for (int i = 0; h[i]; i++) b[n++] = h[i];
        const char *s = " sectors (";
        for (int i = 0; s[i]; i++) b[n++] = s[i];
        h = hex_str(b2, (unsigned long)(sectors / 2 / 1024));
        for (int i = 0; h[i]; i++) b[n++] = h[i];
        const char *s2 = " MB)\n";
        for (int i = 0; s2[i]; i++) b[n++] = s2[i];
        b[n] = 0;
        LOG(b);
    }

    /* 注册磁盘驱动: 成功后内核的 disk_read_sector() 等会立即走 ahci.drv
     * (见 kernel.c drvi_register_driver -> drv_attach_disk_ops) */
    int rc = iface->register_driver("ahci", DRV_TYPE_DISK, &g_ahci_ops, sizeof(g_ahci_ops));
    if (rc == 0) {
        LOG("[ahci] registered disk ops OK (ahci.drv active).\n");
    } else {
        char b[32]; int n = 0;
        const char *p = "[ahci] register_driver failed, rc=";
        for (int i = 0; p[i]; i++) b[n++] = p[i];
        char tmp[16]; int tp = 0;
        unsigned int u = (rc < 0) ? (unsigned int)(-rc) : (unsigned int)rc;
        if (rc < 0) b[n++] = '-';
        if (u == 0) tmp[tp++] = '0';
        while (u) { tmp[tp++] = '0' + (u % 10); u /= 10; }
        while (tp--) b[n++] = tmp[tp];
        b[n++] = '\n'; b[n] = 0;
        LOG(b);
    }
}
