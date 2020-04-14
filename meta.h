#ifndef MALLOC_META_H
#define MALLOC_META_H

#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include "assert.h"

// use macros to appropriately namespace these. for libc,
// the names would be changed to lie in __ namespace.
#define size_classes malloc_size_classes
#define ctx malloc_context

__attribute__((__visibility__("hidden")))
extern const uint16_t size_classes[];

#define MMAP_THRESHOLD 131052

struct group {
	struct meta *meta;
	char pad[16 - sizeof(struct meta *)];
	unsigned char storage[];
};

struct meta {
	struct meta *prev, *next;
	struct group *mem;
	volatile int avail_mask, freed_mask;
	uintptr_t last_idx:5;
	uintptr_t freeable:1;
	uintptr_t sizeclass:6;
	uintptr_t maplen:8*sizeof(uintptr_t)-12;
};

struct meta_area {
	uint64_t check;
	struct meta_area *next;
	int nslots;
	struct meta slots[];
};

struct malloc_context {
	uint64_t secret;
#ifndef PAGESIZE
	size_t pagesize;
#endif
	int init_done;
	unsigned mmap_counter;
	struct meta *free_meta_head;
	struct meta *avail_meta;
	size_t avail_meta_count, avail_meta_area_count, meta_alloc_shift;
	struct meta_area *meta_area_head, *meta_area_tail;
	unsigned char *avail_meta_areas;
	struct meta *active[48];
	size_t usage_by_class[48];
	uint8_t bounces_by_class[48];
};

__attribute__((__visibility__("hidden")))
extern struct malloc_context ctx;

#ifdef PAGESIZE
#define PGSZ PAGESIZE
#else
#define PGSZ ctx.pagesize
#endif

static inline void queue(struct meta **phead, struct meta *m)
{
	assert(!m->next);
	assert(!m->prev);
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

static inline void dequeue(struct meta **phead, struct meta *m)
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

static inline struct meta *dequeue_head(struct meta **phead)
{
	struct meta *m = *phead;
	if (m) dequeue(phead, m);
	return m;
}

static inline void free_meta(struct meta *m)
{
	*m = (struct meta){0};
	queue(&ctx.free_meta_head, m);
}

static inline int get_slot_index(const unsigned char *p)
{
	return p[-3] & 31;
}

static inline struct meta *get_meta(const unsigned char *p)
{
	assert(!((uintptr_t)p & 15));
	int offset = *(const uint16_t *)(p - 2);
	int index = get_slot_index(p);
	assert(!p[-4]);
	const struct group *base = (const void *)(p - 16*offset - sizeof *base);
	const struct meta *meta = base->meta;
	assert(meta->mem == base);
	assert(index <= meta->last_idx);
	assert(!(meta->avail_mask & (1u<<index)));
	assert(!(meta->freed_mask & (1u<<index)));
	const struct meta_area *area = (void *)((uintptr_t)meta & -4096);
	assert(area->check == ctx.secret);
	if (meta->sizeclass < 48) {
		assert(offset >= size_classes[meta->sizeclass]*index);
		assert(offset < size_classes[meta->sizeclass]*(index+1));
	} else {
		assert(meta->sizeclass == 63);
		assert(offset <= meta->maplen*4096/16 - 1);
	}
	return (struct meta *)meta;
}

static inline size_t get_nominal_size(const unsigned char *p, const unsigned char *end)
{
	size_t reserved = p[-3] >> 5;
	if (reserved >= 5) {
		assert(reserved == 5);
		reserved = *(const uint32_t *)(end-4);
		assert(reserved >= 5);
		assert(!end[-5]);
	}
	assert(reserved <= end-p);
	assert(!*(end-reserved));
	// also check the slot's overflow byte
	assert(!*end);
	return end-reserved-p;
}

static inline size_t get_stride(const struct meta *g)
{
	if (!g->last_idx) {
		return g->maplen*4096 - sizeof(struct group);
	} else {
		return 16*size_classes[g->sizeclass];
	}
}

static inline void set_size(unsigned char *p, unsigned char *end, size_t n)
{
	int reserved = end-p-n;
	if (reserved) end[-reserved] = 0;
	if (reserved >= 5) {
		*(uint32_t *)(end-4) = reserved;
		end[-5] = 0;
		reserved = 5;
	}
	p[-3] = (p[-3]&31) + (reserved<<5);
}

static inline void *enframe(struct meta *g, int idx, size_t n)
{
	size_t stride = get_stride(g);
	unsigned char *p = g->mem->storage + stride*idx;
	unsigned char *end = p+stride-4;
	// cycle offset within slot to increase interval to address
	// reuse, facilitate trapping double-free.
	int off = *(uint16_t *)(p-2) + 1;
	assert(!p[-4]);
	if (16*off <= stride-4-n) {
		// store offset in unused header at offset zero
		// if enframing at non-zero offset.
		*(uint16_t *)(p-2) = off;
		p[-3] = 7<<5;
		p += 16*off;
		// for nonzero offset there is no permanent check
		// byte, so make one.
		p[-4] = 0;
	}
	*(uint16_t *)(p-2) = (size_t)(p-g->mem->storage)/16;
	p[-3] = idx;
	set_size(p, end, n);
	return p;
}

static inline int a_ctz_32(uint32_t x)
{
	return __builtin_ctz(x);
}

static inline int a_clz_32(uint32_t x)
{
	return __builtin_clz(x);
}

static inline int size_to_class(size_t n)
{
	n = (n+3)>>4;
	if (n<10) return n;
	n++;
	int i = (28-a_clz_32(n))*4 + 8;
	if (n>size_classes[i+1]) i+=2;
	if (n>size_classes[i]) i++;
	return i;
}

static inline int size_overflows(size_t n)
{
	if (n >= SIZE_MAX/2 - 4096) {
		errno = ENOMEM;
		return 1;
	}
	return 0;
}

#endif
