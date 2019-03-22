#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termbox.h>
#include <time.h>
#include "colors.h"
#include "config.h"
#include "state.h"
#include "ui.h"
#include "util/unicode.h"
#include "util/list.h"
#include "util/stringop.h"
#include "subprocess.h"
#include "worker.h"
#include "log.h"

static void clear_remaining(struct tb_cell *cell, struct geometry geo) {
	cell->ch = ' ';
	for (int _y = 0; _y < geo.height; ++_y) {
		for (int _x = 0; _x < geo.width; ++_x) {
			tb_put_cell(geo.x + _x, geo.y + _y, cell);
		}
	}
}

void render_account_bar(struct geometry geo) {
	struct tb_cell cell;

	/* Render folder list header */
	get_color("borders", &cell);
	const char *aerc = "aerc"; // 4 chars
	int sides = (config->ui.sidebar_width - 4) / 2;
	for (int _x = 0; _x < sides; ++_x) {
		tb_printf(geo.x++, geo.y, &cell, " ");
	}
	tb_printf(geo.x, geo.y, &cell, aerc); geo.x += 4;
	for (int _x = 0; _x < sides - 1; ++_x) {
		tb_printf(geo.x++, geo.y, &cell, " ");
	}
	tb_printf(geo.x, geo.y, &cell, " "); geo.x += 1;

	bool render_account_tabs;
	if (strcasecmp(config->ui.render_account_tabs, "auto") == 0) {
		render_account_tabs = state->accounts->length > 1;
	} else {
		render_account_tabs = strcasecmp(config->ui.render_account_tabs, "on") == 0;
	}
	state->panels.tabs_rendered = render_account_tabs;
	if (render_account_tabs) {
		/* Render account tabs */
		for (size_t i = 0; i < state->accounts->length; ++i) {
			struct account_state *account = state->accounts->items[i];
			if (i == state->selected_account) {
				get_color("account-selected", &cell);
			} else {
				get_color("account-unselected", &cell);
				if (account->status.status == ACCOUNT_ERROR) {
					get_color("account-error", &cell);
				}
			}
			geo.x += tb_printf(geo.x, 0, &cell, " %s ", account->name);
		}
		get_color("borders", &cell);
		geo.height = 1;
		clear_remaining(&cell, geo);
	}
}

static int compare_mailboxes(const void *_a, const void *_b) {
	const struct aerc_mailbox *a = *(void **)_a;
	const struct aerc_mailbox *b = *(void **)_b;
	return strcmp(a->name, b->name);
}

void render_sidebar(struct geometry geo) {
	struct account_state *account =
		state->accounts->items[state->selected_account];

	struct tb_cell cell;
	get_color("borders", &cell);
	int _x = geo.x, _y = geo.y;
	_x += geo.width - 1;
	for (; _y < geo.height + 1; ++_y) {
		cell.ch = ' ';
		tb_put_cell(_x, _y, &cell);
	}

	_x = geo.x, _y = geo.y;
	if (account->mailboxes) {
		list_qsort(account->mailboxes, compare_mailboxes);
		for (size_t i = 0; geo.y < geo.height && i < account->mailboxes->length; ++i, ++geo.y) {
			struct aerc_mailbox *mailbox = account->mailboxes->items[i];
			if (account->config->folders && strcmp(mailbox->name, account->selected)) {
				if (list_seq_find(account->config->folders,
							lenient_strcmp, mailbox->name) == -1) {
					--geo.y;
					continue;
				}
			}
			if (strcmp(mailbox->name, account->selected) == 0) {
				get_color("folder-selected", &cell);
			} else {
				get_color("folder-unselected", &cell);
			}
			char c = '\0';
			// TODO: decode mailbox names according to spec
			if ((int)utf8_strlen(mailbox->name) > geo.width - 1) {
				mailbox->name[geo.width - 1] = '\0';
			}
			int l = tb_printf(geo.x, geo.y, &cell, "%s", mailbox->name);
			if (c != '\0') {
				mailbox->name[geo.width - 1] = c;
			}
			cell.ch = ' ';
			while (l < geo.width - 1) {
				tb_put_cell(geo.x + l, geo.y, &cell);
				l++;
			}
			if (get_mailbox_flag(mailbox, "\\HasChildren")) {
				cell.ch = '.';
				tb_put_cell(geo.x + geo.width - 2, geo.y, &cell);
				tb_put_cell(geo.x + geo.width - 3, geo.y, &cell);
			}
		}
		geo.x = _x;
	} else {
		add_loading(geo);
		geo.x = _x;
	}
	get_color("folder-unselected", &cell);
	geo.width--;
	clear_remaining(&cell, geo);
}

static void render_command(struct geometry geo) {
	struct tb_cell cell;
	get_color("ex-line", &cell);
	cell.ch = ' ';
	for (int _x = 0; _x < geo.width; ++_x) {
		tb_put_cell(geo.x + _x, geo.y, &cell);
	}
	tb_printf(geo.x, geo.y, &cell, ":%s", state->command.text);
}

static void render_partial_input(struct geometry geo, const char* input) {
	struct tb_cell cell;
	get_color("ex-line", &cell);
	cell.ch = ' ';
	for (int _x = 0; _x < geo.width; ++_x) {
		tb_put_cell(geo.x + _x, geo.y, &cell);
	}
	tb_printf(geo.x, geo.y, &cell, "> %s", input);
}

