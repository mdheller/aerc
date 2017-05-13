#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "config.h"
#include "subprocess.h"
#include "email/headers.h"
#include "util/list.h"
#include "state.h"
#include "worker.h"
#include "pipeline.h"

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

void spawn_email_handler(struct account_state *account, struct aerc_message *msg) {
	for (size_t i = 0; i < msg->parts->length; ++i) {
		struct aerc_message_part *part = msg->parts->items[i];
		if (strcasecmp(part->type, "text") == 0) {
			char *argv[] = { "sh", "-c", "fold -sw $(tput cols) | less", NULL };
			struct subprocess *subp = subprocess_init(argv, true);
			header_subp = subp;
			list_foreach(msg->headers, add_header);
			subprocess_queue_stdin(subp, part->content, part->size);
			account->viewer.term = subp;
			subprocess_start(subp);
			break;
		}
	}
}
