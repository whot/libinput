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
#define _GNU_SOURCE
#include <config.h>

#include <linux/input.h>

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib.h>

#include <sys/timerfd.h>
#include <assert.h>

#include <libinput.h>
#include <libinput-util.h>

#include "shared.h"

/* XXX:kinetics
 * Adjustable parameters, you can tweak those to play around a bit
 */
/* speed calculation only considers the last X events */
#define CLICK_EVENTS_COUNT 5
/* Max allowed time between two real wheel events (for the last NEVENTS), if
 * greater we won't trigger kinetics */
#define MAX_TIME_BETWEEN_EVENTS 100 /* ms */
/* movement of the scroll bar in pixels per mouse click. Only a visual
 * adjustment, you shouldn't need to toggle this. */
#define CLICK_MOVEMENT_IN_PX 10
/* Speed at which kinetic scrolling kicks in, in clicks/s */
#define THRESHOLD_SPEED 0.2 /* clicks/ms */
/* Friction factor: many clicks per second per second to reduce */
#define FRICTION 1

#define clip(val_, min_, max_) min((max_), max((min_), (val_)))

struct tools_options options;

struct touch {
	int active;
	int x, y;
};

/* XXX kinetics */
struct wheel_event {
	uint32_t time;
	int v, h;
};

struct window {
	GtkWidget *win;
	GtkWidget *area;
	int width, height; /* of window */

	/* sprite position */
	double x, y;

	/* abs position */
	int absx, absy;

	/* scroll bar positions */
	double vx, vy;
	double hx, hy;

	/* touch positions */
	struct touch touches[32];

	/* l/m/r mouse buttons */
	int l, m, r;

	struct libinput_device *devices[50];

	/* XXX: kinetic scrolling things */
	struct kinetics {
		struct wheel_event events[CLICK_EVENTS_COUNT];
		int timerfd;
		double speed; /* clicks/ms, reduces through friction */
		uint32_t start_time; /* last physical event that triggered
					kinetics */
		uint32_t last_time; /* last emulated event */
	} kinetics;
};

static int
error(const char *fmt, ...)
{
	va_list args;
	fprintf(stderr, "error: ");

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	return EXIT_FAILURE;
}

static void
msg(const char *fmt, ...)
{
	va_list args;
	printf("info: ");

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

static gboolean
draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	struct window *w = data;
	struct touch *t;

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_rectangle(cr, 0, 0, w->width, w->height);
	cairo_fill(cr);

	/* draw pointer sprite */
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_save(cr);
	cairo_move_to(cr, w->x, w->y);
	cairo_rel_line_to(cr, 10, 15);
	cairo_rel_line_to(cr, -10, 0);
	cairo_rel_line_to(cr, 0, -15);
	cairo_fill(cr);
	cairo_restore(cr);

	/* draw scroll bars */
	cairo_set_source_rgb(cr, .4, .8, 0);

	cairo_save(cr);
	cairo_rectangle(cr, w->vx - 10, w->vy - 20, 20, 40);
	cairo_rectangle(cr, w->hx - 20, w->hy - 10, 40, 20);
	cairo_fill(cr);
	cairo_restore(cr);

	/* touch points */
	cairo_set_source_rgb(cr, .8, .2, .2);

	ARRAY_FOR_EACH(w->touches, t) {
		cairo_save(cr);
		cairo_arc(cr, t->x, t->y, 10, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_restore(cr);
	}

	/* abs position */
	cairo_set_source_rgb(cr, .2, .4, .8);

	cairo_save(cr);
	cairo_arc(cr, w->absx, w->absy, 10, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_restore(cr);

	/* lmr buttons */
	cairo_save(cr);
	if (w->l || w->m || w->r) {
		cairo_set_source_rgb(cr, .2, .8, .8);
		if (w->l)
			cairo_rectangle(cr, w->width/2 - 100, w->height - 200, 70, 30);
		if (w->m)
			cairo_rectangle(cr, w->width/2 - 20, w->height - 200, 40, 30);
		if (w->r)
			cairo_rectangle(cr, w->width/2 + 30, w->height - 200, 70, 30);
		cairo_fill(cr);
	}

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, w->width/2 - 100, w->height - 200, 70, 30);
	cairo_rectangle(cr, w->width/2 - 20, w->height - 200, 40, 30);
	cairo_rectangle(cr, w->width/2 + 30, w->height - 200, 70, 30);
	cairo_stroke(cr);
	cairo_restore(cr);

	return TRUE;
}

static void
map_event_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	struct window *w = data;
	GdkDisplay *display;
	GdkWindow *window;

	gtk_window_get_size(GTK_WINDOW(widget), &w->width, &w->height);

	w->x = w->width/2;
	w->y = w->height/2;

	w->vx = w->width/2;
	w->vy = w->height/2;
	w->hx = w->width/2;
	w->hy = w->height/2;

	g_signal_connect(G_OBJECT(w->area), "draw", G_CALLBACK(draw), w);

	window = gdk_event_get_window(event);
	display = gdk_window_get_display(window);

	gdk_window_set_cursor(gtk_widget_get_window(w->win),
			      gdk_cursor_new_for_display(display,
							 GDK_BLANK_CURSOR));
}

