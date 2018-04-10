/*
 * Copyright © 2014 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <config.h>

#include <linux/input.h>

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib.h>

#include <libinput.h>
#include <libinput-util.h>

#include "shared.h"

#define clip(val_, min_, max_) min((max_), max((min_), (val_)))

struct touch {
	int active;
	int x, y;
};

struct point {
	double x, y;
};

struct window {
	struct tools_options options;

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

	/* touchpad swipe */
	struct {
		int nfingers;
		double x, y;
	} swipe;

	struct {
		int nfingers;
		double scale;
		double angle;
		double x, y;
	} pinch;

	struct {
		double x, y;
		double x_in, y_in;
		double x_down, y_down;
		double x_up, y_up;
		double pressure;
		double distance;
		double tilt_x, tilt_y;

		/* these are for the delta coordinates, but they're not
		 * deltas, they are converted into abs positions */
		size_t ndeltas;
		struct point deltas[64];
	} tool;

	struct libinput_device *devices[50];
};

LIBINPUT_ATTRIBUTE_PRINTF(1, 2)
static inline void
msg(const char *fmt, ...)
{
	va_list args;
	printf("info: ");

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

static inline void
draw_gestures(struct window *w, cairo_t *cr)
{
	int i;
	int offset;

	/* swipe */
	cairo_save(cr);
	cairo_translate(cr, w->swipe.x, w->swipe.y);
	for (i = 0; i < w->swipe.nfingers; i++) {
		cairo_set_source_rgb(cr, .8, .8, .4);
		cairo_arc(cr, (i - 2) * 40, 0, 20, 0, 2 * M_PI);
		cairo_fill(cr);
	}

	for (i = 0; i < 4; i++) { /* 4 fg max */
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_arc(cr, (i - 2) * 40, 0, 20, 0, 2 * M_PI);
		cairo_stroke(cr);
	}
	cairo_restore(cr);

	/* pinch */
	cairo_save(cr);
	offset = w->pinch.scale * 100;
	cairo_translate(cr, w->pinch.x, w->pinch.y);
	cairo_rotate(cr, w->pinch.angle * M_PI/180.0);
	if (w->pinch.nfingers > 0) {
		cairo_set_source_rgb(cr, .4, .4, .8);
		cairo_arc(cr, offset, -offset, 20, 0, 2 * M_PI);
		cairo_arc(cr, -offset, offset, 20, 0, 2 * M_PI);
		cairo_fill(cr);
	}

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_arc(cr, offset, -offset, 20, 0, 2 * M_PI);
	cairo_stroke(cr);
	cairo_arc(cr, -offset, offset, 20, 0, 2 * M_PI);
	cairo_stroke(cr);

	cairo_restore(cr);

}

static inline void
draw_scrollbars(struct window *w, cairo_t *cr)
{
	cairo_set_source_rgb(cr, .4, .8, 0);

	cairo_save(cr);
	cairo_rectangle(cr, w->vx - 10, w->vy - 20, 20, 40);
	cairo_rectangle(cr, w->hx - 20, w->hy - 10, 40, 20);
	cairo_fill(cr);
	cairo_restore(cr);
}

static inline void
draw_touchpoints(struct window *w, cairo_t *cr)
{
	struct touch *t;

	cairo_set_source_rgb(cr, .8, .2, .2);

	ARRAY_FOR_EACH(w->touches, t) {
		cairo_save(cr);
		cairo_arc(cr, t->x, t->y, 10, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_restore(cr);
	}
}

static inline void
draw_abs_pointer(struct window *w, cairo_t *cr)
{
	cairo_set_source_rgb(cr, .2, .4, .8);

	cairo_save(cr);
	cairo_arc(cr, w->absx, w->absy, 10, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_restore(cr);
}

static inline void
draw_buttons(struct window *w, cairo_t *cr)
{
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
}

static inline void
draw_tablet(struct window *w, cairo_t *cr)
{
	double x, y;
	int first, last;
	size_t mask;
	int i;

	/* tablet tool, square for prox-in location */
	cairo_save(cr);
	cairo_set_source_rgb(cr, .8, .8, .8);
	if (w->tool.x_in && w->tool.y_in) {
		cairo_rectangle(cr, w->tool.x_in - 15, w->tool.y_in - 15, 30, 30);
		cairo_stroke(cr);
		cairo_restore(cr);
		cairo_save(cr);
	}

	if (w->tool.x_down && w->tool.y_down) {
		cairo_rectangle(cr, w->tool.x_down - 10, w->tool.y_down - 10, 20, 20);
		cairo_stroke(cr);
		cairo_restore(cr);
		cairo_save(cr);
	}

	if (w->tool.x_up && w->tool.y_up) {
		cairo_rectangle(cr, w->tool.x_up - 10, w->tool.y_up - 10, 20, 20);
		cairo_stroke(cr);
		cairo_restore(cr);
		cairo_save(cr);
	}

	if (w->tool.pressure)
		cairo_set_source_rgb(cr, .8, .8, .2);

	cairo_translate(cr, w->tool.x, w->tool.y);
	cairo_scale(cr, 1.0 + w->tool.tilt_x/30.0, 1.0 + w->tool.tilt_y/30.0);
	cairo_arc(cr, 0, 0,
		  1 + 10 * max(w->tool.pressure, w->tool.distance),
		  0, 2 * M_PI);
	cairo_fill(cr);
	cairo_restore(cr);

	/* tablet deltas */
	mask = ARRAY_LENGTH(w->tool.deltas);
	first = max(w->tool.ndeltas + 1, mask) - mask;
	last = w->tool.ndeltas;

	cairo_save(cr);
	cairo_set_source_rgb(cr, .8, .8, .2);

	x = w->tool.deltas[first % mask].x;
	y = w->tool.deltas[first % mask].y;
	cairo_move_to(cr, x, y);

	for (i = first + 1; i < last; i++) {
		x = w->tool.deltas[i % mask].x;
		y = w->tool.deltas[i % mask].y;
		cairo_line_to(cr, x, y);
	}

	cairo_stroke(cr);

}

static inline void
draw_pointer(struct window *w, cairo_t *cr)
{
	/* draw pointer sprite */
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_save(cr);
	cairo_move_to(cr, w->x, w->y);
	cairo_rel_line_to(cr, 10, 15);
	cairo_rel_line_to(cr, -10, 0);
	cairo_rel_line_to(cr, 0, -15);
	cairo_fill(cr);
	cairo_restore(cr);
}

static inline void
draw_background(struct window *w, cairo_t *cr)
{
	int x1, x2, y1, y2, x3, y3, x4, y4;
	int cols;

	/* 10px and 5px grids */
	cairo_save(cr);
	cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
	x1 = w->width/2 - 200;
	y1 = w->height/2 - 200;
	x2 = w->width/2 + 200;
	y2 = w->height/2 - 200;
	for (cols = 1; cols < 10; cols++) {
		cairo_move_to(cr, x1 + 10 * cols, y1);
		cairo_rel_line_to(cr, 0, 100);
		cairo_move_to(cr, x1, y1 + 10 * cols);
		cairo_rel_line_to(cr, 100, 0);

		cairo_move_to(cr, x2 + 5 * cols, y2);
		cairo_rel_line_to(cr, 0, 50);
		cairo_move_to(cr, x2, y2 + 5 * cols);
		cairo_rel_line_to(cr, 50, 0);
	}

	/* 3px horiz/vert bar codes */
	x3 = w->width/2 - 200;
	y3 = w->height/2 + 200;
	x4 = w->width/2 + 200;
	y4 = w->height/2 + 100;
	for (cols = 0; cols < 50; cols++) {
		cairo_move_to(cr, x3 + 3 * cols, y3);
		cairo_rel_line_to(cr, 0, 20);

		cairo_move_to(cr, x4, y4 + 3 * cols);
		cairo_rel_line_to(cr, 20, 0);
	}
	cairo_stroke(cr);

	/* round targets */
	for (int i = 0; i <= 3; i++) {
		x1 = w->width * i/4.0;
		x2 = w->width * i/4.0;

		y1 = w->height * 1.0/4.0;
		y2 = w->height * 3.0/4.0;

		cairo_arc(cr, x1, y1, 10, 0, 2 * M_PI);
		cairo_stroke(cr);
		cairo_arc(cr, x2, y2, 10, 0, 2 * M_PI);
		cairo_stroke(cr);
	}

}

static gboolean
draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	struct window *w = data;

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_rectangle(cr, 0, 0, w->width, w->height);
	cairo_fill(cr);

	draw_background(w, cr);

	draw_gestures(w, cr);
	draw_scrollbars(w, cr);
	draw_touchpoints(w, cr);
	draw_abs_pointer(w, cr);
	draw_buttons(w, cr);
	draw_tablet(w, cr);
	draw_pointer(w, cr);

	return TRUE;
}

static void
map_event_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	struct window *w = data;
	GdkDisplay *display;
	GdkSeat *seat;
	GdkWindow *window;

	gtk_window_get_size(GTK_WINDOW(widget), &w->width, &w->height);

	w->x = w->width/2;
	w->y = w->height/2;

	w->vx = w->width/2;
	w->vy = w->height/2;
	w->hx = w->width/2;
	w->hy = w->height/2;

	w->swipe.x = w->width/2;
	w->swipe.y = w->height/2;

	w->pinch.scale = 1.0;
	w->pinch.x = w->width/2;
	w->pinch.y = w->height/2;

	g_signal_connect(G_OBJECT(w->area), "draw", G_CALLBACK(draw), w);

	window = gdk_event_get_window(event);
	display = gdk_window_get_display(window);

	gdk_window_set_cursor(gtk_widget_get_window(w->win),
			      gdk_cursor_new_for_display(display,
							 GDK_BLANK_CURSOR));

	seat = gdk_display_get_default_seat(display);
	gdk_seat_grab(seat,
		      window,
		      GDK_SEAT_CAPABILITY_ALL_POINTING,
		      FALSE, /* owner-events */
		      NULL, /* cursor */
		      NULL, /* triggering event */
		      NULL, /* prepare_func */
		      NULL /* prepare_func_data */
		     );
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
	size_t i;

	if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED)
		type = "added";
	else
		type = "removed";

	msg("%s %-30s %s\n",
	    libinput_device_get_sysname(dev),
	    libinput_device_get_name(dev),
	    type);

	li = libinput_event_get_context(ev);
	w = libinput_get_user_data(li);

	tools_device_apply_config(libinput_event_get_device(ev),
				  &w->options);

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

static void
handle_event_axis(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double value;

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

static void
handle_event_swipe(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_gesture *g = libinput_event_get_gesture_event(ev);
	int nfingers;
	double dx, dy;

	nfingers = libinput_event_gesture_get_finger_count(g);

	switch (libinput_event_get_type(ev)) {
	case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		w->swipe.nfingers = nfingers;
		w->swipe.x = w->width/2;
		w->swipe.y = w->height/2;
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		dx = libinput_event_gesture_get_dx(g);
		dy = libinput_event_gesture_get_dy(g);
		w->swipe.x += dx;
		w->swipe.y += dy;
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_END:
		w->swipe.nfingers = 0;
		w->swipe.x = w->width/2;
		w->swipe.y = w->height/2;
		break;
	default:
		abort();
	}
}

static void
handle_event_pinch(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_gesture *g = libinput_event_get_gesture_event(ev);
	int nfingers;
	double dx, dy;

	nfingers = libinput_event_gesture_get_finger_count(g);

	switch (libinput_event_get_type(ev)) {
	case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
		w->pinch.nfingers = nfingers;
		w->pinch.x = w->width/2;
		w->pinch.y = w->height/2;
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
		dx = libinput_event_gesture_get_dx(g);
		dy = libinput_event_gesture_get_dy(g);
		w->pinch.x += dx;
		w->pinch.y += dy;
		w->pinch.scale = libinput_event_gesture_get_scale(g);
		w->pinch.angle += libinput_event_gesture_get_angle_delta(g);
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_END:
		w->pinch.nfingers = 0;
		w->pinch.x = w->width/2;
		w->pinch.y = w->height/2;
		w->pinch.angle = 0.0;
		w->pinch.scale = 1.0;
		break;
	default:
		abort();
	}
}

static void
handle_event_tablet(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_tablet_tool *t = libinput_event_get_tablet_tool_event(ev);
	double x, y;
	struct point point;
	int idx;
	const int mask = ARRAY_LENGTH(w->tool.deltas);

	x = libinput_event_tablet_tool_get_x_transformed(t, w->width);
	y = libinput_event_tablet_tool_get_y_transformed(t, w->height);

	switch (libinput_event_get_type(ev)) {
	case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
		if (libinput_event_tablet_tool_get_proximity_state(t) ==
		    LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT) {
			w->tool.x_in = 0;
			w->tool.y_in = 0;
			w->tool.x_down = 0;
			w->tool.y_down = 0;
			w->tool.x_up = 0;
			w->tool.y_up = 0;
		} else {
			w->tool.x_in = x;
			w->tool.y_in = y;
			w->tool.ndeltas = 0;
			w->tool.deltas[0].x = w->width/2;
			w->tool.deltas[0].y = w->height/2;
		}
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_TIP:
		w->tool.pressure = libinput_event_tablet_tool_get_pressure(t);
		w->tool.distance = libinput_event_tablet_tool_get_distance(t);
		w->tool.tilt_x = libinput_event_tablet_tool_get_tilt_x(t);
		w->tool.tilt_y = libinput_event_tablet_tool_get_tilt_y(t);
		if (libinput_event_tablet_tool_get_tip_state(t) ==
		    LIBINPUT_TABLET_TOOL_TIP_DOWN) {
			w->tool.x_down = x;
			w->tool.y_down = y;
		} else {
			w->tool.x_up = x;
			w->tool.y_up = y;
		}
		/* fallthrough */
	case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
		w->tool.x = x;
		w->tool.y = y;
		w->tool.pressure = libinput_event_tablet_tool_get_pressure(t);
		w->tool.distance = libinput_event_tablet_tool_get_distance(t);
		w->tool.tilt_x = libinput_event_tablet_tool_get_tilt_x(t);
		w->tool.tilt_y = libinput_event_tablet_tool_get_tilt_y(t);

		/* Add the delta to the last position and store them as abs
		 * coordinates */
		idx = w->tool.ndeltas % mask;
		point = w->tool.deltas[idx];

		idx = (w->tool.ndeltas + 1) % mask;
		point.x += libinput_event_tablet_tool_get_dx(t);
		point.y += libinput_event_tablet_tool_get_dy(t);
		w->tool.deltas[idx] = point;
		w->tool.ndeltas++;
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		break;
	default:
		abort();
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
		case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		case LIBINPUT_EVENT_GESTURE_SWIPE_END:
			handle_event_swipe(ev, w);
			break;
		case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
		case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
		case LIBINPUT_EVENT_GESTURE_PINCH_END:
			handle_event_pinch(ev, w);
			break;
		case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
		case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
		case LIBINPUT_EVENT_TABLET_TOOL_TIP:
		case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
			handle_event_tablet(ev, w);
			break;
		case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
		case LIBINPUT_EVENT_TABLET_PAD_RING:
		case LIBINPUT_EVENT_TABLET_PAD_STRIP:
			break;
		case LIBINPUT_EVENT_SWITCH_TOGGLE:
			break;
		}

		libinput_event_destroy(ev);
		libinput_dispatch(li);
	}
	gtk_widget_queue_draw(w->area);

	return TRUE;
}

static void
sockets_init(struct libinput *li)
{
	GIOChannel *c = g_io_channel_unix_new(libinput_get_fd(li));

	g_io_channel_set_encoding(c, NULL, NULL);
	g_io_add_watch(c, G_IO_IN, handle_event_libinput, li);
}

static void
usage(void) {
	printf("Usage: libinput debug-gui [options] [--udev <seat>|--device /dev/input/event0]\n");
}

int
main(int argc, char **argv)
{
	struct window w;
	struct tools_options options;
	struct libinput *li;
	enum tools_backend backend = BACKEND_UDEV;
	const char *seat_or_device = "seat0";
	bool grab = false;
	bool verbose = false;

	gtk_init(&argc, &argv);

	tools_init_options(&options);

	while (1) {
		int c;
		int option_index = 0;
		enum {
			OPT_DEVICE = 1,
			OPT_UDEV,
			OPT_GRAB,
			OPT_VERBOSE,
		};
		static struct option opts[] = {
			CONFIGURATION_OPTIONS,
			{ "help",                      no_argument,       0, 'h' },
			{ "device",                    required_argument, 0, OPT_DEVICE },
			{ "udev",                      required_argument, 0, OPT_UDEV },
			{ "grab",                      no_argument,       0, OPT_GRAB },
			{ "verbose",                   no_argument,       0, OPT_VERBOSE },
			{ 0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "h", opts, &option_index);
		if (c == -1)
			break;

		switch(c) {
		case '?':
			exit(1);
			break;
		case 'h':
			usage();
			exit(0);
			break;
		case OPT_DEVICE:
			backend = BACKEND_DEVICE;
			seat_or_device = optarg;
			break;
		case OPT_UDEV:
			backend = BACKEND_UDEV;
			seat_or_device = optarg;
			break;
		case OPT_GRAB:
			grab = true;
			break;
		case OPT_VERBOSE:
			verbose = true;
			break;
		default:
			if (tools_parse_option(c, optarg, &options) != 0) {
				usage();
				return 1;
			}
			break;
		}

	}

	if (optind < argc) {
		usage();
		return 1;
	}

	li = tools_open_backend(backend, seat_or_device, verbose, grab);
	if (!li)
		return 1;

	libinput_set_user_data(li, &w);

	window_init(&w);
	w.options = options;
	sockets_init(li);
	handle_event_libinput(NULL, 0, li);

	gtk_main();

	window_cleanup(&w);
	libinput_unref(li);

	return 0;
}
