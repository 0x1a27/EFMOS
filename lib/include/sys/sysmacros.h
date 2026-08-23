#ifndef _EFMLIBC_SYS_SYSMACROS_H
#define _EFMLIBC_SYS_SYSMACROS_H
/* EFMOS: 提供 glibc 风格的 major()/minor()/makedev().
 * 实现为普通函数符号 (位于 libefmlibc.a), 不使用宏以避免与函数声明冲突.
 * loader.c / 其它 Linux 代码 include <sys/sysmacros.h> 或 <sys/types.h> 后即可调用. */
#include <sys/types.h>

int   major(dev_t dev);
int   minor(dev_t dev);
dev_t makedev(unsigned int maj, unsigned int min);
#endif
