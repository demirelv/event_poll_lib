#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <poll.h>

#include "event_poll.h"

#include "list.h"

#ifndef TEMP_FAILURE_RETRY
/* Used to retry syscalls that can return EINTR. */
#define TEMP_FAILURE_RETRY(exp) ({				\
	typeof (exp) _rc;									\
	do {													\
		_rc = (exp);									\
	} while (_rc == -1 && errno == EINTR);		\
	_rc; })
#endif

typedef struct {
	struct listnode fd_list;
	struct listnode timer_list;
	struct listnode qfd_list;
	struct listnode qtimer_list;
	pthread_mutex_t lock;
	int recv_fd;
	int send_fd;
} event_list_t;

typedef struct {
	int interval;
	int id;
	long int t;
	int deleted;
	void *arg;
	int (*callback)(void *arg);
	struct listnode list;
} timer_list_t;

typedef struct {
	int fd;
	int id;
	short int events;
	void *arg;
	int deleted;
	int (*callback)(int fd, short int events, void *arg);
	struct listnode list;
} fd_t;

static void list_init(struct listnode *node)
{
	node->next = node;
	node->prev = node;
}

static void list_add_tail(struct listnode *head, struct listnode *item)
{
	item->next = head;
	item->prev = head->prev;
	head->prev->next = item;
	head->prev = item;
}

static void list_remove(struct listnode *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;
}

event_poll_t *event_init(void)
{
	event_poll_t *e;
	event_list_t *l;
	int s[2];

	e = malloc(sizeof(event_poll_t));
	l = malloc(sizeof(event_list_t));

	if (l == NULL || e == NULL) {
		goto bail;
	}

	l->send_fd = l->recv_fd = -1;
	e->ctx = l;

	list_init(&l->fd_list);
	list_init(&l->timer_list);

	list_init(&l->qfd_list);
	list_init(&l->qtimer_list);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) != 0) {
		goto bail;
	}

	l->send_fd = s[0];
	l->recv_fd = s[1];
	fcntl(s[0], F_SETFD, FD_CLOEXEC);
	fcntl(s[0], F_SETFL, O_NONBLOCK);
	fcntl(s[1], F_SETFD, FD_CLOEXEC);
	fcntl(s[1], F_SETFL, O_NONBLOCK);

	if (pthread_mutex_init(&l->lock, NULL)) {
		goto bail;
	}

	return e;
bail:
	if (l != NULL) {
		if (l->send_fd > 0)
			close(l->send_fd);
		if (l->recv_fd > 0)
			close(l->recv_fd);
		free(l);
	}
	if (e != NULL) {
		free(e);
	}
	return NULL;
}

static void _event_remove_fd(fd_t *fd)
{
	list_remove(&fd->list);
	free(fd);
}

static void _event_remove_timer(timer_list_t *t)
{
	list_remove(&t->list);
	free(t);
}

static int find_fd_id(event_list_t *l)
{
	int i;

	for (i = 1; i < 0xFFFFFFF; i++) {
		int found = 0;
		fd_t *fd = NULL;

		list_for_each_entry(fd, &l->fd_list, list) {
			if (fd->id == i) {
				found = 1;
				break;
			}
		}
		if (found == 0 ) {
			list_for_each_entry(fd, &l->qfd_list, list) {
				if (fd->id == i) {
					found = 1;
					break;
				}
			}
		}
		if (found == 0)
			return i;
	}
	return -1;
}

int event_remove_fd(event_poll_t *e, int handle)
{
	event_list_t *l;
	fd_t *fd;
	int s;
	int rc = -1;
	int found = 0;

	if (e == NULL || handle < 0) {
		goto out;
	}
	l = e->ctx;

	pthread_mutex_lock(&l->lock);
	list_for_each_entry(fd, &l->fd_list, list) {
		if (fd->id == handle) {
			fd->deleted = 1;
			TEMP_FAILURE_RETRY(write(l->send_fd, &s, sizeof(s)));
			rc = 0;
			found = 1;
			break;
		}
	}
	if (found == 0) {
		list_for_each_entry(fd, &l->qfd_list, list) {
			if (fd->id == handle) {
				fd->deleted = 1;
				TEMP_FAILURE_RETRY(write(l->send_fd, &s, sizeof(s)));
				rc = 0;
				break;
			}
		}
	}
	pthread_mutex_unlock(&l->lock);
out:
	return rc;
}

