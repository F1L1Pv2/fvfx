#ifndef FVFX_ARENA_ALLOCATOR
#define FVFX_ARENA_ALLOCATOR

#include <stdlib.h>

typedef struct ArenaAllocator ArenaAllocator;

#ifndef AA_INIT_CAP
#define AA_INIT_CAP 1024*1024
#endif

struct ArenaAllocator{
    char* items;
    size_t count;
    size_t capacity;
    ArenaAllocator* next;
};

void* aa_alloc(ArenaAllocator* aa, size_t size);
char* aa_strdup(ArenaAllocator* aa, const char* str);
void aa_reset(ArenaAllocator* aa);

#endif
