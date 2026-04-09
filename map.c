#include "map.h"

#include <stdlib.h>
#include <assert.h>

typedef struct entry {
	long key;
	breakpoint* obj;
	struct entry* next;
} entry;

typedef struct UnorderedMap {
	uint32_t size;
	cleanupfunction* cf;
	entry** elements;
} map;

static size_t map_index(map* ht, long key) {
	size_t result = (size_t)key % ht->size;
	return result;
}

map* map_init(uint32_t size, cleanupfunction* cf) {
	map* ht = malloc(sizeof(map));
	ht->size = size;
	if (cf) {
		ht->cf = cf;
	} else {
		ht->cf = NULL;
	}
	ht->elements = calloc(ht->size, sizeof(entry*));
	return ht;
}

bool map_insert(map* ht, long key, breakpoint* obj) {
	if (ht == NULL || obj == NULL) return false;
	if (map_lookup(ht, key) != NULL) return false;
	size_t index = map_index(ht, key);

	// creating entry
	entry* e = malloc(sizeof(entry));
	e->obj = obj;
	e->key = key;

	// inserting entry
	e->next = ht->elements[index];
	ht->elements[index] = e;
	return true;
}

void* map_lookup(map* ht, long key) {
	if (ht == NULL) return NULL;
	size_t index = map_index(ht, key);
	entry* temp = ht->elements[index];
	while (temp != NULL && key == temp->key) {
		temp = temp->next;
	}
	if (temp == NULL) return NULL;
	return temp->obj;
}

bool map_it_exsists(map* ht, long key) {
	if (ht == NULL) return false;
	size_t index = map_index(ht, key);
	entry* temp = ht->elements[index];
	while (temp != NULL && key == temp->key) {
		temp = temp->next;
	}
    if (temp == NULL) return false;
    return true;
}

void* map_delete(map* ht, long key) {
	if (ht == NULL) return false;
	size_t index = map_index(ht, key);
	entry* temp = ht->elements[index];
	entry* prev = NULL;
	while (temp != NULL && temp->key == key) {
		prev = temp;
		temp = temp->next;
	}
	if (temp == NULL) return NULL;
	if (prev == NULL) {
		// deleting the head node
		ht->elements[index] = temp->next;
	} else {
		prev->next = temp->next;
	}
	void* result = temp->obj;
	free(temp);
	return result;
}

void map_free(map* ht) {
	if (ht == NULL) return;
	for (size_t i = 0; i < ht->size; i++) {
		while (ht->elements) {
			entry* temp = ht->elements[i];
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
