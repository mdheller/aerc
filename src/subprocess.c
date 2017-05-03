#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <libtsm.h>
#include <termbox.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <wordexp.h>
#include <termios.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <pty.h>
#include "xkbcommon-keysyms.h"
#include "ui.h"
#include "log.h"
#include "subprocess.h"

static void subprocess_pty_log(void *data, const char *file, int line,
		const char *func, const char *subs, unsigned int sev,
		const char *format, va_list args) {
	worker_vlog(L_DEBUG, format, args);
}

static void subprocess_pty_write(struct tsm_vte *vte, const char *u8,
		size_t len, void *data) {
	struct subprocess *subp = data;
	write(subp->pty->fd, u8, len);
	request_rerender(PANEL_MESSAGE_VIEW);
}

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
	if (pty) {
		subp->pty = calloc(sizeof(struct pty), 1);
		tsm_screen_new(&subp->pty->screen, subprocess_pty_log, subp);
		tsm_screen_resize(subp->pty->screen, 80, 24);
		tsm_vte_new(&subp->pty->vte, subp->pty->screen,
				subprocess_pty_write, subp, subprocess_pty_log, subp);
		subp->pty->fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
	}
	return subp;
}

static void init_child_pty(struct subprocess *subp) {
	if (grantpt(subp->pty->fd) < 0) {
		close(subp->pty->fd);
		goto fail;
	}
	if (unlockpt(subp->pty->fd) < 0) {
		close(subp->pty->fd);
		goto fail;
	}
	char *slave_name = ptsname(subp->pty->fd);
	if (!slave_name) {
		close(subp->pty->fd);
		goto fail;
	}
	int slave = open(slave_name, O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (slave < 0) {
		close(subp->pty->fd);
		goto fail;
	}
	pid_t pid = setsid();
	if (pid < 0) {
		close(subp->pty->fd);
		goto fail_slave;
	}
	close(subp->pty->fd);
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
	if (subp->stdin_piped) {
		dup2(subp->pipes[0][0], STDIN_FILENO);
	} else {
		dup2(slave, STDIN_FILENO);
	}
	dup2(slave, STDOUT_FILENO);
	dup2(slave, STDERR_FILENO);
	ioctl(slave, TIOCSCTTY, NULL);
	close(slave);
	setenv("TERM", "xterm-256color", 1);
	return;
fail_slave:
	close(slave);
fail:
	// TODO: bubble up error
	exit(1);
}

static void init_child(struct subprocess *subp) {
	// Clear signals
	sigset_t sigset;
	sigemptyset(&sigset);
	if (sigprocmask(SIG_SETMASK, &sigset, NULL) < 0) {
		exit(1);
	}
	for (int i = 0; i < SIGUNUSED; ++i) {
		signal(i, SIG_DFL);
	}
	if (subp->pty) {
		init_child_pty(subp);
	} else {
		// Set up file descriptors
		dup2(subp->pipes[0][0], STDIN_FILENO);
		dup2(subp->pipes[1][1], STDOUT_FILENO);
		dup2(subp->pipes[2][1], STDERR_FILENO);
	}
	for (size_t i = 0; i < sizeof(subp->pipes) / sizeof(*subp->pipes); ++i) {
		close(subp->pipes[i][0]);
		close(subp->pipes[i][1]);
	}
	execvp(subp->argv[0], subp->argv);
	exit(1);
}

static void init_parent(struct subprocess *subp) {
	close(subp->pipes[0][0]);
	for (size_t i = 1; i < sizeof(subp->pipes) / sizeof(*subp->pipes); ++i) {
		close(subp->pipes[i][1]);
	}
	subp->io_fds[0] = subp->pipes[0][1];
	subp->io_fds[1] = subp->pipes[1][0];
	subp->io_fds[2] = subp->pipes[2][0];
}

void subprocess_start(struct subprocess *subp) {
	signal(SIGPIPE, SIG_IGN); // we prefer EPIPE
	subp->pid = fork();
	if (subp->pid < 0) {
		// TODO: bubble up error?
		worker_log(L_DEBUG, "fork() failed (%d)", errno);
	} else if (!subp->pid) {
		init_child(subp);
	} else {
		init_parent(subp);
	}
}

void subprocess_queue_stdin(struct subprocess *subp, uint8_t *data, size_t length) {
	subp->stdin_piped = true;
	struct io_capture *cap = calloc(sizeof(struct io_capture), 1);
	cap->data = data;
	cap->size = cap->len = length;
	if (!subp->io_stdin) {
		subp->io_stdin = cap;
	} else {
		struct io_capture *prev = subp->io_stdin;
		while (prev->next) {
			prev = prev->next;
		}
		prev->next = cap;
	}
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
	to->stdin_piped = true;
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

static bool subprocess_update_pty(struct pty *pty) {
	static char buf[1024];
	int r = read(pty->fd, &buf, sizeof(buf));
	if (r == -1) {
		if (errno != EAGAIN) {
			worker_log(L_DEBUG, "pty read error %d", errno);
			return true;
		}
	}
	if (r > 0) {
		tsm_vte_input(pty->vte, buf, r);
		request_rerender(PANEL_MESSAGE_VIEW);
	}
	return false;
}

bool subprocess_update(struct subprocess *subp) {
	while (subp->io_fds[0] != -1 && subp->io_stdin && subp->io_stdin->len) {
		size_t amt = subp->io_stdin->len;
		int written = write(subp->io_fds[0], subp->io_stdin->data
				+ subp->io_stdin->index, subp->io_stdin->len);
		if (written > 0) {
			worker_log(L_DEBUG, "Wrote %d of %zd bytes to child %d",
					written, amt, subp->pid);
			subp->io_stdin->len -= written;
			subp->io_stdin->index += written;
			if (subp->io_stdin->len == 0) {
				if (subp->io_stdin->next) {
					struct io_capture *next = subp->io_stdin->next;
					free(subp->io_stdin);
					// TODO: optional destructor for capture data
					subp->io_stdin = next;
					continue;
				} else {
					close(subp->io_fds[0]);
					subp->io_fds[0] = -1;
				}
			}
		} else {
			worker_log(L_DEBUG, "Error %d writing to child %d",
					errno, subp->pid);
			close(subp->io_fds[0]);
			subp->io_fds[0] = -1;
			// TODO: Anything else?
		}
		break;
	}
	if (subp->io_fds[1] != -1 && subp->io_stdout) {
		int amt = update_io_capture(subp->io_fds[1], subp->io_stdout);
		if (amt >= 0) {
			worker_log(L_DEBUG, "Read %d bytes from child %d", amt, subp->pid);
		} else if (errno != EAGAIN) {
			close(subp->io_fds[1]);
			subp->io_fds[1] = -1;
		}
	}
	if (subp->io_fds[2] != -1 && subp->io_stderr) {
		int amt = update_io_capture(subp->io_fds[2], subp->io_stderr);
		if (amt >= 0) {
			worker_log(L_DEBUG, "Read %d bytes from child %d", amt, subp->pid);
		} else if (errno != EAGAIN) {
			close(subp->io_fds[2]);
			subp->io_fds[2] = -1;
		}
	}
	if (subp->pty) {
		if (subprocess_update_pty(subp->pty)) {
			return true;
		}
	}
	int w;
	if (waitpid(subp->pid, &w, WNOHANG) != 0)
		return WIFEXITED(w);
	return false;
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

struct tb_to_xkb_map {
	uint32_t tb_key;
	uint32_t xkb_keysym;
	uint32_t tsm_mods;
};

struct tb_to_xkb_map subp_tb_to_xkb[] = {
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
	{ TB_KEY_BACKSPACE2, XKB_KEY_BackSpace, 0 },
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
	{ TB_KEY_SPACE, XKB_KEY_KP_Space, 0 },
};

void subprocess_pty_key(struct subprocess *subp, struct tb_event *event) {
	assert(subp->pty);
	if (event->key) {
		for (size_t i = 0; i < sizeof(subp_tb_to_xkb) / sizeof(subp_tb_to_xkb[0]); ++i) {
			if (subp_tb_to_xkb[i].tb_key == event->key) {
				uint32_t mods = subp_tb_to_xkb[i].tsm_mods;
				if ((event->mod & TB_MOD_ALT)) {
					mods |= TSM_ALT_MASK;
				}
				tsm_vte_handle_keyboard(subp->pty->vte, subp_tb_to_xkb[i].xkb_keysym,
						event->ch, mods, event->ch);
				return;
			}
		}
	} else {
		uint32_t mods = 0;
		if ((event->mod & TB_MOD_ALT)) {
			mods |= TSM_ALT_MASK;
		}
		tsm_vte_handle_keyboard(subp->pty->vte, 0, event->ch, mods, event->ch);
	}
}

void subprocess_pty_resize(struct subprocess *subp,
		unsigned short width, unsigned short height) {
	tsm_screen_resize(subp->pty->screen, width, height);

	struct winsize ws = {
		.ws_col = width,
		.ws_row = height,
	};
	ioctl(subp->pty->fd, TIOCSWINSZ, &ws);
}
