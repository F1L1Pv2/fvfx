#include "circular_buffer.h"
#include <assert.h>
#include <malloc.h>
#include <string.h>

CircularBuffer initCircularBuffer(size_t item_size, size_t count){
    CircularBuffer circularbuffer = {0};

    circularbuffer.items = malloc(item_size*count);
    assert(circularbuffer.items && "BUY MORE RAM");
    circularbuffer.item_size = item_size;
    circularbuffer.count = count;

    return circularbuffer;
}

void* readCircularBuffer(CircularBuffer* circularBuffer){
    if(circularBuffer->read_cur == circularBuffer->write_cur) return NULL;
    void* data = circularBuffer->items + circularBuffer->item_size * circularBuffer->read_cur;
    circularBuffer->read_cur = (circularBuffer->read_cur + 1) % circularBuffer->count;
    return data;
}

bool canWriteCircularBuffer(CircularBuffer* circularBuffer){
    return !((circularBuffer->write_cur == circularBuffer->count-1 && circularBuffer->read_cur == 0) || (circularBuffer->write_cur + 1 == circularBuffer->read_cur));
}

bool writeCircularBuffer(CircularBuffer* circularBuffer, void* item){
    if(!canWriteCircularBuffer(circularBuffer)) return false;
    memcpy(circularBuffer->items + circularBuffer->item_size * circularBuffer->write_cur, item, circularBuffer->item_size);
    circularBuffer->write_cur = (circularBuffer->write_cur + 1) % circularBuffer->count;
    return true;
}

void freeCircularBuffer(CircularBuffer circularBuffer){
    free(circularBuffer.items);
}