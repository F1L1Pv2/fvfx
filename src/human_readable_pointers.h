// human_readable_pointers.h
// Single-header library to assign stable human-readable names to pointers.
//
// Usage:
//   #define HUMAN_READABLE_POINTERS_IMPLEMENTATION
//
//   #include "human_readable_pointers.h"
// Example:
//   void* p = malloc(1);
//   printf("%s\n", hrp_name(p));  // e.g. "grape"
//   printf("%s\n", hrp_name(p));  // same name every time
//
// MIT License
// Copyright 2025 F1L1P (Filip Młodzik)
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#ifndef HUMAN_READABLE_POINTERS_H
#define HUMAN_READABLE_POINTERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// API
const char* hrp_name(void* ptr);
void hrp_reset(void);

#ifdef __cplusplus
}
#endif

#endif

#ifdef HUMAN_READABLE_POINTERS_IMPLEMENTATION

// Word list
static const char* HRP_WORDS[] = {
    "apple", "banana", "orange", "grape", "pear", "peach", "cherry", "plum", "lemon", "lime",
    "strawberry", "blueberry", "raspberry", "melon", "watermelon", "mango", "pineapple", "coconut", "kiwi", "papaya",
    "wood", "stone", "metal", "iron", "gold", "silver", "copper", "glass", "plastic", "paper",
    "cloth", "wool", "cotton", "silk", "leather", "sand", "clay", "brick", "cement", "oil",
    "chair", "table", "book", "cup", "plate", "spoon", "fork", "knife", "door", "window",
    "bed", "lamp", "phone", "computer", "pen", "pencil", "bag", "shoe", "hat", "bottle",
    "dog", "cat", "bird", "fish", "horse", "cow", "sheep", "pig", "duck", "goat",
    "lion", "tiger", "bear", "wolf", "fox", "deer", "mouse", "rat", "rabbit", "monkey",
    "bread", "cheese", "milk", "butter", "egg", "rice", "bean", "soup", "meat", "cake",
    "sugar", "salt", "pepper", "honey", "coffee", "tea", "juice", "water", "flour", "corn",
    "tree", "flower", "grass", "leaf", "root", "branch", "seed", "fruit", "river", "lake",
    "sea", "ocean", "mountain", "hill", "valley", "rock", "cloud", "rain", "snow", "wind"
};

#define HRP_WORD_COUNT (sizeof(HRP_WORDS) / sizeof(HRP_WORDS[0]))
#define HRP_ARENA_SIZE 65536
#define HRP_MAX_ENTRIES 1024

static char hrp_arena[HRP_ARENA_SIZE];
static size_t hrp_arena_offset = 0;

static char* hrp_arena_alloc(size_t size) {
    if (hrp_arena_offset + size >= HRP_ARENA_SIZE) {
        fprintf(stderr, "HRP: Arena out of memory!\n");
        exit(1);
    }
    char* ptr = &hrp_arena[hrp_arena_offset];
    hrp_arena_offset += size;
    return ptr;
}

typedef struct {
    void* key;
    char* value;
} hrp_entry;

static hrp_entry hrp_table[HRP_MAX_ENTRIES];
static int hrp_word_usage[HRP_WORD_COUNT] = {0};

void hrp_reset(void) {
    memset(hrp_table, 0, sizeof(hrp_table));
    memset(hrp_word_usage, 0, sizeof(hrp_word_usage));
    hrp_arena_offset = 0;
}

static size_t hrp_hash_ptr(void* p) {
    return ((uintptr_t)p >> 3) % HRP_MAX_ENTRIES;
}

const char* hrp_name(void* ptr) {
    if (!ptr) return "null";

    size_t h = hrp_hash_ptr(ptr);
    for (size_t i = 0; i < HRP_MAX_ENTRIES; i++) {
        size_t idx = (h + i) % HRP_MAX_ENTRIES;

        if (hrp_table[idx].key == ptr) {
            return hrp_table[idx].value;
        } else if (hrp_table[idx].key == NULL) {
            size_t word_idx = (size_t)ptr % HRP_WORD_COUNT;
            int count = hrp_word_usage[word_idx]++;

            char temp[64];
            if (count == 0) {
                snprintf(temp, sizeof(temp), "%s", HRP_WORDS[word_idx]);
            } else {
                snprintf(temp, sizeof(temp), "%s%d", HRP_WORDS[word_idx], count + 1);
            }

            size_t len = strlen(temp) + 1;
            char* name = hrp_arena_alloc(len);
            memcpy(name, temp, len);

            hrp_table[idx].key = ptr;
            hrp_table[idx].value = name;
            return name;
        }
    }

    fprintf(stderr, "HRP: Too many entries in table!\n");
    exit(1);
}

#endif