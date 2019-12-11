#ifndef MALLOC_LOCKING_H
#define MALLOC_LOCKING_H

#include <pthread.h>

#if 1
static struct {
	int threads_minus_1;
} libc = { 1 };
#endif

__attribute__((__visibility__("hidden")))
extern pthread_rwlock_t malloc_lock;

#define LOCK_OBJ_DEF \
pthread_rwlock_t malloc_lock = PTHREAD_RWLOCK_INITIALIZER

static inline void rdlock()
{
	if (libc.threads_minus_1)
		pthread_rwlock_rdlock(&malloc_lock);
}

static inline void wrlock()
{
	if (libc.threads_minus_1)
		pthread_rwlock_wrlock(&malloc_lock);
}

static inline void unlock()
{
	if (libc.threads_minus_1)
		pthread_rwlock_unlock(&malloc_lock);
}

static inline void upgradelock()
{
	unlock();
	wrlock();
}

#endif
