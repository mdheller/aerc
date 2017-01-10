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

#include "xkbcommon-keysyms.h"
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

static void initialize_child(int comm[2], int fd, const char *exe) {
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
	setenv("TERM", "xterm-256color", 1);
	char **argv = (char*[]){ strdup(exe), NULL };
	execvp(argv[0], argv);
	exit(1);
fail_slave:
	close(slave);
fail:
	pty_send(comm[1], SHL_PTY_FAILED);
	close(comm[1]);
	exit(1);
}

static bool initialize_host(pid_t *out_pid, int *out_fd, const char *exe) {
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
		initialize_child(comm, fd, exe);
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

void subterm_resize(struct subterm *st, unsigned short width, unsigned short height) {
	tsm_screen_resize(st->screen, width, height);

	struct winsize ws = {
		.ws_col = width,
		.ws_row = height,
	};
	ioctl(st->fd, TIOCSWINSZ, &ws);
}

static void subterm_log(void *data, const char *file, int line, const char *func,
		const char *subs, unsigned int sev, const char *format, va_list args) {
	worker_vlog(L_DEBUG, format, args);
}

static void subterm_write(struct tsm_vte *vte, const char *u8,
		size_t len, void *data) {
	struct subterm *st = data;
	write(st->fd, u8, len);
	request_rerender(PANEL_MESSAGE_VIEW);
}

struct subterm *initialize_subterm(const char *exe) {
	worker_log(L_DEBUG, "Setting up subterm");
	struct subterm *st = calloc(sizeof(struct subterm), 1);
	tsm_screen_new(&st->screen, subterm_log, st);
	tsm_screen_resize(st->screen, 80, 24);
	tsm_vte_new(&st->vte, st->screen, subterm_write, st, subterm_log, st);

	if (!initialize_host(&st->pid, &st->fd, exe)) {
		struct account_state *account =
			state->accounts->items[state->selected_account];
		set_status(account, ACCOUNT_ERROR, "Error initializing terminal");
		return NULL;
	}

	st->clear = true;
	request_rerender(PANEL_MESSAGE_VIEW);
	return st;
}

void cleanup_subterm(struct subterm **st) {
	if (!st || !*st) {
		return;
	}

	close((*st)->fd);
	tsm_vte_unref((*st)->vte);
	tsm_screen_unref((*st)->screen);
	free(*st);
	*st = 0;

	request_rerender(PANEL_ALL);
}

bool subterm_tick(struct subterm *st) {
	static char buf[1024];
	int r = read(st->fd, &buf, sizeof(buf));
	if (r == -1) {
		if (errno != EAGAIN) {
			cleanup_subterm(&st);
			return true;
		}
	}
	if (r > 0) {
		tsm_vte_input(st->vte, buf, r);
	}
	request_rerender(PANEL_MESSAGE_VIEW);
	return false;
}

struct tb_to_xkb_map {
	uint32_t tb_key;
	uint32_t xkb_keysym;
	uint32_t tsm_mods;
};

struct tb_to_xkb_map tb_to_xkb[] = {
	{ TB_KEY_F1, XKB_KEY_F1, 0 },
	{ TB_KEY_F2, XKB_KEY_F2, 0 },
	{ TB_KEY_F3, XKB_KEY_F3, 0 },
	{ TB_KEY_F4, XKB_KEY_F4, 0 },
	{ TB_KEY_F5, XKB_KEY_F5, 0 },
	{ TB_KEY_F6, XKB_KEY_F6, 0 },
	{ TB_KEY_F7, XKB_KEY_F7, 0 },
	{ TB_KEY_F8, XKB_KEY_F8, 0 },
	{ TB_KEY_F9, XKB_KEY_F9, 0 },
	{ TB_KEY_F9, XKB_KEY_F9, 0 },
	{ TB_KEY_F10, XKB_KEY_F10, 0 },
	{ TB_KEY_F11, XKB_KEY_F11, 0 },
	{ TB_KEY_F12, XKB_KEY_F12, 0 },
	{ TB_KEY_INSERT, XKB_KEY_Insert, 0 },
	{ TB_KEY_DELETE, XKB_KEY_Delete, 0 },
	{ TB_KEY_HOME, XKB_KEY_Home, 0 },
	{ TB_KEY_END, XKB_KEY_End, 0 },
	{ TB_KEY_PGUP, XKB_KEY_Page_Up, 0 },
	{ TB_KEY_PGDN, XKB_KEY_Page_Down, 0 },
	{ TB_KEY_ARROW_UP, XKB_KEY_Up, 0 },
	{ TB_KEY_ARROW_DOWN, XKB_KEY_Down, 0 },
	{ TB_KEY_ARROW_LEFT, XKB_KEY_Left, 0 },
	{ TB_KEY_ARROW_RIGHT, XKB_KEY_Right, 0 },
	{ TB_KEY_CTRL_2, XKB_KEY_2, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_A, XKB_KEY_a, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_B, XKB_KEY_b, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_C, XKB_KEY_c, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_D, XKB_KEY_d, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_E, XKB_KEY_e, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_F, XKB_KEY_f, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_G, XKB_KEY_g, TSM_CONTROL_MASK },
	{ TB_KEY_BACKSPACE, XKB_KEY_BackSpace, 0 },
	{ TB_KEY_TAB, XKB_KEY_Tab, 0 },
	{ TB_KEY_CTRL_J, XKB_KEY_J, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_K, XKB_KEY_K, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_L, XKB_KEY_L, TSM_CONTROL_MASK },
	{ TB_KEY_ENTER, XKB_KEY_Return, 0 },
	{ TB_KEY_CTRL_N, XKB_KEY_N, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_O, XKB_KEY_O, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_P, XKB_KEY_P, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_Q, XKB_KEY_Q, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_R, XKB_KEY_R, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_S, XKB_KEY_S, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_T, XKB_KEY_T, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_U, XKB_KEY_U, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_V, XKB_KEY_V, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_W, XKB_KEY_W, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_X, XKB_KEY_X, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_Y, XKB_KEY_Y, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_Z, XKB_KEY_Z, TSM_CONTROL_MASK },
	{ TB_KEY_ESC, XKB_KEY_Escape, 0 },
	{ TB_KEY_CTRL_4, XKB_KEY_4, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_5, XKB_KEY_5, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_6, XKB_KEY_6, TSM_CONTROL_MASK },
	{ TB_KEY_CTRL_7, XKB_KEY_7, TSM_CONTROL_MASK },
	{ TB_KEY_SPACE, XKB_KEY_space, 0 },
};

void subterm_handle_key(struct tsm_vte *vte, struct tb_event *event) {
	if (event->key) {
		for (size_t i = 0; i < sizeof(tb_to_xkb) / sizeof(tb_to_xkb[0]); ++i) {
			if (tb_to_xkb[i].tb_key == event->key) {
				uint32_t mods = tb_to_xkb[i].tsm_mods;
				if ((event->mod & TB_MOD_ALT)) {
					mods |= TSM_ALT_MASK;
				}
				tsm_vte_handle_keyboard(vte, tb_to_xkb[i].xkb_keysym,
						event->ch, mods, event->ch);
				return;
			}
		}
	} else {
		uint32_t mods = 0;
		if ((event->mod & TB_MOD_ALT)) {
			mods |= TSM_ALT_MASK;
		}
		tsm_vte_handle_keyboard(vte, 0, event->ch, mods, event->ch);
	}
}
