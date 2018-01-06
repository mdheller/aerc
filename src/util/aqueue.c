/*
 * aqueue.c - single producer/single consumer lockless asynchronous queue
 */
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "util/aqueue.h"

struct aqueue_node {
	void *value;
	struct aqueue_node *next;
};

typedef struct aqueue_node aqueue_node_t;

struct aqueue {
	aqueue_node_t *first;
	atomic_intptr_t head, tail;
};

aqueue_t *aqueue_new() {
	aqueue_t *q = malloc(sizeof(aqueue_t));
	if (!q) return NULL;
	aqueue_node_t* dummy = calloc(1, sizeof(aqueue_node_t));
	if (!dummy) {
		free(q);
		return NULL;
	}
	q->first = dummy;
	atomic_init(&q->head, (intptr_t)dummy);
	atomic_init(&q->tail, (intptr_t)dummy);
	return q;
}

void aqueue_free(aqueue_t *q) {
	while (q->first != NULL) {
		aqueue_node_t *node = q->first;
		q->first = node->next;
		free(node);
	}
	free(q);
}

bool aqueue_enqueue(aqueue_t *q, void *val) {
	aqueue_node_t *node = calloc(1, sizeof(aqueue_node_t));
	if (!node) {
		return false;
	}
	node->value = val;

	aqueue_node_t *tail = (aqueue_node_t*)atomic_load(&q->tail);
	tail->next = node;
	atomic_store(&q->tail, (intptr_t)node);

	while ((intptr_t)q->first != (intptr_t)atomic_load(&q->head)) {
		aqueue_node_t *n = q->first;
		q->first = n->next;
		free(n);
	}
	return true;
}

bool aqueue_dequeue(aqueue_t *q, void **val) {
	if (atomic_load(&q->head) != atomic_load(&q->tail)) {
		aqueue_node_t *node = (aqueue_node_t *)atomic_load(&q->head);
		*val = node->next->value;
		atomic_store(&q->head, (intptr_t)node->next);
		return true;
	}
	return false;
}
