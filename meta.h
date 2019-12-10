#ifndef MALLOC_META_H
#define MALLOC_META_H

#include <stdint.h>
#include "assert.h"

#define size_classes malloc_size_classes

__attribute__((__visibility__("hidden")))
extern const uint16_t size_classes[];

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
	struct meta slots[];
};

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
		assert(reserved >= 5 && !end[-5]);
	}
	assert(reserved <= end-p && !*(end-reserved));
	return end-reserved-p;
}

static inline size_t get_stride(struct meta *g)
{
	if (g->sizeclass >= 48) {
		assert(g->sizeclass == 63);
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
	*(uint16_t *)(p-2) = (p-g->mem->storage)/16U;
	p[-3] = idx;
	set_size(p, p+stride-4, n);
	return p;
}

#endif
