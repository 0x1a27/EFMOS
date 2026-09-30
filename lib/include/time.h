/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_TIME_H
#define _EFMLIBC_TIME_H
#ifdef __cplusplus
#include_next <time.h>
#else
#include <sys/types.h>
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCKS_PER_SEC 1000000
#ifndef _EFM_TIMESPEC_DEFINED
#define _EFM_TIMESPEC_DEFINED
struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
#endif
typedef long clock_t;
struct tm {
    int tm_sec, tm_min, tm_hour;
    int tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};
int   clock_gettime(clockid_t id, struct timespec *tp);
int   clock_getres(clockid_t id, struct timespec *r);
int   clock_nanosleep(clockid_t id, int flags, const struct timespec *req, struct timespec *rem);
clock_t clock(void);
double  difftime(time_t t1, time_t t0);
time_t  time(time_t *t);
time_t  mktime(struct tm *tm);
time_t  timegm(struct tm *tm);
struct timezone;
int   gettimeofday(struct timeval *tv, void *tz);
#define gettimeofday_voidtz gettimeofday
int   nanosleep(const struct timespec *req, struct timespec *rem);
int   timespec_get(struct timespec *ts, int base);
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime_r(const time_t *t, struct tm *out);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
char *asctime_r(const struct tm *tm, char *buf);
char *ctime_r(const time_t *t, char *buf);
char *asctime(const struct tm *tm);
char *ctime(const time_t *t);
#define TIMER_ABSTIME 1
#endif
#endif
