#include <stdio.h>
#include "meta.h"

static void print_group(FILE *f, struct meta *g)
{
	size_t size = g->sizeclass>48
		? g->maplen*4096UL-UNIT
		: UNIT*size_classes[g->sizeclass];
	fprintf(f, "%p: %p [%d slots] [class %d (%zu)]: ", g, g->mem,
		g->last_idx+1, g->sizeclass, size);
	for (int i=0; i<=g->last_idx; i++) {
		putc((g->avail_mask & (1u<<i)) ? 'a'
			: (i > g->mem->active_idx) ? 'i'
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

static size_t count_list(struct meta *h)
{
	size_t cnt = 0;
	struct meta *m = h;
	if (!m) return 0;
	do cnt++;
	while ((m=m->next)!=h);
	return cnt;
}

void dump_heap(FILE *f)
{
	wrlock();

	fprintf(f, "free meta records: %zu\n", count_list(ctx.free_meta_head));
	fprintf(f, "available new meta records: %zu\n", ctx.avail_meta_count);
	fprintf(f, "available new meta areas: %zu\n", ctx.avail_meta_area_count);

	fprintf(f, "entirely filled, inactive groups:\n");
	print_full_groups(f);

	fprintf(f, "free groups by size class:\n");
	for (int i=0; i<48; i++) {
		if (!ctx.active[i]) continue;
		fprintf(f, "-- class %d (%d) (%zu used) --\n", i, size_classes[i]*UNIT, ctx.usage_by_class[i]);
		print_group_list(f, ctx.active[i]);
	}

	unlock();
}
