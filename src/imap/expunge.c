/*
 * imap/expunge.c - handles IMAP EXPUNGE commands
 */
#define _POSIX_C_SOURCE 201112LL
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "imap/imap.h"
#include "internal/imap.h"
#include "log.h"

void handle_imap_expunge(struct imap_connection *imap, const char *token,
		const char *cmd, imap_arg_t *args) {
	assert(args && args->type == IMAP_NUMBER);
	long i = args->num - 1;
	struct mailbox *mbox = get_mailbox(imap, imap->selected);
	struct mailbox_message *msg = NULL;
	worker_log(L_DEBUG, "Deleting message %d", (int)i);
	for (size_t j = 0; j < mbox->messages->length; ++j) {
		struct mailbox_message *_msg = mbox->messages->items[j];
		if (_msg->index > i) {
			--_msg->index;
		} else if (_msg->index == i) {
			msg = _msg;
			list_del(mbox->messages, j);
			--mbox->exists;
			--j;
		}
	}
	if (msg && imap->events.message_deleted) {
		imap->events.message_deleted(imap, msg);
	}
}
