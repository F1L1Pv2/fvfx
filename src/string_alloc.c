#include "string_alloc.h"
#include <malloc.h>
#include <string.h>

void sa_init(StringAllocator* sa){
    sa->capacity = SA_INIT_CAP;
    sa->items = calloc(sa->capacity, sizeof(char));
    sa->count = 0;
}

char* sa_strdup(StringAllocator* sa, const char* str){
    if(sa->capacity == 0) sa_init(sa);
    size_t len = strlen(str);
    if(sa->count + len + 1 >= sa->capacity){
        if(sa->next == NULL) sa->next = calloc(sizeof(StringAllocator), 1);
        return sa_strdup(sa->next, str);
    }

    char* out = sa->items+sa->count;
    memcpy(sa->items+sa->count, str, len);
    sa->count += len;
    sa->items[sa->count++] = '\0';
    return out;
}

void sa_reset(StringAllocator* sa){
    sa->count = 0;
    if(sa->next) sa_reset(sa->next);
}