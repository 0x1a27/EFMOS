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

/* efmshell.c - EFMOS 独立 Shell (编译为 efmshell.efs)
 * 加载地址: 0xA00000 (10MB, 不与内核/其他 EFS 程序冲突)
 * 入口: _start (二进制首字节, efm_flat.ld + .text.start 保证)
 *
 * [shell 独立化 · 全功能移植] 原 kernel.c 内建 shell (kernelold.c) 的全部功能
 * 迁出为独立 EFS 程序, 通过 kernel API (0x9000) 完成所有系统操作:
 *   - 增强 readline_ext: 历史(Up/Down)/行内编辑(Left/Right/Home/End/Del)/
 *     Tab 补全/Esc 清行/闪烁光标 (内核实现, 回调注入历史与补全源)
 *   - read_text: 多行文本编辑器 (write/append/edit, ENDOFFILE 结束, Esc 取消)
 *   - fsop: ln/readlink/truncate/rmdir/unlink/sync/append/shutdown +
 *           pwd/date/sysinfo/memfree/df/stat/tree/find/du/ps/wminfo/memls/memread
 *   - 文件: file_read/file_write/file_exists/file_list/file_delete/mkdir
 *   - 进程: spawn (同步) / spawn_async (start 命令 & 桌面模式)
 *   - CWD:  chdir (kernel 同步 current_dir_ino, fsop 相对名操作依赖它)
 */

/* ========== 内核 API 表 (与内核 struct kernel_api 严格匹配) ========== */
#include "efmos/efm_api.h"

/* ========== Logo 像素数据 (gen_logo.py 生成, 128x128, RLE) ========== */
#include "logo.h"

/* ========== freestanding 小工具 ========== */
static int sh_strlen(const char *s) { int n=0; while (s && s[n]) n++; return n; }
static void sh_strcpy(char *d, const char *s) { int i=0; while (s && s[i]) { d[i]=s[i]; i++; } d[i]=0; }
static void sh_strncpy(char *d, const char *s, int n) { int i=0; while (i<n && s && s[i]) { d[i]=s[i]; i++; } d[i]=0; }
static int sh_strcmp(const char *a, const char *b) {
    int i=0; while (a[i] && b[i] && a[i]==b[i]) i++; return (unsigned char)a[i]-(unsigned char)b[i];
}
static int sh_strncmp(const char *a, const char *b, int n) {
    for (int i=0;i<n;i++) { if (a[i]!=b[i] || !a[i] || !b[i]) return (unsigned char)a[i]-(unsigned char)b[i]; }
    return 0;
}
static void sh_strcat(char *d, const char *s) { int n=sh_strlen(d); int i=0; while (s && s[i]) d[n++]=s[i++]; d[n]=0; }
static void sh_memcpy(char *d, const char *s, int n) { for (int i=0;i<n;i++) d[i]=s[i]; }
static void sh_puts(const char *s) { if (API->print) API->print(s); }
static void sh_putc(char c) { if (API->put_char) API->put_char(c); }
static void sh_dec(unsigned int v) {
    char b[16]; int n=0;
    if (!v) { sh_putc('0'); return; }
    while (v) { b[n++]='0'+v%10; v/=10; }
    while (n--) sh_putc(b[n]);
}
static void sh_hex(unsigned int v) {
    char b[16]; int n=0;
    if (!v) { sh_putc('0'); return; }
    while (v) { unsigned d=v&0xF; b[n++]=(d<10)?('0'+d):('A'+d-10); v>>=4; }
    while (n--) sh_putc(b[n]);
}
static int sh_atoi(const char *s) { int v=0; while (*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;} return v; }
static int sh_strchr(const char *s, char c) { for (int i=0; s[i]; i++) if (s[i]==c) return i; return -1; }
static int starts_with(const char *s, const char *prefix) {
    while (*prefix) if (*s++ != *prefix++) return 0;
    return 1;
}
/* 多参数解析 (支持双引号), 就地修改 line */
static int parse_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') { *p = 0; p++; }
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) { *p = 0; p++; }
        }
    }
    return argc;
}

/* 双语: 跟随内核语言设置 (0=英文, 1=中文)
 * [注意] EFS 加载只搬文件 (.text/.rodata/.data), 不含 .bss →
 *        全局必须强制放 .data (显式 section 属性), 否则读到内存垃圾. */
__attribute__((section(".data"))) static int g_lang = 0;
#define TR(en, zh) (g_lang ? (zh) : (en))

/* ========== shell 状态 (全部 .data, 不落 .bss) ========== */
__attribute__((section(".data"))) static char g_cwd[256] = "/";      /* shell 视角 cwd (与内核 chdir 同步) */
__attribute__((section(".data"))) static char g_opbuf[8192];         /* fsop 文本输出缓冲 */
__attribute__((section(".data"))) static char g_textbuf[8192];       /* 文件内容/read_text 缓冲 */

/* fsop 便捷包装: 文本 op 结果直接打印 */
static int fsop_print(const char *op, const char *a, const char *b) {
    if (!API->fsop) return -1;
    int r = API->fsop(op, a, b, g_opbuf, (int)sizeof(g_opbuf));
    if (r >= 0 && g_opbuf[0]) sh_puts(g_opbuf);
    return r;
}

/* 路径规范化: 处理 "." / ".." 分段, 输出绝对路径到 out.
 * base = 当前 cwd, arg = 用户输入 (绝对或相对). */
static void sh_normalize(const char *arg, char *out, int outsz) {
    char tmp[512];
    if (arg && arg[0] == '/') sh_strcpy(tmp, arg);
    else { sh_strcpy(tmp, g_cwd); if (arg && arg[0]) { if (tmp[sh_strlen(tmp)-1] != '/') sh_strcat(tmp, "/"); sh_strcat(tmp, arg); } }
    int oi = 0;
    out[0] = '/'; oi = 1;
    int i = 0;
    while (tmp[i]) {
        while (tmp[i] == '/') i++;
        int st = i;
        while (tmp[i] && tmp[i] != '/') i++;
        int len = i - st;
        if (len <= 0) continue;
        if (len == 1 && tmp[st] == '.') continue;
        if (len == 2 && tmp[st] == '.' && tmp[st+1] == '.') {
            if (oi > 1) { oi--; while (oi > 1 && out[oi-1] != '/') oi--; }
            continue;
        }
        if (oi + len + 2 >= outsz) break;
        for (int k = 0; k < len; k++) out[oi++] = tmp[st+k];
        out[oi++] = '/';
    }
    if (oi > 1) oi--;
    out[oi] = 0;
}

/* 统一文件读取 (原 kernel load_file 移植):
 * mem_mode=0: ext4 (规范化路径)   mem_mode=1: 内存 FS (fsop memread)
 * 返回: >=0 大小, -1 未找到, -2 不是普通文件 */
static int load_file(const char *arg, int mem_mode, char *buf, int bufsz) {
    if (!arg || !arg[0]) return -1;
    if (mem_mode) {
        if (!API->fsop) return -1;
        return API->fsop("memread", arg, 0, buf, bufsz);
    }
    char path[256]; sh_normalize(arg, path, sizeof(path));
    int n = API->file_read(path, buf, bufsz);
    return n;   /* efs_file_read: -1 未找到 */
}

