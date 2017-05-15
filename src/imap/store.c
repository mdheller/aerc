/*
 * imap/store.c - issues IMAP STORE commands
 */
#define _POSIX_C_SOURCE 200809L

#include "imap/imap.h"
#include "internal/imap.h"

void imap_store(struct imap_connection *imap, imap_callback_t callback,
		void *data, size_t min, size_t max, enum imap_store_mode mode,
		const char *flags) {
	const char *_mode;
	switch (mode) {
		case STORE_FLAGS_APPEND:
			_mode = "+FLAGS";
			break;
		case STORE_FLAGS_REMOVE:
			_mode = "-FLAGS";
			break;
		default:
			_mode = "FLAGS";
			break;
	}

	if (min == max) {
		imap_send(imap, callback, data, "STORE %d %s (%s)",
				min + 1, _mode, flags);
	} else {
		imap_send(imap, callback, data, "FETCH %d:%d %s (%s)",
				min + 1, max + 1, _mode, flags);
	}
}