int event_add_fd(event_poll_t *e, int handle, short int events, void *arg, event_poll_callback_t callback)
{
	event_list_t *l;
	fd_t *fd = NULL;
	int s;
	int id;

	if (e == NULL || handle < 0) {
		goto bail;
	}

	l = e->ctx;

	fd = calloc(sizeof(fd_t), 1);
	if (fd == NULL) {
		goto bail;
	}

	fd->fd = handle;
	fd->callback = callback;
	fd->events = events;
	fd->arg = arg;

	pthread_mutex_lock(&l->lock);
	id = find_fd_id(l);
	pthread_mutex_unlock(&l->lock);
	if (id < 0) {
		goto bail;
	}

	fd->id = id;
	pthread_mutex_lock(&l->lock);
	list_add_tail(&l->qfd_list, &fd->list);
	pthread_mutex_unlock(&l->lock);

	TEMP_FAILURE_RETRY(write(l->send_fd, &s, sizeof(s)));

	return fd->id;
bail:
	if (fd != NULL) {
		free(fd);
	}
	return -1;
}

static int find_timer_id(event_list_t *l)
{
	int i;

	for (i = 1; i < 0xFFFFFFF; i++) {
		int found = 0;
		timer_list_t *t;

		list_for_each_entry(t, &l->timer_list, list) {
			if (t->id == i) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			list_for_each_entry(t, &l->qtimer_list, list) {
				if (t->id == i) {
					found = 1;
					break;
				}
			}
		}
		if (found == 0)
			return i;
	}
	return -1;
}

static long int _gettime(void)
{
	int ret;
	struct timespec ts;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {
		return 0;
	}

	return ts.tv_sec * 1000 + (long int)(ts.tv_nsec / 1000000);
}

int event_remove_timer(event_poll_t *e, int handle)
{
	event_list_t *l;
	int s;
	int rc = -1;
	timer_list_t *t;

	if (e == NULL || handle < 0) {
		goto out;
	}

	l = e->ctx;

	pthread_mutex_lock(&l->lock);
	list_for_each_entry(t, &l->timer_list, list) {
		if (t->id == handle) {
			t->deleted = 1;
			TEMP_FAILURE_RETRY(write(l->send_fd, &s, sizeof(s)));
			rc = 0;
		}
	}
	list_for_each_entry(t, &l->qtimer_list, list) {
		if (t->id == handle) {
			t->deleted = 1;
			TEMP_FAILURE_RETRY(write(l->send_fd, &s, sizeof(s)));
			rc = 0;
		}
	}
	pthread_mutex_unlock(&l->lock);
	return rc;
out:
	return -1;
}

int event_add_timer(event_poll_t *e, unsigned int interval, void *arg, event_timer_callback_t callback)
{
	event_list_t *l = NULL;
	timer_list_t *t = NULL;
	int s;
	int id;

	if (e == NULL) {
		goto bail;
	}

	l = e->ctx;
	t = calloc(sizeof(timer_list_t), 1);
	if (t == NULL) {
		goto bail;
	}

	t->interval = interval;
	t->callback = callback;
	t->arg = arg;
	t->t = _gettime() + interval;
	pthread_mutex_lock(&l->lock);
	id = find_timer_id(l);
	pthread_mutex_unlock(&l->lock);
	if (id < 0) {
		goto bail;
	}
	t->id = id;
	pthread_mutex_lock(&l->lock);
	list_add_tail(&l->qtimer_list, &t->list);
	pthread_mutex_unlock(&l->lock);

	TEMP_FAILURE_RETRY(write(l->send_fd, &s, sizeof(s)));

	return t->id;
bail:
	if (t != NULL) {
		free(t);
	}
	return -1;
}

