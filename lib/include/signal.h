/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_SIGNAL_H
#define _EFMLIBC_SIGNAL_H
#ifdef __cplusplus
#include_next <signal.h>
#else
#include <stddef.h>
#include <sys/types.h>

typedef void (*sighandler_t)(int);
#define SIG_ERR  ((sighandler_t)(-1))
#define SIG_DFL  ((sighandler_t)0)
#define SIG_IGN  ((sighandler_t)1)

#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGIOT     6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPWR    30
#define SIGSYS    31
#define NSIG      32

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* siginfo_t: required by SA_SIGINFO handlers */
union sigval {
    int    sival_int;
    void  *sival_ptr;
};
typedef struct {
    int      si_signo;
    int      si_errno;
    int      si_code;
    pid_t    si_pid;
    uid_t    si_uid;
    void    *si_addr;
    int      si_status;
    long     si_band;
    union sigval si_value;
} siginfo_t;

/* "sa_handler" and "sa_sigaction" field names required by POSIX */
struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    } __sa_u;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    unsigned long sa_mask;
};
#define sa_handler   __sa_u.sa_handler
#define sa_sigaction __sa_u.sa_sigaction
#define SA_NOCLDSTOP  0x00000001
#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO    0x00000004
#define SA_ONSTACK    0x08000000
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000
#define SA_NOMASK     SA_NODEFER
#define SA_ONESHOT    SA_RESETHAND

typedef struct { unsigned long __bits[1]; } sigset_t;
typedef int sig_atomic_t;

sighandler_t signal(int sig, sighandler_t handler);
int raise(int sig);
int kill(pid_t pid, int sig);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);
int sigprocmask(int how, const sigset_t *set, sigset_t *oset);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
int sigwait(const sigset_t *set, int *sig);
int sigpending(sigset_t *set);
int pause(void);
#endif
#endif
