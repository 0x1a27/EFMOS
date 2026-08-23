#ifndef _EFMLIBC_CTYPE_H
#define _EFMLIBC_CTYPE_H
#ifdef __cplusplus
#include_next <ctype.h>
#else
int isalpha(int c);
int isdigit(int c);
int isspace(int c);
int isprint(int c);
int isalnum(int c);
int isxdigit(int c);
int isupper(int c);
int islower(int c);
int isascii(int c);
int iscntrl(int c);
int ispunct(int c);
int isgraph(int c);
int toupper(int c);
int tolower(int c);
#endif
#endif
