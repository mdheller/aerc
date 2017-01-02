#ifndef _RENDER_H
#define _RENDER_H

#include "state.h"

enum render_panels {
	PANEL_NONE         = 0x0,
	PANEL_ACCOUNT_TABS = 0x1,
	PANEL_SIDEBAR      = 0x2,
	PANEL_MESSAGE_LIST = 0x4,
	PANEL_MESSAGE_VIEW = 0x8,
	PANEL_STATUS_BAR   = 0x10,
	PANEL_ALL          = 0x80
};

void render_account_bar(struct geometry geo);
void render_sidebar(struct geometry geo);
void render_status(struct geometry geo);
void render_items(struct geometry geo);
void render_item(struct geometry geo, struct aerc_message *message, bool selected);
void render_message_view(struct geometry geo);

#endif
