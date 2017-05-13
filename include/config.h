#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdbool.h>

#include "util/list.h"

struct account_config_extra {
	char *key, *value;
};

struct account_config {
	char *name;
	char *source;
	list_t *folders;
	list_t *extras;
};

struct mimetype {
	char *type;
	char *subtype;
};

struct mime_handler {
	struct mimetype mime;
	char *command;
};

struct aerc_config {
	struct {
		list_t *loading_frames;
		char *index_format;
		char *timestamp_format;
		char *render_account_tabs;
		list_t *show_headers;
		int sidebar_width;
		int preview_height;
	} ui;
	struct {
		list_t *mime_handlers;
		list_t *alternatives;
		char *pager;
	} viewer;
	list_t *accounts;
};

extern struct aerc_config *config;

int handle_config_option(void *_config, const char *section, const char *key, const char *value);
bool load_main_config(const char *file);
bool load_accounts_config();
void free_config(struct aerc_config *config);

#endif
