/*
 * worker.c - support code for mail workers
 */
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "util/hashtable.h"
#include "util/aqueue.h"
#include "worker.h"

static unsigned int hash_ptr(const void *ptr) {
	return (unsigned int)((uintptr_t)ptr * 2654435761);
}

struct worker_pipe *worker_pipe_new() {
	struct worker_pipe *pipe = calloc(1, sizeof(struct worker_pipe));
	if (!pipe) return NULL;
	pipe->messages = aqueue_new();
	pipe->actions = aqueue_new();
	pipe->callbacks = create_hashtable(16, hash_ptr);
	if (!pipe->messages || !pipe->actions) {
		aqueue_free(pipe->messages);
		aqueue_free(pipe->actions);
		free(pipe);
		return NULL;
	}
	return pipe;
}

void worker_pipe_free(struct worker_pipe *pipe) {
	aqueue_free(pipe->messages);
	aqueue_free(pipe->actions);
	free(pipe);
}

static bool _worker_get(aqueue_t *queue,
		struct worker_message **message) {
	void *msg;
	if (aqueue_dequeue(queue, &msg)) {
		*message = (struct worker_message *)msg;
		return true;
	}
	return false;
}

bool worker_get_message(struct worker_pipe *pipe,
		struct worker_message **message) {
	if (_worker_get(pipe->messages, message)) {
		if ((*message)->in_response_to &&
				((*message)->type == WORKER_OKAY || (*message)->type == WORKER_ERROR)) {
			struct worker_task *task = hashtable_del(pipe->callbacks,
					(*message)->in_response_to);
			if (task && task->callback) {
				task->callback(pipe, task->data, *message);
				free(task);
			}
		}
		return true;
	}
	return false;
}

bool worker_get_action(struct worker_pipe *pipe,
		struct worker_message **message) {
	return _worker_get(pipe->actions, message);
}

static struct worker_message *_worker_post(aqueue_t *queue,
		enum worker_message_type type,
		struct worker_message *in_response_to,
		void *data) {
	struct worker_message *message = calloc(1, sizeof(struct worker_message));
	if (!message) {
		fprintf(stderr, "Unable to allocate messages, aborting worker thread");
		pthread_exit(NULL);
		return NULL;
	}
	message->type = type;
	message->in_response_to = in_response_to;
	message->data = data;
	aqueue_enqueue(queue, message);
	return message;
}

void worker_post_message(struct worker_pipe *pipe,
		enum worker_message_type type,
		struct worker_message *in_response_to,
		void *data) {
	_worker_post(pipe->messages, type, in_response_to, data);
}

void worker_post_action(struct worker_pipe *pipe,
		enum worker_message_type type,
		struct worker_message *in_response_to,
		void *data, worker_callback_t callback, void *cbdata) {
	struct worker_message *msg = _worker_post(pipe->actions,
			type, in_response_to, data);
	if (callback) {
		struct worker_task *task = malloc(sizeof(struct worker_task));
		task->callback = callback;
		task->data = cbdata;
		hashtable_set(pipe->callbacks, msg, task);
	}
}

void worker_message_free(struct worker_message *msg) {
	free(msg);
}
