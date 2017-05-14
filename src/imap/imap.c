/*
 * imap/imap.c - responsible for opening and maintaining the IMAP socket
 */
#define _POSIX_C_SOURCE 201112LL

#include <assert.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "absocket.h"
#include "imap/imap.h"
#include "internal/imap.h"
#include "log.h"
#include "urlparse.h"
#include "util/hashtable.h"
#include "util/list.h"
#include "util/time.h"
#include "util/stringop.h"

#define BUFFER_SIZE 1024

bool inited = false;
hashtable_t *internal_handlers = NULL;

FILE *raw;

typedef void (*imap_handler_t)(struct imap_connection *imap,
	const char *token, const char *cmd, imap_arg_t *args);

struct imap_pending_callback *make_callback(imap_callback_t callback, void *data) {
	// This just holds the user reference along with the callback pointer
	struct imap_pending_callback *cb = malloc(sizeof(struct imap_pending_callback));
	cb->callback = callback;
	cb->data = data;
	return cb;
}

int handle_line(struct imap_connection *imap, imap_arg_t *arg) {
	assert(arg && arg->next); // We expect at least a tag and command
	/*
	 * IMAP commands are formatted like this:
	 *
	 * [tag] [command] [...]
	 *
	 * The tag identifies the particular event this command refers to, or '*'.
	 * It's set to whatever tag we passed in when we asked the server to do a
	 * thing, but can often be * to provide updates to aerc's internal state, or
	 * meta responses. Our hashtable is keyed on the command string.
	 */
	if (arg->next && arg->next->type == IMAP_NUMBER) {
		/*
		 * Some commands (namely EXISTS and RECENT) are formatted like so:
		 *
		 * [tag] [arg] [command] [...]
		 *
		 * Which is fucking stupid. But we handle that here by checking if the
		 * command name is a number and then rearranging it.
		 */
		imap_arg_t *num = arg->next;
		imap_arg_t *cmd = num->next;
		imap_arg_t *rest = cmd->next;
		cmd->next = num;
		num->next = rest;
		arg->next = cmd;
	}
	assert(arg->type == IMAP_ATOM);
	assert(arg->next->type == IMAP_ATOM);
	imap_handler_t handler = hashtable_get(internal_handlers, arg->next->str);
	if (handler) {
		handler(imap, arg->str, arg->next->str, arg->next->next);
	} else {
		worker_log(L_DEBUG, "Recieved unknown IMAP command: %s", arg->next->str);
	}
	return 0;
}

void imap_send(struct imap_connection *imap, imap_callback_t callback,
		void *data, const char *fmt, ...) {
	if (imap->mode == RECV_IDLE) {
		worker_log(L_DEBUG, "Leaving IDLE");
		imap->mode = RECV_LINE;
		char *done = "DONE\r\n";
		ab_send(imap->socket, done, strlen(done));
		if (raw) {
			fwrite(done, 1, strlen(done), raw);
			fflush(raw);
		}
	}

	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	char *buf = malloc(len + 1);
	va_start(args, fmt);
	vsnprintf(buf, len + 1, fmt, args);
	va_end(args);

	len = snprintf(NULL, 0, "a%04d", imap->next_tag);
	char *tag = malloc(len + 1);
	snprintf(tag, len + 1, "a%04d", imap->next_tag++);

	len = snprintf(NULL, 0, "%s %s\r\n", tag, buf);
	char *cmd = malloc(len + 1);
	snprintf(cmd, len + 1, "%s %s\r\n", tag, buf);

	ab_send(imap->socket, cmd, len);
	if (raw) {
		fwrite(cmd, 1, len, raw);
		fflush(raw);
	}
	hashtable_set(imap->pending, tag, make_callback(callback, data));

	if (strncmp("LOGIN ", buf, 6) == 0) {
		worker_log(L_DEBUG, "-> %s LOGIN *****", tag);
		memset(buf, 0, strlen(buf));
		memset(cmd, 0, strlen(cmd));
	} else if (strncmp("AUTHENTICATE ", buf, 13) == 0) {
		worker_log(L_DEBUG, "-> %s AUTHENTICATE *****", tag);
		memset(buf, 0, strlen(buf));
		memset(cmd, 0, strlen(cmd));
		worker_log(L_DEBUG, "Note: core dumps do not include your password past this point");
	} else {
		worker_log(L_DEBUG, "-> %s %s", tag, buf);
	}

	free(cmd);
	free(buf);
	free(tag);
}

