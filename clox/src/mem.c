#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* alloc(size_t size)
{
    void* mem = malloc(size);
    if (mem == NULL) {
        fprintf(stderr, "No More Memory to allocate %zu bytes\n", size);
    }
    return mem;
}

// 内存管理，用来删除整个内存
void fr(void* mem)
{
    free(mem);
}

// copy
void* clone(void* src, size_t size)
{
    void* dst = alloc(size);
    memcpy(dst, src, size);
    return dst;
}

void* reallocate(void* previous, size_t oldSize, size_t newSize)
{
    if (newSize == 0) {
        free(previous);
        return NULL;
    }

    return realloc(previous, newSize);
}
