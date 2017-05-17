#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>

#include "util/stringop.h"
#include "handlers.h"
#include "commands.h"
#include "subprocess.h"
#include "config.h"
#include "state.h"
#include "log.h"
#include "ui.h"

static void close_message(struct account_state *account) {
	subprocess_free(account->viewer.term);
	account->viewer.term = NULL;
	account->viewer.msg = NULL;
	request_rerender(PANEL_MESSAGE_LIST);
}

static void handle_quit(int argc, char **argv) {
	// TODO: We may occasionally want to confirm the user's choice here
	state->exit = true;
}

static void handle_reload() {
	load_main_config(NULL);
}

static void handle_message_seek(char *cmd, int mul, int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	int amt = 1;
	bool scroll = false;
	if (argc > 0) {
		if (strcmp(argv[0], "--scroll") == 0) {
			scroll = true;
			argv = &argv[1];
			argc--;
		}
		char *end;
		amt = strtol(argv[0], &end, 10);
		if (end == argv[0]) {
			set_status(account, ACCOUNT_ERROR, "Usage: %s [--scroll] [amount|%]", cmd);
			return;
		}
		if (*end) {
			if (end[0] == '%' && !end[1]) {
				amt = state->panels.message_list.height * (amt / 100.0);
			} else {
				set_status(account, ACCOUNT_ERROR, "Usage: %s [--scroll] [amount|%]", cmd);
				return;
			}
		}
	}
	amt *= mul;
	close_message(account);
	struct aerc_mailbox *mbox = get_aerc_mailbox(account, account->selected);
	if (!mbox) {
		return;
	}
	int new = (int)account->ui.selected_message + amt;
	if (new < 0) amt -= new;
	if (new >= (int)mbox->messages->length) amt -= new - mbox->messages->length + 1;
	if (scroll) {
		account->ui.list_offset += amt;
	}
	account->ui.selected_message += amt;
	scroll_selected_into_view();
	request_rerender(PANEL_MESSAGE_LIST);
}

static void handle_next_message(int argc, char **argv) {
	handle_message_seek("next-message", 1, argc, argv);
}

static void handle_previous_message(int argc, char **argv) {
	handle_message_seek("previous-message", -1, argc, argv);
}

static void handle_select_message(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	if (argc != 1) {
		set_status(account, ACCOUNT_ERROR, "Usage: select-message [n]");
		return;
	}
	char *end;
	int requested = strtol(argv[0], &end, 10);
	if (end == argv[0] || *end) {
		set_status(account, ACCOUNT_ERROR, "Usage: select-message [n]");
		return;
	}
	close_message(account);
	struct aerc_mailbox *mbox = get_aerc_mailbox(account, account->selected);
	if (!mbox) {
		return;
	}
	if (requested < 0) {
		requested = mbox->messages->length + requested;
	}
	if (requested > (int)mbox->messages->length) {
		set_status(account, ACCOUNT_ERROR, "Requested message is out of range.");
		return;
	}
	account->ui.selected_message = requested;
	scroll_selected_into_view();
	request_rerender(PANEL_MESSAGE_LIST);
}

static void handle_next_account(int argc, char **argv) {
	state->selected_account++;
	state->selected_account %= state->accounts->length;
	request_rerender(PANEL_ALL);
}

static void handle_previous_account(int argc, char **argv) {
	if (state->selected_account == 0) {
		state->selected_account = state->accounts->length - 1;
	} else {
		state->selected_account--;
	}
	request_rerender(PANEL_ALL);
}

static void handle_next_folder(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	close_message(account);
	int i = -1;
	worker_log(L_DEBUG, "Current: %s", account->selected);
	for (i = 0; i < (int)account->mailboxes->length; ++i) {
		struct aerc_mailbox *mbox = account->mailboxes->items[i];
		if (!mbox) {
			return;
		}
		if (!strcmp(mbox->name, account->selected)) {
			break;
		}
	}
	if (i == -1 || i == (int)account->mailboxes->length) {
		return;
	}
	i++;
	i %= account->mailboxes->length;
	struct aerc_mailbox *next = account->mailboxes->items[i];
	if (account->config->folders) {
		while (list_seq_find(account->config->folders,
					lenient_strcmp, next->name) == -1
				&& strcmp(next->name, account->selected) != 0) {
			i++;
			i %= account->mailboxes->length;
			next = account->mailboxes->items[i];
		}
	}
	worker_post_action(account->worker.pipe, WORKER_SELECT_MAILBOX,
			NULL, strdup(next->name));
	request_rerender(PANEL_SIDEBAR | PANEL_MESSAGE_LIST);
}

