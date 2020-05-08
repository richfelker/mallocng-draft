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
		ctx.usage_by_class[sc] -= g->last_idx+1;
	}
	if (g->maplen) {
		// Compute ceil-log of map lenth. If it's in range for
		// bounce counter tracking, cache it or update unmap
		// counter as appropriate.
		int mc = 31-a_clz_32(2UL*g->maplen-1);
		if (mc < 8U) {
			if (ctx.potcount[mc] < ctx.potlimit[mc] &&
			    g->maplen == 1<<mc) {
				// dummy class so this doesn't look like group
				g->sizeclass = 62;
				ctx.potcount[mc]++;
				queue(&ctx.potcache[mc], g);
				// tell caller not to unmap anything, and
				// return without freeing the meta since it's
				// been used in potcache.
				return mi;
			}
			if (sc < 40) ctx.unmaps[mc]++;
		}
		mi.base = g->mem;
		mi.len = g->maplen*4096UL;
	} else {
		void *p = g->mem;
		struct meta *m = get_meta(p);
		int idx = get_slot_index(p);
		// not checking size/reserved here; it's intentionally invalid
		mi = nontrivial_free(m, idx);
	}
	free_meta(g);
	return mi;
}

static struct mapinfo nontrivial_free(struct meta *g, int i)
{
	uint32_t self = 1u<<i;
	int sc = g->sizeclass;
	uint32_t mask = g->freed_mask | g->avail_mask;

	if (mask+self == (2u<<g->last_idx)-1 && g->freeable) {
		// any multi-slot group is necessarily on an active list
		// here, but single-slot groups might or might not be.
		if (g->next) {
			assert(sc < 48);
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
	size_t stride = get_stride(g);
	unsigned char *start = g->mem->storage + stride*idx;
	unsigned char *end = start + stride - 4;
	get_nominal_size(p, end);
	uint32_t self = 1u<<idx, all = (2u<<g->last_idx)-1;
	((unsigned char *)p)[-3] = 255;
	// invalidate offset to group header, and cycle offset of
	// used region within slot if current offset is zero.
	*(uint16_t *)((char *)p-2) = 0;

	// release any whole pages contained in the slot to be freed
	// unless it's a single-slot group that will be unmapped.
	if (((uintptr_t)(start-1) ^ (uintptr_t)end) >= 2*PGSZ && g->last_idx) {
		unsigned char *base = start + (-(uintptr_t)start & (PGSZ-1));
		size_t len = (end-base) & -PGSZ;
		if (len) madvise(base, len, MADV_DONTNEED);
	}

	// atomic free without locking if this is neither first or last slot
	for (;;) {
		uint32_t freed = g->freed_mask;
		uint32_t avail = g->avail_mask;
		uint32_t mask = freed | avail;
		assert(!(mask&self));
		if (!freed || mask+self==all) break;
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
