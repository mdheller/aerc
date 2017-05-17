/*
 * imap/copy.c - issues IMAP COPY commands
 */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "imap/imap.h"
#include "internal/imap.h"
#include "log.h"
#include "util/list.h"

void imap_copy(struct imap_connection *imap, imap_callback_t callback,
		void *data, long index, const char *destination) {
	// TODO: Support range
	imap_send(imap, callback, data, "COPY %d \"%s\"", index, destination);
}
