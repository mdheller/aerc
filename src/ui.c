#define _POSIX_C_SOURCE 201112LL

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <termbox.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <errno.h>

#include "util/time.h"
#include "util/stringop.h"
#include "util/list.h"
#include "handlers.h"
#include "subprocess.h"
#include "commands.h"
#include "config.h"
#include "colors.h"
#include "state.h"
#include "render.h"
#include "util/list.h"
#include "util/stringop.h"
#include "subprocess.h"
#include "log.h"
#include "ui.h"

int frame = 0;

struct loading_indicator {
	int x, y;
};

list_t *loading_indicators = NULL;

void init_ui() {
	tb_init();
	tb_select_input_mode(TB_INPUT_ESC | TB_INPUT_MOUSE);
	tb_select_output_mode(TB_OUTPUT_256);
	state->command.cmd_history = create_list();
}

void teardown_ui() {
	tb_shutdown();
}

int tb_printf(int x, int y, struct tb_cell *basis, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	char *buf = malloc(len + 1);
	va_start(args, fmt);
	vsnprintf(buf, len + 1, fmt, args);
	va_end(args);

	int l = 0;
	int _x = x, _y = y;
	char *b = buf;
	while (b < buf + len + 1 && *b) {
		b += tb_utf8_char_to_unicode(&basis->ch, b);
		++l;
		switch (basis->ch) {
		case '\n':
			_x = x;
			_y++;
			break;
		case '\r':
			_x = x;
			break;
		default:
			tb_put_cell(_x, _y, basis);
			_x++;
			break;
		}
	}

	free(buf);
	return l;
}

void request_rerender(enum render_panels panel) {
	state->rerender |= panel;
}

void reset_fetches() {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	while (account->ui.fetch_requests->length) {
		struct message_range *range = account->ui.fetch_requests->items[0];
		free(range);
		list_del(account->ui.fetch_requests, 0);
	}
}

void request_fetch(struct aerc_message *message) {
	if (message->fetching || message->fetched) {
		return;
	}
	worker_log(L_DEBUG, "Requested fetch of %d", message->index);
	message->fetching = true;
	struct account_state *account =
		state->accounts->items[state->selected_account];
	bool merged = false;
	for (size_t i = 0; i < account->ui.fetch_requests->length; ++i) {
		struct message_range *range = account->ui.fetch_requests->items[i];
		if (range->min <= message->index && range->max >= message->index) {
			return;
		}
		if (range->min - 1 == message->index) {
			range->min--;
			merged = true;
			break;
		}
		if (range->max + 1 == message->index) {
			range->max++;
			merged = true;
			break;
		}
	}
	if (!merged) {
		struct message_range *range = malloc(sizeof(struct message_range));
		range->min = message->index;
		range->max = message->index;
		list_add(account->ui.fetch_requests, range);
	}
}

void fetch_pending() {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	while (account->ui.fetch_requests->length) {
		struct message_range *range = account->ui.fetch_requests->items[0];
		range->max++;
		range->min++;
		worker_log(L_DEBUG, "Fetching message range %d - %d", range->min, range->max);
		worker_post_action(account->worker.pipe, WORKER_FETCH_MESSAGES,
				NULL, range, NULL, NULL);
		list_del(account->ui.fetch_requests, 0);
	}
}

static void rerender_account_tabs() {
	struct geometry geo = {
		.x = 0,
		.y = 0,
		.width = tb_width(),
		.height = 1
	};
	state->panels.account_tabs = geo;
	render_account_bar(geo);
}

static void rerender_sidebar() {
	struct geometry geo = {
		.x = 0,
		.y = 1,
		.width = config->ui.sidebar_width,
		.height = tb_height() - 1
	};
	state->panels.sidebar = geo;
	render_sidebar(geo);
}

static void rerender_message_list() {
	struct geometry geo = {
		.x = config->ui.sidebar_width,
		.y = state->panels.tabs_rendered,
		.width = tb_width() - config->ui.sidebar_width,
		.height = tb_height() - 1 - state->panels.tabs_rendered
	};
	state->panels.message_list = geo;
	render_items(geo);
}

static void rerender_status_bar() {
	struct geometry geo = {
		.x = config->ui.sidebar_width,
		.y = tb_height() - 1,
		.width = tb_width() - config->ui.sidebar_width,
		.height = 1
	};
	state->panels.status_bar = geo;
	render_status(geo);
}

