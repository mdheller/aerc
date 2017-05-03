#define _POSIX_C_SOURCE 200809L
#include <libtsm.h>
#include <termbox.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <wordexp.h>
#include <sys/wait.h>
#include "ui.h"
#include "log.h"
#include "subprocess.h"

struct subprocess *subprocess_init(char **argv, bool pty) {
	struct subprocess *subp = calloc(sizeof(struct subprocess), 1);
	if (!subp) {
		return NULL;
	}
	for (size_t i = 0; i < sizeof(subp->pipes) / sizeof(*subp->pipes); ++i) {
		if (pipe(subp->pipes[i]) < 0) {
			worker_log(L_DEBUG, "Failed to create pipe");
			return NULL;
		}
		if (i >= 1) { // stdout, stderr
			int flags = fcntl(subp->pipes[i][0], F_GETFL, 0);
			fcntl(subp->pipes[i][0], F_SETFL, flags | O_NONBLOCK);
		}
	}
	subp->argv = argv;
	// TODO: pty
	return subp;
}

void subprocess_start(struct subprocess *subp) {
	subp->pid = fork();
	if (subp->pid < 0) {
		worker_log(L_DEBUG, "fork() failed (%d)", errno);
		// TODO: bubble up error
	} else if (!subp->pid) { // child
		dup2(subp->pipes[0][0], STDIN_FILENO);
		dup2(subp->pipes[1][1], STDOUT_FILENO);
		dup2(subp->pipes[2][1], STDERR_FILENO);
		for (size_t i = 0; i < sizeof(subp->pipes) / sizeof(*subp->pipes); ++i) {
			close(subp->pipes[i][0]);
			close(subp->pipes[i][1]);
		}
		execvp(subp->argv[0], subp->argv);
		exit(1);
	} else { // parent
		close(subp->pipes[0][0]);
		for (size_t i = 1; i < sizeof(subp->pipes) / sizeof(*subp->pipes); ++i) {
			close(subp->pipes[i][1]);
		}
		subp->io_fds[0] = subp->pipes[0][1];
		subp->io_fds[1] = subp->pipes[1][0];
		subp->io_fds[2] = subp->pipes[2][0];
	}
}

void subprocess_set_stdin(struct subprocess *subp, uint8_t *data, size_t length) {
	subp->io_stdin = calloc(sizeof(struct io_capture), 1);
	subp->io_stdin->data = data;
	subp->io_stdin->size = subp->io_stdin->len = length;
}

void subprocess_capture_stdout(struct subprocess *subp) {
	subp->io_stdout = calloc(sizeof(struct io_capture), 1);
	subp->io_stdout->size = 256;
	subp->io_stdout->len = 0;
	subp->io_stdout->data = malloc(256);
}

void subprocess_capture_stderr(struct subprocess *subp) {
	subp->io_stderr = calloc(sizeof(struct io_capture), 1);
	subp->io_stderr->size = 256;
	subp->io_stderr->len = 0;
	subp->io_stderr->data = malloc(256);
}

void subprocess_pipe(struct subprocess *from, struct subprocess *to) {
	to->pipes[0][0] = from->pipes[1][0];
}

static int update_io_capture(int fd, struct io_capture *cap) {
	int amt = read(fd, &cap->data[cap->len], cap->size - cap->len);
	if (amt > 0) {
		cap->len += amt;
		if (cap->len >= cap->size) {
			uint8_t *new = realloc(cap->data, cap->size + 1024);
			// TODO: OOM handling
			cap->size = cap->size + 1024;
			cap->data = new;
		}
	}
	return amt;
}

bool subprocess_update(struct subprocess *subp) {
	if (subp->io_fds[0] != -1 && subp->io_stdin && subp->io_stdin->len) {
		size_t amt = subp->io_stdin->len < 1024 ?
			subp->io_stdin->len : 1024;
		int written = write(subp->io_fds[0], subp->io_stdin->data
				+ subp->io_stdin->index, amt);
		if (written > 0) {
			worker_log(L_DEBUG, "Wrote %d of %zd bytes to child %d",
					written, amt, subp->pid);
			subp->io_stdin->len -= written;
			subp->io_stdin->index += written;
			if (subp->io_stdin->len == 0) {
				close(subp->io_fds[0]);
				subp->io_fds[0] = -1;
			}
		} else {
			worker_log(L_DEBUG, "Error %d writing to child %d",
					errno, subp->pid);
			close(subp->io_fds[0]);
			subp->io_fds[0] = -1;
			// TODO: Anything else?
		}
	}
	if (subp->io_fds[1] != -1 && subp->io_stdout) {
		int amt = update_io_capture(subp->io_fds[1], subp->io_stdout);
		if (amt > 0) {
			worker_log(L_DEBUG, "Read %d bytes from child %d", amt, subp->pid);
		} else if (errno != EAGAIN) {
			close(subp->io_fds[1]);
			subp->io_fds[1] = -1;
		}
	}
	if (subp->io_fds[2] != -1 && subp->io_stderr) {
		int amt = update_io_capture(subp->io_fds[2], subp->io_stderr);
		if (amt > 0) {
			worker_log(L_DEBUG, "Read %d bytes from child %d", amt, subp->pid);
		} else if (errno != EAGAIN) {
			close(subp->io_fds[2]);
			subp->io_fds[2] = -1;
		}
	}
	int w;
	waitpid(subp->pid, &w, WNOHANG);
	return WIFEXITED(w);
}

void subprocess_free(struct subprocess *subp) {
	if (!subp) {
		return;
	}
	if (subp->pty) {
		close(subp->pty->fd);
		tsm_vte_unref(subp->pty->vte);
		tsm_screen_unref(subp->pty->screen);
		free(subp->pty);
		request_rerender(PANEL_ALL);
	}
	if (subp->io_stdin) {
		free(subp->io_stdin);
	}
	if (subp->io_stdout) {
		free(subp->io_stdout->data);
		free(subp->io_stdout);
	}
	if (subp->io_stderr) {
		free(subp->io_stderr->data);
		free(subp->io_stderr);
	}
	for (size_t i = 0; i < sizeof(subp->io_fds) / sizeof(*subp->io_fds); ++i) {
		if (subp->io_fds[i] != -1) {
			close(subp->io_fds[i]);
		}
	}
	free(subp);
}
