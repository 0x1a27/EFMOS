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

/* efmloader.c - EFMOS 驱动加载器 (编译为 efmloader.efs)
 * 加载地址: 0x400000 (4MB, 位于 userman 5MB 之前, 不与 setting 1MB / fileman 3MB 冲突)
 * 入口: _start
 *
 * 功能:
 *   - 在内核启动早期 (用户登录前) 由 kmain 通过 spawn_async("efmloader") 异步启动
 *   - 以最高优先级 (nice=0) 运行, 确保驱动加载先于一切用户进程
 *   - 扫描 /EFMOS/DRIVERS 目录, 查找所有 *.drv 文件
 *   - 按文件名排序 (字母序) 逐个调用 api->load_driver(path) 加载并注册
 *   - 输出统计信息 (成功/失败/总) 到屏幕和串口
 *   - 加载完毕后 **不退出**, 进入后台监控循环:
 *       * 周期性检查已注册驱动数量是否变化
 *       * 周期性扫描 /EFMOS/DRIVERS 是否有新 .drv 文件 (热加载)
 *       * 大部分时间 sleep_ms + yield, 不占用 CPU
 *   - 若 /EFMOS/DRIVERS 不存在, 仍进入后台循环等待目录出现
 *
 * [设计]
 *   驱动由 efmloader 统一加载, 而非每个用户进程各自加载, 好处:
 *     1. 加载顺序可控 (磁盘 → 显卡 → 网卡, 避免依赖倒置)
 *     2. 权限集中, 普通 .efs 程序无权调 load_driver (API 虽然暴露在 kernel_api,
 *        但普通进程一般不使用, 未来可在 API 中增加权限位检查)
 *     3. 启动最早, 用户登录前驱动就绪 (显卡驱动可让登录界面立即用新驱动)
 *     4. 作为常驻后台进程, 可支持后续驱动热插拔/热加载
 */

/* ========== 内核 API 表 (必须与 struct kernel_api 字段顺序/对齐完全一致) ==========
 * 注意: 2026+ 新增 load_driver / driver_count / driver_list / sleep_ms 等 API
 *       必须严格匹配 kernel.c 中的顺序! */
struct efs_dirent {
    char name[64];
    unsigned int size;
    unsigned int is_dir;
};
struct kernel_api {
    unsigned int magic;
    unsigned int _pad;
    void (*put_char)(char);
    void (*print)(const char*);
    void (*print_utf8)(const char*);
    void (*clear_screen)(void);
    int (*file_read)(const char*, char*, int);
    int (*file_write)(const char*, const char*, int);
    int (*file_exists)(const char*);
    int (*mkdir)(const char*);
    int (*readline)(char*, int);
    void (*reboot)(void);
    int (*get_lang)(void);
    void (*set_lang)(int);
    int (*save_settings)(void);
    int  (*mouse_poll)(void *out_event);
    void (*mouse_set_cursor)(int show);
    int  (*file_list)(const char *dir_path, struct efs_dirent *out, int max_count);
    int  (*file_delete)(const char *path);
    int  (*key_poll)(void);
    /* ========== 用户系统 API ========== */
    int  (*get_current_user)(char *buf, int bufsz);
    int  (*set_current_user)(const char *username);
    int  (*user_list)(struct efs_dirent *out, int max_count);
    int  (*user_create)(const char *username);
    int  (*user_delete)(const char *username);
    /* ========== 扩展 API ========== */
    void *(*malloc)(unsigned long);
    void  (*free)(void*);
    int   (*spawn)(const char *name, const char *args);
    int   (*get_args)(char *buf, int max);
    /* 字体像素尺寸 */
    int font_w;
    int font_h;
    /* 2025+ 窗口系统 */
    int current_pid;
    int wm_enabled;
    void (*put_pixel)(int x, int y, unsigned int c);
    void (*fill_rect)(int x1, int y1, int x2, int y2, unsigned int c);
    void (*draw_rect)(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill);
    void (*get_viewport)(int *cx, int *cy, int *cw, int *ch);
    void (*get_fb_info)(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base);
    int  (*blit_to_window)(const void *src, int src_w, int src_h, int src_pitch);
    /* ========== 2026+ 驱动子系统 API ========== */
    int   (*load_driver)(const char *path);
    int   (*driver_count)(void);
    int   (*driver_list)(char out_names[][32], int max);
    /* ========== 2026+ 多线程调度 API ========== */
    void  (*sleep_ms)(unsigned long ms);
    void  (*yield)(void);
    int   (*get_pid)(void);
    int   (*spawn_async)(const char *name, const char *args);
    int   (*set_priority)(int pid, int nice);
    /* ========== 2026+ Mesa 合成器 API ========== */
    int   (*get_wm_snapshot)(void *out, int max_bytes);
    void  (*set_compositor_active)(int active);
    void *(*dlsym)(const char *name);
    int   (*get_backbuffer)(void **out_ptr, int *out_pitch, int *out_w, int *out_h);
    void  (*mark_dirty_rect)(int x1, int y1, int x2, int y2);
    void  (*flush_now)(void);
    int   (*draw_char_unicode)(int x, int y, unsigned int codepoint,
                               unsigned int fg, unsigned int bg, int cell_w, int cell_h);
    void  (*set_gfx_info)(void *info);
};

