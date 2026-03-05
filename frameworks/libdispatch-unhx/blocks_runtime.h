/*
 * frameworks/libdispatch/blocks_runtime.h — Blocks runtime support
 *
 * Provides _Block_copy, _Block_release, and block class symbols
 * required by Clang's blocks extension.
 */

#ifndef BLOCKS_RUNTIME_H
#define BLOCKS_RUNTIME_H

#include <stdint.h>

/* Block descriptor */
struct Block_descriptor {
    unsigned long reserved;
    unsigned long size;
    void (*copy_helper)(void *dst, void *src);
    void (*dispose_helper)(void *src);
};

/* Block layout — matches Clang's ABI */
struct Block_layout {
    void *isa;
    int   flags;
    int   reserved;
    void (*invoke)(void *, ...);
    struct Block_descriptor *descriptor;
    /* Captured variables follow */
};

/* Block flags */
#define BLOCK_NEEDS_FREE     (1 << 24)
#define BLOCK_HAS_COPY       (1 << 25)
#define BLOCK_IS_GLOBAL      (1 << 28)
#define BLOCK_REFCOUNT_MASK  (0xFFFFu)

/* Block class symbols */
extern void *_NSConcreteStackBlock;
extern void *_NSConcreteGlobalBlock;
extern void *_NSConcreteMallocBlock;

/* Copy a block from stack to heap */
void *_Block_copy(const void *block);

/* Release a heap block */
void _Block_release(const void *block);

#endif /* BLOCKS_RUNTIME_H */
