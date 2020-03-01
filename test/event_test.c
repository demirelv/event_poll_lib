
#include <stdio.h>
#include<stdlib.h>
#include <time.h>
#include <event_poll.h>

typedef struct {
	char *name;
	long int t;
	int count;
} timer_test_t;

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

static int timer(void *arg)
{
	timer_test_t *t = (timer_test_t *)arg;

	printf("entered %s :%ld %d \n", t->name, t->t ? _gettime() - t->t : 0, ++t->count);

	if (t->count > 10)
		return 1;
	t->t = _gettime();
	return 0;
}

int main (void)
{
	event_poll_t *e;
	timer_test_t *t;

	e = event_init();

	t = calloc(sizeof(timer_test_t), 1);
	t->name = "400";
	event_add_timer(e, 400, t, timer);
	t = calloc(sizeof(timer_test_t), 1);
	t->name = "3000";
	event_add_timer(e, 3000, t, timer);
	t = calloc(sizeof(timer_test_t), 1);
	t->name = "700";
	event_add_timer(e, 700, t, timer);
	t = calloc(sizeof(timer_test_t), 1);
	t->name = "4000";
	event_add_timer(e, 4000, t, timer);

	event_main_loop(e);

	return 0;
}
