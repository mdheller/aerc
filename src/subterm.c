#include <libtsm.h>
#include <stdarg.h>

#include "render.h"
#include "subterm.h"
#include "state.h"
#include "log.h"
#include "ui.h"

void subterm_log(void *data, const char *file, int line, const char *func,
		const char *subs, unsigned int sev, const char *format, va_list args) {
	worker_vlog(L_DEBUG, format, args);
}

void initialize_subterm() {
	worker_log(L_DEBUG, "Setting up subterm");
	struct account_state *account =
		state->accounts->items[state->selected_account];
	tsm_screen_new(&account->viewer.screen, NULL, NULL);
	tsm_screen_resize(account->viewer.screen, 80, 24);
	tsm_vte_new(&account->viewer.vte,
			account->viewer.screen,
			NULL, NULL,
			NULL, NULL);
	request_rerender(PANEL_MESSAGE_VIEW);
}

void cleanup_subterm() {
	// TODO
}
