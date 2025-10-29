#ifndef LINKED_LIST_HEADER
#define LINKED_LIST_HEADER

void* linked_list_add_common(void** dynamic_list_ptr,void* item,size_t item_size,size_t next_member_offsetof,void* (*alloc_func)(size_t size, void* caller_data), void* caller_data);

// item is passed by value
#define ll_push(ll_ptr, item, alloc_function, caller_data) (linked_list_add_common((void**)(ll_ptr),&(item),sizeof(item), offsetof(__typeof__(item), next), (alloc_function), (caller_data)))

// returns NULL on accessing out of bounds
void* linked_list_get_at(void* linked_list, size_t next_member_offsetof, size_t i);

#define ll_at(ll, index) (linked_list_get_at(ll, offsetof(__typeof__(*(ll)), next), index))

void linked_list_qsort(void** list_ptr, size_t next_member_offsetof, int (*cmp_func)(const void*, const void*));

#define ll_qsort(ll_ptr, cmp_func) \
    (linked_list_qsort((void**)(ll_ptr), offsetof(__typeof__(**(ll_ptr)), next), (cmp_func)))

#ifdef LINKED_LIST_IMPLEMENTATION
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
void* linked_list_add_common(
    void** dynamic_list_ptr,
    void* item,
    size_t item_size,
    size_t next_member_offsetof,
    void* (*alloc_func)(size_t size, void* caller_data),
    void* caller_data
){
    if (dynamic_list_ptr == NULL) return NULL;
    if (*dynamic_list_ptr == NULL) {
        *dynamic_list_ptr = alloc_func(item_size, caller_data);
        memcpy(*dynamic_list_ptr, item, item_size);
        *(void**)(((char*)*dynamic_list_ptr) + next_member_offsetof) = NULL;
        return *dynamic_list_ptr;
    }
    void* current_item = *dynamic_list_ptr;
    while (*(void**)(((char*)current_item) + next_member_offsetof) != NULL) {
        current_item = *(void**)(((char*)current_item) + next_member_offsetof);
    }
    void* new_node = alloc_func(item_size, caller_data);
    memcpy(new_node, item, item_size);
    *(void**)(((char*)new_node) + next_member_offsetof) = NULL;
    *(void**)(((char*)current_item) + next_member_offsetof) = new_node;
    return new_node;
}

void* linked_list_get_at(void* linked_list, size_t next_member_offsetof, size_t i){
    void* current_item = linked_list;
    size_t n = 0;
    while (current_item != NULL && n < i) {
        current_item = *(void**)(((char*)current_item) + next_member_offsetof);
        n++;
    }
    return current_item;
}

// count elements in the linked list
static size_t linked_list_count(void* linked_list, size_t next_member_offsetof) {
    size_t count = 0;
    void* current = linked_list;
    while (current) {
        count++;
        current = *(void**)(((char*)current) + next_member_offsetof);
    }
    return count;
}

// sort linked list using qsort
void linked_list_qsort(void** list_ptr, size_t next_member_offsetof, int (*cmp_func)(const void*, const void*)) {
    if (!list_ptr || !*list_ptr) return;

    size_t count = linked_list_count(*list_ptr, next_member_offsetof);
    if (count < 2) return;

    // Allocate array of pointers
    void** array = malloc(count * sizeof(void*));
    if (!array) return;

    void* current = *list_ptr;
    for (size_t i = 0; i < count; ++i) {
        array[i] = current;
        current = *(void**)(((char*)current) + next_member_offsetof);
    }

    // Sort array using standard qsort
    qsort(array, count, sizeof(void*), cmp_func);

    // Rebuild linked list
    for (size_t i = 0; i < count - 1; ++i) {
        *(void**)(((char*)array[i]) + next_member_offsetof) = array[i + 1];
    }
    *(void**)(((char*)array[count - 1]) + next_member_offsetof) = NULL;

    // Update list head
    *list_ptr = array[0];
    free(array);
}
#endif

#endif