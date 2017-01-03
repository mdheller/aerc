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
	list_t *extras;
};

struct aerc_config {
	struct {
		list_t *loading_frames;
		char *index_format;
		char *timestamp_format;
		char *render_account_tabs;
		char *show_headers;
		char *viewer_command;
		int sidebar_width;
		int preview_height;
	} ui;
	list_t *accounts;
};

extern struct aerc_config *config;

int handle_config_option(void *_config, const char *section, const char *key, const char *value);
bool load_main_config(const char *file);
bool load_accounts_config();
void free_config(struct aerc_config *config);

#endif
