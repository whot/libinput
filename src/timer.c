/*
 * Copyright © 2014 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "libinput-private.h"
#include "timer.h"

void
libinput_timer_init(struct libinput_timer *timer, struct libinput *libinput,
		    void (*timer_func)(uint64_t now, void *timer_func_data),
		    void *timer_func_data)
{
	timer->libinput = libinput;
	timer->timer_func = timer_func;
	timer->timer_func_data = timer_func_data;
}

static void
libinput_timer_arm_timer_fd(struct libinput *libinput)
{
	int r;
	struct libinput_timer *timer;
	struct itimerspec its = { { 0, 0 }, { 0, 0 } };
	uint64_t earliest_expire = UINT64_MAX;

	list_for_each(timer, &libinput->timer.list, link) {
		if (timer->expire < earliest_expire)
			earliest_expire = timer->expire;
	}

	if (earliest_expire != UINT64_MAX) {
		its.it_value.tv_sec = earliest_expire / 1000;
		its.it_value.tv_nsec = (earliest_expire % 1000) * 1000 * 1000;
	}

	r = timerfd_settime(libinput->timer.fd, TFD_TIMER_ABSTIME, &its, NULL);
	if (r)
		log_error(libinput, "timerfd_settime error: %s\n", strerror(errno));
}

void
libinput_timer_set(struct libinput_timer *timer, uint64_t expire)
{
#ifndef NDEBUG
	uint64_t now = libinput_now(timer->libinput);
	if (abs(expire - now) > 5000)
		log_bug_libinput(timer->libinput,
				 "timer offset more than 5s, now %"
				 PRIu64 " expire %" PRIu64 "\n",
				 now, expire);
#endif

	assert(expire);

	if (!timer->expire)
		list_insert(&timer->libinput->timer.list, &timer->link);

	timer->expire = expire;
	libinput_timer_arm_timer_fd(timer->libinput);
}

void
libinput_timer_cancel(struct libinput_timer *timer)
{
	if (!timer->expire)
		return;

	timer->expire = 0;
	list_remove(&timer->link);
	libinput_timer_arm_timer_fd(timer->libinput);
}

static void
libinput_timer_handler(void *data)
{
	struct libinput *libinput = data;
	struct libinput_timer *timer, *tmp;
	uint64_t now;

	now = libinput_now(libinput);
	if (now == 0)
		return;

	list_for_each_safe(timer, tmp, &libinput->timer.list, link) {
		if (timer->expire <= now) {
			/* Clear the timer before calling timer_func,
			   as timer_func may re-arm it */
			libinput_timer_cancel(timer);
			timer->timer_func(now, timer->timer_func_data);
		}
	}
}

int
libinput_timer_subsys_init(struct libinput *libinput)
{
	libinput->timer.fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (libinput->timer.fd < 0)
		return -1;

	list_init(&libinput->timer.list);

	libinput->timer.source = libinput_add_fd(libinput,
						 libinput->timer.fd,
						 libinput_timer_handler,
						 libinput);
	if (!libinput->timer.source) {
		close(libinput->timer.fd);
		return -1;
	}

	return 0;
}

void
libinput_timer_subsys_destroy(struct libinput *libinput)
{
	/* All timer users should have destroyed their timers now */
	assert(list_empty(&libinput->timer.list));

	libinput_remove_source(libinput, libinput->timer.source);
	close(libinput->timer.fd);
}
