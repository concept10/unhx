/*
 * frameworks/libdispatch/blocks_runtime.c — Blocks runtime
 *
 * _Block_copy copies a stack block to the heap.
 * _Block_release decrements refcount and frees at zero.
 */

#include "blocks_runtime.h"
#include "malloc.h"
#include "string.h"

/* Block class symbols — referenced by compiler-emitted blocks */
void *_NSConcreteStackBlock  = 0;
void *_NSConcreteGlobalBlock = 0;
void *_NSConcreteMallocBlock = 0;

void *_Block_copy(const void *block)
{
    if (!block)
        return 0;

    struct Block_layout *src = (struct Block_layout *)block;

    /* Global blocks don't need copying */
    if (src->flags & BLOCK_IS_GLOBAL)
        return (void *)block;

    /* Already on heap? Increment refcount */
    if (src->flags & BLOCK_NEEDS_FREE) {
        src->flags += 1;  /* increment refcount in low bits */
        return (void *)block;
    }

    /* Stack → heap copy */
    unsigned long size = src->descriptor ? src->descriptor->size
                                         : sizeof(struct Block_layout);
    struct Block_layout *dst = (struct Block_layout *)malloc(size);
    if (!dst)
        return 0;

    memcpy(dst, src, size);
    dst->isa   = &_NSConcreteMallocBlock;
    dst->flags = BLOCK_NEEDS_FREE | 1;  /* refcount = 1 */

    /* Call copy helper if present */
    if ((src->flags & BLOCK_HAS_COPY) && src->descriptor &&
        src->descriptor->copy_helper) {
        src->descriptor->copy_helper(dst, src);
    }

    return dst;
}

void _Block_release(const void *block)
{
    if (!block)
        return;

    struct Block_layout *blk = (struct Block_layout *)block;

    /* Global blocks are never freed */
    if (blk->flags & BLOCK_IS_GLOBAL)
        return;

    /* Not a heap block? Nothing to do */
    if (!(blk->flags & BLOCK_NEEDS_FREE))
        return;

    /* Decrement refcount */
    int refcount = (blk->flags & BLOCK_REFCOUNT_MASK);
    if (refcount <= 1) {
        /* Call dispose helper if present */
        if ((blk->flags & BLOCK_HAS_COPY) && blk->descriptor &&
            blk->descriptor->dispose_helper) {
            blk->descriptor->dispose_helper((void *)blk);
        }
        free(blk);
    } else {
        blk->flags -= 1;
    }
}