static void
window_init(struct window *w)
{
	memset(w, 0, sizeof(*w));

	w->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_events(w->win, 0);
	gtk_window_set_title(GTK_WINDOW(w->win), "libinput debugging tool");
	gtk_window_set_default_size(GTK_WINDOW(w->win), 1024, 768);
	gtk_window_maximize(GTK_WINDOW(w->win));
	gtk_window_set_resizable(GTK_WINDOW(w->win), TRUE);
	gtk_widget_realize(w->win);
	g_signal_connect(G_OBJECT(w->win), "map-event", G_CALLBACK(map_event_cb), w);
	g_signal_connect(G_OBJECT(w->win), "delete-event", G_CALLBACK(gtk_main_quit), NULL);

	w->area = gtk_drawing_area_new();
	gtk_widget_set_events(w->area, 0);
	gtk_container_add(GTK_CONTAINER(w->win), w->area);
	gtk_widget_show_all(w->win);
}

static void
window_cleanup(struct window *w)
{
	struct libinput_device **dev;
	ARRAY_FOR_EACH(w->devices, dev) {
		if (*dev)
			libinput_device_unref(*dev);
	}
}

static void
change_ptraccel(struct window *w, double amount)
{
	struct libinput_device **dev;

	ARRAY_FOR_EACH(w->devices, dev) {
		double speed;
		enum libinput_config_status status;

		if (*dev == NULL)
			continue;

		if (!libinput_device_config_accel_is_available(*dev))
			continue;

		speed = libinput_device_config_accel_get_speed(*dev);
		speed = clip(speed + amount, -1, 1);

		status = libinput_device_config_accel_set_speed(*dev, speed);

		if (status != LIBINPUT_CONFIG_STATUS_SUCCESS) {
			msg("%s: failed to change accel to %.2f (%s)\n",
			    libinput_device_get_name(*dev),
			    speed,
			    libinput_config_status_to_str(status));
		} else {
			printf("%s: speed is %.2f\n",
			       libinput_device_get_name(*dev),
			       speed);
		}

	}
}

static void
handle_event_device_notify(struct libinput_event *ev)
{
	struct libinput_device *dev = libinput_event_get_device(ev);
	struct libinput *li;
	struct window *w;
	const char *type;
	int i;

	if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED)
		type = "added";
	else
		type = "removed";

	msg("%s %-30s %s\n",
	    libinput_device_get_sysname(dev),
	    libinput_device_get_name(dev),
	    type);

	tools_device_apply_config(libinput_event_get_device(ev),
				  &options);

	li = libinput_event_get_context(ev);
	w = libinput_get_user_data(li);

	if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED) {
		for (i = 0; i < ARRAY_LENGTH(w->devices); i++) {
			if (w->devices[i] == NULL) {
				w->devices[i] = libinput_device_ref(dev);
				break;
			}
		}
	} else  {
		for (i = 0; i < ARRAY_LENGTH(w->devices); i++) {
			if (w->devices[i] == dev) {
				libinput_device_unref(w->devices[i]);
				w->devices[i] = NULL;
				break;
			}
		}
	}
}

