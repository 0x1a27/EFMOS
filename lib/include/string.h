#ifndef _EFMLIBC_STRING_H
#define _EFMLIBC_STRING_H
#ifdef __cplusplus
#include_next <string.h>
#else
#include <sys/types.h>
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
void *memrchr(const void *s, int c, size_t n);
void *memmem(const void *h, size_t hl, const void *n, size_t nl);
void *mempcpy(void *dst, const void *src, size_t n);
int   ffs(int i);
int   ffsl(long i);
int   ffsll(long long i);
void  explicit_bzero(void *d, size_t n);
void  bzero(void *d, size_t n);
void  bcopy(const void *s, void *d, size_t n);
int   bcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
char *strdup(const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strchrnul(const char *s, int c);
char *strstr(const char *hay, const char *needle);
size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);
char *strpbrk(const char *s, const char *accept);
char *strtok(char *s, const char *delim);
char *strtok_r(char *s, const char *delim, char **saveptr);
size_t strnlen(const char *s, size_t maxlen);
char *strndup(const char *s, size_t n);
int   strcasecmp(const char *a, const char *b);
int   strncasecmp(const char *a, const char *b, size_t n);
int   strcoll(const char *a, const char *b);
size_t strxfrm(char *d, const char *s, size_t n);
char *strerror(int errnum);
int   strerror_r(int errnum, char *buf, size_t buflen);
char *strsignal(int sig);
void *rawmemchr(const void *s, int c);
void *explicit_memset(void *s, int c, size_t n);
#endif
#endif
