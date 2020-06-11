#ifndef MALLOC_GLUE_H
#define MALLOC_GLUE_H

#include <stdint.h>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>

// use macros to appropriately namespace these. for libc,
// the names would be changed to lie in __ namespace.
#define size_classes malloc_size_classes
#define ctx malloc_context
#define alloc_meta malloc_alloc_meta
#define is_allzero malloc_allzerop

#if USE_REAL_ASSERT
#include <assert.h>
#else
#undef assert
#define assert(x) do { if (!(x)) __builtin_trap(); } while(0)
#endif

#undef brk
#if USE_BRK
#include <sys/syscall.h>
#define brk(p) ((uintptr_t)syscall(SYS_brk, p))
#else
#define brk(p) ((p)-1)
#endif

#ifndef MADV_FREE
#undef madvise
#define madvise(p,l,a) (-1)
#define MADV_FREE 0
#endif

#ifndef MREMAP_MAYMOVE
#undef mremap
#define mremap(p,o,n,f) MAP_FAILED
#endif

static inline int a_ctz_32(uint32_t x)
{
	return __builtin_ctz(x);
}

static inline int a_clz_32(uint32_t x)
{
	return __builtin_clz(x);
}

static inline int a_cas(volatile int *p, int t, int s)
{
	return __sync_val_compare_and_swap(p, t, s);
}

static inline int a_swap(volatile int *p, int v)
{
	int x;
	do x = *p;
	while (a_cas(p, x, v)!=x);
	return x;
}

static inline void a_or(volatile int *p, int v)
{
	__sync_fetch_and_or(p, v);
}

static inline uint64_t get_random_secret()
{
	uint64_t secret;
	getentropy(&secret, sizeof secret);
	return secret;
}

static inline size_t get_page_size()
{
	return sysconf(_SC_PAGESIZE);
}

// no portable "is multithreaded" predicate so assume true
#define MT 1

#define LOCK_TYPE_MUTEX 1
#define LOCK_TYPE_RWLOCK 2

#ifndef LOCK_TYPE
#define LOCK_TYPE LOCK_TYPE_MUTEX
#endif

#if LOCK_TYPE == LOCK_TYPE_MUTEX

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

#elif LOCK_TYPE == LOCK_TYPE_RWLOCK

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