/* ========== 内建命令 (原 kernelold.c cmd_* 家族完整移植) ========== */
static void cmd_help(const char *arg) {
    if (!arg || !arg[0]) {
        sh_puts(TR("== EFMOS Shell Commands ==\n", "== EFMOS Shell 命令列表 ==\n"));
        sh_puts("Usage: help <command>");
        sh_puts(TR("  for detailed format & examples\n\n", "  查看详细用法与示例\n\n"));
        sh_puts(TR("-- Disk filesystem (ext4, persistent)\n", "-- 磁盘文件系统 (ext4, 持久化)\n"));
        sh_puts("  ls [-l]                   cd <dir>              pwd\n");
        sh_puts("  cat <file>                stat <file>            hexdump <file>\n");
        sh_puts("  head <file> [n]           tail <file> [n]       wc <file>\n");
        sh_puts("  tree                      du                    find <name>\n");
        sh_puts("  mkdir <dir>               touch <file>          write <file>\n");
        sh_puts("  append <file>             edit <file>           rm <file>\n");
        sh_puts("  rmdir <dir>               cp <src> <dst>        mv <src> <dst>\n");
        sh_puts("  ln <target> <link>        readlink <link>       truncate <file> <size>\n");
        sh_puts("  sync                      df\n");
        sh_puts(TR("-- Memory FS (volatile, prefix 'mem')\n", "-- 内存文件系统 (临时, 使用 'mem' 前缀)\n"));
        sh_puts("  mem ls | cat <file> | stat | hexdump <file>\n");
        sh_puts("  mem head <file> [n] | tail <file> [n] | wc <file>\n");
        sh_puts(TR("-- System & misc\n", "-- 系统与其他\n"));
        sh_puts("  echo <text>               clear | cls           ver / info\n");
        sh_puts("  free                      date                  reboot\n");
        sh_puts("  shutdown                  help [cmd]            history            exec <program>\n");
        sh_puts(TR("-- Window / multitasking\n", "-- 窗口 / 多任务\n"));
        sh_puts("  start <program> [args]    ps                    wminfo\n");
        sh_puts(TR("-- Programs (.efs files in /EFMOS, no exec prefix needed)\n",
                 "-- 应用程序 (/EFMOS 下的 .efs 文件, 无需 exec 前缀)\n"));
        sh_puts(TR("  e.g. just type: setting  ->  runs /EFMOS/setting.efs automatically\n",
                   "  例: 直接输入: setting  ->  自动运行 /EFMOS/setting.efs\n"));
        sh_puts(TR("-- Keyboard\n", "-- 键盘操作\n"));
        sh_puts(TR("  Up/Down arrows   = recall previous commands (command history)\n",
                   "  方向键上/下    = 调用之前的命令 (命令历史)\n"));
        sh_puts(TR("  Left/Right       = move cursor within input line\n",
                   "  方向键左/右    = 移动输入行内光标\n"));
        sh_puts(TR("  Home/End         = jump to start/end of line\n",
                   "  Home/End       = 跳到行首/行尾\n"));
        sh_puts(TR("  Backspace        = delete char before cursor\n",
                   "  退格键         = 删除光标前的字符\n"));
        sh_puts(TR("  Delete           = delete char under cursor\n",
                   "  Delete 键     = 删除光标下的字符\n"));
        sh_puts(TR("  Tab              = auto complete filename/command\n",
                   "  Tab 键         = 自动补全文件名/命令\n"));
        sh_puts(TR("  Esc              = clear current input line\n",
                   "  Esc 键         = 清空当前输入行\n"));
        sh_puts(TR("-- Text editor end-of-input marker (write / append / edit)\n",
                   "-- 文本编辑器结束标记 (用于 write / append / edit)\n"));
        sh_puts(TR("  End input by typing ENDOFFILE alone on a line\n",
                   "  在一行上单独输入 ENDOFFILE 结束输入\n"));
        return;
    }
    /* 逐命令简明帮助 (用法行保留英文, 描述双语) */
    if      (sh_strcmp(arg, "ls") == 0) {
        sh_puts("ls [-l] - "); sh_puts(TR("List directory contents\n", "列出目录内容\n"));
        sh_puts(TR("  Use 'mem ls' to list memory filesystem files.\n", "  使用 'mem ls' 列出内存文件系统文件。\n"));
    } else if (sh_strcmp(arg, "cd") == 0) {
        sh_puts("cd <dir> - "); sh_puts(TR("Change current directory\n", "切换当前目录\n"));
        sh_puts(TR("  Supports absolute (/path) and relative paths.\n", "  支持绝对路径 (/path) 与相对路径。\n"));
    } else if (sh_strcmp(arg, "pwd") == 0) {
        sh_puts("pwd - "); sh_puts(TR("Print working directory\n", "显示当前工作目录\n"));
    } else if (sh_strcmp(arg, "cat") == 0) {
        sh_puts("cat <file> - "); sh_puts(TR("Display file contents\n", "显示文件内容\n"));
    } else if (sh_strcmp(arg, "write") == 0) {
        sh_puts("write <file> - "); sh_puts(TR("Write text to file (ENDOFFILE to finish, Esc cancel)\n", "写入文本到文件 (ENDOFFILE 结束, Esc 取消)\n"));
    } else if (sh_strcmp(arg, "append") == 0) {
        sh_puts("append <file> - "); sh_puts(TR("Append text to file (ENDOFFILE to finish, Esc cancel)\n", "追加文本到文件 (ENDOFFILE 结束, Esc 取消)\n"));
    } else if (sh_strcmp(arg, "edit") == 0) {
        sh_puts("edit <file> - "); sh_puts(TR("Edit file, overwrite on save (ENDOFFILE to finish)\n", "编辑文件, 保存时覆盖 (ENDOFFILE 结束)\n"));
    } else if (sh_strcmp(arg, "mkdir") == 0) {
        sh_puts("mkdir <dir> - "); sh_puts(TR("Create directory\n", "创建目录\n"));
    } else if (sh_strcmp(arg, "touch") == 0) {
        sh_puts("touch <file> - "); sh_puts(TR("Create empty file\n", "创建空文件\n"));
    } else if (sh_strcmp(arg, "rm") == 0) {
        sh_puts("rm <file> - "); sh_puts(TR("Delete file\n", "删除文件\n"));
    } else if (sh_strcmp(arg, "rmdir") == 0) {
        sh_puts("rmdir <dir> - "); sh_puts(TR("Delete empty directory\n", "删除空目录\n"));
    } else if (sh_strcmp(arg, "cp") == 0) {
        sh_puts("cp <src> <dst> - "); sh_puts(TR("Copy file\n", "复制文件\n"));
    } else if (sh_strcmp(arg, "mv") == 0) {
        sh_puts("mv <src> <dst> - "); sh_puts(TR("Move/rename file\n", "移动/重命名文件\n"));
    } else if (sh_strcmp(arg, "ln") == 0) {
        sh_puts("ln <target> <link> - "); sh_puts(TR("Create symbolic link\n", "创建符号链接\n"));
    } else if (sh_strcmp(arg, "readlink") == 0) {
        sh_puts("readlink <link> - "); sh_puts(TR("Show symlink target\n", "显示符号链接目标\n"));
    } else if (sh_strcmp(arg, "truncate") == 0) {
        sh_puts("truncate <file> <size> - "); sh_puts(TR("Truncate/extend file to size bytes\n", "截断/扩展文件到指定字节\n"));
    } else if (sh_strcmp(arg, "stat") == 0) {
        sh_puts("stat <file> - "); sh_puts(TR("Show file info (inode/mode/size)\n", "显示文件信息 (inode/模式/大小)\n"));
    } else if (sh_strcmp(arg, "hexdump") == 0) {
        sh_puts("hexdump <file> - "); sh_puts(TR("Hexadecimal dump (first 8KB)\n", "十六进制转储 (前 8KB)\n"));
    } else if (sh_strcmp(arg, "head") == 0) {
        sh_puts("head <file> [n] - "); sh_puts(TR("Show first n lines (default 10)\n", "显示前 n 行 (默认 10)\n"));
    } else if (sh_strcmp(arg, "tail") == 0) {
        sh_puts("tail <file> [n] - "); sh_puts(TR("Show last n lines (default 10)\n", "显示后 n 行 (默认 10)\n"));
    } else if (sh_strcmp(arg, "wc") == 0) {
        sh_puts("wc <file> - "); sh_puts(TR("Count lines/words/bytes\n", "统计行数/词数/字节数\n"));
    } else if (sh_strcmp(arg, "tree") == 0) {
        sh_puts("tree - "); sh_puts(TR("Show directory tree recursively\n", "递归显示目录树\n"));
    } else if (sh_strcmp(arg, "find") == 0) {
        sh_puts("find <name> - "); sh_puts(TR("Search file in directory subtree\n", "在目录子树中查找文件\n"));
    } else if (sh_strcmp(arg, "du") == 0) {
        sh_puts("du - "); sh_puts(TR("Directory usage in bytes\n", "目录占用字节数\n"));
    } else if (sh_strcmp(arg, "df") == 0) {
        sh_puts("df - "); sh_puts(TR("Filesystem free space info\n", "文件系统空闲信息\n"));
    } else if (sh_strcmp(arg, "sync") == 0) {
        sh_puts("sync - "); sh_puts(TR("Flush disk write cache\n", "刷新磁盘写缓存\n"));
    } else if (sh_strcmp(arg, "echo") == 0) {
        sh_puts("echo <text> - "); sh_puts(TR("Print text\n", "输出文本\n"));
    } else if (sh_strcmp(arg, "clear") == 0 || sh_strcmp(arg, "cls") == 0) {
        sh_puts("clear - "); sh_puts(TR("Clear screen\n", "清屏\n"));
    } else if (sh_strcmp(arg, "ver") == 0) {
        sh_puts("ver - "); sh_puts(TR("Show shell/kernel version\n", "显示 shell/内核版本\n"));
    } else if (sh_strcmp(arg, "info") == 0) {
        sh_puts("info - "); sh_puts(TR("Show system info\n", "显示系统信息\n"));
    } else if (sh_strcmp(arg, "free") == 0) {
        sh_puts("free - "); sh_puts(TR("Show memory FS usage\n", "显示内存 FS 使用量\n"));
    } else if (sh_strcmp(arg, "date") == 0) {
        sh_puts("date - "); sh_puts(TR("Show current date/time (RTC)\n", "显示当前日期/时间 (RTC)\n"));
    } else if (sh_strcmp(arg, "reboot") == 0) {
        sh_puts("reboot - "); sh_puts(TR("Reboot system\n", "重启系统\n"));
    } else if (sh_strcmp(arg, "shutdown") == 0) {
        sh_puts("shutdown - "); sh_puts(TR("Power off (ACPI)\n", "关机 (ACPI)\n"));
    } else if (sh_strcmp(arg, "history") == 0) {
        sh_puts("history - "); sh_puts(TR("Show command history\n", "显示命令历史\n"));
    } else if (sh_strcmp(arg, "exec") == 0) {
        sh_puts("exec <program> - "); sh_puts(TR("Run .efs program (CWD first)\n", "运行 .efs 程序 (优先当前目录)\n"));
    } else if (sh_strcmp(arg, "start") == 0) {
        sh_puts("start <program> [args] - "); sh_puts(TR("Start program in new window (async)\n", "在新窗口中启动程序 (异步)\n"));
    } else if (sh_strcmp(arg, "ps") == 0) {
        sh_puts("ps - "); sh_puts(TR("List all tasks (pid/state/nice/name)\n", "列出所有任务 (pid/状态/优先级/名称)\n"));
    } else if (sh_strcmp(arg, "wminfo") == 0) {
        sh_puts("wminfo - "); sh_puts(TR("Show window manager status\n", "显示窗口管理器状态\n"));
    } else if (sh_strcmp(arg, "mem") == 0) {
        sh_puts(TR("mem <cmd> - prefix to operate on memory FS\n", "mem <命令> - 前缀, 操作内存文件系统\n"));
        sh_puts("  mem ls | cat <f> | stat <f> | hexdump <f> | head | tail | wc\n");
    } else {
        sh_puts(TR("No help for: ", "无帮助: ")); sh_puts(arg); sh_putc('\n');
    }
}

