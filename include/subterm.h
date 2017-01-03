#ifndef SUBTERM_H
#define SUBTERM_H

void initialize_subterm(const char *exe);
void cleanup_subterm();
void subterm_tick();
void subterm_resize(unsigned short width, unsigned short height);

#endif
