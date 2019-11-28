
ALL = libmallocng.a libmallocng.so
SRCS = malloc.c
OBJS = $(SRCS:.c=.o)
CFLAGS = -Wall -O2 -fno-asynchronous-unwind-tables -fno-align-jumps -fno-align-functions -fno-align-loops -fno-align-labels -fno-prefetch-loop-arrays -freorder-blocks-algorithm=simple -fPIC

-include config.mak

all: $(ALL)

clean:
	rm -f $(ALL) $(OBJS)

libmallocng.a: $(OBJS)
	rm -f $@
	ar rc $@ $(OBJS)
	ranlib $@

libmallocng.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $(OBJS)
