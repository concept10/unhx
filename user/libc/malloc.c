/*
 * user/libc/malloc.c — Free-list heap allocator for UNHOX userspace
 *
 * Backed by sbrk() (SYS_SBRK syscall).  Each allocation has a chunk
 * header storing the size and free status.  free() coalesces with the
 * next chunk if it is also free.
 */

#include "malloc.h"
#include "string.h"
#include "syscall.h"

struct chunk_header {
    size_t size;                /* usable bytes (not counting header) */
    int    is_free;
    struct chunk_header *next;
};

#define HEADER_SIZE  sizeof(struct chunk_header)
#define ALIGN16(x)   (((x) + 15) & ~(size_t)15)

static struct chunk_header *free_list = (void *)0;

static struct chunk_header *extend_heap(size_t size)
{
    size_t total = HEADER_SIZE + size;
    void *block = sbrk((long)total);
    if (block == (void *)-1)
        return (void *)0;

    struct chunk_header *hdr = (struct chunk_header *)block;
    hdr->size = size;
    hdr->is_free = 0;
    hdr->next = (void *)0;

    /* Append to the end of the free list */
    if (!free_list) {
        free_list = hdr;
    } else {
        struct chunk_header *cur = free_list;
        while (cur->next)
            cur = cur->next;
        cur->next = hdr;
    }

    return hdr;
}

void *malloc(size_t size)
{
    if (size == 0)
        return (void *)0;

    size = ALIGN16(size);

    /* Search free list for a suitable block */
    struct chunk_header *cur = free_list;
    while (cur) {
        if (cur->is_free && cur->size >= size) {
            /* Split if significantly larger */
            if (cur->size >= size + HEADER_SIZE + 16) {
                struct chunk_header *split = (struct chunk_header *)
                    ((char *)cur + HEADER_SIZE + size);
                split->size = cur->size - size - HEADER_SIZE;
                split->is_free = 1;
                split->next = cur->next;

                cur->size = size;
                cur->next = split;
            }
            cur->is_free = 0;
            return (char *)cur + HEADER_SIZE;
        }
        cur = cur->next;
    }

    /* No free block found — extend the heap */
    struct chunk_header *hdr = extend_heap(size);
    if (!hdr)
        return (void *)0;

    return (char *)hdr + HEADER_SIZE;
}

void free(void *ptr)
{
    if (!ptr)
        return;

    struct chunk_header *hdr = (struct chunk_header *)((char *)ptr - HEADER_SIZE);
    hdr->is_free = 1;

    /* Coalesce with the next chunk if it is also free */
    if (hdr->next && hdr->next->is_free) {
        hdr->size += HEADER_SIZE + hdr->next->size;
        hdr->next = hdr->next->next;
    }
}

void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    void *ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t new_size)
{
    if (!ptr)
        return malloc(new_size);

    if (new_size == 0) {
        free(ptr);
        return (void *)0;
    }

    struct chunk_header *hdr = (struct chunk_header *)((char *)ptr - HEADER_SIZE);
    if (hdr->size >= new_size)
        return ptr;

    void *new_ptr = malloc(new_size);
    if (!new_ptr)
        return (void *)0;

    memcpy(new_ptr, ptr, hdr->size);
    free(ptr);
    return new_ptr;
}