void render_status(struct geometry geo) {
	if (state->command.text != NULL) {
		render_command(geo);
		return;
	}

	struct account_state *account =
		state->accounts->items[state->selected_account];

	char *input = bind_input_buffer(
		account->viewer.msg ? state->mbinds : state->lbinds);
	if (strlen(input) > 0) {
		render_partial_input(geo, input);
		free(input);
		return;
	}
	free(input);

	if (!account->status.text) return;

	struct tb_cell cell;
	get_color("status-line", &cell);
	cell.ch = ' ';
	if (account->status.status == ACCOUNT_ERROR) {
		get_color("status-line-error", &cell);
	}
	for (int _x = 0; _x < geo.width; ++_x) {
		tb_put_cell(geo.x + _x, geo.y, &cell);
	}
	if (state->confirm.prompt != NULL) {
		tb_printf(geo.x, geo.y, &cell, "%s [y/n]", state->confirm.prompt);
	}
	else if (account->status.status == ACCOUNT_OKAY) {
		tb_printf(geo.x, geo.y, &cell, "%s -- %s",
				account->selected,
				account->status.text);
	} else {
		tb_printf(geo.x, geo.y, &cell, "%s", account->status.text);
	}
}

void render_item(struct geometry geo, struct aerc_message *message, bool selected) {
	if (geo.y > geo.height) {
		return;
	}
	struct tb_cell cell;
	get_color("message-list-unselected", &cell);
	if (!message || !message->fetched) {
		add_loading(geo);
		if (message) {
			request_fetch(message);
		}
	} else {
		bool seen = get_message_flag(message, "\\Seen");
		if (selected) {
			get_color("message-list-selected", &cell);
			if (!seen) {
				get_color("message-list-selected-unread", &cell);
			}
		} else {
			get_color("message-list-unselected", &cell);
			if (!seen) {
				get_color("message-list-unselected-unread", &cell);
			}
		}
		char date[64];
		strftime(date, sizeof(date), config->ui.timestamp_format,
				message->internal_date);
		const char *subject = get_message_header(message, "Subject");
		int l = tb_printf(geo.x, geo.y, &cell, "%s %s", date, subject);
		geo.x += l;
		geo.width -= 1;
		geo.height = 1;
		clear_remaining(&cell, geo);
	}
}

void render_items(struct geometry geo) {
	struct tb_cell cell;
	get_color("message-list-unselected", &cell);
	clear_remaining(&cell, geo);

	struct account_state *account =
		state->accounts->items[state->selected_account];
	struct aerc_mailbox *mailbox = get_aerc_mailbox(account, account->selected);

	if (!mailbox || !mailbox->messages) {
		geo.x += geo.width / 2;
		add_loading(geo);
		return;
	}

	if (account->selected && mailbox->messages->length == 0) {
		geo.x += geo.width / 2 - strlen(config->ui.empty_message) / 2;
		get_color("message-list-empty", &cell);
		tb_printf(geo.x, geo.y, &cell, config->ui.empty_message);
	}

	int limit = geo.height + geo.y;
	int selected = mailbox->messages->length - account->ui.selected_message - 1;
	for (int i = mailbox->messages->length - account->ui.list_offset - 1;
			i >= 0 && geo.y < limit;
			--i, ++geo.y) {
		if ((size_t)i >= mailbox->messages->length) {
			continue;
		}
		struct aerc_message *message = mailbox->messages->items[i];
		const char *subject = get_message_header(message, "Subject");
		worker_log(L_DEBUG, "Rendering message %d of %zd at %d (offs %zd) [%s]",
				i, mailbox->messages->length, geo.y, account->ui.list_offset, subject);
		render_item(geo, message, selected == i);
	}
}

static int tsm_draw_cb(struct tsm_screen *con,uint32_t id, const uint32_t *ch,
	   size_t len, unsigned int width, unsigned int posx, unsigned int posy,
	   const struct tsm_screen_attr *attr, tsm_age_t age, void *data) {
	struct geometry *geo = data;
	// TODO: handle colors
	struct tb_cell cell = {
		.fg = TB_DEFAULT,
		.bg = TB_DEFAULT,
	};
	if (attr->bold) {
		cell.fg |= TB_BOLD;
	}
	if (attr->underline) {
		cell.fg |= TB_UNDERLINE;
	}
	if (attr->inverse) {
		cell.fg |= TB_REVERSE;
	}
	while (len--) {
		cell.ch = *ch;
		tb_put_cell(geo->x + posx++, geo->y + posy++, &cell);
		++ch;
	}
	return 0;
}

void render_message_view(struct geometry geo) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	struct tb_cell cell = {
		.fg = TB_DEFAULT,
		.bg = TB_DEFAULT,
	};
	if (!account->viewer.term || !account->viewer.term->pty) {
		clear_remaining(&cell, geo);
		add_loading(geo);
		return;
	}
	clear_remaining(&cell, geo);
	subprocess_pty_resize(account->viewer.term, geo.width, geo.height);
	account->viewer.term->pty->age = tsm_screen_draw(
			account->viewer.term->pty->screen, tsm_draw_cb, &geo);
}