static void cmd_ls(const char *arg, int mem_mode) {
    int long_fmt = 0;
    if (arg && arg[0] == '-' && sh_strchr(arg, 'l') >= 0) long_fmt = 1;

    if (mem_mode) {
        fsop_print("memls", 0, 0);
        return;
    }
    /* ext4: 默认当前目录, 支持路径参数 (旧版仅当前目录, 此为增强) */
    char path[256];
    sh_normalize(arg && arg[0] && arg[0] != '-' ? arg : 0, path, sizeof(path));
    struct { struct efs_dirent e[64]; } ents;   /* 栈缓冲 ~4.6KB (EFS 栈 1MB) */
    int n = API->file_list(path, ents.e, 64);
    if (n < 0) {
        sh_puts(TR("ls: cannot list directory: ", "ls: 无法列出目录: ")); sh_puts(path); sh_putc('\n');
        return;
    }
    sh_puts(TR("== ext4 ", "== ext4 ")); sh_puts(path); sh_puts(TR(" ==\n", " ==\n"));
    for (int i = 0; i < n; i++) {
        if (long_fmt) {
            sh_puts(ents.e[i].is_dir ? "drw  " : "frw  ");
            sh_dec(ents.e[i].size); sh_puts("  ");
        }
        sh_putc(ents.e[i].is_dir ? '[' : ' ');
        sh_puts(ents.e[i].name);
        sh_putc(ents.e[i].is_dir ? ']' : ' ');
        sh_putc('\n');
    }
}

static void cmd_cd(const char *arg) {
    char path[256];
    if (!arg || !arg[0]) sh_strcpy(path, "/");
    else sh_normalize(arg, path, sizeof(path));
    if (API->chdir(path) != 0) {
        sh_puts(TR("Directory not found: ", "目录未找到: ")); sh_puts(arg ? arg : "/"); sh_putc('\n');
        return;
    }
    sh_strcpy(g_cwd, path);   /* 与内核 current_path 一致 */
}

static void cmd_pwd(void) { sh_puts(g_cwd); sh_putc('\n'); }

static void cmd_cat(const char *arg, int mem_mode) {
    if (!arg || !arg[0]) { sh_puts("Usage: cat <file>\n"); return; }
    int size = load_file(arg, mem_mode, g_textbuf, (int)sizeof(g_textbuf));
    if (size == -1) { sh_puts(TR("File not found\n", "文件未找到\n")); return; }
    if (size == -2) { sh_puts(TR("Not a file\n", "不是文件\n")); return; }
    for (int i = 0; i < size; i++) sh_putc(g_textbuf[i]);
    sh_putc('\n');
}

static void cmd_mkdir(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: mkdir <dir>\n"); return; }
    char path[256]; sh_normalize(arg, path, sizeof(path));
    if (API->mkdir(path) == 0) sh_puts(TR("Directory created (ext4)\n", "目录已创建 (ext4)\n"));
    else sh_puts(TR("Failed\n", "失败\n"));
}

static void cmd_touch(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: touch <file>\n"); return; }
    char path[256]; sh_normalize(arg, path, sizeof(path));
    if (API->file_exists(path)) { sh_puts(TR("File exists\n", "文件已存在\n")); return; }
    if (API->file_write(path, "", 0) == 0) sh_puts(TR("File created (ext4)\n", "文件已创建 (ext4)\n"));
    else sh_puts(TR("Failed\n", "失败\n"));
}

/* 多行文本输入 (read_text 优先; 旧内核无此 API 时回退 '.' 行编辑) */
static int sh_read_text(char *buf, int max) {
    if (API->read_text) return API->read_text(buf, max);
    /* 回退: 逐行输入, 单独 '.' 结束 */
    int n = 0; char line[256];
    for (;;) {
        int ln = API->readline(line, sizeof(line));
        if (ln < 0) return -1;
        if (sh_strcmp(line, ".") == 0) break;
        if (n + ln + 1 >= max) break;
        for (int i = 0; i < ln; i++) buf[n++] = line[i];
        buf[n++] = '\n';
    }
    return n;
}

static void cmd_write(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: write <file>\n"); return; }
    sh_puts(TR("Enter text (end with ENDOFFILE to save, Esc to cancel):\n",
               "输入文本 (输入 ENDOFFILE 结束保存, Esc 取消):\n"));
    int n = sh_read_text(g_textbuf, (int)sizeof(g_textbuf));
    if (n < 0) { sh_puts(TR("Cancelled\n", "已取消\n")); return; }
    char path[256]; sh_normalize(arg, path, sizeof(path));
    if (API->file_write(path, g_textbuf, n) == 0) {
        sh_puts(TR("Written ", "已写入 ")); sh_dec((unsigned int)n); sh_puts(TR(" bytes (ext4)\n", " 字节 (ext4)\n"));
    } else sh_puts(TR("Write failed\n", "写入失败\n"));
}

