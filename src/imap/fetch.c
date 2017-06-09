/*
 * imap/select.c - issues and handles IMAP SELECT commands, as well as common
 * responses from SELECT commands
 */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "email/encodings.h"
#include "email/headers.h"
#include "imap/date.h"
#include "imap/imap.h"
#include "internal/imap.h"
#include "log.h"
#include "util/list.h"
#include "util/stringop.h"
#include "util/base64.h"
#include "util/iconv.h"

void imap_fetch(struct imap_connection *imap, imap_callback_t callback,
		void *data, size_t min, size_t max, const char *what) {
	bool separate = false;
	// TODO: More optimal strategy for splitting up a range with intermetient
	// fetching messages into several sub-ranges instead of sending each
	// individually
	struct mailbox *mbox = get_mailbox(imap, imap->selected);
	assert(min >= 1);
	assert(max <= mbox->messages->length);
	assert(min <= max);
	for (size_t i = min; i < max; ++i) {
		struct mailbox_message *msg = mbox->messages->items[i - 1];
		if (msg->fetching) {
			separate = true;
			break;
		}
		msg->fetching = true;
	}

	if (separate && false) {
		// TODO
	} else {
		if (min == max) {
			imap_send(imap, callback, data, "FETCH %d (%s)", min, what);
		} else {
			imap_send(imap, callback, data, "FETCH %d:%d (%s)", min, max, what);
		}
	}
}

static int handle_flags(struct mailbox_message *msg, imap_arg_t *args) {
	args = args->list;
	free_flat_list(msg->flags);
	msg->flags = create_list();
	while (args) {
		assert(args->type == IMAP_ATOM);
		list_add(msg->flags, strdup(args->str));
		worker_log(L_DEBUG, "Set flag for message: %s", args->str);
		args = args->next;
	}
	return 0;
}

static int handle_uid(struct mailbox_message *msg, imap_arg_t *args) {
	assert(args->type == IMAP_NUMBER);
	worker_log(L_DEBUG, "Message UID: %ld", args->num);
	msg->uid = args->num;
	return 0;
}

static int handle_internaldate(struct mailbox_message *msg, imap_arg_t *args) {
	assert(args->type == IMAP_STRING);
	msg->internal_date = malloc(sizeof(struct tm));
	char *r = parse_imap_date(args->str, msg->internal_date);
	if (!r || *r) {
		worker_log(L_DEBUG, "Warning: received invalid date for message (%s)",
				args->str);
	} else {
		char date[64];
		strftime(date, sizeof(date), "%F %H:%M %z", msg->internal_date);
		worker_log(L_DEBUG, "Message internal date: %s", date);
	}
	return 0;
}

static void handle_body_content(struct message_part *part, imap_arg_t *args) {
	free(part->content);
	part->content = malloc(part->size);
	memcpy(part->content, args->str, part->size);
	worker_log(L_DEBUG, "Received message body");
	if (part->body_encoding) {
		if (strcasecmp(part->body_encoding, "7bit") == 0 ||
			strcasecmp(part->body_encoding, "8bit") == 0 ||
			strcasecmp(part->body_encoding, "binary") == 0) {
			// no further action necessary
		} else if (strcasecmp(part->body_encoding, "quoted-printable") == 0) {
			int len = quoted_printable_decode((char *)part->content, part->size, QP_BODY);
			part->size = len;
		} else if (strcasecmp(part->body_encoding, "base64") == 0) {
			size_t len;
			char *b64 = (char *)part->content;
			unsigned char *plain = b64_decode(b64, part->size, &len);
			if (!plain) {
				worker_log(L_ERROR, "Invalid base64 data in message.");
				return;
			}
			free(part->content);
			part->content = plain;
			part->size = strlen((char *)plain);
		} else {
			worker_log(L_ERROR, "Unknown encoding %s. Please report this.", part->body_encoding);
		}
	}
	for (size_t i = 0; i < part->parameters->length; ++i) {
		struct message_parameter *param = part->parameters->items[i];
		if (strcasecmp(param->key, "charset") == 0) {
			if (strcasecmp(param->value, "UTF-8") == 0) {
				// no further action necessary
			} else if (strcasecmp(param->value, "iso-8859-1") == 0) {
				int len = iso_8859_1_to_utf8(&part->content, part->size);
				part->size = len;
			} else if (strcasecmp(param->value, "us-ascii") == 0) {
				// no further action necessary
			} else {
				unsigned char *old = part->content, *new;
				size_t news;
				worker_log(L_DEBUG, "Converting message encoding from %s", param->value);
				if (!(new = iconv_convert4((char *)part->content, param->value, part->size, &news))) {
					continue;
				}
				free(old);
				part->content = new, part->size = news;
			}
		}
	}
}

static int flag_cmp(const void *_item, const void *_flag) {
	const char *item = _item;
	const char *flag = _flag;
	return strcmp(item, flag);
}

