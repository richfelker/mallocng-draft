# Next-gen malloc for musl libc - Working draft

This is a draft of the upcoming next-gen `malloc` implementation for
musl libc. It is not complete, lacking some logic for strategy to
obtain new memory, memalign-family functions, etc. as well as some
hardening features and optimizations that the final version is
expected to include.

The included `Makefile` builds static and shared library files that
programs can link with, or use with `LD_PRELOAD` (for the shared
version).
