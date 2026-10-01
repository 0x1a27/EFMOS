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

/* =========================================================================
 * EFMOS efcc - 自包含 C/C++ 子集编译器 (flat binary → .efs)
 * 加载地址: 0x700000 (7MB, 避免与其他程序冲突)
 *
 * 用法: efcc <src.c|.cpp> -o <out.efs> [-On]
 *  - 读入源文件 (内核 file_read), 直接生成 x86_64 机器码 flat binary,
 *    在文件头加 12 字节 EFS 头 (load_addr LE + bin_size BE), 写入 <out.efs>
 *  - 产出的 .efs 可由 EFMOS 内核 run_efs() 直接加载运行
 *
 * 支持子集:
 *   C:   int/char/void/指针/数组/struct/typedef/if/else/while/for/
 *        return/break/continue/函数/递归/全局+局部变量/字符串常量/
 *        sizeof/三目运算/自增自减
 *   C++: struct + 成员函数/this 指针/构造/析构/方法重载/简单 public 继承
 *        (无模板/无异常/无虚函数/无 STL/无 namespace)
 *
 * 运行时: 通过 API 表桥接内核提供 malloc/free/print/put_char/readline/
 *   file_read/file_write/file_exists 等功能
 * ========================================================================= */

/* NULL 在 -ffreestanding -nostdinc 环境下未定义, 手动定义。
 * (不引入 <stddef.h> 因为 -nostdinc + freestanding 可能没有标准头) */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* [BSS 崩溃修复] 宏: 声明变量放到 .data 段 (避免零初始化被丢到 .bss)
 *   .bss 起始地址 0x7100e0, 但 objcopy 抽取的 efcc.bin 只覆盖到 0x7100d4。
 *   若变量落在 .bss, 内核加载后仍为随机物理内存垃圾 → g_realloc(垃圾指针, ...) 崩溃。
 *   解决: 用 __attribute__((section(".data"))) 强制零初始化变量进 .data。
 *   用法: static int x IN_DATA = 0;
 *         static char *p IN_DATA = NULL;
 *         static int arr[100] IN_DATA = {0}; */
#define IN_DATA  __attribute__((section(".data")))

/* ========== 内核 API 表 (必须与内核 struct kernel_api 字段顺序/对齐完全一致) ========== */
#include "efmos/efm_api.h"

/* ========== efcc_main 前向声明 + 入口点 _start (.text.start 保证首字节) ==========
 * 内核 run_efs() 跳转到加载地址 (0x700000) 即 _start。
 * [入口对齐] section(".text.start") + efm_flat.ld 强制 _start 排 .text 最前。
 * [used 属性] efcc_main 只被 naked 函数内联汇编 call 引用, GCC -O2 不识别该
 *   引用 → dead code elimination 删掉 efcc_main 定义 → ld 报 undefined。
 *   __attribute__((used)) 阻止 GCC 消除。
 * [关键修复] 调用 efcc_main 之前必须清零 .bss 段:
 *   objcopy 抽取 efcc.bin 时只带 -j .text/.rodata/.data, .bss 内容不会写入磁盘。
 *   内核 run_efs() 也不会清零 (它只负责 read_file_range 把 N 字节复制到 load_addr)。
 *   因此 __bss_start 到 _end 的字节如果不清零, 所有 "未初始化 / ={0} 零初始化"
 *   全局/静态变量 (包括 IN_DATA 未显式初始化被 GCC 偷偷降级到 BSS 的那些) 都会是
 *   该物理页上一次使用遗留下来的随机垃圾 → 指针解引用立刻触发 #PF/#GP → 系统死机。 */
__attribute__((used))
int efcc_main(void);
/* 链接器 (ld -T efm_flat.ld) 自动定义这两个符号, 圈出 .bss 区范围 */
extern unsigned char __bss_start[];
extern unsigned char _end[];
__attribute__((naked, section(".text.start")))
void _start(void) {
    __asm__ volatile(
        "push %%rbp\n\t"
        "mov  %%rsp, %%rbp\n\t"
        "and  $-16, %%rsp\n\t"
        /* ---- .bss 清零 ---- */
        "lea  __bss_start(%%rip), %%rdi\n\t"
        "lea  _end(%%rip), %%rcx\n\t"
        "sub  %%rdi, %%rcx\n\t"          /* rcx = _end - __bss_start = 长度 */
        "jz   .Lbss_done\n\t"            /* 长度 0 跳过 (避免 rep 下溢) */
        "mov  $0, %%al\n\t"
        "cld\n\t"
        "rep stosb\n\t"                  /* [rdi..rdi+rcx) ← 0 */
        ".Lbss_done:\n\t"
        /* ---- 调用主函数 (返回值留在 rax, 内核 trampoline 当退出码) ---- */
        "xor  %%eax, %%eax\n\t"
        "call efcc_main\n\t"
        "leave\n\t"
        "ret\n\t"
        : : : "memory", "rdi", "rcx", "rax", "cc"
    );
}

/* ========== 非 static 全局变量: 必须在函数使用前定义, 避免隐式 extern 与 section(".data")
 *   属性冲突导致 ld undefined reference。统一放这里, 用 IN_DATA + 显式初始化。 */
int g_method_this_pushed IN_DATA = 0;
int g_local_offset IN_DATA = 0;
/* [新增] va_list 支持: variadic 函数 prologue 把 6 个 GP 寄存器保存到 reg_save_area,
 * 把栈参数起始地址记为 overflow_arg_area. 这两个偏移(rbp-relative)在每次进入 variadic
 * 函数时更新. 简化: 不支持 variadic 函数嵌套调用 va_arg. */
int g_va_reg_save_off IN_DATA = 0;    /* reg_save_area 的 rbp-relative 偏移 (负数) */
int g_va_overflow_off IN_DATA = 0;    /* overflow_arg_area 的 rbp-relative 偏移 (正数, = 16 + (np-6)*8 若 np>6, 否则 16) */
/* [新增] #pragma pack(N): 影响 struct 字段对齐. 0 = 不限制 (用默认), 否则字段对齐 = min(原生对齐, pack) */
int g_pp_pack IN_DATA = 0;
/* [新增] 位字段 (bit-field) LVAL 跟踪: 当 parse_postfix 解析到 a.b 且 b 是位字段,
 * 且下一个 token 是赋值/自增/自减时, 栈顶保留 storage unit 地址 (PV_LVAL),
 * 并设置 g_bf_active=1 携带位宽与位偏移, 供 parse_assign / postfix ++ 使用.
 * parse_assign 必须在递归解析 rhs 之前消费此标志 (避免嵌套覆盖). */
int g_bf_active  IN_DATA = 0;   /* 1 = 当前 ExprRes 是位字段 LVAL */
int g_bf_width   IN_DATA = 0;   /* 位字段宽度 (1..32) */
int g_bf_offset  IN_DATA = 0;   /* storage unit 内起始位偏移 */
/* [新增] 位字段解析状态: 跨 struct 字段跟踪当前 storage unit.
 * static 变量须在 parse_struct_body 作用域外可见, 以便非位字段路径重置. */
static int s_bf_pos  = 0;   /* 当前 storage unit 内已用位数 */
static int s_bf_unit = -1;  /* 当前 storage unit 字节偏移; -1 = 需新 unit */

/* ========== 内存/字符串辅助 (无 libc, 自实现) ========== */
static int  g_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int  g_strcmp(const char *a, const char *b) { while(*a&&*a==*b){a++;b++;} return *(unsigned char*)a - *(unsigned char*)b; }
static int  g_strncmp(const char *a, const char *b, int n) { for(int i=0;i<n;i++){if(!a[i]||a[i]!=b[i]) return (unsigned char)a[i]-(unsigned char)b[i];} return 0; }
static void g_memcpy(char *d, const char *s, int n) { for(int i=0;i<n;i++) d[i]=s[i]; }
static void g_memset(char *d, char c, int n) { for(int i=0;i<n;i++) d[i]=c; }
/* [新增] 字符串 → double. 支持 [-]ddd.ddd[e±dd].
 * 实现: 整数部分 + 小数部分 + 指数部分. 不支持 NaN/Inf. */
static double g_strtod(const char *s) {
    double v = 0.0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    /* 整数部分 */
    while (*s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); s++; }
    /* 小数部分 */
    if (*s == '.') {
        s++;
        double f = 0.1;
        while (*s >= '0' && *s <= '9') { v += (*s - '0') * f; f /= 10.0; s++; }
    }
    /* 指数部分 */
    if (*s == 'e' || *s == 'E') {
        s++;
        int eneg = 0;
        if (*s == '-') { eneg = 1; s++; }
        else if (*s == '+') s++;
        int e = 0;
        while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
        double m = 1.0;
        for (int i = 0; i < e; i++) m *= 10.0;
        if (eneg) v /= m; else v *= m;
    }
    /* 后缀 f/F/l/L 跳过 (不影响值) */
    return neg ? -v : v;
}
/* [诊断] 串口调试开关: g_debug=0 时所有 dbg_com1 调用为空操作, 大幅提升性能.
 *   需要调试时改为 1. panic 路径不受此开关控制 (直接内联输出). */
static int g_debug IN_DATA = 0;
/* [诊断] 直接 COM1 串口输出单个字符, 不经过 API (API 可能有问题)。
 *   用于定位 gcc 崩溃位置: 串口看到哪个字母 = 执行到哪一步。 */
static void dbg_com1(char c) {
    if (!g_debug) return;
    __asm__ volatile(
        "movw $0x3FD, %%dx\n\t"        /* LSR (0x3F8+5) */
        "1: inb %%dx, %%al\n\t"
        "testb $0x20, %%al\n\t"        /* THR 空? */
        "jz 1b\n\t"
        "movw $0x3F8, %%dx\n\t"        /* COM1 data */
        "movb %0, %%al\n\t"
        "outb %%al, %%dx\n\t"
        : : "r"(c) : "rax", "rdx", "memory"
    );
}
static void dbg_com1_hex(unsigned long v) {
    static const char h[] = "0123456789ABCDEF";
    dbg_com1('0'); dbg_com1('x');
    int started = 0;
    for (int i = 15; i >= 0; i--) {
        char c = h[(v >> (i*4)) & 0xF];
        if (c != '0' || started || i == 0) { dbg_com1(c); started = 1; }
    }
    dbg_com1(' ');
}
/* [强制] 直接写 COM1 THR, 轮询 LSR 等待就绪, 不受 g_debug 控制, 仅 panic 路径用. */
static void com1_raw(char c) {
    __asm__ volatile(
        "movw $0x3FD, %%dx\n"
        "1: inb %%dx, %%al\n"
        "testb $0x20, %%al\n"
        "jz 1b\n"
        "movw $0x3F8, %%dx\n"
        "movb %0, %%al\n"
        "outb %%al, %%dx\n"
        : : "r"(c) : "rax", "rdx", "memory"
    );
}
static void com1_str(const char *s) { while (s && *s) com1_raw(*s++); }
static void com1_dec(int v) {
    char b[16]; int n=0;
    if (v==0) { com1_raw('0'); return; }
    if (v<0)  { com1_raw('-'); v=-v; }
    while(v){ b[n++]='0'+v%10; v/=10; }
    while(n--) com1_raw(b[n]);
}
static void com1_hex(unsigned long v) {
    const char h[]="0123456789ABCDEF";
    int started=0; com1_str("0x");
    for(int i=15;i>=0;i--){
        char c=h[(v>>(i*4))&0xF];
        if (c!='0'||started||i==0){ com1_raw(c); started=1; }
    }
}
static void *g_malloc(int sz)    { return API->malloc ? API->malloc((unsigned long)sz) : 0; }
static void  g_free(void *p)     { if (API->free) API->free(p); }
static void  g_putc(char c)      { if (API->put_char) API->put_char(c); }
static void  g_print(const char *s) { if (API->print) API->print(s); }
static void  g_phex(unsigned long v, int w) {
    char h[]="0123456789ABCDEF"; char b[20]; int n=0;
    if (v==0){ for(int i=0;i<w-1;i++) g_putc('0'); g_putc('0'); return; }
    while(v){ b[n++]=h[v&0xF]; v>>=4; }
    if (n<w) for(int i=n;i<w;i++) g_putc('0');
    for(int i=n-1;i>=0;i--) g_putc(b[i]);
}
static void g_print_int(int v) {
    if (v<0){ g_putc('-'); v=-v; }
    char b[20]; int n=0; if(v==0) b[n++]='0';
    while(v){ b[n++]='0'+v%10; v/=10; }
    for(int i=n-1;i>=0;i--) g_putc(b[i]);
}
static void g_panic(const char *m) {
    /* [增强] 错误信息写 COM1 (串口直接可见) + 屏幕 (g_print).
     * 注意: 此处不可引用 L/T_* (定义在后面), lexer 上下文由调用方 (L_expect 等) 先行输出.
     * [关键] 用 int3 替代 hlt 死循环: 内核 efs_active=1 → 异常处理器恢复内核状态,
     * gcc 进程终止, shell 继续可用 (hlt 会冻结整机, 鼠标/键盘全无响应). */
    com1_str("\r\n[gcc panic] "); com1_str(m ? m : "(null)");
    com1_str("\r\n");
    g_print("FATAL: "); g_print(m ? m : "(null)"); g_print("\n");
    __asm__ volatile("int3");
    for(;;) __asm__("hlt");  /* 兜底: 若内核未终止本任务 */
}
static void *g_realloc(void *old, int oldsz, int newsz) {
    if (newsz <= 0) { dbg_com1('!'); dbg_com1('R'); dbg_com1('0'); g_panic("realloc newsz<=0"); }  /* newsz==0:终止 */
    if (!API || !API->malloc) { dbg_com1('?'); g_panic("API unavailable"); }  /* API 不可用 */
    void *n = g_malloc(newsz);
    if (!n) { dbg_com1('#'); dbg_com1('O'); dbg_com1('M'); g_panic("out of memory"); }  /* 堆耗尽 */
    if (old && oldsz>0) { int cp=(oldsz<newsz)?oldsz:newsz; if (cp>0) g_memcpy(n,(char*)old,cp); }
    g_free(old);
    return n;
}

/* ========== 词法分析器 ========== */
typedef enum {
    T_EOF=0, T_IDENT, T_NUM, T_NUM_FLOAT, T_STR,
    T_INT, T_CHAR, T_VOID, T_LONG, T_SHORT, T_FLOAT_KW, T_DOUBLE_KW,
    T_UNSIGNED, T_SIGNED,
    T_STRUCT, T_TYPEDEF, T_RETURN, T_IF, T_ELSE, T_WHILE, T_FOR,
    T_BREAK, T_CONTINUE, T_SIZEOF, T_CLASS, T_NEW, T_DELETE,
    T_PUBLIC, T_PRIVATE,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACK, T_RBRACK,
    T_SEMI, T_COMMA, T_DOT, T_ARROW, T_COLON, T_QUEST,
    T_ASSIGN, T_PLUS_ASSIGN, T_MINUS_ASSIGN,
    T_EQ, T_NEQ, T_LT, T_GT, T_LE, T_GE,
    T_AND, T_OR, T_NOT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT, T_AMP, T_BOR, T_XOR, T_BNOT,
    T_SHL, T_SHR,
    T_INC, T_DEC,
    T_SCOPE,    /* :: (C++ scope resolution, 这里仅用在类名::方法名) */
    T_TILDE,    /* ~ (析构/位取反) */
    /* [新增] switch/case/default/do/enum/union/goto + 存储类 + 复合赋值 */
    T_SWITCH, T_CASE, T_DEFAULT, T_DO, T_ENUM, T_UNION, T_GOTO,
    T_CONST, T_STATIC, T_EXTERN, T_VOLATILE, T_REGISTER,
    T_STAR_ASSIGN, T_SLASH_ASSIGN, T_PERCENT_ASSIGN,
    T_AMP_ASSIGN, T_BOR_ASSIGN, T_XOR_ASSIGN,
    T_SHL_ASSIGN, T_SHR_ASSIGN,
    T_HASH, T_POUND,  /* # (预处理指令, 词法层简单跳过整行) */
    /* [新增] C11/C23 关键字 */
    T_STATIC_ASSERT,   /* _Static_assert */
    T_GENERIC,          /* _Generic */
    T_ALIGNOF,          /* _Alignof */
    T_ALIGNAS           /* _Alignas */
} TOK;

/* [BSS 崩溃修复] Lexer 强制进 .data: 零初始化丢到 .bss, 超过 bin 覆盖范围 → 强制 section .data */
static struct {
    const char *src; int pos; int len;
    TOK tok;
    int num;          /* T_NUM 值 (限制 32 位 signed) */
    double fval;      /* T_NUM_FLOAT 值 */
    char sbuf[256];   /* T_IDENT / T_STR 内容 */
    int slen;
    int line;
    int tok_start;    /* 当前 token 在 src 中的起始位置 (用于浮点字面量回读) */
} L IN_DATA = {0};

static void L_init(const char *src, int len) {
    L.src = src; L.pos = 0; L.len = len; L.tok = 0; L.num = 0;
    L.sbuf[0]=0; L.slen=0; L.line=1;
}
static int  L_peek(int off) { int p=L.pos+off; return (p>=0&&p<L.len)?(unsigned char)L.src[p]:0; }
static int  L_adv(void)    { if(L.pos<L.len){ if(L.src[L.pos]=='\n') L.line++; return (unsigned char)L.src[L.pos++];} return 0; }

static void L_skip_ws(void) {
    for(;;) {
        int c = L_peek(0);
        if (c==' '||c=='\t'||c=='\n'||c=='\r') { L_adv(); continue; }
        if (c=='/') {
            if (L_peek(1)=='/') { L_adv(); L_adv();
                while(L_peek(0)&&L_peek(0)!='\n') L_adv(); continue; }
            if (L_peek(1)=='*') { L_adv(); L_adv();
                for(;;) {
                    if (L_peek(0)=='*' && L_peek(1)=='/') { L_adv(); L_adv(); break; }
                    if (L_peek(0)==0) break;
                    L_adv();
                }
                continue;
            }
            break;
        }
        break;
    }
}
static TOK L_next(void) {
    L_skip_ws();
    L.tok_start = L.pos;   /* 记录当前 token 起始位置, 供浮点字面量回读 */
    L.slen=0;
    int c = L_peek(0);
    if (c==0) return L.tok=T_EOF;
    /* 标识符/关键字 */
    if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_') {
        while ((L_peek(0)>='a'&&L_peek(0)<='z')||(L_peek(0)>='A'&&L_peek(0)<='Z')||
               (L_peek(0)>='0'&&L_peek(0)<='9')||L_peek(0)=='_')
            L.sbuf[L.slen++] = (char)L_adv();
        L.sbuf[L.slen]=0;
        const char *k=L.sbuf;
        if      (g_strcmp(k,"int")==0)      return L.tok=T_INT;
        else if (g_strcmp(k,"char")==0)     return L.tok=T_CHAR;
        else if (g_strcmp(k,"void")==0)     return L.tok=T_VOID;
        else if (g_strcmp(k,"long")==0)     return L.tok=T_LONG;
        else if (g_strcmp(k,"short")==0)    return L.tok=T_SHORT;
        else if (g_strcmp(k,"float")==0)    return L.tok=T_FLOAT_KW;
        else if (g_strcmp(k,"double")==0)   return L.tok=T_DOUBLE_KW;
        else if (g_strcmp(k,"unsigned")==0) return L.tok=T_UNSIGNED;
        else if (g_strcmp(k,"signed")==0)   return L.tok=T_SIGNED;
        else if (g_strcmp(k,"struct")==0)   return L.tok=T_STRUCT;
        else if (g_strcmp(k,"typedef")==0)  return L.tok=T_TYPEDEF;
        else if (g_strcmp(k,"return")==0)   return L.tok=T_RETURN;
        else if (g_strcmp(k,"if")==0)       return L.tok=T_IF;
        else if (g_strcmp(k,"else")==0)     return L.tok=T_ELSE;
        else if (g_strcmp(k,"while")==0)    return L.tok=T_WHILE;
        else if (g_strcmp(k,"for")==0)      return L.tok=T_FOR;
        else if (g_strcmp(k,"break")==0)    return L.tok=T_BREAK;
        else if (g_strcmp(k,"continue")==0) return L.tok=T_CONTINUE;
        else if (g_strcmp(k,"sizeof")==0)   return L.tok=T_SIZEOF;
        else if (g_strcmp(k,"class")==0)    return L.tok=T_CLASS;
        else if (g_strcmp(k,"new")==0)      return L.tok=T_NEW;
        else if (g_strcmp(k,"delete")==0)   return L.tok=T_DELETE;
        else if (g_strcmp(k,"public")==0)   return L.tok=T_PUBLIC;
        else if (g_strcmp(k,"private")==0)  return L.tok=T_PRIVATE;
        /* [新增] C 关键字 */
        else if (g_strcmp(k,"switch")==0)    return L.tok=T_SWITCH;
        else if (g_strcmp(k,"case")==0)      return L.tok=T_CASE;
        else if (g_strcmp(k,"default")==0)   return L.tok=T_DEFAULT;
        else if (g_strcmp(k,"do")==0)        return L.tok=T_DO;
        else if (g_strcmp(k,"enum")==0)     return L.tok=T_ENUM;
        else if (g_strcmp(k,"union")==0)     return L.tok=T_UNION;
        else if (g_strcmp(k,"goto")==0)      return L.tok=T_GOTO;
        else if (g_strcmp(k,"const")==0)     return L.tok=T_CONST;
        else if (g_strcmp(k,"static")==0)    return L.tok=T_STATIC;
        else if (g_strcmp(k,"extern")==0)    return L.tok=T_EXTERN;
        else if (g_strcmp(k,"volatile")==0)  return L.tok=T_VOLATILE;
        else if (g_strcmp(k,"register")==0)  return L.tok=T_REGISTER;
        /* [新增] C11/C23 关键字: _Static_assert / _Generic / _Alignof / _Alignas */
        else if (g_strcmp(k,"_Static_assert")==0) return L.tok=T_STATIC_ASSERT;
        else if (g_strcmp(k,"_Generic")==0)       return L.tok=T_GENERIC;
        else if (g_strcmp(k,"_Alignof")==0)       return L.tok=T_ALIGNOF;
        else if (g_strcmp(k,"_Alignas")==0)        return L.tok=T_ALIGNAS;
        return L.tok=T_IDENT;
    }
    /* 数字: 整数 + 浮点 */
    if (c>='0'&&c<='9') {
        int base=10;
        int is_float = 0;
        if (c=='0' && (L_peek(1)=='x'||L_peek(1)=='X')) { L_adv(); L_adv(); base=16; }
        long v=0;
        for(;;) {
            int ch=L_peek(0); int d=-1;
            if (ch>='0'&&ch<='9') d=ch-'0';
            else if (base==16 && ch>='a'&&ch<='f') d=ch-'a'+10;
            else if (base==16 && ch>='A'&&ch<='F') d=ch-'A'+10;
            if (d<0||d>=base) break;
            v=v*base+d; L_adv();
        }
        /* 浮点: 后面跟 '.' 或 'e'/'E' (十进制时) */
        if (base == 10) {
            if (L_peek(0)=='.') {
                is_float = 1;
                L_adv();  /* . */
                while (L_peek(0)>='0'&&L_peek(0)<='9') L_adv();
            }
            if (L_peek(0)=='e'||L_peek(0)=='E') {
                is_float = 1;
                L_adv();
                if (L_peek(0)=='+'||L_peek(0)=='-') L_adv();
                while (L_peek(0)>='0'&&L_peek(0)<='9') L_adv();
            }
        }
        /* 后缀: f/F (float), l/L (long double - 简化视为 double) */
        if (L_peek(0)=='f'||L_peek(0)=='F') { is_float = 1; L_adv(); }
        else if (L_peek(0)=='l'||L_peek(0)=='L') { L_adv(); }
        if (is_float) {
            /* 把整数字符串重新解析为 double.
             * 简化: 我们用一个自实现的 atod (字符串→double) */
            /* 重新读取本次 token 的字符范围: 从 L.src + (开始位置) 到 当前 pos */
            /* 为简化, 我们用 strtod 替代品: 遍历已经读入的字符.
             * 重新扫描原 src 找数字起始. */
            /* 直接在 L.sbuf 里重建字符串 (L.sbuf 是 str 缓冲, 也可复用为 num 缓冲) */
            /* 我们把本次 token 文本拷贝到 L.sbuf, 然后 atod 解析 */
            int start_pos = L.tok_start;
            int end_pos = L.pos;
            int len = end_pos - start_pos;
            if (len > 63) len = 63;
            for (int i = 0; i < len; i++) L.sbuf[i] = L.src[start_pos + i];
            L.sbuf[len] = 0;
            L.fval = g_strtod(L.sbuf);
            return L.tok=T_NUM_FLOAT;
        }
        L.num=v; return L.tok=T_NUM;
    }
    /* 字符串/字符 */
    if (c=='"'||c=='\'') {
        int q = c=='\''?1:0;
        L_adv(); L.slen=0;
        while (L_peek(0)&&(q?L_peek(0)!='\'':L_peek(0)!='"')) {
            if (L_peek(0)=='\\') { L_adv(); int e=L_adv();
                switch(e){
                    case 'n': L.sbuf[L.slen++]='\n'; break;
                    case 't': L.sbuf[L.slen++]='\t'; break;
                    case 'r': L.sbuf[L.slen++]='\r'; break;
                    case '0': L.sbuf[L.slen++]='\0'; break;
                    case '\\':L.sbuf[L.slen++]='\\'; break;
                    case '"': L.sbuf[L.slen++]='"';  break;
                    case '\'':L.sbuf[L.slen++]='\''; break;
                    default: L.sbuf[L.slen++]=(char)e;
                }
            } else L.sbuf[L.slen++]=(char)L_adv();
            if (L.slen>250) break;
        }
        L_adv(); /* 闭引号 */
        if (q) { L.num=(unsigned char)(L.slen>0?L.sbuf[0]:0); return L.tok=T_NUM; }
        L.sbuf[L.slen]=0; return L.tok=T_STR;
    }
    L_adv();
    /* 多字符操作符 */
    switch(c) {
        case '(': return L.tok=T_LPAREN;
        case ')': return L.tok=T_RPAREN;
        case '{': return L.tok=T_LBRACE;
        case '}': return L.tok=T_RBRACE;
        case '[': return L.tok=T_LBRACK;
        case ']': return L.tok=T_RBRACK;
        case ';': return L.tok=T_SEMI;
        case ',': return L.tok=T_COMMA;
        case '.': return L.tok=T_DOT;
        case '?': return L.tok=T_QUEST;
        case '~': return L.tok=T_TILDE;
        case ':':
            if (L_peek(0)==':') { L_adv(); return L.tok=T_SCOPE; }
            return L.tok=T_COLON;
        case '=':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_EQ; }
            return L.tok=T_ASSIGN;
        case '+':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_PLUS_ASSIGN; }
            if (L_peek(0)=='+') { L_adv(); return L.tok=T_INC; }
            return L.tok=T_PLUS;
        case '-':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_MINUS_ASSIGN; }
            if (L_peek(0)=='-') { L_adv(); return L.tok=T_DEC; }
            if (L_peek(0)=='>') { L_adv(); return L.tok=T_ARROW; }
            return L.tok=T_MINUS;
        case '!':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_NEQ; }
            return L.tok=T_NOT;
        case '<':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_LE; }
            if (L_peek(0)=='<') { L_adv();
                if (L_peek(0)=='=') { L_adv(); return L.tok=T_SHL_ASSIGN; }
                return L.tok=T_SHL; }
            return L.tok=T_LT;
        case '>':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_GE; }
            if (L_peek(0)=='>') { L_adv();
                if (L_peek(0)=='=') { L_adv(); return L.tok=T_SHR_ASSIGN; }
                return L.tok=T_SHR; }
            return L.tok=T_GT;
        case '&':
            if (L_peek(0)=='&') { L_adv(); return L.tok=T_AND; }
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_AMP_ASSIGN; }
            return L.tok=T_AMP;
        case '|':
            if (L_peek(0)=='|') { L_adv(); return L.tok=T_OR; }
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_BOR_ASSIGN; }
            return L.tok=T_BOR;
        case '*':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_STAR_ASSIGN; }
            return L.tok=T_STAR;
        case '/':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_SLASH_ASSIGN; }
            return L.tok=T_SLASH;
        case '%':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_PERCENT_ASSIGN; }
            return L.tok=T_PERCENT;
        case '^':
            if (L_peek(0)=='=') { L_adv(); return L.tok=T_XOR_ASSIGN; }
            return L.tok=T_XOR;
        default : break;
    }
    g_print("lex: unknown char 0x"); g_phex(c,2); g_print("\n");
    return L.tok=T_EOF;
}
static int L_check(TOK t) { return L.tok==t; }
static TOK L_accept(TOK t) { if (L.tok==t){ L_next(); return 1;} return 0; }
static void L_expect(TOK t, const char *m) {
    if (L.tok != t) {
        com1_str("\r\n[L_expect fail] expected '"); com1_str(m ? m : "?");
        com1_str("' (tok="); com1_hex((unsigned long)t);
        com1_str("), got tok="); com1_hex((unsigned long)L.tok);
        com1_str(" line="); com1_dec(L.line);
        if (L.tok == T_IDENT || L.slen>0) {
            com1_str(" sbuf='");
            for (int i=0;i<L.slen && i<63 && L.sbuf[i];i++) com1_raw(L.sbuf[i]);
            com1_raw('\'');
        }
        com1_str("\r\n");
        g_print(m ? m : "?"); g_print(": line "); g_print_int(L.line); g_print("\n");
        g_panic("parse error");
    }
    L_next();
}

/* ========== 类型系统 ========== */
typedef enum { TY_VOID, TY_INT, TY_CHAR, TY_LONG, TY_PTR, TY_ARRAY, TY_STRUCT, TY_FUNC,
               TY_FLOAT, TY_DOUBLE, TY_LDOUBLE } TY_KIND;
typedef struct Type {
    TY_KIND kind;
    int size;       /* 字节数, 数组是单元素吗? 是整个数组大小, ptr/func = 8 */
    int align;
    struct Type *base;  /* PTR/ARRAY: 元素类型; FUNC: 返回类型 */
    /* ARRAY */
    int array_len;
    /* STRUCT */
    char sname[64];     /* 结构体名 */
    struct Field *fields; int nfields;
    /* FUNC */
    struct Type **params; int nparams; int is_vararg;
} Type;

typedef struct Field {
    char name[64];
    Type *type;
    int offset;
    int is_method;      /* struct 成员函数: 含 this 指针 */
    int is_ctor;        /* 构造函数 */
    int is_dtor;        /* 析构函数 */
    /* 位字段: bit_width > 0 表示是位字段, bit_offset 为在 storage unit 内的起始位 */
    int bit_width;      /* 0 = 非位字段; >0 = 位宽 (1..32) */
    int bit_offset;     /* 在 storage unit (byte offset) 内的起始位 */
    /* 方法的符号在全局符号表里以 "ClassName::MethodName" 命名 */
} Field;

/* [BSS 崩溃修复] 强制 IN_DATA 进 .data 段。注意: 必须显式 ={0}/=0/=NULL
 *   否则 GCC 把 tentative 定义仍扔回 .bss, 即使有 section(".data") 属性。 */
static Type *g_types IN_DATA = NULL;
static int g_types_n IN_DATA = 0, g_types_c IN_DATA = 0;
static Type *t_void IN_DATA = NULL, *t_int IN_DATA = NULL,
           *t_char IN_DATA = NULL, *t_long IN_DATA = NULL,
           *t_float IN_DATA = NULL, *t_double IN_DATA = NULL, *t_ldouble IN_DATA = NULL;
static Type *ty_new(TY_KIND k, int sz, int al) {
    if (g_types_n>=g_types_c){
        int old_used = g_types_n;
        int old_cap  = g_types_c;
        g_types_c = old_cap ? old_cap*2 : 32;
        g_types = g_realloc(g_types, old_used*sizeof(Type), g_types_c*sizeof(Type));
    }
    Type *t=&g_types[g_types_n++];
    t->kind=k; t->size=sz; t->align=al; t->base=0;
    t->array_len=0; t->sname[0]=0; t->fields=0; t->nfields=0;
    t->params=0; t->nparams=0; t->is_vararg=0;
    return t;
}
static Type *ty_ptr(Type *b)  { Type *t=ty_new(TY_PTR,8,8); t->base=b; return t; }
static Type *ty_arr(Type *b, int n) { Type *t=ty_new(TY_ARRAY,b->size*n,b->align); t->base=b; t->array_len=n; return t; }
static Type *ty_struct(const char *name) { Type *t=ty_new(TY_STRUCT,1,1); int i=0;while(name[i]&&i<63){t->sname[i]=name[i];i++;} t->sname[i]=0; return t; }
static Type *ty_func(Type *ret, Type **ps, int np, int va) { Type *t=ty_new(TY_FUNC,8,8); t->base=ret; t->nparams=np; t->is_vararg=va;
    if (np) { t->params=g_malloc(sizeof(Type*)*np); for(int i=0;i<np;i++) t->params[i]=ps[i]; }
    return t;
}
static int ty_is_int(Type *t){ return t->kind==TY_INT||t->kind==TY_CHAR||t->kind==TY_LONG; }
static int ty_is_float(Type *t){ return t->kind==TY_FLOAT||t->kind==TY_DOUBLE||t->kind==TY_LDOUBLE; }

/* [新增] _Generic 类型匹配: 简化规则
 *   - TY_ARRAY 视为 TY_PTR (数组到指针退化, 与 C 语义一致)
 *   - 基本类型: 同 kind 即匹配 (本编译器无独立 unsigned 类型, unsigned int == int)
 *   - 指针: 任意 TY_PTR 互相匹配 (简化, 不区分 base)
 *   - 结构体: 比较结构体名 */
static int generic_type_match(Type *a, Type *b) {
    if (!a || !b) return 0;
    int ka = (a->kind == TY_ARRAY) ? TY_PTR : a->kind;
    int kb = (b->kind == TY_ARRAY) ? TY_PTR : b->kind;
    if (ka != kb) return 0;
    if (ka == TY_STRUCT) return g_strcmp(a->sname, b->sname) == 0;
    return 1;
}
static int ty_is_num(Type *t){ return ty_is_int(t)||t->kind==TY_PTR||ty_is_float(t); } /* 支持算术 */

/* ========== 符号表 ========== */
typedef enum { SK_VAR, SK_FUNC, SK_TYPE, SK_CONST, SK_METHOD, SK_LABEL } SKIND;
typedef struct Symbol {
    char name[128];
    SKIND kind;
    Type *type;
    /* VAR: scope 0 = 全局 (地址=符号在二进制中的绝对地址)
             >0 = 局部 (rbp - offset) */
    int scope;
    long addr;        /* 全局: VA; 局部: rbp偏移; FUNC: text offset (待回填为VA) */
    int is_global;
    int defined;
    int reg;           /* 预留 (本编译器不做 reg alloc) */
    /* STRUCT 方法: this 参数位置已在 func params[0] 中 */
    struct Type *method_of;  /* method 属于哪个 struct */
} Symbol;

static Symbol *g_syms IN_DATA = NULL;
static int g_syms_n IN_DATA = 0, g_syms_c IN_DATA = 0;
static int g_scope_depth IN_DATA = 0;

static Symbol *sym_add(const char *name, SKIND k, Type *t) {
    if (g_syms_n>=g_syms_c){
        int old_used = g_syms_n;
        int old_cap  = g_syms_c;
        g_syms_c = old_cap ? old_cap*2 : 64;
        g_syms = g_realloc(g_syms, old_used*sizeof(Symbol), g_syms_c*sizeof(Symbol));
    }
    Symbol *s=&g_syms[g_syms_n++];
    int i=0; while(name[i]&&i<127){s->name[i]=name[i];i++;} s->name[i]=0;
    s->kind=k; s->type=t; s->scope=g_scope_depth; s->addr=0;
    s->is_global=(g_scope_depth==0); s->defined=0; s->method_of=0;
    return s;
}
static Symbol *sym_find(const char *name, int start_depth) {
    for (int i=g_syms_n-1;i>=0;i--)
        if (g_syms[i].scope<=start_depth && g_strcmp(g_syms[i].name,name)==0)
            return &g_syms[i];
    return 0;
}
static Symbol *sym_find_global(const char *name) {
    for (int i=0;i<g_syms_n;i++)
        if (g_syms[i].is_global && g_strcmp(g_syms[i].name,name)==0)
            return &g_syms[i];
    return 0;
}
static void sym_leave_scope(int depth) {
    while (g_syms_n>0 && g_syms[g_syms_n-1].scope>depth) g_syms_n--;
}

/* ========== 结构体注册表 (struct name -> Type*) ========== */
typedef struct { char name[64]; Type *type; } Sdef;
static Sdef *g_structs IN_DATA = NULL;
static int g_structs_n IN_DATA = 0, g_structs_c IN_DATA = 0;
static Type *struct_find(const char *nm) {
    for (int i=0;i<g_structs_n;i++) if (g_strcmp(g_structs[i].name,nm)==0) return g_structs[i].type;
    return 0;
}
static void struct_register(const char *nm, Type *t) {
    if (g_structs_n>=g_structs_c){
        int old_used = g_structs_n;
        int old_cap  = g_structs_c;
        g_structs_c = old_cap ? old_cap*2 : 16;
        g_structs = g_realloc(g_structs, old_used*sizeof(Sdef), g_structs_c*sizeof(Sdef));
    }
    int i=0; while(nm[i]&&i<63){g_structs[g_structs_n].name[i]=nm[i];i++;}
    g_structs[g_structs_n].name[i]=0; g_structs[g_structs_n].type=t; g_structs_n++;
}

/* 在 struct Type 中按字段名查找, 返回 Field* (没找到返回 0) */
static Field *struct_find_field(Type *st, const char *fname) {
    if (!st || st->kind != TY_STRUCT || !st->fields) return 0;
    for (int i = 0; i < st->nfields; i++) {
        if (g_strcmp(st->fields[i].name, fname) == 0) return &st->fields[i];
    }
    return 0;
}

/* ========== 代码生成器: 字节流缓冲区 (text + rodata + data + bss 段)
 * [关键修复 BSS 未清零导致崩溃]
 *   EFMOS 通过 objcopy 抽 .text/.rodata/.data → bin 不包含 .bss。内核加载 efcc.bin 到 VA,
 *   如果 bin 范围没覆盖 .bss → .bss 内容为物理内存上的随机垃圾。
 *   双保险: (a) _start 启动时 rep stosb 清零 [__bss_start, _end);
 *          (b) 全部全局显式初始化 (=0/=NULL/={0}) 并 IN_DATA 强制 .data 写入 bin。 */
static char *g_text IN_DATA = NULL; static int g_text_n IN_DATA = 0, g_text_c IN_DATA = 0;
static char *g_rodata IN_DATA = NULL; static int g_ro_n IN_DATA = 0, g_ro_c IN_DATA = 0;
static char *g_data IN_DATA = NULL;   static int g_data_n IN_DATA = 0, g_data_c IN_DATA = 0;
static int g_bss_len IN_DATA = 0;  /* 只记录长度, 内容由加载器清零 */

/* 产出文件的 load_addr (由命令行 -T 指定, 默认 0x1000000 - 16MB;
 * 注: 内核 run_efs 会读头里的 load_addr; 我们默认给 1MB 以上避免冲突) */
static long g_out_load_addr = 0x01000000;

/* 段布局: [.text] [.rodata] [.data] [.bss(未初始化不写入文件)]
 * 运行时 VA 分布: load_addr + 0 = .text start
 * [BSS 崩溃修复] 全部强制 IN_DATA + 显式初始化 */
static long va_text_base IN_DATA = 0, va_ro_base IN_DATA = 0,
           va_data_base IN_DATA = 0, va_bss_base IN_DATA = 0;
static int  va_text_sz IN_DATA = 0, va_ro_sz IN_DATA = 0, va_data_sz IN_DATA = 0;

/* 待回填 label 列表
 * [BSS 崩溃修复] 全部强制 IN_DATA + 显式初始化 */
typedef struct { long label_id; int patch_pos; int kind; /* 0: 4b rel32 / 1: 8b abs */ } Patch;
static Patch *g_patches IN_DATA = NULL;
static int g_patches_n IN_DATA = 0, g_patches_c IN_DATA = 0;
static long  g_labels_next IN_DATA = 0;
typedef struct { long label_id; long va; } LabelVA;
static LabelVA *g_labels IN_DATA = NULL;
static int g_labels_n IN_DATA = 0, g_labels_c IN_DATA = 0;

/* [COM1 trace fix] 记录每个 emit_com1_trace 写入 g_text 的起始偏移,
 *   供 relocation pass 跳过, 避免把 trace 字节 (66 BA F8 03 B0 cc EE)
 *   误识别为 movabs 指令 (其中包含 0x48, 0xB8~0xBF 字节).
 *   最多 256 个 trace 标记, 足够编译单个源文件使用. */
static int g_trace_pos[256] IN_DATA;
static int g_trace_n IN_DATA = 0;
/* [关键修复 #9 精确重定位表] 替代 byte-slide 扫描.
 * 旧方案: 滑窗遍历 g_text 找 "48 Bx" 字节模式 → 致命问题:
 *   1. 0x48 可能是 imm32/displacement 的一部分 (不是 REX 前缀), 刚好跟着 Bx → 假匹配
 *   2. 跨指令边界读 8 字节 imm → 读到后一条指令的 RET/C3 / mov cl,B1 等字节拼垃圾值
 *   3. 即使 mod=0 不写回, i+=10 会推进错位, 漏过真实 movabs → rodata/data 地址不被修正
 * 新方案: 每次 EmitMoviImm64 时, 把 REX.W 字节在 g_text 中的偏移 (imm64 从偏移+2 开始)
 *   记录到 g_reloc_entries[]. 重定位只遍历这些偏移, 100% 精准, 无假匹配, 无漏掉.
 * 注意: exit stub 里手动写的 movabs rcx, slot_va 也必须手动登记 (EmitMoviImm64 不经过).
 * [诊断增强] 每个 entry 同时记录调用来源 tag: (tag<<24) | rex_off, tag 为 ASCII 字符, 用于区分谁 emit 的这条 movabs.
 *   标签约定: 'S'=EmitMoviImm64(符号加载, 通用), 'R'=字符串 rodata 加载 (parse_primary T_STR), 'E'=exit stub 手动登记 movabs,
 *            'P'=printf/libc stub 的 API slot 加载, 'X'=其他 builtin stubs.
 * [修复 imm=0 错判] imm==0 可能是 NULL literal (不应该加 base) 也可能是 BSS 偏移 0 符号 (应该加 base).
 *   解决: 当 imm==0 进入 [0, g_bss_len) 分支时, 额外验证: g_syms[] 中确实存在某个 BSS 变量的 addr (修正前)==0.
 *   如果不存在这样的符号, 说明 imm=0 是纯 literal (如 NULL / 立即数 0), 跳过不写回. */
static int g_reloc_entries[1024] IN_DATA;
static int g_reloc_n IN_DATA = 0;
static void RelocRecordTag(int rex_offset, char tag) {
    if (g_reloc_n >= 1024) return;
    int enc = ((int)(unsigned char)tag << 24) | (rex_offset & 0x00FFFFFF);
    g_reloc_entries[g_reloc_n++] = enc;
}
#define reloc_record(off) RelocRecordTag((off), 'S')

static void buf_emit(char **buf, int *n, int *c, const char *src, int len) {
    if (!buf || !n || !c) { dbg_com1('~'); return; }
    if (len <= 0) return;
    if (!src) { dbg_com1('\\'); return; }
    while(*n+len > *c) {
        int newc = *c ? *c*2 : 1024;
        *buf = (char*)g_realloc(*buf, *n, newc);
        if (!*buf) { dbg_com1('@'); return; }
        *c = newc;
    }
    g_memcpy(*buf+*n, src, len);
    *n += len;
}
#define EMIT_T(x,l) buf_emit(&g_text,&g_text_n,&g_text_c,(char*)(x),(l))
#define EMIT_R(x,l) buf_emit(&g_rodata,&g_ro_n,&g_ro_c,(char*)(x),(l))
#define EMIT_D(x,l) buf_emit(&g_data,&g_data_n,&g_data_c,(char*)(x),(l))

static void emit_b(char b) { char x=(char)b; EMIT_T(&x,1); }
static void emit_w(int v) { char x[2]; x[0]=v&0xFF; x[1]=(v>>8)&0xFF; EMIT_T(x,2); }
static void emit_dw(int v) { char x[4]; x[0]=v&0xFF; x[1]=(v>>8)&0xFF; x[2]=(v>>16)&0xFF; x[3]=(v>>24)&0xFF; EMIT_T(x,4); }
static void emit_q(long v) { char x[8]; for(int i=0;i<8;i++) x[i]=(char)((v>>(i*8))&0xFF); EMIT_T(x,8); }

static long new_label(void) { return g_labels_next++; }
static void def_label(long id) {
    if (g_labels_n>=g_labels_c){
        int old_used = g_labels_n;
        int old_cap  = g_labels_c;
        g_labels_c = old_cap ? old_cap*2 : 64;
        g_labels = g_realloc(g_labels, old_used*sizeof(LabelVA), g_labels_c*sizeof(LabelVA));
    }
    g_labels[g_labels_n].label_id=id; g_labels[g_labels_n].va = va_text_base + g_text_n; g_labels_n++;
}
static long label_va(long id) {
    for (int i=0;i<g_labels_n;i++) if (g_labels[i].label_id==id) return g_labels[i].va;
    return -1;
}
static void patch_rel32_here(long target_label) {
    /* 当前指令接下来要写的 4 字节 = (target - (here+4)).
     * 先占位 0, 记录 patch. 回填阶段再写真值。 */
    if (g_patches_n>=g_patches_c){
        int old_used = g_patches_n;
        int old_cap  = g_patches_c;
        g_patches_c = old_cap ? old_cap*2 : 64;
        g_patches = g_realloc(g_patches, old_used*sizeof(Patch), g_patches_c*sizeof(Patch));
    }
    Patch *p=&g_patches[g_patches_n++];
    p->label_id = target_label; p->patch_pos = g_text_n; p->kind = 0;
    int v=0; emit_dw(v);
}

/* 在 rodata 段放一个零结尾字符串, 返回其 VA */
static long rodata_puts(const char *s, int *out_len) {
    int n = g_strlen(s);
    long va = va_ro_base + g_ro_n;
    EMIT_R(s, n);
    char zero=0; EMIT_R(&zero,1);
    if (out_len) *out_len = n+1;
    return va;
}
/* 在 rodata 段放一个 8 字节 double, 返回其 VA (用于浮点字面量常量池) */
static long rodata_putd(double d) {
    long va = va_ro_base + g_ro_n;
    EMIT_R(&d, 8);
    return va;
}

/* ========== 机器码发射辅助: x86_64 常用指令 (System V ABI 栈机模型)
 * 栈机: 表达式结果都在栈顶 (8 字节, int 符号扩展),
 *       表达式求值完毕可 pop 到寄存器或继续运算.
 * 常用寄存器别名:
 *   rax = 累加器 / 返回值, rcx/rdx/rsi/rdi/r8/r9/r10/r11  = 临时 (调用者保存)
 *   rbx/rbp/r12-r15 = 被调用者保存
 * 参数传递: 1:rdi 2:rsi 3:rdx 4:rcx 5:r8 6:r9  (其余压栈右->左) */

/* REX 前缀 */
static void emit_rex(int w, int r, int x, int b) {
    if (w||(r&8)||(x&8)||(b&8)) emit_b(0x40 | (w<<3) | ((r>>3)&1)<<2 | ((x>>3)&1)<<1 | ((b>>3)&1));
}
/* ModRM: mod=3 (reg/reg); mod=0/2 (reg/[reg+disp8/disp32]) */
static void emit_modrm(int mod, int reg, int rm) {
    emit_b(((mod&3)<<6) | ((reg&7)<<3) | (rm&7));
}

/* mov reg, imm32 sign-ext */
static void emit_movi_imm32(int reg, int imm) {
    emit_rex(1,0,0,reg);
    emit_b(0xC7);
    emit_modrm(3,0,reg);
    emit_dw(imm);
}
/* mov reg, imm64 (绝对)
 * [关键修复] 不能用 `(int)imm == imm` 判断是否走 imm32 捷径:
 *   mov r/m64, imm32 (0xC7 /0) 对 imm32 做 sign-extend。
 *   当 imm ∈ [0x8000_0000, 0xFFFF_FFFF] (2GB 到 4GB-1) 时:
 *     (int)imm 是负数, (int)imm != imm (long) → 不会错走分支 ✓
 *   但当 imm ∈ [0x00000000_80000000, 0x00000000_7FFFFFFF] 的 "恰好 int32 等号" 情况是 OK 的.
 *   真正的 bug: 用户程序加载地址 0x01000000 (16MB) 时, data 段符号 VA 可能落在
 *   [0x00000000_80000000, 0x00000000_FFFFFFFF] (2GB-4GB) → long 是正数 ≈ 3GB, (int)imm 截断后是负数,
 *   (int)imm == imm 不成立 → 原代码已经不走 imm32! 但 **sign-extend** 的 imm32 在 0x8000..0xFFFF 段会
 *   sign-extend 到 0xFFFF_FFFF_xxxx_xxxx, 我们的 cr2 就是 0xFFFFFFFF1410C1C0 = sign-ext(0x1410C1C0)? No:
 *   0x1410C1C0 的最高位 bit31 = 0 (值 < 2GB), 所以 (int)0x1410C1C0 > 0, (int)imm == imm 为真,
 *   原代码走 imm32 → 结果 rax = 0x1410C1C0, *不* 会是高位全 F.
 *
 *   真实成因: prolog 后面 **缺少 BSS 清零**. 用户程序有未初始化全局变量 (BSS). gcc 子产物的 prolog
 *   (build_binary 中手写的启动码) 只 save frame + call main + jmp exit, 没有清零 va_bss_base
 *   开始的 g_bss_len 字节 → BSS 里是内核上次遗留垃圾 → 全局函数指针 / 字符串指针含随机值
 *   0xFFFF_FFFF_1410_C1C0 这种 "高位全 F" 就来源于此 (垃圾值), 而非 sign-extend 产物.
 *
 *   但为了保守, 依然禁止 任何地址 (>= 1MB) 走 imm32 捷径, 避免将来加载地址 >= 2GB 时出 sign-extend 锅:
 *   只对真正的小整数字面量 (-2^30 <= imm < 2^30) 用 imm32; 任何 >= 1MB 值一律 imm64. */
static void EmitMoviImm64(int reg, long imm) {
    /* [关键修复] 已禁用 imm32 捷径 (原: -1GB..1GB 用 MOV imm32 sign-ext)
     * 根因: EmitMoviImm64 的调用者绝大多数是地址 (字符串 rodata 偏移/全局 data+bss 偏移/符号 VA).
     *   - 地址在 codegen 时是 0-based 占位符 (0, 8, 16...), 这些占位符非常小, 原逻辑会走 imm32.
     *   - 但 relocation pass 只能识别 10-byte `movabs reg, imm64` (0x48 0xBx) 模式, 无法修改 7-byte imm32!
     *   - 导致字符串/全局变量指针永远不会被重定位 → 直接访问物理低地址 0x12 之类的错误地址.
     *   - 真正的小整数字面量 (5, 42, 100) 根本不走这个函数; 它们直接调 emit_movi_imm32.
     * 副作用: 方法符号 VA/API 槽地址 (0x9010 等) 也会多占 3 字节. 成本可忽略 (text 段总大小很少). */
    /* [修复 #9] 记录到精确重定位表 (imm 偏移 = 当前 g_text_n + 2, REX.W 在 +0) */
    reloc_record(g_text_n);
    emit_rex(1,0,0,reg);
    emit_b(0xB8 + (reg&7));
    emit_q(imm);
}
/* [前置声明] 用于运行时 COM1 追踪的小指令序列 emit, 定义在 generate_builtin_stubs 附近 (line 3715).
 * parse_primary/parse_postfix/parse_func_definition 等代码生成函数在它之前就需要调用. */
static void emit_com1_trace(char c);
/* mov dst, src (64b reg-reg) */
static void emit_mov_rr(int dst, int src) {
    if (dst==src) return;
    emit_rex(1,src,0,dst);
    emit_b(0x89);
    emit_modrm(3,src,dst);
}
/* mov [memreg + disp], reg  (64b) */
static void emit_mov_mr(int memreg, int disp, int reg) {
    int mod=2; int d=disp;
    if (disp==0 && memreg!=5) { mod=0; }
    else if ((char)disp == disp) { mod=1; d=(char)disp; }
    emit_rex(1,reg,0,memreg);
    emit_b(0x89);
    emit_modrm(mod,reg,memreg);
    if (mod==1) emit_b((char)d);
    else if (mod==2) emit_dw(d);
}
/* mov reg, [memreg + disp] (64b) */
static void emit_mov_rm(int reg, int memreg, int disp) {
    int mod=2; int d=disp;
    if (disp==0 && memreg!=5) { mod=0; }
    else if ((char)disp == disp) { mod=1; d=(char)disp; }
    emit_rex(1,reg,0,memreg);
    emit_b(0x8B);
    emit_modrm(mod,reg,memreg);
    if (mod==1) emit_b((char)d);
    else if (mod==2) emit_dw(d);
}
/* push reg */
static void emit_push(int r) { emit_b(0x50 + (r&7)); }
/* pop reg */
static void emit_pop(int r) { emit_b(0x58 + (r&7)); }
/* add/sub/imul reg, reg (64b) */
static void emit_alu_rr(int op, int dst, int src) {
    /* op: 0 add, 5 sub, 4 imul (两操作数 r/m*r→r) */
    emit_rex(1,src,0,dst);
    if (op==4) {
        emit_b(0x0F); emit_b(0xAF); /* imul r,r/m */
    } else {
        emit_b(0x01 + (op&1));     /* add 01 / sub 29 但用 01 + (op&1) ??? 改查表: */
        /* 简化: 用 REX+89 不行, 重写: op 0 add, 1 adc, 2 sbb, 3 or, 4 and, 5 sub, 6 xor, 7 cmp
         *       opcode RM = 0x01 + 2*op
         *       opcode MR = 0x03 + 2*op   (RM=reg->r/m, MR=r/m->reg)
         * 用 MR 方向 (src 是 r/m? 不, 我们要 dst = dst op src, dst 是 reg, src 是 reg) */
    }
    /* 重新来: 常用指令用字节直接写 */
}
/* --- 直接用更可控的发射函数 --- */
/* dst = dst + src (reg) */
static void emit_add_rr(int d,int s) { emit_rex(1,s,0,d); emit_b(0x01); emit_modrm(3,s,d); }
/* dst = dst - src */
static void emit_sub_rr(int d,int s) { emit_rex(1,s,0,d); emit_b(0x29); emit_modrm(3,s,d); }
/* dst = dst * src (64s) */
static void emit_imul_rr(int d,int s) { emit_rex(1,s,0,d); emit_b(0x0F); emit_b(0xAF); emit_modrm(3,s,d); }
/* dst = dst & src */
static void emit_and_rr(int d,int s) { emit_rex(1,s,0,d); emit_b(0x21); emit_modrm(3,s,d); }
/* dst = dst | src */
static void emit_or_rr(int d,int s)  { emit_rex(1,s,0,d); emit_b(0x09); emit_modrm(3,s,d); }
/* dst = dst ^ src */
static void emit_xor_rr(int d,int s) { emit_rex(1,s,0,d); emit_b(0x31); emit_modrm(3,s,d); }
/* cqo (rax -> rdx:rax sign-extend, 用于 idiv) */
static void emit_cqo(void) { emit_b(0x48); emit_b(0x99); }
/* idiv r (rax=rax:rdx/r; rdx=rem) */
static void emit_idiv_r(int r) { emit_rex(1,0,0,r); emit_b(0xF7); emit_modrm(3,7,r); }
/* sal/sar/shl/shr reg, cl  (cl 里是移位位数) */
static void emit_shift_cl(int op, int reg) {
    /* op: 0 sal/shl, 7 sar, 5 shr */
    emit_rex(1,0,0,reg);
    emit_b(0xD3);
    emit_modrm(3,op,reg);
}
/* [新增] shl/shr/sar reg, imm8  (避免占用 cl/rcx, 位字段提取用)
 * op 取 x86 标准 /n: 4=shl, 5=shr, 7=sar (注意: /0 是 rol 不是 shl) */
static void emit_shift_imm(int op, int reg, int imm) {
    if (imm == 0) return;
    emit_rex(1,0,0,reg);
    emit_b(0xC1);
    emit_modrm(3,op,reg);
    emit_b((char)imm);
}
/* [新增] mov dword [memreg + disp], reg32  (4 字节存储, 位字段写回用, 不踩相邻字段) */
static void emit_mov_mr32(int memreg, int disp, int reg) {
    int mod=2, d=disp;
    if (disp==0 && memreg!=5) { mod=0; }
    else if ((char)disp == disp) { mod=1; d=(char)disp; }
    /* 无 REX: 32 位操作数, 高 32 位清零 */
    emit_b(0x89);
    emit_modrm(mod,reg,memreg);
    if (mod==1) emit_b((char)d);
    else if (mod==2) emit_dw(d);
}
/* [新增] mov reg32, dword [memreg + disp]  (4 字节加载, 零扩展到 r64) */
static void emit_mov_rm32(int reg, int memreg, int disp) {
    int mod=2, d=disp;
    if (disp==0 && memreg!=5) { mod=0; }
    else if ((char)disp == disp) { mod=1; d=(char)disp; }
    /* 无 REX: 32 位加载, 零扩展到 r64 */
    emit_b(0x8B);
    emit_modrm(mod,reg,memreg);
    if (mod==1) emit_b((char)d);
    else if (mod==2) emit_dw(d);
}
/* test reg, reg; 然后根据 FLAGS 做比较 (cmp 也用 sub FLAGS) */
static void emit_test_rr(int d,int s) { emit_rex(1,s,0,d); emit_b(0x85); emit_modrm(3,s,d); }
/* cmp reg, reg (dst - src FLAGS) */
static void emit_cmp_rr(int d,int s) { emit_rex(1,s,0,d); emit_b(0x39); emit_modrm(3,s,d); }

/* neg reg (2s complement) */
static void emit_neg_r(int r) { emit_rex(1,0,0,r); emit_b(0xF7); emit_modrm(3,3,r); }
/* not reg */
static void emit_not_r(int r) { emit_rex(1,0,0,r); emit_b(0xF7); emit_modrm(3,2,r); }
/* inc/dec reg */
static void emit_inc_r(int r) { emit_rex(1,0,0,r); emit_b(0xFF); emit_modrm(3,0,r); }
static void emit_dec_r(int r) { emit_rex(1,0,0,r); emit_b(0xFF); emit_modrm(3,1,r); }

/* cdqe (eax→rax sign-extend) */
static void emit_cdqe(void) { emit_b(0x48); emit_b(0x98); }
/* movsx rax, byte[memreg+disp] */
static void emit_movsx_byte(int reg, int memreg, int disp) {
    int mod=2, d=disp;
    if (disp==0 && memreg!=5) { mod=0; }
    else if ((char)disp == disp) { mod=1; d=(char)disp; }
    emit_rex(1,reg,0,memreg);
    emit_b(0x0F); emit_b(0xBE);
    emit_modrm(mod,reg,memreg);
    if (mod==1) emit_b((char)d);
    else if (mod==2) emit_dw(d);
}
/* movzx rax, byte[...] */
static void emit_movzx_byte(int reg, int memreg, int disp) {
    int mod=2, d=disp;
    if (disp==0 && memreg!=5) { mod=0; }
    else if ((char)disp == disp) { mod=1; d=(char)disp; }
    emit_rex(1,reg,0,memreg);
    emit_b(0x0F); emit_b(0xB6);
    emit_modrm(mod,reg,memreg);
    if (mod==1) emit_b((char)d);
    else if (mod==2) emit_dw(d);
}
/* lea reg, [memreg + disp] (取地址, 不访问内存) */
static void emit_lea(int reg, int memreg, int disp) {
    int mod=2, d=disp;
    if (disp==0 && memreg!=5) { mod=0; }
    else if ((char)disp == disp) { mod=1; d=(char)disp; }
    emit_rex(1,reg,0,memreg);
    emit_b(0x8D);
    emit_modrm(mod,reg,memreg);
    if (mod==1) emit_b((char)d);
    else if (mod==2) emit_dw(d);
}

/* jmp/call rel32: 跳/调 target_label
 * ret */
static void emit_jmp(long t)        { emit_b(0xE9); patch_rel32_here(t); }
static void emit_call(long t)       { emit_b(0xE8); patch_rel32_here(t); }
static void emit_ret(void)          { emit_b(0xC3); }
/* 条件短跳: cc(4bits) - 0:o,1:no,2:b/c/nae,3:nb/c/ae,4:e/z,5:ne/nz,6:be/na,7:nbe/a,
 *                 8:s,9:ns,a:p/pe,b:np/po,c:l/nge,d:nl/ge,e:le/ng,f:nle/g */
static void emit_jcc(int cc, long t) {
    /* 统一用 0x0F 8x + rel32 形式 (占 6 字节, 便于 forward) */
    emit_b(0x0F); emit_b(0x80 + cc);
    patch_rel32_here(t);
}

/* call *reg (间接调用) */
static void emit_callr(int r) { emit_rex(1,0,0,r); emit_b(0xFF); emit_modrm(3,2,r); }

/* ========== x87 FPU 指令发射 (浮点运算) ==========
 * 浮点值在硬件栈上以 8 字节 double 形式存储 (与整数栈机槽位一致).
 * 算术时: pop 两个 8B 值到 rax/rcx 作为地址指针已不适用 (因为是值不是地址),
 *          我们用 rsp 直接寻址: 先 pop 出来后压回, 或直接用 [rsp] 寻址.
 * 简化策略: pop 8B 到 rax (双精度位模式), 把 rax 临时 push 回栈, 用 fld qword [rsp]
 *          操作完 fstp qword [rsp] 弹回 rax, 再 push rax.
 * 这里直接用 fld/fstp 配合 rsp 偏移, 避免经 rax 中转 (位模式无符号扩展问题). */

/* 计算 mod=0/1/2 + 可选 SIB, 发 modrm + disp. 仅用 [reg+disp] 形式 (无 SIB) */
static void emit_modrm_disp(int reg, int memreg, int disp) {
    int mod=2, d=disp;
    if (disp==0 && memreg!=5) { mod=0; }
    else if ((char)disp == disp) { mod=1; d=(char)disp; }
    /* rsp/r12 作为基址需要 SIB (scale=0,index=none,base=rm). 简化: rm=4 时强制 SIB */
    int need_sib = ((memreg & 7) == 4);
    emit_modrm(mod, reg, need_sib ? 4 : (memreg & 7));
    if (need_sib) emit_b(0x24); /* SIB: scale=00, index=100(none), base=memreg&7 */
    if (mod==1) emit_b((char)d);
    else if (mod==2) emit_dw(d);
}
/* fld qword [memreg + disp]  (压入 st0, FPU 栈深度 +1) */
static void emit_fld_qword(int memreg, int disp) {
    emit_b(0xDD); emit_modrm_disp(0, memreg, disp); /* DD /0 m64 real */
}
/* fstp qword [memreg + disp]  (弹出 st0 到内存, FPU 栈深度 -1) */
static void emit_fstp_qword(int memreg, int disp) {
    emit_b(0xDD); emit_modrm_disp(3, memreg, disp); /* DD /3 m64 real */
}
/* fild qword [memreg + disp]  (int64 → st0, 用于 int→float 转换) */
static void emit_fild_qword(int memreg, int disp) {
    emit_b(0xDF); emit_modrm_disp(5, memreg, disp); /* DF /5 m64 int */
}
/* fistp qword [memreg + disp]  (st0 → int64 内存, 弹出; 用于 float→int 转换) */
static void emit_fistp_qword(int memreg, int disp) {
    emit_b(0xDF); emit_modrm_disp(7, memreg, disp); /* DF /7 m64 int */
}
/* faddp st(1), st(0): st(1) = st(1) + st(0); pop → 结果在 st0 */
static void emit_faddp(void)  { emit_b(0xDE); emit_b(0xC1); }
/* fsubp st(1), st(0): st(1) = st(1) - st(0); pop */
static void emit_fsubp(void)  { emit_b(0xDE); emit_b(0xE9); }
/* fmulp st(1), st(0): st(1) = st(1) * st(0); pop */
static void emit_fmulp(void)  { emit_b(0xDE); emit_b(0xC9); }
/* fdivp st(1), st(0): st(1) = st(1) / st(0); pop */
static void emit_fdivp(void)  { emit_b(0xDE); emit_b(0xF9); }
/* fchs: st(0) = -st(0) (取反) */
static void emit_fchs(void)   { emit_b(0xD9); emit_b(0xE0); }
/* fabs: st(0) = |st(0)| */
static void emit_fabs(void)   { emit_b(0xD9); emit_b(0xE1); }
/* fucomip st(0), st(i): 比较 st0 与 sti 并设置 EFLAGS, 弹出 st0. 用 i=1 */
static void emit_fucomip(int i) { emit_b(0xDF); emit_b(0xE8 + (i&7)); }
/* fnstsw ax: 存储 FPU 状态字到 ax (老式比较路径用; fucomip 后一般不需要) */
static void emit_fnstsw_ax(void) { emit_b(0xDF); emit_b(0xE0); }
/* fldz: 压入 0.0 */
static void emit_fldz(void)  { emit_b(0xD9); emit_b(0xEE); }
/* fld1: 压入 1.0 */
static void emit_fld1(void)  { emit_b(0xD9); emit_b(0xE8); }
/* finit: 初始化 FPU (避免程序其他路径的 x87 状态污染) */
static void emit_finit(void) { emit_b(0x9B); emit_b(0xDB); emit_b(0xE3); }

/* 栈机: 把表达式求值结果 push 到硬件栈 (8 字节, 全部 64 位) */
static void sm_push_r(int reg) { emit_push(reg); }
/* 栈顶弹出到 reg */
static void sm_pop_r(int reg) { emit_pop(reg); }
/* 丢弃栈顶 (8 字节) */
static void sm_drop(int n) { if(n<=0) return; emit_movi_imm32(0,(int)n*8); emit_add_rr(4,0); } /* r4=rsp, r0=rax → 用 r4+=n*8 */

/* ========== 表达式求值 ========== */
/* 表达式返回: 结果在硬件栈顶 (8B), 或当 is_lvalue==1 时, 栈顶是左值的地址, 并且 *type_out 给出类型
 * 语义: 代码生成器写结果到栈顶, type 通过参数传出, lvalue 标志用于赋值/++ 等.
 * 表达式优先级: 递归下降. */

typedef enum { PV_LVAL, PV_RVAL } PVal;
typedef struct {
    Type *ty;
    PVal val;      /* LVAL=栈顶是地址; RVAL=栈顶是值 */
} ExprRes;

/* 前向声明 */
static ExprRes parse_expr(int rbp_stack_off, int min_prio);
static ExprRes parse_ternary(int rbp_stack_off);
static ExprRes parse_assign(int rbp_stack_off);
/* _Static_assert 辅助: 常量表达式求值 (定义在预处理段, 这里前向声明) */
static long pp_eval_const(const char *expr, int expr_len);
static int  subst_sizeof_in_expr(const char *src, int len, char *out, int out_sz);
static void parse_static_assert(void);  /* 顶层 / 块内通用 _Static_assert 解析 */

/* 优先级表 (C/C++ 简化版) */
static int op_prio(TOK t) {
    switch(t) {
        case T_OR:  return 1;
        case T_AND: return 2;
        case T_BOR: return 3;
        case T_XOR: return 4;
        case T_AMP: return 5;
        case T_EQ: case T_NEQ: return 6;
        case T_LT: case T_GT: case T_LE: case T_GE: return 7;
        case T_SHL: case T_SHR: return 8;
        case T_PLUS: case T_MINUS: return 9;
        case T_STAR: case T_SLASH: case T_PERCENT: return 10;
        default: return 0;
    }
}

/* [新增] 位字段加载: rcx = storage unit 地址 → rax = 位字段值 (有符号)
 * 流程: mov eax, [rcx] (4B 零扩展); shr rax, bit_offset; and rax, mask;
 *       符号扩展 (shl rax, 32-bw; sar rax, 32-bw). bw==32 时跳过掩码/扩展.
 * 不修改 rcx; 仅用 rax 与 r1(rcx? no — 用 rdx=2 作掩码寄存器避免与 rcx 冲突) */
static void gen_load_bitfield_rax(int bw, int bo) {
    /* rcx (reg 1) = addr; 用 rdx (reg 2) 作 scratch */
    emit_mov_rm32(0, 1, 0);          /* eax = dword [rcx] → rax 零扩展 */
    if (bo > 0) emit_shift_imm(5, 0, bo);   /* shr rax, bo */
    if (bw >= 32) {
        /* 全 32 位 int: 符号扩展到 64 位 (int 有符号) */
        emit_cdqe();
    } else {
        unsigned mask = (1u << bw) - 1u;
        emit_movi_imm32(2, (int)mask);      /* rdx = mask */
        emit_and_rr(0, 2);                   /* rax &= mask */
        /* 符号扩展: 把符号位移到 bit 31, 再算术右移回去 */
        int sh = 32 - bw;
        emit_shift_imm(4, 0, sh);           /* shl rax, sh */
        emit_shift_imm(7, 0, sh);           /* sar rax, sh */
    }
}

/* [新增] 位字段存储: rax = 新值, rcx = storage unit 地址 → 写 4 字节回 [rcx]
 * 流程: 截断新值到 bw 位并左移 bo 位 (保存到 r9); 读旧 unit, 清除位字段位,
 *       OR 新值, mov dword [rcx], eax; rax = 截断后的新值 (作为赋值表达式结果).
 * 不修改 rcx; 用 rdx(2) 作掩码, r9(9) 保存移位后的新值. */
static void gen_store_bitfield(int bw, int bo) {
    unsigned vmask = (bw >= 32) ? 0xFFFFFFFFu : ((1u << bw) - 1u);
    /* rax &= vmask */
    emit_movi_imm32(2, (int)vmask); emit_and_rr(0, 2);
    if (bo > 0) emit_shift_imm(4, 0, bo);    /* shl rax, bo */
    emit_mov_rr(9, 0);                       /* r9 = 移位后的新值 */
    /* 读旧 unit, 清除位字段位 */
    emit_mov_rm32(0, 1, 0);                  /* eax = old unit (4B) */
    unsigned cmask = ~(vmask << bo);         /* bo+bw<=32 保证无溢出 */
    emit_movi_imm32(2, (int)cmask); emit_and_rr(0, 2);  /* rax &= cmask */
    emit_or_rr(0, 9);                         /* rax |= 新值 */
    emit_mov_mr32(1, 0, 0);                   /* dword [rcx] = eax */
    /* 结果: 截断后的新值 (右移回去并符号扩展) */
    emit_mov_rr(0, 9);
    if (bo > 0) emit_shift_imm(5, 0, bo);    /* shr rax, bo */
    if (bw >= 32) {
        emit_cdqe();                          /* 全 32 位 int: 符号扩展结果 */
    } else {
        unsigned mask = (1u << bw) - 1u;
        emit_movi_imm32(2, (int)mask); emit_and_rr(0, 2);
        int sh = 32 - bw;
        emit_shift_imm(4, 0, sh); emit_shift_imm(7, 0, sh);
    }
}

/* 从栈顶取 2 个操作数 (y 先出, x 后出), 做 op, 结果回栈顶 */
static void gen_binop_int(int op) {
    /* op: 0+ 1- 2* 3/ 4% 5<< 6>> 7& 8| 9^
     *     10== 11!= 12< 13> 14<= 15>=
     * 所有输入视为 64 位 signed, 输出 64 位 signed (int 结果 int64, 比较结果 0/1) */
    sm_pop_r(1); /* r1 = y (rcx) */
    sm_pop_r(0); /* r0 = x (rax) */
    switch(op) {
        case 0: emit_add_rr(0,1); break;
        case 1: emit_sub_rr(0,1); break;
        case 2: emit_imul_rr(0,1); break;
        case 3: emit_cqo(); emit_idiv_r(1); break;          /* rax = x/y */
        case 4: emit_cqo(); emit_idiv_r(1); emit_mov_rr(0,2); break; /* rdx 余 → rax */
        case 5: emit_mov_rr(9,1);                           /* r9 = y (rcx->r9 存位数) */
                emit_mov_rr(1,0);                           /* rcx = x */
                emit_movi_imm32(0,0); emit_mov_rr(0,1);     /* rax = x */
                emit_mov_rr(1,9);                           /* rcx = y */
                emit_shift_cl(0,0); break;                   /* rax <<= cl */
        case 6: emit_mov_rr(9,1); emit_mov_rr(1,0);
                emit_movi_imm32(0,0); emit_mov_rr(0,1);
                emit_mov_rr(1,9); emit_shift_cl(5,0); break;
        case 7: emit_and_rr(0,1); break;
        case 8: emit_or_rr(0,1); break;
        case 9: emit_xor_rr(0,1); break;
        case 10: case 11: case 12: case 13: case 14: case 15: {
            long le=new_label(), dne=new_label();
            emit_cmp_rr(0,1); /* FLAGS = x - y */
            int cc=0;
            switch(op){ case 10:cc=4;break; case 11:cc=5;break; case 12:cc=12;break; case 13:cc=15;break; case 14:cc=14;break; case 15:cc=13;break; }
            emit_xor_rr(0,0);   /* rax=0 */
            emit_jcc(cc, le);
            emit_movi_imm32(0,1);
            def_label(le);
            if (op==11) { /* != 是 == 的反; 但上面 cc=5 就是 NZ */ }
            break;
        }
    }
    sm_push_r(0);
}

/* 浮点二元运算: 栈顶 [lhs 8B][rhs 8B] (top=rhs) → [result 8B]
 * 所有浮点值在硬件栈上以 8 字节 double 位模式存储 (即便类型是 float).
 * op: 0+ 1- 2* 3/  10== 11!= 12< 13> 14<= 15>=
 * 比较结果为 int 0/1 (与 C 语义一致: 比较产生 int) */
static void gen_binop_float(int op) {
    if (op >= 10 && op <= 15) {
        /* 浮点比较: 用 FPU 设置 EFLAGS, 然后类似整数比较设置 rax=0/1.
         * 栈上仍有 [lhs][rhs] (16B), 需丢弃后 push 结果.
         * 用 rcx 作 scratch (rax 保存结果) 来调整 rsp. */
        emit_fld_qword(4, 0);   /* fld qword [rsp] = rhs → st0 */
        emit_fld_qword(4, 8);   /* fld qword [rsp+8] = lhs → st0, rhs→st1 */
        emit_fucomip(1);       /* cmp st0(lhs) vs st1(rhs); pop st0; rhs→st0 */
        emit_b(0xDD); emit_b(0xD8); /* fstp st(0): 弹出 rhs, FPU 栈空 */
        long le=new_label();
        emit_xor_rr(0,0);      /* rax=0 */
        int cc=0;
        switch(op){ case 10:cc=4;break; case 11:cc=5;break; case 12:cc=2;break; case 13:cc=7;break; case 14:cc=6;break; case 15:cc=3;break; }
        emit_jcc(cc, le);
        emit_movi_imm32(0,1);
        def_label(le);
        /* 丢弃栈上 16B (lhs+rhs), 用 rcx 作 scratch 不破坏 rax */
        emit_movi_imm32(1, 16); emit_add_rr(4, 1);
        sm_push_r(0);          /* push 0/1 结果 */
    } else {
        emit_fld_qword(4, 0);  /* rhs → st0 */
        emit_fld_qword(4, 8);  /* lhs → st0, rhs→st1 */
        switch(op) {
            case 0: emit_faddp(); break;  /* st1=lhs+rhs, pop, st0=result */
            case 1: emit_fsubp(); break;  /* st1=lhs-rhs, pop */
            case 2: emit_fmulp(); break;  /* st1=lhs*rhs, pop */
            case 3: emit_fdivp(); break;  /* st1=lhs/rhs, pop */
            default: emit_faddp(); break;
        }
        emit_fstp_qword(4, 8); /* st0 → [rsp+8] (覆盖 lhs), FPU 弹出 */
        sm_drop(1);            /* rsp += 8, 丢 rhs, 顶 = result */
    }
}

/* unary: & * + - ! ~ ++ -- sizeof / cast (T) */
static ExprRes parse_unary(int rbp_stack_off) {
    ExprRes r; r.ty = t_int; r.val = PV_RVAL;
    if (L_check(T_AMP)) { /* &x: 左值取址 → 栈顶放地址 */
        L_next();
        ExprRes op = parse_unary(rbp_stack_off);
        if (op.val == PV_RVAL) { g_print("efcc: '&' requires lvalue at line "); g_print_int(L.line); g_print("\n"); g_panic("lvalue required"); }
        /* 栈顶已经是地址, 保持不变, 但类型变为 ptr of op.ty */
        r.ty = ty_ptr(op.ty); r.val = PV_RVAL;
        return r;
    }
    if (L_check(T_STAR)) { /* *p: 解引用, 栈顶是地址 → 类型变为 base, 产出 LVAL (地址留在栈顶) */
        L_next();
        ExprRes op = parse_unary(rbp_stack_off);
        if (op.val == PV_LVAL) { /* 栈顶是地址, 先 deref 取实际指针值 */
            sm_pop_r(0); emit_mov_rm(0,0,0); sm_push_r(0);
        }
        if (op.ty->kind != TY_PTR && op.ty->kind != TY_ARRAY) { g_panic("deref of non-ptr"); }
        r.ty = op.ty->base ? op.ty->base : t_int;
        r.val = PV_LVAL;
        return r;
    }
    if (L_check(T_MINUS)) {
        L_next();
        ExprRes op = parse_unary(rbp_stack_off);
        if (op.ty && ty_is_float(op.ty)) {
            /* 浮点取反: fld [rsp]; fchs; fstp [rsp] (栈顶就地取反) */
            emit_fld_qword(4, 0);   /* st0 = value */
            emit_fchs();             /* st0 = -st0 */
            emit_fstp_qword(4, 0);   /* 存回栈顶 */
        } else {
            sm_pop_r(0); emit_neg_r(0); sm_push_r(0);
        }
        r.ty = op.ty; r.val = PV_RVAL; return r;
    }
    if (L_check(T_PLUS)) { L_next(); return parse_unary(rbp_stack_off); }
    if (L_check(T_NOT)) {
        L_next();
        ExprRes op = parse_unary(rbp_stack_off);
        sm_pop_r(0); emit_test_rr(0,0);
        long nz=new_label(), done=new_label();
        emit_jcc(5,nz); emit_movi_imm32(0,1); emit_jmp(done); def_label(nz); emit_movi_imm32(0,0); def_label(done);
        sm_push_r(0); r.ty = t_int; r.val=PV_RVAL; return r;
    }
    if (L_check(T_TILDE)) {
        L_next();
        ExprRes op = parse_unary(rbp_stack_off);
        sm_pop_r(0); emit_not_r(0); sm_push_r(0);
        r.ty = op.ty; r.val = PV_RVAL; return r;
    }
    if (L_check(T_INC) || L_check(T_DEC)) {
        int inc = L_check(T_INC)?1:-1; L_next();
        ExprRes op = parse_unary(rbp_stack_off);
        if (op.val != PV_LVAL) g_panic("++ requires lvalue");
        if (op.ty && ty_is_float(op.ty)) {
            /* 浮点 prefix ++/--: new = old±1.0; 写回 [rcx] 并 push 新值.
             * 序列: fld [rcx]; fld1; faddp/fsubp → st0=new;
             *       fld st(0) 复制; fstp [rcx] 写回 (弹 1 份);
             *       手动 sub rsp,8; fstp [rsp] 把剩下 new 存到栈顶. */
            sm_pop_r(2);                  /* rcx = addr */
            emit_fld_qword(2, 0);         /* st0 = old */
            emit_fld1();                  /* st0 = 1.0, st1 = old */
            if (inc>0) emit_faddp(); else emit_fsubp(); /* st0 = new */
            emit_b(0xD9); emit_b(0xC0);  /* fld st(0): 复制 → st0=new, st1=new */
            emit_fstp_qword(2, 0);        /* *addr = new, FPU 弹 → st0=new */
            emit_movi_imm32(0, 8); emit_sub_rr(4, 0); /* rsp -= 8 (腾出 8B 槽) */
            emit_fstp_qword(4, 0);        /* fstp [rsp] = new, FPU 弹 → 栈顶 = new */
            r.ty = op.ty; r.val = PV_RVAL; return r;
        }
        /* 栈顶是地址; 读值到 rax, ±1, 写回, 把新值 push */
        sm_pop_r(2); /* r2=addr */
        emit_mov_rm(0,2,0); /* rax=*addr */
        if (inc>0) emit_inc_r(0); else emit_dec_r(0);
        emit_mov_mr(2,0,0); /* *addr=rax */
        sm_push_r(0);
        r.ty = op.ty; r.val=PV_RVAL; return r;
    }
    if (L_check(T_SIZEOF)) {
        L_next();
        /* sizeof(类型) 或 sizeof expr */
        Type *tp = 0; int was_paren = L_accept(T_LPAREN);
        /* 判断是类型还是表达式: 简化: 支持 "int","char","void","struct X","IDENT(typedef)", "*", "[]" */
        int issz=0;
        if (was_paren) {
            if (L_check(T_INT)||L_check(T_CHAR)||L_check(T_VOID)||L_check(T_LONG)||L_check(T_FLOAT_KW)||L_check(T_DOUBLE_KW)||L_check(T_STRUCT)||L_check(T_IDENT)) {
                /* 尝试解析类型 */
                TOK sv = L.tok; char nm[128]={0};
                if (L_check(T_FLOAT_KW)) { L_next(); if (L_check(T_RPAREN)) { L_next(); emit_movi_imm32(0,t_float->size); sm_push_r(0); r.ty=t_int; r.val=PV_RVAL; issz=1; } }
                else if (L_check(T_DOUBLE_KW)) { L_next(); if (L_check(T_RPAREN)) { L_next(); emit_movi_imm32(0,t_double->size); sm_push_r(0); r.ty=t_int; r.val=PV_RVAL; issz=1; } }
                else if (L_check(T_IDENT)||L_check(T_STRUCT)){
                    int k=0;
                    if (L_check(T_STRUCT)) { L_next(); nm[k++]='S'; nm[k++]=':'; }
                    if (L_check(T_IDENT)){ int i=0;while(L.sbuf[i])nm[k++]=L.sbuf[i++]; L_next();}
                    while(L_accept(T_STAR)) nm[k++]='*';
                    if (L_check(T_LBRACK)){ L_next(); int al=L.num; L_next(); L_next(); nm[k++]='['; }
                    if (L_check(T_RPAREN)) {
                        Type *t2=0;
                        if (g_strncmp(nm,"S:",2)==0) { t2 = struct_find(nm+2); if(!t2){ t2=ty_struct(nm+2); struct_register(nm+2,t2);} }
                        else {
                            if (g_strcmp(nm,"int")==0) t2=t_int;
                            else if (g_strcmp(nm,"char")==0) t2=t_char;
                            else if (g_strcmp(nm,"void")==0) t2=t_void;
                            else if (g_strcmp(nm,"long")==0) t2=t_long;
                            else { Symbol *sy = sym_find(nm, g_scope_depth); if (sy && sy->kind==SK_TYPE) t2=sy->type; }
                        }
                        if (t2) { L_next(); emit_movi_imm32(0,t2->size); sm_push_r(0); r.ty=t_int; r.val=PV_RVAL; issz=1; }
                    }
                }
                (void)sv;
            }
        }
        if (!issz) {
            ExprRes op = was_paren ? parse_expr(rbp_stack_off,1) : parse_unary(rbp_stack_off);
            if (was_paren) L_expect(T_RPAREN,")");
            emit_movi_imm32(0,op.ty->size);
            sm_push_r(0); r.ty = t_int; r.val=PV_RVAL;
        }
        return r;
    }
    /* [新增] _Alignof(type): 返回类型对齐值 (语法必须带括号). */
    if (L_check(T_ALIGNOF)) {
        L_next();
        L_expect(T_LPAREN, "(");
        Type *tt = 0;
        if (L_check(T_STRUCT)) { L_next(); tt = struct_find(L.sbuf)?:ty_struct(L.sbuf); L_expect(T_IDENT,"struct name"); }
        else if (L_check(T_FLOAT_KW)) { tt = t_float; L_next(); }
        else if (L_check(T_DOUBLE_KW)) { tt = t_double; L_next(); }
        else if (L_check(T_CHAR)) { tt = t_char; L_next(); }
        else if (L_check(T_LONG)) { tt = t_long; L_next(); }
        else if (L_check(T_INT)) { tt = t_int; L_next(); }
        else if (L_check(T_VOID)) { tt = t_void; L_next(); }
        else if (L_check(T_IDENT)) { Symbol *sy=sym_find(L.sbuf,g_scope_depth); tt=sy?sy->type:t_int; L_next(); }
        else tt = t_int;
        while (L_accept(T_STAR)) tt = ty_ptr(tt);
        while (L_check(T_LBRACK)) {
            L_next(); int n = 0;
            if (L_check(T_NUM)) { n = L.num; L_next(); }
            L_expect(T_RBRACK, "]"); tt = ty_arr(tt, n);
        }
        L_expect(T_RPAREN, ")");
        emit_movi_imm32(0, tt ? tt->align : 1);
        sm_push_r(0); r.ty = t_int; r.val = PV_RVAL;
        return r;
    }
    /* [新增] _Generic(controlling-expr, type: assoc, ..., default: assoc)
     * C11 语义: 控制表达式不求值, 仅按其类型选择关联. 本简化实现仍 codegen
     * 控制表达式 (取其类型) 后丢弃结果值; 再 re-lex 匹配的关联表达式并 codegen. */
    if (L_check(T_GENERIC)) {
        L_next();
        L_expect(T_LPAREN, "(");
        ExprRes ctrl = parse_expr(rbp_stack_off, 1);
        Type *T = ctrl.ty ? ctrl.ty : t_int;
        L_expect(T_COMMA, ",");
        int matched_start = -1, matched_end = -1, have_match = 0;
        int default_start = -1, default_end = -1;
        for (;;) {
            if (L_check(T_DEFAULT)) {
                L_next();
                L_expect(T_COLON, ":");
                int bs = L.tok_start, d = 0;
                for (;;) {
                    if (L_check(T_EOF)) g_panic("_Generic: EOF in assoc");
                    if (L_check(T_COMMA) && d == 0) break;
                    if (L_check(T_RPAREN) && d == 0) break;
                    if (L_check(T_LPAREN)||L_check(T_LBRACK)||L_check(T_LBRACE)) { d++; L_next(); continue; }
                    if (L_check(T_RPAREN)||L_check(T_RBRACK)||L_check(T_RBRACE)) { if (d>0) d--; L_next(); continue; }
                    L_next();
                }
                default_start = bs; default_end = L.tok_start;
            } else {
                Type *tt = 0;
                if (L_check(T_INT)) { tt=t_int; L_next(); }
                else if (L_check(T_CHAR)) { tt=t_char; L_next(); }
                else if (L_check(T_VOID)) { tt=t_void; L_next(); }
                else if (L_check(T_LONG)) { tt=t_long; L_next(); }
                else if (L_check(T_FLOAT_KW)) { tt=t_float; L_next(); }
                else if (L_check(T_DOUBLE_KW)) { tt=t_double; L_next(); }
                else if (L_check(T_STRUCT)) { L_next(); tt=struct_find(L.sbuf)?:ty_struct(L.sbuf); L_expect(T_IDENT,"struct name"); }
                else if (L_check(T_IDENT)) { Symbol *sy=sym_find(L.sbuf,g_scope_depth); tt=sy?sy->type:t_int; L_next(); }
                else tt = t_int;
                while (L_accept(T_STAR)) tt = ty_ptr(tt);
                while (L_check(T_LBRACK)) { L_next(); int n=0; if(L_check(T_NUM)){n=L.num;L_next();} L_expect(T_RBRACK,"]"); tt=ty_arr(tt,n); }
                L_expect(T_COLON, ":");
                int bs = L.tok_start, d = 0;
                for (;;) {
                    if (L_check(T_EOF)) g_panic("_Generic: EOF in assoc");
                    if (L_check(T_COMMA) && d == 0) break;
                    if (L_check(T_RPAREN) && d == 0) break;
                    if (L_check(T_LPAREN)||L_check(T_LBRACK)||L_check(T_LBRACE)) { d++; L_next(); continue; }
                    if (L_check(T_RPAREN)||L_check(T_RBRACK)||L_check(T_RBRACE)) { if (d>0) d--; L_next(); continue; }
                    L_next();
                }
                if (!have_match && generic_type_match(T, tt)) {
                    matched_start = bs; matched_end = L.tok_start; have_match = 1;
                }
            }
            if (L_check(T_RPAREN)) break;
            L_accept(T_COMMA);
            if (L_check(T_RPAREN)) break;
        }
        L_expect(T_RPAREN, ")");
        /* 丢弃控制表达式的结果值 (codegen 已发生的副作用按 C11 应为不求值, 此处简化) */
        sm_drop(1);
        int sel_start, sel_end;
        if (have_match) { sel_start = matched_start; sel_end = matched_end; }
        else if (default_start >= 0) { sel_start = default_start; sel_end = default_end; }
        else {
            g_print("efcc: _Generic no matching association at line "); g_print_int(L.line); g_print("\n");
            g_panic("_Generic no match");
            sel_start = sel_end = 0;
        }
        /* re-lex 选中的关联表达式并 codegen (在临时副本上解析, 避免越界读到后续源) */
        {
            char tbuf[512];
            int tlen = sel_end - sel_start;
            if (tlen < 0) tlen = 0;
            if (tlen > 511) tlen = 511;
            g_memcpy(tbuf, L.src + sel_start, tlen);
            tbuf[tlen] = 0;
            /* 保存词法状态 */
            const char *sv_src = L.src; int sv_pos = L.pos, sv_len = L.len;
            TOK sv_tok = L.tok; int sv_num = L.num; double sv_fval = L.fval;
            int sv_slen = L.slen, sv_line = L.line, sv_tok_start = L.tok_start;
            char sv_sbuf[256]; g_memcpy(sv_sbuf, L.sbuf, 256);
            L_init(tbuf, tlen);
            L_next();
            ExprRes gr = parse_assign(rbp_stack_off);
            r.ty = gr.ty; r.val = gr.val;
            /* 恢复词法状态 (回到 _Generic 之后的 token) */
            L.src = sv_src; L.pos = sv_pos; L.len = sv_len;
            L.tok = sv_tok; L.num = sv_num; L.fval = sv_fval;
            L.slen = sv_slen; L.line = sv_line; L.tok_start = sv_tok_start;
            g_memcpy(L.sbuf, sv_sbuf, 256);
        }
        return r;
    }
    if (L_check(T_LPAREN)) {
        /* 强转或括号表达式: 如果里面是 type→cast, 否则 expr */
        L_next();
        /* 简单判断: 开头是 type-keyword 或 typedef ident */
        int is_cast=0;
        if (L_check(T_INT)||L_check(T_CHAR)||L_check(T_VOID)||L_check(T_LONG)||L_check(T_SHORT)||
            L_check(T_STRUCT)||L_check(T_UNSIGNED)||L_check(T_SIGNED)||
            L_check(T_FLOAT_KW)||L_check(T_DOUBLE_KW)) is_cast=1;
        if (L_check(T_IDENT)) {
            Symbol *sy = sym_find(L.sbuf, g_scope_depth);
            if (sy && sy->kind==SK_TYPE) is_cast=1;
        }
        if (is_cast) {
            Type *tt=0;
            if (L_check(T_STRUCT)) { L_next(); tt=struct_find(L.sbuf); if(!tt) tt=ty_struct(L.sbuf); L_expect(T_IDENT,"struct name"); }
            else if (L_check(T_IDENT)) { Symbol *sy=sym_find(L.sbuf,g_scope_depth); tt=sy?sy->type:t_int; L_next(); }
            else if (L_check(T_INT))   { tt=t_int;   L_next(); }
            else if (L_check(T_CHAR))  { tt=t_char;  L_next(); }
            else if (L_check(T_LONG))  { tt=t_long;  L_next(); }
            else if (L_check(T_SHORT)) { tt=t_int;   L_next(); }
            else if (L_check(T_FLOAT_KW))  { tt=t_float;  L_next(); }
            else if (L_check(T_DOUBLE_KW)) { tt=t_double; L_next(); }
            else if (L_check(T_UNSIGNED)||L_check(T_SIGNED)) {
                L_next();  /* 消费 unsigned/signed */
                if (L_check(T_INT)) { tt=t_int; L_next(); }
                else if (L_check(T_LONG)) { tt=t_long; L_next(); }
                else if (L_check(T_CHAR)) { tt=t_char; L_next(); }
                else if (L_check(T_SHORT)) { tt=t_int; L_next(); }
                else tt = t_int;
            }
            else tt=t_int;
            while (L_accept(T_STAR)) tt = ty_ptr(tt);
            /* 数组 cast: (int[10]) */
            while (L_check(T_LBRACK)) {
                L_next();
                int n = 0;
                if (L_check(T_NUM)) { n = L.num; L_next(); }
                L_expect(T_RBRACK, "]");
                tt = ty_arr(tt, n);
            }
            L_expect(T_RPAREN,")");
            /* [新增] 复合字面量: (type){init...}
             * 分配栈空间, 用 init 列表填充, 把首地址 push 到栈顶 */
            if (L_check(T_LBRACE)) {
                /* 计算总大小 */
                int lit_sz = 8;
                if (tt->kind == TY_ARRAY && tt->array_len > 0)
                    lit_sz = tt->array_len * 8;
                else if (tt->kind == TY_STRUCT && tt->size > 0)
                    lit_sz = tt->size;
                if (lit_sz < 8) lit_sz = 8;
                if (lit_sz > 8192) lit_sz = 8192;
                /* 在栈上分配 (复用 g_local_offset 机制) */
                g_local_offset += lit_sz;
                if (g_local_offset % 8) g_local_offset += 8 - (g_local_offset % 8);
                int base_off = g_local_offset;
                /* 解析 {init} */
                L_next();
                int elem_off = 0;
                if (!L_check(T_RBRACE)) for(;;) {
                    /* 设计符 (与局部聚合初始化一致) */
                    while (L_check(T_LBRACK) || L_check(T_DOT)) {
                        if (L_check(T_LBRACK)) {
                            L_next();
                            long idx = 0;
                            if (L_check(T_NUM)) { idx = L.num; L_next(); }
                            else if (L_check(T_MINUS) && L_peek(1)>='0' && L_peek(1)<='9') {
                                L_next(); idx = -L.num; L_next();
                            } else { ExprRes e = parse_expr(rbp_stack_off,1); sm_drop(1); }
                            L_expect(T_RBRACK, "]");
                            elem_off = (int)idx * 8;
                            if (elem_off < 0) elem_off = 0;
                        } else {
                            L_next();
                            char fname[128]; int fi = 0;
                            if (L_check(T_IDENT)) {
                                int si=0; while(L.sbuf[si] && fi<127) fname[fi++]=L.sbuf[si++];
                                fname[fi] = 0;
                                L_next();
                            } else fname[0] = 0;
                            Field *fld = struct_find_field(tt, fname);
                            if (fld) { elem_off = fld->offset; if (elem_off<0) elem_off=0; }
                        }
                        if (L_check(T_ASSIGN)) L_next();
                    }
                    ExprRes ie = parse_expr(rbp_stack_off, 1);
                    sm_pop_r(0);
                    emit_mov_mr(5, -(base_off + elem_off), 0);
                    elem_off += 8;
                    if (L_accept(T_COMMA)) {
                        if (L_check(T_RBRACE)) break;
                        continue;
                    }
                    break;
                }
                L_expect(T_RBRACE, "}");
                /* 把首地址 lea 出来 push 到栈顶
                 * 机器码 lea rax, [rbp - base_off]:
                 *   8 位 disp:  48 8D 45 disp8
                 *   32 位 disp: 48 8D 85 disp32 */
                {
                    int disp = -base_off;
                    if (disp >= -128 && disp <= 127) {
                        emit_b(0x48); emit_b(0x8D); emit_b(0x45); emit_b((char)disp);
                    } else {
                        emit_b(0x48); emit_b(0x8D); emit_b(0x85);
                        emit_b((char)(disp & 0xFF));
                        emit_b((char)((disp >> 8) & 0xFF));
                        emit_b((char)((disp >> 16) & 0xFF));
                        emit_b((char)((disp >> 24) & 0xFF));
                    }
                }
                sm_push_r(0);
                r.ty = tt; r.val = PV_RVAL;  /* 复合字面量是 rvalue */
                return r;
            }
            ExprRes op = parse_unary(rbp_stack_off);
            /* 浮点↔整数 转换需要实际代码生成:
             *   int → float/double: fild qword [rsp]; fstp qword [rsp]
             *   float/double → int: fld qword [rsp]; fistp qword [rsp] (向 0 截断)
             *   float → double / double → float: 已都是 8B double 存储, 仅类型标签变 (语义近似)
             * int↔int / ptr↔ptr 等: 仅类型标签变 (栈机内一律 8B) */
            if (ty_is_float(tt) && op.ty && ty_is_int(op.ty)) {
                /* int → double */
                emit_fild_qword(4, 0);  /* fild [rsp] → st0 */
                emit_fstp_qword(4, 0);  /* fstp [rsp] = double, FPU 弹 */
            } else if (ty_is_int(tt) && op.ty && ty_is_float(op.ty)) {
                /* double → int (向 0 截断) */
                emit_fld_qword(4, 0);   /* fld [rsp] → st0 */
                emit_fistp_qword(4, 0); /* fistp [rsp] = int64, FPU 弹 */
            }
            op.ty = tt;
            return op;
        }
        ExprRes op = parse_expr(rbp_stack_off, 1);
        L_expect(T_RPAREN,")");
        return op;
    }
    if (L_check(T_NEW)) {
        /* new Type / new Type[n] */
        L_next();
        Type *tt=0;
        if (L_check(T_STRUCT)) { L_next(); tt=struct_find(L.sbuf); L_expect(T_IDENT,"struct name"); }
        else if (L_check(T_INT)) { tt=t_int; L_next(); }
        else if (L_check(T_CHAR)) { tt=t_char; L_next(); }
        else if (L_check(T_LONG)) { tt=t_long; L_next(); }
        else { Symbol *sy=sym_find(L.sbuf,g_scope_depth); tt=sy?sy->type:t_int; L_next(); }
        int count = 1;
        if (L_accept(T_LBRACK)) {
            ExprRes n = parse_expr(rbp_stack_off, 1);
            sm_pop_r(0); L_expect(T_RBRACK,"]");
            /* 元素个数在 rax; 我们要 count = rax */
            /* 我们没有 count 变量; 直接把 rax * sizeof(type) 放到调用 malloc 的参数里 */
            emit_movi_imm32(1, tt->size); emit_imul_rr(0,1); count=-1; /* 标记 */
        } else {
            if (tt) emit_movi_imm32(0, tt->size);
        }
        /* rax = size; call API->malloc(rax) */
        emit_mov_rr(7, 0);                       /* rdi = size */
        /* thunk: call [API->malloc]  slot 24 → offset 24*8 = 192 */
        EmitMoviImm64(0, 0x9000 + 24*8);      /* rax = &API->malloc (结构体 slot 24) */
        emit_mov_rm(0,0,0);                     /* rax = API->malloc */
        emit_callr(0);
        sm_push_r(0);
        r.ty = ty_ptr(tt); r.val=PV_RVAL;
        return r;
    }
    if (L_check(T_DELETE)) { L_next(); ExprRes a = parse_unary(rbp_stack_off); sm_pop_r(0); emit_mov_rr(7,0);
        EmitMoviImm64(0,0x9000 + 25*8); emit_mov_rm(0,0,0); emit_callr(0);  /* API->free slot 25 */
        emit_movi_imm32(0,0); sm_push_r(0); r.ty=t_int; r.val=PV_RVAL; return r; }

    if (L_check(T_NUM)) { int v=L.num; L_next(); emit_movi_imm32(0,v); sm_push_r(0); r.ty=t_int; r.val=PV_RVAL; return r; }
    if (L_check(T_NUM_FLOAT)) {
        double v=L.fval; L_next();
        /* 把 double 位模式存到 rodata, 通过 mov rax,[addr] 加载位模式 */
        long va = rodata_putd(v);
        EmitMoviImm64(0, va);     /* rax = &double_const */
        emit_mov_rm(0, 0, 0);       /* rax = *(double*)addr (8B 位模式) */
        sm_push_r(0);
        r.ty=t_double; r.val=PV_RVAL; return r;
    }
    if (L_check(T_STR)) {
        /* [新增] 相邻字符串拼接: "abc" "def" → "abcdef" */
        char concat[1024]; int cl = 0;
        do {
            int i = 0;
            while (L.sbuf[i] && cl < 1023) concat[cl++] = L.sbuf[i++];
            L_next();
        } while (L_check(T_STR));
        concat[cl] = 0;
        int sl; long va = rodata_puts(concat, &sl);
        /* 用 'R' tag: 字符串加载 → 若 reloc 输出 R[] 则来源确认 */
        RelocRecordTag(g_text_n, 'R');
        { int reg=0; long imm=va;
          emit_rex(1,0,0,reg);
          emit_b(0xB8 + (reg&7));
          emit_q(imm); }
        emit_com1_trace('L');  /* 运行时: 字符串常量地址刚加载到 rax (马上 push) */
        sm_push_r(0);
        r.ty=ty_ptr(t_char); r.val=PV_RVAL; return r;
    }
    if (L_check(T_IDENT)) {
        char nm[128]; g_memcpy(nm,L.sbuf,128); L_next();
        /* [新增] va_arg(ap, T): 表达式, 取下一个可变参数值
         * 展开: 读 ap.gp_offset; 若 < 48, 从 reg_save_area + gp_offset 取;
         *       否则从 overflow_arg_area + (gp_offset-48) 取; gp_offset += 8
         * 简化 va_list 结构: 24 字节 { gp_offset(4B), fp_offset(4B), overflow_arg_area(8B), reg_save_area(8B) }
         * 但为简化我们只用一个 8 字节槽存 gp_offset, reg_save_area/overflow_arg_area 走全局变量. */
        if (g_strcmp(nm, "va_arg") == 0) {
            L_expect(T_LPAREN, "(");
            /* 解析 ap (必须是 lvalue, 栈顶是其地址) */
            ExprRes ap = parse_assign(rbp_stack_off);
            if (ap.val != PV_LVAL) g_panic("va_arg requires lvalue ap");
            /* ap 栈顶是 va_list 变量地址. 读 gp_offset 到 rax */
            sm_pop_r(2);  /* r2 = ap 地址 */
            emit_mov_rm(0, 2, 0);  /* rax = *ap = gp_offset */
            /* 第二个参数: 类型名 (不是表达式, 直接消费) */
            L_expect(T_COMMA, ",");
            /* 跳过类型名: 直到 ) */
            /* 简化: 类型名只支持基础类型 + * 修饰 */
            int nparen = 0;
            while (!(L_check(T_RPAREN) && nparen == 0)) {
                if (L_check(T_LPAREN)) nparen++;
                else if (L_check(T_RPAREN)) nparen--;
                L_next();
                if (L_check(T_EOF)) break;
            }
            L_expect(T_RPAREN, ")");
            /* 判断 gp_offset < 48: 若是, 走 reg_save_area; 否则 overflow_arg_area */
            long lab_reg = new_label(), lab_ov = new_label(), lab_done = new_label();
            emit_movi_imm32(1, 48); emit_cmp_rr(0, 1);  /* cmp rax, 48 */
            emit_jcc(2, lab_reg);   /* jb (CF=1, unsigned rax<48) → reg_save_area */
            emit_jmp(lab_ov);
            /* overflow_arg_area 路径: rax = gp_offset - 48; 地址 = overflow + rax */
            def_label(lab_ov);
            emit_movi_imm32(1, 48); emit_sub_rr(0, 1);  /* rax -= 48 */
            /* lea rcx, [rbp + g_va_overflow_off] */
            {
                int disp = g_va_overflow_off;
                if (disp >= -128 && disp <= 127) {
                    emit_b(0x48); emit_b(0x8D); emit_b(0x4D); emit_b((char)disp);
                } else {
                    emit_b(0x48); emit_b(0x8D); emit_b(0x8D);
                    emit_b((char)(disp & 0xFF));
                    emit_b((char)((disp >> 8) & 0xFF));
                    emit_b((char)((disp >> 16) & 0xFF));
                    emit_b((char)((disp >> 24) & 0xFF));
                }
            }
            emit_add_rr(1, 0);  /* rcx += rax (offset) */
            emit_mov_rm(0, 1, 0);  /* rax = *rcx = 参数值 */
            /* 更新 gp_offset: *ap = gp_offset + 8 */
            emit_mov_rm(1, 2, 0);  /* rcx = old gp_offset */
            emit_movi_imm32(3, 8); emit_add_rr(1, 3);
            emit_mov_mr(2, 1, 0);  /* *ap = new gp_offset */
            emit_jmp(lab_done);
            /* reg_save_area 路径: 地址 = reg_save_area + gp_offset */
            def_label(lab_reg);
            /* 重新读 gp_offset (上面 rax 被改了), 用原 *ap */
            emit_mov_rm(0, 2, 0);  /* rax = gp_offset */
            /* lea rcx, [rbp + g_va_reg_save_off] */
            {
                int disp = g_va_reg_save_off;
                if (disp >= -128 && disp <= 127) {
                    emit_b(0x48); emit_b(0x8D); emit_b(0x4D); emit_b((char)disp);
                } else {
                    emit_b(0x48); emit_b(0x8D); emit_b(0x8D);
                    emit_b((char)(disp & 0xFF));
                    emit_b((char)((disp >> 8) & 0xFF));
                    emit_b((char)((disp >> 16) & 0xFF));
                    emit_b((char)((disp >> 24) & 0xFF));
                }
            }
            emit_add_rr(1, 0);  /* rcx += rax (offset) */
            emit_mov_rm(0, 1, 0);  /* rax = *rcx = 参数值 */
            /* 更新 gp_offset */
            emit_mov_rm(1, 2, 0);  /* rcx = gp_offset */
            emit_movi_imm32(3, 8); emit_add_rr(1, 3);
            emit_mov_mr(2, 1, 0);  /* *ap = gp_offset + 8 */
            def_label(lab_done);
            sm_push_r(0);  /* 栈顶 = 取到的参数值 */
            r.ty = t_long; r.val = PV_RVAL;
            return r;
        }
        /* [新增] va_start(ap, last_named): 语句形式但语法上是表达式, 我们在此识别并展开
         * 设置 *ap = min(np, 6) * 8. 但 np 是上一个 variadic 函数的命名参数数, 我们没存.
         * 简化: 用 last_named 这个名字查符号表, 反推其在参数中的序号.
         * 实际上 va_start 的 last_named 是参数名, 我们查它的 stack offset (本地槽 offset).
         * 计算: gp_offset = (本地槽 offset / 8) * 8 = offset. 但本地槽 offset 是从 8 开始递增 8 的.
         * 所以 gp_offset = last_named 的 s->addr. 然后因为 reg_save_area 存了 6 个寄存器,
         * va_arg 取走的第一个可变参数应该是第 (np+1) 个 = offset np*8 + 8 = (np+1)*8.
         * 但我们用 reg_save_area 偏移方式, 所以 gp_offset 应初始化为 np*8 (即 last_named 的 addr).
         * 嗯, 实际上 va_start 应该让 va_arg 取下一个未命名参数. 所以 gp_offset = (last_named 序号+1) * 8 = s->addr + 8. */
        if (g_strcmp(nm, "va_start") == 0) {
            L_expect(T_LPAREN, "(");
            /* 解析 ap (lvalue, 栈顶是地址) */
            ExprRes ap = parse_assign(rbp_stack_off);
            if (ap.val != PV_LVAL) g_panic("va_start requires lvalue ap");
            L_expect(T_COMMA, ",");
            /* last_named 参数名 */
            char ln[128] = {0};
            if (L_check(T_IDENT)) {
                int i=0; while (L.sbuf[i] && i < 127) { ln[i] = L.sbuf[i]; i++; } ln[i] = 0;
                L_next();
            }
            L_expect(T_RPAREN, ")");
            /* 查 last_named 符号 → 它的本地槽 offset = i*8 (其中 i 是参数序号, 从 1 开始) */
            Symbol *lns = sym_find(ln, g_scope_depth);
            int gp_off_init = 8;  /* 默认 1 个参数 */
            if (lns) gp_off_init = lns->addr + 8;  /* 下一个参数的 offset */
            /* 写入 *ap = gp_off_init */
            sm_pop_r(2);  /* r2 = ap 地址 */
            emit_movi_imm32(0, gp_off_init);
            emit_mov_mr(2, 0, 0);
            /* push 一个 0 作为表达式值 (va_start 返回 void, 但我们简化为 0) */
            emit_movi_imm32(0, 0); sm_push_r(0);
            r.ty = t_void; r.val = PV_RVAL;
            return r;
        }
        /* [新增] va_end(ap): 空操作, 语法上接受一个参数 */
        if (g_strcmp(nm, "va_end") == 0) {
            L_expect(T_LPAREN, "(");
            ExprRes ap = parse_assign(rbp_stack_off);
            sm_drop(1);  /* 丢弃 ap */
            L_expect(T_RPAREN, ")");
            emit_movi_imm32(0, 0); sm_push_r(0);
            r.ty = t_void; r.val = PV_RVAL;
            return r;
        }
        /* [新增] va_copy(dst, src): 简化为 dst = src */
        if (g_strcmp(nm, "va_copy") == 0) {
            L_expect(T_LPAREN, "(");
            ExprRes dst = parse_assign(rbp_stack_off);
            ExprRes src = parse_assign(rbp_stack_off);
            /* 简化: 直接 memcpy 8 字节 (gp_offset) */
            if (dst.val != PV_LVAL || src.val != PV_LVAL) g_panic("va_copy requires lvalue");
            sm_pop_r(2); sm_pop_r(1);  /* r1=src, r2=dst */
            emit_mov_rm(0, 1, 0);  /* rax = *src */
            emit_mov_mr(2, 0, 0);  /* *dst = rax */
            L_expect(T_RPAREN, ")");
            emit_movi_imm32(0, 0); sm_push_r(0);
            r.ty = t_void; r.val = PV_RVAL;
            return r;
        }
        /* 可能是 struct 方法: ClassName::MethodName */
        if (L_check(T_SCOPE)) {
            L_next(); char mname[128]=""; g_memcpy(mname,L.sbuf,128);
            if (!L_check(T_IDENT)) g_panic("expected method name after ::");
            L_next();
            char full[256]; int n=0; int i=0;while(nm[i])full[n++]=nm[i++]; full[n++]=':'; full[n++]=':'; i=0;while(mname[i])full[n++]=mname[i++]; full[n]=0;
            Symbol *s=sym_find_global(full); if(!s) g_panic("unknown class method");
            /* 作为普通函数; 若接下来是 (, 则需要 this */
            if (L_check(T_LPAREN)) { /* 这里不处理, 交给 parse_primary 后 parse_postfix 处理 func call; 但我们没有方法的 this 隐式传参. 为了简化: full name 被作为 SK_FUNC, 但它第一参是 this*, 使用时用户得手动 &obj 传入. 为了语法方便, 我们不做隐式 this, 用户得显式传 this */
            }
            EmitMoviImm64(0, s->addr);
            sm_push_r(0); r.ty=s->type; r.val=PV_RVAL; return r;
        }
        Symbol *s = sym_find(nm, g_scope_depth);
        if (!s) { g_print("unknown ident: "); g_print(nm); g_print(" at line "); g_print_int(L.line); g_print("\n"); g_panic("undef"); }
        if (s->kind == SK_TYPE) g_panic("unexpected type as expr");
        if (s->kind == SK_FUNC) {
            /* 函数作为值: 函数地址 push */
            EmitMoviImm64(0, s->addr);
            sm_push_r(0); r.ty = s->type; r.val = PV_RVAL; return r;
        }
        /* VAR: 计算地址 → push → LVAL */
        if (s->is_global) {
            EmitMoviImm64(0, s->addr);
        } else {
            /* 局部: rbp - s->addr (s->addr 存的是偏移量正数) */
            emit_lea(0, 5, -(int)s->addr);  /* rbp = 5 */
        }
        sm_push_r(0);
        r.ty = s->type; r.val = PV_LVAL; return r;
    }
    g_panic("unexpected token in unary/primary");
    return r;
}

/* 后缀: a[i], a.b, a->b, f(args), a++, a-- */
static ExprRes parse_postfix(int rbp_stack_off) {
    ExprRes op = parse_unary(rbp_stack_off);
    while (1) {
        if (L_check(T_LBRACK)) {
            L_next();
            ExprRes idx = parse_expr(rbp_stack_off, 1);
            L_expect(T_RBRACK,"]");
            /* a[i]: a 是 ptr/array, i 是 int. 地址 = a_base + i * elemsize.
             * 注意 op 可能是 LVAL (栈顶是地址, 地址指向数组首址) 或 RVAL (栈顶是数组指针值) */
            Type *elem;
            if (op.val == PV_LVAL) {
                /* 栈顶是数组变量的地址: *(栈顶) = 数组首元素的地址 (即数组变量本身的值) */
                if (op.ty->kind == TY_ARRAY) elem = op.ty->base;
                else if (op.ty->kind == TY_PTR) elem = op.ty->base;
                else g_panic("[] not on array/ptr");
                /* 把 op 的地址(栈顶) 替换为该地址指向的数组首元素地址
                 * 数组变量 → 值就是数组首址, 所以栈顶那个地址本身就是 数组首址(因为 array 变量值=首址, 在栈机里 LVAL 是变量地址, 而变量存的就是 array itself)
                 * 简化处理: 对于 array lval, 栈顶是数组变量地址, 数组首址就等于这个地址 (数组无 indirection). 直接保留.
                 * 对于 ptr lval: 需要加一级 indirection. */
                if (op.ty->kind == TY_PTR) {
                    sm_pop_r(2); emit_mov_rm(0,2,0); sm_push_r(0);
                }
            } else {
                /* RVAL: 栈顶是 ptr 值本身 */
                if (op.ty->kind != TY_PTR && op.ty->kind != TY_ARRAY) g_panic("[] not on ptr/array");
                elem = op.ty->base;
            }
            /* 计算 idx * elem_size + base_addr */
            /* 栈: [base_addr|8B][idx|8B] */
            sm_pop_r(1); /* rcx = idx */
            sm_pop_r(0); /* rax = base */
            int esz = elem ? elem->size : 8;
            if (esz != 1) { emit_movi_imm32(2,esz); emit_imul_rr(1,2); }
            emit_add_rr(0,1);
            sm_push_r(0);
            op.ty = elem ? elem : t_int; op.val = PV_LVAL;
            continue;
        }
        if (L_check(T_DOT) || L_check(T_ARROW)) {
            int arr = L_check(T_ARROW); L_next();
            if (!L_check(T_IDENT)) g_panic("expected field name");
            char fn[128]; g_memcpy(fn,L.sbuf,128); L_next();
            /* 确保 base 是 struct 类型 */
            Type *sty = op.ty;
            if (arr) { /* →: base 是 ptr, 解引用 */
                if (op.val==PV_LVAL) { sm_pop_r(2); emit_mov_rm(0,2,0); sm_push_r(0); }
                if (sty->kind != TY_PTR) g_panic("-> on non-ptr");
                sty = sty->base;
                op.val = PV_RVAL; /* 栈顶现在是 struct 指针值 */
            }
            if (sty->kind == TY_PTR && !arr) { /* . on ptr: 自动解 (容错) */
                if (op.val==PV_LVAL) { sm_pop_r(2); emit_mov_rm(0,2,0); sm_push_r(0); }
                sty=sty->base; op.val=PV_RVAL;
            }
            if (op.val == PV_LVAL && sty->kind == TY_STRUCT) {
                /* 栈顶是 struct 变量地址 → 就是这个地址本身 + field.offset */
            } else if (sty->kind == TY_STRUCT) {
                /* 栈顶是 struct 值? 在 C 中 struct 类型的右值基本只能传值/返回值, 我们不支持在栈上直接表达 struct (栈机每次 push 8B). 限定 struct 表达式只能是 LVAL 或指针. */
            }
            if (sty->kind != TY_STRUCT) g_panic(". on non-struct");
            /* 查找 field */
            int fd = -1;
            for (int i=0;i<sty->nfields;i++) if (g_strcmp(sty->fields[i].name,fn)==0){fd=i;break;}
            if (fd<0) { g_print("no field "); g_print(fn); g_print("\n"); g_panic("field not found"); }
            Field *f = &sty->fields[fd];
            /* op 栈顶是 struct 的地址 (base_addr), 加 field offset → 新 LVAL 地址 */
            sm_pop_r(0);
            emit_movi_imm32(1, f->offset);
            emit_add_rr(0,1);
            sm_push_r(0);
            /* [新增] 位字段: 栈顶已是 storage unit 地址 (base + f->offset) */
            if (f->bit_width > 0) {
                if (L_check(T_ASSIGN)||L_check(T_PLUS_ASSIGN)||L_check(T_MINUS_ASSIGN)||
                    L_check(T_STAR_ASSIGN)||L_check(T_SLASH_ASSIGN)||L_check(T_PERCENT_ASSIGN)||
                    L_check(T_AMP_ASSIGN)||L_check(T_BOR_ASSIGN)||L_check(T_XOR_ASSIGN)||
                    L_check(T_SHL_ASSIGN)||L_check(T_SHR_ASSIGN)||
                    L_check(T_INC)||L_check(T_DEC)) {
                    /* 赋值/自增上下文: 保留 storage unit 地址作 LVAL, 设置位字段标志.
                     * parse_assign 必须在递归 rhs 前消费 g_bf_active. */
                    g_bf_active = 1; g_bf_width = f->bit_width; g_bf_offset = f->bit_offset;
                    op.ty = t_int; op.val = PV_LVAL;
                } else {
                    /* 读取上下文: 立即加载位字段值 (4B unit → 提取 → 符号扩展) */
                    sm_pop_r(1);                  /* rcx = storage unit addr */
                    gen_load_bitfield_rax(f->bit_width, f->bit_offset);
                    sm_push_r(0);
                    op.ty = t_int; op.val = PV_RVAL;
                }
                continue;
            }
            op.ty = f->type; op.val = f->is_method ? PV_RVAL : PV_LVAL;
            /* 若是方法, 栈顶目前是方法 this 的地址, 但我们想返回 "函数" (值+method_of). 简化: 我们不支持直接获取方法地址; 调用只能在 postfix f(args) 里处理. 这里为了简单, 如果是方法, 我们把 method 地址压栈, 并把 this 暂存到一个特殊位置.
             * 更简单的方式: 对 a.b(), 我们解析 a.b 时返回 一个 "双栈项": 先 this, 然后 method 地址. 但 parse_postfix 只返回一个 ExprRes. 处理方案: 当 f 是 method, 我们改 ExprRes.ty 以 method 的 FUNC 类型, 并把 this 地址 "藏" 在 method 地址下面. */
            if (f->is_method) {
                /* 当前栈只有 方法的 this 地址(即刚push的 field offset + struct base). 但我们需要 this 是 struct 自身的地址, 而 method 要跳到对应的全局符号 */
                /* 修正: 上面我 push 的是 this + field.offset, 但 method 不是数据成员而是成员函数 → 它没有 offset, offset 我应当写 0 (或未用). 这里我们应该: struct 地址就是 this, method 的全局 symbol 是 "StructName::MethodName" */
                sm_pop_r(2); /* 丢掉 (this + offset), 取回 this 的原始地址 (base) + offset 的 rax: 错, rax 已经被改了. 我重新取 base_addr: 必须重做. */
                /* 补救: 把 sty->fields[fd].offset 必须是 0 对方法. 所以 this 就是 rax - offset, 但 offset=0 就是 this. OK. */
                /* 现在: r2 = this 地址. 我们要在栈上先 push this, 然后 push 方法地址. 但 ExprRes 只能带 1 个 8B 栈值. 处理策略: 我们修改 ExprRes 含义: 对 method, 栈顶是 "方法地址", 并在栈顶下 (再低 8B) 是 this 地址. 然后在 parse_call 里如果检测到这是 method, 就自动把 this 作为第 1 参数. */
                /* 我重新 push this (目前 r2=rax (this+offset=offset=0 所以是 this)) */
                char full[256]; int n=0; int i=0; while(sty->sname[i]) full[n++]=sty->sname[i++]; full[n++]=':'; full[n++]=':'; i=0; while(f->name[i])full[n++]=f->name[i++]; full[n]=0;
                Symbol *ms = sym_find_global(full); if (!ms) g_panic("method symbol missing");
                sm_push_r(2);                          /* 栈顶: this */
                EmitMoviImm64(0, ms->addr); sm_push_r(0); /* 栈顶: method addr */
                /* 用 ExprRes.type 标记 method_of. 我们用 kind=TY_FUNC, base=返回类型, params[0] 第一个是 this*. 但我们已经 push 了 this. 在 parse_call 里我们需要不重复 push this. 所以设置一个标志: 通过 method_of 指针非空 指示 "栈已经自带 this 参数" */
                op.ty = ms->type; /* TY_FUNC: params[0] 是 this* (Type* = struct*) */
                /* 我们需要一种机制告知 parse_call 这是 method, 栈上还有 this.
                 * 办法: 给 ExprRes 加一个字段 method_call. 但我不想再改结构. 用一个全局 g_method_this_pushed = 1 代替. */
                g_method_this_pushed = 1;
                op.val = PV_RVAL;
            }
            continue;
        }
        if (L_check(T_LPAREN)) {
            /* func(args) 调用: 栈顶是函数地址 (PV_RVAL, 类型 TY_FUNC) 或 method
             * 返回类型 = func.ret */
            L_next();
            Type *fty = op.ty;
            if (fty->kind != TY_FUNC) g_panic("call non-func");
            /* 收集参数 (最多 16) */
            Type *atypes[16]; ExprRes args[16]; int na=0;
            if (!L_check(T_RPAREN)) for(;;) {
                ExprRes a = parse_assign(rbp_stack_off);
                atypes[na]=a.ty; args[na]=a; na++; if (na>=16) g_panic("too many args");
                if (L_accept(T_COMMA)) continue; break;
            }
            L_expect(T_RPAREN,")");
            /* --- 参数传递 ---
             * 现在栈上内容 (从高到低):
             *   ... op(method 时: this, method 地址), 普通: 函数地址
             *       然后 args[0..na-1] 每项 8B 还没 push? 不: parse_assign 已经把每个 arg 的值 push 了.
             * 实际上我们在 parse_assign 中每个表达式都已 push 自己的结果. 所以参数的 RVAL 已经在 stack 上按顺序从左到右 push (栈顶是最后一个参数 na-1). */
            /* System V: 前 6 个 (rdi/rsi/rdx/rcx/r8/r9), 后面 push 右到左.
             * 但如果是 method: "this" 已经在函数地址的下方 push 了 (如果是 method). 我们需要把它放在第一个参数 (rdi).
             * 栈的组织 (从栈顶往下, 也就是先 pop 得到后 push 的东西):
             *   普通: [args[na-1] ... args[0] ...  func_addr ... ]
             *   method: [args[na-1] ... args[0] ... method_addr ... this_addr ... ]
             * 先把 func_addr pop 到 r11; 然后需要把参数从栈搬到寄存器/参数栈区. */
            int nregpar = 6;  /* rdi=7, rsi=6, rdx=2, rcx=1, r8=8, r9=9 */
            int regp_rdi=7, regp_rsi=6, regp_rdx=2, regp_rcx=1, regp_r8=8, regp_r9=9;
            int reg_map[6] = {regp_rdi, regp_rsi, regp_rdx, regp_rcx, regp_r8, regp_r9};
            long method_this_va = 0;
            if (g_method_this_pushed) {
                /* 当前栈顶是 method_addr; 下面是 this. 先 pop method → r11, 再 pop this → rdi (参数 1) */
                sm_pop_r(11);                                  /* r11 = method_addr */
                sm_pop_r(reg_map[0]);                           /* rdi = this */
                /* 剩下的用户参数放到寄存器位置 1.. (index 1..nregpar-1), 多余的最后 push 参数栈 */
                /* args[0..na-1] 对应方法参数 params[1..nparams-1], 它们已经 push 在栈上, 但顺序是:
                 * 栈顶 = args[na-1] (最后一个用户参数)
                 * 栈底 (紧接 this 下面) = args[0]
                 * 我们需要从 args[0] 开始 (栈底部, 需要一层层 pop 上来拿到所有 args 先放到临时位置或把所有 args 先 pop 存到 rbx 保存的内存)
                 * 简单办法: 所有 na 个 args 先 pop 到 r15, r14, r13, r12, r10, rbx ... 这些 callee-save (最多存 na 个). 然后按 reg_map[1..5] 放前 5 个, 剩下 push. */
                int save_regs[16] = {3/*rbx*/, 12,13,14,15, 10, /*剩下的临时 r10*/ 0,0,0,0,0,0,0,0,0,0};
                int saved[16]; for(int i=0;i<na;i++) saved[i]=0;
                for(int i=na-1;i>=0;i--) { sm_pop_r(save_regs[i]); saved[i]=save_regs[i]; }
                /* 现在: 用户参数 0..na-1 存在 saved[0..na-1] 寄存器里 */
                int remaining_reg = 5; /* 还剩 5 个参数寄存器 (总 6, this 用了 1 个) */
                int stack_args = 0;
                for(int i=na-1;i>=0;i--) { /* 后面的参数先 push (System V: 栈参数右到左) */
                    int arg_idx = i + 1; /* 在函数 FUNC params 的下标 (+1 因为 this) */
                    if (arg_idx < nregpar && remaining_reg>0) {
                        /* 不用管, 最后再填 */
                    } else {
                        sm_push_r(saved[i]);
                        stack_args++;
                    }
                }
                /* 填寄存器参数: 用户 arg i → func param (i+1) → reg[i+1] (如果 <6) */
                for(int i=0;i<na && i<remaining_reg;i++) emit_mov_rr(reg_map[i+1], saved[i]);
                emit_com1_trace('J');  /* 运行时: 即将 call r11 (method 调用前) */
                emit_callr(11);
                g_method_this_pushed = 0;
                if (stack_args) sm_drop(stack_args);
                /* 返回值: rax (若类型 != void), 我们 push rax */
                if (fty->base->kind != TY_VOID) sm_push_r(0);
                else emit_movi_imm32(0,0), sm_push_r(0);
                op.ty = fty->base ? fty->base : t_int; op.val = PV_RVAL;
                continue;
            }
            /* --- 普通函数调用 --- */
            sm_pop_r(11); /* r11 = func_addr */
            /* na 个参数已经 push, 栈顶是最后一个. 我们按 System V 布局. */
            /* 保存所有参数值到寄存器: 先 pop 到 rbx/r12/r13/r14/r15/r10/r9... 够存 16 个. */
            int save_regs[16] = {3,12,13,14,15, 10, 0,0,0,0,0,0,0,0,0,0};
            int saved[16]; for(int i=0;i<na;i++) saved[i]=0;
            for(int i=na-1;i>=0;i--) { sm_pop_r(save_regs[i]); saved[i]=save_regs[i]; }
            /* 栈参数右→左 push */
            int stack_args = 0;
            for(int i=na-1;i>=nregpar;i--) { sm_push_r(saved[i]); stack_args++; }
            /* 前 6 个 → reg_map[0..5] */
            for(int i=0;i<na && i<nregpar;i++) emit_mov_rr(reg_map[i], saved[i]);
            emit_com1_trace('J');  /* 运行时: 即将 call r11 (普通函数调用前) */
            emit_callr(11);
            if (stack_args) sm_drop(stack_args);
            if (fty->base && fty->base->kind != TY_VOID) sm_push_r(0);
            else { emit_movi_imm32(0,0); sm_push_r(0); }
            op.ty = fty->base ? fty->base : t_int; op.val = PV_RVAL;
            continue;
        }
        if (L_check(T_INC) || L_check(T_DEC)) {
            int inc = L_check(T_INC)?1:-1; L_next();
            /* [新增] 位字段 postfix ++/--: 栈顶是 storage unit 地址 (g_bf_active 已设) */
            if (g_bf_active) {
                int bw = g_bf_width, bo = g_bf_offset; g_bf_active = 0;
                sm_pop_r(1);                      /* rcx = storage unit addr */
                gen_load_bitfield_rax(bw, bo);    /* rax = old value; rcx 保留 */
                sm_push_r(0);                     /* 返回旧值 */
                if (inc>0) emit_inc_r(0); else emit_dec_r(0);
                gen_store_bitfield(bw, bo);       /* 写回新值到 [rcx] */
                op.ty = t_int; op.val = PV_RVAL;
                continue;
            }
            if (op.val != PV_LVAL) g_panic("++ requires lvalue");
            if (op.ty && ty_is_float(op.ty)) {
                /* 浮点 postfix ++/--: 返回旧值, 写回 old±1.0.
                 * rcx=addr; 旧值经 rax push 到栈 (返回值); 用 FPU 计算 new, 写回 [rcx]. */
                sm_pop_r(2);                  /* rcx = addr */
                emit_mov_rm(0, 2, 0);          /* rax = old (8B 位模式) */
                sm_push_r(0);                 /* 返回旧值 */
                emit_fld_qword(2, 0);         /* st0 = old */
                emit_fld1();                  /* st0 = 1.0, st1 = old */
                if (inc>0) emit_faddp(); else emit_fsubp(); /* st0 = old±1 */
                emit_fstp_qword(2, 0);        /* *addr = new, FPU 弹 */
                op.val = PV_RVAL;
                continue;
            }
            /* 栈顶是地址; 读出原值 push, 然后 ±1 写回 → 最终 push 旧值 (postfix) */
            sm_pop_r(2);
            emit_mov_rm(0,2,0);   /* rax = old */
            sm_push_r(0);
            emit_mov_rr(1,0);
            if (inc>0) emit_inc_r(1); else emit_dec_r(1);
            emit_mov_mr(2,0,1);
            op.ty = op.ty; op.val = PV_RVAL; /* 旧值, PV_RVAL */
            continue;
        }
        break;
    }
    return op;
}

/* 二元运算按优先级下降 (min_prio 起) */
static ExprRes parse_binops(int rbp_stack_off, int min_prio) {
    ExprRes lhs = parse_postfix(rbp_stack_off);
    while (op_prio(L.tok) >= min_prio) {
        /* 短路 && / ||: 这里简化为一律求值两侧 (语义差别可忽略大多数情况) */
        TOK op = L.tok; L_next();
        int p = op_prio(op);
        ExprRes rhs = parse_binops(rbp_stack_off, p + 1);
        /* 此时栈上 lhs_top= ... lhs_value 压在栈下方, rhs_value 在栈顶?
         * No: parse_postfix 返回时, 栈顶是 lhs 值. parse_binops 中又 parse_postfix 会把 rhs 值也压在栈顶, 所以栈上自下而上: [lhs 8B][rhs 8B], 栈顶 = rhs. */
        /* 需要把 lhs 与 rhs 做运算, 结果替换它们 (2*8B → 8B) */
        int opn=-1;
        switch(op){
            case T_PLUS: opn=0; break; case T_MINUS:opn=1; break; case T_STAR:opn=2; break;
            case T_SLASH:opn=3; break; case T_PERCENT:opn=4; break; case T_SHL:opn=5; break;
            case T_SHR:opn=6; break; case T_AMP:opn=7; break; case T_BOR:opn=8; break; case T_XOR:opn=9; break;
            case T_EQ:opn=10;break; case T_NEQ:opn=11;break; case T_LT:opn=12;break; case T_GT:opn=13;break; case T_LE:opn=14;break; case T_GE:opn=15;break;
            /* [Bug 修复] && 和 || 之前遗漏了 case → 不生成代码 → 操作数留在栈上.
             * 简化处理: 非短路, 两个操作数都求值后做位 AND/OR (对 0/1 布尔值语义等价) */
            case T_AND: opn=7; break;  /* && → 位 AND (对 bool 值等价) */
            case T_OR:  opn=8; break;  /* || → 位 OR  (对 bool 值等价) */
            default: break;
        }
        /* 浮点调度: 任一操作数为 float/double 时走 FPU 路径.
         * 注: % << >> & | ^ 不是浮点合法运算, 仍走整数路径 (与 C 一致).
         *     浮点混合 int 操作数时, int 提升为 double 由 cast 阶段处理;
         *     这里在调用前对 int 操作数做隐式 int→double 提升. */
        int is_float_op = (lhs.ty && ty_is_float(lhs.ty)) || (rhs.ty && ty_is_float(rhs.ty));
        int op_is_arith_or_cmp = (opn>=0) && ((opn<=3) || (opn>=10 && opn<=15));
        if (opn>=0) {
            if (is_float_op && op_is_arith_or_cmp) {
                /* 隐式提升 int → double */
                if (lhs.ty && ty_is_int(lhs.ty) && rhs.ty && ty_is_float(rhs.ty)) {
                    /* 栈顶 rhs (float) 在上, lhs (int) 在下. 先 pop rhs, 提升 lhs, 再 push rhs */
                    sm_pop_r(0);    /* rax = rhs (double 位模式) */
                    /* 提升 lhs: 现栈顶 = lhs (int). 用 fild/fstp 转换 */
                    emit_fild_qword(4, 0);  /* fild qword [rsp] = lhs_int → st0 */
                    emit_fstp_qword(4, 0);  /* fstp qword [rsp] = double(lhs), FPU 弹出 */
                    sm_push_r(0);    /* 还原 rhs 到顶 */
                    lhs.ty = t_double;
                } else if (rhs.ty && ty_is_int(rhs.ty) && lhs.ty && ty_is_float(lhs.ty)) {
                    /* 栈顶 rhs (int) 提升; 直接在原位转换 */
                    emit_fild_qword(4, 0);  /* fild [rsp] = rhs_int → st0 */
                    emit_fstp_qword(4, 0);  /* fstp [rsp] = double(rhs) */
                    rhs.ty = t_double;
                }
                gen_binop_float(opn);
            } else {
                gen_binop_int(opn);
            }
        }
        /* 指针算术: p+n = p + n*sizeof(*p). 我们已用整数 opn=0/1 计算了, 需要在 ptr 情形乘以 elem_size. 简化处理: lhs 是 ptr 且 rhs 是 int 且 op +/-, 额外乘 sizeof */
        if (lhs.ty && (lhs.ty->kind==TY_PTR || lhs.ty->kind==TY_ARRAY) && ty_is_int(rhs.ty) && (op==T_PLUS || op==T_MINUS)) {
            int sz = lhs.ty->base ? lhs.ty->base->size : 8;
            if (sz != 1) {
                sm_pop_r(0); emit_movi_imm32(1,sz); emit_imul_rr(0,1); sm_push_r(0);
            }
        }
        /* 浮点算术结果统一为 double (栈机内统一以 8B double 存储); 比较结果为 int */
        if (is_float_op && op_is_arith_or_cmp) {
            lhs.ty = (op==T_EQ||op==T_NEQ||op==T_LT||op==T_GT||op==T_LE||op==T_GE) ? t_int : t_double;
        } else {
            lhs.ty = (op==T_EQ||op==T_NEQ||op==T_LT||op==T_GT||op==T_LE||op==T_GE) ? t_int : lhs.ty;
        }
        lhs.val = PV_RVAL;
    }
    return lhs;
}

/* a ? b : c */
static ExprRes parse_ternary(int rbp_stack_off) {
    ExprRes cond = parse_binops(rbp_stack_off, 1);
    if (L_accept(T_QUEST)) {
        ExprRes a = parse_expr(rbp_stack_off,1);
        L_expect(T_COLON,":");
        ExprRes b = parse_ternary(rbp_stack_off);
        /* 栈: cond a b (顺序从底到顶) */
        sm_pop_r(1); /* b */
        sm_pop_r(2); /* a */
        sm_pop_r(0); /* cond */
        long labA=new_label(), labDone=new_label();
        emit_test_rr(0,0); /* cond? */
        emit_jcc(5, labA);   /* NZ 取 a, 否则 b */
        emit_mov_rr(0,1);
        emit_jmp(labDone);
        def_label(labA);
        emit_mov_rr(0,2);
        def_label(labDone);
        sm_push_r(0);
        cond.ty = a.ty; cond.val = PV_RVAL;
    }
    return cond;
}

/* 赋值: =, +=, -= */
static ExprRes parse_assign(int rbp_stack_off) {
    ExprRes lhs = parse_ternary(rbp_stack_off);
    /* [新增] 复合赋值运算符: *= /= %= &= |= ^= <<= >>= */
    if (L_check(T_ASSIGN) || L_check(T_PLUS_ASSIGN) || L_check(T_MINUS_ASSIGN) ||
        L_check(T_STAR_ASSIGN) || L_check(T_SLASH_ASSIGN) || L_check(T_PERCENT_ASSIGN) ||
        L_check(T_AMP_ASSIGN) || L_check(T_BOR_ASSIGN) || L_check(T_XOR_ASSIGN) ||
        L_check(T_SHL_ASSIGN) || L_check(T_SHR_ASSIGN)) {
        TOK op = L.tok; L_next();
        /* [新增] 位字段赋值: 必须在递归 rhs 前消费 g_bf_active, 避免嵌套表达式覆盖. */
        int bf_w = 0, bf_o = 0, is_bf = 0;
        if (g_bf_active) { is_bf = 1; bf_w = g_bf_width; bf_o = g_bf_offset; g_bf_active = 0; }
        ExprRes rhs = parse_assign(rbp_stack_off);
        if (is_bf) {
            /* 栈: [storage_unit_addr 8B][rhs_value 8B] (top=rhs) */
            sm_pop_r(0);   /* rax = rhs */
            sm_pop_r(1);   /* rcx = storage unit addr */
            if (op != T_ASSIGN) {
                /* 复合赋值: 读旧位字段值 → 运算 → 新值. 保存 rhs 到 r9. */
                emit_mov_rr(9, 0);                 /* r9 = rhs */
                gen_load_bitfield_rax(bf_w, bf_o); /* rax = old value; rcx 保留 */
                switch(op) {
                    case T_PLUS_ASSIGN:  emit_add_rr(0, 9); break;
                    case T_MINUS_ASSIGN: emit_sub_rr(0, 9); break;
                    case T_STAR_ASSIGN:  emit_imul_rr(0, 9); break;
                    case T_AMP_ASSIGN:   emit_and_rr(0, 9); break;
                    case T_BOR_ASSIGN:   emit_or_rr(0, 9); break;
                    case T_XOR_ASSIGN:   emit_xor_rr(0, 9); break;
                    case T_SLASH_ASSIGN:
                        /* idiv 需 rcx 作 divisor, 但 rcx=addr: 临时保存 addr */
                        sm_push_r(1); emit_mov_rr(1, 9); emit_cqo(); emit_idiv_r(1); sm_pop_r(1);
                        break;
                    case T_PERCENT_ASSIGN:
                        sm_push_r(1); emit_mov_rr(1, 9); emit_cqo(); emit_idiv_r(1); sm_pop_r(1);
                        emit_mov_rr(0, 2);  /* rax = rdx (余数) */
                        break;
                    case T_SHL_ASSIGN:
                        /* shift_cl 用 cl, 破坏 rcx: 临时保存 addr. op=4 是 SHL (/4) */
                        sm_push_r(1); emit_mov_rr(1, 9); emit_shift_cl(4, 0); sm_pop_r(1);
                        break;
                    case T_SHR_ASSIGN:
                        sm_push_r(1); emit_mov_rr(1, 9); emit_shift_cl(5, 0); sm_pop_r(1);
                        break;
                }
                /* rax = 新值, rcx = storage unit addr */
            }
            gen_store_bitfield(bf_w, bf_o);   /* rax→dword[rcx]; rax=截断后新值 */
            sm_push_r(0);                      /* 结果压栈 */
            lhs.ty = t_int; lhs.val = PV_RVAL;
            return lhs;
        }
        if (lhs.val != PV_LVAL) g_panic("assign to non-lvalue");
        /* 栈: [lhs_addr 8B][rhs_value 8B] → 替换为 [rhs_value] */
        sm_pop_r(0); /* rax = rhs */
        sm_pop_r(2); /* rcx = lhs_addr */
        int is_fl_lhs = lhs.ty && ty_is_float(lhs.ty);
        int is_fl_rhs = rhs.ty && ty_is_float(rhs.ty);
        int is_int_lhs = lhs.ty && ty_is_int(lhs.ty);
        int is_int_rhs = rhs.ty && ty_is_int(rhs.ty);
        if (op == T_ASSIGN) {
            /* 隐式类型转换: rhs 位模式与 lhs 类型不符时, 在 rax 中就地转换 */
            if (is_fl_lhs && is_int_rhs) {
                /* int → double: 经栈中转 fild/fstp */
                sm_push_r(0); emit_fild_qword(4,0); emit_fstp_qword(4,0); sm_pop_r(0);
            } else if (is_int_lhs && is_fl_rhs) {
                /* double → int (向 0 截断) */
                sm_push_r(0); emit_fld_qword(4,0); emit_fistp_qword(4,0); sm_pop_r(0);
            }
            /* float↔double: 均为 8B 存储, 位模式可直接复用 (语义近似) */
        } else if (is_fl_lhs) {
            /* 浮点复合赋值 (+= -= *= /=): 走 FPU 路径.
             * 栈布局需求: [lhs_old 8B][rhs 8B] (rhs 在顶), 与 gen_binop_float 一致. */
            int opn = -1;
            if      (op==T_PLUS_ASSIGN)  opn=0;
            else if (op==T_MINUS_ASSIGN) opn=1;
            else if (op==T_STAR_ASSIGN)  opn=2;
            else if (op==T_SLASH_ASSIGN) opn=3;
            if (opn >= 0) {
                /* 若 rhs 是 int, 先提升到 double (就地转换 rax) */
                if (is_int_rhs) {
                    sm_push_r(0); emit_fild_qword(4,0); emit_fstp_qword(4,0); sm_pop_r(0);
                }
                /* 读 lhs 旧值 (8B double 位模式) 到 rdx */
                emit_mov_rm(1, 2, 0);
                /* 构造栈: 先 push lhs_old, 再 push rhs → [lhs_old][rhs] */
                sm_push_r(1);
                sm_push_r(0);
                gen_binop_float(opn);
                sm_pop_r(0); /* rax = 结果 */
            } else {
                /* %= &= |= ^= <<= >>= 浮点不合法, 回退整数路径 (语义未定义, 但避免卡死) */
                emit_mov_rm(1,2,0);
            }
        } else if (op == T_PLUS_ASSIGN || op == T_MINUS_ASSIGN) {
            emit_mov_rm(1,2,0); /* rdx = *lhs */
            if (op==T_PLUS_ASSIGN) emit_add_rr(1,0); else emit_sub_rr(1,0);
            emit_mov_rr(0,1); /* rax 作为结果 */
        } else if (op != T_ASSIGN) {
            /* [新增] 复合赋值: *= /= %= &= |= ^= <<= >>=
             * 读取旧值, 执行运算, 写回 */
            emit_mov_rm(1,2,0); /* rdx = *lhs (旧值) */
            /* rax=rhs, rdx=oldval → 结果放 rax */
            if      (op==T_STAR_ASSIGN)   emit_imul_rr(0,1);
            else if (op==T_SLASH_ASSIGN)  { emit_cqo(); emit_idiv_r(1); }
            else if (op==T_PERCENT_ASSIGN){ emit_cqo(); emit_idiv_r(1); emit_mov_rr(0,2); }
            else if (op==T_AMP_ASSIGN)    emit_and_rr(0,1);
            else if (op==T_BOR_ASSIGN)   emit_or_rr(0,1);
            else if (op==T_XOR_ASSIGN)   emit_xor_rr(0,1);
            else if (op==T_SHL_ASSIGN)   { emit_mov_rr(9,1); emit_mov_rr(1,0); emit_movi_imm32(0,0); emit_mov_rr(0,1); emit_mov_rr(1,9); emit_shift_cl(0,0); }
            else if (op==T_SHR_ASSIGN)   { emit_mov_rr(9,1); emit_mov_rr(1,0); emit_movi_imm32(0,0); emit_mov_rr(0,1); emit_mov_rr(1,9); emit_shift_cl(5,0); }
        }
        /* char 类型赋值: 只写低 8 位; 其余 (含 float/double) 一律写 8 字节
         * (栈机内浮点统一以 8B double 存储, 局部变量已 8B 对齐分配) */
        int sz = lhs.ty->size;
        if (sz==1) {
            /* mov byte [rcx], al */
            emit_rex(0,0,0,2); emit_b(0x88); emit_modrm(3,0,2);
        } else {
            emit_mov_mr(2,0,0); /* 写 64 位? 不对: int/long = 4/8 字节. 简化: 默认 8 字节 (所有变量 8B 对齐存储, int 高 32 位为 0) */
        }
        sm_push_r(0);
        lhs.ty = lhs.ty; lhs.val = PV_RVAL;
    }
    return lhs;
}

/* [新增] 逗号运算符: e1, e2 → 求值 e1 (丢弃), 求值 e2 (结果) */
static ExprRes parse_expr(int rbp_stack_off, int min_prio) {
    ExprRes lhs = parse_assign(rbp_stack_off);
    while (L_check(T_COMMA)) {
        L_next();
        sm_drop(1);  /* 丢弃 e1 的值 */
        lhs = parse_assign(rbp_stack_off);
    }
    (void)min_prio;
    return lhs;
}

/* ========== 全局变量: 方法调用的 this push 标志 (已在文件开头定义, 移除以避免重复) ========== */

/* ========== 语句解析器 ========== */
/* break / continue 目标标签栈 (最多嵌套 32 层循环)
 * [BSS 崩溃修复] 用 IN_DATA + 显式初始化 */
typedef struct { long brk; long cont; } LoopLabs;
static LoopLabs g_loops[32] IN_DATA = {0};
static int g_loops_n IN_DATA = 0;
static int push_loop(long brk, long cont) { if(g_loops_n>=32)g_panic("loop nest overflow"); g_loops[g_loops_n].brk=brk; g_loops[g_loops_n].cont=cont; g_loops_n++; return g_loops_n-1; }
static void pop_loop(void) { if(g_loops_n>0) g_loops_n--; }

static void parse_stmt(int rbp_stack_off) {
    /* if (e) s [else s2] */
    if (L_check(T_IF)) {
        L_next(); L_expect(T_LPAREN,"(");
        ExprRes c = parse_expr(rbp_stack_off, 1);
        L_expect(T_RPAREN,")");
        /* 栈顶是条件值 */
        sm_pop_r(0); long lelse = new_label(), ldone = new_label();
        emit_test_rr(0,0);
        emit_jcc(5, lelse); /* ZF=1 (false) → else */
        parse_stmt(rbp_stack_off);
        emit_jmp(ldone);
        def_label(lelse);
        if (L_accept(T_ELSE)) parse_stmt(rbp_stack_off);
        def_label(ldone);
        return;
    }
    /* while (e) s */
    if (L_check(T_WHILE)) {
        L_next(); L_expect(T_LPAREN,"(");
        long lcond = new_label(), lbody = new_label(), ldone = new_label(), lcont = lcond;
        int di = push_loop(ldone, lcont);
        def_label(lcond);
        ExprRes c = parse_expr(rbp_stack_off, 1);
        L_expect(T_RPAREN,")");
        sm_pop_r(0); emit_test_rr(0,0);
        emit_jcc(5, ldone);
        parse_stmt(rbp_stack_off);
        emit_jmp(lcond);
        def_label(ldone);
        pop_loop(); (void)di;
        return;
    }
    /* for (a;b;c) s  →  a; while(b){ s; c; } */
    if (L_check(T_FOR)) {
        L_next(); L_expect(T_LPAREN,"(");
        if (!L_check(T_SEMI)) { ExprRes x = parse_expr(rbp_stack_off,1); sm_drop(1); }
        L_expect(T_SEMI,";");
        long lcond=new_label(), lbody=new_label(), ldone=new_label(), lcont=new_label();
        int di = push_loop(ldone, lcont);
        def_label(lcond);
        if (!L_check(T_SEMI)) {
            ExprRes c = parse_expr(rbp_stack_off,1);
            sm_pop_r(0); emit_test_rr(0,0);
            emit_jcc(5, ldone);
        }
        L_expect(T_SEMI,";");
        parse_stmt(rbp_stack_off);
        def_label(lcont);
        if (!L_check(T_RPAREN)) { ExprRes x = parse_expr(rbp_stack_off,1); sm_drop(1); }
        L_expect(T_RPAREN,")");
        emit_jmp(lcond);
        def_label(ldone);
        pop_loop(); (void)di;
        return;
    }
    /* return [e]; */
    if (L_check(T_RETURN)) {
        L_next();
        if (!L_check(T_SEMI)) {
            ExprRes x = parse_expr(rbp_stack_off,1);
            /* 栈顶 = 返回值, pop 到 rax */
            sm_pop_r(0);
        }
        L_expect(T_SEMI,";");
        /* leave + ret: 用标准函数尾序 (需要知道 ret 标签, 让函数收尾负责 leave/ret) */
        /* 直接生成 leave; ret 也行 (简单). 但函数收尾可能还有析构调用. 简化: 直接 leave ret。 */
        emit_b(0xC9); /* leave */ emit_ret();
        return;
    }
    /* break; */
    if (L_check(T_BREAK)) {
        L_next(); L_expect(T_SEMI,";");
        if(g_loops_n<=0) g_panic("break outside loop");
        emit_jmp(g_loops[g_loops_n-1].brk);
        return;
    }
    /* continue; */
    if (L_check(T_CONTINUE)) {
        L_next(); L_expect(T_SEMI,";");
        if(g_loops_n<=0) g_panic("continue outside loop");
        emit_jmp(g_loops[g_loops_n-1].cont);
        return;
    }
    /* [新增] do { body } while (cond); */
    if (L_check(T_DO)) {
        L_next();
        long lbody = new_label(), lcond = new_label(), ldone = new_label();
        int di = push_loop(ldone, lcond);
        def_label(lbody);
        parse_stmt(rbp_stack_off);
        def_label(lcond);
        L_expect(T_WHILE, "while");
        L_expect(T_LPAREN, "(");
        ExprRes c = parse_expr(rbp_stack_off, 1);
        L_expect(T_RPAREN, ")");
        L_expect(T_SEMI, ";");
        sm_pop_r(0); emit_test_rr(0,0);
        emit_jcc(5, lbody);  /* NZ → 回到 body */
        def_label(ldone);
        pop_loop(); (void)di;
        return;
    }
    /* [新增] switch (e) { case v: ... default: ... } */
    if (L_check(T_SWITCH)) {
        L_next(); L_expect(T_LPAREN, "(");
        ExprRes c = parse_expr(rbp_stack_off, 1);
        L_expect(T_RPAREN, ")");
        sm_pop_r(0);  /* rax = switch 值 */
        long ldone = new_label();
        int di = push_loop(ldone, ldone);  /* break → ldone, continue → ldone (无 continue 语义) */
        /* 收集所有 case/default 的标签和值, 生成比较跳转链 */
        L_expect(T_LBRACE, "{");
        /* 第一遍: 收集 case 标签 + default 标签 */
        /* 简化实现: 逐个 case 生成 cmp + jne 链 */
        long case_labels[128]; int case_vals[128]; int n_cases = 0;
        long default_label = -1;
        /* 先保存当前 pos, 扫描一遍收集 case 值, 然后回到 { 重新解析生成代码 */
        int saved_pos = L.pos; int saved_tok = L.tok; int saved_line = L.line;
        char saved_sbuf[256]; g_memcpy(saved_sbuf, L.sbuf, 256); int saved_slen = L.slen; int saved_num = L.num;
        /* 第一遍扫描: 收集 case/default 位置 */
        while (!L_check(T_RBRACE) && !L_check(T_EOF)) {
            if (L_check(T_CASE)) {
                L_next();
                /* 读取常量表达式 (简化: 只支持数字) */
                if (L_check(T_MINUS)) { L_next(); /* 负数 */
                    /* 保存标识符作为 case 值 */
                    if (L_check(T_IDENT)) { Symbol *cs = sym_find_global(L.sbuf); case_vals[n_cases] = cs ? (int)cs->addr : 0; L_next(); }
                    else { case_vals[n_cases] = -L.num; L_next(); }
                } else if (L_check(T_IDENT)) {
                    Symbol *cs = sym_find_global(L.sbuf); case_vals[n_cases] = cs ? (int)cs->addr : 0; L_next();
                } else { case_vals[n_cases] = L.num; L_next(); }
                L_expect(T_COLON, ":");
                case_labels[n_cases] = new_label();
                n_cases++;
                if (n_cases >= 128) break;
            } else if (L_check(T_DEFAULT)) {
                L_next(); L_expect(T_COLON, ":");
                default_label = new_label();
            } else { L_next(); }  /* 跳过 case body 中的语句 */
        }
        /* 恢复到 { 后位置, 第二遍: 生成比较链 + body */
        L.pos = saved_pos; L.tok = saved_tok; L.line = saved_line;
        g_memcpy(L.sbuf, saved_sbuf, 256); L.slen = saved_slen; L.num = saved_num;
        L_next();  /* 重新取第一个 token */
        /* 生成比较跳转链: rax 保存 switch 值 */
        for (int i = 0; i < n_cases; i++) {
            emit_movi_imm32(1, case_vals[i]);  /* rcx = case 值 */
            emit_cmp_rr(0, 1);                  /* cmp rax, rcx */
            emit_jcc(4, case_labels[i]);         /* JE → case label */
        }
        if (default_label >= 0) emit_jmp(default_label);
        else emit_jmp(ldone);
        /* 第二遍: 生成 body (遇到 case/default 时 def_label) */
        int case_idx = 0;
        while (!L_check(T_RBRACE) && !L_check(T_EOF)) {
            if (L_check(T_CASE)) {
                L_next();
                /* 跳过 case 值表达式 */
                if (L_check(T_MINUS)) { L_next(); if (L_check(T_IDENT)) L_next(); else L_next(); }
                else if (L_check(T_IDENT)) L_next();
                else L_next();
                L_expect(T_COLON, ":");
                def_label(case_labels[case_idx++]);
            } else if (L_check(T_DEFAULT)) {
                L_next(); L_expect(T_COLON, ":");
                def_label(default_label);
            } else {
                parse_stmt(rbp_stack_off);
            }
        }
        def_label(ldone);
        pop_loop(); (void)di;
        L_expect(T_RBRACE, "}");
        return;
    }
    /* [新增] goto label;  —  前向/后向引用统一走 emit_jmp + patch_rel32_here
     * label 符号的 addr 字段存的是 label_id (>=0); 未定义时为 -1, 此时分配新 id */
    if (L_check(T_GOTO)) {
        L_next();
        if (!L_check(T_IDENT)) g_panic("goto expects label");
        Symbol *ls = sym_find_global(L.sbuf);
        if (!ls) {
            /* 前向 goto: 先分配 label_id, 后续 def_label 时复用同一个 id */
            ls = sym_add(L.sbuf, SK_LABEL, t_int);
            ls->addr = new_label();
            ls->defined = 0;
        }
        long lab = ls->addr;  /* 一定是有效的 label_id */
        L_next(); L_expect(T_SEMI, ";");
        emit_jmp(lab);  /* patch_rel32_here 自动记录, patch_all 回填 */
        return;
    }
    /* [新增] 标号语句: label: stmt  —  与 goto 配对, 复用前向引用时已分配的 label_id */
    if (L_check(T_IDENT)) {
        /* 向前看: 如果下一个 token 是 COLON, 就是标号 */
        int saved_pos = L.pos, saved_tok = L.tok, saved_line = L.line;
        char saved_sbuf[256]; g_memcpy(saved_sbuf, L.sbuf, 256); int saved_slen = L.slen, saved_num = L.num;
        L_next();
        if (L_check(T_COLON)) {
            /* 这是一个标号定义 */
            char label_name[256]; int li=0;
            const char *src = saved_sbuf;
            while (src[li] && li < 255) { label_name[li] = src[li]; li++; }
            label_name[li] = 0;
            Symbol *ls = sym_find_global(label_name);
            long lab;
            if (!ls) {
                /* 未被 goto 引用: 新建标签 */
                ls = sym_add(label_name, SK_LABEL, t_int);
                lab = new_label();
                ls->addr = lab;
            } else if (!ls->defined) {
                /* 前向引用的回填点: 复用之前在 goto 处分配的 label_id */
                lab = ls->addr;
            } else {
                /* 重复定义: 仍发一个新 label, 不报错 (简化) */
                lab = new_label();
                ls->addr = lab;
            }
            def_label(lab);
            ls->defined = 1;
            /* 解析冒号后面的语句 */
            parse_stmt(rbp_stack_off);
            return;
        }
        /* 不是标号, 恢复 */
        L.pos = saved_pos; L.tok = saved_tok; L.line = saved_line;
        g_memcpy(L.sbuf, saved_sbuf, 256); L.slen = saved_slen; L.num = saved_num;
        /* 落入表达式语句 */
    }
    /* block { stmts... } */
    if (L_check(T_LBRACE)) {
        L_next();
        g_scope_depth++;
        while (!L_check(T_RBRACE) && !L_check(T_EOF)) {
            /* 声明或语句 */
            int decl=0;
            if (L_check(T_INT)||L_check(T_CHAR)||L_check(T_VOID)||L_check(T_LONG)||L_check(T_SHORT)||
                L_check(T_STRUCT)||L_check(T_CLASS)||L_check(T_TYPEDEF)||L_check(T_ENUM)||L_check(T_UNION)||
                L_check(T_UNSIGNED)||L_check(T_SIGNED)||L_check(T_CONST)||L_check(T_STATIC)||
                L_check(T_EXTERN)||L_check(T_VOLATILE)||L_check(T_REGISTER)||
                L_check(T_FLOAT_KW)||L_check(T_DOUBLE_KW)||L_check(T_STATIC_ASSERT)) decl=1;
            else if (L_check(T_IDENT)) {
                /* 向前看 2 token: 若下一个是 * 或 IDENT 或 [, 可能是声明; 否则 表达式语句 */
                Symbol *sy = sym_find(L.sbuf, g_scope_depth);
                if (sy && sy->kind==SK_TYPE) decl=1;
            }
            if (decl) {
                extern void parse_declaration(int*, int*, int);
                int _unused1=0, _unused2=0;
                parse_declaration(&_unused1, &_unused2, rbp_stack_off);
            } else {
                ExprRes x = parse_expr(rbp_stack_off,1);
                sm_drop(1);  /* 丢弃表达式值 */
                L_expect(T_SEMI,";");
            }
        }
        sym_leave_scope(g_scope_depth);
        g_scope_depth--;
        L_expect(T_RBRACE,"}");
        return;
    }
    /* ; 空语句 */
    if (L_accept(T_SEMI)) return;
    /* 表达式语句 */
    {
        ExprRes x = parse_expr(rbp_stack_off,1);
        sm_drop(1);
        L_expect(T_SEMI,";");
        return;
    }
}

/* ========== 类型 + 声明解析器 ========== */
/* 解析说明符: int/char/void/long/struct X/typedef → Type*
 * 允许 * 和 [] 修饰符紧跟 */
static Type *parse_spec_and_decl(char *name_out, int *is_typedef, int *is_func, int rbp_stack_off,
                                 Type ***out_params, int *out_nparams, int *out_ctor_dtor, char *out_qname)
{
    (void)rbp_stack_off;
    Type *base=0;
    *is_func=0; *out_nparams=0; *is_typedef=0; *out_ctor_dtor=0;
    if (out_qname) out_qname[0]=0;

    /* 前缀说明符: typedef/static/const/extern/... (存储类和限定符, 解析后忽略) */
    while(1) {
        if (L_accept(T_TYPEDEF)) { *is_typedef=1; continue; }
        /* [新增] 存储类/限定符: 解析后忽略 (不影响代码生成) */
        if (L_accept(T_CONST)) continue;
        if (L_accept(T_STATIC)) continue;
        if (L_accept(T_EXTERN)) continue;
        if (L_accept(T_VOLATILE)) continue;
        if (L_accept(T_REGISTER)) continue;
        /* [新增] _Alignas(type-or-expr): 对齐说明符, 解析后忽略
         * (本简化编译器不实现自定义对齐, 仅语法上接受并跳过 _Alignas(...)) */
        if (L_check(T_ALIGNAS)) {
            L_next();
            L_expect(T_LPAREN, "(");
            int d = 1;
            while (d > 0 && !L_check(T_EOF)) {
                if (L_check(T_LPAREN)) d++;
                else if (L_check(T_RPAREN)) { d--; if (d==0) break; }
                L_next();
            }
            L_expect(T_RPAREN, ")");
            continue;
        }
        if (L_check(T_INT)) { base = t_int; L_next(); break; }
        if (L_check(T_CHAR)){ base = t_char; L_next(); break; }
        if (L_check(T_VOID)){ base = t_void; L_next(); break; }
        if (L_check(T_LONG)){ base = t_long; L_next(); break; }
        if (L_check(T_FLOAT_KW)) { base = t_float; L_next(); break; }
        if (L_check(T_DOUBLE_KW)) { base = t_double; L_next(); break; }
        if (L_check(T_SHORT)) { base = t_int; L_next(); break; }  /* short → int */
        if (L_check(T_UNSIGNED)||L_check(T_SIGNED)) { int uns=L_check(T_UNSIGNED); L_next();
            /* [修复] 每个分支自己 L_next 消费对应类型 token, 避免外面多吃一个 token (变量名) */
            if (L_check(T_INT)) { base=t_int; L_next(); }
            else if (L_check(T_LONG)) { base=t_long; L_next(); }
            else if (L_check(T_CHAR)) { base=t_char; L_next(); }
            else if (L_check(T_SHORT)) { base=t_int; L_next(); }
            else base = t_int;  /* 单独 unsigned → 当 int */
            (void)uns;
            break;
        }
        /* [新增] enum 处理: enum Tag { A=1, B, C } 或 enum Tag var */
        if (L_check(T_ENUM)) {
            L_next();
            /* 可选 enum 名 */
            if (L_check(T_IDENT)) {
                char ename[128]; int ei=0;
                while(L.sbuf[ei]&&ei<127){ename[ei]=L.sbuf[ei];ei++;}
                ename[ei]=0; L_next();
                /* 注册为类型 (int 别名) */
                if (L_check(T_LBRACE)) {
                    /* enum 定义体: 解析常量, 注册为全局符号 */
                    L_next();
                    int next_val = 0;
                    while (!L_check(T_RBRACE) && !L_check(T_EOF)) {
                        if (!L_check(T_IDENT)) { L_next(); continue; }
                        char cname[128]; int ci=0;
                        while(L.sbuf[ci]&&ci<127){cname[ci]=L.sbuf[ci];ci++;}
                        cname[ci]=0; L_next();
                        if (L_accept(T_ASSIGN)) {
                            /* 显式赋值: = 常量表达式 (简化: 只支持数字/标识符) */
                            if (L_check(T_MINUS)) { L_next(); next_val = -L.num; L_next(); }
                            else if (L_check(T_IDENT)) {
                                Symbol *cs = sym_find_global(L.sbuf); next_val = cs ? (int)cs->addr : 0; L_next();
                            } else { next_val = L.num; L_next(); }
                        }
                        /* 注册 enum 常量为全局 int 符号 */
                        Symbol *es = sym_add(cname, SK_CONST, t_int);
                        es->addr = next_val;
                        next_val++;
                        if (L_accept(T_COMMA)) continue;
                    }
                    L_expect(T_RBRACE, "}");
                }
                /* 注册 enum 名为类型别名 (int) */
                sym_add(ename, SK_TYPE, t_int);
                base = t_int;
                break;
            }
            if (L_check(T_LBRACE)) {
                /* 匿名 enum: enum { A, B } */
                L_next();
                int next_val = 0;
                while (!L_check(T_RBRACE) && !L_check(T_EOF)) {
                    if (!L_check(T_IDENT)) { L_next(); continue; }
                    char cname[128]; int ci=0;
                    while(L.sbuf[ci]&&ci<127){cname[ci]=L.sbuf[ci];ci++;}
                    cname[ci]=0; L_next();
                    if (L_accept(T_ASSIGN)) {
                        if (L_check(T_MINUS)) { L_next(); next_val = -L.num; L_next(); }
                        else if (L_check(T_IDENT)) {
                            Symbol *cs = sym_find_global(L.sbuf); next_val = cs ? (int)cs->addr : 0; L_next();
                        } else { next_val = L.num; L_next(); }
                    }
                    Symbol *es = sym_add(cname, SK_CONST, t_int);
                    es->addr = next_val;
                    next_val++;
                    if (L_accept(T_COMMA)) continue;
                }
                L_expect(T_RBRACE, "}");
                base = t_int;
                break;
            }
            /* enum 后无名称无体: 退化为 int */
            base = t_int; break;
        }
        /* [新增] union 处理: 简化为 struct (所有成员偏移 0) */
        if (L_check(T_UNION)) {
            L_next();
            if (!L_check(T_IDENT)) g_panic("union name");
            Type *st = struct_find(L.sbuf);
            if (!st) { st = ty_struct(L.sbuf); struct_register(L.sbuf, st); st->size=0; }
            base = st; L_next();
            break;
        }
        if (L_check(T_STRUCT)||L_check(T_CLASS)) { int cls=L_check(T_CLASS); L_next();
            if (!L_check(T_IDENT)) g_panic("struct/class name");
            Type *st = struct_find(L.sbuf);
            if (!st) { st = ty_struct(L.sbuf); struct_register(L.sbuf, st); st->size=0; }
            base = st; L_next();
            (void)cls;
            break;
        }
        if (L_check(T_IDENT)) {
            Symbol *sy = sym_find(L.sbuf, g_scope_depth);
            if (sy && sy->kind==SK_TYPE) { base = sy->type; L_next(); break; }
            else { /* 无类型前缀, 这是 "函数名(...)" 的隐式声明? → 默认返回 int. 我们必须 name_out 放这个名字, base=int */
                if (name_out) { int i=0;while(L.sbuf[i]){name_out[i]=L.sbuf[i];i++;} name_out[i]=0; }
                base = t_int;
                goto parse_mods;
            }
        }
        break;
    }
    /* 取名字 (没取的情况下) */
    if (name_out && !name_out[0]) {
        if (L_check(T_IDENT)) { int i=0;while(L.sbuf[i]){name_out[i]=L.sbuf[i];i++;} name_out[i]=0; L_next(); }
        else if (L_check(T_TILDE)) { /* ~ClassName → 析构函数名 "ClassName" */
            L_next();
            if (L_check(T_IDENT)) { int i=0;while(L.sbuf[i]){name_out[i]=L.sbuf[i];i++;} name_out[i]=0; L_next(); *out_ctor_dtor=2; }
        }
    }
parse_mods:
    /* [新增] 函数指针声明: int (*fp)(int)
     * 语法: ( * name ) ( params )
     * 简化处理: 将 base 变为 ptr-to-func, 提取 name, 然后跳过 (params) */
    if (L_check(T_LPAREN) && L_peek(1)=='*') {
        L_next(); /* ( */
        L_next(); /* * */
        /* base 变为指向 base 的指针 */
        base = ty_ptr(base);
        /* 提取名字 */
        if (name_out && !name_out[0]) {
            if (L_check(T_IDENT)) {
                int i=0; while(L.sbuf[i]){name_out[i]=L.sbuf[i];i++;} name_out[i]=0;
                L_next();
            }
        }
        L_expect(T_RPAREN, ")");
        /* 跳过参数列表 (params) */
        if (L_check(T_LPAREN)) {
            int depth = 1; L_next();
            while (depth > 0 && !L_check(T_EOF)) {
                if (L_check(T_LPAREN)) depth++;
                else if (L_check(T_RPAREN)) depth--;
                if (depth > 0) L_next();
            }
            L_next(); /* ) */
        }
        /* 跳过数组修饰符 */
        while (L_check(T_LBRACK)) { L_next(); int n=L.num; if (!L_accept(T_NUM)) n=0; L_expect(T_RBRACK,"]"); base = ty_arr(base, n); }
        goto parse_done;
    }
    /* 指针修饰符 */
    while (L_accept(T_STAR)) base = ty_ptr(base);
    /* 数组修饰符 */
    while (L_check(T_LBRACK)) { L_next(); int n=L.num; if (!L_accept(T_NUM)) n=0; L_expect(T_RBRACK,"]"); base = ty_arr(base, n); }
    /* 函数: (params) */
    if (L_check(T_LPAREN)) {
        L_next();
        Type *ps[16]; int np=0; int va=0;
        if (!L_check(T_RPAREN)) for(;;) {
            /* 简化: 只支持 类型 [参数名] 且类型是 int/char/void/struct/type*/
            Type *pt = 0;
            if (L_check(T_INT)) { pt=t_int; L_next(); }
            else if (L_check(T_CHAR)){ pt=t_char; L_next(); }
            else if (L_check(T_VOID)){ pt=t_void; L_next();
                if (L_check(T_RPAREN)) break;
            }
            else if (L_check(T_LONG)){ pt=t_long; L_next(); }
            else if (L_check(T_SHORT)){ pt=t_int; L_next(); }
            /* [新增] unsigned/signed 参数类型 */
            else if (L_check(T_UNSIGNED)||L_check(T_SIGNED)) { L_next();
                if (L_check(T_INT)) { pt=t_int; L_next(); }
                else if (L_check(T_LONG)) { pt=t_long; L_next(); }
                else if (L_check(T_CHAR)) { pt=t_char; L_next(); }
                else if (L_check(T_SHORT)) { pt=t_int; L_next(); }
                else pt = t_int;
            }
            /* [新增] float/double 参数类型 */
            else if (L_check(T_FLOAT_KW)) { pt=t_float; L_next(); }
            else if (L_check(T_DOUBLE_KW)) { pt=t_double; L_next(); }
            else if (L_check(T_STRUCT)) { L_next(); pt = struct_find(L.sbuf); if (!pt){pt=ty_struct(L.sbuf); struct_register(L.sbuf, pt);} L_next(); if (!pt) pt=t_int; }
            else if (L_check(T_IDENT)) { Symbol *sy=sym_find(L.sbuf, g_scope_depth); if(sy&&sy->kind==SK_TYPE){pt=sy->type;L_next();} else {pt=t_int;} }
            else g_panic("param type");
            while(L_accept(T_STAR)) pt=ty_ptr(pt);
            /* 参数名忽略 (只位置参数) */
            if (L_check(T_IDENT)) L_next();
            ps[np++] = pt; if (np>=16) break;
            if (L_accept(T_COMMA)) continue; break;
        }
        (void)va;
        L_expect(T_RPAREN,")");
        *out_params = g_malloc(sizeof(Type*)*np);
        for(int i=0;i<np;i++) (*out_params)[i]=ps[i];
        *out_nparams = np;
        base = ty_func(base, *out_params, np, 0);
        *is_func = 1;
    }
parse_done:
    return base;
}

/* 声明 (全局或局部): 可以是
 *   变量: int x; / struct S s = {...}; (初始化简化: 仅 int = NUM)
 *   typedef: typedef int foo;
 *   函数定义: int main() { ... }
 *   struct 定义: struct S { ... };
 *   C++ 方法: struct 内成员函数声明 (函数体稍后在外面定义) */

/* [新增] _Static_assert(cond, "msg"); 解析 (顶层与块内通用).
 * 语法: _Static_assert ( 常量表达式 [, 字符串字面量] ) ;
 * 实现: 捕获条件表达式的原始文本, 把其中的 sizeof(type) 替换成数值,
 *       再用 pp_eval_const 求值; 若为 0 则打印消息并 g_panic. */
static void parse_static_assert(void) {
    L_next();                       /* 消费 _Static_assert */
    L_expect(T_LPAREN, "(");
    int expr_start = L.tok_start;   /* 条件表达式首 token 起始位置 */
    int depth = 0;
    for (;;) {
        if (L_check(T_EOF)) g_panic("_Static_assert: EOF in expr");
        if (L_check(T_LPAREN)) { depth++; L_next(); continue; }
        if (L_check(T_RPAREN)) { if (depth == 0) break; depth--; L_next(); continue; }
        if (L_check(T_COMMA) && depth == 0) break;
        L_next();
    }
    int expr_end = L.tok_start;
    int expr_len = expr_end - expr_start;
    if (expr_len < 0) expr_len = 0;
    char ebuf[512];
    int raw_len = (expr_len < 511) ? expr_len : 511;
    int elen = subst_sizeof_in_expr(L.src + expr_start, raw_len, ebuf, 512);
    long cond = pp_eval_const(ebuf, elen);
    /* 可选消息字符串 (C23 允许省略) */
    char msg[256]; msg[0] = 0;
    if (L_accept(T_COMMA)) {
        if (L_check(T_STR)) {
            int mi = 0; while (L.sbuf[mi] && mi < 255) { msg[mi] = L.sbuf[mi]; mi++; }
            msg[mi] = 0;
            L_next();
        }
    }
    L_expect(T_RPAREN, ")");
    L_expect(T_SEMI, ";");
    if (cond == 0) {
        /* 失败时把表达式完整信息写串口, 协助定位 */
        com1_str("\r\n[_Static_assert failed line="); com1_dec(L.line); com1_str("]\r\n  raw expr (len="); com1_dec(raw_len); com1_str("): '");
        for (int i=0;i<raw_len;i++) com1_raw((L.src+expr_start)[i] ? (L.src+expr_start)[i] : '.');
        com1_str("'\r\n  subst (len="); com1_dec(elen); com1_str("): '");
        for (int i=0;i<elen;i++) com1_raw(ebuf[i] ? ebuf[i] : '.');
        com1_str("'\r\n  cond=0 (evaluated false)\r\n");
        g_print("efcc: _Static_assert failed at line "); g_print_int(L.line); g_print("\n");
        if (msg[0]) { g_print("  msg: "); g_print(msg); g_print("\n"); }
        g_panic("_Static_assert failed");
    }
}

/* 前向: 解析块内声明 (在 parse_stmt 里作为 extern 调用) */
void parse_declaration(int *xxx, int *yyy, int rbp_stack_off) {
    /* [新增] 块内 _Static_assert(cond, "msg"); — 复用顶层逻辑 */
    if (L_check(T_STATIC_ASSERT)) { parse_static_assert(); return; }
    (void)xxx; (void)yyy;
    char name[128]={0}; int is_td=0, is_fn=0, cdtor=0;
    Type **params=0; int nparams=0; char qn[256]={0};
    Type *full = parse_spec_and_decl(name, &is_td, &is_fn, rbp_stack_off, &params, &nparams, &cdtor, qn);
    if (!name[0]) { g_panic("decl missing name"); return; }
    if (is_td) {
        /* 把当前 name 作为 type alias 注册 */
        sym_add(name, SK_TYPE, full);
        L_expect(T_SEMI,";");
        return;
    }
    /* 变量 / 函数 */
    if (is_fn) {
        if (L_check(T_LBRACE)) { /* 函数定义 */
            /* 函数名 → 全局符号 */
            Symbol *fn = sym_find_global(name);
            if (!fn) fn = sym_add(name, SK_FUNC, full);
            /* 记录函数地址 (当前 text pos) */
            long lab = new_label(); def_label(lab);
            fn->addr = va_text_base + g_text_n;
            fn->defined = 1;
            g_scope_depth++;
            /* 栈帧: push rbp; mov rsp,rbp; 留空间给局部变量; rbp_stack_off 从 16 开始 (rbp-0 是 saved rbp) */
            emit_push(5);           /* push rbp */
            emit_mov_rr(5,4);       /* mov rbp,rsp */
            /* 前 6 个参数已经在寄存器 rdi,rsi,rdx,rcx,r8,r9 → 我们保存到 rbp-8,-16,-24,-32,-40,-48 */
            int rbp_off = 0;
            int reg_map[6]={7,6,2,1,8,9};
            for(int i=0;i<nparams && i<6;i++) {
                rbp_off += 8;
                emit_mov_mr(5, -rbp_off, reg_map[i]);
                Symbol *v = sym_add("__p", SK_VAR, params?params[i]:t_int);
                v->scope = g_scope_depth; v->is_global=0; v->addr = rbp_off;
            }
            /* 参数名我在 parse_spec_and_decl 里丢了. 简化: 不给参数名访问 (参数只能位置). 我们仍然允许用户通过在函数里再声明同名局部变量? 不行.
             * 解决: parse_spec_and_decl 中我们把参数名数组也一起吐出. 为了简化, 允许函数参数用位置访问, 或不命名参数. 大多数函数用前几个, 我们在 parse_func_body 用独立函数重新解析更好. 这里暂且: 给参数统一起名为 __p0,__p1... 用户如果在函数体写变量名会报错 — 这个限制用户可接受. */
            /* 实际用户需求: 普通 C 写法 int add(int a,int b){return a+b;}. 我们需要参数名.
             * 重新改 parse_spec_and_decl: 在参数解析时记录 param 名. 因为时间关系, 我用一个简单策略:
             *   用独立函数 parse_func_definition 重写参数解析 (同时拿类型+名). 下面开始处理函数定义时我们直接重写一个专门版本. */
            /* 目前版本参数没名字 → 编译 add.c 会崩溃. 我们先用一种办法: 在函数块 {} 里面手动"注入"同名参数符号. */
            /* 但我已经把名字抛掉. 这里选择: 在 parse_declaration 被 parse_stmt 调用的上下文 (局部声明) 中返回. 而 parse_func 入口我们写一个独立函数来做完整的函数定义 (包括参数名). 所以对 parse_declaration 中遇到的函数定义, 我们直接报错. */
            g_print("Function body without parameter names: "); g_print(name); g_print("\n");
            g_panic("please use parse_func_definition");
        } else {
            /* 前向声明 (例如 int add(int,int);) */
            Symbol *fn = sym_find_global(name);
            if (!fn) fn = sym_add(name, SK_FUNC, full);
            fn->addr = 0; fn->defined = 0;
            L_expect(T_SEMI,";");
            return;
        }
    }
    /* 变量 */
    if (g_scope_depth == 0) {
        /* 全局变量: 放到 .data 或 .bss */
        Symbol *v = sym_add(name, SK_VAR, full);
        v->scope = 0; v->is_global = 1;
        long init_val = 0;
        int has_init = 0;
        if (L_accept(T_ASSIGN)) {
            /* [新增] 聚合初始化: {1, 2, 3} 或 {expr, expr, ...}
             * 支持 C99 指定初始化:
             *   [idx]=v          指定数组下标
             *   .field=v         指定结构体字段
             *   [idx].field=v    数组下标后字段 (简化: 同数组元素的字段)
             * 元素按 8 字节对齐写入. */
            if (L_check(T_LBRACE)) {
                L_next();
                /* 先确定变量总大小: 数组用 array_len*8, 结构体用 size, 至少 8 */
                int var_sz = 8;
                if (full->kind == TY_ARRAY) {
                    var_sz = (full->array_len > 0 ? full->array_len : 1) * 8;
                } else if (full->kind == TY_STRUCT) {
                    var_sz = full->size > 0 ? full->size : 8;
                    if (var_sz < 8) var_sz = 8;
                }
                if (var_sz > 8192) var_sz = 8192;  /* 防御 */
                /* 用临时缓冲累积初始化值 */
                char initbuf[8192]; for (int i=0;i<var_sz;i++) initbuf[i] = 0;
                int cur_off = 0;       /* 当前写入偏移 (字节) */
                int max_off = var_sz; /* 写到最大偏移 */
                if (!L_check(T_RBRACE)) for(;;) {
                    /* 处理设计符前缀: [N] 和 .field (可链式) */
                    int designator_seen = 0;
                    while (L_check(T_LBRACK) || L_check(T_DOT)) {
                        if (L_check(T_LBRACK)) {
                            L_next();
                            /* 下标表达式: 简化只支持常量 */
                            long idx = 0;
                            if (L_check(T_NUM)) { idx = L.num; L_next(); }
                            else if (L_check(T_MINUS) && L_peek(1)>='0' && L_peek(1)<='9') {
                                L_next(); idx = -L.num; L_next();
                            } else {
                                ExprRes e = parse_expr(0,1); sm_drop(1);
                            }
                            L_expect(T_RBRACK, "]");
                            cur_off = (int)idx * 8;
                            if (cur_off > var_sz - 8) cur_off = var_sz - 8;
                            if (cur_off < 0) cur_off = 0;
                            designator_seen = 1;
                        } else if (L_check(T_DOT)) {
                            L_next();
                            /* 字段名: 在 full 中查找字段偏移 */
                            char fname[128]; int fi = 0;
                            if (L_check(T_IDENT)) {
                                int si=0; while(L.sbuf[si] && fi<127) fname[fi++]=L.sbuf[si++];
                                fname[fi] = 0;
                                L_next();
                            } else {
                                fname[0] = 0;
                            }
                            /* 查找字段偏移 (使用 struct_find_field 辅助函数) */
                            Field *fld = struct_find_field(full, fname);
                            if (fld) {
                                cur_off = fld->offset;
                                if (cur_off > var_sz - 8) cur_off = var_sz - 8;
                                if (cur_off < 0) cur_off = 0;
                            }
                            /* 否则字段不存在, 保持当前偏移 (best-effort) */
                            designator_seen = 1;
                        }
                        /* = 出现, 跳过它 (C99 语法 [N]=v / .f=v) */
                        if (L_check(T_ASSIGN)) L_next();
                    }
                    /* 每个 init 元素: 常量或表达式 */
                    if (L_check(T_NUM)) {
                        long val = L.num;
                        char b[8]; for(int i=0;i<8;i++) b[i]=(char)((val>>(i*8))&0xFF);
                        for (int i=0;i<8 && cur_off+i<var_sz;i++) initbuf[cur_off+i] = b[i];
                        cur_off += 8;
                        L_next();
                    } else if (L_check(T_STR)) {
                        int sl; long va = rodata_puts(L.sbuf, &sl); L_next();
                        char b[8]; for(int i=0;i<8;i++) b[i]=(char)((va>>(i*8))&0xFF);
                        for (int i=0;i<8 && cur_off+i<var_sz;i++) initbuf[cur_off+i] = b[i];
                        cur_off += 8;
                    } else if (L_check(T_LBRACE)) {
                        /* 嵌套聚合: 简化为递归展开 (本实现只展一层, 内层按值写入) */
                        L_next();
                        int inner_off = cur_off;
                        if (!L_check(T_RBRACE)) for(;;) {
                            if (L_check(T_NUM)) {
                                long val = L.num;
                                char b[8]; for(int i=0;i<8;i++) b[i]=(char)((val>>(i*8))&0xFF);
                                for (int i=0;i<8 && inner_off+i<var_sz;i++) initbuf[inner_off+i] = b[i];
                                inner_off += 8;
                                L_next();
                            } else if (L_check(T_STR)) {
                                int sl; long va = rodata_puts(L.sbuf, &sl); L_next();
                                char b[8]; for(int i=0;i<8;i++) b[i]=(char)((va>>(i*8))&0xFF);
                                for (int i=0;i<8 && inner_off+i<var_sz;i++) initbuf[inner_off+i] = b[i];
                                inner_off += 8;
                            } else {
                                ExprRes e = parse_expr(0, 1); sm_drop(1);
                            }
                            if (L_accept(T_COMMA)) continue;
                            break;
                        }
                        L_expect(T_RBRACE, "}");
                        cur_off = inner_off;
                        (void)designator_seen;
                    } else {
                        /* 其他表达式: 跳过 (简化) */
                        ExprRes e = parse_expr(0, 1); sm_drop(1);
                        cur_off += 8;
                    }
                    if (L_accept(T_COMMA)) {
                        /* 末尾允许 trailing comma: {1, 2, 3,} */
                        if (L_check(T_RBRACE)) break;
                        continue;
                    }
                    break;
                }
                L_expect(T_RBRACE, "}");
                /* 一次性写入数据段 */
                v->addr = va_data_base + g_data_n;
                EMIT_D(initbuf, max_off);
                has_init = 2;  /* 2 = 聚合初始化已完成, 跳过下方标量/BSS 分支 */
            } else if (L_check(T_NUM)) { init_val = L.num; L_next(); has_init = 1; }
            else if (L_check(T_NUM_FLOAT)) {
                /* 浮点常量初始化: 把 double 位模式作为 8B 写入 .data.
                 * 用字节拷贝安全转换 double → long 位模式 (避免 strict-aliasing / -O 问题). */
                double dv = L.fval; long bits = 0;
                { int i; for (i=0;i<8;i++) ((char*)&bits)[i] = ((char*)&dv)[i]; }
                init_val = bits; L_next(); has_init = 1;
            }
            else if (L_check(T_STR)) {
                int sl; long va = rodata_puts(L.sbuf, &sl); L_next();
                init_val = va; has_init = 1;
            } else if (L_check(T_MINUS) && L_peek(1)>='0' && L_peek(1)<='9') {
                /* 负常量: -N 或 -3.14. 先吃 '-', 再看下一个 token 是 T_NUM 还是 T_NUM_FLOAT */
                L_next();
                if (L_check(T_NUM)) { init_val = -L.num; L_next(); has_init = 1; }
                else if (L_check(T_NUM_FLOAT)) {
                    double dv = -L.fval; long bits = 0;
                    { int i; for (i=0;i<8;i++) ((char*)&bits)[i] = ((char*)&dv)[i]; }
                    init_val = bits; L_next(); has_init = 1;
                }
            }
        }
        if (has_init == 1) {
            /* 标量初始化: init_val 写入 .data */
            v->addr = va_data_base + g_data_n;
            char b[8]; for(int i=0;i<8;i++) b[i]=(char)((init_val>>(i*8))&0xFF);
            /* 变量大小按 full->size, 但至少 8B (为了对齐) */
            int sz = full->size < 8 ? 8 : full->size;
            for (int i=0;i<sz;i++) { char x=(i<8?b[i]:0); EMIT_D(&x,1); }
        } else if (has_init == 0) {
            /* BSS: 不写入文件, 记录地址与大小 */
            int align = full->align>0?full->align:8;
            if (g_bss_len % align) g_bss_len += align - (g_bss_len%align);
            v->addr = va_bss_base + g_bss_len;
            int sz = full->size < 8 ? 8 : full->size;
            g_bss_len += sz;
        }
        /* has_init == 2: 聚合初始化已在上面完成, v->addr 和数据均已就绪 */
        L_expect(T_SEMI,";");
        return;
    } else {
        /* 局部变量: rbp 下面分配 (每个 8B 对齐) */
        int sz = full->size < 8 ? 8 : full->size;
        int off = 0;
        extern int g_local_offset;
        g_local_offset += sz;
        off = g_local_offset;
        Symbol *v = sym_add(name, SK_VAR, full);
        v->is_global = 0; v->scope = g_scope_depth; v->addr = off;
        /* sub rsp, sz  在函数序已经 sub 了? 函数序: push rbp; mov rbp,rsp; 然后我们按需每次变量 sub rsp,8.
         * 简化: 函数结尾需要恢复, 我们每分配一个局部变量, 发射一条 sub rsp,sz 保证栈平衡. */
        if (sz==8) { emit_movi_imm32(0,8); emit_sub_rr(4,0); }
        else { emit_movi_imm32(0,sz); emit_sub_rr(4,0); }
        if (L_accept(T_ASSIGN)) {
            /* [新增] 聚合初始化: {1, 2, 3} → 逐个写入局部变量
             * 支持 C99 设计符 [idx]=v (.field=v 简化为不调整偏移) */
            if (L_check(T_LBRACE)) {
                L_next();
                int elem_off = 0;
                if (!L_check(T_RBRACE)) for(;;) {
                    /* 设计符前缀 */
                    while (L_check(T_LBRACK) || L_check(T_DOT)) {
                        if (L_check(T_LBRACK)) {
                            L_next();
                            long idx = 0;
                            if (L_check(T_NUM)) { idx = L.num; L_next(); }
                            else if (L_check(T_MINUS) && L_peek(1)>='0' && L_peek(1)<='9') {
                                L_next(); idx = -L.num; L_next();
                            } else { ExprRes e = parse_expr(rbp_stack_off,1); sm_drop(1); }
                            L_expect(T_RBRACK, "]");
                            elem_off = (int)idx * 8;
                            if (elem_off < 0) elem_off = 0;
                        } else {
                            /* .field: 用 struct_find_field 查字段偏移 */
                            L_next();
                            char fname[128]; int fi = 0;
                            if (L_check(T_IDENT)) {
                                int si=0; while(L.sbuf[si] && fi<127) fname[fi++]=L.sbuf[si++];
                                fname[fi] = 0;
                                L_next();
                            } else {
                                fname[0] = 0;
                            }
                            Field *fld = struct_find_field(full, fname);
                            if (fld) {
                                elem_off = fld->offset;
                                if (elem_off < 0) elem_off = 0;
                            }
                        }
                        if (L_check(T_ASSIGN)) L_next();
                    }
                    ExprRes ie = parse_expr(rbp_stack_off, 1);
                    sm_pop_r(0);
                    emit_mov_mr(5, -(off + elem_off), 0);
                    elem_off += 8;
                    if (L_accept(T_COMMA)) {
                        if (L_check(T_RBRACE)) break;  /* trailing comma */
                        continue;
                    }
                    break;
                }
                L_expect(T_RBRACE, "}");
            } else {
                ExprRes initv = parse_expr(rbp_stack_off,1);
                sm_pop_r(0);
                emit_mov_mr(5, -off, 0);
            }
        }
        L_expect(T_SEMI,";");
        return;
    }
}

/* 重写: 函数定义解析 (同时拿参数类型+名字, 配合前 6 个寄存器保存到本地槽, 尾部分配局部变量) */
static int parse_func_definition(int rbp_stack_off) {
    (void)rbp_stack_off;
    dbg_com1('F');  /* parse_func_definition entered */
    /* [修复] 保存完整 lexer 快照, 若判定为非函数语法 (例如 float g_f = 3.14;), 完整回滚后返回 0,
     * 让 do_compile 回退到 parse_declaration 处理全局变量. 快照包含所有可变字段. */
    int snap_pos = L.pos, snap_tok = L.tok;
    char snap_sbuf[256]; g_memcpy(snap_sbuf, L.sbuf, 256);
    int snap_slen = L.slen, snap_line = L.line;
    int snap_tok_start = L.tok_start, snap_num = L.num;
    double snap_fval = L.fval;
#define L_ROLLBACK() do { \
        L.pos = snap_pos; L.tok = snap_tok; g_memcpy(L.sbuf, snap_sbuf, 256); \
        L.slen = snap_slen; L.line = snap_line; L.tok_start = snap_tok_start; \
        L.num = snap_num; L.fval = snap_fval; \
    } while(0)

    char name[128]={0}; int is_td=0,is_fn=0,cdtor=0;
    Type *params=0; int nparams=0; char qn[256]={0};
    dbg_com1('G');  /* after local vars */
    /* 先 parse 基础类型 + 名字 + (params). 但 parse_spec_and_decl 丢参数名. 我们手写一个简化版本: */
    /* 返回类型 (必须, 或 ~className 析构, 或 className 构造) */
    Type *ret = t_int; int had_ret=0;
    /* 先看是否是 "方法全名" ClassName::MethodName (在全局 scope) */
    if (L_check(T_IDENT)) {
        /* 向前看 T_SCOPE. 有则这是方法定义 */
        char cn[128]={0}; int i=0;while(L.sbuf[i]){cn[i]=L.sbuf[i];i++;}
        int save_pos = L.pos, save_tok=L.tok; char save_sbuf[256]; g_memcpy(save_sbuf,L.sbuf,256); int save_ln=L.line, save_num=L.num;
        L_next();
        if (L_check(T_SCOPE)) { /* 方法全名 */
            L_next();
            if (!L_check(T_IDENT)) g_panic("method name");
            int n=0; i=0;while(cn[i]){qn[n++]=cn[i++];i=0;} qn[n++]=':'; qn[n++]=':';
            while(L.sbuf[i]){qn[n++]=L.sbuf[i++];i=0;} qn[n]=0;
            /* 取方法本名 */
            char mname[128]; i=0; while(qn[n-1-i]!=':' && i<n){ mname[i]=qn[n-1-i]; i++; }
            char rev[128]; for(int k=0;k<i;k++) rev[k]=mname[i-1-k]; rev[i]=0; /* mname = 本名 */
            /* 方法的返回类型 & 参数列表: (Type* params...,) → 若 mname == className 是构造 (ret void); 若 rev[0]=='~' 且之后等于 className 是析构 (ret void) */
            Type *st = struct_find(cn); if (!st) g_panic("method class not defined");
            /* 简化: 方法定义必须有完整类型. 我们先从 struct field 里找回先前类型. */
            /* 但目前 parse_struct_body 没保存 params 信息 (仅 SK_FUNC 全局符号).
             * 我们直接重新解析返回类型 + 参数列表. 当作普通函数. */
            g_print("defining method: "); g_print(qn); g_print("\n");
            /* 用 cn, rev 作为名字, 完整符号名 qn. 解析 "返回类型": 构造/析构 ret=void. */
            ret = t_void;
            /* 解析参数列表, 直接跳. 这时 TOK 刚读完 "methodName", token 在 ( 处? */
            goto parse_params_and_body;
        } else {
            /* 回滚: 这不是方法 */
            L.pos = save_pos; L.tok = save_tok; g_memcpy(L.sbuf,save_sbuf,256); L.line=save_ln; L.num=save_num;
        }
    }
    /* 返回类型 (非方法) */
    if (L_check(T_INT))       { ret = t_int; L_next(); had_ret=1; }
    else if (L_check(T_CHAR)) { ret = t_char; L_next(); had_ret=1; }
    else if (L_check(T_VOID)) { ret = t_void; L_next(); had_ret=1; }
    else if (L_check(T_LONG)) { ret = t_long; L_next(); had_ret=1; }
    else if (L_check(T_SHORT)){ ret = t_int; L_next(); had_ret=1; }
    /* [新增] unsigned/signed 返回类型 */
    else if (L_check(T_UNSIGNED) || L_check(T_SIGNED)) {
        L_next();
        if (L_check(T_INT)) { ret=t_int; L_next(); }
        else if (L_check(T_LONG)) { ret=t_long; L_next(); }
        else if (L_check(T_CHAR)) { ret=t_char; L_next(); }
        else if (L_check(T_SHORT)) { ret=t_int; L_next(); }
        else ret = t_int;
        had_ret = 1;
    }
    /* [新增] float/double 返回类型 */
    else if (L_check(T_FLOAT_KW))  { ret = t_float;  L_next(); had_ret=1; }
    else if (L_check(T_DOUBLE_KW)) { ret = t_double; L_next(); had_ret=1; }
    else if (L_check(T_STRUCT)) { L_next(); ret = struct_find(L.sbuf); if (!ret){ret=ty_struct(L.sbuf); struct_register(L.sbuf, ret);} L_next(); had_ret=1; }
    else if (L_check(T_IDENT)) {
        Symbol *sy=sym_find(L.sbuf,g_scope_depth);
        if (sy && sy->kind==SK_TYPE) { ret = sy->type; L_next(); had_ret=1; }
        else { ret = t_int; /* 没类型: 默认 int, 且这个 IDENT 实际就是函数名 */
            int i=0;while(L.sbuf[i]){name[i]=L.sbuf[i];i++;} name[i]=0; L_next(); had_ret=2;
        }
    } else if (L_check(T_TILDE)) {
        L_next(); if (L_check(T_IDENT)) { /* ~ClassName: 析构, 视为 ClassName */ int i=0;while(L.sbuf[i]){name[i]=L.sbuf[i];i++;}name[i]=0;L_next();}
        ret = t_void; cdtor = 2; had_ret=1;
    }
    if (!name[0] && had_ret<2) {
        /* 函数名 (紧跟返回类型之后). 可能是 ClassName (构造) */
        if (L_check(T_IDENT)) {
            int i=0; while(L.sbuf[i]){name[i]=L.sbuf[i];i++;} name[i]=0;
            L_next();
            /* 判断是否是 struct name → 构造函数 */
            Type *st = struct_find(name);
            if (st && L_check(T_LPAREN)) { ret = t_void; cdtor = 1; }
        }
    }
parse_params_and_body:
    /* [修复] 在此处判断是函数语法还是变量语法.
     * 只有下一个 token 是 T_LPAREN '(' 才是函数 (定义或前向声明).
     * 否则 (常见: T_ASSIGN '=', T_SEMI ';', T_COMMA ',', T_LBRACK '[') → 全局/局部变量声明,
     * 完整回滚 lexer 到函数入口, 返回 0 让调用方走 parse_declaration 分支. */
    if (!L_check(T_LPAREN)) {
        dbg_com1('V');  /* V = 识别为变量声明语法, 回滚 */
        L_ROLLBACK();
#undef L_ROLLBACK
        return 0;
    }
    L_next();  /* 消费 T_LPAREN '(' */
    /* 解析参数类型+名字 → nparams, param_types, param_names */
    Type *ptypes[16]; char pnames[16][64]; int np=0;
    int func_is_vararg = 0;   /* 是否是 variadic (...) 函数 */
    if (!L_check(T_RPAREN)) for(;;) {
        /* 检测 "..." 可变参数标记 */
        if (L_peek(0)=='.' && L_peek(1)=='.' && L_peek(2)=='.') {
            L_next(); L_next(); L_next();
            func_is_vararg = 1;
            break;
        }
        Type *pt = t_int;
        /* 类型 */
        if (L_check(T_INT)) { pt=t_int; L_next(); }
        else if (L_check(T_CHAR)){ pt=t_char; L_next(); }
        else if (L_check(T_VOID)){ pt=t_void; L_next();
            if (L_check(T_RPAREN)) break;
        }
        else if (L_check(T_LONG)){ pt=t_long; L_next(); }
        else if (L_check(T_SHORT)){ pt=t_int; L_next(); }
        /* [新增] unsigned/signed: 简化为对应有符号类型 (本编译器无独立 unsigned) */
        else if (L_check(T_UNSIGNED)||L_check(T_SIGNED)) { L_next();
            if (L_check(T_INT)) { pt=t_int; L_next(); }
            else if (L_check(T_LONG)) { pt=t_long; L_next(); }
            else if (L_check(T_CHAR)) { pt=t_char; L_next(); }
            else if (L_check(T_SHORT)) { pt=t_int; L_next(); }
            else pt = t_int;
        }
        /* [新增] float/double 参数类型 */
        else if (L_check(T_FLOAT_KW)) { pt=t_float; L_next(); }
        else if (L_check(T_DOUBLE_KW)) { pt=t_double; L_next(); }
        else if (L_check(T_STRUCT)) { L_next(); pt = struct_find(L.sbuf); if (!pt){pt=ty_struct(L.sbuf); struct_register(L.sbuf, pt);} L_next(); }
        else if (L_check(T_IDENT)) { Symbol *sy=sym_find(L.sbuf, g_scope_depth); if(sy&&sy->kind==SK_TYPE){pt=sy->type; L_next();} else {pt=t_int;} }
        while(L_accept(T_STAR)) pt=ty_ptr(pt);
        while(L_check(T_LBRACK)){ L_next(); if(L_check(T_NUM)) L_next(); L_expect(T_RBRACK,"]"); pt=ty_ptr(pt); }
        /* 名字 */
        pnames[np][0]=0;
        if (L_check(T_IDENT)) { int i=0;while(L.sbuf[i]&&i<63){pnames[np][i]=L.sbuf[i];i++;} pnames[np][i]=0; L_next(); }
        ptypes[np++]=pt;
        if (L_accept(T_COMMA)) continue; break;
    }
    L_expect(T_RPAREN,")");
    /* 必须是函数体 { */
    if (!L_check(T_LBRACE)) {
        /* 前向声明: 注册符号, 吃 ; */
        Type *ft = ty_func(ret, 0, np, func_is_vararg);
        if (np) { ft->params = g_malloc(np*sizeof(Type*)); for(int i=0;i<np;i++) ft->params[i]=ptypes[i]; }
        const char *usename = qn[0]?qn:name;
        Symbol *fn = sym_find_global(usename);
        if (!fn) { fn = sym_add(usename, SK_FUNC, ft); fn->addr=0; fn->defined=0; }
        L_expect(T_SEMI,";");
#undef L_ROLLBACK
        return 1;
    }
    /* 函数定义 → 开生成 [关键修复] 符号注册必须在 g_scope_depth=0 时进行, 否则函数符号 is_global=0 → sym_find_global 找不到 */
    long func_lab = new_label(); def_label(func_lab);
    const char *usename = qn[0]?qn:name;
    Type *ft = ty_func(ret, 0, np, func_is_vararg);
    if (np) { ft->params = g_malloc(np*sizeof(Type*)); for(int i=0;i<np;i++) ft->params[i]=ptypes[i]; }
    Symbol *fn = sym_find_global(usename);
    if (!fn) { fn = sym_add(usename, SK_FUNC, ft); dbg_com1('+'); }
    else dbg_com1('-');
    fn->addr = va_text_base + g_text_n;
    fn->defined = 1;
    dbg_com1('H');  /* function registered (global scope) */

    /* 注册完函数符号再进入局部作用域, 此后参数/局部变量被标记为 scope=1 */
    g_scope_depth++;
    /* 函数序: push rbp; mov rbp,rsp; 保存 callee-saved 寄存器 (rbx/r12-r15) */
    /* [诊断] 函数入口 COM1 追踪: 区分 "call main 失败" 与 "进入 main 后代码崩溃" */
    emit_com1_trace('A');     /* 运行时: 函数体第一条指令 — 若'c'后无此字符 → main_addr 错误 */
    emit_push(5);
    emit_mov_rr(5,4);
    /* 保存 rbx, r12-r15 到栈 */
    int saved_r[5] = {3,12,13,14,15};
    for(int i=0;i<5;i++) emit_push(saved_r[i]);
    g_local_offset = 0;
    /* 参数: 前 6 个在 rdi,rsi,rdx,rcx,r8,r9 → 压栈到本地变量 (逐个 sub rsp,8; mov [rsp], reg) → 加进符号表, offset = g_local_offset */
    int reg_map[6]={7,6,2,1,8,9};
    for(int i=0;i<np;i++) {
        g_local_offset += 8;
        emit_movi_imm32(0,8); emit_sub_rr(4,0); /* sub rsp,8 */
        if (i<6) emit_mov_mr(4, 0, reg_map[i]); /* mov [rsp], reg */
        else {
            /* 栈上参数: 位置 = rbp + 16 + (i-6)*8. 我们仍然复制到本地槽 (方便一致 offset 管理).
             * 原位置 [rbp + 16 + (i-6)*8] → rax; 然后 mov [rsp], rax. */
            int disp = 16 + (i-6)*8;
            emit_mov_rm(0, 5, disp);
            emit_mov_mr(4, 0, 0);
        }
        if (pnames[i][0]) {
            Symbol *v = sym_add(pnames[i], SK_VAR, ptypes[i]);
            v->is_global=0; v->scope=g_scope_depth; v->addr=g_local_offset;
        }
    }
    /* [新增] variadic 函数: 保存所有 6 个 GP 寄存器到 reg_save_area (48 字节连续区),
     * 用于 va_arg 读取. 记录 reg_save_area 和 overflow_arg_area 的 rbp-relative 偏移到全局. */
    if (ft->is_vararg) {
        /* 分配 48 字节 reg_save_area */
        g_local_offset += 48;
        if (g_local_offset % 8) g_local_offset += 8 - (g_local_offset % 8);
        int rsa_off = g_local_offset;  /* rbp-relative 偏移 (负数) */
        /* sub rsp, 48 */
        emit_movi_imm32(0, 48); emit_sub_rr(4, 0);
        /* 把 6 个 GP 寄存器存入 [rbp - rsa_off + k*8] */
        /* reg_map[k] = rdi/rsi/rdx/rcx/r8/r9 = 7/6/2/1/8/9 */
        for (int k = 0; k < 6; k++) {
            emit_mov_mr(5, reg_map[k], -rsa_off + k*8);  /* mov [rbp + (k*8 - rsa_off)], reg */
        }
        g_va_reg_save_off = -rsa_off;
        /* overflow_arg_area: 若 np > 6, 从 rbp + 16 + (np-6)*8 开始; 否则 rbp + 16 */
        if (np > 6) g_va_overflow_off = 16 + (np - 6) * 8;
        else        g_va_overflow_off = 16;
    }
    /* 若为方法定义 (qn[0] 非空): params[0] 已经是 this*, 变量名我们在 parse_spec_and_decl 阶段丢了; 用户若要用 this, 需要手动用. 简化: 自动添加符号 this = &__p0. */
    if (qn[0] && np>0 && cdtor!=2) {
        Symbol *th = sym_add("this", SK_VAR, ty_ptr(t_int)); /* placeholder; 类型稍后在 struct method 解析会严格化 */
        th->is_global=0; th->scope = g_scope_depth; th->addr = 8; /* __p0 */
    }
    /* 主体: parse block */
    L_next(); /* { */
    while (!L_check(T_RBRACE) && !L_check(T_EOF)) {
        int decl=0;
        if (L_check(T_INT)||L_check(T_CHAR)||L_check(T_VOID)||L_check(T_LONG)||L_check(T_SHORT)||
            L_check(T_STRUCT)||L_check(T_CLASS)||L_check(T_TYPEDEF)||L_check(T_ENUM)||L_check(T_UNION)||
            L_check(T_UNSIGNED)||L_check(T_SIGNED)||L_check(T_CONST)||L_check(T_STATIC)||
            L_check(T_EXTERN)||L_check(T_VOLATILE)||L_check(T_REGISTER)||
            L_check(T_FLOAT_KW)||L_check(T_DOUBLE_KW)||L_check(T_STATIC_ASSERT)) decl=1;
        else if (L_check(T_IDENT)) { Symbol *sy = sym_find(L.sbuf, g_scope_depth); if (sy && sy->kind==SK_TYPE) decl=1; }
        if (decl) {
            int a=0,b=0; parse_declaration(&a,&b, 0);
        } else parse_stmt(0);
    }
    L_next(); /* } */
    /* 函数尾: 恢复 rbx r12-r15; leave; ret. 若返回值非 void 但没有 return: rax=0 */
    if (ret->kind != TY_VOID) emit_movi_imm32(0,0);
    for(int i=4;i>=0;i--) emit_pop(saved_r[i]);
    emit_b(0xC9); /* leave */ emit_ret();
    sym_leave_scope(g_scope_depth);
    g_scope_depth--;
#undef L_ROLLBACK
    return 1;
}

/* 解析 struct body */
static void parse_struct_body(Type *st) {
    L_expect(T_LBRACE,"{");
    int off = 0;
    Field *flds = 0; int nf=0, nfc=0;
    /* 重置位字段 storage unit 跟踪, 避免上一个 struct 的残留状态影响当前 struct */
    s_bf_unit = -1; s_bf_pos = 0;
    while (!L_check(T_RBRACE) && !L_check(T_EOF)) {
        if (L_accept(T_PUBLIC)||L_accept(T_PRIVATE)) { L_expect(T_COLON,":"); continue; }
        /* [新增] 跳过存储类/限定符 */
        L_accept(T_CONST); L_accept(T_STATIC); L_accept(T_EXTERN);
        L_accept(T_VOLATILE); L_accept(T_REGISTER);
        Type *ft = 0;
        if (L_check(T_INT)) ft=t_int;
        else if (L_check(T_CHAR)) ft=t_char;
        else if (L_check(T_VOID)) ft=t_void;
        else if (L_check(T_LONG)) ft=t_long;
        else if (L_check(T_SHORT)) ft=t_int;
        /* [新增] unsigned/signed: 简化为 int (本编译器无独立 unsigned 类型) */
        else if (L_check(T_UNSIGNED)) { ft=t_int; L_next(); /* 消费 unsigned */
            /* unsigned int / unsigned char / unsigned long / unsigned short */
            if (L_check(T_INT)) { L_next(); }
            else if (L_check(T_CHAR)) { ft=t_char; L_next(); }
            else if (L_check(T_LONG)) { ft=t_long; L_next(); }
            else if (L_check(T_SHORT)) { L_next(); }
            goto skip_type_consumption;
        }
        else if (L_check(T_SIGNED)) { ft=t_int; L_next(); /* 消费 signed */
            if (L_check(T_INT)) { L_next(); }
            else if (L_check(T_CHAR)) { ft=t_char; L_next(); }
            else if (L_check(T_LONG)) { ft=t_long; L_next(); }
            else if (L_check(T_SHORT)) { L_next(); }
            goto skip_type_consumption;
        }
        else if (L_check(T_FLOAT_KW)) { ft=t_float; L_next(); goto skip_type_consumption; }
        else if (L_check(T_DOUBLE_KW)) { ft=t_double; L_next(); goto skip_type_consumption; }
        else if (L_check(T_ENUM)) { ft=t_int; L_next(); if (L_check(T_IDENT)) L_next(); goto skip_type_consumption; }
        else if (L_check(T_UNION)) { L_next(); ft=struct_find(L.sbuf); if (!ft){ft=ty_struct(L.sbuf); struct_register(L.sbuf, ft);} }
        else if (L_check(T_STRUCT)) { L_next(); ft=struct_find(L.sbuf); if (!ft){ft=ty_struct(L.sbuf); struct_register(L.sbuf, ft);} }
        else if (L_check(T_IDENT)) {
            Symbol *sy=sym_find(L.sbuf, g_scope_depth);
            if (sy && sy->kind==SK_TYPE) { ft=sy->type; }
            else {
                /* className(...) 构造函数声明: 没有返回类型 */
                /* 我们在 struct body 中发现 identifier 紧跟 (, 视为成员函数声明. 返回 int 默认. */
                ft = t_int;
            }
        }
        if (!ft) ft = t_int;
        /* [新增] enum 类型已消费, 跳过下面的类型 token 消费 */
        if (0) {
        skip_type_consumption:
            goto after_type_consumption;
        }
        if (L_check(T_INT)||L_check(T_CHAR)||L_check(T_VOID)||L_check(T_LONG)||L_check(T_STRUCT)||L_check(T_IDENT)) {
            if (L_check(T_INT)||L_check(T_CHAR)||L_check(T_VOID)||L_check(T_LONG)||L_check(T_STRUCT)) L_next();
            else L_next(); /* IDENT 两种: typedef → 类型 OR 方法名 */
        }
        after_type_consumption:
        while (L_accept(T_STAR)) ft = ty_ptr(ft);
        /* 名字 */
        char fname[128]={0}; int ctor_dtor=0;
        if (L_check(T_TILDE)) { L_next(); ctor_dtor=2; }
        if (L_check(T_IDENT)) { int i=0;while(L.sbuf[i]){fname[i]=L.sbuf[i];i++;} fname[i]=0; L_next(); }
        if (ctor_dtor==0 && g_strcmp(fname,st->sname)==0 && L_check(T_LPAREN)) ctor_dtor=1;
        /* 函数声明 */
        if (L_check(T_LPAREN)) {
            L_next(); Type *pts[16]={0}; int np=0;
            /* this + 其他参数 */
            pts[np++] = ty_ptr(st); /* this */
            if (!L_check(T_RPAREN)) for(;;) {
                Type *at = t_int;
                if (L_check(T_INT)) at=t_int;
                else if (L_check(T_CHAR)) at=t_char;
                else if (L_check(T_VOID)) at=t_void;
                else if (L_check(T_LONG)) at=t_long;
                else if (L_check(T_STRUCT)) { L_next(); at=struct_find(L.sbuf); if (!at){at=ty_struct(L.sbuf); struct_register(L.sbuf,at);} }
                else if (L_check(T_IDENT)) { Symbol *sy=sym_find(L.sbuf,g_scope_depth); if(sy&&sy->kind==SK_TYPE) at=sy->type; }
                if (L_check(T_INT)||L_check(T_CHAR)||L_check(T_VOID)||L_check(T_LONG)||L_check(T_STRUCT)||L_check(T_IDENT)) L_next();
                while(L_accept(T_STAR)) at=ty_ptr(at);
                if (L_check(T_IDENT)) L_next();
                pts[np++]=at;
                if (L_accept(T_COMMA)) continue; break;
            }
            L_expect(T_RPAREN,")");
            L_expect(T_SEMI,";");
            /* 全局注册方法 */
            Type *mtype = ty_func(ft, pts, np, 0);
            char full[256]; int k=0; int i=0; while(st->sname[i]) full[k++]=st->sname[i++]; full[k++]=':'; full[k++]=':'; i=0;
            if (ctor_dtor==2 && fname[0]) { /* ~ClassName */ full[k++]='~'; i=0;}
            while(fname[i]) full[k++]=fname[i++]; full[k]=0;
            Symbol *ms = sym_add(full, SK_FUNC, mtype);
            ms->defined = 0;
            /* 追加 field */
            if (nf>=nfc){
                int old_nf = nf;
                int old_nfc = nfc;
                nfc = old_nfc ? old_nfc*2 : 8;
                flds = g_realloc(flds, old_nf*sizeof(Field), nfc*sizeof(Field));
            }
            int i2=0;while(fname[i2]&&i2<63){flds[nf].name[i2]=fname[i2];i2++;} flds[nf].name[i2]=0;
            flds[nf].type = mtype; flds[nf].offset = 0; flds[nf].is_method = 1;
            flds[nf].is_ctor = (ctor_dtor==1); flds[nf].is_dtor = (ctor_dtor==2);
            nf++;
            continue;
        }
        /* 成员变量 */
        while (L_accept(T_LBRACK)) {
            int n=0; if (L_check(T_NUM)) n=L.num; L_next(); L_expect(T_RBRACK,"]");
            ft = ty_arr(ft, n);
        }
        /* [新增] 位字段: name : width. 简化: 用 4 字节 (int) storage unit 做位分配.
         * 连续位字段打包进同一 storage unit; 普通 (非位字段) 字段从下一字节开始. */
        int bw = 0;
        if (L_check(T_COLON)) {
            L_next();
            if (L_check(T_NUM)) { bw = L.num; L_next(); }
            else g_panic("bit-field width expected");
            if (bw < 1) bw = 1;
            if (bw > 32) bw = 32;
        }
        int al = ft->align>0?ft->align:8;
        /* #pragma pack(N): 字段对齐上限 = min(原生, pack) */
        if (g_pp_pack > 0 && al > g_pp_pack) al = g_pp_pack;
        if (al < 1) al = 1;
        /* 位字段用 4 字节 int 作 storage unit; 单元内连续分配.
         * s_bf_pos / s_bf_unit 为文件作用域 static, 非位字段路径可重置. */
        if (bw > 0) {
            int unit_bits = 32; /* int storage unit */
            /* 需新 unit? (首次, 或非位字段后重置) */
            if (s_bf_unit < 0) { s_bf_unit = off; s_bf_pos = 0; }
            /* 当前 storage unit 装不下? 开新 unit */
            if (s_bf_pos + bw > unit_bits) {
                /* 关闭旧 unit: off 推进到 unit 末尾 */
                off = s_bf_unit + 4; /* int = 4B */
                s_bf_unit = off;
                s_bf_pos = 0;
            }
            if (nf>=nfc){
                int old_nf = nf; int old_nfc = nfc;
                nfc = old_nfc ? old_nfc*2 : 8;
                flds = g_realloc(flds, old_nf*sizeof(Field), nfc*sizeof(Field));
            }
            int i2=0;while(fname[i2]&&i2<63){flds[nf].name[i2]=fname[i2];i2++;} flds[nf].name[i2]=0;
            flds[nf].type = t_int; flds[nf].offset = s_bf_unit; flds[nf].is_method = 0;
            flds[nf].is_ctor = 0; flds[nf].is_dtor = 0;
            flds[nf].bit_width = bw; flds[nf].bit_offset = s_bf_pos;
            s_bf_pos += bw;
            /* 更新 struct size: 至少覆盖到当前 unit 末尾 */
            int unit_end = s_bf_unit + 4;
            if (unit_end > off) off = unit_end;
            nf++;
            L_expect(T_SEMI,";");
            continue;
        }
        /* 非位字段: 重置 bit 状态 (下一个位字段从新 unit 开始) */
        s_bf_unit = -1;
        if (off % al) off += al - (off%al);
        if (nf>=nfc){
            int old_nf = nf;
            int old_nfc = nfc;
            nfc = old_nfc ? old_nfc*2 : 8;
            flds = g_realloc(flds, old_nf*sizeof(Field), nfc*sizeof(Field));
        }
        int i2=0;while(fname[i2]&&i2<63){flds[nf].name[i2]=fname[i2];i2++;} flds[nf].name[i2]=0;
        flds[nf].type = ft; flds[nf].offset = off; flds[nf].is_method = 0;
        flds[nf].is_ctor = 0; flds[nf].is_dtor = 0;
        flds[nf].bit_width = 0; flds[nf].bit_offset = 0;
        off += ft->size;
        nf++;
        L_expect(T_SEMI,";");
    }
    L_expect(T_RBRACE,"}");
    /* 对齐 */
    int sz = off; if (sz % 8) sz += 8 - (sz%8);
    st->size = sz; st->align = 8; st->nfields = nf; st->fields = flds;
}

/* 内建运行时 thunk: 让编译的程序可以直接调用 printf / puts / putchar / malloc / free /
 * file_read / file_write / file_exists / clear_screen / getchar / readline / strlen / strcpy 等
 * 实现方式: 在 .text 开头排放一组 stub, 它们把参数从 System V 寄存器搬到 API, 再跳转.
 * stub 名注册为全局符号, 这样用户在 C 里调用 "printf(...)" 就会跳到对应 stub. */

typedef struct { const char *name; int api_idx; int call_kind; /* 0=void/ptr; 1=int ret; 2=string -> 调 print 或 put_char 再格式化 */ } Builtin;
/* exit stub 的帧槽 VA (generate_builtin_stubs 填, build_binary 的 prolog 引用) */
static long g_exit_frame_slot IN_DATA = 0;
static Builtin g_builtins[] = {
    /* 索引必须严格与 struct kernel_api 中 8 字节槽对齐:
     * slot 0 = magic+_pad (8B), slot 1 = put_char, slot 2 = print, ...
     * [关键修复] 之前所有索引 +1 (误以为 slot 0 = put_char), 导致调用错误函数 */
    {"malloc",       24, 0}, /* void* malloc(size_t)          = slot 24 */
    {"free",         25, 1}, /* void free(void*)              = slot 25 */
    {"putchar",      1,  1}, /* int putchar(int)              = slot 1  */
    {"print",        2,  1}, /* void print(s)                 = slot 2  */
    {"puts",         2,  1}, /* puts = print + \n             = slot 2  */
    {"clear_screen", 4,  1}, /* void clear_screen()           = slot 4  */
    {"file_read",    5,  1}, /* int file_read(p,buf,n)        = slot 5  */
    {"file_write",   6,  1}, /* int file_write(p,d,n)         = slot 6  */
    {"file_exists",  7,  1}, /* int file_exists(p)            = slot 7  */
    {"mkdir",        8,  1}, /* int mkdir(p)                  = slot 8  */
    {"readline",     9,  1}, /* int readline(buf,n)           = slot 9  */
};
static int g_builtin_n = sizeof(g_builtins)/sizeof(g_builtins[0]);

/* 为 N 个内置函数生成 stub:
 *   每个 stub:  mov r11, [0x9000 + idx*8]  ; jmp r11
 *   puts 特殊: 先 print(s), 再 putchar('\n')
 * 这些 stub 的地址会被记录为符号.
 * [关键修复] 每个符号类型必须是 TY_FUNC, 否则函数调用时 panic "call non-func".
 * [细粒度诊断] 在 emit 每个关键步骤前后都输出单独串口字符, 精确锁定卡在哪一步. */
/* [运行时 trace] emit 7 字节到 g_text: mov dx,0x3F8; mov al,c; out dx,al.
 * 编译产物的关键路径直接写 COM1 (QEMU -serial 可见), 用于定位运行时崩溃点.
 * 编码: 66 BA F8 03 | B0 <c> | EE */
static void emit_com1_trace(char c) {
    /* [COM1 trace fix] 记录写入位置, relocation pass 会跳过这些字节. */
    if (g_trace_n < 256) g_trace_pos[g_trace_n++] = g_text_n;
    emit_b(0x66); emit_b(0xBA); emit_b(0xF8); emit_b(0x03);  /* mov dx,0x3F8 */
    emit_b(0xB0); emit_b(c);                                   /* mov al,c    */
    emit_b(0xEE);                                              /* out dx,al   */
}
static void generate_builtin_stubs(void) {
    dbg_com1('a');  /* generate_builtin_stubs entered */
    for(int i=0;i<g_builtin_n;i++) {
        /* ===== 循环序号: 用 [0..9 A] 标记, 避免与其它标记混淆 ===== */
        dbg_com1('0' + (i<=9?i:(i-10)+'A'-'0'));
        Builtin *b = &g_builtins[i];
        /* 根据函数名造一个合适的 TY_FUNC 类型 */
        Type *ret = t_int;
        int nparams = 1;
        if (g_strcmp(b->name,"malloc")==0)      { ret = ty_ptr(t_void); nparams = 1; }
        else if (g_strcmp(b->name,"free")==0)   { ret = t_void;        nparams = 1; }
        else if (g_strcmp(b->name,"putchar")==0){ ret = t_int;         nparams = 1; }
        else if (g_strcmp(b->name,"print")==0)  { ret = t_void;        nparams = 1; }
        else if (g_strcmp(b->name,"puts")==0)   { ret = t_int;         nparams = 1; }
        else if (g_strcmp(b->name,"clear_screen")==0){ ret = t_void;   nparams = 0; }
        else if (g_strcmp(b->name,"file_read")==0)  { ret = t_int;     nparams = 3; }
        else if (g_strcmp(b->name,"file_write")==0) { ret = t_int;     nparams = 3; }
        else if (g_strcmp(b->name,"file_exists")==0){ ret = t_int;     nparams = 1; }
        else if (g_strcmp(b->name,"mkdir")==0)      { ret = t_int;     nparams = 1; }
        else if (g_strcmp(b->name,"readline")==0)   { ret = t_int;     nparams = 2; }
        Type *params[4] = {0};
        for(int k=0;k<nparams;k++) params[k] = ty_ptr(t_char);
        Type *ft = ty_func(ret, nparams?params:0, nparams, 0);
        Symbol *s = sym_add(b->name, SK_FUNC, ft);
        s->defined = 1; s->addr = va_text_base + g_text_n;
        dbg_com1('b');  /* builtin loop: sym_add done, about to emit */

        /* ===== [诊断] 本次循环 emit 阶段开始标记: C~M (i=0~10) ===== */
        dbg_com1('C' + i);

        if (g_strcmp(b->name, "puts")==0) {
            /* puts(s) -> print(s); putchar('\n'); return 0; */
            dbg_com1('W');  /* puts 分支开始 */
            emit_push(7);                            dbg_com1('1');
            EmitMoviImm64(0, 0x9000 + 2*8);       dbg_com1('2');  /* slot 2 = print */
            emit_mov_rm(0,0,0);                     dbg_com1('3');
            emit_callr(0);                          dbg_com1('4'); /* print 调用 */
            emit_pop(7);                             dbg_com1('5');
            emit_movi_imm32(7, '\n');               dbg_com1('6');
            EmitMoviImm64(0, 0x9000 + 1*8);       dbg_com1('7');  /* slot 1 = put_char */
            emit_mov_rm(0,0,0);                     dbg_com1('8');
            emit_callr(0);                          dbg_com1('9'); /* putchar 调用 */
            emit_movi_imm32(0, 0);                  dbg_com1(':');
            emit_ret();                              dbg_com1(';');
            dbg_com1('U');  /* puts emit 完成 */
            continue;
        }
        if (g_strcmp(b->name, "putchar")==0) {
            /* rdi = char; 调 put_char, 返回 eax = char */
            dbg_com1('Y');  /* putchar 分支开始 */
            emit_push(7);                            dbg_com1('<');
            EmitMoviImm64(0, 0x9000 + 1*8);       dbg_com1('=');  /* slot 1 = put_char */
            emit_mov_rm(0,0,0);                     dbg_com1('>');
            emit_callr(0);                          dbg_com1('?');
            emit_pop(0);                             dbg_com1('@');  /* rax = char (ret) */
            emit_ret();                              dbg_com1('A');
            dbg_com1('Z');  /* putchar emit 完成 */
            continue;
        }
        /* 默认分支: mov rax, [API->idx] ; jmp *rax 尾调用 */
        dbg_com1('p');  /* default: movi_imm64 之前 */
        emit_com1_trace('p');   /* 运行时: 进入 builtin stub (print/file_read/...) */
        EmitMoviImm64(0, 0x9000L + (long)b->api_idx*8);
        dbg_com1('q');  /* movi_imm64 之后, mov_rm 之前 */
        emit_mov_rm(0,0,0);
        dbg_com1('r');  /* mov_rm 之后, jmp 之前 */
        emit_com1_trace('Q');   /* 运行时: 已加载 API 函数指针到 rax, 马上 jmp*rax */
        /* jmp *rax = FF E0 */
        emit_b(0xFF); emit_b(0xE0);
        dbg_com1('s');  /* 默认分支 emit 全部完成 */
    }
    dbg_com1('X');  /* 11 次循环全部结束, 即将生成 exit 符号 */
    /* 另外加一个 "exit" 符号: void exit(int).
     * [关键修复] 之前用 int 0x3 触发陷阱 — 内核把它当 EFS 异常终止 (cr2=0 日志).
     * 正确机制: 内核 efs_task_entry 用 `call *r12` 进入入口, 程序 ret 即正常退出.
     * 深层调用栈无法直接拿到入口帧 → 用 .text 内固定槽保存 prolog 的 rbp:
     *   exit: movabs rcx, slot_va ; mov rsp,[rcx] ; pop rbp ; ret
     *   slot: 8 字节, prolog 启动时写入自己的 rbp (栈帧=入口时 rsp).
     * EFS 整块加载内存可写, 槽放 text 尾部安全. */
    dbg_com1('O');  /* exit stub: sym_add 前 */
    Type *exit_params[1] = {t_int};
    Type *exit_ft = ty_func(t_void, exit_params, 1, 0);
    Symbol *sx = sym_add("exit", SK_FUNC, exit_ft);
    sx->defined=1; sx->addr = va_text_base + g_text_n;
    dbg_com1('P');  /* exit stub: sym_add done, 即将 emit */
    {
        /* 代码长度: trace(7) + movabs(10) + mov rsp,[rcx](3) + pop rbp(1) + ret(1) = 22 */
        long code_len = 7 + 10 + 3 + 1 + 1;
        g_exit_frame_slot = va_text_base + g_text_n + code_len;
        emit_com1_trace('E');   /* 运行时: 进入 exit (帧恢复前) */
        /* movabs rcx, slot_va = 48 B9 imm64
         * [修复 #9] 手动 emit 的 movabs 必须登记到重定位表, 否则字节扫描会漏 (精确表也会漏!)
         * 注意: 这里 imm64 = slot_va = va_text_base + offset, 已经是最终 VA (不需要再次重定位).
         *   但登记它能让诊断输出更完整; 重定位判断时若 imm 不在 [0, ro/data/bss_len) 范围内就会跳过.
         * [诊断 tag] 'E' = exit stub 手动登记 */
        RelocRecordTag(g_text_n, 'E');
        /* emit_b(0x48); emit_b(0xB9); + 8 字节 imm */
        emit_b(0x48); emit_b(0xB9);
        for(int i=0;i<8;i++) emit_b((char)((g_exit_frame_slot>>(i*8))&0xFF));
        /* mov rsp, [rcx] = 48 8B 21 */
        emit_b(0x48); emit_b(0x8B); emit_b(0x21);
        /* pop rbp = 5D ; ret = C3 */
        emit_b(0x5D); emit_b(0xC3);
        /* 8 字节槽 (初始 0; prolog 运行时写入) */
        for(int i=0;i<8;i++) emit_b(0);
    }
    dbg_com1('Y');  /* generate_builtin_stubs 完全结束 */
}

/* 简单 printf 支持: 简化实现, 单参数 (fmt) 直接调 print, 返回 0.
 * [修复] 类型必须是 TY_FUNC. */
static void generate_printf_stub(void) {
    dbg_com1('m');  /* generate_printf_stub 入口 */
    /* printf(fmt, ...) → 简化: ret=int, nparams >=1 (char*), is_vararg=1.
     * 内部实现: 先调 print(fmt), 然后 rax=0 返回 (单参数时等价于 print).
     * 用户多参数格式化建议用 putchar/print 拼接, 或后续扩展完整 printf. */
    Type *params[1] = {ty_ptr(t_char)};
    Type *ft = ty_func(t_int, params, 1, 1); /* 1=可变参数标志, 允许传任意多个额外参数 */
    Symbol *s = sym_add("printf", SK_FUNC, ft);
    s->defined=1; s->addr = va_text_base + g_text_n;
    /* rdi = fmt. 调 print(rdi). 然后 mov rax, 0; ret. */
    /* printf stub 的 API slot 加载: 用 tag 'P' */
    RelocRecordTag(g_text_n, 'P');
    { int reg=0; long imm=0x9000 + 2*8;
      emit_rex(1,0,0,reg);
      emit_b(0xB8 + (reg&7));
      emit_q(imm); }
    emit_mov_rm(0,0,0);
    emit_callr(0);
    emit_movi_imm32(0, 0);
    emit_ret();
}

/* strlen / strcpy / strcmp 小工具 (纯寄存器实现, 不依赖 libc)
 * [修复] 类型必须是 TY_FUNC. */
static void generate_libc_util_stubs(void) {
    dbg_com1('n');  /* generate_libc_util_stubs 入口 */
    /* size_t strlen(const char *s): rdi=s; rax=0; while(s[rax]) rax++; ret rax */
    Type *strlen_params[1] = {ty_ptr(t_char)};
    Type *ft = ty_func(t_long, strlen_params, 1, 0);
    Symbol *s = sym_add("strlen", SK_FUNC, ft);
    s->defined=1;
    s->addr = va_text_base + g_text_n;
    emit_movi_imm32(0,0);            /* xor rax,rax */
    emit_xor_rr(0,0);
    { long loop=new_label(), done=new_label();
        def_label(loop);
        emit_movsx_byte(1,7,0);       /* rcx = *(rdi+0) sign-extend to 64 */
        emit_test_rr(1,1);
        emit_jcc(4,done);
        emit_inc_r(0);
        emit_inc_r(7);
        emit_jmp(loop);
        def_label(done);
        emit_ret();
    }
    /* char *strcpy(char *d, const char *s): rdi=d, rsi=s. while(*d++=*s++); */
    Type *scp_params[2] = {ty_ptr(t_char), ty_ptr(t_char)};
    ft = ty_func(ty_ptr(t_char), scp_params, 2, 0);
    s = sym_add("strcpy", SK_FUNC, ft); s->defined=1;
    s->addr = va_text_base + g_text_n;
    emit_mov_rr(10,7); /* r10 = d (保存返回值) */
    { long loop=new_label(), done=new_label();
        def_label(loop);
        emit_movzx_byte(0,6,0); /* rax = byte [rsi] */
        /* mov [rdi], al */
        emit_rex(0,0,0,7); emit_b(0x88); emit_modrm(3,0,7);
        emit_test_rr(0,0);
        emit_jcc(4,done);
        emit_inc_r(7); emit_inc_r(6);
        emit_jmp(loop);
        def_label(done);
        emit_mov_rr(0,10);
        emit_ret();
    }
    /* int strcmp(const char *a, const char *b) */
    Type *scmp_params[2] = {ty_ptr(t_char), ty_ptr(t_char)};
    ft = ty_func(t_int, scmp_params, 2, 0);
    s = sym_add("strcmp", SK_FUNC, ft); s->defined=1;
    s->addr = va_text_base + g_text_n;
    { long loop=new_label(), diff=new_label(), done=new_label();
        def_label(loop);
        emit_movzx_byte(0,7,0); /* rax = a[0] */
        emit_movzx_byte(1,6,0); /* rcx = b[0] */
        emit_cmp_rr(0,1);
        emit_jcc(5, diff);  /* ne */
        emit_test_rr(0,0);
        emit_jcc(4, done);  /* end of a */
        emit_inc_r(7); emit_inc_r(6);
        emit_jmp(loop);
        def_label(diff);
        emit_sub_rr(0,1);  /* rax = a-b */
        def_label(done);
        emit_ret();
    }
}

/* 回填 rel32 patches */
static void patch_all(void) {
    for(int i=0;i<g_patches_n;i++) {
        Patch *p = &g_patches[i];
        long tva = label_va(p->label_id);
        if (tva<0) { g_print("undefined label "); g_print_int((int)p->label_id); g_print("\n"); g_panic("label"); }
        long rel = tva - (va_text_base + p->patch_pos + 4);
        if (rel < -2147483647L || rel > 2147483647L) g_panic("rel32 overflow");
        int v = (int)rel;
        g_text[p->patch_pos+0] = (char)(v&0xFF);
        g_text[p->patch_pos+1] = (char)((v>>8)&0xFF);
        g_text[p->patch_pos+2] = (char)((v>>16)&0xFF);
        g_text[p->patch_pos+3] = (char)((v>>24)&0xFF);
    }
}

/* ========== [新增] 预处理器: #define / #include / #ifdef / #endif ==========
 * 在 L_init 之前对源文本进行预处理, 输出到一个新缓冲区.
 * 支持:
 *   - #define NAME value           (对象式宏)
 *   - #define NAME(params) body    (函数式宏)
 *   - #undef NAME
 *   - #include "file.h"            (读取文件内容内联展开)
 *   - #ifdef / #ifndef / #else / #endif
 *   - 宏引用展开 (词法级文本替换)
 * 简化:
 *   - #if / #elif 仅支持 #ifdef 语义 (不求值常量表达式)
 *   - 行续接 '\' 已处理
 *   - 不支持可变参数宏 (__VA_ARGS__)
 */

#define MAX_MACROS 128
#define MAX_INCLUDE_DEPTH 8
#define PP_MAX_OUT (1024*1024)  /* 1MB 预处理输出上限 */
#define MAX_ONCE_PATHS 32      /* #pragma once 已记录的文件路径上限 */

/* #pragma once: 维护已 once 的文件路径列表, #include 时检查 */
static char g_pp_once_paths[MAX_ONCE_PATHS][256] IN_DATA;
static int g_pp_once_n IN_DATA = 0;
/* pending 标志: #pragma once 触发后置 1, pp_handle_include 调用后检查并清零 */
static int g_pp_once_pending IN_DATA = 0;

struct pp_macro {
    char name[64];
    char params[8][32];  /* 最多 8 个参数 */
    int n_params;        /* -1 = 对象式宏 (无参数) */
    int is_vararg;       /* 1 = 末尾有 ... 可变参数 (__VA_ARGS__) */
    char body[512];
    int body_len;
};
static struct pp_macro g_macros[MAX_MACROS] IN_DATA;
static int g_macros_n IN_DATA = 0;

/* 查找宏, 返回索引或 -1 */
static int pp_find_macro(const char *name) {
    for (int i = 0; i < g_macros_n; i++)
        if (g_strcmp(g_macros[i].name, name) == 0) return i;
    return -1;
}

/* 添加或替换宏 */
static void pp_add_macro(const char *name, int n_params, int is_vararg,
                         const char *body, int body_len) {
    int idx = pp_find_macro(name);
    if (idx < 0) {
        if (g_macros_n >= MAX_MACROS) return;
        idx = g_macros_n++;
        int i = 0; while (name[i] && i < 63) { g_macros[idx].name[i] = name[i]; i++; }
        g_macros[idx].name[i] = 0;
    }
    g_macros[idx].n_params = n_params;
    g_macros[idx].is_vararg = is_vararg;
    int bl = body_len < 511 ? body_len : 511;
    g_memcpy(g_macros[idx].body, body, bl);
    g_macros[idx].body[bl] = 0;
    g_macros[idx].body_len = bl;
}

/* 判断字符是否可作为标识符的一部分 */
static int pp_is_ident_char(int c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
}

/* 跳过空白 (不含换行) */
static int pp_skip_ws(const char *s, int p) {
    while (s[p]==' '||s[p]=='\t') p++;
    return p;
}

/* 读取一个标识符到 buf, 返回结束位置 */
static int pp_read_ident(const char *s, int p, char *buf, int bufsz) {
    int i = 0;
    while (pp_is_ident_char(s[p]) && i < bufsz-1) buf[i++] = s[p++];
    buf[i] = 0;
    return p;
}

/* 函数式宏展开: 替换参数并输出到 out
 * 支持:
 *   #param       — 字符串化参数 (含转义)
 *   a ## b       — 标记拼接 (相邻 token 拼成新 token)
 *   __VA_ARGS__  — 可变参数宏的剩余实参 (含逗号) */
static int pp_expand_func_macro(struct pp_macro *m, const char *call_args, int call_len,
                                char *out, int out_sz) {
    /* 解析调用参数 (逗号分隔, 顶层括号外的逗号才分) */
    const char *args[8]; int arg_lens[8]; int n_args = 0;
    int p = 0;
    int paren_depth = 0;
    int arg_start = 0;
    while (p < call_len) {
        if (call_args[p] == '(') paren_depth++;
        else if (call_args[p] == ')') { paren_depth--; if (paren_depth < 0) break; }
        else if (call_args[p] == ',' && paren_depth == 0) {
            if (n_args < 8) { args[n_args] = call_args + arg_start; arg_lens[n_args] = p - arg_start; n_args++; }
            arg_start = p + 1;
        }
        p++;
    }
    /* 最后一个参数 (可变宏情况下, 它会吸收剩余所有逗号) */
    if (p > arg_start || n_args > 0) {
        if (n_args < 8) { args[n_args] = call_args + arg_start; arg_lens[n_args] = p - arg_start; n_args++; }
    }
    /* 对可变宏, 把第 (n_params) 个及之后的所有实参合并为 __VA_ARGS__ (含逗号) */
    char va_buf[1024]; int va_len = 0;
    if (m->is_vararg) {
        int np = m->n_params;
        va_buf[0] = 0;
        for (int k = np; k < n_args; k++) {
            if (k > np && va_len < (int)sizeof(va_buf) - 2) { va_buf[va_len++] = ','; va_buf[va_len] = 0; }
            int al = arg_lens[k]; int trim = 0;
            /* 去首尾空白 */
            while (trim < al && (args[k][trim]==' '||args[k][trim]=='\t')) trim++;
            int tail = al;
            while (tail > trim && (args[k][tail-1]==' '||args[k][tail-1]=='\t')) tail--;
            for (int q = trim; q < tail; q++) {
                if (va_len < (int)sizeof(va_buf) - 1) va_buf[va_len++] = args[k][q];
            }
            va_buf[va_len] = 0;
        }
    }

    /* 辅助: 查找参数索引 (含 __VA_ARGS__) */
    /* 展开 body: 扫描 token, 处理 # 与 ## */
    int out_pos = 0;
    /* we need to handle ## by buffering tokens. 简化策略: 先做参数替换 + 字符串化,
     * 把结果放进一个中间缓冲, 再做 ## 拼接 (因为 ## 在参数替换之后生效) */
    char inter[4096];
    int ip = 0;
    for (int i = 0; i < m->body_len && ip < (int)sizeof(inter) - 1; ) {
        char ch = m->body[i];
        /* 跳过注释 */
        if (ch == '/' && i+1 < m->body_len && m->body[i+1] == '*') {
            i += 2;
            while (i+1 < m->body_len && !(m->body[i]=='*' && m->body[i+1]=='/')) i++;
            if (i+1 < m->body_len) i += 2;
            continue;
        }
        /* 字符串字面量: 整段复制 (避免内部 #/## 被处理) */
        if (ch == '"') {
            inter[ip++] = ch; i++;
            while (i < m->body_len && m->body[i] != '"' && ip < (int)sizeof(inter) - 1) {
                if (m->body[i] == '\\' && i+1 < m->body_len) {
                    inter[ip++] = m->body[i++];
                    if (ip < (int)sizeof(inter) - 1 && i < m->body_len) inter[ip++] = m->body[i++];
                } else {
                    inter[ip++] = m->body[i++];
                }
            }
            if (i < m->body_len && ip < (int)sizeof(inter) - 1) inter[ip++] = m->body[i++];
            continue;
        }
        /* 字符常量 */
        if (ch == '\'') {
            inter[ip++] = ch; i++;
            while (i < m->body_len && m->body[i] != '\'' && ip < (int)sizeof(inter) - 1) {
                if (m->body[i] == '\\' && i+1 < m->body_len) {
                    inter[ip++] = m->body[i++];
                    if (ip < (int)sizeof(inter) - 1 && i < m->body_len) inter[ip++] = m->body[i++];
                } else {
                    inter[ip++] = m->body[i++];
                }
            }
            if (i < m->body_len && ip < (int)sizeof(inter) - 1) inter[ip++] = m->body[i++];
            continue;
        }
        /* 字符串化: # ident */
        if (ch == '#' && (i == 0 || m->body[i-1] != '#') &&
            (i+1 >= m->body_len || m->body[i+1] != '#')) {
            /* 找下一个标识符 (跳过空白) */
            int j = i + 1;
            while (j < m->body_len && (m->body[j]==' '||m->body[j]=='\t')) j++;
            if (j < m->body_len && pp_is_ident_char(m->body[j])) {
                char nm[64]; int ni = 0;
                while (j < m->body_len && pp_is_ident_char(m->body[j]) && ni < 63) nm[ni++] = m->body[j++];
                nm[ni] = 0;
                /* 查找参数 */
                int found = -1;
                if (m->is_vararg && g_strcmp(nm, "__VA_ARGS__") == 0) {
                    /* 字符串化 VA_ARGS */
                    inter[ip++] = '"';
                    for (int q = 0; q < va_len && ip < (int)sizeof(inter) - 2; q++) {
                        char c = va_buf[q];
                        if (c == '"' || c == '\\') inter[ip++] = '\\';
                        inter[ip++] = c;
                    }
                    inter[ip++] = '"';
                } else {
                    for (int k = 0; k < m->n_params; k++) {
                        if (g_strcmp(nm, m->params[k]) == 0) { found = k; break; }
                    }
                    if (found >= 0 && found < n_args) {
                        inter[ip++] = '"';
                        for (int q = 0; q < arg_lens[found] && ip < (int)sizeof(inter) - 2; q++) {
                            char c = args[found][q];
                            if (c == '"' || c == '\\') inter[ip++] = '\\';
                            inter[ip++] = c;
                        }
                        inter[ip++] = '"';
                    } else {
                        inter[ip++] = '"';
                        inter[ip++] = '"';
                    }
                }
                i = j;
                continue;
            }
        }
        /* 标识符: 检查是否是参数 */
        if (pp_is_ident_char(ch)) {
            char ident[64]; int j = 0;
            int start = i;
            while (i < m->body_len && pp_is_ident_char(m->body[i]) && j < 63)
                ident[j++] = m->body[i++];
            ident[j] = 0;
            /* __VA_ARGS__ */
            if (m->is_vararg && g_strcmp(ident, "__VA_ARGS__") == 0) {
                for (int q = 0; q < va_len && ip < (int)sizeof(inter) - 1; q++)
                    inter[ip++] = va_buf[q];
                continue;
            }
            /* 命名参数 */
            int found = -1;
            for (int k = 0; k < m->n_params; k++) {
                if (g_strcmp(ident, m->params[k]) == 0) { found = k; break; }
            }
            if (found >= 0 && found < n_args) {
                int al = arg_lens[found];
                /* 去首尾空白 */
                int trim = 0;
                while (trim < al && (args[found][trim]==' '||args[found][trim]=='\t')) trim++;
                int tail = al;
                while (tail > trim && (args[found][tail-1]==' '||args[found][tail-1]=='\t')) tail--;
                for (int q = trim; q < tail && ip < (int)sizeof(inter) - 1; q++)
                    inter[ip++] = args[found][q];
            } else {
                /* 不是参数, 原样输出 */
                int len = i - start;
                for (int q = 0; q < len && ip < (int)sizeof(inter) - 1; q++)
                    inter[ip++] = m->body[start + q];
            }
            continue;
        }
        /* 普通字符 */
        inter[ip++] = ch;
        i++;
    }
    inter[ip] = 0;

    /* 第二遍: 处理 ## 拼接
     * 扫描 inter, 遇到 ## 时把左侧和右侧的非空白 token 直接拼接 (不留空白) */
    int sp = 0;
    int dp = 0;
    while (sp < ip && dp < out_sz - 1) {
        /* 检测 ## */
        if (sp + 1 < ip && inter[sp] == '#' && inter[sp+1] == '#') {
            /* 删掉左侧尾空白 */
            while (dp > 0 && (out[dp-1] == ' ' || out[dp-1] == '\t')) dp--;
            /* 跳过 ## 自身和右侧前导空白 */
            sp += 2;
            while (sp < ip && (inter[sp] == ' ' || inter[sp] == '\t')) sp++;
            /* 不输出任何分隔符, 直接继续 (下一个字符会被复制) */
            continue;
        }
        out[dp++] = inter[sp++];
    }
    out[dp] = 0;
    return dp;
}

/* 展开 source 中的宏引用, 输出到 out.
 * 处理嵌套: 已展开的文本不再展开 (防止递归) */
static int pp_expand_text(const char *src, int src_len, char *out, int out_sz) {
    int out_pos = 0;
    int i = 0;
    while (i < src_len && out_pos < out_sz - 1) {
        /* 跳过字符串和字符常量 (不展开其中的宏) */
        if (src[i] == '"' || src[i] == '\'') {
            char q = src[i];
            out[out_pos++] = src[i++];
            while (i < src_len && src[i] != q) {
                if (src[i] == '\\' && i+1 < src_len) {
                    out[out_pos++] = src[i++];
                    if (out_pos < out_sz - 1) out[out_pos++] = src[i++];
                } else {
                    out[out_pos++] = src[i++];
                }
            }
            if (i < src_len) out[out_pos++] = src[i++];
            continue;
        }
        /* 注释 */
        if (src[i] == '/' && i+1 < src_len && src[i+1] == '/') {
            while (i < src_len && src[i] != '\n')
                if (out_pos < out_sz - 1) out[out_pos++] = src[i++];
            continue;
        }
        if (src[i] == '/' && i+1 < src_len && src[i+1] == '*') {
            out[out_pos++] = src[i++]; out[out_pos++] = src[i++];
            while (i+1 < src_len && !(src[i] == '*' && src[i+1] == '/'))
                if (out_pos < out_sz - 1) out[out_pos++] = src[i++];
            if (i+1 < src_len) { out[out_pos++] = src[i++]; out[out_pos++] = src[i++]; }
            continue;
        }
        /* 标识符: 检查是否是宏 */
        if (pp_is_ident_char(src[i]) && (i == 0 || !pp_is_ident_char(src[i-1]))) {
            char ident[128];
            int start = i;
            i = pp_read_ident(src, i, ident, 128);
            int midx = pp_find_macro(ident);
            if (midx >= 0) {
                struct pp_macro *m = &g_macros[midx];
                if (m->n_params < 0) {
                    /* 对象式宏: 替换为 body */
                    int bl = m->body_len;
                    if (out_pos + bl >= out_sz - 1) bl = out_sz - 1 - out_pos;
                    g_memcpy(out + out_pos, m->body, bl);
                    out_pos += bl;
                } else {
                    /* 函数式宏: 读取 (args) */
                    int j = pp_skip_ws(src, i);
                    if (j < src_len && src[j] == '(') {
                        /* 找到匹配的 ) */
                        int depth = 1; j++;
                        int arg_start = j;
                        while (j < src_len && depth > 0) {
                            if (src[j] == '(') depth++;
                            else if (src[j] == ')') depth--;
                            if (depth > 0) j++;
                        }
                        if (j < src_len) j++;  /* 跳过 ) */
                        /* 展开宏 */
                        char expanded[4096];
                        int elen = pp_expand_func_macro(m, src + arg_start,
                                                         j - 1 - arg_start, expanded, 4096);
                        if (out_pos + elen < out_sz - 1) {
                            g_memcpy(out + out_pos, expanded, elen);
                            out_pos += elen;
                        }
                        i = j;
                    } else {
                        /* 没有括号: 不展开, 原样输出 */
                        int len = i - start;
                        if (out_pos + len < out_sz - 1) {
                            g_memcpy(out + out_pos, src + start, len);
                            out_pos += len;
                        }
                    }
                }
            } else {
                /* 不是宏, 原样输出 */
                int len = i - start;
                if (out_pos + len < out_sz - 1) {
                    g_memcpy(out + out_pos, src + start, len);
                    out_pos += len;
                }
            }
        } else {
            out[out_pos++] = src[i++];
        }
    }
    out[out_pos] = 0;
    return out_pos;
}

/* 预处理: 读源文件 → 处理 #define/#include/#ifdef → 输出到 out
 * depth: #include 递归深度 (防止无限递归)
 * 返回输出长度, -1 表示错误 */
static int pp_process(const char *src, int src_len, char *out, int out_sz, int depth);

/* ========== [新增] #if 常量表达式求值器 ==========
 * 支持子集:
 *   - 整数字面量 (十/八/十六进制)
 *   - defined(NAME) / defined NAME  (1=已定义, 0=未定义)
 *   - 标识符 (宏被展开后再求值; 未定义标识符视为 0, C 标准如此)
 *   - 一元: !  -  +  ~
 *   - 二元: + - * / %  & | ^  << >>  && ||  < > <= >= == !=
 *   - 三元: ?:
 *   - 括号 ( )
 * 不支持浮点、字符常量 (简化).
 * 入口: pp_eval_const(expr, expr_len) → long
 */
typedef struct {
    const char *s;
    int len;
    int pos;
} PpEval;

static long pp_eval_ternary(PpEval *e);
static long pp_eval_logor(PpEval *e);

static void pp_eval_skip_ws(PpEval *e) {
    while (e->pos < e->len) {
        int c = e->s[e->pos];
        if (c == ' ' || c == '\t') e->pos++;
        else if (c == '/' && e->pos+1 < e->len && e->s[e->pos+1] == '*') {
            e->pos += 2;
            while (e->pos+1 < e->len && !(e->s[e->pos]=='*' && e->s[e->pos+1]=='/')) e->pos++;
            if (e->pos+1 < e->len) e->pos += 2;
        } else if (c == '/' && e->pos+1 < e->len && e->s[e->pos+1] == '/') {
            while (e->pos < e->len && e->s[e->pos] != '\n') e->pos++;
        } else break;
    }
}

static int pp_evalpeek(PpEval *e) {
    pp_eval_skip_ws(e);
    return e->pos < e->len ? (unsigned char)e->s[e->pos] : 0;
}

/* 读取标识符; 返回填充长度 */
static int pp_eval_read_ident(PpEval *e, char *buf, int bufsz) {
    pp_eval_skip_ws(e);
    int n = 0;
    while (e->pos < e->len && pp_is_ident_char(e->s[e->pos]) && n < bufsz-1)
        buf[n++] = e->s[e->pos++];
    buf[n] = 0;
    return n;
}

static long pp_eval_primary(PpEval *e) {
    pp_eval_skip_ws(e);
    int c = pp_evalpeek(e);
    if (c == '(') {
        e->pos++;  /* ( */
        long v = pp_eval_ternary(e);
        pp_eval_skip_ws(e);
        if (e->pos < e->len && e->s[e->pos] == ')') e->pos++;  /* ) */
        return v;
    }
    /* 数字字面量: 0x.. / 0.. / [1-9].. */
    if ((c >= '0' && c <= '9') ||
        (c == '-' && e->pos+1 < e->len && e->s[e->pos+1] >= '0' && e->s[e->pos+1] <= '9')) {
        int neg = 0;
        if (c == '-') { neg = 1; e->pos++; c = pp_evalpeek(e); }
        long v = 0;
        if (c == '0' && e->pos+1 < e->len && (e->s[e->pos+1]=='x' || e->s[e->pos+1]=='X')) {
            e->pos += 2;
            while (e->pos < e->len) {
                int ch = e->s[e->pos]; int d = -1;
                if (ch >= '0' && ch <= '9') d = ch - '0';
                else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
                if (d < 0) break;
                v = v * 16 + d; e->pos++;
            }
        } else if (c == '0' && e->pos+1 < e->len && e->s[e->pos+1] >= '0' && e->s[e->pos+1] <= '7') {
            e->pos++;
            while (e->pos < e->len && e->s[e->pos] >= '0' && e->s[e->pos] <= '7') {
                v = v * 8 + (e->s[e->pos] - '0'); e->pos++;
            }
        } else {
            while (e->pos < e->len && e->s[e->pos] >= '0' && e->s[e->pos] <= '9') {
                v = v * 10 + (e->s[e->pos] - '0'); e->pos++;
            }
        }
        /* 跳过后缀 U/L/UL/LL 等 */
        while (e->pos < e->len &&
               (e->s[e->pos]=='u'||e->s[e->pos]=='U'||e->s[e->pos]=='l'||e->s[e->pos]=='L'))
            e->pos++;
        return neg ? -v : v;
    }
    /* 字符常量 'x' → 简化处理 */
    if (c == '\'') {
        e->pos++;
        long v = (e->pos < e->len) ? (unsigned char)e->s[e->pos] : 0;
        if (e->pos < e->len) e->pos++;
        if (e->pos < e->len && e->s[e->pos] == '\\' && e->pos+1 < e->len) {
            e->pos++;
            int esc = e->s[e->pos];
            switch (esc) {
                case 'n': v = 10; break;
                case 't': v = 9; break;
                case 'r': v = 13; break;
                case '0': v = 0; break;
                case '\\': v = '\\'; break;
                case '\'': v = '\''; break;
                case '"': v = '"'; break;
                default: v = esc; break;
            }
            e->pos++;
        }
        if (e->pos < e->len && e->s[e->pos] == '\'') e->pos++;
        return v;
    }
    /* defined(NAME) / defined NAME */
    if (pp_is_ident_char(c)) {
        char ident[128];
        int saved = e->pos;
        int n = pp_eval_read_ident(e, ident, 128);
        if (n == 7 && g_strcmp(ident, "defined") == 0) {
            pp_eval_skip_ws(e);
            if (e->pos < e->len && e->s[e->pos] == '(') {
                e->pos++;
                char nm[128]; pp_eval_read_ident(e, nm, 128);
                pp_eval_skip_ws(e);
                if (e->pos < e->len && e->s[e->pos] == ')') e->pos++;
                return pp_find_macro(nm) >= 0 ? 1 : 0;
            } else {
                char nm[128]; pp_eval_read_ident(e, nm, 128);
                return pp_find_macro(nm) >= 0 ? 1 : 0;
            }
        }
        /* 普通标识符: 先看是否是对象式宏, 是则替换 body 后递归求值;
         * 否则按 C 标准 = 0 */
        int midx = pp_find_macro(ident);
        if (midx >= 0 && g_macros[midx].n_params < 0) {
            /* 对象式宏: 直接对 body 递归求值 */
            PpEval sub;
            sub.s = g_macros[midx].body;
            sub.len = g_macros[midx].body_len;
            sub.pos = 0;
            return pp_eval_ternary(&sub);
        }
        /* 特殊关键字 true/false (简化支持) */
        if (n == 4 && g_strcmp(ident, "true") == 0) return 1;
        if (n == 5 && g_strcmp(ident, "false") == 0) return 0;
        (void)saved;
        return 0;
    }
    return 0;
}

static long pp_eval_unary(PpEval *e) {
    pp_eval_skip_ws(e);
    int c = pp_evalpeek(e);
    /* 同时检测运算符字符 */
    if (e->pos < e->len) {
        int ch = e->s[e->pos];
        if (ch == '!') { e->pos++; long v = pp_eval_unary(e); return !v; }
        if (ch == '-') { e->pos++; long v = pp_eval_unary(e); return -v; }
        if (ch == '+') { e->pos++; return pp_eval_unary(e); }
        if (ch == '~') { e->pos++; long v = pp_eval_unary(e); return ~v; }
    }
    (void)c;
    return pp_eval_primary(e);
}

static long pp_eval_mul(PpEval *e) {
    long v = pp_eval_unary(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos >= e->len) break;
        int ch = e->s[e->pos];
        if (ch == '*') { e->pos++; v = v * pp_eval_unary(e); }
        else if (ch == '/') { e->pos++; long r = pp_eval_unary(e); v = r ? v / r : 0; }
        else if (ch == '%') { e->pos++; long r = pp_eval_unary(e); v = r ? v % r : 0; }
        else break;
    }
    return v;
}

static long pp_eval_add(PpEval *e) {
    long v = pp_eval_mul(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos >= e->len) break;
        int ch = e->s[e->pos];
        if (ch == '+') { e->pos++; v = v + pp_eval_mul(e); }
        else if (ch == '-') { e->pos++; v = v - pp_eval_mul(e); }
        else break;
    }
    return v;
}

static long pp_eval_shift(PpEval *e) {
    long v = pp_eval_add(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos+1 >= e->len) break;
        if (e->s[e->pos]=='<' && e->s[e->pos+1]=='<') { e->pos+=2; v = v << pp_eval_add(e); }
        else if (e->s[e->pos]=='>' && e->s[e->pos+1]=='>') { e->pos+=2; v = v >> pp_eval_add(e); }
        else break;
    }
    return v;
}

static long pp_eval_rel(PpEval *e) {
    long v = pp_eval_shift(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos+1 >= e->len) break;
        if (e->s[e->pos]=='<' && e->s[e->pos+1]=='=') { e->pos+=2; long r=pp_eval_shift(e); v = (v <= r); }
        else if (e->s[e->pos]=='>' && e->s[e->pos+1]=='=') { e->pos+=2; long r=pp_eval_shift(e); v = (v >= r); }
        else if (e->s[e->pos]=='<' && e->s[e->pos+1]!='<') { e->pos++; long r=pp_eval_shift(e); v = (v < r); }
        else if (e->s[e->pos]=='>' && e->s[e->pos+1]!='>') { e->pos++; long r=pp_eval_shift(e); v = (v > r); }
        else break;
    }
    return v;
}

static long pp_eval_eq(PpEval *e) {
    long v = pp_eval_rel(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos+1 >= e->len) break;
        if (e->s[e->pos]=='=' && e->s[e->pos+1]=='=') { e->pos+=2; long r=pp_eval_rel(e); v = (v == r); }
        else if (e->s[e->pos]=='!' && e->s[e->pos+1]=='=') { e->pos+=2; long r=pp_eval_rel(e); v = (v != r); }
        else break;
    }
    return v;
}

static long pp_eval_band(PpEval *e) {
    long v = pp_eval_eq(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos >= e->len) break;
        if (e->s[e->pos]=='&' && (e->pos+1>=e->len || e->s[e->pos+1]!='&')) {
            e->pos++; v = v & pp_eval_eq(e);
        } else break;
    }
    return v;
}

static long pp_eval_bxor(PpEval *e) {
    long v = pp_eval_band(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos >= e->len) break;
        if (e->s[e->pos]=='^') { e->pos++; v = v ^ pp_eval_band(e); }
        else break;
    }
    return v;
}

static long pp_eval_bor(PpEval *e) {
    long v = pp_eval_bxor(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos >= e->len) break;
        if (e->s[e->pos]=='|' && (e->pos+1>=e->len || e->s[e->pos+1]!='|')) {
            e->pos++; v = v | pp_eval_bxor(e);
        } else break;
    }
    return v;
}

static long pp_eval_logand(PpEval *e) {
    long v = pp_eval_bor(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos+1 >= e->len) break;
        if (e->s[e->pos]=='&' && e->s[e->pos+1]=='&') { e->pos+=2; long r=pp_eval_bor(e); v = (v && r); }
        else break;
    }
    return v;
}

static long pp_eval_logor(PpEval *e) {
    long v = pp_eval_logand(e);
    for (;;) {
        pp_eval_skip_ws(e);
        if (e->pos+1 >= e->len) break;
        if (e->s[e->pos]=='|' && e->s[e->pos+1]=='|') { e->pos+=2; long r=pp_eval_logand(e); v = (v || r); }
        else break;
    }
    return v;
}

static long pp_eval_ternary(PpEval *e) {
    long c = pp_eval_logor(e);
    pp_eval_skip_ws(e);
    if (e->pos < e->len && e->s[e->pos] == '?') {
        e->pos++;
        long a = pp_eval_ternary(e);
        pp_eval_skip_ws(e);
        if (e->pos < e->len && e->s[e->pos] == ':') e->pos++;
        long b = pp_eval_ternary(e);
        return c ? a : b;
    }
    return c;
}

/* 公开入口: 求值 #if 表达式 */
static long pp_eval_const(const char *expr, int expr_len) {
    PpEval e;
    e.s = expr; e.len = expr_len; e.pos = 0;
    return pp_eval_ternary(&e);
}

/* ========== _Static_assert 辅助: 常量表达式中的 sizeof(type) 文本替换 ==========
 * pp_eval_const 只能求值纯整型常量表达式, 不认识 sizeof(type).
 * _Static_assert 最常见用法正是 sizeof(T) == N, 因此在求值前把表达式文本里
 * 所有 sizeof(type) 模式替换成对应类型的字节数.
 *
 * resolve_type_size_from_text: 从原始文本解析一个类型描述, 返回其 size.
 *   支持: struct/union Name, int/char/void/long/short/float/double (+signed/unsigned/long long),
 *         typedef 名, 尾随 '*' (指针=8), 尾随 '[N]' (数组=elem*N).
 */
static int resolve_type_size_from_text(const char *t, int len) {
    int p = 0;
    while (p < len && (t[p]==' '||t[p]=='\t'||t[p]=='\n'||t[p]=='\r')) p++;
    int star_count = 0;

    /* struct / union Name */
    int is_struct = 0;
    if (p + 6 <= len && g_strncmp(t+p, "struct", 6) == 0 && !pp_is_ident_char((unsigned char)t[p+6])) { is_struct = 1; p += 6; }
    else if (p + 5 <= len && g_strncmp(t+p, "union", 5) == 0 && !pp_is_ident_char((unsigned char)t[p+5])) { is_struct = 1; p += 5; }
    if (is_struct) {
        while (p < len && (t[p]==' '||t[p]=='\t')) p++;
        char nm[64]; int ni = 0;
        while (p < len && pp_is_ident_char((unsigned char)t[p]) && ni < 63) nm[ni++] = t[p++];
        nm[ni] = 0;
        while (p < len && t[p]=='*') { star_count++; p++; }
        if (star_count > 0) return 8;
        Type *st = struct_find(nm);
        return st ? st->size : 0;
    }

    /* 收集所有类型词 (int/char/.../unsigned/signed/long/short/typedef名) */
    char words[8][32]; int nw = 0;
    for (;;) {
        while (p < len && (t[p]==' '||t[p]=='\t')) p++;
        if (p >= len || !pp_is_ident_char((unsigned char)t[p])) break;
        char w[32]; int wl = 0;
        while (p < len && pp_is_ident_char((unsigned char)t[p]) && wl < 31) w[wl++] = t[p++];
        w[wl] = 0;
        if (nw < 8) { int k=0; while(w[k] && k<31){words[nw][k]=w[k];k++;} words[nw][k]=0; nw++; }
    }
    /* 尾随 '*' */
    while (p < len && t[p]=='*') { star_count++; p++; }
    if (star_count > 0) return 8;  /* 指针 = 8 字节 */

    /* 用最后一个有意义的类型词判断基础大小; signed/unsigned 不影响 size.
     * 本编译器 long=8, 无独立 long long 类型 → "long long" 视为 long (8). */
    int base_size = -1;
    /* 找到 int/char/void/long/short/float/double 之一 */
    for (int i = 0; i < nw; i++) {
        if      (g_strcmp(words[i], "char")==0)   base_size = 1;
        else if (g_strcmp(words[i], "short")==0)  base_size = 2;
        else if (g_strcmp(words[i], "int")==0)     { if (base_size < 0) base_size = 4; }
        else if (g_strcmp(words[i], "long")==0)    base_size = 8;
        else if (g_strcmp(words[i], "float")==0)   base_size = 4;
        else if (g_strcmp(words[i], "double")==0)  base_size = 8;
        else if (g_strcmp(words[i], "void")==0)    base_size = 1;
    }
    if (base_size >= 0) {
        /* 尾随 '[N]' 数组 */
        while (p < len && (t[p]==' '||t[p]=='\t')) p++;
        if (p < len && t[p]=='[') {
            p++;
            int arrlen = 0;
            while (p < len && t[p] >= '0' && t[p] <= '9') { arrlen = arrlen*10 + (t[p]-'0'); p++; }
            return base_size * (arrlen > 0 ? arrlen : 0);
        }
        return base_size;
    }

    /* 可能是 typedef 名 (单一标识符) */
    if (nw == 1) {
        Symbol *sy = sym_find(words[0], g_scope_depth);
        if (sy && sy->kind == SK_TYPE && sy->type) {
            while (p < len && (t[p]==' '||t[p]=='\t')) p++;
            if (p < len && t[p]=='[') {
                p++;
                int arrlen = 0;
                while (p < len && t[p] >= '0' && t[p] <= '9') { arrlen = arrlen*10 + (t[p]-'0'); p++; }
                return sy->type->size * (arrlen > 0 ? arrlen : 0);
            }
            return sy->type->size;
        }
    }
    return 0;
}

/* 在表达式文本中把 sizeof(type) 替换为数字. out 以 0 结尾, 返回长度. */
static int subst_sizeof_in_expr(const char *src, int len, char *out, int out_sz) {
    int oi = 0, i = 0;
    while (i < len) {
        /* 检测 "sizeof" 作为完整单词 */
        if (i + 6 <= len && g_strncmp(src+i, "sizeof", 6) == 0) {
            int prev_ok = (i == 0) || !pp_is_ident_char((unsigned char)src[i-1]);
            int next_ok = (i+6 >= len) || !pp_is_ident_char((unsigned char)src[i+6]);
            if (prev_ok && next_ok) {
                int j = i + 6;
                while (j < len && (src[j]==' '||src[j]=='\t')) j++;
                if (j < len && src[j] == '(') {
                    int depth = 1; j++;
                    int type_start = j;
                    while (j < len && depth > 0) {
                        if (src[j] == '(') depth++;
                        else if (src[j] == ')') { depth--; if (depth==0) break; }
                        j++;
                    }
                    int type_end = j;  /* ')' 的位置 */
                    int sz = resolve_type_size_from_text(src + type_start, type_end - type_start);
                    /* 写入数字 (非负) */
                    char numbuf[24]; int nl = 0;
                    if (sz == 0) numbuf[nl++] = '0';
                    else { int tmp = sz; char tb[24]; int tn=0;
                        while (tmp) { tb[tn++] = '0' + (tmp%10); tmp /= 10; }
                        for (int k = tn-1; k >= 0; k--) numbuf[nl++] = tb[k];
                    }
                    for (int k = 0; k < nl && oi < out_sz-1; k++) out[oi++] = numbuf[k];
                    i = (type_end < len) ? type_end + 1 : len;  /* 跳过 sizeof(...) */
                    continue;
                }
            }
        }
        if (oi < out_sz - 1) out[oi++] = src[i++];
        else i++;
    }
    out[oi] = 0;
    return oi;
}

/* 处理 #include: 读取文件并内联 */
static int pp_handle_include(const char *line, char *out, int out_pos, int out_sz, int depth) {
    /* 找到引号或尖括号 */
    int p = 0;
    while (line[p] && line[p] != '"' && line[p] != '<') p++;
    char fpath[256];
    int fp_len = 0;
    if (line[p] == '"') {
        p++;
        while (line[p] && line[p] != '"' && fp_len < 255) fpath[fp_len++] = line[p++];
    } else if (line[p] == '<') {
        p++;
        while (line[p] && line[p] != '>' && fp_len < 255) fpath[fp_len++] = line[p++];
    }
    fpath[fp_len] = 0;
    if (fp_len == 0) return out_pos;

    /* #pragma once: 检查是否已 once 过此路径 */
    for (int k = 0; k < g_pp_once_n; k++) {
        if (g_strcmp(g_pp_once_paths[k], fpath) == 0) {
            /* 已 once 过, 跳过本次 include */
            return out_pos;
        }
    }

    /* 限制递归深度 */
    if (depth >= MAX_INCLUDE_DEPTH) return out_pos;

    /* 读取文件 */
    char *inc_src = g_malloc(256 * 1024);
    if (!inc_src) return out_pos;
    int inc_sz = API->file_read ? API->file_read(fpath, inc_src, 256*1024 - 1) : -1;
    if (inc_sz < 0) { g_free(inc_src); return out_pos; }
    inc_src[inc_sz] = 0;

    /* 递归预处理前清 pending, 调用后检查是否设置了 pending */
    g_pp_once_pending = 0;
    int n = pp_process(inc_src, inc_sz, out + out_pos, out_sz - out_pos, depth + 1);
    /* 若 pp_process 期间遇到 #pragma once, pending 被置 1, 把 fpath 加入列表 */
    if (g_pp_once_pending && g_pp_once_n < MAX_ONCE_PATHS) {
        int k = 0; while (fpath[k] && k < 255) { g_pp_once_paths[g_pp_once_n][k] = fpath[k]; k++; }
        g_pp_once_paths[g_pp_once_n][k] = 0;
        g_pp_once_n++;
    }
    g_pp_once_pending = 0;
    g_free(inc_src);
    return out_pos + n;
}

/* 主预处理函数 */
static int pp_process(const char *src, int src_len, char *out, int out_sz, int depth) {
    int out_pos = 0;
    int i = 0;
    /* 条件编译栈: 每层三元组
     *   cond_parent[k] = 外层 (父分支) 当前是否激活
     *   cond_taken[k]  = 本层 #if/#elif 链中是否已有任意分支被选中过
     *   cond_active[k] = 本层当前分支是否激活 (父=1 且 本分支表达式=1 且 之前未 taken)
     */
    int cond_parent[64];
    int cond_taken[64];
    int cond_active[64];
    int cond_n = 0;

    int active = 1;  /* 当前是否在激活的分支中 */

    while (i < src_len && out_pos < out_sz - 1) {
        /* 找到行首 */
        int line_start = i;
        /* 跳过行首空白 */
        while (i < src_len && (src[i] == ' ' || src[i] == '\t')) i++;
        if (i < src_len && src[i] == '#') {
            /* 预处理指令: 读取整行 */
            i++;  /* 跳过 # */
            /* 读取指令名 */
            int ws = i;
            while (ws < src_len && (src[ws] == ' ' || src[ws] == '\t')) ws++;
            char directive[32]; int di = 0;
            while (ws < src_len && pp_is_ident_char(src[ws]) && di < 31)
                directive[di++] = src[ws++];
            directive[di] = 0;
            /* 读取行剩余内容 */
            int rest_start = ws;
            int rest_end = rest_start;
            while (rest_end < src_len && src[rest_end] != '\n') rest_end++;
            /* 处理行续接 '\' */
            /* (简化: 不处理行续接, 大多数源码可接受) */

            /* 计算当前是否激活: 所有 cond 层都激活才算激活 */
            active = 1;
            for (int c = 0; c < cond_n; c++) if (!cond_active[c]) { active = 0; break; }

            if (g_strcmp(directive, "define") == 0) {
                if (!active) { i = rest_end; continue; }
                /* 解析: #define NAME(params) body  或  #define NAME body */
                int p = pp_skip_ws(src, rest_start);
                char name[128];
                p = pp_read_ident(src, p, name, 128);
                int n_params = -1;  /* 默认对象式 */
                int is_vararg = 0;   /* 可变参数宏 */
                char params[8][32];
                /* 检查是否紧跟 ( (无空格) → 函数式宏 */
                if (src[p] == '(') {
                    p++;
                    n_params = 0;
                    int pp2 = pp_skip_ws(src, p);
                    if (src[pp2] != ')') {
                        for (;;) {
                            pp2 = pp_skip_ws(src, pp2);
                            /* 检测 ... 可变参数末尾 (a, b, ...) */
                            if (pp2+2 < src_len && src[pp2]=='.' && src[pp2+1]=='.' && src[pp2+2]=='.') {
                                is_vararg = 1;
                                pp2 += 3;
                                while (pp2 < src_len && src[pp2] != ')') pp2++;
                                break;
                            }
                            int pi = 0;
                            while (pp2 < src_len && pp_is_ident_char(src[pp2]) && pi < 63)
                                params[n_params][pi++] = src[pp2++];
                            params[n_params][pi] = 0;
                            n_params++;
                            pp2 = pp_skip_ws(src, pp2);
                            if (src[pp2] == ',') {
                                pp2++;
                                int pv = pp_skip_ws(src, pp2);
                                /* 检测 ", ..." */
                                if (pv+2 < src_len && src[pv]=='.' && src[pv+1]=='.' && src[pv+2]=='.') {
                                    is_vararg = 1;
                                    pp2 = pv + 3;
                                    while (pp2 < src_len && src[pp2] != ')') pp2++;
                                    break;
                                }
                                continue;
                            }
                            break;
                        }
                    }
                    while (pp2 < src_len && src[pp2] != ')') pp2++;
                    if (pp2 < src_len) pp2++;  /* 跳过 ) */
                    p = pp2;
                }
                /* body: 跳过前导空白, 到行尾 */
                p = pp_skip_ws(src, p);
                int body_len = rest_end - p;
                /* 去尾部空白 */
                while (body_len > 0 && (src[p + body_len - 1] == ' ' || src[p + body_len - 1] == '\r' || src[p + body_len - 1] == '\t'))
                    body_len--;
                pp_add_macro(name, n_params, is_vararg, src + p, body_len);
                /* 复制参数到宏结构 */
                if (n_params >= 0) {
                    int midx = pp_find_macro(name);
                    if (midx >= 0) {
                        g_macros[midx].n_params = n_params;
                        g_macros[midx].is_vararg = is_vararg;
                        for (int k = 0; k < n_params; k++) {
                            int li = 0; while (params[k][li] && li < 63) { g_macros[midx].params[k][li] = params[k][li]; li++; }
                            g_macros[midx].params[k][li] = 0;
                        }
                    }
                }
                i = rest_end;
                continue;
            }
            if (g_strcmp(directive, "undef") == 0) {
                if (!active) { i = rest_end; continue; }
                int p = pp_skip_ws(src, rest_start);
                char name[128];
                p = pp_read_ident(src, p, name, 128);
                int midx = pp_find_macro(name);
                if (midx >= 0) {
                    /* 标记为已取消: 简单删除 (后移) */
                    for (int k = midx; k < g_macros_n - 1; k++)
                        g_macros[k] = g_macros[k+1];
                    g_macros_n--;
                }
                i = rest_end;
                continue;
            }
            if (g_strcmp(directive, "include") == 0) {
                if (!active) { i = rest_end; continue; }
                /* 提取路径 */
                int p = rest_start;
                while (p < rest_end && src[p] != '"' && src[p] != '<') p++;
                char fpath[256]; int fp_len = 0;
                if (p < rest_end && src[p] == '"') {
                    p++;
                    while (p < rest_end && src[p] != '"' && fp_len < 255) fpath[fp_len++] = src[p++];
                } else if (p < rest_end && src[p] == '<') {
                    p++;
                    while (p < rest_end && src[p] != '>' && fp_len < 255) fpath[fp_len++] = src[p++];
                }
                fpath[fp_len] = 0;
                if (fp_len > 0 && depth < MAX_INCLUDE_DEPTH) {
                    char *inc_src = g_malloc(256 * 1024);
                    if (inc_src) {
                        int inc_sz = API->file_read ? API->file_read(fpath, inc_src, 256*1024 - 1) : -1;
                        if (inc_sz >= 0) {
                            inc_src[inc_sz] = 0;
                            int n = pp_process(inc_src, inc_sz, out + out_pos, out_sz - out_pos, depth + 1);
                            out_pos += n;
                        }
                        g_free(inc_src);
                    }
                }
                i = rest_end;
                continue;
            }
            if (g_strcmp(directive, "ifdef") == 0) {
                int parent_active = active;
                int p = pp_skip_ws(src, rest_start);
                char name[128];
                p = pp_read_ident(src, p, name, 128);
                int defined = (pp_find_macro(name) >= 0);
                cond_parent[cond_n] = parent_active ? 1 : 0;
                cond_taken[cond_n]  = (defined && parent_active) ? 1 : 0;
                cond_active[cond_n] = cond_taken[cond_n];
                cond_n++;
                i = rest_end;
                continue;
            }
            if (g_strcmp(directive, "ifndef") == 0) {
                int parent_active = active;
                int p = pp_skip_ws(src, rest_start);
                char name[128];
                p = pp_read_ident(src, p, name, 128);
                int defined = (pp_find_macro(name) >= 0);
                cond_parent[cond_n] = parent_active ? 1 : 0;
                cond_taken[cond_n]  = (!defined && parent_active) ? 1 : 0;
                cond_active[cond_n] = cond_taken[cond_n];
                cond_n++;
                i = rest_end;
                continue;
            }
            if (g_strcmp(directive, "if") == 0) {
                /* 真正求值常量表达式: defined() 支持已在 pp_eval_const 中处理 */
                int parent_active = active;
                int cond_val = 0;
                if (parent_active) {
                    /* 先把行内宏做对象式展开 (函数式宏跳过, defined() 会用到的)
                     * 为简化, 直接对原文求值 — pp_eval_const 内部会展开对象式宏 */
                    cond_val = (pp_eval_const(src + rest_start, rest_end - rest_start) != 0) ? 1 : 0;
                }
                cond_parent[cond_n] = parent_active ? 1 : 0;
                cond_taken[cond_n]  = cond_val;
                cond_active[cond_n] = cond_val;
                cond_n++;
                i = rest_end;
                continue;
            }
            if (g_strcmp(directive, "else") == 0) {
                if (cond_n > 0) {
                    int parent = cond_parent[cond_n-1];
                    int take = (!cond_taken[cond_n-1] && parent) ? 1 : 0;
                    cond_active[cond_n-1] = take;
                    if (take) cond_taken[cond_n-1] = 1;
                }
                i = rest_end;
                continue;
            }
            if (g_strcmp(directive, "elif") == 0) {
                /* 仅当本层父分支激活 且 之前未 taken 时才求值表达式 */
                if (cond_n > 0) {
                    int parent = cond_parent[cond_n-1];
                    int take = 0;
                    if (parent && !cond_taken[cond_n-1]) {
                        int cond_val = (pp_eval_const(src + rest_start, rest_end - rest_start) != 0) ? 1 : 0;
                        take = cond_val;
                    }
                    cond_active[cond_n-1] = take;
                    if (take) cond_taken[cond_n-1] = 1;
                }
                i = rest_end;
                continue;
            }
            if (g_strcmp(directive, "endif") == 0) {
                if (cond_n > 0) cond_n--;
                i = rest_end;
                continue;
            }
            /* #error MSG: 输出消息并 panic (符合 C 标准) */
            if (g_strcmp(directive, "error") == 0) {
                /* 输出 "error: " + 行内容 */
                g_print("#error: ");
                int p = rest_start;
                while (p < rest_end) { g_putc(src[p++]); }
                g_putc('\n');
                /* 即使非 active 也报 (C 标准如此, 这里简化为 active 才报) */
                if (active) g_panic("#error directive");
                i = rest_end;
                continue;
            }
            /* #line N "file": 改变后续行号/文件名 (简化: 忽略) */
            if (g_strcmp(directive, "line") == 0) {
                i = rest_end;
                continue;
            }
            /* #pragma: 在 #pragma once 中处理, 其他忽略 */
            if (g_strcmp(directive, "pragma") == 0) {
                /* 检测 "once" */
                int p = pp_skip_ws(src, rest_start);
                char kw[16]; int ki = 0;
                while (p < rest_end && pp_is_ident_char(src[p]) && ki < 15) kw[ki++] = src[p++];
                kw[ki] = 0;
                if (g_strcmp(kw, "once") == 0) {
                    /* 设置 pending 标志, 由 pp_handle_include 在本次调用结束后
                     * 把刚 include 的路径加入 once 列表 */
                    g_pp_once_pending = 1;
                }
                /* #pragma pack(N) / pack(push, N) / pack(pop) - 简化: 只取末尾数字 */
                else if (g_strcmp(kw, "pack") == 0) {
                    /* 找末尾括号内的数字 */
                    int q = p;
                    while (q < rest_end && src[q] != '(') q++;
                    if (q < rest_end) {
                        q++;  /* ( */
                        int n = 0; int has_num = 0;
                        /* 简化: 跳过 push/pop, 找数字 */
                        while (q < rest_end && src[q] != ')') {
                            if (src[q] >= '0' && src[q] <= '9') {
                                n = n * 10 + (src[q] - '0');
                                has_num = 1;
                            }
                            q++;
                        }
                        if (has_num) g_pp_pack = n;
                        /* pack() 无参数 = 恢复默认 */
                        else g_pp_pack = 0;
                    }
                }
                /* 其他 pragma: warning, GCC diagnostic 等 - 忽略 */
                i = rest_end;
                continue;
            }
            /* 未知指令: 忽略 */
            i = rest_end;
            continue;
        }
        /* 普通行: 如果当前激活, 复制并展开宏 */
        if (active) {
            /* 找到行尾 */
            int line_end = i;
            while (line_end < src_len && src[line_end] != '\n') line_end++;
            /* 展开宏 */
            char expanded[8192];
            int elen = pp_expand_text(src + i, line_end - i, expanded, 8192);
            if (out_pos + elen < out_sz - 1) {
                g_memcpy(out + out_pos, expanded, elen);
                out_pos += elen;
            }
            /* 复制换行符 */
            if (line_end < src_len && out_pos < out_sz - 1)
                out[out_pos++] = src[line_end++];
            i = line_end;
        } else {
            /* 跳过非激活的行 */
            while (i < src_len && src[i] != '\n') i++;
            if (i < src_len) i++;  /* 跳过换行 */
        }
    }
    out[out_pos] = 0;
    return out_pos;
}

/* ========== 入口函数 _start + 转 main(argc, argv) ==========
 * flat binary 首字节就是 _start. 我们在 codegen 最开头生成启动 stub:
 *   对齐栈 → xor ebp,ebp → call main → mov edi, eax → call exit
 * 若程序中没有 main, 我们报错。 */
static long g_start_stub_end_lab IN_DATA = 0;

static void emit_start_stub(void) {
    long main_ref = new_label();  /* 占位, main 符号如果已定义 later patch, 否则报错 */
    /* 启动 stub 占一个 label 0 开始 */
    long lstart = new_label(); def_label(lstart);
    /* xor ebp,ebp (栈帧链结束标志) */
    emit_xor_rr(5,5);
    /* mov edi, 0; mov esi, 0 → argc, argv (本编译器暂不提供命令行参数给 main, 但占位) */
    emit_xor_rr(7,7); /* edi = 0 argc */
    emit_xor_rr(6,6); /* rsi = 0 argv */
    /* 调用 main (patch main 符号地址) */
    emit_b(0xE8); patch_rel32_here(main_ref);
    /* 退出 (mov rdi, rax; call exit) */
    emit_mov_rr(7,0);
    emit_b(0xE8); /* call exit; 先写 5 字节; 稍后 patch exit 地址 */
    long exit_ref = new_label();
    patch_rel32_here(exit_ref);
    long stub_end = new_label(); def_label(stub_end);
    g_start_stub_end_lab = stub_end;
    /* 保存 main_ref/exit_ref → 之后 main 符号定义后回填
     * [BSS 崩溃修复] function-local static 默认进 .bss → 用 IN_DATA 强制 .data */
    static long saved_main_ref IN_DATA = 0, saved_exit_ref IN_DATA = 0;
    saved_main_ref = main_ref; saved_exit_ref = exit_ref;
}

/* 在解析完全局声明后, 我们回填 main_ref, exit_ref 到 symbol 地址 */
static void patch_start_stub_refs(void) {
    Symbol *mn = sym_find_global("main");
    if (!mn) { g_print("error: no 'main' function\n"); g_panic("missing main"); }
    /* 取 start stub 里 saved_main_ref (我们放在 label 0 = main_ref 的 label_id 是 1? 需要从 new_label 顺序知道. 简化: 我们重新实现:
     * emit_start_stub 时它的 main_ref 是第一个 new_label 之后生成的第 1 个 (label 1) , exit_ref = label 3 (在 lstart=0, main_ref=1, stub_end=2, exit_ref=3? 实际顺序:
     *   lstart = new_label() → 0; def_label(0); main_ref = new_label() → 1; emit call (main_ref);
     *   exit_ref = new_label() → 2; emit call (exit_ref); stub_end = new_label() → 3; def_label(3)
     * 这取决于. 为了可靠, 我们不这样做; 而是在 emit_start_stub() 内部把 main_ref 对应到一个 label, 然后在 parse 完后 def_label(main_ref, main->addr).
     * 但我们只有 def_label(id) 不支持给一个已 label_id 直接赋 VA. 解决方法: 我们在 parse 完全局后, 直接遍历 g_patches 里的 label_id=main_ref/exit_ref 并修改 target VA? 其实 patch_all 只查 label_va(), 所以我们只要给 main_ref 这个 label_id 人工添加一条 LabelVA 即可。 */
    /* label id 的分配我们无法精确拿到. 改方法: 不 patch rel32_here 调用, 而是在 parse 之后直接调用 start stub 重写.
     * 更简单: 启动 stub 用 mov rax, imm64; call rax. 这样之后我们只需要改 imm64 处. */
    /* 由于已经写了 code → 我们选择: 忽略 start stub 正确性, 改走另一个入口: 给 _start 作为 main 后序代码.
     * 实际简单办法: 我们在 parse 完后用 jmp 到 main: 把整个 flat binary 尾部追加 "mov edi,0; mov esi,0; call [main_addr]; call exit" thunk, 并让文件 load_addr + 0 = 这部分代码.
     * 我们先在所有 code 生成完毕后做最后的 binary 组装:
     * 生成 binary 时, 在最前面 prepend 一个 prolog (8 bytes 以内): jmp rel8? rel32.
     * 具体: binary = [5 bytes E9 <rel32 to real start stub>][rest of text/rodata/data]
     *       real start stub 单独拼接在 text 末尾 (postfix).
     * 采用: 我们在 code 生成结束后, 先生成 start stub, 然后 binary 拼接:
     *       final_text = [real_start_stub] + [original_text] + [rodata] + [data]
     *       load_addr + 0 = start of final_text
     * 实现如下: */
}

/* 重新写一个 clean 的 start stub, 在所有代码生成之后拼到 text 最前面.
 * [关键对齐修复] 之前我们设置 va_text_base = load_addr + 64 (prolog_reserve=64),
 *   但如果实际 prolog (pn) < 64, original_text 的实际文件偏移就是 pn,
 *   那么内核加载后: load_addr+64 对应的是 original_text[64 - pn], 而不是 original_text[0]!
 *   这会导致所有 call/jmp/imm64 label 地址全部错位 64-pn 字节 → 立即 crash.
 *
 * 修复: 在 prolog 后面用 NOP (0x90) 填充到 prolog_reserve=64 字节, 确保
 *   final_bin[64] = original_text[0] → load_addr+64 = va_text_base 正确! */
static char *BuildBinary(long *out_size) {
    com1_raw('(');  /* build_binary: 查找 main (强制, 不受 g_debug 影响) */
    Symbol *mn = sym_find_global("main");
    if (!mn) { com1_raw('!'); com1_raw('m'); g_print("error: no main() function defined\n"); g_panic("no main"); }
    /* [VITAL 诊断] 输出找到的 mn 符号信息: name / kind / defined / addr
     * 防止: sym_find_global 匹配到错误的符号 (同名前向声明 addr=0 defined=0 而非函数定义) */
    com1_raw('S'); com1_raw('Y'); com1_raw('M'); com1_raw('=');
    com1_str(mn->name); com1_raw(',');
    com1_raw('K'); com1_dec((int)mn->kind); com1_raw(',');   /* SK_VAR=0 SK_FUNC=1 */
    com1_raw('D'); com1_dec((int)mn->defined); com1_raw(',');/* defined=1 → OK */
    com1_raw('G'); com1_dec((int)mn->is_global); com1_raw('\n');
    com1_raw(')');  /* main found */
    Symbol *ex = sym_find_global("exit");
    /* [诊断 V2] 使用 com1_raw (绕过 g_debug) 输出关键 VA, 运行时验证 */
    com1_raw('#');  /* 分隔符: 以下是 build_binary 内部诊断输出 (raw, 不受 g_debug) */
    com1_hex((unsigned long)va_text_base);   /* T=va_text_base (应 == load_addr+356) */
    com1_raw('M'); com1_hex((unsigned long)mn->addr);       /* M=main_addr (应 == va_text_base+main_offset) */
    com1_raw('E'); com1_hex((unsigned long)(ex? ex->addr : -1));       /* E=exit_addr */
    com1_raw('B'); com1_hex((unsigned long)va_bss_base);    /* B=va_bss_base */
    com1_raw('L'); com1_hex((unsigned long)g_bss_len);  /* L=g_bss_len */
    com1_raw('A'); com1_hex((unsigned long)g_out_load_addr);  /* A=g_out_load_addr (1.efs 默认 0x1000000) */
    com1_raw('N'); com1_hex((unsigned long)g_text_n);  /* N=g_text_n (builtin stubs + 用户函数代码总长度) */
    com1_raw('O'); com1_hex((unsigned long)g_ro_n);    /* O=g_ro_n */
    com1_raw('R'); com1_dec(g_reloc_n); com1_raw('\n');    /* R=精确重定位表条目数 */
    /* [关键修复 #8 BUFFER OVERFLOW] 原 char pro[256] + prolog_reserve=356:
     * while (pn < prolog_reserve) pro[pn++]=NOP; 会写溢出 pro 数组 100 字节!
     * 栈上 pro[256] 之后存着 Symbol *mn / Symbol *ex / pn / 返回地址等,
     * 全被 0x90 (NOP) 覆盖 → mn = 0x90909090 → mn->addr = 读 0x90909090+offsetof(addr)
     * → prolog 写 movabs rax, 0 → call rax → 跳到 0 地址, 取指 #PF cr2=0!
     * 修复: pro 数组扩容到 2048, 远超任何合理 prolog 大小 (356). */
    char pro[2048]; int pn=0;
    /* [关键修复] prolog 建立标准帧并保存 rbp 到 exit 帧槽:
     *   push rbp; mov rbp,rsp; movabs rax,slot; mov [rax],rbp
     * 内核 efs_task_entry: mov r13,rsp; mov r13,rbp; call *r12
     * → 入口时 rbp=rsp=ustack_top, [rsp]=内核返回地址.
     * push rbp 后 rbp 指向帧基, exit stub 用槽恢复此帧 → pop rbp; ret 正常回内核. */
    if (g_exit_frame_slot == 0) g_panic("exit frame slot not init");
    /* [运行时 trace] 小宏: 7 字节 COM1 直写 (mov dx,0x3F8; mov al,c; out dx,al) */
    #define PRO_TRACE(ch) do { \
        pro[pn++]=0x66; pro[pn++]=0xBA; pro[pn++]=0xF8; pro[pn++]=0x03; \
        pro[pn++]=0xB0; pro[pn++]=(char)(ch); \
        pro[pn++]=0xEE; \
    } while(0)
    PRO_TRACE('V');   /* 运行时: 入口第一条指令 — [VERSION 标记] 若串口仍出现'0'→跑的是旧 efcc.efs */
    /* push rbp = 55 */
    pro[pn++]=0x55;
    PRO_TRACE('W');   /* [VERSION] 旧代码为 '1' */
    /* mov rbp, rsp = 48 89 E5 */
    pro[pn++]=0x48; pro[pn++]=0x89; pro[pn++]=0xE5;
    PRO_TRACE('2');
    /* movabs rax, slot_va = 48 B8 imm64 */
    pro[pn++]=0x48; pro[pn++]=0xB8;
    for(int i=0;i<8;i++) pro[pn++]=(char)((g_exit_frame_slot>>(i*8))&0xFF);
    PRO_TRACE('3');
    /* mov [rax], rbp = 48 89 28 */
    pro[pn++]=0x48; pro[pn++]=0x89; pro[pn++]=0x28;
    PRO_TRACE('4');   /* 运行时: 帧已保存到槽 (原'F') */

    /* [BSS 清零 根因修复] efcc.c 子产物 objcopy 只抽取 .text/.rodata/.data 写 flat binary,
     *   内核 run_efs() 也不会清零 → 未初始化全局变量 (BSS) = 上次遗留随机垃圾
     *   → 全局指针/字符串指针指向随机地址 (比如 0xFFFF_FFFF_1410_C1C0) 立刻 #PF.
     *   [关键 BUG 修复] 之前 jz .Lbss_done 直接放在 movabs rcx 之后:
     *     movabs 不修改 FLAGS! 所以 jz 用的是内核残留的 STALE FLAGS!
     *     正确: 在 jz 前加 test rcx,rcx 设置 ZF。
     *   指令序列:
     *     movabs rdi, va_bss_base        (10B)
     *     movabs rcx, g_bss_len          (10B)
     *     test   rcx, rcx                ; ← 新增, 设置 ZF=1 当且仅当 bss_len==0
     *     jz     .Lbss_done              (跳过 0 长 BSS, 避免 rep 下溢)
     *     xor    eax, eax
     *     cld
     *     rep stosb
     *   .Lbss_done: */
    if (g_bss_len > 0) {
        long bss_start_va = va_bss_base;
        long bss_sz      = (long)g_bss_len;
        /* movabs rdi, bss_start_va → 48 BF imm64 */
        pro[pn++]=0x48; pro[pn++]=0xBF;
        for(int i=0;i<8;i++) pro[pn++]=(char)((bss_start_va>>(i*8))&0xFF);
        PRO_TRACE('5');
        /* movabs rcx, bss_sz → 48 B9 imm64 */
        pro[pn++]=0x48; pro[pn++]=0xB9;
        for(int i=0;i<8;i++) pro[pn++]=(char)((bss_sz>>(i*8))&0xFF);
        PRO_TRACE('6');
        /* [BUG FIX] test rcx,rcx → 48 85 C9: 显式设置 ZF, 不再依赖残留 FLAGS! */
        pro[pn++]=0x48; pro[pn++]=0x85; pro[pn++]=0xC9;
        PRO_TRACE('7');
        /* jz .Lbss_done → 74 XX; 跳过 6 字节 (xor+cld+rep_stosb = 2+1+2 = 5? 加 PRO_TRACE('8') 重算) */
        {
            /* 跳过目标: PRO_TRACE('8') 之后 (即 xor+cld+rep_stosb 三条指令, 共 2+1+2=5 字节) */
            int skip = 2+1+2;  /* 目标指令字节数: jz 跳过这些字节到 PRO_TRACE('9') 之前 */
            pro[pn++]=0x74; pro[pn++]=(char)skip;
        }
        PRO_TRACE('8');
        /* xor eax,eax → 31 C0 */
        pro[pn++]=0x31; pro[pn++]=0xC0;
        /* cld → FC */
        pro[pn++]=0xFC;
        /* rep stosb → F3 AA */
        pro[pn++]=0xF3; pro[pn++]=0xAA;
        /* .Lbss_done: */
    } else {
        /* BSS 长度 0: 跳过整个清零块 */
    }
    PRO_TRACE('9');   /* 运行时: BSS 清零完成 (原'Z') */

    /* xor edi,edi = 31 FF (argc=0) */
    pro[pn++]=0x31; pro[pn++]=0xFF;
    PRO_TRACE('a');
    /* xor esi,esi = 31 F6 (argv=0) */
    pro[pn++]=0x31; pro[pn++]=0xF6;
    PRO_TRACE('b');
    /* movabs rax, main_addr */
    pro[pn++]=0x48; pro[pn++]=0xB8;
    long mva = mn->addr;
    for(int i=0;i<8;i++) pro[pn++]=(char)((mva>>(i*8))&0xFF);
    /* [运行时诊断]  movabs rax, main_addr 之后, call rax 之前 dump rax 到 COM1,
     * 验证 rax 是否为预期值 (0x1000000 + 356 + offset). 如果是 0 → mn->addr 写坏了.
     * 指令序列:
     *   push rcx; push rbx; mov rbx,rax   ; save rcx/rax (rbx = rax copy)
     *   mov dx, 0x3FD                    ; LSR
     *   mov cl, 16                       ; 16 hex digits
     * L1: in al, dx; test $0x20,al; jz L1 ; wait THR empty
     *   rol rbx, 4; mov al,bl; and $0xF,al
     *   cmp $10,al; sbb al,0x69; das     ; hex digit (al = 0..9 -> '0'..'9', A..F -> 'A'..'F')
     *   mov dx,0x3F8; out dx,al; mov dx,0x3FD
     *   dec cl; jnz L1
     *   mov dx,0x3FD; L2: in al,dx; test $0x20,al; jz L2
     *   mov al,0x20; mov dx,0x3F8; out dx,al   ; ' ' space
     *   pop rbx; pop rcx                      ; restore */
    {
        /* [HEX BUG FIX] 原: cmp al,10 → CF=1 当 al<10; adc al,0x30 → al=n+0x31 (n=0..8) → off by 1.
         * 正确: 先 add al,0x30. 如果结果 > '9' (非数字), 再加 7 跳到 'A'..'F'.
         * 重写 hex 转换的 2 条指令 + 分支. */
        unsigned char dump[] = {
            0x51, 0x53,                                  /* 0: push rcx; push rbx */
            0x48, 0x89, 0xC3,                            /* 2: mov rbx, rax */
            0x66, 0xBA, 0xFD, 0x03,                      /* 5: mov dx, 0x3FD (LSR) */
            0xB1, 0x10,                                  /* 9: mov cl, 16 */
            /* L1: wait THR empty. offset = 0x0B */
            0xEC,                                        /* 0x0B: in al, dx */
            0xA8, 0x20,                                  /* 0x0C: test al, 0x20 */
            0x74, 0xFA,                                  /* 0x0E: jz 0x0B (L1) */
            0x48, 0xC1, 0xC3, 0x04,                      /* 0x10: rol rbx, 4 */
            0x88, 0xD8,                                  /* 0x14: mov al, bl */
            0x24, 0x0F,                                  /* 0x16: and al, 0x0F */
            0x04, 0x30,                                  /* 0x18: add al, 0x30 → al = n+'0' (0..9) or ':'..'?' (10..15) */
            0x3C, 0x39,                                  /* 0x1A: cmp al, '9' */
            0x76, 0x04,                                  /* 0x1C: jbe +4 (skip add 7 → it's 0..9). +4 = 0x1C+2+4 = 0x22 */
            0x04, 0x07,                                  /* 0x1E: add al, 7 → 'A'..'F' */
            /* offset 0x20: output to COM1 */
            0x66, 0xBA, 0xF8, 0x03,                      /* 0x20: mov dx, 0x3F8 (THR) */
            0xEE,                                        /* 0x24: out dx, al */
            0x66, 0xBA, 0xFD, 0x03,                      /* 0x25: mov dx, 0x3FD */
            0xFE, 0xC9,                                  /* 0x29: dec cl */
            0x75, 0xDE,                                  /* 0x2B: jnz 0x0B (-34 = 0xDE back to in al,dx) */
            /* L2: wait before space. offset 0x2D */
            0xEC,                                        /* 0x2D: in al, dx */
            0xA8, 0x20,                                  /* 0x2E: test al, 0x20 */
            0x74, 0xFA,                                  /* 0x30: jz 0x2D (L2) */
            0xB0, 0x20,                                  /* 0x32: mov al, ' ' */
            0x66, 0xBA, 0xF8, 0x03,                      /* 0x34: mov dx, 0x3F8 */
            0xEE,                                        /* 0x38: out dx, al */
            0x5B, 0x59                                   /* 0x39: pop rbx; pop rcx */
            /* total = 0x3B = 59 bytes */
        };
        for(unsigned u=0;u<sizeof(dump);u++) pro[pn++]=dump[u];
    }
    PRO_TRACE('c');
    /* call rax = FF D0 */
    pro[pn++]=0xFF; pro[pn++]=0xD0;
    PRO_TRACE('d');   /* 运行时: main 返回 (原'R') */
    /* mov rdi, rax = 48 89 C7 (exit code = main 返回值) */
    pro[pn++]=0x48; pro[pn++]=0x89; pro[pn++]=0xC7;
    PRO_TRACE('e');
    /* movabs rax, exit_addr */
    pro[pn++]=0x48; pro[pn++]=0xB8;
    long eva = ex->addr;
    for(int i=0;i<8;i++) pro[pn++]=(char)((eva>>(i*8))&0xFF);
    PRO_TRACE('f');
    /* jmp rax = FF E0 */
    pro[pn++]=0xFF; pro[pn++]=0xE0;
    PRO_TRACE('g');   /* 不应到达: jmp exit 之前的最后一个标记 */
    #undef PRO_TRACE

    /* [扩容] NOP 填充到 prolog_reserve = 356 字节 (之前 256, 故意扩大 100 字节以验证 1.efs 大小必定变化) */
    int prolog_reserve = 356;
    if (pn > prolog_reserve) {
        g_print("efcc internal: prolog too large ("); g_print_int(pn); g_print(" > 256)\n");
        g_panic("prolog overflow");
    }
    while (pn < prolog_reserve) pro[pn++] = 0x90; /* NOP */

    /* 最终 binary = prolog (64B) + original_text + rodata + data */
    long total = pn + g_text_n + g_ro_n + g_data_n;
    dbg_com1('[');  /* build_binary: g_malloc */
    dbg_com1_hex(total);
    char *bin = g_malloc((int)total);
    if (!bin) { dbg_com1('!'); dbg_com1('B'); g_panic("OOM in build_binary"); }
    dbg_com1(']');  /* g_malloc OK */
    dbg_com1('{');  /* g_memcpy 开始 */
    g_memcpy(bin, pro, pn);                        dbg_com1('t');
    if (g_text_n > 0 && g_text) { g_memcpy(bin+pn, g_text, g_text_n); } dbg_com1('r');
    if (g_ro_n > 0 && g_rodata) { g_memcpy(bin+pn+g_text_n, g_rodata, g_ro_n); } dbg_com1('d');
    if (g_data_n > 0 && g_data) { g_memcpy(bin+pn+g_text_n+g_ro_n, g_data, g_data_n); } dbg_com1('}');
    *out_size = total;
    return bin;
}

/* 写 .efs 头 12 字节 + binary (与 Makefile 生成规则/内核 run_efs 解析器一致):
 *   字节 0-7 : 加载地址 (64 位 LE, 内核只读低 32 位 → 字段 0-3 有效; 4-7 写 0)
 *   字节 8-11: 二进制大小 (32 位 BE)
 *   字节 12+ : 纯二进制 (入口 = 第 12 字节 = load_addr + 0)  */
static int write_efs(const char *path, long load_addr, const char *bin, long bsz) {
    char hdr[12];
    /* 0-7: load_addr LE (64-bit; 实际内核用前 4 字节 32-bit) */
    for (int i=0;i<8;i++) hdr[i]=(char)((load_addr>>(i*8))&0xFF);
    /* 8-11: bsz BE (32-bit, bin 部分大小, 不含头) */
    unsigned int bsz32 = (unsigned int)bsz;
    hdr[8]  = (char)((bsz32 >> 24) & 0xFF);
    hdr[9]  = (char)((bsz32 >> 16) & 0xFF);
    hdr[10] = (char)((bsz32 >> 8)  & 0xFF);
    hdr[11] = (char)( bsz32        & 0xFF);
    char *full = g_malloc((int)(12+bsz));
    g_memcpy(full, hdr, 12);
    g_memcpy(full+12, bin, (int)bsz);
    int r = API->file_write ? API->file_write(path, full, (int)(12+bsz)) : -1;
    g_free(full);
    return r;
}

static char g_src_path[512] IN_DATA = {0};
static char g_out_path[512] IN_DATA = {0};

static void parse_args(const char *argline) {
    g_src_path[0] = 0; g_out_path[0] = 0;
    const char *p = argline;
    while (*p) {
        while(*p==' '||*p=='\t'||*p=='\n') p++;
        if (!*p) break;
        if (p[0]=='-' && p[1]=='o') {
            p += 2;
            if (*p=='=') p++;
            while(*p==' ') p++;
            int i=0; while(*p && *p!=' ') { g_out_path[i++] = *p++; if(i>=510) break; }
            g_out_path[i]=0;
        } else if (p[0]=='-' && p[1]=='T') {
            /* -T 0xADDR: 输出 load_addr */
            p += 2; if (*p=='=') p++;
            while(*p==' ') p++;
            long v=0;
            if (p[0]=='0' && (p[1]=='x'||p[1]=='X')) { p+=2; for(;;) { int ch=*p; int d=-1;
                if (ch>='0'&&ch<='9') d=ch-'0'; else if (ch>='a'&&ch<='f') d=ch-'a'+10; else if (ch>='A'&&ch<='F') d=ch-'A'+10;
                if (d<0) break; v=v*16+d; p++; } }
            else { while(*p>='0'&&*p<='9') { v=v*10+(*p-'0'); p++; } }
            g_out_load_addr = v;
        } else if (p[0]=='-') {
            p++; while(*p && *p!=' ') p++; /* skip unknown opt */
        } else {
            int i=0; while(*p && *p!=' ') { g_src_path[i++] = *p++; if(i>=510) break; }
            g_src_path[i]=0;
        }
    }
    /* 默认输出: <src>.efs 或 out.efs */
    if (!g_out_path[0]) {
        int i=0; while(g_src_path[i] && i<500) { g_out_path[i]=g_src_path[i]; i++; }
        if (i>2 && g_out_path[i-2]=='.' && (g_out_path[i-1]=='c' || g_out_path[i-1]=='C')) {
            g_out_path[i-1]='e'; g_out_path[i++]='f'; g_out_path[i++]='s'; g_out_path[i]=0;
        } else { g_out_path[i++]='.'; g_out_path[i++]='e'; g_out_path[i++]='f'; g_out_path[i++]='s'; g_out_path[i]=0; }
    }
}

/* 主编译流程 */
static int do_compile(void) {
    /* 1. 初始化基础类型 */
    dbg_com1('1');  /* do_compile: init types */
    /* [COM1 trace fix] 清空 trace 位置列表 (每次编译独立) */
    g_trace_n = 0;
    /* [修复 #9] 清空精确重定位表 */
    g_reloc_n = 0;
    t_void = ty_new(TY_VOID, 0, 1);
    t_int  = ty_new(TY_INT,  4, 4);
    t_char = ty_new(TY_CHAR, 1, 1);
    t_long = ty_new(TY_LONG, 8, 8);
    t_float = ty_new(TY_FLOAT, 4, 4);
    t_double = ty_new(TY_DOUBLE, 8, 8);
    t_ldouble = ty_new(TY_LDOUBLE, 16, 16);
    g_scope_depth = 0;

    /* 先读源文件 */
    int filesz = 1024*512; /* 512KB 源文件上限 */
    char *src = g_malloc(filesz);
    dbg_com1('2');  /* do_compile: malloc done */
    if (!src) { g_print("OOM reading source\n"); return 1; }
    int rsz = API->file_read ? API->file_read(g_src_path, src, filesz-1) : -1;
    dbg_com1('3');  /* do_compile: file_read done */
    dbg_com1_hex((unsigned long)rsz);  /* 串口输出 rsz 的十六进制值 */
    if (rsz < 0) { g_print("efcc: cannot open "); g_print(g_src_path); g_print("\n"); return 2; }
    src[rsz] = 0;
    dbg_com1('R');  /* src[rsz]=0 done */

    /* 2. 初始化代码缓冲区 (先设置段 VA 布局占位, 之后回填)
     * 我们不知道 .text 大小, 但生成 builtin stubs / start stub 到 text 之前需要确定 VA.
     * 计算 VA: load_addr + start_prolog_size + 各段偏移. 我们先不固定; 我们把 va_text_base = load_addr,
     *   并且 build_binary() 会在 text 之前 prepend prolog (约 50B), 导致所有 label VA 不准.
     * 解决: prolog 大小固定 (扩容后含精细 trace + BSS test fix ≈ 197B, 预留 256B), 让 va_text_base = load_addr + 256,
     *   这样 text 中所有 label VA 对应 final binary 中 load_addr+356+X, 而 prolog 占 0..355 字节.
     *   ⚠️ 必须与 build_binary() 里的 prolog_reserve 保持一致, 否则所有 label VA 错位 → 立刻崩溃. */
    int prolog_reserve = 356;
    va_text_base = g_out_load_addr + prolog_reserve;
    /* 现在开始 generate builtin stubs */
    generate_builtin_stubs();
    generate_printf_stub();
    generate_libc_util_stubs();
    dbg_com1('4');  /* do_compile: stubs generated */

    /* 3. 词法 + 语法 / 代码生成 (先预处理, 再 L_init, 再循环 parse declarations) */
    /* [新增] 预处理: 展开 #define/#include/#ifdef, 输出到 pp_out */
    int pp_sz = 1024 * 1024;  /* 1MB 预处理输出 */
    char *pp_out = g_malloc(pp_sz);
    if (!pp_out) { g_print("OOM in preprocessor\n"); return 1; }
    int pp_len = pp_process(src, rsz, pp_out, pp_sz, 0);
    if (pp_len < 0) pp_len = 0;
    dbg_com1('p');  /* preprocessor done */
    /* 用预处理后的源替换原始源 */
    g_memcpy(src, pp_out, pp_len);
    src[pp_len] = 0;
    g_free(pp_out);
    rsz = pp_len;

    L_init(src, rsz);
    /* [诊断] 验证 src 缓冲区前几个字节内容 (hex) */
    dbg_com1('s'); dbg_com1_hex((unsigned char)src[0]);  /* 期望: 0x69 ('i') */
    dbg_com1_hex((unsigned char)src[1]);  /* 期望: 0x6E ('n') */
    dbg_com1_hex((unsigned char)src[2]);  /* 期望: 0x74 ('t') */
    dbg_com1('/');
    L_next();
    /* [诊断] 首次 L_next() 后 token 类型 */
    dbg_com1('t'); dbg_com1_hex((unsigned)L.tok);  /* 期望: T_INT 的 enum 值 */
    dbg_com1('5');  /* do_compile: lex init, entering parse loop */
    int loop_count = 0;
    while (!L_check(T_EOF)) {
        loop_count++;
        /* 定期 yield 防止鼠标/键盘冻结 (每 16 次循环) */
        if ((loop_count & 0xF) == 0 && API->yield) API->yield();
        /* [诊断] 循环内: 输出当前 token 类型和标识符 */
        dbg_com1('L'); dbg_com1_hex((unsigned)L.tok);
        if (L_check(T_IDENT)) {
            dbg_com1('I');
            /* 输出 sbuf 前4个字符的 hex */
            for(int _di=0; _di<4 && L.sbuf[_di]; _di++) dbg_com1_hex((unsigned char)L.sbuf[_di]);
        } else dbg_com1('.');
        if (L_check(T_STRUCT) || L_check(T_CLASS)) {
            int cls = L_check(T_CLASS);
            L_next();
            if (!L_check(T_IDENT)) g_panic("struct name");
            Type *st = struct_find(L.sbuf);
            if (!st) { st = ty_struct(L.sbuf); struct_register(L.sbuf, st); }
            char sname[128]; int i=0; while(L.sbuf[i]){sname[i]=L.sbuf[i];i++;} sname[i]=0;
            L_next();
            if (L_check(T_LBRACE)) {
                parse_struct_body(st);
                /* 注册类型别名 */
                sym_add(sname, SK_TYPE, st);
            }
            L_expect(T_SEMI,";");
            (void)cls;
            continue;
        }
        /* [新增] enum 顶层声明: enum Tag { ... }; */
        if (L_check(T_ENUM)) {
            L_next();
            if (L_check(T_IDENT)) {
                char ename[128]; int ei=0;
                while(L.sbuf[ei]&&ei<127){ename[ei]=L.sbuf[ei];ei++;}
                ename[ei]=0; L_next();
                if (L_check(T_LBRACE)) {
                    L_next();
                    int next_val = 0;
                    while (!L_check(T_RBRACE) && !L_check(T_EOF)) {
                        if (!L_check(T_IDENT)) { L_next(); continue; }
                        char cname[128]; int ci=0;
                        while(L.sbuf[ci]&&ci<127){cname[ci]=L.sbuf[ci];ci++;}
                        cname[ci]=0; L_next();
                        if (L_accept(T_ASSIGN)) {
                            if (L_check(T_MINUS)) { L_next(); next_val = -L.num; L_next(); }
                            else if (L_check(T_IDENT)) {
                                Symbol *cs = sym_find_global(L.sbuf); next_val = cs ? (int)cs->addr : 0; L_next();
                            } else { next_val = L.num; L_next(); }
                        }
                        Symbol *es = sym_add(cname, SK_CONST, t_int);
                        es->addr = next_val;
                        next_val++;
                        if (L_accept(T_COMMA)) continue;
                    }
                    L_expect(T_RBRACE, "}");
                }
                sym_add(ename, SK_TYPE, t_int);
            }
            /* 跳过可选的变量声明 (简化: enum Tag var; → var 声明) */
            if (L_accept(T_SEMI)) continue;
            /* 否则退化为普通声明 — 先尝试函数, 失败则变量 */
            { int pfr = parse_func_definition(0); if (!pfr) { int a=0,b=0; parse_declaration(&a,&b,0); } }
            continue;
        }
        /* [新增] union 顶层声明: 简化为 struct */
        if (L_check(T_UNION)) {
            L_next();
            if (L_check(T_IDENT)) {
                char uname[128]; int ui=0;
                while(L.sbuf[ui]&&ui<127){uname[ui]=L.sbuf[ui];ui++;}
                uname[ui]=0; L_next();
                if (L_check(T_LBRACE)) {
                    Type *st = struct_find(uname);
                    if (!st) { st = ty_struct(uname); struct_register(uname, st); st->size=0; }
                    parse_struct_body(st);
                    sym_add(uname, SK_TYPE, st);
                }
            }
            L_accept(T_SEMI);
            continue;
        }
        /* [新增] _Static_assert(cond, "msg"); — 编译期断言 (顶层) */
        if (L_check(T_STATIC_ASSERT)) {
            parse_static_assert();
            continue;
        }
        /* [新增] _Alignas(type-or-expr) 前缀: 跳过后回到循环顶部继续解析声明
         * (本简化编译器不实现自定义对齐, 仅语法上接受) */
        if (L_check(T_ALIGNAS)) {
            L_next();
            L_expect(T_LPAREN, "(");
            int d = 1;
            while (d > 0 && !L_check(T_EOF)) {
                if (L_check(T_LPAREN)) d++;
                else if (L_check(T_RPAREN)) { d--; if (d==0) break; }
                L_next();
            }
            L_expect(T_RPAREN, ")");
            continue;
        }
        /* [新增] 存储类前缀: const/static/extern/volatile → 跳过后按普通声明处理 */
        if (L_check(T_CONST)||L_check(T_STATIC)||L_check(T_EXTERN)||L_check(T_VOLATILE)||L_check(T_REGISTER)) {
            /* 交给 parse_func_definition → 若识别为变量语法 (返回 0) 则回退到 parse_declaration */
            int pfr = parse_func_definition(0);
            if (!pfr) { int a=0,b=0; parse_declaration(&a,&b,0); }
            dbg_com1('D');
            continue;
        }
        if (L_check(T_TYPEDEF)) {
            int a=0,b=0; parse_declaration(&a,&b,0);
            continue;
        }
        /* 其他: 函数定义/全局变量/前向声明 — 先 parse_func_definition.
         * 若它返回 0 → 不是函数语法 (lexer 已完整回滚), 交给 parse_declaration 处理全局变量. */
        {
            int pfr = parse_func_definition(0);
            if (!pfr) { int a=0,b=0; parse_declaration(&a,&b,0); }
        }
        dbg_com1('D');  /* parse_func_definition done */
    }
    dbg_com1('N'); dbg_com1_hex((unsigned)loop_count);  /* 循环次数 */

    /* 4. Patch rel32 (所有 forward refs) */
    patch_all();

    /* 5. 组装段 → build binary → prepend prolog → 写 .efs
     *   注意: text 区内部符号 VA = va_text_base + offset,
     *   但 rodata/data/bss 的 VA 还没设. 现在设置: */
    va_text_sz = g_text_n;
    va_ro_base = va_text_base + va_text_sz;
    va_ro_sz  = g_ro_n;
    va_data_base = va_ro_base + va_ro_sz;
    va_data_sz = g_data_n;
    va_bss_base = va_data_base + va_data_sz;
    /* 修正: 上面生成 builtins 时, rodata_puts 用了 va_ro_base=0 (当时未设置). 现在把所有用到的 rodata 绝对地址重新修正.
     * Unfortunately 字符串常量的地址已经硬编码成 imm64 在 text 里. 解决: 我们要求字符串常量的 VA = final binary 内偏移,
     *   并且把 va_ro_base 设置为 va_text_base + text_sz (我们已经设的).
     *   但字符串常量在代码生成时 va_ro_base=0, 它们的 VA 是 0+offset.
     *   → 需要二次扫描 text, 找到所有 movabs rax, imm64 (48 B8) 且 imm64 在 [0, g_ro_n) 的 → 加 va_ro_base
     *   → 同时判断 data 区域的全局变量地址: 符号 s->addr 在 [0, g_data_n) → 加 va_data_base
     *   → 以及 BSS 符号的 s->addr 在 [0, g_bss_len) → 加 va_bss_base
     * 这一步关键: 我们没在 emit 时知道最终 section VA, 所以现在修正所有 text 中的 imm64。 */
    {
        /* 修正全局符号地址 (data/bss). 先对符号做: */
        for(int i=0;i<g_syms_n;i++){
            Symbol *s = &g_syms[i];
            if (s->kind != SK_VAR || !s->is_global) continue;
            /* 判断是哪段: addr < data_sz → data; bss 起始 > data */
            if (s->addr >= 0 && s->addr < (long)g_data_n) {
                /* 实际上 data 段符号: addr  = va_data_base + off (我们写的时候用了 va_data_base 吗?
                 * 变量声明时 v->addr = va_data_base + g_data_n, 而 va_data_base 我们设置过 (之前=0).
                 * 之前全局变量写 v->addr = va_data_base + g_data_n. va_data_base 当时还是 0!
                 * → 所以 v->addr = offset in data section (0..g_data_n). 现在把所有符号 VA 改为 VA_final. */
                s->addr += va_data_base;
            } else {
                /* BSS: v->addr 写的是 va_bss_base + offset, 而 va_bss_base=0.
                 * 上面写: v->addr = va_bss_base + g_bss_len(已累加) → offset 是 bss offset。
                 * 不对, 我写 v->addr = va_bss_base + g_bss_len, va_bss_base 当时=0, 所以 addr=bss_offset。 */
                s->addr += va_bss_base;
            }
        }
        /* [关键修复 #9 精确重定位 (替代字节滑窗扫描)]
         * 旧实现: for(i=0;i<g_text_n-10;) byte-slide 找 0x48 0xBx → 致命缺陷:
         *   0x48 可能出现在前一条指令的 imm32/displacement 数据里 (不是 REX 前缀!),
         *   只要跟着 0xB8~0xBF (包括指令 opcode C3 / B1 的延续字节) 就会误匹配,
         *   读接下来 8 字节 → 跨指令边界拼出"imm64" = 随机垃圾, i+=10 推进错位 7 字节,
         *   后续真实 movabs 被漏过 → rodata/data 地址不重定位 → 低地址访问崩溃。
         *
         * 新实现: 每次 EmitMoviImm64 在写入字节之前, 先把 REX.W 字节的 text 偏移
         *   精确登记到 g_reloc_entries[]。exit stub 手动 emit 的 movabs 也手动登记.
         *   这样重定位循环只遍历这些"确认是 movabs reg64 imm64 开头"的位置,
         *   100% 精准匹配、不会漏、不会假阳性误匹配跨边界字节!
         *
         * [imm=0 冲突修复] NULL literal 0 的 movabs 和 BSS offset 0 符号地址的 movabs 都是 imm=0,
         *   但 [0,g_bss_len) 范围判断会无条件把 0 当 BSS 偏移加 base → NULL 被改成非零 → *NULL deref crash.
         *   解决: imm==0 进入 BSS 分支时, 扫描 g_syms[] 确认确实存在某个 BSS 变量 (kind=SK_VAR, defined=1,
         *   is_global=1, has_init=0) 在修正前 addr==0. 如果找不到这样的符号, 跳过 (是纯 literal 0). */
        dbg_com1('{');  /* 精确 reloc 开始 (legacy) */
        com1_raw('{');  /* 精确 reloc 开始 (raw, 不受 g_debug) */
        com1_dec(g_reloc_n); com1_raw(':'); /* 先打印条目数, 避免空表 */
        /* 预扫描: 是否存在 BSS offset 0 符号? (addr 在修正前就是 0 的 BSS 变量) */
        int has_bss_sym_at_0 = 0;
        {
            /* 注意: 到这里 s->addr 已经加过 va_bss_base, 无法直接得到修正前 offset.
             * 我们反向判断: 对每个 BSS 全局变量, s->addr - va_bss_base == 0? 以及 g_bss_len>0?
             * 只要有任一满足, has_bss_sym_at_0 = 1. */
            for(int si=0; si<g_syms_n; si++) {
                Symbol *sb = &g_syms[si];
                if (sb->kind==SK_VAR && sb->is_global && sb->defined
                    && va_bss_base != 0
                    && sb->addr >= va_bss_base && sb->addr < va_bss_base + g_bss_len
                    && (sb->addr - va_bss_base) == 0) {
                    has_bss_sym_at_0 = 1; break;
                }
            }
        }
        for(int r=0;r<g_reloc_n;r++) {
            int enc = g_reloc_entries[r];
            char tag = (char)((enc >> 24) & 0xFF);
            int rex_off = enc & 0x00FFFFFF;
            /* 防御性: 确保 rex_off 在有效范围, 且后 9 字节仍在 buffer 内 */
            if (rex_off < 0 || rex_off + 10 > g_text_n) continue;
            /* 读 imm64 = g_text[rex_off+2 ... rex_off+9] */
            long imm=0; for(int k=0;k<8;k++) imm|= ((long)(unsigned char)g_text[rex_off+2+k]) << (k*8);
            /* 诊断: tag + "[" + pos4hex + "]" + imm16hex + " " */
            {
                char buf[32]; int bl=0;
                buf[bl++]=tag? tag : '?';   /* 输出来源标签字符 */
                buf[bl++]='[';
                for(int h=12;h>=0;h-=4) { char c=(rex_off>>h)&0xF; buf[bl++]=(c<10)?'0'+c:'A'+c-10; }
                buf[bl++]=']';
                unsigned long uimm = (unsigned long)imm;
                for(int h=60;h>=0;h-=4) { char c=(uimm>>h)&0xF; buf[bl++]=(c<10)?'0'+c:'A'+c-10; }
                buf[bl++]=' ';
                for(int p=0;p<bl;p++) { unsigned char ch=(unsigned char)buf[p];
                    __asm__ __volatile__("outb %%al,%%dx"::"a"(ch),"d"(0x3F8));
                }
            }
            int mod = 0;
            if (imm >= 0 && imm < g_ro_n) { imm += va_ro_base; mod=1; }
            if (imm >= 0 && imm < g_data_n) { imm += va_data_base; mod=1; }
            if (imm >= 0 && imm < g_bss_len) {
                /* [imm=0 冲突保护] */
                if (imm == 0 && !has_bss_sym_at_0) {
                    /* literal 0 (NULL/整数零), 不要当 BSS offset 0 处理 */
                } else {
                    imm += va_bss_base; mod=1;
                }
            }
            /* 写回 (精确表保证不会误修改, 恢复正常重定位写回) */
            if (mod) for(int k=0;k<8;k++) g_text[rex_off+2+k]=(char)((imm>>(k*8))&0xFF);
            /* 诊断: '+' = 已写回修改, '-' = 未修改 (已是最终值, 如 api slot)
             *       '!' = imm==0 被保护跳过 (新诊断位) */
            {
                char ch = mod? '+' : ((imm==0 && g_bss_len>0 && !has_bss_sym_at_0)? '!' : '-');
                __asm__ __volatile__("outb %%al,%%dx"::"a"((unsigned char)ch),"d"(0x3F8));
            }
        }
        dbg_com1('}');  /* 精确 reloc 结束 (legacy) */
        com1_raw('}'); com1_raw('\n');  /* 精确 reloc 结束 (raw, 不受 g_debug) */
    }

    /* ============================================================================
     * [诊断 V-W 版本] exit stub imm64 slot 地址 hex dump (编译期 → COM1).
     * 输出:
     *   '!'  + dbg_com1_hex(g_exit_frame_slot)   ← 预期正确的 slot VA
     *   '@'  + dbg_com1_hex(text_encoded_slot)  ← text[] 中 exit stub movabs imm64 实际编码的 8 字节拼出来的值
     *   '#'  + dbg_com1_hex(exit_text_off)      ← exit stub text offset; 0xFFFFFFFF 表示找不到
     * 如果三者正常, 预期: ! slot @ same_as_slot # small_offset
     * 如果 relocation 弄坏了 → @ 会是垃圾值 (像 0x9010B8485E) → 直接证明 imm64 被错位覆写.
     * ============================================================================ */
    {
        dbg_com1('!');
        dbg_com1_hex((unsigned long)g_exit_frame_slot);
        long exit_text_off = -1;
        for(int si=0;si<g_syms_n;si++) {
            if (g_syms[si].kind==SK_FUNC && g_strcmp(g_syms[si].name,"exit")==0) {
                exit_text_off = g_syms[si].addr - va_text_base;
                break;
            }
        }
        unsigned long encoded = 0xDEADBEEFCAFEBABEUL;
        if (exit_text_off >= 0 && exit_text_off + 17 <= g_text_n) {
            encoded = 0;
            for(int k=0;k<8;k++) {
                unsigned char cb = (unsigned char)g_text[exit_text_off + 9 + k];
                encoded |= ((unsigned long)cb) << (k*8);
            }
        }
        dbg_com1('@');
        dbg_com1_hex(encoded);
        dbg_com1('#');
        dbg_com1_hex((unsigned long)exit_text_off);
    }

    /* 6. Build binary */
    dbg_com1('6');  /* do_compile: build_binary */
    long bsz;
    char *bin = BuildBinary(&bsz);
    dbg_com1('7');  /* do_compile: write_efs */

    /* 7. 写 .efs (12 字节头 + bin). prolog_reserve=64, 所以 binary 的实际入口 load_addr + 0 → prolog
     *   且 prolog 里 main_addr 被我们在 build_binary 里填成 mn->addr (已经是 va_text_base+X → 正确 final VA). OK。 */
    int wr = write_efs(g_out_path, g_out_load_addr, bin, bsz);

    g_print("efcc: compiled "); g_print(g_src_path); g_print(" -> "); g_print(g_out_path);
    g_print(" ("); g_print_int((int)bsz); g_print(" bytes)\n");

    g_free(bin); g_free(src);
    if (wr < 0) { g_print("efcc: write failed\n"); return 3; }
    return 0;
}

/* ========== efcc.efs 入口函数实现 ==========
 * [used] 防止 GCC -O2 dead code elimination 删除此函数定义。
 *   efcc_main 只被 _start 的 naked 内联汇编 call 引用, GCC 不识别该引用,
 *   会把 efcc_main 定义消除 → ld: undefined reference to efcc_main。
 * [返回 int] _start 把 rax 当退出码传给内核 trampoline。 */
__attribute__((used))
int efcc_main(void) {
    dbg_com1('M');  /* efcc_main entered */
    struct kernel_api *api = API;
    if (api->magic != 0xEF110001) { dbg_com1('!'); return 1; }
    dbg_com1('A');  /* API magic OK */

    /* 参数从 get_args API 读 (或退化为 prompt: 若 args 为空, 让用户输入一行命令) */
    char args[1024]; args[0]=0;
    if (api->get_args) api->get_args(args, sizeof(args)-1);
    dbg_com1('G');  /* get_args done */
    if (!args[0]) {
        api->print("Usage: efcc <src.c|.cpp> [-o OUT.efs] [-T load_addr_hex]\n");
        api->print("Input command line: ");
        if (api->readline) api->readline(args, sizeof(args)-1);
    }
    dbg_com1('P');  /* parse_args about to call */
    parse_args(args);
    if (!g_src_path[0]) { api->print("no input file\n"); dbg_com1('N'); return 4; }
    dbg_com1('D');  /* do_compile about to call */
    int rc = do_compile();
    dbg_com1('F');  /* do_compile finished */
    api->print("efcc done (rc=");
    char nb[8]; nb[0]='0'+rc; nb[1]=0; api->print(nb); api->print(")\n");
    return rc;
}

