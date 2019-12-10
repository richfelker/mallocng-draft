
ALL = libmallocng.a libmallocng.so
SRCS = malloc.c memalign.c posix_memalign.c aligned_alloc.c malloc_usable_size.c
OBJS = $(SRCS:.c=.o)
CFLAGS = -Wall -O2 -ffreestanding -fno-asynchronous-unwind-tables -fno-align-jumps -fno-align-functions -fno-align-loops -fno-align-labels -fno-prefetch-loop-arrays -freorder-blocks-algorithm=simple -fPIC

-include config.mak

all: $(ALL)

$(OBJS): meta.h assert.h

clean:
	rm -f $(ALL) $(OBJS)

libmallocng.a: $(OBJS)
	rm -f $@
	ar rc $@ $(OBJS)
	ranlib $@

libmallocng.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $(OBJS)
