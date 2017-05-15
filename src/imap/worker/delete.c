/*
 * imap/worker/delete.c - Handles IMAP worker delete actions
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include "imap/imap.h"
#include "internal/imap.h"
#include "worker.h"
#include "log.h"

void handle_worker_delete_mailbox(struct worker_pipe *pipe, struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	worker_post_message(pipe, WORKER_ACK, message, NULL);
	imap_delete(imap, NULL, NULL, (const char *)message->data);
}

static void delete_message_done(struct imap_connection *imap,
		void *data, enum imap_status status, const char *args) {
	if (status == STATUS_OK) {
		imap_expunge(imap, NULL, NULL);
	}
}

void handle_worker_delete_message(struct worker_pipe *pipe, struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	worker_post_message(pipe, WORKER_ACK, message, NULL);
	size_t *index = message->data;
	struct mailbox *mbox = get_mailbox(imap, imap->selected);
	struct mailbox_message *msg = mbox->messages->items[*index];
	worker_log(L_DEBUG, "Deleting message %d", msg->index);
	imap_store(imap, delete_message_done, NULL, msg->index, msg->index,
			STORE_FLAGS_APPEND, "\\Deleted");
	free(message->data);
}