static void cmd_rm(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: rm <file>\n"); return; }
    char path[256]; sh_normalize(arg, path, sizeof(path));
    if (API->file_delete(path) == 0) sh_puts(TR("Removed (ext4)\n", "已删除 (ext4)\n"));
    else sh_puts(TR("Failed (not found, or is directory)\n", "失败 (未找到或为目录)\n"));
}

static void cmd_rmdir(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: rmdir <dir>\n"); return; }
    if (!API->fsop) return;
    if (API->fsop("rmdir", arg, 0, 0, 0) == 0) sh_puts(TR("Removed (ext4)\n", "已删除 (ext4)\n"));
    else sh_puts(TR("Failed (not found, not empty, or not a dir)\n", "失败 (未找到、目录非空或不是目录)\n"));
}

static void cmd_exec(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: exec <program>\n"); return; }
    /* [安全] efmlogin 仅开机内核调用 */
    if (sh_strcmp(arg, "efmlogin") == 0) {
        sh_puts(TR("efmlogin is a system login program, cannot be run manually.\n",
                   "efmlogin 是系统登录程序, 不允许手动执行。\n"));
        return;
    }
    /* exec: 优先当前目录, 再查系统路径 (/EFMOS, /Program/<name>/)
     * [返回值展示] spawn 返回: 正=程序退出码; 0=静默 ok; -1=找不到文件/加载失败;
     *   -256=程序崩溃异常 (#GP/#PF 等). */
    int rc = API->spawn(arg, 0);
    if (rc == 0) return;   /* 内核已打印 "name.efs exited (rc=0)" */
    if (rc == -1) {
        sh_puts(TR("exec: not found or failed to load: ", "exec: 找不到或加载失败: "));
        sh_puts(arg); sh_putc('\n');
    } else if (rc <= -256) {
        sh_puts(TR("exec: crashed (signal ", "exec: 程序崩溃 (信号 "));
        sh_dec((unsigned int)(-(rc + 256)));
        sh_puts(TR(")\n", ")\n"));
    } else {
        sh_puts(TR("exec: exit code = ", "exec: 退出码 = "));
        sh_dec((unsigned int)rc); sh_putc('\n');
    }
}

static void cmd_echo(const char *rest) { sh_puts(rest ? rest : ""); sh_putc('\n'); }

static void cmd_clear(void) { if (API->clear_screen) API->clear_screen(); }

static void cmd_ver(void) {
    sh_puts(TR("EFMOS efmshell v1.1 (full port from kernel shell)\n",
               "EFMOS efmshell v1.1 (内核 shell 全功能移植版)\n"));
    sh_puts(TR("Kernel: EFMOS v1.0.0 - AHCI(rw) + ext4(rw,extent 0-2)\n",
               "内核: EFMOS v1.0.0 - AHCI(读写) + ext4(读写, 深度0-2 extent)\n"));
    sh_puts(TR("Shell: history + tab-completion + full line editor + blinking cursor\n",
               "Shell: 命令历史 + Tab补全 + 完整行编辑器 + 闪烁光标\n"));
    sh_puts(TR("Build: 2026-08-14\n", "构建日期: 2026-08-14\n"));
}

/* ---------- info: logo + 大标题 + 版本号 + sysinfo 文本 ---------- */
/* 8x10 像素大字 (ASCII), 用于 "EFMOS" 标题.  1=前景, 0=背景.
 * [注意] 不使用 const: 若加 const 编译器默认放入 .rodata, 会与显式指定
 * section(".data") 的 builtin_cmds 产生 section type 冲突. */
__attribute__((section(".data")))
static unsigned char big_font[][10] = {
    /* 'E' (index 0) */
    {0xFF,0x80,0x80,0x80,0xFE,0x80,0x80,0x80,0xFF,0x00},
    /* 'F' (index 1) */
    {0xFF,0x80,0x80,0x80,0xFE,0x80,0x80,0x80,0x80,0x00},
    /* 'M' (index 2) */
    {0x81,0xC3,0xE7,0xDB,0x99,0x99,0x81,0x81,0x81,0x00},
    /* 'O' (index 3) */
    {0x7E,0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x7E,0x00},
    /* 'S' (index 4) */
    {0x7E,0x81,0x80,0x40,0x3C,0x02,0x01,0x81,0x7E,0x00},
};
__attribute__((section(".data")))
static char big_font_map[6] = {'E','F','M','O','S',0};

/* 把 logo 画到 (x0, y0) (窗口内容区相对坐标).
 * [修复] 原直接写 backbuffer 有 3 个坑:
 *   1. 像素字节顺序 BGRX vs RGBX 由内核内部 g_pixfmt_rgbx 决定, 外部不可知 → 颜色错乱
 *   2. WM 模式下 EFS 程序直接写 fb 绕开了 efs_should_redirect → 位置错(没加窗口偏移)
 *   3. 合成器读 has_efm_gfx 决定是否重绘窗口, 直接写 fb 漏设此位 → logo 被合成器下次
 *      画背景时直接覆盖 ("渲染不完整" 实际 = 合成器覆盖覆盖了大部分像素)
 *  现在统一走 API->put_pixel: 内部自动做 WM 重定向、正确颜色格式、标记 has_efm_gfx.
 *  用 fill_rect 批处理连续同色 run (原 RLE 天然批量化), 比逐像素 put_pixel 快约 20 倍. */
static void draw_logo(int x0, int y0) {
    int cw = 0, ch = 0;
    if (API->get_viewport) { int cx, cy; API->get_viewport(&cx, &cy, &cw, &ch); }
    for (int y = 0; y < LOGO_H; y++) {
        struct logo_row_hdr h = logo_rows[y];
        int rx = 0;
        int draw_y = y0 + y;
        if (ch > 0 && (draw_y < 0 || draw_y >= ch)) continue;
        for (int k = 0; k < h.n_runs; k++) {
            struct logo_run r = logo_runs[h.start + k];
            unsigned int color = ((unsigned int)r.r << 16) | ((unsigned int)r.g << 8) | (unsigned int)r.b;
            int cnt = r.cnt;
            int x1 = x0 + rx;
            int x2 = x1 + cnt - 1;
            if (cw > 0) {
                if (x2 < 0 || x1 >= cw) { rx += cnt; continue; }
                if (x1 < 0) x1 = 0;
                if (x2 >= cw) x2 = cw - 1;
            }
            /* 连续同色 = 1 像素高的水平线段.
             * fill_rect(x1, y, x2, y, c) 比 cnt 次 put_pixel 快得多. */
            API->fill_rect(x1, draw_y, x2, draw_y, color);
            rx += cnt;
        }
    }
}

/* 画 3x 放大 8x10 大字 (得到 24x30 px / 字).  ch = 字符 'E'/'F'/'M'/'O'/'S', color 前景色. */
static void draw_big_char(int x0, int y0, char ch, unsigned int fg, unsigned int bg, int scale) {
    int idx = -1;
    for (int i = 0; big_font_map[i]; i++) if (big_font_map[i] == ch) { idx = i; break; }
    if (idx < 0) return;
    const unsigned char *pat = big_font[idx];
    for (int row = 0; row < 10; row++) {
        unsigned char bits = pat[row];
        for (int col = 0; col < 8; col++) {
            int on = (bits >> (7 - col)) & 1;
            unsigned int c = on ? fg : bg;
            if (scale == 1) {
                API->put_pixel(x0 + col, y0 + row, c);
            } else {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        API->put_pixel(x0 + col * scale + sx, y0 + row * scale + sy, c);
            }
        }
    }
}

