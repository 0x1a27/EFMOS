#ifndef _EFMLIBC_STRINGS_H
#define _EFMLIBC_STRINGS_H
#ifdef __cplusplus
#include_next <strings.h>
#else
#include <sys/types.h>
void  bzero(void *d, size_t n);
void  bcopy(const void *s, void *d, size_t n);
int   bcmp(const void *a, const void *b, size_t n);
int   strcasecmp(const char *a, const char *b);
int   strncasecmp(const char *a, const char *b, size_t n);
#endif
#endif
