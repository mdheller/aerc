/*
 * imap/worker/select.c - Handles IMAP worker mailbox select actions
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>

#include "imap/imap.h"
#include "worker.h"
#include "log.h"

static void select_done(struct imap_connection *imap,
		void *data, enum imap_status status, const char *args) {
	struct worker_message *msg = data;
	struct worker_pipe *pipe = imap->data;
	if (status == STATUS_OK) {
		worker_log(L_DEBUG, "Selected %s", imap->selected);
		worker_post_message(pipe, WORKER_OKAY, msg, imap->selected);
	} else {
		// TODO: Include a more descriptive error message?
		worker_post_message(pipe, WORKER_ERROR, msg, NULL);
	}
}

void handle_worker_select_mailbox(struct worker_pipe *pipe, struct worker_message *message) {
	/*
	 * This issues two IMAP commands - one to find the delimiter, and one to
	 * do the actual listing.
	 */
	struct imap_connection *imap = pipe->data;
	worker_post_message(pipe, WORKER_ACK, message, NULL);
	imap_select(imap, select_done, message, (const char *)message->data);
}