/* 用 draw_char_unicode 画字符串 (透明 bg 模式).  x0,y0 = 内容区相对.  返回 x 步进. */
static int draw_string_tx(int x0, int y0, const char *s, unsigned int fg) {
    int x = x0;
    int cell_w = API->font_w > 0 ? API->font_w : 10;
    int cell_h = API->font_h > 0 ? API->font_h : 18;
    for (int i = 0; s[i]; ) {
        /* 解码 UTF-8 单字节 (ASCII/西欧) 或 3 字节 CJK — 这里只用于 ASCII 标题, 走 1 字节 */
        unsigned int cp = (unsigned char)s[i++];
        int adv = API->draw_char_unicode(x, y0, cp, fg, 0xFEEDFACEu, cell_w, cell_h);
        x += (adv > 0) ? adv : cell_w;
    }
    return x - x0;
}

static void cmd_info(void) {
    /* [隐含 clear] 先清屏再画 banner, 保持画面干净 (语言已由 process_command 同步) */
    if (API->clear_screen) API->clear_screen();

    int cw = 800, ch = 600;
    int cx_abs = 0, cy_abs = 0;
    if (API->get_viewport) API->get_viewport(&cx_abs, &cy_abs, &cw, &ch);
    const unsigned int BG     = LOGO_BG;
    const unsigned int WHITE  = 0xFFFFFFu;
    const unsigned int SILVER = 0x98A2B8u;
    const unsigned int ACCENT = 0x6FB1FFu;

    /* ---- Banner: 足够高以容纳 192x192 logo ---- */
    int banner_h = 224;
    if (ch > 0 && banner_h > ch / 2) banner_h = ch / 2;
    if (banner_h < LOGO_H + 16) banner_h = LOGO_H + 16;
    API->fill_rect(0, 0, cw - 1, banner_h - 1, BG);
    API->fill_rect(0, banner_h, cw - 1, banner_h, ACCENT);

    /* ---- Logo 192x192: 左侧 20px 边距, 垂直居中 ---- */
    int logo_x = 20;
    int logo_y = (banner_h - LOGO_H) / 2;
    if (logo_y < 8) logo_y = 8;
    draw_logo(logo_x, logo_y);

    /* ---- 右侧 "EFMOS" 大字 (4x 放大 8x10 → 32x40 / 字) ---- */
    const char TITLE[] = "EFMOS";
    int scale = 4;
    int lw = 8 * scale;           /* 32 px / 字 */
    int lh = 10 * scale;          /* 40 px / 字 */
    int gap = 8;                  /* 字间距 */
    int title_x = logo_x + LOGO_W + 40;
    int title_y = (banner_h - lh) / 2 - 8;   /* 稍微偏上, 下方留版本号空间 */
    if (title_y < 12) title_y = 12;
    int tx = title_x;
    for (int i = 0; TITLE[i]; i++) {
        draw_big_char(tx + 3, title_y + 3, TITLE[i], 0x0B0B1Au, BG, scale);
        draw_big_char(tx,     title_y,     TITLE[i], WHITE,       BG, scale);
        tx += lw + gap;
    }
    /* ---- 右下角版本信息 (两行: 版本白 / 构建灰) ---- */
    const char *ver_str1 = TR("EFMOS v1.0.0",       "EFMOS v1.0.0");
    const char *ver_str2 = TR("Build 2026-08-14",   "构建 2026-08-14");
    int cell_w = API->font_w > 0 ? API->font_w : 10;
    int w1 = (int)sh_strlen(ver_str1) * cell_w;
    int w2 = (int)sh_strlen(ver_str2) * cell_w;
    int maxw = (w1 > w2) ? w1 : w2;
    int ver_top = banner_h - 16 - 2 * 16;   /* 底部 16px 留空 + 两行文字 */
    if (ver_top < title_y + lh + 16) ver_top = title_y + lh + 16;
    int vx = (cw - 16 - maxw > title_x) ? (cw - 16 - maxw) : title_x;
    draw_string_tx(vx, ver_top,       ver_str1, WHITE);
    draw_string_tx(vx, ver_top + 16,  ver_str2, SILVER);

    if (API->flush_now && !API->wm_enabled) API->flush_now();

    /* ---- sysinfo 文本 (先让 TTY 光标跳过 banner 区, 避免与图形重叠) ----
     * clear_screen 将 TTY 光标重置到 (0,0), 而 banner 是像素图形, 所以
     * 必须先输出 ceil(banner_h / font_h) 个换行, 把光标推到 banner 下方.
     * 额外 +1 行做视觉分隔. */
    {
        int fh = API->font_h > 0 ? API->font_h : 16;
        int lines = (banner_h + fh - 1) / fh + 1;
        for (int i = 0; i < lines; i++) sh_putc('\n');
    }
    if (API->fsop) {
        int r = API->fsop("sysinfo", 0, 0, g_opbuf, (int)sizeof(g_opbuf));
        if (r >= 0) sh_puts(g_opbuf);
    } else sh_puts("info: fsop not available\n");
}

static void cmd_free(void)    { if (fsop_print("memfree", 0, 0) < 0) sh_puts("free: fsop not available\n"); }
static void cmd_date(void)    { if (fsop_print("date", 0, 0) < 0) sh_puts("date: fsop not available\n"); }
static void cmd_shutdown(void){ if (fsop_print("shutdown", 0, 0) < 0) API->reboot(); }

static void cmd_stat(const char *arg, int mem_mode) {
    if (!arg || !arg[0]) { sh_puts("Usage: stat <file>\n"); return; }
    if (mem_mode) {
        char path[256]; sh_normalize(arg, path, sizeof(path));
        int size = load_file(arg, 1, g_textbuf, 0);   /* size 探测 */
        if (size >= 0) {
            sh_puts(TR("File:  ", "文件:  ")); sh_puts(arg); sh_putc('\n');
            sh_puts(TR("FS:    memory\n", "文件系统: 内存\n"));
            sh_puts(TR("Type:  regular file\n", "类型:  普通文件\n"));
            sh_puts(TR("Size:  ", "大小:  ")); sh_dec((unsigned int)size); sh_puts(TR(" bytes\n", " 字节\n"));
        } else if (size == -2) {
            sh_puts(TR("File:  ", "文件:  ")); sh_puts(arg); sh_putc('\n');
            sh_puts(TR("FS:    memory\n", "文件系统: 内存\n"));
            sh_puts(TR("Type:  directory\n", "类型:  目录\n"));
        } else sh_puts(TR("File not found\n", "文件未找到\n"));
        return;
    }
    if (fsop_print("stat", arg, 0) < 0) sh_puts(TR("File not found\n", "文件未找到\n"));
}

static void cmd_hexdump(const char *arg, int mem_mode) {
    int size = load_file(arg, mem_mode, g_textbuf, (int)sizeof(g_textbuf));
    if (size == -1) { sh_puts(TR("File not found\n", "文件未找到\n")); return; }
    if (size == -2) { sh_puts(TR("Not a file\n", "不是文件\n")); return; }
    if (size > 8192) size = 8192;
    for (int off = 0; off < size; off += 16) {
        sh_hex((unsigned int)off); sh_puts("  ");
        for (int i = 0; i < 16; i++) {
            if (off + i < size) {
                unsigned char b = (unsigned char)g_textbuf[off + i];
                sh_putc("0123456789ABCDEF"[b >> 4]);
                sh_putc("0123456789ABCDEF"[b & 0xF]);
                sh_putc(' ');
            } else sh_puts("   ");
            if (i == 7) sh_putc(' ');
        }
        sh_puts(" |");
        for (int i = 0; i < 16; i++) {
            if (off + i < size) {
                unsigned char b = (unsigned char)g_textbuf[off + i];
                sh_putc((b >= 32 && b < 127) ? (char)b : '.');
            }
        }
        sh_puts("|\n");
    }
}

static void cmd_tree(void) { fsop_print("tree", 0, 0); }

static void cmd_find(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: find <name>\n"); return; }
    sh_puts(TR("Searching for: ", "正在搜索: ")); sh_puts(arg); sh_putc('\n');
    fsop_print("find", arg, 0);
}

static void cmd_du(void)  { fsop_print("du", 0, 0); }
static void cmd_df(void)  { fsop_print("df", 0, 0); }

