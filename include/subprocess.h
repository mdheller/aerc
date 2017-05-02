#ifndef _AERC_SUBPROCESS_H
#define _AERC_SUBPROCESS_H

#include <libtsm.h>
#include <termbox.h>
#include <unistd.h>

struct pty {
	struct tsm_screen *screen;
	struct tsm_vte *vte;
	bool clear;
	tsm_age_t age;
	pid_t pid;
	int fd;
};

struct io_capture {
	size_t len, size, index;
	uint8_t *data;
};

struct subprocess {
	pid_t pid;
	int io_fds[3];
	int pipe[2];
	struct pty *pty;
	struct io_capture *io_stdin;
	struct io_capture *io_stdout;
	struct io_capture *io_stderr;
};

struct subprocess *subprocess_spawn(char **argv, bool pty);
void subprocess_start(struct subprocess *subp);
void subprocess_free(struct subprocess *subp);
void subprocess_pipe(struct subprocess *from, struct subprocess *to);
void subprocess_set_stdin(struct subprocess *subp, uint8_t *data, size_t length);
void subprocess_capture_stdout(struct subprocess *subp);
void subprocess_capture_stderr(struct subprocess *subp);
bool subprocess_update(struct subprocess *subp);

#endif
