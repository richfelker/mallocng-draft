
ALL = libmallocng.a libmallocng.so
SRCS = malloc.c free.c realloc.c aligned_alloc.c posix_memalign.c memalign.c malloc_usable_size.c dump.c
OBJS = $(SRCS:.c=.o)
CFLAGS = -fPIC -Wall -O2 -ffreestanding

-include config.mak

all: $(ALL)

$(OBJS): meta.h glue.h

clean:
	rm -f $(ALL) $(OBJS)

libmallocng.a: $(OBJS)
	rm -f $@
	ar rc $@ $(OBJS)
	ranlib $@

libmallocng.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $(OBJS)
