#ifndef EFMOS_EFM_API_H
#define EFMOS_EFM_API_H

/* EFMOS 内核 API 表 —— 全系统唯一权威定义。
 * EFS 程序 (.efs) 与内核必须使用本头文件, 不得再各自复制结构体, 否则字段漂移会导致 ABI 偏移 bug。 */

/* 目录条目 (内核 file_list/user_list 回填用) */
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
    /* [新增 3.0] 鼠标 API:
     *   mouse_poll:   非阻塞取一个鼠标事件, 1=成功 out=填充, 0=无事件
     *   mouse_set_cursor: show=1 显示/0=隐藏 内核绘制的箭头光标
     * mouse_event 结构 (.efs 程序侧要字段对齐完全一致):
     *   struct { int dx,dy; unsigned char btn; int x,y; };  dx/dy 相对位移, btn bit0=L 1=R 2=M, x/y 绝对坐标 */
    int  (*mouse_poll)(void *out_event);
    void (*mouse_set_cursor)(int show);
    /* [新增] 文件管理 API:
     *   file_list:  列出目录内容到 out 数组, 返回条目数 (>=0) 或 -1
     *   file_delete: 删除一个普通文件, 返回 0=成功 -1=失败 */
    int  (*file_list)(const char *dir_path, struct efs_dirent *out, int max_count);
    int  (*file_delete)(const char *path);
    /* [新增] 非阻塞键盘输入 API (统一由内核 classify 硬件字节, 避免 .efs 程序 IN 0x60 和内核抢读):
     *   返回值: 0=无按键; 正数=可打印 ASCII (0x20..0x7E); 负数=特殊键:
     *        -101=Enter  -102=Backspace  -103=ESC */
    int  (*key_poll)(void);
    /* ========== 用户系统 API (User Management) ========== */
    /* get_current_user: 把当前登录用户名写入 buf (最多 bufsz-1 字节),
     *   返回写入字节数 (>=0), -1=未登录或失败 */
    int  (*get_current_user)(char *buf, int bufsz);
    /* set_current_user: 切换当前用户为 username, 返回 0=成功 -1=用户不存在
     *   (会更新 shell 默认目录到 /users/<username>) */
    int  (*set_current_user)(const char *username);
    /* user_list: 列出 /users 下的用户目录, out[i].name 为用户名,
     *   out[i].is_dir 始终为 1, out[i].size 为该用户目录大小。
     *   返回条目数 (>=0), -1=失败。 */
    int  (*user_list)(struct efs_dirent *out, int max_count);
    /* user_create: 创建新用户 (在 /users 下创建 <username> 目录).
     *   返回 0=成功, -1=失败 (用户已存在 / 无效名) */
    int  (*user_create)(const char *username);
    /* user_delete: 删除用户 (删除 /users/<username> 目录, 要求为空目录)
     *   返回 0=成功, -1=失败 (当前用户不可删除/非空/不存在) */
    int  (*user_delete)(const char *username);
    /* ========== 扩展 API (为编译器等大型程序准备) ==========
     * malloc/free: 内核堆动态内存 (32MB~64MB 区域)
     * spawn: 运行另一个 /EFMOS/*.efs 并等待其返回 (efcc 调用 as/ld 的基础)
     * get_args: 取本程序启动时的命令行参数串 (程序名之后的参数, 空格分隔) */
    void *(*malloc)(unsigned long);
    void  (*free)(void*);
    int   (*spawn)(const char *name, const char *args);
    int   (*get_args)(char *buf, int max);
    /* 2024+ 扩展: 字体像素尺寸 (ASCII). CJK 宽 = 2*font_w, 高 = font_h.
     * 用于 .efs 程序计算按钮/布局坐标, 替代硬编码 8/16. */
    int font_w;
    int font_h;
    /* 2025+ 窗口系统:
     *   current_pid: 本 EFS 进程所属 pid (由 efs_setup_api_table / WM 启动时写入)
     *   wm_enabled:  内核是否处于窗口模式 (1=窗口化, 0=传统全屏)
     * 当 wm_enabled=1 且 current_pid>0 时, api->put_char/print/clear_screen
     * 会自动重定向到 current_pid 绑定的窗口内容区 (由内核 wm_redirect_* 实现)。 */
    int current_pid;
    int wm_enabled;
    /* 2025+ 窗口像素绘制 API:
     *   当 wm_enabled=1 时, 所有坐标以当前进程的窗口内容区为参考系 (左上=(0,0)),
     *   并被裁剪到内容区内; 当 wm_enabled=0 时, 坐标为全局 FB 绝对坐标 (向后兼容)。
     * 没有这些 API 的旧版 .efs 程序直接写 0x1000 (GOP FB struct) 会破坏窗口画面,
     *   建议所有程序切换到这些安全 API。 */
    void (*put_pixel)(int x, int y, unsigned int c);
    void (*fill_rect)(int x1, int y1, int x2, int y2, unsigned int c);
    void (*draw_rect)(int x1, int y1, int x2, int y2, unsigned int border, unsigned int fill);
    /* 取当前进程"虚拟屏幕"几何:
     *   wm_enabled=1  → 返回内容区 (cx,cy) 为全局 FB 偏移, (cw,ch) 为尺寸
     *   wm_enabled=0  → 返回 (0,0, hr, vr)
     * 程序如果需要在全屏 vs 窗口化模式下自适应布局, 可以用这些值决定绘制区域。 */
    void (*get_viewport)(int *cx, int *cy, int *cw, int *ch);
    /* 取底层 GOP fb 信息 (只读, 不要直接写 fb_base; 用 put_pixel/fill_rect 安全写入) */
    void (*get_fb_info)(unsigned int *hr, unsigned int *vr, unsigned int *ppsl, unsigned int **fb_base);
    /* [Mesa 集成] 批量 blit: 把外部像素缓冲区写入当前进程窗口的内容区.
     * 用途: Mesa swrast 渲染完一帧后, eglSwapBuffers 调用此函数把结果写入
     *       Graphics.drv 的 back buffer (而非直接写 front), 保持双缓冲一致性.
     * 参数: src=源像素, src_w/h=源宽高, src_pitch=源每行字节数
     * 返回: 0=成功, -1=失败
     * WM 感知: wm_enabled=1 时自动重定向到窗口内容区并裁剪 */
    int  (*blit_to_window)(const void *src, int src_w, int src_h, int src_pitch);
    /* ========== 驱动子系统 API (EFMOS 2026) ==========
     * load_driver: 读取磁盘上 <path> 指定的 .drv 文件, 加载到其头部 load_addr,
     *   调用入口 (drv_entry) 并通过内部 iface 注册其 ops。返回注册的驱动数/非负=ok,
     *   -1=文件找不到, -2=头部非法, -3=加载地址冲突, -4=注册失败。
     * driver_count: 当前 registry 内驱动数 (用于 ps/drv 调试输出)
     * driver_list:  复制驱动名 (每个 32 字节) 到 out_names, 返回条目数 (最多 max) */
    int   (*load_driver)(const char *path);
    int   (*driver_count)(void);
    int   (*driver_list)(char out_names[][32], int max);
    /* ========== 2026+ 多线程调度 API ==========
     *   sleep_ms:  当前任务睡眠至少 ms 毫秒 (TSC 近似精度)
     *   yield:     主动让出 CPU (不睡眠, 立刻可被再次调度)
     *   get_pid:   返回当前任务 pid
     *   spawn_async: 创建新内核线程运行另一个 /EFMOS/*.efs (不等返回, 返回子 pid)
     *   set_priority(pid, nice): 调整指定任务 nice [0..39], 0=最高优先级 */
    void  (*sleep_ms)(unsigned long ms);
    void  (*yield)(void);
    int   (*get_pid)(void);
    int   (*spawn_async)(const char *name, const char *args);
    int   (*set_priority)(int pid, int nice);
    /* ========== 2026+ Mesa 合成器 API ==========
     * get_wm_snapshot: 把当前 WM 窗口状态 + 鼠标状态拷贝到 out 缓冲区.
     *   返回窗口数 (>=0), -1=失败. out 必须足够大 (见 efm_wm_snapshot).
     * set_compositor_active: 通知内核合成器已接管渲染 (1=接管, 0=交还内核).
     *   合成器接管后, 内核 wm_composite 不再直接渲染, 仅维护窗口状态. */
    int   (*get_wm_snapshot)(void *out, int max_bytes);
    void  (*set_compositor_active)(int active);
    void *(*dlsym)(const char *name);   /* 从已加载 SO 解析符号 (给合成器调用 Mesa EGL/GL) */
    /* [优化] 获取 Graphics.drv 的 back buffer 指针, 让 Mesa swrast 直接渲染到 back buffer.
     * 消除 BO→back buffer 全屏拷贝. 返回 0=成功. */
    int  (*get_backbuffer)(void **out_ptr, int *out_pitch, int *out_w, int *out_h);
    /* [优化] 标记 back buffer 脏矩形 (inclusive) + 立即 flush 到 front.
     * 直接映射模式下 swrast 直接写 back buffer, 需手动标记脏区并 flush. */
    void (*mark_dirty_rect)(int x1, int y1, int x2, int y2);
    void (*flush_now)(void);
    /* [2026+ TTF] Unicode 字符渲染 (TTF 字体, 透明 alpha 混合).
     * bg=0xFEEDFACE 时为透明模式: 不画背景方块, 字形 alpha 混合到现有像素.
     * 返回字符实际像素宽度 (0=失败/不支持). cell_w/cell_h<=0 时只画字形不限制单元. */
    int  (*draw_char_unicode)(int x, int y, unsigned int codepoint,
                              unsigned int fg, unsigned int bg, int cell_w, int cell_h);
    /* [新架构] 设置当前进程窗口的 EFS 图形信息指针.
     * EFS 图形程序 (efmlogin/userman/fileman/setting) 分配 efm_gfx_info 结构,
     * 填写矩形+文字信息后调用此 API, 内核把指针存入窗口, efmcompositor 通过
     * get_wm_snapshot 读取并渲染. EFS 程序不再直接写 back buffer. */
    void (*set_gfx_info)(void *info);
    /* [efmshell 独立] chdir: 切换内核全局 CWD (current_dir_ino + current_path).
     * 供独立 shell 程序 (efmshell.efs) 同步自己的 cwd, 使 run_efs 的 CWD 查找
     * 和 resolve_inode 的相对路径解析与 shell 视角一致. 返回 0=成功 -1=失败. */
    int  (*chdir)(const char *path);
    /* [efmshell 全功能移植] 增强行编辑引擎: 原 kernel 内建 shell_loop 的输入部分
     * (历史 Up/Down, 行内 Left/Right/Home/End, Backspace/Delete, Esc 清行,
     *  Tab 补全, 闪烁光标) 移植为内核 API, 在 EFS 任务上下文内执行.
     * prompt 由本函数绘制 (整行重绘需要). hist_get: idx 0=最近, NULL=无更多.
     * complete: 收集 prefix 候选到 matches (每条 match_len 字节), 返回数量.
     * 返回输入长度; Esc 中止返回 -1. */
    int  (*readline_ext)(const char *prompt, char *buf, int max,
                         const char *(*hist_get)(int idx),
                         int (*complete)(const char *prefix, char *matches, int max_n, int match_len));
    /* [efmshell 全功能移植] 多行文本编辑器 (write/append/edit 命令).
     * 可视化编辑: 跨行光标移动, Backspace/Delete, ENDOFFILE 结束符, Esc 取消.
     * 控制台模式完整移植原 kb_read_text; WM 窗口模式为逐行编辑版.
     * 返回文本长度; Esc 取消返回 -1. */
    int  (*read_text)(char *buf, int max);
    /* [efmshell 全功能移植] 文件系统/系统杂项操作 (无法经现有扁平 API 表达的操作):
     *   "ln" a=target b=linkname -> ext4_symlink (返回 ino/负值)
     *   "readlink" a=link out=target
     *   "truncate" a=file b=size(十进制)
     *   "rmdir" a=dir
     *   "sync" -> disk_flush
     *   "ps" out=任务列表文本    "wminfo" out=WM 状态文本
     *   "sysinfo" out=struct efm_sysinfo (二进制, 见实现处定义)
     * 返回 0=成功 (文本 op 返回字节数), 负值=失败, -2=未知 op. */
    int  (*fsop)(const char *op, const char *a, const char *b, char *out, int outsz);
};

#define EFM_API_MAGIC  0xEF110001u
#ifndef EFS_API_MAGIC
#define EFS_API_MAGIC  EFM_API_MAGIC
#endif
#ifndef API_MAGIC
#define API_MAGIC      EFM_API_MAGIC
#endif
/* EFS 程序访问内核 API 表的固定入口地址 */
#define API            ((struct kernel_api*)0x9000)

#endif /* EFMOS_EFM_API_H */