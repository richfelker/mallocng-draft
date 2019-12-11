#ifndef MALLOC_LOCKING_H
#define MALLOC_LOCKING_H

#include <pthread.h>

#define MT 1
#define RDLOCK_IS_EXCLUSIVE 0

__attribute__((__visibility__("hidden")))
extern pthread_rwlock_t malloc_lock;

#define LOCK_OBJ_DEF \
pthread_rwlock_t malloc_lock = PTHREAD_RWLOCK_INITIALIZER

static inline void rdlock()
{
	if (MT) pthread_rwlock_rdlock(&malloc_lock);
}

static inline void wrlock()
{
	if (MT) pthread_rwlock_wrlock(&malloc_lock);
}

static inline void unlock()
{
	if (MT) pthread_rwlock_unlock(&malloc_lock);
}

static inline void upgradelock()
{
	unlock();
	wrlock();
}

#endif
