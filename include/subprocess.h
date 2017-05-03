#ifndef _AERC_SUBPROCESS_H
#define _AERC_SUBPROCESS_H

#include <sys/types.h>
#include <libtsm.h>
#include <termbox.h>
#include <unistd.h>

struct pty {
	struct tsm_screen *screen;
	struct tsm_vte *vte;
	tsm_age_t age;
	pid_t pid;
	int fd;
};

struct io_capture {
	size_t len, size, index;
	uint8_t *data;
	struct io_capture *next;
};

struct subprocess {
	pid_t pid;
	int io_fds[3];
	int pipes[3][2];
	char **argv;
	struct pty *pty;
	bool stdin_piped;
	struct io_capture *io_stdin;
	struct io_capture *io_stdout;
	struct io_capture *io_stderr;
};

struct subprocess *subprocess_init(char **argv, bool pty);
void subprocess_start(struct subprocess *subp);
void subprocess_free(struct subprocess *subp);
void subprocess_pipe(struct subprocess *from, struct subprocess *to);
void subprocess_queue_stdin(struct subprocess *subp, uint8_t *data, size_t length);
void subprocess_capture_stdout(struct subprocess *subp);
void subprocess_capture_stderr(struct subprocess *subp);
bool subprocess_update(struct subprocess *subp);
void subprocess_pty_key(struct subprocess *subp, struct tb_event *event);
void subprocess_pty_resize(struct subprocess *subp,
		unsigned short width, unsigned short height);

#endif
