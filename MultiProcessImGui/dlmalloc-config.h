#pragma once

#define USE_DL_PREFIX 1
#define MSPACES 1
#define ONLY_MSPACES 1
//#define FOOTERS 1 // Can't use footers because the magic comes from the global mparams field in dlmalloc
#define DEBUG 1
#define MALLOC_FAILURE_ACTION abort();
#define HAVE_MORECORE 0

// Can't actually define HAVE_MMAP=0 here because WIN32 is defined and dlmalloc uses that automatically.
// dlmalloc is modified to result in failure if MMAP is called. In theory mmap could be supported, but we need to implement it to only commit/decommit pages from our shared memory.
//#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define MMAP_CLEARS 0

// Avoid mmap for now since it's not implemented
#define DEFAULT_TRIM_THRESHOLD MAX_SIZE_T
#define DEFAULT_MMAP_THRESHOLD MAX_SIZE_T
#define MAX_RELEASE_CHECK_RATE MAX_SIZE_T

// Disabled for simplicity
#define NO_MALLINFO 1
#define NO_MALLOC_STATS 1