static void
handle_event_motion(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double dx = libinput_event_pointer_get_dx(p),
	       dy = libinput_event_pointer_get_dy(p);

	w->x += dx;
	w->y += dy;
	w->x = clip(w->x, 0.0, w->width);
	w->y = clip(w->y, 0.0, w->height);
}

static void
handle_event_absmotion(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double x = libinput_event_pointer_get_absolute_x_transformed(p, w->width),
	       y = libinput_event_pointer_get_absolute_y_transformed(p, w->height);

	w->absx = x;
	w->absy = y;
}

static void
handle_event_touch(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
	int slot = libinput_event_touch_get_seat_slot(t);
	struct touch *touch;
	double x, y;

	if (slot == -1 || slot >= (int) ARRAY_LENGTH(w->touches))
		return;

	touch = &w->touches[slot];

	if (libinput_event_get_type(ev) == LIBINPUT_EVENT_TOUCH_UP) {
		touch->active = 0;
		return;
	}

	x = libinput_event_touch_get_x_transformed(t, w->width),
	y = libinput_event_touch_get_y_transformed(t, w->height);

	touch->active = 1;
	touch->x = (int)x;
	touch->y = (int)y;
}

/* XXX: kinetics */
/*************************************************************************
 * Simple speed calculation: take the last 5 events, and require all to be
 * in the same direction and each of them less than 100ms from the previous
 * one.
 *
 * Add up the values (likely more than 1 click per event at that speed),
 * divide by the total time of those last 5 events and you have a speed in
 * clicks/ms.
 *
 * Set a timer for the next calculated event. If we wake up and there hasn't
 * been a more recent physical event in the pipe, reduce the speed by a
 * friction factor, emulate a wheel click (visually only) and re-schedule a
 * wakeup.
 *
 ************************************************************************/
static int
calculate_wheel_speed(struct window *w, double *v_out, double *h_out)
{
	double v = 0.0,
	       h = 0.0;
	int i;
	struct wheel_event *cur, *next;
	struct wheel_event *last, *first;
	uint32_t tdelta;

	for (i = 0; i < ARRAY_LENGTH(w->kinetics.events) - 1; i++) {
		cur = &w->kinetics.events[i];
		next = &w->kinetics.events[i + 1];
		/* not enough events */
		if (next->time == 0)
			return 1;

		/* last X events aren't close enough together */
		if (cur->time - next->time > MAX_TIME_BETWEEN_EVENTS)
			return 1;

		/* require all events to go in the same direction */
		if (signbit(cur->v) != signbit(next->v) ||
		    signbit(cur->h) != signbit(next->h))
			return 1;

		/* add up all values */
		v += cur->v;
		h += cur->h;
	}

	first = &w->kinetics.events[0];
	last = &w->kinetics.events[ARRAY_LENGTH(w->kinetics.events) - 1];
	tdelta = first->time - last->time;

	/* calculate v, h as clicks per ms for the last
	 * ARRAY_LENGTH(w->kinetics.events) events */
	v = v/tdelta;
	h = h/tdelta;

	*v_out = v;
	*h_out = h;

	return 0;
}

/* XXX: kinetics */
static void
kinetics_arm_timer_for_speed(struct window *w, uint32_t now)
{
	struct itimerspec its = { { 0, 0 }, { 0, 0 } };
	uint64_t next; /* milliseconds */
	int r;
	uint32_t tdelta;
	double friction;

	/* speed is in clicks per ms, reduce by friction clicks per ms */
	tdelta = now - w->kinetics.last_time;
	friction = 1.0 * tdelta * FRICTION/1000.0;

	if (fabs(w->kinetics.speed) < fabs(friction)) {
		printf("time: %d Well, that was fun. I need to lie down now.\n",
		       now);
		w->kinetics.speed = 0.0;
		return;
	}

	if (w->kinetics.speed > 0.001)
		w->kinetics.speed -= friction;
	else if (w->kinetics.speed < -0.001)
		w->kinetics.speed += friction;

	w->kinetics.last_time = now;

	/* ms until next click */
	next = now + 1.0/fabs(w->kinetics.speed);
	its.it_value.tv_sec = next/1000;
	its.it_value.tv_nsec = (next % 1000) * 1000 * 1000;

	r = timerfd_settime(w->kinetics.timerfd, TFD_TIMER_ABSTIME, &its, NULL);
	assert(r == 0);

	printf("time: %d speed %f next: %ld Wheeeee!\n",
	       now, w->kinetics.speed, next);
}

