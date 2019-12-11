#ifndef MALLOC_ATOMIC_H
#define MALLOC_ATOMIC_H

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

#endif
