#ifndef _EFMLIBC_UNISTD_H
#define _EFMLIBC_UNISTD_H
#ifdef __cplusplus
#include_next <unistd.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
void   *sbrk(ptrdiff_t incr);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     close(int fd);
off_t   lseek(int fd, off_t offset, int whence);
pid_t   getpid(void);
pid_t   gettid(void);
uid_t   getuid(void);
uid_t   geteuid(void);
gid_t   getgid(void);
gid_t   getegid(void);
int     usleep(useconds_t us);
unsigned sleep(unsigned s);
/* sysconf 定义在后面 (POSIX long 版本) */
int     getpagesize(void);
int     pipe(int fds[2]);
int     dup2(int a, int b);
pid_t   fork(void);
int     execve(const char *p, char *const a[], char *const e[]);
pid_t   wait(int *s);
pid_t   waitpid(pid_t p, int *s, int o);
int     unlink(const char *path);
int     ftruncate(int fd, off_t length);
int     truncate(const char *path, off_t length);
int     fsync(int fd);
int     fdatasync(int fd);
void    sync(void);
int     flock(int fd, int operation);
int     link(const char *oldpath, const char *newpath);
int     symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int     chmod(const char *path, mode_t mode);
int     fchmod(int fd, mode_t mode);
int     chown(const char *path, uid_t owner, gid_t group);
int     fchown(int fd, uid_t owner, gid_t group);
int     access(const char *path, int mode);
int     faccessat(int dirfd, const char *pathname, int mode, int flags);
int     rmdir(const char *path);
int     gethostname(char *name, size_t len);
int     sethostname(const char *name, size_t len);
int     getdomainname(char *name, size_t len);
int     setdomainname(const char *name, size_t len);
int     chdir(const char *path);
int     fchdir(int fd);
char   *getcwd(char *buf, size_t size);
int     isatty(int fd);
ssize_t writev(int fd, const void *iov, int iovcnt);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int     dup(int oldfd);
int     close_range(unsigned int first, unsigned int last, int flags);
int     memfd_create(const char *name, unsigned int flags);
int     eventfd(unsigned int initval, int flags);
int     eventfd_read(int fd, uint64_t *value);
int     eventfd_write(int fd, uint64_t value);
int     getdtablesize(void);
int     nice(int inc);
uid_t   geteuid(void);
gid_t   getegid(void);
int     setuid(uid_t uid);
int     setgid(gid_t gid);
pid_t   setsid(void);
pid_t   getsid(pid_t pid);
pid_t   getpgrp(void);
int     setpgid(pid_t pid, pid_t pgid);
int     setpgrp(void);
int     tcsetpgrp(int fd, pid_t pgrp);
pid_t   tcgetpgrp(int fd);
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#define MFD_CLOEXEC       0x0001U
#define MFD_ALLOW_SEALING 0x0002U
#define MFD_HUGETLB       0x0004U
#define EFD_SEMAPHORE     1
#define EFD_CLOEXEC       0x80000
#define EFD_NONBLOCK      0x800
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#ifndef AT_FDCWD
#define AT_FDCWD            (-100)
#endif
#define AT_EACCESS          0x200
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR        0x200
#define AT_SYMLINK_FOLLOW   0x400
#define _SC_NPROCESSORS_ONLN    1
#define _SC_NPROCESSORS_CONF    2
#define _SC_PAGESIZE            3
#define _SC_PAGE_SIZE           _SC_PAGESIZE
#define _SC_PHYS_PAGES          4
#define _SC_AVPHYS_PAGES        5
#define _SC_ARG_MAX             6
#define _SC_CHILD_MAX           7
#define _SC_CLK_TCK             8
#define _SC_OPEN_MAX            9
#define _SC_STREAM_MAX          10
#define _SC_TZNAME_MAX          11
#define _SC_VERSION             12
#define _SC_XOPEN_VERSION       13
#define _SC_MONOTONIC_CLOCK     14
#define _SC_GETPW_R_SIZE_MAX    15
#define _SC_GETGR_R_SIZE_MAX    16
#define _SC_HOST_NAME_MAX       17
#define _SC_TTY_NAME_MAX        18
#define _SC_LINE_MAX            19
#define _SC_RTSIG_MAX           20
#define _SC_SEM_NSEMS_MAX       21
#define _SC_SEM_VALUE_MAX       22
#define _SC_SIGQUEUE_MAX        23
#define _SC_TIMER_MAX           24
#define _SC_2_VERSION           25
#define _SC_2_C_BIND            26
#define _SC_2_C_DEV             27
#define _SC_SYSMALLOC           28
#define PAGE_SIZE               4096
long sysconf(int name);
#define STDIN_FILENO         0
#define STDOUT_FILENO        1
#define STDERR_FILENO        2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#endif