/* XXX: kinetics */
static void
handle_wheel_kinetics(struct libinput_event_pointer *p, struct window *w)
{
	int vclicks = 0, hclicks = 0;
	struct kinetics *kinetics = &w->kinetics;
	struct wheel_event *cur = &kinetics->events[0];
	double vspeed, hspeed;

	/* get the wheel click data */
	if (libinput_event_pointer_has_axis(p,
					    LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
		vclicks = libinput_event_pointer_get_axis_value_discrete(p,
					LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
	if (libinput_event_pointer_has_axis(p,
					    LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
		hclicks = libinput_event_pointer_get_axis_value_discrete(p,
					LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);

	w->vy += vclicks * CLICK_MOVEMENT_IN_PX;
	w->vy = clip(w->vy, 0, w->height);
	w->hx += hclicks * CLICK_MOVEMENT_IN_PX;
	w->hx = clip(w->hx, 0, w->width);

	/* store the current click time in the list with all previous events */
	memmove(&kinetics->events[1],
		&kinetics->events[0],
		sizeof(kinetics->events) -
			sizeof(kinetics->events[0]));
	cur->time = libinput_event_pointer_get_time(p);
	cur->v = vclicks;
	cur->h = hclicks;

	if (calculate_wheel_speed(w, &vspeed, &hspeed) == 0) {
		printf("time: %d real event speed: vert %f\n", cur->time, vspeed);
		/* only vertical for now */
		if (fabs(vspeed) > THRESHOLD_SPEED) {
			printf("time: %d Kinetics started, off we go\n",
			       cur->time);
			w->kinetics.speed = vspeed;
			w->kinetics.start_time = cur->time;
			w->kinetics.last_time = cur->time;
			kinetics_arm_timer_for_speed(w, w->kinetics.start_time);
		}
	}
}

/* XXX: kinetics */
static gboolean
handle_kinetics_timer(GIOChannel *source, GIOCondition condition, gpointer data)
{
	struct window *w = data;
	uint64_t now;
	struct timespec ts = { 0, 0 };
	struct wheel_event *most_recent = &w->kinetics.events[0];

	/* drain the fd */
	read(w->kinetics.timerfd, &now, sizeof(now));

	/* abort if there's a more recent wheel event. this happens
	 * happens on wheels with low resistance or when the user manually
	 * slows to down */
	if (most_recent->time > w->kinetics.start_time) {
		printf("Aborting, got more wheel events (newest is %d, start time was %d)\n",
		       most_recent->time, w->kinetics.start_time);
		return TRUE;
	}

	clock_gettime(CLOCK_MONOTONIC, &ts);
	now = ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000;

	kinetics_arm_timer_for_speed(w, now);

	if (w->kinetics.speed > 0.0)
		w->vy += CLICK_MOVEMENT_IN_PX;
	else
		w->vy -= CLICK_MOVEMENT_IN_PX;
	w->vy = clip(w->vy, 0, w->height);

	gtk_widget_queue_draw(w->area);
	return TRUE;
}

static void
handle_event_axis(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double value;

	/* XXX kinetics */
	if (libinput_event_pointer_get_axis_source(p) ==
	    LIBINPUT_POINTER_AXIS_SOURCE_WHEEL) {
		handle_wheel_kinetics(p, w);
		return;
	}

	if (libinput_event_pointer_has_axis(p,
			LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
		value = libinput_event_pointer_get_axis_value(p,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		w->vy += value;
		w->vy = clip(w->vy, 0, w->height);
	}

	if (libinput_event_pointer_has_axis(p,
			LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
		value = libinput_event_pointer_get_axis_value(p,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
		w->hx += value;
		w->hx = clip(w->hx, 0, w->width);
	}
}

static int
handle_event_keyboard(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_keyboard *k = libinput_event_get_keyboard_event(ev);
	unsigned int key = libinput_event_keyboard_get_key(k);

	if (libinput_event_keyboard_get_key_state(k) ==
	    LIBINPUT_KEY_STATE_RELEASED)
		return 0;

	switch(key) {
	case KEY_ESC:
		return 1;
	case KEY_UP:
		change_ptraccel(w, 0.1);
		break;
	case KEY_DOWN:
		change_ptraccel(w, -0.1);
		break;
	default:
		break;
	}

	return 0;
}

static void
handle_event_button(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	unsigned int button = libinput_event_pointer_get_button(p);
	int is_press;

	is_press = libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED;

	switch (button) {
	case BTN_LEFT:
		w->l = is_press;
		break;
	case BTN_RIGHT:
		w->r = is_press;
		break;
	case BTN_MIDDLE:
		w->m = is_press;
		break;
	}

}

static gboolean
handle_event_libinput(GIOChannel *source, GIOCondition condition, gpointer data)
{
	struct libinput *li = data;
	struct window *w = libinput_get_user_data(li);
	struct libinput_event *ev;

	libinput_dispatch(li);

	while ((ev = libinput_get_event(li))) {
		switch (libinput_event_get_type(ev)) {
		case LIBINPUT_EVENT_NONE:
			abort();
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			handle_event_device_notify(ev);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION:
			handle_event_motion(ev, w);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
			handle_event_absmotion(ev, w);
			break;
		case LIBINPUT_EVENT_TOUCH_DOWN:
		case LIBINPUT_EVENT_TOUCH_MOTION:
		case LIBINPUT_EVENT_TOUCH_UP:
			handle_event_touch(ev, w);
			break;
		case LIBINPUT_EVENT_POINTER_AXIS:
			handle_event_axis(ev, w);
			break;
		case LIBINPUT_EVENT_TOUCH_CANCEL:
		case LIBINPUT_EVENT_TOUCH_FRAME:
			break;
		case LIBINPUT_EVENT_POINTER_BUTTON:
			handle_event_button(ev, w);
			break;
		case LIBINPUT_EVENT_KEYBOARD_KEY:
			if (handle_event_keyboard(ev, w)) {
				libinput_event_destroy(ev);
				gtk_main_quit();
				return FALSE;
			}
			break;
		}

		libinput_event_destroy(ev);
		libinput_dispatch(li);
	}
	gtk_widget_queue_draw(w->area);

	return TRUE;
}

static void
sockets_init(struct libinput *li, struct window *w)
{
	GIOChannel *c = g_io_channel_unix_new(libinput_get_fd(li));

	g_io_channel_set_encoding(c, NULL, NULL);
	g_io_add_watch(c, G_IO_IN, handle_event_libinput, li);

	/* XXX: kinetics */
	w->kinetics.timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	assert(w->kinetics.timerfd >= 0);
	c = g_io_channel_unix_new(w->kinetics.timerfd);
	g_io_channel_set_encoding(c, NULL, NULL);
	g_io_add_watch(c, G_IO_IN, handle_kinetics_timer, w);
}

static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

static const struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

int
main(int argc, char *argv[])
{
	struct window w;
	struct libinput *li;
	struct udev *udev;

	gtk_init(&argc, &argv);

	tools_init_options(&options);

	if (tools_parse_args(argc, argv, &options) != 0)
		return 1;

	udev = udev_new();
	if (!udev)
		error("Failed to initialize udev\n");

	li = tools_open_backend(&options, &w, &interface);
	if (!li)
		return 1;

	window_init(&w);
	sockets_init(li, &w);

	gtk_main();

	window_cleanup(&w);
	libinput_unref(li);
	udev_unref(udev);

	return 0;
}
