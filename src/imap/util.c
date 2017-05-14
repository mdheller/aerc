/*
 * imap/util.c - misc imap utility functions
 */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "imap/imap.h"
#include "email/headers.h"
#include "util/list.h"

static int get_mbox_compare(const void *_mbox, const void *_name) {
	const struct mailbox *mbox = _mbox;
	const char *name = _name;
	return strcmp(mbox->name, name);
}

struct mailbox *get_mailbox(struct imap_connection *imap, const char *name) {
	int i = list_seq_find(imap->mailboxes, get_mbox_compare, name);
	if (i == -1) {
		return NULL;
	}
	return imap->mailboxes->items[i];
}

struct mailbox *get_or_make_mailbox(struct imap_connection *imap,
		const char *name) {
	struct mailbox *mbox = get_mailbox(imap, name);
	if (!mbox) {
		mbox = malloc(sizeof(struct mailbox));
		mbox->name = strdup(name);
		mbox->flags = create_list();
		mbox->messages = create_list();
		mbox->exists = mbox->unseen = mbox->recent = -1;
		list_add(imap->mailboxes, mbox);
	}
	return mbox;
}

struct mailbox_flag *mailbox_get_flag(struct imap_connection *imap,
		const char *mbox, const char *flag) {
	const struct mailbox *box = get_mailbox(imap, mbox);
	if (!box) {
		return NULL;
	}
	for (size_t i = 0; i < box->flags->length; ++i) {
		struct mailbox_flag *f = box->flags->items[i];
		if (strcasecmp(f->name, flag) == 0) {
			return f;
		}
	}
	return NULL;
}

struct mailbox_message *get_message(struct mailbox *mbox, long index) {
	struct mailbox_message *msg = NULL;
	for (size_t i = 0; i < mbox->messages->length; ++i) {
		msg = mbox->messages->items[i];
		if (msg->index == index) {
			return msg;
		}
	}
	return NULL;
}

void message_part_free(struct message_part *msg) {
	if (!msg) {
		return;
	}
	free(msg->type);
	free(msg->subtype);
	free(msg->body_id);
	free(msg->body_description);
	free(msg->body_encoding);
	free(msg->content);
	for (size_t i = 0; msg->parameters && i < msg->parameters->length; ++i) {
		struct message_parameter *param = msg->parameters->items[i];
		free(param->key);
		free(param->value);
		free(param);
	}
	list_free(msg->parameters);
	free(msg);
}

void mailbox_message_free(struct mailbox_message *msg) {
	for (size_t i = 0; msg->flags && i < msg->flags->length; ++i) {
		char *f = msg->flags->items[i];
		free(f);
	}
	list_free(msg->flags);
	for (size_t i = 0; msg->parts && i < msg->parts->length; ++i) {
		struct message_part *part = msg->parts->items[i];
		message_part_free(part);
	}
	list_free(msg->parts);
	free_headers(msg->headers);
	free(msg->internal_date);
	free(msg);
}

void mailbox_free(struct mailbox *mbox) {
	for (size_t i = 0; i < mbox->flags->length; ++i) {
		struct mailbox_flag *f = mbox->flags->items[i];
		free(f->name);
		free(f);
	}
	list_free(mbox->flags);
	for (size_t i = 0; i < mbox->messages->length; ++i) {
		struct mailbox_message *m = mbox->messages->items[i];
		mailbox_message_free(m);
	}
	list_free(mbox->messages);
	free(mbox->name);
	free(mbox);
}
