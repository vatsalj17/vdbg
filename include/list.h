#ifndef LIST_H
#define LIST_H

#include <stddef.h> // for offsetof

// straight from the linux kernel :)

#define containerof(ptr, type, member)                                                             \
	({                                                                                             \
		const typeof(((type *)0)->member) *__mptr = (ptr);                                         \
		(type *)((char *)__mptr - offsetof(type, member));                                         \
	})

#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head)                                                           \
	for (pos = (head)->next, n = (pos)->next; pos != (head); pos = n, n = (pos)->next)

#define list_entry(ptr, type, member) containerof(ptr, type, member)

#define list_empty(head) ((head)->next == (head))

typedef struct list_node {
	struct list_node *next;
	struct list_node *prev;
} list_node;

static inline void list_init(list_node *head) {
	head->next = head;
	head->prev = head;
}

// insert new in between prev and next
static inline void list_add(list_node *new, list_node *prev, list_node *next) {
	new->next = next;
	new->prev = prev;
	prev->next = new;
	next->prev = new;
}

static inline void list_add_front(list_node *head, list_node *new) {
	list_add(new, head, head->next);
}

static inline void list_add_back(list_node *head, list_node *new) {
	list_add(new, head->prev, head);
}

static inline void list_delete(list_node *target) {
	target->prev->next = target->next;
	target->next->prev = target->prev;
}

#endif
