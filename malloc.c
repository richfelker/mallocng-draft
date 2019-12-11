#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include "assert.h"
#include "meta.h"

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

static inline size_t get_page_size()
{
#ifdef PAGESIZE
	return PAGESIZE;
#else
	return sysconf(_SC_PAGESIZE);
#endif
}

#if 1
static struct {
	int threads_minus_1;
} libc = { 1 };
#endif

static pthread_rwlock_t malloc_lock = PTHREAD_RWLOCK_INITIALIZER;

static void rdlock()
{
	if (libc.threads_minus_1)
		pthread_rwlock_rdlock(&malloc_lock);
}

static void wrlock()
{
	if (libc.threads_minus_1)
		pthread_rwlock_wrlock(&malloc_lock);
}

static void unlock()
{
	if (libc.threads_minus_1)
		pthread_rwlock_unlock(&malloc_lock);
}

static void upgradelock()
{
	unlock();
	wrlock();
}

const uint16_t size_classes[] = {
	1, 2, 3, 4, 5, 6, 7, 8,
	9, 10, 12, 15,
	18, 21, 25, 31,
	36, 42, 51, 63,
	73, 85, 102, 127,
	146, 170, 204, 255,
	292, 341, 409, 511,
	584, 682, 818, 1023,
	1169, 1364, 1637, 2047,
	2340, 2730, 3276, 4095,
	4680, 5460, 6552, 8191,
};

static const uint8_t small_cnt_tab[][3] = {
	{ 30, 30, 30 },
	{ 31, 15, 15 },
	{ 20, 10, 10 },
	{ 31, 15, 7 },
	{ 25, 12, 6 },
	{ 21, 10, 5 },
	{ 18, 8, 4 },
	{ 31, 15, 7 },
};

static const uint8_t med_cnt_tab[4] = { 28, 24, 20, 32 };
static const uint8_t med_twos_tab[4] = { 2, 3, 2, 4 };

struct malloc_context ctx;

static void queue(struct meta **phead, struct meta *m)
{
	assert(!m->next && !m->prev);
	if (*phead) {
		struct meta *head = *phead;
		m->next = head;
		m->prev = head->prev;
		m->next->prev = m->prev->next = m;
	} else {
		m->prev = m->next = m;
		*phead = m;
	}
}

static void dequeue(struct meta **phead, struct meta *m)
{
	if (m->next != m) {
		m->prev->next = m->next;
		m->next->prev = m->prev;
		if (*phead == m) *phead = m->next;
	} else {
		*phead = 0;
	}
	m->prev = m->next = 0;
}

static struct meta *dequeue_head(struct meta **phead)
{
	struct meta *m = *phead;
	if (m) dequeue(phead, m);
	return m;
}

static struct meta *alloc_meta(void)
{
	struct meta *m;
	unsigned char *p;
	size_t pagesize = get_page_size();
	if (pagesize < 4096) pagesize = 4096;
	if ((m = dequeue_head(&ctx.free_meta_head))) return m;
	if (!ctx.avail_meta_count) {
		if (!ctx.avail_meta_area_count) {
			size_t n = 2UL << ctx.meta_alloc_shift;
			p = mmap(0, n*pagesize, PROT_NONE,
				MAP_PRIVATE|MAP_ANON, -1, 0);
			if (p==MAP_FAILED) return 0;
			ctx.avail_meta_areas = p + pagesize;
			ctx.avail_meta_area_count = (n-1)*(pagesize>>12);
			ctx.meta_alloc_shift++;
		}
		p = ctx.avail_meta_areas;
		if (!((uintptr_t)p & (pagesize-1)))
			if (mprotect(p, pagesize, PROT_READ|PROT_WRITE))
				return 0;
		ctx.avail_meta_area_count--;
		ctx.avail_meta_areas = p + 4096;
		if (ctx.meta_area_tail) {
			ctx.meta_area_tail->next = (void *)p;
		} else {
			ctx.meta_area_head = (void *)p;
		}
		ctx.meta_area_tail = (void *)p;
		ctx.avail_meta_count = ctx.meta_area_tail->nslots
			= (4096-sizeof(struct meta_area))/sizeof *m;
		ctx.avail_meta = ctx.meta_area_tail->slots;
	}
	ctx.avail_meta_count--;
	m = ctx.avail_meta++;
	m->prev = m->next = 0;
	return m;
}

static void free_meta(struct meta *m)
{
	*m = (struct meta){0};
	queue(&ctx.free_meta_head, m);
}

static uint32_t try_avail(struct meta **pm)
{
	struct meta *m = *pm;
	uint32_t first;
	if (!m) return 0;
	uint32_t mask = m->avail_mask;
	if (!mask) {
		if (!m) return 0;
		if (!m->freed_mask) {
			dequeue(pm, m);
			m = *pm;
			if (!m) return 0;
		} else {
			*pm = m = m->next;
		}
		mask = a_swap(&m->freed_mask, 0);
		if (!mask) return 0;
	}
	first = mask&-mask;
	m->avail_mask = mask-first;
	return first;
}