static void handle_previous_folder(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	close_message(account);
	int i = -1;
	for (i = 0; i < (int)account->mailboxes->length; ++i) {
		struct aerc_mailbox *mbox = account->mailboxes->items[i];
		if (!mbox) {
			return;
		}
		if (!strcmp(mbox->name, account->selected)) {
			break;
		}
	}
	if (i == -1 || i == (int)account->mailboxes->length) {
		return;
	}
	i--;
	if (i == -1) {
		i = account->mailboxes->length - 1;
	}
	struct aerc_mailbox *next = account->mailboxes->items[i];
	if (account->config->folders) {
		while (list_seq_find(account->config->folders,
					lenient_strcmp, next->name) == -1
				&& strcmp(next->name, account->selected) != 0) {
			i--;
			if (i == -1) {
				i = account->mailboxes->length - 1;
			}
			next = account->mailboxes->items[i];
		}
	}
	worker_post_action(account->worker.pipe, WORKER_SELECT_MAILBOX,
			NULL, strdup(next->name));
	request_rerender(PANEL_SIDEBAR | PANEL_MESSAGE_LIST);
}

static void handle_cd(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	close_message(account);
	char *joined = join_args(argv, argc);
	worker_post_action(account->worker.pipe, WORKER_SELECT_MAILBOX,
			NULL, joined);
	request_rerender(PANEL_SIDEBAR | PANEL_MESSAGE_LIST);
}

static void handle_delete_mailbox(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	char *joined = join_args(argv, argc);
	worker_post_action(account->worker.pipe, WORKER_DELETE_MAILBOX,
			NULL, strdup(joined));
	free(joined);
	request_rerender(PANEL_SIDEBAR | PANEL_MESSAGE_LIST);
}

static void handle_create_mailbox(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	char *joined = join_args(argv, argc);
	worker_post_action(account->worker.pipe, WORKER_CREATE_MAILBOX,
			NULL, strdup(joined));
	free(joined);
	request_rerender(PANEL_SIDEBAR | PANEL_MESSAGE_LIST);
}

static void handle_set(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	if (argc < 2) {
		set_status(account, ACCOUNT_ERROR, "Usage: set [section].[key] [value]");
		return;
	}
	char *seckey = argv[0];
	char *dot = strchr(seckey, '.');
	if (!dot) {
		set_status(account, ACCOUNT_ERROR, "Usage: set [section].[key] [value]");
		return;
	}
	*dot = '\0';
	char *section = seckey;
	char *key = dot + 1;
	char *value = join_args(argv + 1, argc - 1);
	handle_config_option(config, section, key, value);
	request_rerender(PANEL_ALL);
	set_status(account, ACCOUNT_OKAY, "Connected.");
	free(value);
}

static void handle_view_message(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	if (argc != 0) {
		set_status(account, ACCOUNT_ERROR, "Usage: view-message");
		return;
	}
	if (account->viewer.term) {
		set_status(account, ACCOUNT_ERROR, "Terminal in use");
		return;
	}
	struct aerc_mailbox *mbox = get_aerc_mailbox(account, account->selected);
	if (!mbox) {
		set_status(account, ACCOUNT_ERROR, "Failed to read mailbox");
		return;
	}
	account->viewer.msg = mbox->messages->items[
		mbox->messages->length - account->ui.selected_message - 1];
	load_message_viewer(account);
	request_rerender(PANEL_MESSAGE_VIEW);
}

static void handle_term_exec(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	if (argc < 1) {
		set_status(account, ACCOUNT_ERROR, "Usage: term-exec [shell command...]");
		return;
	}
	if (account->viewer.term) {
		set_status(account, ACCOUNT_ERROR, "Terminal in use");
		return;
	}
	char *exec = join_args(argv, argc);
	char *sub_argv[] = { "sh", "-c", exec, NULL };
	account->viewer.term = subprocess_init(sub_argv, true);
	subprocess_start(account->viewer.term);
	request_rerender(PANEL_MESSAGE_VIEW);
}

static void handle_confirm(int argc, char **argv) {
	char *exec = join_args(argv + 1, argc - 1);
	char *arg = strdup(*argv);

	state->confirm.prompt = arg;
	state->confirm.command = exec;
}

static void handle_close_message(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	if (argc != 0) {
		set_status(account, ACCOUNT_ERROR, "Usage: close-message");
		return;
	}
	close_message(account);
}

static void handle_delete_message(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	if (argc > 1) {
		set_status(account, ACCOUNT_ERROR, "Usage: delete-message [n]");
		return;
	}
	size_t requested = account->ui.selected_message;
	if (argc == 1) {
		char *end;
		requested = (size_t)strtol(argv[0], &end, 10);
		if (end == argv[0] || *end) {
			set_status(account, ACCOUNT_ERROR, "Usage: delete-message [n]");
			return;
		}
	}
	close_message(account);
	struct aerc_mailbox *mbox = get_aerc_mailbox(account, account->selected);
	if (!mbox) {
		return;
	}
	if (requested > mbox->messages->length) {
		set_status(account, ACCOUNT_ERROR, "Requested message is out of range.");
		return;
	}
	requested = mbox->messages->length - requested - 1;
	size_t *req = malloc(sizeof(size_t));
	memcpy(req, &requested, sizeof(size_t));
	worker_post_action(account->worker.pipe, WORKER_DELETE_MESSAGE, NULL, req);
	handle_command("next-message");
	request_rerender(PANEL_MESSAGE_LIST);
}