#define API_MAGIC  0xEF110001
#define API        ((volatile struct kernel_api*)0x9000)

/* ========== 语言 (efmloader 使用极简 i18n: 英文 + 中文, 跟随内核 get_lang) ==========
 * 0 = English, 1 = 简体中文 (与内核 efm_lang 编码一致) */
static int L = 0;
#define TR(en, zh)  (L == 1 ? (zh) : (en))

/* ========== 字符串/工具函数 ========== */
static int my_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void my_strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++)) {}
}
static int ends_with_drv(const char *name) {
    int n = 0; while (name[n]) n++;
    if (n < 4) return 0;
    return (name[n-4]=='.' && (name[n-3]=='d'||name[n-3]=='D') &&
            (name[n-2]=='r'||name[n-2]=='R') && (name[n-1]=='v'||name[n-1]=='V'));
}
/* 文件名比较 (不区分大小写比较 ASCII A-Z),
 * 保证 ahci.drv 排在 vga.drv 前 (a<v), 且数字顺序递增 10, 20 ... 99 */
static int fname_cmp(const char *a, const char *b) {
    while (1) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
        if (cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
        a++; b++;
    }
}
static void print_dec(int v) {
    if (v < 0) { API->put_char('-'); v = -v; }
    if (v == 0) { API->put_char('0'); return; }
    char b[12]; int p = 0;
    while (v) { b[p++] = '0' + (v % 10); v /= 10; }
    while (p--) API->put_char(b[p]);
}

/* ========== 主程序 ========== */
void efmloader_main(void);

__attribute__((naked, section(".text.start")))
void _start(void) {
    __asm__ volatile(
        "push %rbp\n\t"
        "mov %rsp, %rbp\n\t"
        "and $-16, %rsp\n\t"
        "call efmloader_main\n\t"
        "leave\n\t"
        "ret\n\t"
    );
}

/* 把 dirent 列表插入排序, 便于按名字顺序加载 (ahci 先于 vga 等) */
static void sort_entries(struct efs_dirent *arr, int n) {
    for (int i = 1; i < n; i++) {
        struct efs_dirent key = arr[i];
        int j = i - 1;
        while (j >= 0 && fname_cmp(arr[j].name, key.name) > 0) {
            arr[j+1] = arr[j];
            j--;
        }
        arr[j+1] = key;
    }
}

/* 构造 /EFMOS/DRIVERS/<fname> 路径 */
static void build_drv_path(char *out, int outsz, const char *fname) {
    const char *prefix = "/EFMOS/DRIVERS/";
    int pl = 0; while (prefix[pl]) pl++;
    int fl = 0; while (fname[fl]) fl++;
    int n = 0;
    for (int i = 0; i < pl && n < outsz - 1; i++) out[n++] = prefix[i];
    for (int i = 0; i < fl && n < outsz - 1; i++) out[n++] = fname[i];
    out[n] = 0;
}

