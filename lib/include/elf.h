/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_ELF_H
#define _EFMLIBC_ELF_H

/* ELF 类型定义 (Mesa u_cpu_detect.c 读取 auxv 用) */

/* EI_NIDENT */
#define EI_NIDENT 16

/* ELF 标识索引 */
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_OSABI    7
#define EI_ABIVERSION 8
#define EI_PAD      9

/* ELF 类 */
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

/* ELF 数据 */
#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

/* auxv 类型 (Mesa 读取 /proc/self/auxv 检测 CPU features) */
#define AT_NULL         0
#define AT_IGNORE       1
#define AT_EXECFD       2
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_BASE         7
#define AT_FLAGS        8
#define AT_ENTRY        9
#define AT_NOTELF      10
#define AT_UID         11
#define AT_EUID        12
#define AT_GID         13
#define AT_EGID        14
#define AT_PLATFORM    15
#define AT_HWCAP       16
#define AT_CLKTCK      17
#define AT_SECURE      23
#define AT_BASE_PLATFORM 24
#define AT_RANDOM      25
#define AT_HWCAP2      26
#define AT_EXECFN      31

/* 32-bit ELF 头 */
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned int   e_version;
    unsigned int   e_entry;
    unsigned int   e_phoff;
    unsigned int   e_shoff;
    unsigned int   e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} Elf32_Ehdr;

/* 64-bit ELF 头 */
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned int   e_version;
    unsigned long long e_entry;
    unsigned long long e_phoff;
    unsigned long long e_shoff;
    unsigned int   e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} Elf64_Ehdr;

/* auxv 条目 */
typedef struct {
    unsigned int a_type;
    union {
        unsigned int a_val;
        void *a_ptr;
        void (*a_fcn)(void);
    } a_un;
} Elf32_auxv_t;

typedef struct {
    unsigned long long a_type;
    union {
        unsigned long long a_val;
        void *a_ptr;
        void (*a_fcn)(void);
    } a_un;
} Elf64_auxv_t;

#endif
