#ifndef SUBTERM_H
#define SUBTERM_H

#include <libtsm.h>
#include <termbox.h>
#include <unistd.h>

struct subterm {
	struct tsm_screen *screen;
	struct tsm_vte *vte;
	bool clear;
	tsm_age_t age;
	pid_t pid;
	int fd;
};

struct subterm *initialize_subterm(const char *exe);
void cleanup_subterm(struct subterm **st);
bool subterm_tick(struct subterm *st);
void subterm_resize(struct subterm *st, unsigned short width, unsigned short height);
void subterm_handle_key(struct tsm_vte *vte, struct tb_event *event);

#endif
