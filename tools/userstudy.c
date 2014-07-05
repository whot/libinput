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

#include <assert.h>
#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib.h>

#include <libevdev/libevdev.h>
#include <libinput.h>
#include <libinput-util.h>

#define clip(val_, min_, max_) min((max_), max((min_), (val_)))

#define NUM_TRAINING_TARGETS 3
#define NUM_STUDY_TARGETS 3

enum study_state {
	STATE_WELCOME,
	STATE_CONFIRM,
	STATE_TRAINING,
	STATE_TRAINING_DONE,
	STATE_STUDY,
	STATE_DONE,
};

struct touch {
	int active;
	int x, y;
};

struct device {
	struct list node;
	struct libinput_device *dev;
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
	int vx, vy;
	int hx, hy;

	/* touch positions */
	struct touch touches[32];

	/* l/m/r mouse buttons */
	int l, m, r;

	struct list device_list;

	/* the device used during the study */
	struct libinput_device *device;


	enum study_state state;
	enum study_state new_state; /* changed on release */

	int object_x,
	    object_y;
	int object_radius;

	int ntargets;

	int fd;
	char *filename;
	char *cwd;
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

static void
usage_device(void)
{
	printf("To function correctly, this tool needs read access to \n"
	       "the device used during analysis. Run it as root, or enable\n"
	       "read access on the /dev/input/event<N> devices that you\n"
	       "want to use during the study. e.g. \n"
	       "	sudo chmod o+r /dev/input/event7\n");
}

static void
usage(void)
{
	printf("%s [path/to/device]\n", program_invocation_short_name);
	printf("\n"
	       "This  tool runs a basic user-study, analyzing input events \n"
	       "from pointer devices.\n"
	       "\n");
	usage_device();

}

static void
default_target(struct window *w)
{
	w->object_x = w->width/2;
	w->object_y = w->height * 0.75;
	w->object_radius = 50;
}

static void
show_text(cairo_t *cr, struct window *w)
{
	const int font_size = 14;
	const char **str;
	int line;

	const char *welcome_message[] =
		{"Thank you for participating in this study. The goal of this study",
		 "is to analyze the pointer acceleration code. The study",
		 "consists of several randomized sets of moving targets.",
		 "Your task is to simply click on these targets as they appear,",
		 "using a mouse-like input device.",
		 "",
		 "The data collected by this program is limited to:",
		 "- input device name and capabilities (what evtest(1) would see)",
		 "- input events with timestamps",
		 "- converted events and timestamps",
		 "",
		 "No data that can personally identify you is collected.",
		 "Key events are received by this program but not collected or",
		 "analyzed. Only the Esc key is handled.",
		 "",
		 "If you are worried about key event handling, restart as user (not as root)",
		 "or specify the mouse device path on the commandline.",
		 "",
		 "The data collected is available in a plain text file and must",
		 "be sent to me via email. This tool does not send any data.",
		 "",
		 "You can abort any time by hitting Esc, or closing the window",
		 "",
		 "When you're ready to go, please click on the green circle",
		 "with your mouse. This will also confirm the device you will",
		 "be using for this study",
		 NULL};

	const char *confirm_message[] = {
		"Thanks again for participipating. I know which device you",
		"will be using now. Almost ready to go, please confirm the following:",
		"",
		"- events from other devices will be ignored (except the Esc key).",
		"  if the device you just used wasn't the right one, please restart",
		"- you have normal or corrected vision and you are able to see the target below",
		"- you are familiar with using a mouse-like input device",
		"- the input device is NOT a touchpad or trackball",
		"  (sorry, we'll be evaluating those separately)",
		"- this is the first time you are running this study.",
		"  There is no benefit in training for this study to get ",
		"  better results, it just skews the data",
		"",
		 "When you're ready to go, please click on the green circle",
		 "with your mouse. This starts training for this study.",
		 "No data is collected during training",
		"",
		"You can abort any time by hitting Esc, or closing the window",
		NULL
	};

	const char *training_message[] = {
		"Click on the targets as they appear.",
		NULL
	};

	const char *training_done_message[] = {
		"Thanks. Training is now complete.",
		"",
		"To start the study, click on the green circle. This",
		"will start event collection",
		"During the stury, click on the targets as they appear.",
		"",
		"To run through the training again, click anywhere else",
		"",
		"You can abort any time by hitting Esc, or closing the window",
		NULL
	};

	const char *done_message[] = {
		"Thank you for completing the study.",
		"Click on the green circle to exit",
		"",
		"Your results are available in the file shown below.",
		"Please send them unmodified to peter.hutterer@who-t.net, with a subject",
		"of \"userstudy results\"",
		"",
		NULL
	};

	switch(w->state) {
	case STATE_WELCOME:
		str = welcome_message;
		break;
	case STATE_CONFIRM:
		str = confirm_message;
		break;
	case STATE_TRAINING:
	case STATE_STUDY:
		str = training_message;
		break;
	case STATE_TRAINING_DONE:
		str = training_done_message;
		break;
	case STATE_DONE:
		str = done_message;
		break;
	default:
			    return;
	}

	cairo_save(cr);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_font_size(cr, font_size);

	line = 0;

	while (*str != 0) {
		cairo_move_to(cr, 400, 100 + line * font_size * 1.2);
		cairo_show_text(cr, *str);
		str++;
		line++;
	}

	if (w->state == STATE_DONE) {
		char buf[PATH_MAX];
		cairo_move_to(cr, 400, 100 + line * font_size * 1.2);
		snprintf(buf, sizeof(buf), "%s/%s", w->cwd, w->filename);
		cairo_show_text(cr, buf);
	}

	cairo_restore(cr);

}

static gboolean
draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	struct window *w = data;
#if 0
	struct touch *t;
#endif

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_rectangle(cr, 0, 0, w->width, w->height);
	cairo_fill(cr);

