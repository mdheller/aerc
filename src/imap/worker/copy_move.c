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