static void cmd_cp(const char *src, const char *dst) {
    if (!src || !dst) { sh_puts("Usage: cp <src> <dst>\n"); return; }
    char spath[256], dpath[256];
    sh_normalize(src, spath, sizeof(spath));
    sh_normalize(dst, dpath, sizeof(dpath));
    if (API->file_exists(dpath)) { sh_puts(TR("Destination exists\n", "目标已存在\n")); return; }
    /* 用内核 malloc 做 64KB 中转 (与旧版 0x80000 缓冲等价) */
    char *buf = (char*)API->malloc(65536);
    if (!buf) buf = g_textbuf;
    int bufsz = (buf == g_textbuf) ? (int)sizeof(g_textbuf) : 65536;
    int sz = API->file_read(spath, buf, bufsz);
    if (sz < 0) { sh_puts(TR("Source not found\n", "源文件未找到\n")); if (buf != g_textbuf) API->free(buf); return; }
    if (API->file_write(dpath, buf, sz) == 0) {
        sh_puts(TR("Copied ", "已复制 ")); sh_dec((unsigned int)sz); sh_puts(TR(" bytes (ext4)\n", " 字节 (ext4)\n"));
    } else sh_puts(TR("Write dst failed\n", "写入目标失败\n"));
    if (buf != g_textbuf) API->free(buf);
}

static void cmd_mv(const char *src, const char *dst) {
    if (!src || !dst) { sh_puts("Usage: mv <src> <dst>\n"); return; }
    char spath[256], dpath[256];
    sh_normalize(src, spath, sizeof(spath));
    sh_normalize(dst, dpath, sizeof(dpath));
    if (API->file_exists(dpath)) { sh_puts(TR("Destination exists\n", "目标已存在\n")); return; }
    char *buf = (char*)API->malloc(65536);
    if (!buf) buf = g_textbuf;
    int bufsz = (buf == g_textbuf) ? (int)sizeof(g_textbuf) : 65536;
    int sz = API->file_read(spath, buf, bufsz);
    if (sz < 0) { sh_puts(TR("Source not found\n", "源文件未找到\n")); if (buf != g_textbuf) API->free(buf); return; }
    if (API->file_write(dpath, buf, sz) != 0) {
        sh_puts(TR("Write dst failed\n", "写入目标失败\n"));
        if (buf != g_textbuf) API->free(buf);
        return;
    }
    if (API->file_delete(spath) == 0) sh_puts(TR("Moved (ext4)\n", "已移动 (ext4)\n"));
    else sh_puts(TR("Dst created but src delete failed\n", "目标创建成功但源文件删除失败\n"));
    if (buf != g_textbuf) API->free(buf);
}

static void cmd_ln(const char *src, const char *dst) {
    if (!src || !dst) { sh_puts("Usage: ln <target> <linkname>\n"); return; }
    int ino = API->fsop ? API->fsop("ln", src, dst, 0, 0) : 0;
    if (ino > 0) {
        sh_puts(TR("Symlink created (ext4, ino=", "符号链接已创建 (ext4, inode="));
        sh_dec((unsigned int)ino); sh_puts(") -> "); sh_puts(src); sh_putc('\n');
    } else sh_puts(TR("Failed (exists or no space)\n", "失败 (已存在或无剩余空间)\n"));
}

static void cmd_readlink(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: readlink <link>\n"); return; }
    char target[256];
    int r = API->fsop ? API->fsop("readlink", arg, 0, target, (int)sizeof(target)) : -1;
    if (r < 0) { sh_puts(TR("Not found or not a symlink\n", "未找到或不是符号链接\n")); return; }
    target[r < 255 ? r : 255] = 0;
    sh_puts(target); sh_putc('\n');
}

static void cmd_truncate(const char *file, const char *size_str) {
    if (!file || !file[0] || !size_str || !size_str[0]) { sh_puts("Usage: truncate <file> <size>\n"); return; }
    int r = API->fsop ? API->fsop("truncate", file, size_str, 0, 0) : -1;
    if (r == 0) {
        sh_puts(TR("Truncated to ", "已截断至 ")); sh_puts(size_str); sh_puts(TR(" bytes (ext4)\n", " 字节 (ext4)\n"));
    } else sh_puts(TR("Failed\n", "失败\n"));
}

static void cmd_sync(void) {
    int r = API->fsop ? API->fsop("sync", 0, 0, 0, 0) : -1;
    if (r == 0) sh_puts(TR("Disk cache flushed.\n", "磁盘缓存已刷新。\n"));
    else sh_puts(TR("Flush failed (or not supported).\n", "刷新失败 (或不支持)。\n"));
}

static void cmd_append(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: append <file>\n"); return; }
    sh_puts(TR("Append text (end with ENDOFFILE to save, Esc to cancel):\n",
               "追加文本 (输入 ENDOFFILE 结束保存, Esc 取消):\n"));
    int n = sh_read_text(g_textbuf, (int)sizeof(g_textbuf));
    if (n < 0) { sh_puts(TR("Cancelled\n", "已取消\n")); return; }
    int r = API->fsop ? API->fsop("append", arg, g_textbuf, 0, 0) : -1;
    if (r >= 0) {
        sh_puts(TR("Appended, total ", "已追加, 总计 ")); sh_dec((unsigned int)r); sh_puts(TR(" bytes (ext4)\n", " 字节 (ext4)\n"));
    } else sh_puts(TR("Failed (file not found)\n", "失败 (文件未找到)\n"));
}

static void cmd_head(const char *arg, int mem_mode) {
    if (!arg || !arg[0]) { sh_puts("Usage: head <file> [n]\n"); return; }
    char a1[128]; int n = 10;
    int sp = sh_strchr(arg, ' ');
    if (sp >= 0) { sh_strncpy(a1, arg, sp); n = sh_atoi(arg + sp + 1); }
    else sh_strcpy(a1, arg);
    if (n <= 0) n = 10;
    int size = load_file(a1, mem_mode, g_textbuf, (int)sizeof(g_textbuf));
    if (size == -1) { sh_puts(TR("File not found\n", "文件未找到\n")); return; }
    if (size == -2) { sh_puts(TR("Not a file\n", "不是文件\n")); return; }
    int lines = 0;
    for (int i = 0; i < size && lines < n; i++) { sh_putc(g_textbuf[i]); if (g_textbuf[i] == '\n') lines++; }
    sh_putc('\n');
}

static void cmd_tail(const char *arg, int mem_mode) {
    if (!arg || !arg[0]) { sh_puts("Usage: tail <file> [n]\n"); return; }
    char a1[128]; int n = 10;
    int sp = sh_strchr(arg, ' ');
    if (sp >= 0) { sh_strncpy(a1, arg, sp); n = sh_atoi(arg + sp + 1); }
    else sh_strcpy(a1, arg);
    if (n <= 0) n = 10;
    int size = load_file(a1, mem_mode, g_textbuf, (int)sizeof(g_textbuf));
    if (size == -1) { sh_puts(TR("File not found\n", "文件未找到\n")); return; }
    if (size == -2) { sh_puts(TR("Not a file\n", "不是文件\n")); return; }
    int cnt = 0; int end = size;
    for (int i = size - 1; i >= 0 && cnt <= n; i--) {
        if (g_textbuf[i] == '\n') { cnt++; if (cnt > n) { end = i + 1; break; } }
        if (i == 0 && cnt <= n) { end = 0; break; }
    }
    for (int i = end; i < size; i++) sh_putc(g_textbuf[i]);
    sh_putc('\n');
}

static void cmd_wc(const char *arg, int mem_mode) {
    int size = load_file(arg, mem_mode, g_textbuf, (int)sizeof(g_textbuf));
    if (size == -1) { sh_puts(TR("File not found\n", "文件未找到\n")); return; }
    if (size == -2) { sh_puts(TR("Not a file\n", "不是文件\n")); return; }
    unsigned int lines = 0, words = 0;
    int in_word = 0;
    for (int i = 0; i < size; i++) {
        if (g_textbuf[i] == '\n') lines++;
        if (g_textbuf[i] == ' ' || g_textbuf[i] == '\n' || g_textbuf[i] == '\t') in_word = 0;
        else if (!in_word) { in_word = 1; words++; }
    }
    sh_dec(lines); sh_puts("  "); sh_dec(words); sh_puts("  "); sh_dec((unsigned int)size); sh_putc('\n');
}

