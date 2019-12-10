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

	if (align <= 16) return malloc(len);

	unsigned char *p = malloc(len + align - 1);
	struct meta *g = get_meta(p);
	int idx = get_slot_index(p);
	size_t stride = get_stride(g);
	unsigned char *end = g->mem->storage + stride*(idx+1) - 4;
	size_t adj = -(uintptr_t)p & (align-1);

	if (!adj) return p;
	p += adj;
	*(uint16_t *)(p-2) = (p-g->mem->storage)/16U;
	p[-3] = idx;
	p[-4] = 0;
	set_size(p, end, len);
	return p;
}
