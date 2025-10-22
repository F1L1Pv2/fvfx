#include "arena_alloc.h"
#include <malloc.h>
#include <string.h>
#include <assert.h>

void aa_init(ArenaAllocator* aa){
    aa->capacity = AA_INIT_CAP;
    aa->items = malloc(aa->capacity*sizeof(char));
    aa->count = 0;
}

void* aa_alloc(ArenaAllocator* aa, size_t size){
    if(aa->capacity == 0) aa_init(aa);
    if(aa->count + size >= aa->capacity){
        if(aa->next == NULL) aa->next = calloc(sizeof(ArenaAllocator), 1);
        return aa_alloc(aa->next, size);
    }
    void* out = aa->items+aa->count;
    aa->count += size;
    return out;
}

char* aa_strdup(ArenaAllocator* aa, const char* str){
    size_t len = strlen(str);
    char* out = aa_alloc(aa, len+1);
    assert(out != NULL && "UNREACHABLE");
    memcpy(out, str, len);
    out[len] = '\0';
    return out;
}

void aa_reset(ArenaAllocator* aa){
    aa->count = 0;
    if(aa->next) aa_reset(aa->next);
}