static void cmd_edit(const char *arg) {
    if (!arg || !arg[0]) { sh_puts("Usage: edit <file>\n"); return; }
    sh_puts(TR("Editing ", "正在编辑 ")); sh_puts(arg);
    sh_puts(TR(" (end with ENDOFFILE to save, Esc to cancel)\n", " (输入 ENDOFFILE 结束保存, Esc 取消)\n"));
    int n = sh_read_text(g_textbuf, (int)sizeof(g_textbuf));
    if (n < 0) { sh_puts(TR("Cancelled\n", "已取消\n")); return; }
    char path[256]; sh_normalize(arg, path, sizeof(path));
    if (API->file_write(path, g_textbuf, n) == 0) {
        sh_puts(TR("Written ", "已写入 ")); sh_dec((unsigned int)n); sh_puts(TR(" bytes (ext4)\n", " 字节 (ext4)\n"));
    } else sh_puts(TR("Cannot create/write file\n", "无法创建/写入文件\n"));
}

/* ========== Command History (原 kernel 移植) ========== */
#define HISTORY_SIZE 16
__attribute__((section(".data"))) static char history[HISTORY_SIZE][256];
__attribute__((section(".data"))) static int history_count = 0;
__attribute__((section(".data"))) static int history_pos = 0;

static void history_add(const char *cmd) {
    if (!cmd[0]) return;
    if (history_count > 0) {
        int last = (history_pos == 0) ? HISTORY_SIZE - 1 : history_pos - 1;
        if (sh_strcmp(history[last], cmd) == 0) return;
    }
    sh_strcpy(history[history_pos], cmd);
    history_pos = (history_pos + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) history_count++;
}

static const char *history_get(int idx) {
    if (idx < 0 || idx >= history_count) return 0;
    int pos = (history_pos - 1 - idx + HISTORY_SIZE) % HISTORY_SIZE;
    return history[pos];
}

static void cmd_history(void) {
    for (int i = 0; i < history_count; i++) {
        sh_dec((unsigned int)(i + 1)); sh_puts("  ");
        sh_puts(history_get(i)); sh_putc('\n');
    }
}

/* readline_ext 的历史回调 */
static const char *sh_hist_get_cb(int idx) { return history_get(idx); }

/* ========== Tab Completion (原 kernel complete_collect 移植) ==========
 * 匹配: 内置命令 + ext4 当前目录条目 (mem FS 文件经 fsop memls 亦可, 从简省略) */
__attribute__((section(".data"))) static const char *builtin_cmds[] = {
    "ls","cd","pwd","cat","mkdir","touch","write","rm","rmdir","exec","echo",
    "clear","cls","help","ver","info","free","date","reboot","shutdown","stat",
    "hexdump","tree","find","du","df","cp","mv","ln","readlink","truncate",
    "sync","append","head","tail","wc","edit","history","mem",
    "start","ps","wminfo",0
};

static int sh_complete_cb(const char *prefix, char *matches, int max_n, int match_len) {
    int n = 0;
    /* 内置命令 */
    for (int i = 0; builtin_cmds[i] && n < max_n; i++)
        if (starts_with(builtin_cmds[i], prefix)) {
            sh_strcpy(matches + n * match_len, builtin_cmds[i]);
            n++;
        }
    /* ext4 当前目录条目 */
    struct { struct efs_dirent e[48]; } ents;   /* 栈 ~3.5KB */
    int cnt = API->file_list(g_cwd, ents.e, 48);
    for (int i = 0; i < cnt && n < max_n; i++) {
        if (starts_with(ents.e[i].name, prefix)) {
            sh_strcpy(matches + n * match_len, ents.e[i].name);
            n++;
        }
    }
    return n;
}

/* ========== Process Command (多参数 + mem 前缀, 原 kernel 移植) ========== */
static void process_command(char *cmd) {
    int len = sh_strlen(cmd);
    while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == '\r' || cmd[len-1] == ' ')) cmd[--len] = 0;
    if (len == 0) return;

    /* [语言同步] 每条命令执行前重新读取, setting.efs 改语言后所有命令立即生效 */
    if (API->get_lang) g_lang = API->get_lang();

    history_add(cmd);   /* parse_args 会就地打断 cmd, 先存历史 */

    char *argv[16];
    int argc = parse_args(cmd, argv, 16);
    if (argc == 0) return;

    /* "mem" 前缀: 读命令切换到内存 FS */
    int mem_mode = 0;
    char *c = argv[0];
    if (sh_strcmp(c, "mem") == 0 && argc >= 2) {
        mem_mode = 1;
        for (int i = 1; i < argc; i++) argv[i-1] = argv[i];
        argc--;
        c = argv[0];
    }
    /* a1/a2 合并: head/tail/truncate 等需要 "剩余参数原样" 的命令用 rest */
    const char *a1 = argc > 1 ? argv[1] : 0;
    const char *a2 = argc > 2 ? argv[2] : 0;
    /* rest = argv[1] 起原始串 (parse_args 已在分隔符处写 0, 重新拼) */
    char rest[512]; rest[0] = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1) sh_strcat(rest, " ");
        sh_strcat(rest, argv[i]);
    }

    if      (sh_strcmp(c, "ls") == 0)      cmd_ls(a1, mem_mode);
    else if (sh_strcmp(c, "cd") == 0)      cmd_cd(a1);
    else if (sh_strcmp(c, "pwd") == 0)     cmd_pwd();
    else if (sh_strcmp(c, "cat") == 0)     cmd_cat(a1, mem_mode);
    else if (sh_strcmp(c, "mkdir") == 0)   cmd_mkdir(a1);
    else if (sh_strcmp(c, "touch") == 0)   cmd_touch(a1);
    else if (sh_strcmp(c, "write") == 0)   cmd_write(a1);
    else if (sh_strcmp(c, "rm") == 0 || sh_strcmp(c, "del") == 0) cmd_rm(a1);
    else if (sh_strcmp(c, "rmdir") == 0)   cmd_rmdir(a1);
    else if (sh_strcmp(c, "exec") == 0)    cmd_exec(a1);
    else if (sh_strcmp(c, "echo") == 0)    cmd_echo(rest);
    else if (sh_strcmp(c, "clear") == 0)   cmd_clear();
    else if (sh_strcmp(c, "cls") == 0)     cmd_clear();
    else if (sh_strcmp(c, "help") == 0)    cmd_help(a1);
    else if (sh_strcmp(c, "?") == 0)       cmd_help(a1);
    else if (sh_strcmp(c, "ver") == 0 || sh_strcmp(c, "version") == 0) cmd_ver();
    else if (sh_strcmp(c, "info") == 0)    cmd_info();
    else if (sh_strcmp(c, "free") == 0)    cmd_free();
    else if (sh_strcmp(c, "date") == 0)    cmd_date();
    else if (sh_strcmp(c, "reboot") == 0)  { if (API->reboot) API->reboot(); }
    else if (sh_strcmp(c, "shutdown") == 0) cmd_shutdown();
    else if (sh_strcmp(c, "stat") == 0)    cmd_stat(a1, mem_mode);
    else if (sh_strcmp(c, "hexdump") == 0) cmd_hexdump(a1, mem_mode);
    else if (sh_strcmp(c, "tree") == 0)    cmd_tree();
    else if (sh_strcmp(c, "find") == 0)    cmd_find(a1);
    else if (sh_strcmp(c, "du") == 0)      cmd_du();
    else if (sh_strcmp(c, "df") == 0)      cmd_df();
    else if (sh_strcmp(c, "cp") == 0)      cmd_cp(a1, a2);
    else if (sh_strcmp(c, "mv") == 0)      cmd_mv(a1, a2);
    else if (sh_strcmp(c, "ln") == 0)      cmd_ln(a1, a2);
    else if (sh_strcmp(c, "readlink") == 0) cmd_readlink(a1);
    else if (sh_strcmp(c, "truncate") == 0) cmd_truncate(a1, a2);
    else if (sh_strcmp(c, "sync") == 0)    cmd_sync();
    else if (sh_strcmp(c, "append") == 0)  cmd_append(a1);
    else if (sh_strcmp(c, "head") == 0)    cmd_head(rest, mem_mode);
    else if (sh_strcmp(c, "tail") == 0)    cmd_tail(rest, mem_mode);
    else if (sh_strcmp(c, "wc") == 0)      cmd_wc(a1, mem_mode);
    else if (sh_strcmp(c, "edit") == 0)    cmd_edit(a1);
    else if (sh_strcmp(c, "history") == 0) cmd_history();
    /* ---- 窗口/多任务命令 ---- */
    else if (sh_strcmp(c, "start") == 0) {
        if (!a1) {
            sh_puts(TR("Usage: start <program> [args...]\n", "用法: start <程序名> [参数...]\n"));
        } else if (API->wm_enabled) {
            /* 桌面模式: 参数 = argv[2..] 空格连接 */
            char sargs[384]; sargs[0] = 0;
            for (int i = 2; i < argc; i++) {
                if (i > 2) sh_strcat(sargs, " ");
                sh_strcat(sargs, argv[i]);
            }
            int pid = API->spawn_async(a1, sargs);
            if (pid > 0) {
                sh_puts(TR("Started ", "已启动 ")); sh_puts(a1);
                sh_puts(TR(" in window (pid=", " 在窗口中 (pid="));
                sh_dec((unsigned int)pid); sh_puts(")\n");
            } else {
                sh_puts(TR("start: failed (error=", "start: 启动失败 (错误="));
                sh_dec((unsigned int)(-pid)); sh_puts(")\n");
            }
        } else {
            sh_puts(TR("start: Window Manager not active; running synchronously.\n",
                       "start: 窗口管理器未激活; 改为同步运行。\n"));
            API->spawn(a1, rest[0] ? rest : 0);
        }
    }
    else if (sh_strcmp(c, "ps") == 0)     { fsop_print("ps", 0, 0); }
    else if (sh_strcmp(c, "wminfo") == 0) { fsop_print("wminfo", 0, 0); }
    else {
        /* [安全] efmlogin 仅开机内核调用 */
        if (sh_strcmp(c, "efmlogin") == 0) {
            sh_puts(TR("efmlogin is a system login program, cannot be run manually.\n",
                       "efmlogin 是系统登录程序, 不允许手动执行。\n"));
            return;
        }
        /* 非内置命令: 运行 <c>.efs (内核查找 /EFMOS → /Program/<c>/ → CWD).
         * [桌面模式] 无附加参数 → 新窗口异步启动; 有参数 → shell 同步运行
         * [返回值展示] 同步 spawn 与 cmd_exec 规则一致:
         *   rc==0: 静默 ok (内核已打印 exited rc=0)
         *   rc==-1: 找不到 / 加载失败 → "Unknown command"
         *   rc<=-256: 程序崩溃异常
         *   rc>0: 程序返回非零 → 打印退出码 */
        if (API->wm_enabled && argc == 1) {
            int pid = API->spawn_async(c, "");
            if (pid > 0) {
                sh_puts(TR("Started ", "已启动 ")); sh_puts(c);
                sh_puts(TR(" in window (pid=", " 在窗口中 (pid="));
                sh_dec((unsigned int)pid); sh_puts(")\n");
            } else {
                sh_puts(TR("Unknown command: ", "未知命令: ")); sh_puts(c);
                sh_puts(TR(" (try help)\n", " (输入 help 查看帮助)\n"));
            }
        } else {
            int rc = API->spawn(c, rest[0] ? rest : 0);
            if (rc == 0) {
                /* 内核已打印 "name.efs exited (rc=0)" */
            } else if (rc == -1) {
                sh_puts(TR("Unknown command: ", "未知命令: ")); sh_puts(c);
                sh_puts(TR(" (try help)\n", " (输入 help 查看帮助)\n"));
            } else if (rc <= -256) {
                sh_puts(c); sh_puts(TR(": crashed (signal ", ": 崩溃 (信号 "));
                sh_dec((unsigned int)(-(rc + 256)));
                sh_puts(TR(")\n", ")\n"));
            } else {
                sh_puts(c); sh_puts(TR(": exit code = ", ": 退出码 = "));
                sh_dec((unsigned int)rc); sh_putc('\n');
            }
        }
    }
}