struct mapinfo {
	void *base;
	size_t len;
};

static struct mapinfo nontrivial_free(struct meta *, int);

static struct mapinfo free_group(struct meta *g)
{
	struct mapinfo mi = { 0 };
	int sc = g->sizeclass;
	if (sc < 48) {
		ctx.usage_by_class[sc] -= (g->last_idx+1)*size_classes[sc]*16;
	}
	if (g->maplen) {
		mi.base = g->mem;
		mi.len = g->maplen*4096;
	} else if (g->freeable) {
		void *p = g->mem;
		struct meta *m = get_meta(p);
		int idx = get_slot_index(p);
		// not checking size/reserved here; it's intentionally invalid
		mi = nontrivial_free(m, idx);
	}
	free_meta(g);
	return mi;
}

static int alloc_slot(int);

static struct meta *alloc_group(int sc)
{
	size_t size = 16*size_classes[sc];
	int i = 0, cnt;
	unsigned char *p;
	struct meta *m = alloc_meta();
	if (!m) return 0;
	size_t usage = ctx.usage_by_class[sc];
	size_t pagesize = get_page_size();
	if (sc < 8) {
		while (i<2 && size*small_cnt_tab[sc][i] > usage/2)
			i++;
		cnt = small_cnt_tab[sc][i];
	} else {
		// lookup max number of slots fitting in power-of-two size
		// from a table, along with number of factors of two we
		// can divide out without a remainder or reaching 1.
		i = med_twos_tab[sc&3];
		cnt = med_cnt_tab[sc&3];

		// reduce cnt to avoid excessive eagar allocation.
		while (i-- && size*cnt > usage/2)
			cnt >>= 1;

		// data structures don't support groups whose slot offsets
		// in 16-byte units don't fit in 16 bits.
		while (size*cnt >= 65536*16)
			cnt >>= 1;
	}
	// All choices of size*cnt are "just below" a power of two, so anything
	// larger than half the page size should be allocated as whole pages.
	if (size*cnt+16 >= pagesize/2) {
		size_t needed = size*cnt + sizeof(struct group);
		needed += -needed & (pagesize-1);
		p = mmap(0, needed, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
		if (p==MAP_FAILED) {
			free_meta(m);
			return 0;
		}
		m->maplen = needed>>12;
	} else {
		int j = size_to_class(16+cnt*size-4);
		int idx = alloc_slot(j);
		if (idx < 0) {
			free_meta(m);
			return 0;
		}
		struct meta *g = ctx.active[j];
		p = enframe(g, idx, 16*size_classes[j]-4);
		m->maplen = 0;
		p[-3] = (p[-3]&31) | (6<<5);
		for (int i=0; i<=cnt; i++)
			p[16+i*size-4] = 0;
	}
	ctx.usage_by_class[sc] += cnt*size;
	m->avail_mask = (2u<<(cnt-1))-1;
	m->freed_mask = 0;
	m->mem = (void *)p;
	m->mem->meta = m;
	m->last_idx = cnt-1;
	m->freeable = 1;
	m->sizeclass = sc;
	return m;
}

static int alloc_slot(int sc)
{
	uint32_t first = try_avail(&ctx.active[sc]);
	if (first) return a_ctz_32(first);

	struct meta *g = alloc_group(sc);
	if (!g) return -1;

	g->avail_mask--;
	queue(&ctx.active[sc], g);
	return 0;
}

void *malloc(size_t n)
{
	if (size_overflows(n)) return 0;
	struct meta *g;
	uint32_t mask, first;
	int sc;
	int idx;

	if (n >= MMAP_THRESHOLD) {
		size_t needed = n + 4 + sizeof(struct group);
		void *p = mmap(0, needed, PROT_READ|PROT_WRITE,
			MAP_PRIVATE|MAP_ANON, -1, 0);
		if (p==MAP_FAILED) return 0;
		wrlock();
		g = alloc_meta();
		if (!g) {
			unlock();
			munmap(p, needed);
			return 0;
		}
		g->mem = p;
		g->mem->meta = g;
		g->last_idx = 0;
		g->freeable = 1;
		g->sizeclass = 63;
		g->maplen = (needed+4095)/4096;
		g->avail_mask = g->freed_mask = 0;
		idx = 0;
		goto success;
	}

	sc = size_to_class(n);

	rdlock();
	g = ctx.active[sc];
	for (;;) {
		mask = g ? g->avail_mask : 0;
		first = mask&-mask;
		if (!first) break;
		if (!libc.threads_minus_1)
			g->avail_mask = mask-first;
		else if (a_cas(&g->avail_mask, mask, mask-first)!=mask)
			continue;
		idx = a_ctz_32(first);
		goto success;
	}
	upgradelock();

	// use coarse size classes initially when there are not yet
	// any groups of desired size. this allows counts of 2 or 3
	// to be allocated at first rather than having to start with
	// 7 or 5, the min counts for even size classes.
	if (sc>=16 && !(sc&1) && !ctx.usage_by_class[sc]) {
		size_t usage = ctx.usage_by_class[sc|1];
		// if a new group may be allocated, count it toward
		// usage in deciding if we can use coarse class.
		if (!ctx.active[sc|1] || !ctx.active[sc|1]->avail_mask)
			usage += 3*16*size_classes[sc|1];
		if (usage <= 6*16*size_classes[sc|1])
			sc |= 1;
	}

	idx = alloc_slot(sc);
	if (idx < 0) {
		unlock();
		return 0;
	}
	g = ctx.active[sc];

success:
	unlock();
	return enframe(g, idx, n);
}

static struct mapinfo nontrivial_free(struct meta *g, int i)
{
	uint32_t self = 1u<<i;
	int sc = g->sizeclass;
	uint32_t mask = g->freed_mask;
	if (!mask) {
		// might still be active, or may be on full groups list
		if (ctx.active[sc] != g) {
			assert(!g->prev && !g->next);
			queue(&ctx.active[sc], g);
		}
	} else if (mask+self == (2u<<g->last_idx)-1 && (g->maplen || g->freeable)) {
		// FIXME: decide whether to free the whole group
		if (sc <= 48) {
			int activate_new = (ctx.active[sc]==g);
			dequeue(&ctx.active[sc], g);
			if (activate_new && ctx.active[sc]) {
				struct meta *m = ctx.active[sc];
				m->avail_mask = a_swap(&m->freed_mask, 0);
			}
		}
		return free_group(g);
	}
	a_or(&g->freed_mask, self);
	return (struct mapinfo){ 0 };
}


void free(void *p)
{
	if (!p) return;

	struct meta *g = get_meta(p);
	int idx = get_slot_index(p);
	get_nominal_size(p, g->mem->storage+get_stride(g)*(idx+1)-4);
	unsigned mask, self = 1u<<idx, all = (2u<<g->last_idx)-1;
	((unsigned char *)p)[-3] = 255;

	// atomic free without locking if this is neither first or last slot
	do {
		mask = g->freed_mask;
		assert(!(mask&self));
	} while (mask && mask+self!=all && a_cas(&g->freed_mask, mask, mask+self)!=mask);
	if (mask && mask+self!=all) return;

	/* free individually-mmapped allocation by performing munmap
	 * before taking the lock, since we are exclusive user. */
	if (!g->last_idx) {
		assert(g->maplen);
		munmap(g->mem, g->maplen*4096);
		wrlock();
		int sc = g->sizeclass;
		if (sc < 48) ctx.usage_by_class[sc] -= 16*size_classes[sc];
		free_meta(g);
		unlock();
		return;
	}

	wrlock();
	struct mapinfo mi = nontrivial_free(g, idx);
	unlock();
	if (mi.len) munmap(mi.base, mi.len);
}

void *calloc(size_t m, size_t n)
{
	if (n && m > (size_t)-1/n) {
		errno = ENOMEM;
		return 0;
	}
	n *= m;
	void *p = malloc(n);
	if (!p) return p;
	return n >= MMAP_THRESHOLD ? p : memset(p, 0, n);
}

#include <stdio.h>

static void print_group(FILE *f, struct meta *g)
{
	size_t size = g->sizeclass>48
		? g->maplen*4096-16
		: 16*size_classes[g->sizeclass];
	fprintf(f, "%p: %p [%d slots] [class %d (%zu)]: ", g, g->mem,
		g->last_idx+1, g->sizeclass, size);
	for (int i=0; i<=g->last_idx; i++) {
		putc((g->avail_mask & (1u<<i)) ? 'a'
			: (g->freed_mask & (1u<<i)) ? 'f' : '_', f);
	}
	putc('\n', f);
}

static void print_group_list(FILE *f, struct meta *h)
{
	struct meta *m = h;
	if (!m) return;
	do print_group(f, m);
	while ((m=m->next)!=h);
}

static void print_full_groups(FILE *f)
{
	struct meta_area *p;
	struct meta *m;
	for (p=ctx.meta_area_head; p; p=p->next) {
		for (int i=0; i<p->nslots; i++) {
			m = &p->slots[i];
			if (m->mem && !m->next)
				print_group(f, m);
		}
	}
}

void dump_heap(FILE *f)
{
	wrlock();

	fprintf(f, "free meta records:\n");
	print_group_list(f, ctx.free_meta_head);

	fprintf(f, "entirely filled, inactive groups:\n");
	print_full_groups(f);

	fprintf(f, "free groups by size class:\n");
	for (int i=0; i<48; i++) {
		if (!ctx.active[i]) continue;
		fprintf(f, "-- class %d (%d) (%zu used) --\n", i, size_classes[i]*16, ctx.usage_by_class[i]);
		print_group_list(f, ctx.active[i]);
	}

	unlock();
}