int imap_receive(struct imap_connection *imap) {
	poll(imap->poll, 1, 0);
	if (imap->poll[0].revents & POLLIN) {
		get_nanoseconds(&imap->last_network);
		if (imap->mode == RECV_WAIT) {
			/* The mode may be RECV_WAIT if we are waiting on the user to verify
			 * the SSL certificate, for example. */
		} else {
			ssize_t amt = ab_recv(imap->socket, imap->line + imap->line_index,
					imap->line_size - imap->line_index);
			imap->line_index += amt;
			if (imap->line_index == imap->line_size) {
				imap->line = realloc(imap->line,
						imap->line_size + BUFFER_SIZE + 1);
				memset(imap->line + imap->line_index + 1, 0,
						(imap->line_size - imap->line_index + 1));
				imap->line_size = imap->line_size + BUFFER_SIZE;
			}
			int remaining = 0;
			while (!remaining) {
				imap_arg_t *arg = calloc(1, sizeof(imap_arg_t));
				int len = imap_parse_args(imap->line, arg, &remaining);
				if (remaining == 0) { // Parsed a complete command
					char c = imap->line[len];
					imap->line[len] = '\0';
					worker_log(L_DEBUG, "Handling %s", imap->line);
					if (raw) {
						fwrite(imap->line, 1, len, raw);
						fflush(raw);
					}
					imap->line[len] = c;

					handle_line(imap, arg);
				}
				imap_arg_free(arg);
				if (len > 0 && remaining == 0) {
					memmove(imap->line, imap->line + len, imap->line_size - len);
					imap->line_index -= len;
					memset(imap->line + imap->line_index, 0, imap->line_size - imap->line_index);
				}
			}
			return amt;
		}
	} else {
		// Nothing being received, we wait 3 seconds and then start IDLE
		struct timespec ts;
		get_nanoseconds(&ts);
		if (imap->logged_in && imap->cap->idle && imap->mode != RECV_IDLE) {
			if (ts.tv_sec - imap->last_network.tv_sec > 3) {
				worker_log(L_DEBUG, "Entering IDLE mode");
				imap_send(imap, NULL, NULL, "IDLE");
				imap->mode = RECV_IDLE;
				get_nanoseconds(&imap->idle_start);
			}
		}
		if (imap->mode == RECV_IDLE) {
			if (ts.tv_sec - imap->idle_start.tv_sec > 20 * 60) {
				// TODO: Customize the idle refresh timeout
				worker_log(L_DEBUG, "Refreshing IDLE mode");
				imap_send(imap, NULL, NULL, "IDLE");
				imap->mode = RECV_IDLE;
				get_nanoseconds(&imap->idle_start);
			}
		}
	}
	return 0;
}

void handle_noop(struct imap_connection *imap, const char *token,
		const char *cmd, imap_arg_t *args) {
	// This space intentionally left blank
}

void imap_init(struct imap_connection *imap) {
	imap->mode = RECV_WAIT;
	imap->line = calloc(1, BUFFER_SIZE + 1);
	imap->line_index = 0;
	imap->line_size = BUFFER_SIZE;
	imap->next_tag = 1;
	imap->pending = create_hashtable(128, hash_string);
	imap->mailboxes = create_list();
	imap->select_queue = create_list();
	if (internal_handlers == NULL) {
		internal_handlers = create_hashtable(128, hash_string);
		hashtable_set(internal_handlers, "OK", handle_imap_status);
		hashtable_set(internal_handlers, "NO", handle_imap_status);
		hashtable_set(internal_handlers, "BAD", handle_imap_status);
		hashtable_set(internal_handlers, "PREAUTH", handle_imap_status);
		hashtable_set(internal_handlers, "BYE", handle_imap_status);
		hashtable_set(internal_handlers, "CAPABILITY", handle_imap_capability);
		hashtable_set(internal_handlers, "LIST", handle_imap_list);
		hashtable_set(internal_handlers, "FLAGS", handle_imap_flags);
		hashtable_set(internal_handlers, "PERMANENTFLAGS", handle_imap_flags);
		hashtable_set(internal_handlers, "EXISTS", handle_imap_existsunseenrecent);
		hashtable_set(internal_handlers, "UNSEEN", handle_imap_existsunseenrecent);
		hashtable_set(internal_handlers, "RECENT", handle_imap_existsunseenrecent);
		hashtable_set(internal_handlers, "UIDNEXT", handle_imap_uidnext);
		hashtable_set(internal_handlers, "READ-WRITE", handle_imap_readwrite);
		hashtable_set(internal_handlers, "UIDVALIDITY", handle_noop);
		hashtable_set(internal_handlers, "HIGHESTMODSET", handle_noop); // RFC 4551
		hashtable_set(internal_handlers, "FETCH", handle_imap_fetch);
		hashtable_set(internal_handlers, "EXPUNGE", handle_imap_expunge);
	}
}

void imap_close(struct imap_connection *imap) {
	absocket_free(imap->socket);
	free(imap->line);
	free(imap);
}

bool imap_connect(struct imap_connection *imap, const struct uri *uri,
		bool use_ssl, imap_callback_t callback, void *data) {
	raw = fopen("raw.log", "w"); // temp, todo figure out a permenant solution
	imap_init(imap);
	imap->socket = absocket_new(uri, use_ssl);
	if (!imap->socket) {
		return false;
	}
	imap->poll[0].fd = imap->socket->basefd;
	imap->poll[0].events = POLLIN;
	hashtable_set(imap->pending, "*", make_callback(callback, data));
	return true;
}
