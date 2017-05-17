/*
 * imap/worker/create.c - Handles IMAP worker create actions
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "imap/imap.h"
#include "internal/imap.h"
#include "worker.h"
#include "log.h"

void handle_worker_create_mailbox(struct worker_pipe *pipe, struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	worker_post_message(pipe, WORKER_ACK, message, NULL);
	imap_create(imap, NULL, NULL, (const char *)message->data);
	// TODO: Bubble errors/success up to main thread
	// Would be nice to have a generic transactional flow between threads
	// WORKER_* -> WORKER_ACK -> [process] -> WORKER_{DONE,ERROR}
	// With the original error attached and a hashtable of functions for
	// handling the result (similar to how the IMAP code works)
}
