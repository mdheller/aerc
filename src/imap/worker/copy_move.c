/*
 * imap/worker/copy_move.c - Handles IMAP worker copy/move actions
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include "imap/imap.h"
#include "internal/imap.h"
#include "worker.h"
#include "log.h"

void handle_worker_copy_message(struct worker_pipe *pipe, struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	struct aerc_message_move *move = message->data;
	worker_post_message(pipe, WORKER_ACK, message, NULL);
	imap_copy(imap, NULL, NULL, move->index + 1, move->destination);
	free(move->destination);
	free(move);
}

static void delete_message_done(struct imap_connection *imap,
		void *data, enum imap_status status, const char *args) {
	if (status == STATUS_OK) {
		imap_expunge(imap, NULL, NULL);
	}
}

static void move_complete(struct imap_connection *imap,
		void *data, enum imap_status status, const char *args) {
	struct aerc_message_move *move = data;
	imap_store(imap, delete_message_done, NULL, move->index, move->index,
			STORE_FLAGS_APPEND, "\\Deleted");
	free(move->destination);
	free(move);
}

void handle_worker_move_message(struct worker_pipe *pipe, struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	struct aerc_message_move *move = message->data;
	worker_post_message(pipe, WORKER_ACK, message, NULL);
	imap_copy(imap, move_complete, move, move->index + 1, move->destination);
}
