#ifndef MAP_H
#define MAP_H

#include <stdint.h>
#include <stdbool.h>

typedef void cleanupfunction(void *);
typedef struct UnorderedMap map_t;

map_t *map_init(uint32_t size, cleanupfunction *cf);
bool map_insert(map_t *ht, uintptr_t key, void *obj);
void *map_lookup(map_t *ht, uintptr_t key);
void map_delete(map_t *ht, uintptr_t key);
void map_free(map_t *ht);

#endif