/* 打印带颜色的 banner: efmloader 启动标题 (简单 ASCII art + 清屏) */
static void draw_banner(void) {
    API->clear_screen();
    API->print("================================================\n");
    API->print(TR("  EFMOS Driver Manager (efmloader.efs)\n",
                  "  EFMOS 驱动管理器 (efmloader.efs)\n"));
    API->print("================================================\n\n");
}

/* ========== 后台监控循环 (永不退出, 独立线程) ==========
 * 由 efmloader 同步阶段用 spawn_async("--background") 启动:
 *   - nice=0 最高优先级 (驱动线程级)
 *   - 每 3 秒重新扫描 /EFMOS/DRIVERS, 自动热加载新增 .drv
 *   - 大部分时间 sleep_ms(3000) + yield, 不占 CPU */
static void monitor_loop(void) {
    for (;;) {
        /* sleep_ms + yield: 在 LAPIC 定时器未就绪时 sleep_ms 可能立即返回,
         * 加忙等延迟防止空转刷屏 */
        if (API->sleep_ms) API->sleep_ms(3000);
        if (API->yield) API->yield();
        for (volatile int j = 0; j < 5000000; j++) __asm__ volatile("pause");

        /* 热加载: 重新扫描 /EFMOS/DRIVERS 并加载新增 .drv
         * 先获取已注册驱动列表, 跳过已加载的驱动 */
        char loaded[16][32];
        int loaded_cnt = 0;
        if (API->driver_list) loaded_cnt = API->driver_list(loaded, 16);
        if (loaded_cnt <= 0) continue;  /* 驱动表为空说明初始加载未完成, 跳过 */

        struct efs_dirent scan[64];
        int sn = API->file_list("/EFMOS/DRIVERS", scan, 64);
        if (sn <= 0) continue;
        for (int i = 0; i < sn; i++) {
            if (scan[i].is_dir) continue;
            if (!ends_with_drv(scan[i].name)) continue;

            /* 检查是否已加载: 提取驱动核心名 (去掉 "NN-" 前缀和 ".drv" 后缀) */
            char drv_core[64];
            const char *fname = scan[i].name;
            int fn = 0; while (fname[fn]) fn++;
            const char *core = fname;
            if (fn > 3 && fname[2] == '-') core = fname + 3;
            int cl = 0; while (core[cl] && cl < 60) {
                if (core[cl] == '.' && core[cl+1] == 'd') break;
                drv_core[cl] = core[cl]; cl++;
            }
            drv_core[cl] = 0;

            int already = 0;
            for (int j = 0; j < loaded_cnt; j++) {
                if (my_strcmp(drv_core, loaded[j]) == 0) { already = 1; break; }
            }
            if (already) continue;

            char path[256];
            build_drv_path(path, sizeof(path), scan[i].name);
            (void)API->load_driver(path);
        }
    }
}

