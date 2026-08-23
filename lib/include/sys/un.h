#ifndef _EFMLIBC_SYS_UN_H
#define _EFMLIBC_SYS_UN_H
#ifdef __cplusplus
#include_next <sys/un.h>
#else
#include <sys/socket.h>

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[108];
};
#define UNIX_PATH_MAX 108
#endif
#endif
