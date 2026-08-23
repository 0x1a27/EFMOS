#ifndef _EFMLIBC_PTHREAD_H
#define _EFMLIBC_PTHREAD_H
#ifdef __cplusplus
#include_next <pthread.h>
#else
#include <sys/types.h>
#include <time.h>
#include <sched.h>
typedef unsigned long pthread_t;
typedef unsigned long pthread_mutex_t;
typedef unsigned long pthread_cond_t;
typedef unsigned long pthread_key_t;
typedef unsigned long pthread_once_t;
typedef unsigned long pthread_rwlock_t;
typedef unsigned long pthread_spinlock_t;
typedef unsigned long pthread_barrier_t;
typedef unsigned long pthread_attr_t;
typedef unsigned long pthread_mutexattr_t;
typedef unsigned long pthread_condattr_t;

#define PTHREAD_MUTEX_INITIALIZER    0
#define PTHREAD_COND_INITIALIZER     0
#define PTHREAD_RWLOCK_INITIALIZER   0
#define PTHREAD_ONCE_INIT            0
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL
#define PTHREAD_PROCESS_PRIVATE  0
#define PTHREAD_PROCESS_SHARED   1
#define PTHREAD_CREATE_JOINABLE  0
#define PTHREAD_CREATE_DETACHED  1
#define PTHREAD_CANCEL_ENABLE    0
#define PTHREAD_CANCEL_DISABLE   1
#define PTHREAD_CANCEL_DEFERRED  0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED        ((void*)-1)

int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void*), void *arg);
int pthread_join(pthread_t t, void **retval);
pthread_t pthread_self(void);
int pthread_equal(pthread_t a, pthread_t b);
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_trylock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_mutex_destroy(pthread_mutex_t *m);
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
int pthread_cond_destroy(pthread_cond_t *c);
int pthread_rwlock_init(pthread_rwlock_t *r, void *a);
int pthread_rwlock_rdlock(pthread_rwlock_t *r);
int pthread_rwlock_wrlock(pthread_rwlock_t *r);
int pthread_rwlock_unlock(pthread_rwlock_t *r);
int pthread_rwlock_destroy(pthread_rwlock_t *r);
int pthread_barrier_init(pthread_barrier_t *b, void *a, unsigned c);
int pthread_barrier_wait(pthread_barrier_t *b);
int pthread_barrier_destroy(pthread_barrier_t *b);
int pthread_once(pthread_once_t *o, void (*fn)(void));
int pthread_key_create(pthread_key_t *k, void (*dst)(void*));
int pthread_key_delete(pthread_key_t k);
void *pthread_getspecific(pthread_key_t k);
int   pthread_setspecific(pthread_key_t k, const void *v);
int pthread_setname_np(pthread_t t, const char *n);
int pthread_getname_np(pthread_t t, char *n, size_t l);
int pthread_attr_init(pthread_attr_t *a);
int pthread_attr_destroy(pthread_attr_t *a);
int pthread_attr_setstacksize(pthread_attr_t *a, size_t s);
int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *s);
int pthread_attr_setdetachstate(pthread_attr_t *a, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *a, int *detachstate);
int pthread_attr_setinheritsched(pthread_attr_t *a, int inheritsched);
int pthread_attr_getinheritsched(const pthread_attr_t *a, int *inheritsched);
int pthread_attr_setschedpolicy(pthread_attr_t *a, int policy);
int pthread_attr_getschedpolicy(const pthread_attr_t *a, int *policy);
int pthread_attr_setschedparam(pthread_attr_t *a, const void *param);
int pthread_attr_getschedparam(const pthread_attr_t *a, void *param);
int pthread_attr_setscope(pthread_attr_t *a, int contentionscope);
int pthread_attr_getscope(const pthread_attr_t *a, int *contentionscope);
/* mutex attributes */
int pthread_mutexattr_init(pthread_mutexattr_t *a);
int pthread_mutexattr_destroy(pthread_mutexattr_t *a);
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *type);
int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int pshared);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *pshared);
/* mutex timedlock / detach / exit */
int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *abstime);
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int proto);
int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *a, int *proto);
int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int prio);
int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *a, int *prio);
/* cond attributes */
int pthread_condattr_init(pthread_condattr_t *a);
int pthread_condattr_destroy(pthread_condattr_t *a);
int pthread_condattr_setclock(pthread_condattr_t *a, int clock);
int pthread_condattr_getclock(const pthread_condattr_t *a, int *clock);
int pthread_condattr_setpshared(pthread_condattr_t *a, int pshared);
int pthread_condattr_getpshared(const pthread_condattr_t *a, int *pshared);
/* rwlock attributes */
typedef unsigned long pthread_rwlockattr_t;
int pthread_rwlockattr_init(pthread_rwlockattr_t *a);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *a);
int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int pshared);
int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *a, int *pshared);
/* spinlock */
int pthread_spin_init(pthread_spinlock_t *l, int pshared);
int pthread_spin_destroy(pthread_spinlock_t *l);
int pthread_spin_lock(pthread_spinlock_t *l);
int pthread_spin_trylock(pthread_spinlock_t *l);
int pthread_spin_unlock(pthread_spinlock_t *l);
/* detach/exit/setsched */
int pthread_detach(pthread_t t);
void pthread_exit(void *retval) __attribute__((noreturn));
int pthread_cancel(pthread_t t);
int pthread_setcancelstate(int state, int *oldstate);
int pthread_setcanceltype(int type, int *oldtype);
void pthread_testcancel(void);
int pthread_setschedparam(pthread_t t, int policy, const void *param);
int pthread_getschedparam(pthread_t t, int *policy, void *param);
int pthread_setschedprio(pthread_t t, int prio);
/* signal */
typedef unsigned long pthread_attr_t_sigset;
int pthread_sigmask(int how, const void *set, void *oset);
/* CPU 亲和性 / 线程 CPU 时钟 (Mesa u_thread.c 需要) */
int pthread_setaffinity_np(pthread_t t, size_t cpusetsize, const cpu_set_t *cpuset);
int pthread_getaffinity_np(pthread_t t, size_t cpusetsize, cpu_set_t *cpuset);
int pthread_getcpuclockid(pthread_t t, clockid_t *cid);
#endif /* __cplusplus */
#endif