	show_text(cr, w);

	/* draw the click object */
	cairo_save(cr);
	cairo_set_source_rgb(cr, .4, .8, 0);
	cairo_arc(cr, w->object_x, w->object_y, w->object_radius, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_restore(cr);

	/* draw pointer sprite */
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_save(cr);
	cairo_move_to(cr, w->x, w->y);
	cairo_rel_line_to(cr, 10, 15);
	cairo_rel_line_to(cr, -10, 0);
	cairo_rel_line_to(cr, 0, -15);
	cairo_fill(cr);
	cairo_restore(cr);


#if 0
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
#endif

	return TRUE;
}

static void
map_event_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	struct window *w = data;

	gtk_window_get_size(GTK_WINDOW(widget), &w->width, &w->height);

	w->x = w->width/2;
	w->y = w->height/2;

	w->vx = w->width/2;
	w->vy = w->height/2;
	w->hx = w->width/2;
	w->hy = w->height/2;

	g_signal_connect(G_OBJECT(w->area), "draw", G_CALLBACK(draw), w);

	gdk_window_set_cursor(gtk_widget_get_window(w->win),
			      gdk_cursor_new(GDK_BLANK_CURSOR));


	if (w->width < 1024 || w->height < 768) {
		fprintf(stderr, "Sorry, your screen is too small\n");
		gtk_main_quit();
		return;
	}


	default_target(w);
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

	list_init(&w->device_list);
}

static void
study_init(struct window *w)
{
	default_target(w);
	w->state = STATE_WELCOME;
	w->new_state = STATE_WELCOME;
}

static void
device_remove(struct device *d)
{
	list_remove(&d->node);
	libinput_device_unref(d->dev);
	free(d);
}

static void
window_cleanup(struct window *w)
{
	struct device *d, *tmp;

	list_for_each_safe(d, tmp, &w->device_list, node)
		device_remove(d);

	if (w->state != STATE_DONE)
		unlink(w->filename);

	free(w->filename);
	free(w->cwd);
}

