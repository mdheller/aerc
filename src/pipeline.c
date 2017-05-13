#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <termbox.h>
#include "config.h"
#include "subprocess.h"
#include "email/headers.h"
#include "util/list.h"
#include "state.h"
#include "worker.h"
#include "pipeline.h"
#include "ui.h"

// TODO: Global
static struct subprocess *header_subp;

static int header_cmp(const void *_a, const void *_b) {
	const char *a = _a;
	const char *b = _b;
	return strcmp(a, b);
}

static void add_header(void *_header) {
	struct email_header *header = _header;
	if (list_seq_find(config->ui.show_headers, header_cmp, header->key) == -1) {
		return;
	}
	int len = snprintf(NULL, 0, "%s: %s\n", header->key, header->value);
	char *h = malloc(len + 1);
	snprintf(h, len + 1, "%s: %s\n", header->key, header->value);
	subprocess_queue_stdin(header_subp, (uint8_t *)h, len);
}

struct pipeline_state {
	struct account_state *account;
	struct aerc_message *msg;
	struct aerc_message_part *part;
};

static void subp_complete(struct subprocess *subp) {
	struct pipeline_state *state = subp->user;
	free(state);
}

static void spawn_subprocess(struct account_state *account,
		struct aerc_message *msg, struct aerc_message_part *part) {
	struct pipeline_state *state = calloc(1, sizeof(struct pipeline_state));
	state->account = account;
	state->msg = msg;
	state->part = part;

	char *argv[] = { "sh", "-c", "fold -sw $(tput cols) | less", NULL };
	struct subprocess *subp = subprocess_init(argv, true);
	subp->user = state;
	subp->complete = subp_complete;
	header_subp = subp;
	list_foreach(msg->headers, add_header);
	subprocess_queue_stdin(subp, part->content, part->size);
	account->viewer.term = subp;
	subprocess_start(subp);
}

void spawn_email_handler(struct account_state *account,
		struct aerc_message *msg) {
	struct geometry geo;
	message_view_geometry(&geo);
	char width[20] = { 0 }, height[20] = { 0 };
	snprintf(width, sizeof(width) - 1, "%d", geo.width);
	snprintf(height, sizeof(height) - 1, "%d", geo.height);
	setenv("WIDTH", width, 1);
	setenv("HEIGHT", height, 1);

	struct aerc_message_part *part = NULL;
	for (size_t i = 0; i < config->viewer.alternatives->length; ++i) {
		struct mimetype *mime = config->viewer.alternatives->items[i];
		for (size_t j = 0; j < msg->parts->length; ++j) {
			struct aerc_message_part *_part = msg->parts->items[j];
			if (strcmp(_part->type, mime->type) == 0) {
				if (strcmp(mime->subtype, "*") == 0 ||
						strcmp(mime->subtype, _part->type) == 0) {
					part = _part;
					break;
				}
			}
		}
	}
	if (!part) {
		for (size_t i = 0; i < msg->parts->length; ++i) {
			part = msg->parts->items[i];
			if (strcasecmp(part->type, "text") == 0) {
				break;
			} else {
				part = NULL;
			}
		}
	}
	if (!part) {
		return;
	}
	spawn_subprocess(account, msg, part);
}
