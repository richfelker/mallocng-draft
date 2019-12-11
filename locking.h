#ifndef MALLOC_LOCKING_H
#define MALLOC_LOCKING_H

#define LOCK_WITH_PTHREAD_MUTEX 1

#if LOCK_WITH_PTHREAD_MUTEX

#include <pthread.h>

#define MT 1
#define RDLOCK_IS_EXCLUSIVE 1

__attribute__((__visibility__("hidden")))
extern pthread_mutex_t malloc_lock;

#define LOCK_OBJ_DEF \
pthread_mutex_t malloc_lock = PTHREAD_MUTEX_INITIALIZER

static inline void rdlock()
{
	if (MT) pthread_mutex_lock(&malloc_lock);
}

static inline void wrlock()
{
	if (MT) pthread_mutex_lock(&malloc_lock);
}

static inline void unlock()
{
	if (MT) pthread_mutex_unlock(&malloc_lock);
}

static inline void upgradelock()
{
}

#else

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

#endif
