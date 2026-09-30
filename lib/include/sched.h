/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SCHED_H
#define _EFMLIBC_SCHED_H
#ifdef __cplusplus
#include_next <sched.h>
#else
#include <sys/types.h>

/* SCHED 调度策略 */
#define SCHED_OTHER  0
#define SCHED_FIFO   1
#define SCHED_RR     2
#define SCHED_BATCH  3
#define SCHED_IDLE   5
#define SCHED_DEADLINE 6
#define SCHED_RESET_ON_FORK 0x40000000

/* 调度参数 */
#ifndef _SCHED_PARAM_DEFINED_
#define _SCHED_PARAM_DEFINED_
struct sched_param {
    int sched_priority;
};
#endif

/* CPU 亲和性位图 (Linux 兼容: 1024 位 = 128 字节) */
#define CPU_SETSIZE 1024
#define __NCPUBITS  (8 * sizeof(unsigned long))
#define __CPU_BIT(cpu)  ((cpu) / __NCPUBITS)
#define __CPU_MASK(cpu) (1UL << ((cpu) % __NCPUBITS))

typedef struct {
    unsigned long __bits[CPU_SETSIZE / __NCPUBITS];
} cpu_set_t;

#define CPU_ZERO(set) do { \
    unsigned int __i; \
    for (__i = 0; __i < (sizeof((set)->__bits)/sizeof((set)->__bits[0])); __i++) \
        (set)->__bits[__i] = 0; \
} while (0)

#define CPU_SET(cpu, set)   do { if ((unsigned)(cpu) < CPU_SETSIZE) (set)->__bits[__CPU_BIT(cpu)] |= __CPU_MASK(cpu); } while (0)
#define CPU_CLR(cpu, set)   do { if ((unsigned)(cpu) < CPU_SETSIZE) (set)->__bits[__CPU_BIT(cpu)] &= ~__CPU_MASK(cpu); } while (0)
#define CPU_ISSET(cpu, set) (((unsigned)(cpu) < CPU_SETSIZE) && \
    (((set)->__bits[__CPU_BIT(cpu)] & __CPU_MASK(cpu)) != 0))
#define CPU_COUNT(set) __sched_cpucount(set)

/* EFMOS freestanding: 单 CPU, 这些都简化 */
int   sched_getcpu(void);
int   sched_yield(void);
int   sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask);
int   sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
int   sched_getparam(pid_t pid, struct sched_param *param);
int   sched_setscheduler(pid_t pid, int policy, const struct sched_param *param);
int   sched_getscheduler(pid_t pid);
int   sched_get_priority_max(int policy);
int   sched_get_priority_min(int policy);

static inline int __sched_cpucount(const cpu_set_t *set) {
    int count = 0;
    unsigned int i;
    for (i = 0; i < sizeof(set->__bits)/sizeof(set->__bits[0]); i++) {
        unsigned long w = set->__bits[i];
        while (w) { count += (int)(w & 1UL); w >>= 1; }
    }
    return count;
}

#endif
#endif
