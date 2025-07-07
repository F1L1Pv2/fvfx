#ifndef FVFX_STRING_ALLOCATOR
#define FVFX_STRING_ALLOCATOR

#include <stdlib.h>

typedef struct StringAllocator StringAllocator;

#ifndef SA_INIT_CAP
#define SA_INIT_CAP 1024*1024
#endif

struct StringAllocator{
    char* items;
    size_t count;
    size_t capacity;
    StringAllocator* next;
};

char* sa_strdup(StringAllocator* sa, const char* str);
void sa_reset(StringAllocator* sa);

#endif
