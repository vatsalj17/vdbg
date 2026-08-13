#include "hashmap.h"

#include <stdlib.h>
#include <assert.h>

typedef struct entry {
	uintptr_t key;
	breakpoint_t *obj;
	struct entry *next;
} entry;

// using fibonacci hashing cause it's better for storing
// 64-bit virtual memory addresses
// ref.:- https://smplu.link/fibonacci-hashing
typedef struct UnorderedMap {
	uint32_t size;
	uint8_t shift_bits;
	cleanupfunction *cf;
	entry **elements;
} map_t;

static inline uint32_t map_index(map_t *ht, uint64_t key) {
	uint32_t result = (uint32_t)((key * 11400714819323198485lu) >> ht->shift_bits);
	return result;
}

map_t *map_init(uint32_t size, cleanupfunction *cf) {
	if (size == 0) return NULL;
	map_t *ht = malloc(sizeof(map_t));
	ht->size = size;
	if (cf) {
		ht->cf = cf;
	} else {
		ht->cf = NULL;
	}
	ht->shift_bits = 64 - (uint8_t)__builtin_ctz(size); // 64 - log2(size)
	// __builtin_ctz counts trailing zeros from the lsb

	ht->elements = calloc(ht->size, sizeof(entry *));
	return ht;
}

bool map_insert(map_t *ht, uintptr_t key, breakpoint_t *obj) {
	if (ht == NULL || obj == NULL) return false;
	if (map_lookup(ht, key) != NULL) return false;
	size_t index = map_index(ht, key);

	// creating entry
	entry *e = malloc(sizeof(entry));
	e->obj = obj;
	e->key = key;

	// inserting entry
	e->next = ht->elements[index];
	ht->elements[index] = e;
	return true;
}

void *map_lookup(map_t *ht, uintptr_t key) {
	if (ht == NULL) return NULL;
	size_t index = map_index(ht, key);
	entry *temp = ht->elements[index];
	while (temp != NULL && key != temp->key) {
		temp = temp->next;
	}
	if (temp == NULL) return NULL;
	return temp->obj;
}

void map_delete(map_t *ht, uintptr_t key) {
	if (ht == NULL) return;
	size_t index = map_index(ht, key);
	entry *temp = ht->elements[index];
	entry *prev = NULL;
	while (temp != NULL && temp->key != key) {
		prev = temp;
		temp = temp->next;
	}
	if (temp == NULL) return;
	if (prev == NULL) {
		// deleting the head node
		ht->elements[index] = temp->next;
	} else {
		prev->next = temp->next;
	}
	ht->cf(temp->obj);
	free(temp);
}

void map_free(map_t *ht) {
	if (ht == NULL) return;
	for (size_t i = 0; i < ht->size; i++) {
		while (ht->elements[i]) {
			entry *temp = ht->elements[i];
			if (temp == NULL) break;
			ht->elements[i] = ht->elements[i]->next;
			assert(ht->cf);
			ht->cf(temp->obj);
			free(temp);
		}
	}
	free(ht->elements);
	free(ht);
}