static int handle_body(struct mailbox_message *msg, imap_arg_t *args) {
	assert(args->type == IMAP_RESPONSE);
	worker_log(L_DEBUG, "Handling message body fields");
	imap_arg_t *resp = calloc(1, sizeof(imap_arg_t));
	int _;
	imap_parse_args(args->str, resp, &_);
	assert(_ == 2); // imap_parse_args expects \r\n, not present
	args = args->next;
	assert(args);
	switch (resp->type) {
	case IMAP_ATOM:
		if (strcmp(resp->str, "HEADER.FIELDS") == 0) {
			list_t *headers = create_list();
			parse_headers(args->str, headers);
			free_headers(msg->headers);
			msg->headers = headers;
			worker_log(L_DEBUG, "Received message headers");
		}
		break;
	case IMAP_NUMBER: {
		size_t i = resp->num - 1;
		assert(msg->parts);
		assert(i < msg->parts->length);
		int seen = list_seq_find(msg->flags, flag_cmp, "\\Seen");
		if (seen != -1) {
			list_add(msg->flags, strdup("\\Seen"));
		}
		struct message_part *part = msg->parts->items[i];
		handle_body_content(part, args);
		break;
	}
	default:
		// ¯\_(ツ)_/¯
		break;
	}
	imap_arg_free(resp);
	return 1; // We used one extra argument
}

static char *get_str(imap_arg_t *args) {
	if (!args->str || strcmp(args->str, "NIL") == 0) {
		return NULL;
	}
	return strdup(args->str);
}

static struct message_part *handle_message_part(imap_arg_t *args) {
	struct message_part *part = calloc(sizeof(struct message_part), 1);
	assert(part);
	assert(args);
	part->type = get_str(args);
	args = args->next;

	part->subtype = get_str(args);
	args = args->next;

	imap_arg_t *param = args->list;
	part->parameters = create_list();
	while (param) {
		char *key = get_str(param);
		param = param->next;
		char *value = get_str(param);
		param = param->next;
		struct message_parameter *mparam = calloc(
				sizeof(struct message_parameter), 1);
		mparam->key = key;
		mparam->value = value;
		list_add(part->parameters, mparam);
	}
	args = args->next;

	part->body_id = get_str(args);
	args = args->next;

	part->body_description = get_str(args);
	args = args->next;

	part->body_encoding = get_str(args);
	args = args->next;

	part->size = args->num;

	worker_log(L_DEBUG, "Parsed message part: %s/%s, %s / %s / %s / %ld bytes",
			part->type, part->subtype, part->body_id, part->body_description,
			part->body_encoding, part->size);
	return part;
}

static void extract_parts(struct mailbox_message *msg, imap_arg_t *args) {
	if (args->type == IMAP_LIST) {
		// Multipart
		args = args->list;
		while (args) {
			extract_parts(msg, args);
			args = args->next;
			if (args->type == IMAP_STRING) {
				msg->multipart_type = get_str(args);
				break;
			}
		}
	} else {
		struct message_part *part = handle_message_part(args);
		list_add(msg->parts, part);
	}
}

static int handle_bodystructure(struct mailbox_message *msg, imap_arg_t *args) {
	assert(args->type == IMAP_LIST);
	if (!msg->parts) {
		msg->parts = create_list();
	} else {
		for (size_t i = 0; i < msg->parts->length; ++i) {
			struct message_part *p = msg->parts->items[i];
			message_part_free(p);
		}
		list_free(msg->parts);
		msg->parts = create_list();
	}
	extract_parts(msg, args->list);
	return 0;
}

void handle_imap_fetch(struct imap_connection *imap, const char *token,
		const char *cmd, imap_arg_t *args) {
	assert(args->type == IMAP_NUMBER);
	char *selected = imap->selected;
	struct mailbox *mbox = get_mailbox(imap, selected);
	int index = args->num - 1;
	struct mailbox_message *msg = get_message(mbox, index);
	worker_log(L_DEBUG, "Received FETCH for message %d", index + 1);
	args = args->next;
	assert(args->type == IMAP_LIST);
	assert(!args->next);
	args = args->list;

	const struct {
		const char *name;
		enum imap_type expected_type;
		int (*handler)(struct mailbox_message *, imap_arg_t *);
	} handlers[] = {
		{ "UID", IMAP_NUMBER, handle_uid },
		{ "FLAGS", IMAP_LIST, handle_flags },
		{ "INTERNALDATE", IMAP_STRING, handle_internaldate },
		{ "BODY", IMAP_RESPONSE, handle_body },
		{ "BODYSTRUCTURE", IMAP_LIST, handle_bodystructure },
	};
	bool handled[sizeof(handlers) / sizeof(handlers[0])] = { false };

	while (args) {
		const char *name = args->str;
		args = args->next;
		if (!args) {
			break;
		}
		for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
			if (strcmp(handlers[i].name, name) == 0) {
				assert(args->type == handlers[i].expected_type);
				int j = handlers[i].handler(msg, args);
				handled[i] = true;
				while (j-- && args) args = args->next;
			}
		}
		if (args) {
			args = args->next;
		}
	}
	msg->fetching = false;

	msg->populated = true;
	for (size_t i = 0; i < sizeof(handled) / sizeof(handled[0]); ++i) {
		worker_log(L_DEBUG, "%s was %shandled", handlers[i].name, handled[i] ? "" : "not ");
		msg->populated &= handled[i];
	}

	if (imap->events.message_updated) {
		imap->events.message_updated(imap, msg);
	}
}
