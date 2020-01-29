# Next-gen malloc for musl libc - Working draft

This is a draft of the upcoming next-gen `malloc` implementation for
musl libc. It is essentially feature-complete, but is not yet
integrated with musl and lacks some additional hardening and
optimizations that the final version is expected to include.

The included `Makefile` builds static and shared library files that
programs can link with, or use with `LD_PRELOAD` (for the shared
version).

## High-level design

This allocator organizes memory dynamically into small slab-style
groups of up to 32 identical-size allocation units with status
controlled by bitmasks, and utilizes a mix of in-band and out-of-band
metadata to isolate sensitive state from regions easily accessible
through out-of-bounds writes, while avoiding the need for expensive
global data structures.

The closest analogue among other well-known allocators is probably
OpenBSD's omalloc.

Base allocation granularity and alignment is 16 bytes. Large
allocations are made individually by mmap, but they also have
out-of-band metadata records treating them as special one-member
groups, allowing `realloc` and `free` to validate them. Smaller
allocations come from groups of one of 48 size classes, spaced
linearly up to 128 (the first 8 classes), then roughly geometrically
with four steps per doubling, but adjusted to divide powers of two
with minimal remainder (waste).