void efmloader_main(void) {
    if (API->magic != API_MAGIC) {
        API->print("efmloader: bad api magic, abort.\n");
        return;
    }
    L = API->get_lang();

    /* 先判断是否是 "后台模式" 实例 (由同步阶段 spawn_async 出的第二份副本) */
    char args[512]; args[0] = 0;
    int is_background = 0;
    if (API->get_args) {
        int alen = API->get_args(args, sizeof(args));
        if (alen > 0) {
            /* 以 "--background" 开头视为后台模式 (efmloader --background) */
            char needle[] = "--background";
            int i = 0; for (; needle[i]; i++) {
                if (args[i] != needle[i]) break;
            }
            if (needle[i] == 0 && (args[i] == 0 || args[i] == ' ')) is_background = 1;
        }
    }

    /* 以最高优先级 (nice=0) 运行, 确保驱动加载先于一切用户进程 */
    if (API->set_priority && API->get_pid) {
        int pid = API->get_pid();
        (void)API->set_priority(pid, 0);   /* 0 = 最高优先级 */
    }

    /* ========== 后台模式: 直接进入监控循环, 不再重复初始加载 ========== */
    if (is_background) {
        monitor_loop();   /* noreturn (不退出, 不返回) */
        return;           /* 理论 unreachable, 仅消除 warning */
    }

    /* ========== 同步阶段 (第一次运行): 初始驱动加载 ========== */
    draw_banner();
    API->print(TR("Scanning /EFMOS/DRIVERS for .drv files...\n",
                  "正在扫描 /EFMOS/DRIVERS 目录寻找 .drv 驱动文件...\n"));

    /* 列出目录内容 */
    struct efs_dirent ents[64];
    int n = API->file_list("/EFMOS/DRIVERS", ents, 64);
    if (n < 0) {
        API->print(TR("Warning: /EFMOS/DRIVERS not found.\n",
                      "警告: 未找到 /EFMOS/DRIVERS 目录。\n"));
    }

    /* 逐个加载驱动 (过滤 + 排序 + 加载) */
    int ok = 0, failed = 0;
    if (n > 0) {
        struct efs_dirent filtered[64];
        int fn = 0;
        for (int i = 0; i < n; i++) {
            if (ents[i].is_dir) continue;
            if (!ends_with_drv(ents[i].name)) continue;
            filtered[fn++] = ents[i];
            if (fn >= 64) break;
        }
        sort_entries(filtered, fn);

        API->print(TR("Found ", "找到 "));
        print_dec(fn);
        API->print(TR(" driver file(s). Loading sequentially...\n\n",
                      " 个驱动文件, 按顺序加载中...\n\n"));

        for (int i = 0; i < fn; i++) {
            char path[256];
            build_drv_path(path, sizeof(path), filtered[i].name);

            API->print(TR("  [", "  [")); print_dec(i+1); API->print("/"); print_dec(fn);
            API->print("] "); API->print(filtered[i].name); API->print(" ... ");

            int rc = API->load_driver(path);
            if (rc >= 0) {
                API->print(TR("OK (registered: ", "成功 (注册驱动数: "));
                print_dec(rc);
                API->print(")\n");
                ok++;
            } else {
                API->print(TR("FAIL (rc=", "失败 (错误码="));
                print_dec(rc);
                API->print(")\n");
                failed++;
            }
            /* 每次加载后 yield 一下, 避免早期定时器任务饿死 */
            if (API->yield) API->yield();
        }
    }

    API->print("\n");
    API->print(TR("----------------------------------------\n",
                  "----------------------------------------\n"));
    API->print(TR("Summary: ", "加载汇总: "));
    print_dec(ok);
    API->print(TR(" loaded, ", " 成功, "));
    print_dec(failed);
    API->print(TR(" failed, total drivers in registry = ",
                  " 失败, 当前注册表驱动总数 = "));
    if (API->driver_count) print_dec(API->driver_count());
    else API->print("?");
    API->print("\n");

    /* 若有注册驱动, 额外列出已注册驱动名 (调试) */
    if (API->driver_list && API->driver_count && API->driver_count() > 0) {
        API->print(TR("Registered drivers:\n", "已注册驱动列表:\n"));
        char names[16][32];
        int cnt = API->driver_list(names, 16);
        for (int i = 0; i < cnt; i++) {
            API->print("    - "); API->print(names[i]); API->print("\n");
        }
    }

    /* ========== 同步阶段结束: spawn 一个后台监控线程, 然后自己退出 ==========
     * 关键: 此时已经在 EFS run_efs_ex 上下文中 (非 kmain 早期), spawn_async 创建的
     * 新 EFS 线程进入的调度器环境, 已经经过若干次 EFS 生命周期切换, 稳定可靠。*/
    if (API->spawn_async) {
        int bg_pid = API->spawn_async("efmloader", "--background");
        if (bg_pid > 0) {
            API->print(TR("Background monitor started (pid=",
                          "后台监控线程已启动 (pid="));
            print_dec(bg_pid);
            API->print(")\n");
        } else {
            API->print(TR("Warning: failed to start background monitor.\n",
                          "警告: 启动后台监控线程失败。\n"));
        }
    }
    if (API->sleep_ms) API->sleep_ms(500);
}
