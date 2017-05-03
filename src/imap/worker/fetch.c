/*
 * imap/worker/fetch.c - Handles the WORKER_FETCH_* actions
 */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>

#include "imap/imap.h"
#include "worker.h"

void handle_worker_fetch_messages(struct worker_pipe *pipe,
		struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	struct message_range *range = message->data;

	const char *what = "UID FLAGS INTERNALDATE BODYSTRUCTURE BODY.PEEK["
			"HEADER.FIELDS (DATE FROM SUBJECT TO CC MESSAGE-ID REFERENCES "
			"CONTENT-TYPE IN-REPLY-TO REPLY-TO)]";

	imap_fetch(imap, NULL, NULL, range->min, range->max, what);

	free(range);
}

void handle_worker_fetch_message_part(struct worker_pipe *pipe,
		struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	struct fetch_part_request *request = message->data;
	++request->index; // IMAP is 1 indexed
	++request->part;

	const char *fmt = "BODY[%d]";
	int len = snprintf(NULL, 0, fmt, request->part);
	char *what = malloc(len + 1);
	snprintf(what, len + 1, fmt, request->part);

	imap_fetch(imap, NULL, NULL, request->index, request->index, what);

	free(what);
	free(request);
}