/* ========== 主循环 ========== */
/* 提示符: <用户>@EFMOS:<路径>[/]$  (原 kernel build_prompt 格式) */
static void build_prompt(char *buf, int sz) {
    char user[64]; user[0] = 0;
    if (API->get_current_user) API->get_current_user(user, sizeof(user));
    buf[0] = 0;
    if (user[0]) { sh_strcpy(buf, user); sh_strcat(buf, "@"); }
    sh_strcat(buf, "EFMOS:");
    sh_strcat(buf, g_cwd);
    if (sh_strcmp(g_cwd, "/") != 0) sh_strcat(buf, "/");
    sh_strcat(buf, "$ ");
    (void)sz;
}

/* [COM1 探针] 轮询 THR 空后写一字节, 定位 efmshell 运行卡点 (QEMU -serial 可见) */
static void com1(char c) {
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

void efmshell_main(void) {
    com1('M');   /* 探针: efmshell_main 已进入 */
    if (API->magic != 0xEF110001u) {
        com1('X');   /* 探针: magic 异常 (仍继续跑, 只警告) */
    }
    g_lang = API->get_lang ? API->get_lang() : 0;
    /* 初始 CWD: 跟随登录用户默认目录 (/<username>), 失败回 "/" */
    sh_strcpy(g_cwd, "/");
    char user[64]; user[0] = 0;
    if (API->get_current_user && API->get_current_user(user, sizeof(user)) > 0 && user[0]) {
        char up[256]; up[0] = '/'; sh_strcpy(up + 1, user);
        if (API->chdir(up) == 0) sh_strcpy(g_cwd, up);
    }
    sh_puts(TR("efmshell v1.1 (full shell port, detached from kernel)\n",
               "efmshell v1.1 (shell 全功能移植, 已从内核独立)\n"));
    sh_puts(TR("type 'help' for commands, Tab to complete, Up/Down for history\n\n",
               "输入 'help' 查看命令, Tab 补全, 上/下方向键翻历史\n\n"));

    char cmd[256]; char prompt[192];
    com1('L');   /* 探针: 即将进入主循环 */
    for (;;) {
        build_prompt(prompt, sizeof(prompt));
        com1('r');   /* 探针: 进入 readline */
        int n;
        if (API->readline_ext)
            n = API->readline_ext(prompt, cmd, (int)sizeof(cmd), sh_hist_get_cb, sh_complete_cb);
        else
            n = API->readline(cmd, (int)sizeof(cmd));
        com1('R');   /* 探针: readline 返回 */
        if (n < 0) { sh_puts(TR("(input aborted)\n", "(输入中止)\n")); continue; }
        process_command(cmd);
        if (API->yield) API->yield();
    }
}

/* [关键] .text.start + efm_flat.ld 保证 _start 位于 load_addr (0xA00000).
 * 无此约束时 GCC 按定义顺序排函数, 首函数是 sh_strlen → 内核跳入后
 * 执行 sh_strlen 完即 ret, 表现为 "entry returned normally" 秒退. */
__attribute__((naked, section(".text.start"))) void _start(void) {
    __asm__ volatile (
        "movw $0x3FD, %dx\n"
        "1: inb %dx, %al\n"
        "testb $0x20, %al\n"
        "jz 1b\n"
        "movw $0x3F8, %dx\n"
        "movb $0x73, %al\n"      /* 's': _start 第一条指令 */
        "outb %al, %dx\n"
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"
        "call efmshell_main\n\t"
        "movw $0x3FD, %dx\n"
        "2: inb %dx, %al\n"
        "testb $0x20, %al\n"
        "jz 2b\n"
        "movw $0x3F8, %dx\n"
        "movb $0x51, %al\n"      /* 'Q': main 返回 (只有异常路径才会到这里) */
        "outb %al, %dx\n"
        "leave\n\t"
        "ret"
    );
}
