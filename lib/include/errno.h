#ifndef _EFMLIBC_ERRNO_H
#define _EFMLIBC_ERRNO_H
#ifdef __cplusplus
#include_next <errno.h>
#else
extern int *__efm_errno_loc(void);
#define errno (*__efm_errno_loc())
#define EINVAL      22
#define ENOMEM      12
#define ENOENT      2
#define EACCES      13
#define EEXIST      17
#define ENOTDIR     20
#define EISDIR      21
#define EIO         5
#define EBADF       9
#define ENOSYS      38
#define EAGAIN      11
#define EBUSY       16
#define ETIMEDOUT   110
#define EOVERFLOW   75
#define EFAULT      14
#define EINTR        4
#define EPERM        1
#define ERANGE      34
#define ENFILE      23
#define EMFILE      24
#define ENAMETOOLONG 36
#define ELOOP       40
#define ENOSPC      28
#define EDQUOT     122
#define EROFS       30
#define ECANCELED  125
#define ENOTEMPTY   39
#define EDEADLK     36
#define ECHILD      10
/* ETIME is passed via Mesa compiler -DETIME=ETIMEDOUT workaround; don't define here */
#define ENOTSUP     95   /* not supported */
#define EPROTONOSUPPORT 93 /* protocol not supported */
#define EPIPE       32
#define ENODEV      19
#define ENXIO        6
#define ENOTTY      25
#define EAFNOSUPPORT 97
#define EPROTOTYPE   91
#define EADDRINUSE   98
#define ECONNREFUSED 111
#define ECONNRESET   104
#define EHOSTUNREACH 113
#define ENETUNREACH  101
#define ECONNABORTED 103
#define EMSGSIZE     90
#define ENOTCONN    107
#define ESHUTDOWN   108
#define ETOOMANYREFS 109
#define EUSERS      87
#define EDOM        33
#define EILSEQ      84
#define ENOATTR     ENODATA
#define ENODATA     61
#define ENOMSG      42
#define ENOLINK     67
#define ENOTSOCK    88
#define EOPNOTSUPP  95   /* ENOTSUP alias (POSIX) */
#define EWOULDBLOCK EAGAIN
#define EDESTADDRREQ 89
#define EISCONN     106
#endif
#endif
