#ifndef FVFX_CIRCULAR_BUFFER
#define FVFX_CIRCULAR_BUFFER

#include "stdbool.h"
#include "stdint.h"
#include <stdatomic.h>

typedef struct {
    void *items;
    size_t item_size;
    size_t count;
    atomic_int read_cur;
    atomic_int write_cur;
} CircularBuffer;

CircularBuffer initCircularBuffer(size_t item_size, size_t count);
void* readCircularBuffer(CircularBuffer* circularBuffer);
bool writeCircularBuffer(CircularBuffer* circularBuffer, void* item);
void freeCircularBuffer(CircularBuffer circularBuffer);
bool canWriteCircularBuffer(CircularBuffer* circularBuffer);
void resetCircularBuffer(CircularBuffer* circularBuffer);

#endif