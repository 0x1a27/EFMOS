#ifndef _EFMLIBC_SYS_PARAM_H
#define _EFMLIBC_SYS_PARAM_H
#ifdef __cplusplus
#include_next <sys/param.h>
#else
#include <sys/types.h>

#define MAXPATHLEN 4096
#define MAXSYMLINKS 20
#define MAXHOSTNAMELEN 256

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define roundup(x, y) ((((x)+((y)-1))/(y))*(y))
#define howmany(x, y) (((x)+((y)-1))/(y))
#define powerof2(x) (((x)&((x)-1))==0)

#define NBBY    8
#define NBPG    4096
#define DEV_BSIZE 512

#endif
#endif
