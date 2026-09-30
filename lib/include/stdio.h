/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_STDIO_H
#define _EFMLIBC_STDIO_H
#ifdef __cplusplus
#include_next <stdio.h>
#else
#include <stdarg.h>
#include <sys/types.h>
typedef struct { long opaque[16]; } FILE;
#define stdin   ((FILE*)0x1)
#define stdout  ((FILE*)0x2)
#define stderr  ((FILE*)0x3)
#define EOF     (-1)
int   printf(const char *fmt, ...);
int   fprintf(FILE *f, const char *fmt, ...);
int   snprintf(char *buf, size_t sz, const char *fmt, ...);
int   sprintf(char *buf, const char *fmt, ...);
int   vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap);
int   vprintf(const char *fmt, va_list ap);
int   vfprintf(FILE *stream, const char *fmt, va_list ap);
FILE *fopen(const char *path, const char *mode);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int   fclose(FILE *stream);
int   fflush(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int   fputs(const char *s, FILE *stream);
int   puts(const char *s);
void  perror(const char *s);
int   remove(const char *path);
int   asprintf(char **strp, const char *fmt, ...);
int   vasprintf(char **strp, const char *fmt, va_list ap);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);
char *fgetln(FILE *stream, size_t *len);
int   rename(const char *oldpath, const char *newpath);
int   renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
int   fileno(FILE *stream);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
FILE *tmpfile(void);
char *tmpnam(char *s);
int   setvbuf(FILE *stream, char *buf, int mode, size_t size);
void  setbuf(FILE *stream, char *buf);
int   fseek(FILE *stream, long offset, int whence);
long  ftell(FILE *stream);
void  rewind(FILE *stream);
int   fgetpos(FILE *stream, void *pos);
int   fsetpos(FILE *stream, const void *pos);
int   feof(FILE *stream);
int   ferror(FILE *stream);
void  clearerr(FILE *stream);
int   ungetc(int c, FILE *stream);
int   fgetc(FILE *stream);
int   getc(FILE *stream);
int   getchar(void);
int   fputc(int c, FILE *stream);
int   putc(int c, FILE *stream);
int   putchar(int c);
size_t fread_unlocked(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite_unlocked(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int   fflush_unlocked(FILE *stream);
int   feof_unlocked(FILE *stream);
int   ferror_unlocked(FILE *stream);
int   fgetc_unlocked(FILE *stream);
int   fputc_unlocked(int c, FILE *stream);
void  flockfile(FILE *stream);
void  funlockfile(FILE *stream);
int   ftrylockfile(FILE *stream);
FILE *popen(const char *command, const char *mode);
int   pclose(FILE *stream);
FILE *open_memstream(char **ptr, size_t *sizeloc);
FILE *fmemopen(void *buf, size_t size, const char *mode);
FILE *open_wmemstream(void **ptr, size_t *sizeloc);
ssize_t vdprintf(int fd, const char *fmt, va_list ap);
int  dprintf(int fd, const char *fmt, ...);
/* scanf family: 简化 stub, 仅编译通过 */
int vscanf(const char *format, va_list ap);
int vfscanf(FILE *stream, const char *format, va_list ap);
int vsscanf(const char *str, const char *format, va_list ap);
int scanf(const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int sscanf(const char *str, const char *format, ...);
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define BUFSIZ 8192
#define L_tmpnam 20
#define TMP_MAX  238328
#define FILENAME_MAX 4096
#define FOPEN_MAX 16
#endif
#endif
