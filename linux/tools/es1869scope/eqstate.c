// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * eqstate.c: what es1869scope -E knows, shared with a viewer (eqstate.h).
 * Notes:
 *
 * The engine runs as root and holds the devices; a viewer runs as the user
 * at the console and opens none of them. The engine writes the file, mapped,
 * between two steps of a sequence count, odd while it writes, and a viewer
 * copies it whole and takes the copy only if the count was even and the same
 * before and after. The engine holds a write lock on the file for as long as
 * it runs, which a viewer asks after: a lock ends with the process that holds
 * it, so a file left by an engine that crashed reads as stale, and the lock
 * gives the engine's pid with no risk of a pid used again. POSIX drops a lock
 * when any descriptor of the file closes, so the engine opens it once.
 *
 * The directory belongs to root with the audio group (es1869eq's script makes
 * it so), which lets the group read the file and write the FIFO, and no one
 * else make or replace anything in it. The FIFO takes a line a command, a
 * word and a number, written whole by one write, since a line shorter than
 * PIPE_BUF arrives whole. The engine opens it to read and write, so that a
 * read never meets the end of a file once a viewer closes it.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "eqstate.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(struct eqs_block) % 4 == 0, "eqs_block packs");
_Static_assert(sizeof(struct eqs_state) % 4 == 0, "eqs_state packs");

static int state_fd = -1; // held open for the lock's sake

// the directory a path is in, as stat finds it
static int dir_stat(const char *path, struct stat *st)
{
	char d[256];
	char *slash;

	snprintf(d, sizeof(d), "%s", path);
	slash = strrchr(d, '/');
	if (!slash)
		return stat(".", st);
	if (slash == d)
		slash++;
	*slash = 0;
	return stat(d, st);
}

int eqs_create(const char *path, const char *cmd, struct eqs_state **out,
	       int *cmd_fd)
{
	struct flock lk = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
	struct eqs_state *s;
	struct stat dir;
	int fd, err;

	*out = NULL;
	*cmd_fd = -1;
	if (dir_stat(path, &dir))
		return -errno;
	fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0640);
	if (fd < 0)
		return -errno;
	// one engine at a time: another's lock refuses this one
	if (fcntl(fd, F_SETLK, &lk) ||
	    ftruncate(fd, sizeof(struct eqs_state)) || fchmod(fd, 0640)) {
		err = -errno;
		close(fd);
		return err;
	}
	// the directory's group reads it; a run that may not give it the
	// group still writes it, for its own user
	if (fchown(fd, (uid_t)-1, dir.st_gid))
		(void)0;
	s = mmap(NULL, sizeof(*s), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (s == MAP_FAILED) {
		err = -errno;
		close(fd);
		return err;
	}
	memset(s, 0, sizeof(*s));
	s->magic = EQS_MAGIC;
	s->version = EQS_VERSION;
	s->size = sizeof(*s);
	s->pid = getpid();
	state_fd = fd;
	*out = s;
	if (!cmd)
		return 0;
	// the FIFO anew, the group's to write
	unlink(cmd);
	if (mkfifo(cmd, 0620))
		return -errno;
	if (chown(cmd, (uid_t)-1, dir.st_gid) || chmod(cmd, 0620))
		(void)0;
	*cmd_fd = open(cmd, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	return *cmd_fd < 0 ? -errno : 0;
}

void eqs_begin(struct eqs_state *s)
{
	__atomic_store_n(&s->seq, s->seq + 1, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void eqs_end(struct eqs_state *s)
{
	__atomic_store_n(&s->seq, s->seq + 1, __ATOMIC_RELEASE);
}

int eqs_command(int fd, char *word, unsigned int len, uint32_t *num)
{
	static char buf[256];
	static unsigned int have;
	unsigned int at;
	char line[64], w[16];
	char *nl;
	ssize_t r;
	unsigned long n;
	int ok;

	if (fd < 0)
		return 0;
	// a line too long to be a command is dropped
	at = have < sizeof(buf) - 1 ? have : 0;
	r = read(fd, buf + at, sizeof(buf) - 1 - at);
	have = at + (r > 0 ? (unsigned int)r : 0);
	buf[have] = 0;
	nl = strchr(buf, '\n');
	if (!nl)
		return 0;
	*nl = 0;
	snprintf(line, sizeof(line), "%.63s", buf);
	have -= (unsigned int)(nl + 1 - buf);
	memmove(buf, nl + 1, have);
	ok = sscanf(line, "%15s %lu", w, &n) == 2;
	if (!ok || !len)
		return -1;
	snprintf(word, len, "%s", w);
	*num = (uint32_t)n;
	return 1;
}

int eqs_map(const char *path, const struct eqs_state **out)
{
	const struct eqs_state *s;
	struct stat st;
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC), err = 0;

	*out = NULL;
	if (fd < 0)
		return -errno;
	// root's, as the service writes it, or the reader's own
	if (fstat(fd, &st))
		err = -errno;
	else if (!S_ISREG(st.st_mode) ||
		 (st.st_uid != 0 && st.st_uid != getuid()) ||
		 st.st_size < (off_t)sizeof(*s))
		err = -ESTALE;
	if (err) {
		close(fd);
		return err;
	}
	s = mmap(NULL, sizeof(*s), PROT_READ, MAP_SHARED, fd, 0);
	err = s == MAP_FAILED ? -errno : 0;
	close(fd);
	if (err)
		return err;
	if (s->magic != EQS_MAGIC || s->version != EQS_VERSION ||
	    s->size != sizeof(*s)) {
		munmap((void *)s, sizeof(*s));
		return -ESTALE;
	}
	*out = s;
	return 0;
}

int eqs_alive(const char *path)
{
	struct flock lk = { .l_type = F_RDLCK, .l_whence = SEEK_SET };
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC), pid = 0;

	if (fd < 0)
		return 0;
	if (!fcntl(fd, F_GETLK, &lk) && lk.l_type == F_WRLCK)
		pid = lk.l_pid;
	close(fd);
	return pid;
}

int eqs_copy(const struct eqs_state *s, struct eqs_state *out)
{
	int tries;

	for (tries = 0; tries < 200; tries++) {
		uint32_t a = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);

		if (a & 1) {
			usleep(500);
			continue;
		}
		memcpy(out, (const void *)s, sizeof(*out));
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		if (__atomic_load_n(&s->seq, __ATOMIC_RELAXED) == a)
			return 0;
	}
	return -1;
}

int eqs_send(const char *cmd, const char *word, uint32_t num)
{
	char line[48];
	int fd = open(cmd, O_WRONLY | O_NONBLOCK | O_CLOEXEC), n, err = 0;
	ssize_t w;

	if (fd < 0)
		return -errno;
	n = snprintf(line, sizeof(line), "%s %u\n", word, num);
	w = write(fd, line, (size_t)n);
	if (w < 0)
		err = -errno;
	else if (w != n)
		err = -EIO;
	close(fd);
	return err;
}
