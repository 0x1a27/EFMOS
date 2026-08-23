/* ========================================================================
 * EFMOS GPU 固件加载兼容层 (libefmfw.a)
 *
 * 目标: 为 Mesa 的 Gallium3D 硬件驱动 winsys 提供 request_firmware 接口.
 *       swrast (软件光栅化) 不需要 GPU 固件, 但 Mesa 构建时会引用
 *       固件加载符号 (尤其 kmsro 后端和 iris/amdgpu winsys), 必须有 stub.
 *
 * 策略:
 *   - request_firmware(name, ...) → 从 /EFMOS/FIRMWARE/ 读取固件文件
 *     (如果存在); 否则返回空 blob (swrast 不会真正使用固件数据)
 *   - release_firmware → free
 *   - firmware_loading_store / fw_state → no-op (无内核固件加载器)
 *
 * 同时提供 EFMOS 固件目录结构约定:
 *   /EFMOS/FIRMWARE/           ← 固件根目录 (对应 Linux /lib/firmware)
 *   /EFMOS/FIRMWARE/i915/      ← Intel GPU 固件 (swrast 不需要)
 *   /EFMOS/FIRMWARE/amdgpu/    ← AMD GPU 固件 (swrast 不需要)
 *   /EFMOS/FIRMWARE/nvidia/    ← NVIDIA GPU 固件 (swrast 不需要)
 *
 * 如果未来需要硬件加速, 把 .bin 固件放到对应目录即可被加载.
 * ======================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 内部字符串工具前置声明 (避免 implicit declaration) ---------- */
static int  strlen_h(const char *s);
static void strcpy_h(char *d, const char *s);
static void strcat_h(char *d, const char *s);
static void strncpy_h(char *d, const char *s, int n);

/* ---------- 固件对象 ---------- */
struct firmware {
    size_t   size;        /* 固件数据字节大小 */
    uint8_t *data;        /* 固件数据 (malloc 分配, release 时 free) */
    char     name[256];   /* 固件名 (如 "i915/skl_dmc_ver1.bin") */
};

/* 内核 API (0x9000) */
struct __efm_dirent { char name[256]; int is_dir; long size; };
struct __efm_kernel_api {
    unsigned int magic;
    unsigned int _pad;
    void (*put_char)(char);
    void (*print)(const char*);
    void (*print_utf8)(const char*);
    void (*clear_screen)(void);
    int  (*file_read)(const char*, char*, int);
    int  (*file_write)(const char*, const char*, int);
    int  (*file_exists)(const char*);
    int  (*mkdir)(const char*);
    int  (*readline)(char*, int);
    void (*reboot)(void);
    int  (*get_lang)(void);
    void (*set_lang)(int);
    int  (*save_settings)(void);
    int  (*mouse_poll)(void *out_event);
    void (*mouse_set_cursor)(int show);
    int  (*file_list)(const char *dir, struct __efm_dirent *out, int max);
    int  (*file_delete)(const char *path);
    int  (*key_poll)(void);
    int  (*get_current_user)(char *buf, int bufsz);
    int  (*set_current_user)(const char *username);
    int  (*user_list)(struct __efm_dirent *out, int max);
    int  (*user_create)(const char *username);
    int  (*user_delete)(const char *username);
    void *(*malloc_fn)(unsigned long);
    void  (*free_fn)(void*);
    int   (*spawn)(const char *name, const char *args);
    int   (*get_args)(char *buf, int max);
    int   font_w, font_h;
    int   current_pid, wm_enabled;
    void (*put_pixel)(int x, int y, unsigned int c);
    void (*fill_rect)(int x1, int y1, int x2, int y2, unsigned int c);
    void (*draw_rect)(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill);
    void (*get_viewport)(int *cx, int *cy, int *cw, int *ch);
    void (*get_fb_info)(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base);
    int   (*load_driver)(const char *path);
    int   (*driver_count)(void);
    int   (*driver_list)(char out_names[][32], int max);
    void  (*sleep_ms)(unsigned long ms);
    void  (*yield)(void);
    int   (*get_pid)(void);
    int   (*spawn_async)(const char *name, const char *args);
    int   (*set_priority)(int pid, int nice);
};
#define EFM_API_MAGIC 0xEF110001
#define EFM_API ((volatile struct __efm_kernel_api *const)0x9000UL)

#define EFM_FIRMWARE_ROOT  "/EFMOS/FIRMWARE"

/* ---------- request_firmware: 加载固件 ---------- */
/* 对应 Linux 内核 request_firmware(fw_p, name, device)
 *   返回 0=成功, <0=失败
 *   swrast 不会调用此函数, 但 Mesa iris/amdgpu winsys 编译时引用. */