static void handle_copy_message(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	if (argc < 1) {
		set_status(account, ACCOUNT_ERROR, "Usage: copy-message [destination]");
		return;
	}
	size_t requested = account->ui.selected_message;
	struct aerc_mailbox *mbox = get_aerc_mailbox(account, account->selected);
	if (!mbox) {
		return;
	}
	if (requested > mbox->messages->length) {
		set_status(account, ACCOUNT_ERROR, "Requested message is out of range.");
		return;
	}
	requested = mbox->messages->length - requested - 1;
	struct aerc_message_move *req = malloc(sizeof(struct aerc_message_move));
	req->index = requested;
	req->destination = join_args(argv, argc);
	set_status(account, ACCOUNT_OKAY, "Copying message to %s", req->destination);
	worker_post_action(account->worker.pipe, WORKER_COPY_MESSAGE, NULL, req);
	request_rerender(PANEL_MESSAGE_LIST);
}

static void handle_move_message(int argc, char **argv) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	if (argc < 1) {
		set_status(account, ACCOUNT_ERROR, "Usage: copy-message [destination]");
		return;
	}
	size_t requested = account->ui.selected_message;
	struct aerc_mailbox *mbox = get_aerc_mailbox(account, account->selected);
	if (!mbox) {
		return;
	}
	if (requested > mbox->messages->length) {
		set_status(account, ACCOUNT_ERROR, "Requested message is out of range.");
		return;
	}
	requested = mbox->messages->length - requested - 1;
	struct aerc_message_move *req = malloc(sizeof(struct aerc_message_move));
	req->index = requested;
	req->destination = join_args(argv, argc);
	set_status(account, ACCOUNT_OKAY, "Moving message to %s", req->destination);
	worker_post_action(account->worker.pipe, WORKER_MOVE_MESSAGE, NULL, req);
	handle_command("next-message");
	request_rerender(PANEL_MESSAGE_LIST);
}

struct cmd_handler {
	char *command;
	void (*handler)(int argc, char **argv);
};

// Keep alphabetized, please
struct cmd_handler cmd_handlers[] = {
	{ "cd", handle_cd },
	{ "close-message", handle_close_message },
	{ "confirm", handle_confirm },
	{ "copy", handle_copy_message },
	{ "copy-message", handle_copy_message },
	{ "cp", handle_copy_message },
	{ "create-mailbox", handle_create_mailbox },
	{ "delete-mailbox", handle_delete_mailbox },
	{ "delete-message", handle_delete_message },
	{ "exit", handle_quit },
	{ "mkdir", handle_create_mailbox },
	{ "move-message", handle_move_message },
	{ "mv", handle_move_message },
	{ "next-account", handle_next_account },
	{ "next-folder", handle_next_folder },
	{ "next-message", handle_next_message },
	{ "previous-account", handle_previous_account },
	{ "previous-folder", handle_previous_folder },
	{ "previous-message", handle_previous_message },
	{ "q", handle_quit },
	{ "quit", handle_quit },
	{ "reload", handle_reload },
	{ "select-message", handle_select_message },
	{ "set", handle_set },
	{ "term-exec", handle_term_exec },
	{ "view-message", handle_view_message },
};

static int handler_compare(const void *_a, const void *_b) {
	const struct cmd_handler *a = _a;
	const struct cmd_handler *b = _b;
	return strcasecmp(a->command, b->command);
}

static struct cmd_handler *find_handler(char *line) {
	struct cmd_handler d = { .command=line };
	struct cmd_handler *res = NULL;
	worker_log(L_DEBUG, "find_handler(%s)", line);
	res = bsearch(&d, cmd_handlers,
		sizeof(cmd_handlers) / sizeof(struct cmd_handler),
		sizeof(struct cmd_handler), handler_compare);
	return res;
}

void handle_command(const char *_exec) {
	char *exec = strdup(_exec);
	char *head = exec;
	char *cmdlist;
	char *cmd;

	head = exec;
	do {
		// Split command list
		cmdlist = argsep(&head, ";");
		cmdlist += strspn(cmdlist, whitespace);
		do {
			// Split commands
			cmd = argsep(&cmdlist, ",");
			cmd += strspn(cmd, whitespace);
			if (strcmp(cmd, "") == 0) {
				worker_log(L_DEBUG, "Ignoring empty command.");
				continue;
			}
			worker_log(L_DEBUG, "Handling command '%s'", cmd);
			//TODO better handling of argv
			int argc;
			char **argv = split_args(cmd, &argc);
			if (strcmp(argv[0], "exec") != 0) {
				int i;
				for (i = 1; i < argc; ++i) {
					if (*argv[i] == '\"' || *argv[i] == '\'') {
						strip_quotes(argv[i]);
					}
				}
			}
			struct cmd_handler *handler = find_handler(argv[0]);
			if (!handler) {
				worker_log(L_DEBUG, "Unknown command %s", argv[0]);
				free_argv(argc, argv);
				goto cleanup;
			}
			handler->handler(argc-1, argv+1);
			free_argv(argc, argv);
		} while(cmdlist);
	} while(head);
	cleanup:
	free(exec);
}
