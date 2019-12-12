#include <stdlib.h>
#include <sys/mman.h>

#include "assert.h"
#include "meta.h"
#include "locking.h"
#include "atomic.h"

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

static int okay_to_free(struct meta *g)
{
	if (!g->freeable) return 0;

	// always free individual mmaps
	if (!g->last_idx) return 1;

	// always free groups allocated inside another group's slot
	// since recreating them should not be expensive and they
	// might be blocking freeing of a much larger group.
	if (!g->maplen) return 1;

	// if there is another non-full group, free this one to
	// consolidate future allocations, reduce fragmentation.
	if (g->next != g) return 1;

	int sc = g->sizeclass;
	size_t size = 16*size_classes[sc]*(g->last_idx+1);
	size_t usage = ctx.usage_by_class[sc];

	// keep groups with low relative usage unless they are
	// low-count, in which case we'd rather replace them
	// with full-count groups.
	if (4*size <= usage) {
		// FIXME: improve this logic. roughly, if this isn't a
		// max-count group and usage is high enough to warrant
		// allocation of a higher-count group...
		if (g->last_idx+1 < 20 && 2*size <= (usage-size)/2)
			return 1;
		return 0;
	}

	// avoid freeing last group in size classes so small
	// that memory usage is not significant.
	if (sc < 24) return 0;

	return 1;
}

static struct mapinfo nontrivial_free(struct meta *g, int i)
{
	uint32_t self = 1u<<i;
	int sc = g->sizeclass;
	uint32_t mask = g->freed_mask | g->avail_mask;

	if (mask+self == (2u<<g->last_idx)-1 && okay_to_free(g)) {
		if (sc < 48) {
			int activate_new = (ctx.active[sc]==g);
			dequeue(&ctx.active[sc], g);
			if (activate_new && ctx.active[sc]) {
				struct meta *m = ctx.active[sc];
				m->avail_mask = a_swap(&m->freed_mask, 0);
			}
		}
		return free_group(g);
	} else if (!mask) {
		assert(sc < 48);
		// might still be active if there were no allocations
		// after last available slot was taken.
		if (ctx.active[sc] != g) {
			queue(&ctx.active[sc], g);
		}
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
	uint32_t self = 1u<<idx, all = (2u<<g->last_idx)-1;
	((unsigned char *)p)[-3] = 255;

	// atomic free without locking if this is neither first or last slot
	for (;;) {
		uint32_t freed = g->freed_mask;
		uint32_t avail = g->avail_mask;
		uint32_t mask = freed | avail;
		assert(!(mask&self));
		if (!mask || mask+self==all) break;
		if (!MT)
			g->freed_mask = freed+self;
		else if (a_cas(&g->freed_mask, freed, freed+self)!=freed)
			continue;
		return;
	}

	wrlock();
	struct mapinfo mi = nontrivial_free(g, idx);
	unlock();
	if (mi.len) munmap(mi.base, mi.len);
}
