#ifndef LINKED_LIST_HEADER
#define LINKED_LIST_HEADER

void linked_list_add_common(void** dynamic_list_ptr,void* item,size_t item_size,size_t next_member_offsetof,void* (*alloc_func)(size_t size, void* caller_data), void* caller_data);

// item is passed by value
#define ll_push(ll_ptr, item, alloc_function, caller_data) (linked_list_add_common((void**)(ll_ptr),&(item),sizeof(item), offsetof(__typeof__(item), next), (alloc_function), (caller_data)))

// returns NULL on accessing out of bounds
void* linked_list_get_at(void* linked_list, size_t next_member_offsetof, size_t i);

#define ll_at(ll, index) ((__typeof__(ll))linked_list_get_at(ll, offsetof(__typeof__(*(ll)), next), index))

#ifdef LINKED_LIST_IMPLEMENTATION
#include <stddef.h>
#include <assert.h>
#include <string.h>
void linked_list_add_common(
    void** dynamic_list_ptr,
    void* item,
    size_t item_size,
    size_t next_member_offsetof,
    void* (*alloc_func)(size_t size, void* caller_data),
    void* caller_data
){
    if (dynamic_list_ptr == NULL) return;
    if (*dynamic_list_ptr == NULL) {
        *dynamic_list_ptr = alloc_func(item_size, caller_data);
        memcpy(*dynamic_list_ptr, item, item_size);
        *(void**)(((char*)*dynamic_list_ptr) + next_member_offsetof) = NULL;
        return;
    }
    void* current_item = *dynamic_list_ptr;
    while (*(void**)(((char*)current_item) + next_member_offsetof) != NULL) {
        current_item = *(void**)(((char*)current_item) + next_member_offsetof);
    }
    void* new_node = alloc_func(item_size, caller_data);
    memcpy(new_node, item, item_size);
    *(void**)(((char*)new_node) + next_member_offsetof) = NULL;
    *(void**)(((char*)current_item) + next_member_offsetof) = new_node;
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
#endif

#endif