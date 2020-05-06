
ALL = libmallocng.a libmallocng.so
SRCS = malloc.c free.c realloc.c memalign.c posix_memalign.c aligned_alloc.c malloc_usable_size.c dump.c
OBJS = $(SRCS:.c=.o)
CFLAGS = -fPIC -Wall -O2 -ffreestanding

-include config.mak

all: $(ALL)

$(OBJS): meta.h assert.h atomic.h locking.h

clean:
	rm -f $(ALL) $(OBJS)

libmallocng.a: $(OBJS)
	rm -f $@
	ar rc $@ $(OBJS)
	ranlib $@

libmallocng.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $(OBJS)
