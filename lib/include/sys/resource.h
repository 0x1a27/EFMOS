/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SYS_RESOURCE_H
#define _EFMLIBC_SYS_RESOURCE_H
#ifdef __cplusplus
#include_next <sys/resource.h>
#else
#include <sys/types.h>

/* RLIMIT 资源类型 (Linux 兼容) */
#define RLIMIT_CPU        0
#define RLIMIT_FSIZE      1
#define RLIMIT_DATA       2
#define RLIMIT_STACK      3
#define RLIMIT_CORE       4
#define RLIMIT_RSS        5
#define RLIMIT_NPROC      6
#define RLIMIT_NOFILE     7
#define RLIMIT_MEMLOCK    8
#define RLIMIT_AS         9
#define RLIMIT_LOCKS     10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE  12
#define RLIMIT_NICE      13
#define RLIMIT_RTPRIO    14
#define RLIMIT_RTTIME    15
#define RLIM_NLIMITS     16

#define RLIM_INFINITY  (~0UL)
#define RLIM_SAVED_MAX RLIM_INFINITY
#define RLIM_SAVED_CUR RLIM_INFINITY

typedef unsigned long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

/* RUSAGE 标志 */
#define RUSAGE_SELF       0
#define RUSAGE_CHILDREN  (-1)
#define RUSAGE_THREAD     1

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

/* EFMOS freestanding: 资源限制无意义, 返回无限大 / 成功 */
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);
int getrusage(int who, struct rusage *usage);

/* PRIO_*: getpriority/setpriority 的 which 参数 (Mesa u_queue.c 需要) */
#define PRIO_PROCESS    0
#define PRIO_PGRP       1
#define PRIO_USER       2
int getpriority(int which, int who);
int setpriority(int which, int who, int prio);

#endif
#endif
