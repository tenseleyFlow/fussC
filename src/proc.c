#include "proc.h"

#include <errno.h>
#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util.h"

extern char **environ;

/* Growable byte buffer (kept NUL-terminated). */
struct buf {
	char *data;
	size_t len;
	size_t cap;
};

static void buf_append(struct buf *b, const char *src, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		while (b->len + n + 1 > b->cap)
			b->cap = b->cap ? b->cap * 2 : 256;
		b->data = xrealloc(b->data, b->cap);
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
	b->data[b->len] = '\0';
}

static char *buf_take(struct buf *b)
{
	return b->data ? b->data : xstrdup("");
}

int proc_run(char *const argv[], char **out, char **err)
{
	int op[2], ep[2];
	if (pipe(op) != 0)
		return -1;
	if (pipe(ep) != 0) {
		close(op[0]);
		close(op[1]);
		return -1;
	}

	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, op[1], STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&fa, ep[1], STDERR_FILENO);
	posix_spawn_file_actions_addclose(&fa, op[0]);
	posix_spawn_file_actions_addclose(&fa, ep[0]);
	posix_spawn_file_actions_addclose(&fa, op[1]);
	posix_spawn_file_actions_addclose(&fa, ep[1]);

	pid_t pid;
	int rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	close(op[1]);
	close(ep[1]);
	if (rc != 0) {
		close(op[0]);
		close(ep[0]);
		errno = rc;
		return -1;
	}

	/* Drain both pipes concurrently so a large stream on one never
	 * deadlocks the child writing to the other. */
	struct buf ob = {0}, eb = {0};
	struct pollfd p[2] = {{op[0], POLLIN, 0}, {ep[0], POLLIN, 0}};
	int open_fds = 2;
	while (open_fds > 0) {
		p[0].revents = p[1].revents = 0;
		if (poll(p, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (int i = 0; i < 2; i++) {
			if (p[i].fd < 0 || !(p[i].revents & (POLLIN | POLLHUP)))
				continue;
			char chunk[4096];
			ssize_t n = read(p[i].fd, chunk, sizeof(chunk));
			if (n > 0) {
				buf_append(i == 0 ? &ob : &eb, chunk,
				           (size_t)n);
			} else { /* EOF or error: stop watching this fd */
				close(p[i].fd);
				p[i].fd = -1;
				open_fds--;
			}
		}
	}
	if (p[0].fd >= 0)
		close(p[0].fd);
	if (p[1].fd >= 0)
		close(p[1].fd);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	*out = buf_take(&ob);
	*err = buf_take(&eb);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
