#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "render.h"
#include "subterm.h"
#include "state.h"
#include "log.h"
#include "ui.h"

extern char **environ;

enum shl_pty_msg {
	SHL_PTY_FAILED,
	SHL_PTY_SETUP,
};

static int pty_send(int fd, char d) {
	int r;
	do {
		r = write(fd, &d, 1);
	} while (r < 0 && (errno == EINTR || errno == EAGAIN));
	return (r == 1) ? 0 : -EINVAL;
}

static char pty_recv(int fd) {
	int r;
	char d;
	do {
		r = read(fd, &d, 1);
	} while (r < 0 && (errno == EINTR || errno == EAGAIN));
	return (r <= 0) ? SHL_PTY_FAILED : d;
}

static void initialize_child(int comm[2], int fd) {
	// child
	close(comm[0]);

	sigset_t sigset;
	sigemptyset(&sigset);
	if (sigprocmask(SIG_SETMASK, &sigset, NULL) < 0) {
		close(fd);
		goto fail;
	}
	for (int i = 0; i < SIGUNUSED; ++i) {
		signal(i, SIG_DFL);
	}
	if (grantpt(fd) < 0) {
		close(fd);
		goto fail;
	}
	if (unlockpt(fd) < 0) {
		close(fd);
		goto fail;
	}
	char *slave_name = ptsname(fd);
	if (!slave_name) {
		close(fd);
		goto fail;
	}
	int slave = open(slave_name, O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (slave < 0) {
		close(fd);
		goto fail;
	}
	pid_t pid = setsid();
	if (pid < 0) {
		close(fd);
		goto fail_slave;
	}
	close(fd);
	// TODO: Set these to the correct values instead of resizing later
	struct termios attr;
	struct winsize ws;
	if (tcgetattr(slave, &attr) < 0) {
		goto fail_slave;
	}
	attr.c_cc[VERASE] = 010;
	if (tcsetattr(slave, TCSANOW, &attr) < 0) {
		goto fail_slave;
	}
	memset(&ws, 0, sizeof(ws));
	ws.ws_col = 80;
	ws.ws_row = 24;
	if (ioctl(slave, TIOCSWINSZ, &ws) < 0) {
		goto fail_slave;
	}
	if (dup2(slave, STDIN_FILENO) != STDIN_FILENO ||
		dup2(slave, STDOUT_FILENO) != STDOUT_FILENO ||
		dup2(slave, STDERR_FILENO) != STDERR_FILENO) {
		goto fail_slave;
	}
	if (slave > 2) {
		close(slave);
	}
	pty_send(comm[1], SHL_PTY_SETUP);
	close(comm[1]);
	setenv("TERM", "xterm", 1);
	char **argv = (char*[]){ "/usr/bin/htop", NULL };
	execve(argv[0], argv, environ);
	exit(1);
fail_slave:
	close(slave);
fail:
	pty_send(comm[1], SHL_PTY_FAILED);
	close(comm[1]);
	exit(1);
}

static bool initialize_host(pid_t *out_pid, int *out_fd) {
	int fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		return false;
	}
	int comm[2];
	if (pipe(comm) < 0) {
		worker_log(L_ERROR, "Unable to pipe");
		close(fd);
		return false;
	}
	fcntl(comm[0], FD_CLOEXEC);
	fcntl(comm[1], FD_CLOEXEC);
	pid_t pid = fork();
	if (pid < 0) {
		close(comm[0]);
		close(comm[1]);
		close(fd);
		return false;
	} else if (!pid) {
		initialize_child(comm, fd);
	}
	close(comm[1]);
	char d = pty_recv(comm[0]);
	if (d != SHL_PTY_SETUP) {
		close(comm[0]);
		close(fd);
		return false;
	}
	close(comm[0]);
	*out_pid = pid;
	*out_fd = fd;
	return true;
}

void subterm_resize(unsigned short width, unsigned short height) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	tsm_screen_resize(account->viewer.screen, width, height);

	struct winsize ws = {
		.ws_col = width,
		.ws_row = height,
	};
	ioctl(account->viewer.fd, TIOCSWINSZ, &ws);
}

static void subterm_log(void *data, const char *file, int line, const char *func,
		const char *subs, unsigned int sev, const char *format, va_list args) {
	worker_vlog(L_DEBUG, format, args);
}

static void subterm_write(struct tsm_vte *vte, const char *u8,
		size_t len, void *data) {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	write(account->viewer.fd, u8, len);
	request_rerender(PANEL_MESSAGE_VIEW);
}

void initialize_subterm() {
	worker_log(L_DEBUG, "Setting up subterm");
	struct account_state *account =
		state->accounts->items[state->selected_account];
	tsm_screen_new(&account->viewer.screen, subterm_log, NULL);
	tsm_screen_resize(account->viewer.screen, 80, 24);
	tsm_vte_new(&account->viewer.vte,
			account->viewer.screen, subterm_write, NULL, subterm_log, NULL);

	if (!initialize_host(&account->viewer.pid, &account->viewer.fd)) {
		set_status(account, ACCOUNT_ERROR, "Error initializing terminal");
		return;
	}

	request_rerender(PANEL_MESSAGE_VIEW);
	// TODO: handle SIGCHLD
}

void cleanup_subterm() {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	close(account->viewer.fd);
	tsm_vte_unref(account->viewer.vte);
	tsm_screen_unref(account->viewer.screen);
	account->viewer.vte = NULL;
	account->viewer.screen = NULL;
}

void subterm_tick() {
	struct account_state *account =
		state->accounts->items[state->selected_account];
	static char buf[128];
	int r = read(account->viewer.fd, &buf, sizeof(buf));
	if (r == -1) {
		if (errno != EAGAIN) {
			cleanup_subterm();
			return;
		}
	}
	if (r > 0) {
		tsm_vte_input(account->viewer.vte, buf, r);
		request_rerender(PANEL_MESSAGE_VIEW);
	}
}
