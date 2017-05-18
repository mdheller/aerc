/*
 * imap/worker/worker.c - IMAP worker main thread and action dispatcher
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "worker.h"
#include "email/headers.h"
#include "imap/imap.h"
#include "imap/worker.h"
#include "internal/imap.h"
#include "log.h"
#include "util/list.h"

struct action_handler {
	enum worker_message_type action;
	void (*handler)(struct worker_pipe *pipe, struct worker_message *message);
};

struct action_handler handlers[] = {
	{ WORKER_CONNECT, handle_worker_connect },
	{ WORKER_LIST, handle_worker_list },
	{ WORKER_SELECT_MAILBOX, handle_worker_select_mailbox },
#ifdef USE_OPENSSL
	{ WORKER_CONNECT_CERT_OKAY, handle_worker_cert_okay },
#endif
	{ WORKER_CREATE_MAILBOX, handle_worker_create_mailbox },
	{ WORKER_FETCH_MESSAGES, handle_worker_fetch_messages },
	{ WORKER_FETCH_MESSAGE_PART, handle_worker_fetch_message_part },
	{ WORKER_DELETE_MAILBOX, handle_worker_delete_mailbox },
	{ WORKER_DELETE_MESSAGE, handle_worker_delete_message },
	{ WORKER_COPY_MESSAGE, handle_worker_copy_message },
	{ WORKER_MOVE_MESSAGE, handle_worker_move_message },
};

void handle_message(struct worker_pipe *pipe, struct worker_message *message) {
	for (size_t i = 0; i < sizeof(handlers) / sizeof(struct action_handler); i++) {
		if (handlers[i].action == message->type) {
			handlers[i].handler(pipe, message);
			return;
		}
	}
	worker_post_message(pipe, WORKER_UNSUPPORTED, message, NULL);
}

struct aerc_message *serialize_message(struct mailbox_message *source) {
	if (!source) return NULL;
	struct aerc_message *dest = calloc(1, sizeof(struct aerc_message));
	dest->index = source->index;
	dest->fetched = source->populated;
	if (!source->populated) {
		return dest;
	}
	dest->uid = source->uid;
	dest->flags = create_list();
	for (size_t i = 0; i < source->flags->length; ++i) {
		list_add(dest->flags, strdup(source->flags->items[i]));
	}
	dest->headers = create_list();
	for (size_t i = 0; i < source->headers->length; ++i) {
		struct email_header *header = source->headers->items[i];
		struct email_header *copy = malloc(sizeof(struct email_header));
		copy->key = strdup(header->key);
		copy->value = strdup(header->value);
		list_add(dest->headers, copy);
	}
	dest->internal_date = calloc(1, sizeof(struct tm));
	memcpy(dest->internal_date, source->internal_date, sizeof(struct tm));
	if (source->parts) {
		dest->parts = create_list();
		for (size_t i = 0; i < source->parts->length; ++i) {
			struct message_part *spart = source->parts->items[i];
			struct aerc_message_part *dpart =
				calloc(sizeof(struct aerc_message_part), 1);
			// TODO: parameters, if anyone gives a shit
			if (spart->type) dpart->type = strdup(spart->type);
			if (spart->subtype) dpart->subtype = strdup(spart->subtype);
			if (spart->body_id) dpart->body_id = strdup(spart->body_id);
			if (spart->body_description) dpart->body_description = strdup(spart->body_description);
			if (spart->body_encoding) dpart->body_encoding = strdup(spart->body_encoding);
			dpart->size = spart->size;
			if (spart->content) dpart->content = spart->content; // Not duplicated
			list_add(dest->parts, dpart);
		}
	}
	return dest;
}

struct aerc_mailbox *serialize_mailbox(struct mailbox *source) {
	struct aerc_mailbox *dest = calloc(1, sizeof(struct mailbox));
	dest->name = strdup(source->name);
	dest->exists = source->exists;
	dest->recent = source->recent;
	dest->unseen = source->unseen;
	dest->selected = source->selected;
	dest->flags = create_list();
	for (size_t i = 0; i < source->flags->length; ++i) {
		struct mailbox_flag *flag = source->flags->items[i];
		// TODO: Send along the permanent bool as well
		list_add(dest->flags, strdup(flag->name));
	}
	dest->messages = create_list();
	for (size_t i = 0; i < source->messages->length; ++i) {
		list_add(dest->messages, serialize_message(source->messages->items[i]));
	}
	return dest;
}

static void update_mailbox(struct imap_connection *imap, struct mailbox *updated) {
	struct aerc_mailbox *mbox = serialize_mailbox(updated);
	struct worker_pipe *pipe = imap->data;
	worker_post_message(pipe, WORKER_MAILBOX_UPDATED, NULL, mbox);
}

static void update_message(struct imap_connection *imap,
		struct mailbox_message *msg) {
	struct aerc_message *aerc_msg = serialize_message(msg);
	struct worker_pipe *pipe = imap->data;
	struct aerc_message_update *update = calloc(1, sizeof(struct aerc_message_update));
	update->message = aerc_msg;
	update->mailbox = strdup(imap->selected);
	worker_post_message(pipe, WORKER_MESSAGE_UPDATED, NULL, update);
}

static void delete_mailbox(struct imap_connection *imap, const char *mailbox) {
	struct worker_pipe *pipe = imap->data;
	worker_post_message(pipe, WORKER_MAILBOX_DELETED, NULL, strdup(mailbox));
}

static void delete_message(struct imap_connection *imap,
		struct mailbox_message *msg) {
	struct worker_pipe *pipe = imap->data;
	struct aerc_message_delete *event = calloc(1, sizeof(struct aerc_message_delete));
	event->index = msg->index;
	worker_post_message(pipe, WORKER_MESSAGE_DELETED, NULL, event);
}

void *imap_worker(void *_pipe) {
	/* Worker thread main loop */
	struct worker_pipe *pipe = _pipe;
	struct worker_message *message;
	struct imap_connection *imap = calloc(1, sizeof(struct imap_connection));
	pipe->data = imap;
	imap->data = pipe;
	imap->events.mailbox_updated = update_mailbox;
	imap->events.mailbox_deleted = delete_mailbox;
	imap->events.message_updated = update_message;
	imap->events.message_deleted = delete_message;
	worker_log(L_DEBUG, "Starting IMAP worker");
	while (1) {
		bool sleep = true;
		if (worker_get_action(pipe, &message)) {
			if (message->type == WORKER_SHUTDOWN) {
				imap_close(imap);
				free(imap);
				worker_message_free(message);
				return NULL;
			} else {
				handle_message(pipe, message);
			}
			worker_message_free(message);
			sleep = false;
		}
		if (imap_receive(imap)) {
			sleep = false;
		}
		if (sleep) {
			// We only sleep if we aren't working
			// Side note, it is currently 4:39 AM
			struct timespec spec = { 0, .5e+8 };
			nanosleep(&spec, NULL);
		}
	}
	return NULL;
}
