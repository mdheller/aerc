#ifndef SUBTERM_H
#define SUBTERM_H

#include <libtsm.h>
#include <termbox.h>

void initialize_subterm(const char *exe);
void cleanup_subterm();
void subterm_tick();
void subterm_resize(unsigned short width, unsigned short height);
void subterm_handle_key(struct tsm_vte *vte, struct tb_event *event);

#endif
