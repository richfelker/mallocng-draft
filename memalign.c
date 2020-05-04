#include <stdlib.h>
#include <errno.h>
#include "meta.h"

void *memalign(size_t align, size_t len)
{
	if ((align & -align) != align) {
		errno = EINVAL;
		return 0;
	}

	if (len > SIZE_MAX - align || align >= 1<<20) {
		errno = ENOMEM;
		return 0;
	}

	if (align <= UNIT) align = UNIT;

	unsigned char *p = malloc(len + align - UNIT);
	struct meta *g = get_meta(p);
	int idx = get_slot_index(p);
	size_t stride = get_stride(g);
	unsigned char *start = g->mem->storage + stride*idx;
	unsigned char *end = g->mem->storage + stride*(idx+1) - 4;
	size_t adj = -(uintptr_t)p & (align-1);

	if (!adj) {
		set_size(p, end, len);
		return p;
	}
	p += adj;
	*(uint16_t *)(p-2) = (size_t)(p-g->mem->storage)/UNIT;
	p[-3] = idx;
	p[-4] = 0;
	set_size(p, end, len);
	// store offset to aligned enframing. this facilitates cycling
	// offset and also iteration of heap for debugging/measurement.
	*(uint16_t *)(start - 2) = (size_t)(p-start)/UNIT;
	return p;
}