void message_view_geometry(struct geometry *geo) {
	geo->x = config->ui.sidebar_width;
	geo->y = state->panels.tabs_rendered;
	geo->width = tb_width() - config->ui.sidebar_width;
	geo->height = tb_height() - 1 - state->panels.tabs_rendered;
}

static void rerender_message_view() {
	struct geometry geo;
	message_view_geometry(&geo);
	state->panels.message_view = geo;
	render_message_view(geo);
}

void rerender() {
	free_flat_list(loading_indicators);
	loading_indicators = create_list();

	int height = tb_height();
	struct geometry client = {
		.x = 0,
		.y = 0,
		.width = tb_width(),
		.height = height
	};
	state->panels.client = client;

	struct account_state *account =
		state->accounts->items[state->selected_account];

	if(state->rerender & PANEL_ALL) {
		tb_clear();
	}

	if (state->rerender & (PANEL_ACCOUNT_TABS | PANEL_ALL)) {
		rerender_account_tabs();
	}

	if (state->rerender & (PANEL_SIDEBAR | PANEL_ALL)) {
		rerender_sidebar();
	}

	if (state->rerender & (PANEL_MESSAGE_VIEW | PANEL_MESSAGE_LIST | PANEL_ALL)
			&& account->viewer.term) {
		rerender_message_view();
	} else {
		reset_fetches();
		if (state->rerender & (PANEL_MESSAGE_LIST | PANEL_ALL)) {
			rerender_message_list();
		}
		fetch_pending();
	}

	if (state->rerender & (PANEL_STATUS_BAR | PANEL_ALL)) {
		rerender_status_bar();
	}

	if (state->command.text) {
		tb_set_cursor(config->ui.sidebar_width + strlen(state->command.text) + 1, height - 1);
	} else {
		if (account->viewer.term) {
			unsigned int flags = tsm_screen_get_flags(account->viewer.term->pty->screen);
			if ((flags & TSM_SCREEN_HIDE_CURSOR)) {
				tb_set_cursor(TB_HIDE_CURSOR, TB_HIDE_CURSOR);
			} else {
				tb_set_cursor(
					state->panels.message_view.x +
						tsm_screen_get_cursor_x(account->viewer.term->pty->screen),
					state->panels.message_view.y +
						tsm_screen_get_cursor_y(account->viewer.term->pty->screen));
			}
		} else {
			// Fix issues with physical terminals
			tb_set_cursor(client.width - 1, client.height - 1);
			tb_present();
			// End fix
			tb_set_cursor(TB_HIDE_CURSOR, TB_HIDE_CURSOR);
		}
	}
	tb_present();
	state->rerender = PANEL_NONE;
}

void rerender_item(size_t index) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	struct aerc_mailbox *mailbox = get_aerc_mailbox(account, account->selected);
	if (!mailbox || index >= mailbox->messages->length) {
		return;
	}
	int folder_width = config->ui.sidebar_width;
	struct geometry geo = {
		.width = tb_width(),
		.height = tb_height(),
		.x = folder_width,
		.y = state->panels.message_list.y +
			mailbox->messages->length - account->ui.list_offset - (index + 1)
	};
	worker_log(L_DEBUG, "Rerendering item %zd at %d", index, geo.y);
	struct aerc_message *message = mailbox->messages->items[index];
	if (!message) {
		return;
	}
	size_t selected = mailbox->messages->length - account->ui.selected_message - 1;
	for (size_t i = 0; i < loading_indicators->length; ++i) {
		struct loading_indicator *indic = loading_indicators->items[i];
		if (indic->x == geo.x && indic->y == geo.y) {
			list_del(loading_indicators, i);
			break;
		}
	}
	geo.width -= folder_width;
	geo.height -= 2;
	render_item(geo, message, selected == index);
	tb_present();
}

static void render_loading(struct geometry geo) {
	struct tb_cell cell;
	if (config->ui.loading_frames->length == 0) {
		return;
	}
	get_color("loading-indicator", &cell);
	int f = frame / 8 % config->ui.loading_frames->length;
	tb_printf(geo.x, geo.y, &cell, "%s   ",
			(const char *)config->ui.loading_frames->items[f]);
}

void add_loading(struct geometry geo) {
	struct loading_indicator *indic = calloc(1, sizeof(struct loading_indicator));
	indic->x = geo.x;
	indic->y = geo.y;
	list_add(loading_indicators, indic);
	render_loading(geo);
}

static void abort_command() {
	free(state->command.text);
	state->command.text = NULL;
	request_rerender(PANEL_STATUS_BAR);
}