static void
change_ptraccel(struct window *w, double amount)
{
	struct device *d;

	list_for_each(d, &w->device_list, node) {
		double accel, old_accel;
		enum libinput_config_status status;

		if (!libinput_device_config_accel_is_available(d->dev))
			continue;

		accel = libinput_device_config_accel_get_speed(d->dev);
		if (fabs(accel + amount) > 1.0)
			continue;

		old_accel = accel;

		do {
			accel = clip(accel + amount, -1, 1);
			amount += amount;

			status = libinput_device_config_accel_set_speed(d->dev, accel);
			accel = libinput_device_config_accel_get_speed(d->dev);
		} while (status == LIBINPUT_CONFIG_STATUS_SUCCESS &&
			 accel == old_accel);

		if (status != LIBINPUT_CONFIG_STATUS_SUCCESS) {
			msg("%s: failed to change accel to %.2f (%s)\n",
			    libinput_device_get_sysname(d->dev),
			    accel,
			    libinput_config_status_to_str(status));
		}

	}
}


static void
handle_event_device_notify(struct libinput_event *ev)
{
	struct libinput_device *dev = libinput_event_get_device(ev);
	struct libinput *li;
	struct window *w;
	struct device *d;
	const char *type;

	if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED)
		type = "added";
	else
		type = "removed";

	msg("%s %s\n", libinput_device_get_sysname(dev), type);

	li = libinput_event_get_context(ev);
	w = libinput_get_user_data(li);

	if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED) {
		d = malloc(sizeof(*d));
		assert(d != NULL);

		d->dev = libinput_device_ref(dev);
		list_insert(&w->device_list, &d->node);
	} else  {
		list_for_each(d, &w->device_list, node) {
			if (d->dev == dev) {
				device_remove(d);
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

	if (slot == -1 || slot >= ARRAY_LENGTH(w->touches))
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
	enum libinput_pointer_axis axis = libinput_event_pointer_get_axis(p);
	double v = libinput_event_pointer_get_axis_value(p);

	switch (axis) {
	case LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL:
		w->vy += (int)v;
		w->vy = clip(w->vy, 0, w->height);
		break;
	case LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL:
		w->hx += (int)v;
		w->hx = clip(w->hx, 0, w->width);
		break;
	default:
		abort();
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

static bool
click_in_circle(struct window *w, int x, int y)
{
	double dist;

	if (x < w->object_x - w->object_radius ||
	    x > w->object_x + w->object_radius ||
	    y < w->object_y - w->object_radius ||
	    y > w->object_y + w->object_radius)
		return false;

	dist = (x - w->object_x) * (x - w->object_x) +
		(y - w->object_y) * (y - w->object_y);

	if (dist > w->object_radius * w->object_radius)
		return false;

	return true;
}

static void
new_target(struct window *w)
{
	struct timespec tp;
	uint32_t time;
	int r;

	int point_dist = 300;
	int max_radius = 30;

	int xoff = w->width/2 - point_dist * 1.5;
	int yoff = w->height/2 - point_dist;

	clock_gettime(CLOCK_MONOTONIC, &tp);
	time = tp.tv_sec * 1000 + tp.tv_nsec/1000000;

	/* Grid of 4x3 positions */
	r = rand() % 12;

	w->object_x = xoff + (r % 4) * point_dist;
	w->object_y = yoff + (r/4) * point_dist;

	if (w->state == STATE_STUDY) {
		dprintf(w->fd,
			"<target time=\"%d\" number=\"%d\" x=\"%d\" y=\"%d\" r=\"%d\" />\n",
			time,
			w->ntargets,
			w->object_x,
			w->object_y,
			w->object_radius);
	}

	w->ntargets--;
}

static void
start_recording(struct window *w)
{
	struct libevdev *evdev;
	int fd;
	int code, type;
	char path[PATH_MAX];

	w->ntargets = NUM_STUDY_TARGETS;

	w->filename = strdup("userstudy-results.xml.XXXXXX");
	w->fd = mkstemp(w->filename);
	assert(w->fd > -1);
	w->cwd = get_current_dir_name();

	dprintf(w->fd, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	dprintf(w->fd, "<results>\n");
	dprintf(w->fd, "<device name=\"%s\" pid=\"%#x\" vid=\"%#x\">\n",
		libinput_device_get_name(w->device),
		libinput_device_get_id_product(w->device),
		libinput_device_get_id_vendor(w->device));

	snprintf(path,
		 sizeof(path),
		 "/dev/input/%s",
		 libinput_device_get_sysname(w->device));
	fd = open(path, O_RDONLY);
	assert(fd > 0);

	assert(libevdev_new_from_fd(fd, &evdev) == 0);

	for (type = EV_KEY; type < EV_MAX; type++) {
		int max = libevdev_event_type_get_max(type);

		if (!libevdev_has_event_type(evdev, type))
			continue;

		for (code = 0; code < max; code++) {
			if (!libevdev_has_event_code(evdev, type, code))
				continue;

			dprintf(w->fd,
				"<bit type=\"%d\" code=\"%d\"/> <!-- %s %s -->\n",
				type, code,
				libevdev_event_type_get_name(type),
				libevdev_event_code_get_name(type, code));
		}
	}

	libevdev_free(evdev);
	close(fd);

	dprintf(w->fd, "</device>\n");
	dprintf(w->fd, "<events>\n");
}

static void
stop_recording(struct window *w)
{
	dprintf(w->fd, "</events>\n");
	dprintf(w->fd, "</results>\n");
	close(w->fd);
}

static void
record_event(struct window *w, struct libinput_event *ev)
{
	struct libinput_device *device;
	struct libinput_event_pointer *ptrev;
	enum libinput_event_type type;

	if (w->state != STATE_STUDY)
		return;

	device = libinput_event_get_device(ev);
	if (device != w->device)
		return;
	type = libinput_event_get_type(ev);
	switch (type) {
	case LIBINPUT_EVENT_NONE:
		abort();
	case LIBINPUT_EVENT_DEVICE_ADDED:
	case LIBINPUT_EVENT_DEVICE_REMOVED:
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
	case LIBINPUT_EVENT_TOUCH_DOWN:
	case LIBINPUT_EVENT_TOUCH_MOTION:
	case LIBINPUT_EVENT_TOUCH_UP:
	case LIBINPUT_EVENT_POINTER_AXIS:
	case LIBINPUT_EVENT_TOUCH_CANCEL:
	case LIBINPUT_EVENT_TOUCH_FRAME:
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		return;
	case LIBINPUT_EVENT_POINTER_MOTION:
	case LIBINPUT_EVENT_POINTER_BUTTON:
		break;
	}

	ptrev = libinput_event_get_pointer_event(ev);

	if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
		dprintf(w->fd,
			"<button time=\"%d\" x=\"%f\" y=\"%f\" button=\"%d\" state=\"%d\" hit=\"%d\"/>\n",
			libinput_event_pointer_get_time(ptrev),
			w->x, w->y,
			libinput_event_pointer_get_button(ptrev),
			libinput_event_pointer_get_button_state(ptrev),
			(int)click_in_circle(w, w->x, w->y));
	} else {
		dprintf(w->fd,
			"<motion time=\"%d\"  x=\"%f\" y=\"%f\" dx=\"%f\" dy=\"%f\"/>\n",
			libinput_event_pointer_get_time(ptrev),
			w->x, w->y,
			libinput_event_pointer_get_dx(ptrev),
			libinput_event_pointer_get_dy(ptrev));
	}
}

static void
handle_event_button(struct libinput_event *ev, struct window *w)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	unsigned int button = libinput_event_pointer_get_button(p);
	int is_press;
	struct libinput_device *device = libinput_event_get_device(ev);

	if (w->device && device != w->device)
		return;

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

	/* userstudy state transitions */
	if (!is_press) {
		if (w->new_state != w->state) {
			w->state = w->new_state;

			switch(w->state) {
			case STATE_TRAINING:
			case STATE_STUDY:
				new_target(w);
				break;
			default:
				break;
			}
		}
		return;
	}

	switch(w->state) {
	case STATE_WELCOME:
		if (!click_in_circle(w, w->x, w->y))
		    return;
		w->new_state = STATE_CONFIRM;
		assert(w->device == NULL);
		w->device = libinput_event_get_device(ev);
		default_target(w);
		break;
	case STATE_CONFIRM:
		if (!click_in_circle(w, w->x, w->y))
		    return;
		w->new_state = STATE_TRAINING;
		w->ntargets = NUM_TRAINING_TARGETS;
		default_target(w);
		break;
	case STATE_TRAINING:
		if (!click_in_circle(w, w->x, w->y))
			return;

		if (w->ntargets == 0) {
			w->new_state = STATE_TRAINING_DONE;
			default_target(w);
			return;
		}
		new_target(w);
		break;
	case STATE_TRAINING_DONE:
		if (!click_in_circle(w, w->x, w->y)) {
			w->ntargets = NUM_TRAINING_TARGETS;
			w->new_state = STATE_TRAINING;
			return;
		}
		start_recording(w);
		w->new_state = STATE_STUDY;
		break;
	case STATE_STUDY:
		if (!click_in_circle(w, w->x, w->y))
			return;

		if (w->ntargets == 0) {
			stop_recording(w);
			w->state = STATE_DONE;
			w->new_state = STATE_DONE;
			default_target(w);
			return;
		}
		new_target(w);
		break;
	case STATE_DONE:
		if (!click_in_circle(w, w->x, w->y))
			return;

		gtk_main_quit();
		printf("Your results are in %s/%s\n",
		       w->cwd,
		       w->filename);
		break;
	default:
		return;
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
		record_event(w, ev);
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

static int
check_for_devices(struct libinput *li)
{
	libinput_dispatch(li);

	/* we expect all DEVICE_ADDED events before any other events */
	if (libinput_next_event_type(li) ==
	    LIBINPUT_EVENT_DEVICE_ADDED)
		return 0;

	return 1;
}

static void
sockets_init(struct libinput *li)
{
	GIOChannel *c = g_io_channel_unix_new(libinput_get_fd(li));

	g_io_channel_set_encoding(c, NULL, NULL);
	g_io_add_watch(c, G_IO_IN, handle_event_libinput, li);
}

static int
parse_opts(int argc, char *argv[])
{
	while (1) {
		static struct option long_options[] = {
			{ "help", no_argument, 0, 'h' },
		};

		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "h", long_options,
				&option_index);
		if (c == -1)
			break;

		switch(c) {
		case 'h':
			usage();
			return 0;
		default:
			usage();
			return 1;
		}
	}

	return 0;
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

const static struct libinput_interface interface = {
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

	if (parse_opts(argc, argv) != 0)
		return 1;

	udev = udev_new();
	if (!udev)
		error("Failed to initialize udev\n");

	li = libinput_udev_create_context(&interface, &w, udev);
	if (!li || libinput_udev_assign_seat(li, "seat0") != 0)
		error("Failed to initialize context from udev\n");

	if (check_for_devices(li) != 0) {
		fprintf(stderr, "Unable to find at least one input device.\n\n");
		usage_device();
		return 1;
	}

	window_init(&w);
	study_init(&w);
	sockets_init(li);

	gtk_main();

	window_cleanup(&w);
	libinput_unref(li);
	udev_unref(udev);

	return 0;
}
