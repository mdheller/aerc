#ifndef _HANDLERS_H
#define _HANDLERS_H

#include "state.h"
#include "worker.h"

void worker_connected(struct worker_pipe *pipe, void *data,
		struct worker_message *result);
void worker_select_complete(struct worker_pipe *pipe, void *data,
		struct worker_message *result);

void handle_worker_connect_cert_check(struct account_state *account,
		struct worker_message *message);
void handle_worker_mailbox_updated(struct account_state *account,
		struct worker_message *message);
void handle_worker_message_updated(struct account_state *account,
		struct worker_message *message);
void handle_worker_message_deleted(struct account_state *account,
		struct worker_message *message);
void handle_worker_mailbox_deleted(struct account_state *account,
		struct worker_message *message);

void load_message_viewer(struct account_state *account);

#endif
