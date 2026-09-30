/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SYS_TIME_H
#define _EFMLIBC_SYS_TIME_H
#ifdef __cplusplus
#include_next <sys/time.h>
#else
#include <time.h>   /* time_t, struct timeval, gettimeofday decl */

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

/* gettimeofday already declared in <time.h> */
int settimeofday(const struct timeval *tv, const struct timezone *tz);
int getitimer(int which, void *curr_value);
int setitimer(int which, const void *new_val, void *old_val);
int utimes(const char *filename, const struct timeval times[2]);
int futimes(int fd, const struct timeval times[2]);
int lutimes(const char *filename, const struct timeval times[2]);
int adjtime(const struct timeval *delta, struct timeval *olddelta);

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define timerisset(tvp)     ((tvp)->tv_sec || (tvp)->tv_usec)
#define timerclear(tvp)     ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timercmp(a, b, CMP)  (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec CMP (b)->tv_usec) : ((a)->tv_sec CMP (b)->tv_sec))
#define timeradd(a, b, r)                         \
    do {                                           \
        (r)->tv_sec = (a)->tv_sec + (b)->tv_sec;  \
        (r)->tv_usec = (a)->tv_usec + (b)->tv_usec; \
        if ((r)->tv_usec >= 1000000L) {            \
            (r)->tv_sec++;                         \
            (r)->tv_usec -= 1000000L;              \
        }                                          \
    } while (0)
#define timersub(a, b, r)                         \
    do {                                           \
        (r)->tv_sec = (a)->tv_sec - (b)->tv_sec;  \
        (r)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
        if ((r)->tv_usec < 0) {                   \
            (r)->tv_sec--;                         \
            (r)->tv_usec += 1000000L;              \
        }                                          \
    } while (0)
#endif
#endif
