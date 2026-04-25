#ifndef MAP_H
#define MAP_H

#include <stdint.h>
#include <stdbool.h>
#include "breakpoint.h"

typedef void cleanupfunction(breakpoint*);
typedef struct UnorderedMap map;

map* map_init(uint32_t size, cleanupfunction* cf);
bool map_insert(map* ht, uintptr_t key, breakpoint* obj);
void* map_lookup(map* ht, uintptr_t key);
bool map_it_exsists(map* ht, uintptr_t key);
void map_delete(map* ht, uintptr_t key);
void map_free(map* ht);

#endif