static void command_input(uint16_t ch) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	size_t size = tb_utf8_char_length(ch);
	size_t len = strlen(state->command.text);
	if (state->command.length < len + size + 1) {
		state->command.text = realloc(state->command.text,
				state->command.length + 1024);
		state->command.length += 1024;
	}
	memcpy(state->command.text + len, &ch, size);
	state->command.text[len + size] = '\0';
	if (account->viewer.term && strcmp(state->command.text, ":") == 0) {
		// Pass through to term
		abort_command();
		struct tb_event fake_event = {
			.type = TB_EVENT_KEY,
			.ch = ':'
		};
		subprocess_pty_key(account->viewer.term, &fake_event);
	}
	request_rerender(PANEL_STATUS_BAR);
}

static void command_backspace() {
	int len = strlen(state->command.text);
	if (len == 0) {
		return;
	}
	state->command.text[len - 1] = '\0';
	request_rerender(PANEL_STATUS_BAR);
}

static void command_delete_word() {
	int len = strlen(state->command.text);
	if (len == 0) {
		return;
	}
	char *cmd = state->command.text + len - 1;
	if (isspace(*cmd)) --cmd;
	while (cmd != state->command.text && !isspace(*cmd)) --cmd;
	if (cmd != state->command.text) ++cmd;
	*cmd = '\0';
	request_rerender(PANEL_STATUS_BAR);
}


static struct tb_event *parse_input_command(const char **input) {
	if (!**input) {
		return NULL;
	}

	if (**input == '<') {
		const char *term = strchr(*input, '>');
		if (term) {
			char *buf = strdup(*input + 1);
			*strchr(buf, '>') = 0;

			struct tb_event *e = bind_translate_key_name(buf);
			free(buf);
			if (e) {
				*input += 1 + term - *input;
				return e;
			}
		}
	}

	struct tb_event *e = calloc(1, sizeof(struct tb_event));
	e->type = TB_EVENT_KEY;
	e->ch = **input;
	++*input;
	return e;
}

static void simulate_input(aqueue_t *event_queue, const char *input) {
	struct tb_event *new_event = NULL;
	bool in_string = false;
	bool in_char = false;
	while (*input) {
		if (*input == '"' && !in_char) {
			in_string = !in_string;
		} else if (*input == '\'' && !in_string) {
			in_char = !in_char;
		} else if (*input == '\0' || (!in_string && !in_char)) {
			new_event = parse_input_command(&input);
			if (!new_event) {
				break;
			}
			aqueue_enqueue(event_queue, new_event);
			continue;
		}
		struct tb_event *e = calloc(1, sizeof(struct tb_event));
		e->type = TB_EVENT_KEY;
		e->ch = *input;
		aqueue_enqueue(event_queue, e);
		++input;
	}
}

static void pass_event_to_command(struct tb_event *event, aqueue_t *event_queue) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	const char* command = bind_handle_key_event(
		account->viewer.term ? state->mbinds : state->lbinds, event);
	if (command) {
		simulate_input(event_queue, command);
	} else if (account->viewer.term) {
		// TODO: communicate from binding handler that we hit a nonexistent
		// binding and flush all of the keys into the subterm, plus a timeout
		clear_input_buffer(state->mbinds);
		if (event->type == TB_EVENT_KEY) {
			// TODO: pass through mouse events?
			subprocess_pty_key(account->viewer.term, event);
		}
	}
}

static void confirm_accept(struct tb_event *event, aqueue_t *event_queue) {
	simulate_input(event_queue, state->confirm.command);
	state->confirm.prompt = NULL;
	state->confirm.command = NULL;
	request_rerender(PANEL_STATUS_BAR);
}

static void confirm_reject() {
	state->confirm.prompt = NULL;
	state->confirm.command = NULL;
	request_rerender(PANEL_STATUS_BAR);
}

