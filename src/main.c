/*
 * main.c - entry point and temporary test code
 */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "absocket.h"
#include "bind.h"
#include "colors.h"
#include "config.h"
#include "handlers.h"
#include "worker.h"
#include "imap/worker.h"
#include "log.h"
#include "render.h"
#include "state.h"
#include "ui.h"
#include "util/list.h"
#include "util/stringop.h"

struct aerc_state *state;

struct message_handler {
	enum worker_message_type action;
	void (*handler)(struct account_state *account, struct worker_message *message);
};

struct response_handler {
	enum worker_message_type action;
	worker_callback_t handler;
};

struct message_handler message_handlers[] = {
#ifdef USE_OPENSSL
	{ WORKER_CONNECT_CERT_CHECK, handle_worker_connect_cert_check },
#endif
	{ WORKER_MAILBOX_UPDATED, handle_worker_mailbox_updated },
	{ WORKER_MAILBOX_DELETED, handle_worker_mailbox_deleted },
	{ WORKER_MESSAGE_UPDATED, handle_worker_message_updated },
	{ WORKER_MESSAGE_DELETED, handle_worker_message_deleted },
};

struct response_handler response_handlers[] = {
	{ WORKER_SELECT_MAILBOX, worker_select_complete },
};

void handle_worker_message(struct account_state *account, struct worker_message *msg) {
	for (size_t i = 0;
			i < sizeof(message_handlers) / sizeof(message_handlers[0]); ++i) {
		struct message_handler handler = message_handlers[i];
		if (handler.action == msg->type) {
			handler.handler(account, msg);
			break;
		}
	}
	if (!msg->in_response_to) {
		return;
	}
	for (size_t i = 0;
			i < sizeof(response_handlers) / sizeof(response_handlers[0]); ++i) {
		struct response_handler handler = response_handlers[i];
		if (handler.action == msg->in_response_to->type) {
			handler.handler(account->worker.pipe, NULL, msg);
			break;
		}
	}
}

static void init_state() {
	state = calloc(1, sizeof(struct aerc_state));
	state->accounts = create_list();
	state->lbinds = malloc(sizeof(struct bind));
	init_bind(state->lbinds);
	state->mbinds = malloc(sizeof(struct bind));
	init_bind(state->mbinds);
	add_default_bindings(state->lbinds, state->mbinds);
}

static void cleanup_state() {
	destroy_bind(state->lbinds);
	destroy_bind(state->mbinds);
	free_flat_list(state->accounts);
	free(state->command.text);
	free(state);
}

int main(int argc, char **argv) {
	init_state();
	 // TODO: Customizable
	if (!isatty(STDERR_FILENO)) {
		init_log(L_DEBUG);
	} else {
		init_log(L_ERROR);
	}
	abs_init();
	colors_init();

	worker_log(L_DEBUG, "Note: core dumps may include your password beyond this point");
	if (!load_main_config(NULL)) {
		worker_log(L_ERROR, "Error loading config");
		return 1;
	}

	if (config->accounts->length == 0) {
		worker_log(L_ERROR, "No accounts configured. What do you expect me to do?");
		return 1;
	}

	init_ui();

	for (size_t i = 0; i < config->accounts->length; ++i) {
		struct account_config *ac = config->accounts->items[i];
		if (!ac->source) {
			worker_log(L_ERROR, "No source configured for account %s", ac->name);
			return 1;
		}
		struct account_state *account = calloc(1, sizeof(struct account_state));
		account->name = strdup(ac->name);
		account->worker.pipe = worker_pipe_new();
		account->worker.pipe->master = account;
		account->ui.fetch_requests = create_list();
		account->config = ac;
		worker_post_action(account->worker.pipe, WORKER_CONFIGURE, NULL,
				ac->extras, NULL, NULL);
		worker_post_action(account->worker.pipe, WORKER_CONNECT, NULL,
				ac->source, worker_connected, NULL);
		// TODO: Detect appropriate worker based on source
		pthread_create(&account->worker.thread, NULL, imap_worker,
				account->worker.pipe);
		list_add(state->accounts, account);
		set_status(account, ACCOUNT_NOT_READY, "Connecting...");
	}

	state->rerender = PANEL_ALL;
	rerender();

	while (1) {
		bool sleep = true;
		struct worker_message *msg;
		for (size_t i = 0; i < state->accounts->length; ++i) {
			struct account_state *account = state->accounts->items[i];
			if (worker_get_message(account->worker.pipe, &msg)) {
				sleep = false;
				handle_worker_message(account, msg);
				worker_message_free(msg);
			}
		}

		if (sleep && !ui_tick()) {
			break;
		}

		if (sleep) {
			struct timespec spec = { 0, .5e+8 };
			nanosleep(&spec, NULL);
		}
	}

	teardown_ui();
	cleanup_state();
	return 0;
}
