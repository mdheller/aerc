#ifndef _WORKER_H
#define _WORKER_H
#include <stdbool.h>
#include <stdint.h>
#ifdef USE_OPENSSL
#include <openssl/ossl_typ.h>
#endif
#include "util/aqueue.h"
#include "util/list.h"
#include "util/hashtable.h"

/* worker.h
 *
 * Defines an abstract interface to an asynchronous mail worker.
 *
 * Messages are passed through an atomic queue with actions and messages.
 * Whenever passing extra data with a message, ownership of that data is
 * transfered to the recipient.
 */

enum worker_message_type {
	/* A typical transaction goes like this:
	 * main -> worker: WORKER_*
	 * main <- worker: WORKER_ACK | WORKER_UNSUPPORTED
	 * [worker does task]
	 * main <- worker: WORKER_OKAY | WORKER_ERROR
	 * [main thread runs callbacks]
	 */
	WORKER_ACK,
	WORKER_OKAY,
	WORKER_ERROR,
	WORKER_SHUTDOWN,
	WORKER_UNSUPPORTED,
	WORKER_CONFIGURE,
	/* Connection */
	WORKER_CONNECT,
#ifdef USE_OPENSSL
	WORKER_CONNECT_CERT_CHECK,
	WORKER_CONNECT_CERT_OKAY,
#endif
	/* Listing */
	WORKER_LIST,
	/* Mailboxes */
	WORKER_SELECT_MAILBOX,
	WORKER_DELETE_MAILBOX,
	WORKER_CREATE_MAILBOX,
	WORKER_MAILBOX_DELETED,
	WORKER_MAILBOX_UPDATED,
	/* Messages */
	WORKER_FETCH_MESSAGES,
	WORKER_FETCH_MESSAGE_PART,
	WORKER_MESSAGE_UPDATED,
	WORKER_DELETE_MESSAGE,
	WORKER_MESSAGE_DELETED,
	WORKER_MOVE_MESSAGE,
	WORKER_COPY_MESSAGE,
};

struct worker_pipe {
	/* Master callbacks */
	hashtable_t *callbacks;
	/* Messages from master->worker */
	aqueue_t *actions;
	/* Messages from worker->master */
	aqueue_t *messages;
	/* Arbitrary worker-specific data */
	void *data; // TODO: rename to slave
	/* Arbitrary master-specific data */
	void *master;
};

struct worker_message {
	enum worker_message_type type;
	struct worker_message *in_response_to;
	void *data;
};

typedef void (*worker_callback_t)(struct worker_pipe *pipe, void *data,
		struct worker_message *result);

struct worker_task {
	worker_callback_t callback;
	void *data;
};

struct fetch_part_request {
	int index;
	int part;
};

struct message_range {
	int min, max;
};

struct aerc_message_update {
	char *mailbox;
	struct aerc_message *message;
};

struct aerc_message_delete {
	int index;
};

struct aerc_message_move {
	int index;
	char *destination;
};

struct aerc_message_part {
	char *type;
	char *subtype;
	char *body_id;
	char *body_description;
	char *body_encoding;
	long size;
	uint8_t *content; // Note: do not free this, you don't own it
};

struct aerc_message {
	bool fetching, fetched;
	int index;
	long uid;
	list_t *flags, *headers, *parts;
	struct tm *internal_date;
};

struct aerc_mailbox {
	char *name;
	bool read_write;
	bool selected;
	long exists, recent, unseen;
	list_t *flags;
	list_t *messages;
};

#ifdef USE_OPENSSL
struct cert_check_message {
	X509 *cert;
};
#endif

/* Misc */
struct worker_pipe *worker_pipe_new();
void worker_pipe_free(struct worker_pipe *pipe);
bool worker_get_message(struct worker_pipe *pipe,
		struct worker_message **message);
bool worker_get_action(struct worker_pipe *pipe,
		struct worker_message **message);
void worker_post_message(struct worker_pipe *pipe,
		enum worker_message_type type,
		struct worker_message *in_response_to,
		void *data);
void worker_post_action(struct worker_pipe *pipe,
		enum worker_message_type type,
		struct worker_message *in_response_to,
		void *data, worker_callback_t callback, void *cbdata);
void worker_message_free(struct worker_message *msg);

#endif