static void process_event(struct tb_event* event, aqueue_t *event_queue) {
	static size_t cmd_index = 0;
	switch (event->type) {
	case TB_EVENT_RESIZE:
		request_rerender(PANEL_ALL);
		break;
	case TB_EVENT_KEY:
		if (state->confirm.prompt != NULL) {
			switch (event->key) {
			case TB_KEY_ESC:
			case TB_KEY_CTRL_C:
				confirm_reject();
				break;
			case TB_KEY_ENTER:
				confirm_accept(event, event_queue);
				break;
			default:
				if (event->ch == 'y' || event->ch == 'Y') {
					confirm_accept(event, event_queue);
				}
				if (event->ch == 'n' || event->ch == 'N') {
					confirm_reject();
				}
			}
		}
		else if (state->command.text) {
			switch (event->key) {
			case TB_KEY_ESC:
			case TB_KEY_CTRL_C:
				abort_command();
				break;
			case TB_KEY_BACKSPACE:
			case TB_KEY_BACKSPACE2:
				command_backspace();
				break;
			case TB_KEY_CTRL_W:
				command_delete_word();
				break;
			case TB_KEY_SPACE:
				command_input(' ');
				break;
			case TB_KEY_TAB:
				command_input('\t');
				break;
			case TB_KEY_ARROW_UP:
				free(state->command.text);
				if (state->command.cmd_history->length > cmd_index) {
					state->command.text = strdup(state->command.cmd_history->items[state->command.cmd_history->length - 1 - cmd_index++]);
				} else {
					state->command.text = strdup("");
				}
				break;
			case TB_KEY_ARROW_DOWN:
				free(state->command.text);
				if (cmd_index > 0) {
					state->command.text = strdup(state->command.cmd_history->items[--cmd_index]);
				} else {
					state->command.text = strdup("");
				}
				break;
			case TB_KEY_ENTER:
				cmd_index = 0;
				list_add(state->command.cmd_history, strdup(state->command.text));
				handle_command(state->command.text);
				abort_command();
				break;
			default:
				if (event->ch && !event->mod) {
					command_input(event->ch);
				}
				break;
			}
			request_rerender(PANEL_STATUS_BAR);
		} else {
			if (event->ch == ':' && !event->mod) {
				state->command.text = malloc(1024);
				state->command.text[0] = '\0';
				state->command.length = 1024;
				state->command.index = 0;
				state->command.scroll = 0;
				request_rerender(PANEL_STATUS_BAR);
			} else {
				pass_event_to_command(event, event_queue);
			}
		}
		break;
	case TB_EVENT_MOUSE:
		if (event->key == TB_KEY_MOUSE_WHEEL_UP
				|| event->key == TB_KEY_MOUSE_WHEEL_DOWN) {
			pass_event_to_command(event, event_queue);
		}
		// TODO: Clickables
		break;
	}
}

bool ui_tick() {
	struct geometry geo;
	if (loading_indicators->length > 1) {
		frame++;
		for (size_t i = 0; i < loading_indicators->length; ++i) {
			struct loading_indicator *indic = loading_indicators->items[i];
			geo.x = indic->x;
			geo.y = indic->y;
			render_loading(geo);
		}
		tb_present();
	}

	aqueue_t *events = aqueue_new();

	while (1) {
		struct tb_event *event = malloc(sizeof(struct tb_event));
		// Fetch an event and enqueue it if we can
		if (tb_peek_event(event, 0) < 1 || !aqueue_enqueue(events, event)) {
			free(event);
			break;
		}
	}

	struct tb_event *event;
	while (aqueue_dequeue(events, (void**)&event)) {
		process_event(event, events);
		free(event);
		if (state->exit) {
			break;
		}
	}

	// If there's events in the queue still, it's because we're exiting, and we
	// need to clean up.
	while (aqueue_dequeue(events, (void**)&event)) {
		free(event);
	}
	aqueue_free(events);

	struct account_state *account =
		state->accounts->items[state->selected_account];

	if (account->viewer.term) {
		if (subprocess_update(account->viewer.term)) {
			subprocess_free(account->viewer.term);
			account->viewer.msg = NULL;
			account->viewer.term = NULL;
			request_rerender(PANEL_MESSAGE_LIST);
		}
	}

	if (account->viewer.processes) {
		for (size_t i = 0; i < account->viewer.processes->length; ++i) {
			struct subprocess *subp = account->viewer.processes->items[i];
			if (subprocess_update(subp)) {
				subprocess_free(subp);
				list_del(account->viewer.processes, i);
				--i;
				worker_log(L_DEBUG, "Child exited");
			}
		}
	}

	if (state->rerender != PANEL_NONE) {
		rerender();
	}

	return !state->exit;
}

void scroll_selected_into_view() {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	int relative = account->ui.selected_message - account->ui.list_offset;
	int height = state->panels.message_list.height - 1;
	if (relative >= height) {
		account->ui.list_offset += relative - height;
		request_rerender(PANEL_MESSAGE_LIST);
	} else if (relative < 0) {
		account->ui.list_offset += relative;
		request_rerender(PANEL_MESSAGE_LIST);
	}
}
