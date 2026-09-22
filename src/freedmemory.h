#pragma once
#ifdef __GLIBC__
#include <malloc.h>
#endif

// glibc keeps what a program frees on its own free lists and gives memory back
// to the system only from the top of the heap, or when a whole mmapped block is
// released. Short-lived work that allocates a lot in small pieces, such as
// reading or writing a large library, leaves its freed pages between allocations
// that live on, so the process stays at its peak long after the work is done:
// four saves of a 10,000-song library left it 114 MiB larger, all of it memory
// already freed. malloc_trim(0) returns every whole free page wherever it sits,
// in every arena, and brought it back to where it started.
// https://man7.org/linux/man-pages/man3/malloc_trim.3.html
// It walks the free lists, so it belongs after such work rather than on a
// timer; on that heap it took 4.7 ms.
inline void returnFreedMemory() {
#ifdef __GLIBC__
  malloc_trim(0);
#endif
}