int request_firmware(const struct firmware **fw_p, const char *name, void *device) {
    (void)device;
    if (!fw_p || !name) return -1;

    /* 构造完整路径: /EFMOS/FIRMWARE/<name> */
    char path[512];
    int root_len = strlen_h(EFM_FIRMWARE_ROOT);
    memcpy(path, EFM_FIRMWARE_ROOT, root_len);
    path[root_len] = '/';
    int name_len = strlen_h(name);
    if (root_len + 1 + name_len >= (int)sizeof(path) - 1) return -1;
    memcpy(path + root_len + 1, name, name_len + 1);

    struct firmware *fw = (struct firmware*)malloc(sizeof(*fw));
    if (!fw) return -1;
    memset(fw, 0, sizeof(*fw));
    strncpy_h(fw->name, name, sizeof(fw->name) - 1);

    /* 尝试从文件系统读取固件 */
    if (EFM_API->magic == EFM_API_MAGIC && EFM_API->file_exists(path)) {
        /* 先读 4MB 上限 (GPU 固件通常 < 2MB) */
        int max_sz = 4 * 1024 * 1024;
        char *buf = (char*)malloc(max_sz);
        if (!buf) { free(fw); return -1; }
        int rd = EFM_API->file_read(path, buf, max_sz);
        if (rd > 0) {
            fw->data = (uint8_t*)buf;
            fw->size = (size_t)rd;
            *fw_p = (const struct firmware*)fw;
            return 0;
        }
        free(buf);
    }

    /* 固件文件不存在 → 返回空 blob (swrast 不需要真正固件数据)
     * 这让 Mesa 编译通过且运行时不崩溃. 硬件驱动会检测空固件并回退. */
    fw->data = (uint8_t*)malloc(1);
    if (fw->data) fw->data[0] = 0;
    fw->size = 0;
    *fw_p = (const struct firmware*)fw;
    return 0;
}

/* ---------- request_firmware_direct: 不走缓存 ---------- */
int request_firmware_direct(const struct firmware **fw_p, const char *name, void *device) {
    return request_firmware(fw_p, name, device);
}

/* ---------- firmware_request_nowarn: 不打印警告 ---------- */
int firmware_request_nowarn(const struct firmware **fw_p, const char *name, void *device) {
    return request_firmware(fw_p, name, device);
}

/* ---------- request_firmware_into_buf: 读入调用者提供的 buffer ---------- */
int request_firmware_into_buf(const struct firmware **fw_p, const char *name,
                               void *device, void *buf, size_t size) {
    int r = request_firmware(fw_p, name, device);
    if (r != 0 || !*fw_p) return r;
    const struct firmware *fw = *fw_p;
    if (fw->data && fw->size > 0 && buf) {
        size_t n = fw->size < size ? fw->size : size;
        memcpy(buf, fw->data, n);
    }
    return 0;
}

/* ---------- release_firmware: 释放固件 ---------- */
void release_firmware(const struct firmware *fw) {
    if (!fw) return;
    struct firmware *f = (struct firmware*)fw;
    if (f->data) free(f->data);
    free(f);
}

/* ---------- firmware loading state (内核固件加载器接口) ---------- */
/* Mesa 某些 winsys 引用 firmware_loading_store / fw_state 等.
 * EFMOS 没有内核固件加载器, 全部返回 "done" 状态. */
int firmware_loading_store(void *dev, const char *buf, size_t count) {
    (void)dev; (void)buf; (void)count;
    return 0;
}

int fw_state_init(void *fw_sysfs) {
    (void)fw_sysfs;
    return 0;
}

void fw_state_fini(void *fw_sysfs) {
    (void)fw_sysfs;
}

/* ---------- firmware_class 注册 (占位) ---------- */
void *firmware_class_init(void) {
    return NULL;
}
void firmware_class_exit(void *cls) {
    (void)cls;
}

/* ---------- EFMOS 扩展: 列出已安装固件 ---------- */
int efm_fw_list_installed(char out_names[][256], int max_count) {
    if (EFM_API->magic != EFM_API_MAGIC) return 0;
    struct __efm_dirent ents[64];
    int n = EFM_API->file_list(EFM_FIRMWARE_ROOT, ents, 64);
    if (n < 0) return 0;
    int cnt = 0;
    for (int i = 0; i < n && cnt < max_count; i++) {
        if (!ents[i].is_dir) {
            strncpy_h(out_names[cnt], ents[i].name, 255);
            cnt++;
        }
    }
    return cnt;
}

/* ---------- EFMOS 扩展: 创建固件目录 ---------- */
int efm_fw_init_directory(void) {
    if (EFM_API->magic != EFM_API_MAGIC) return -1;
    EFM_API->mkdir(EFM_FIRMWARE_ROOT);
    char path[256];
    strcpy_h(path, EFM_FIRMWARE_ROOT);
    strcat_h(path, "/i915");
    EFM_API->mkdir(path);
    strcpy_h(path, EFM_FIRMWARE_ROOT);
    strcat_h(path, "/amdgpu");
    EFM_API->mkdir(path);
    strcpy_h(path, EFM_FIRMWARE_ROOT);
    strcat_h(path, "/nvidia");
    EFM_API->mkdir(path);
    return 0;
}

/* ---------- 内部字符串工具 (定义) ---------- */
int strlen_h(const char *s) {
    int n = 0; while (*s++) n++; return n;
}
void strcpy_h(char *d, const char *s) {
    while ((*d++ = *s++)) ;
}
void strcat_h(char *d, const char *s) {
    d += strlen_h(d);
    while ((*d++ = *s++)) ;
}
void strncpy_h(char *d, const char *s, int n) {
    int i = 0; while (i < n && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