int event_main_loop(event_poll_t *e)
{
	struct pollfd ufds[256];
	event_list_t *l;
	long int time_dif;
	int rc = -1;

	if (e == NULL) {
		goto bail;
	}

	l = e->ctx;

	for (;;) {
		int nr, i, timeout = 10000;
		int fd_count = 0;
		long int ct;
		fd_t *fd, *nfd;
		timer_list_t *t, *nt;

		ufds[fd_count].fd = l->recv_fd;
		ufds[fd_count].events = POLLIN;
		ufds[fd_count].revents = 0;
		fd_count++;

		ct = _gettime();
		pthread_mutex_lock(&l->lock);
		list_for_each_entry_safe(fd, nfd, &l->qfd_list, list) {
			list_remove(&fd->list);
			list_add_tail(&l->fd_list, &fd->list);
		}
		list_for_each_entry_safe(fd, nfd, &l->fd_list, list) {
			if (fd->fd < 0 || fd->deleted) {
				_event_remove_fd(fd);
				continue;
			}
			ufds[fd_count].fd = fd->fd;
			ufds[fd_count].events = fd->events;
			ufds[fd_count].revents = 0;
			fd_count++;
		}

		list_for_each_entry_safe(t, nt, &l->qtimer_list, list) {
			list_remove(&t->list);
			list_add_tail(&l->timer_list, &t->list);
		}

		list_for_each_entry_safe(t, nt, &l->timer_list, list) {
			if (t->deleted) {
				_event_remove_timer(t);
				continue;
			}
			time_dif = t->t > ct ? t->t - ct : 0;
			if (timeout > time_dif) {
				timeout = time_dif;
			}
		}
		pthread_mutex_unlock(&l->lock);

		nr = poll(ufds, fd_count, timeout);
		if (nr < 0)
			continue;
		ct = _gettime();
		pthread_mutex_lock(&l->lock);
		for (i = 0; i < fd_count; i++) {
			if (ufds[i].fd == l->recv_fd && ufds[i].revents & POLLIN) {
				char tmp[32];
				TEMP_FAILURE_RETRY(read(l->recv_fd, tmp, sizeof(tmp)));
			} else {
				list_for_each_entry(fd, &l->fd_list, list) {
					if (fd->deleted) {
						continue;
					}
					if (fd->fd == ufds[i].fd) {
						do {
							if (ufds[i].revents & fd->events & POLLIN) {
								pthread_mutex_unlock(&l->lock);
								rc = fd->callback(fd->fd, POLLIN, fd->arg);
								pthread_mutex_lock(&l->lock);
								if (rc != 0) {
									fd->deleted = 1;
									break;
								}
							}

							if (ufds[i].revents & fd->events & POLLPRI) {
								pthread_mutex_unlock(&l->lock);
								rc = fd->callback(fd->fd, POLLPRI, fd->arg);
								pthread_mutex_lock(&l->lock);
								if (rc != 0) {
									fd->deleted = 1;
									break;
								}
							}

							if (ufds[i].revents & fd->events & POLLOUT) {
								pthread_mutex_unlock(&l->lock);
								rc = fd->callback(fd->fd, POLLOUT, fd->arg);
								pthread_mutex_lock(&l->lock);
								if (rc != 0) {
									fd->deleted = 1;
									break;
								}
							}

							if (ufds[i].revents & fd->events & POLLERR) {
								pthread_mutex_unlock(&l->lock);
								rc = fd->callback(fd->fd, POLLERR, fd->arg);
								pthread_mutex_lock(&l->lock);
								if (rc != 0) {
									fd->deleted = 1;
									break;
								}
							}

							if (ufds[i].revents & fd->events & POLLHUP) {
								pthread_mutex_unlock(&l->lock);
								rc = fd->callback(fd->fd, POLLHUP, fd->arg);
								pthread_mutex_lock(&l->lock);
								if (rc != 0) {
									fd->deleted = 1;
									break;
								}
							}

							if (ufds[i].revents & fd->events & POLLNVAL) {
								pthread_mutex_unlock(&l->lock);
								rc = fd->callback(fd->fd, POLLNVAL, fd->arg);
								pthread_mutex_lock(&l->lock);
								if (rc != 0) {
									fd->deleted = 1;
									break;
								}
							}
						} while (0);
					}
				}
				pthread_mutex_unlock(&l->lock);
			}
		}

		list_for_each_entry(t, &l->timer_list, list) {
			if (t->deleted) {
				continue;
			}
			if (ct >= t->t) {
				t->t = ct + t->interval;
				pthread_mutex_unlock(&l->lock);
				rc = t->callback(t->arg);
				pthread_mutex_lock(&l->lock);
				if (rc != 0) {
					t->deleted = 1;
				}
			}
		}
		pthread_mutex_unlock(&l->lock);
	}

	return 0;
bail:
	return -1;

}
