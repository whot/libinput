noinst_PROGRAMS = event-debug ptraccel-debug
bin_PROGRAMS = libinput-list-devices libinput-debug-events
noinst_LTLIBRARIES = libshared.la

AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src \
              -I$(top_builddir)/src  # for libinput-version.h

libshared_la_SOURCES = \
		       shared.c \
		       shared.h
libshared_la_CFLAGS = $(LIBEVDEV_CFLAGS)
libshared_la_LIBADD = $(LIBEVDEV_LIBS)

event_debug_SOURCES = event-debug.c
event_debug_LDADD = ../src/libinput.la libshared.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
event_debug_LDFLAGS = -no-install
event_debug_CFLAGS = $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)

ptraccel_debug_SOURCES = ptraccel-debug.c
ptraccel_debug_LDADD = ../src/libfilter.la
ptraccel_debug_LDFLAGS = -no-install

libinput_list_devices_SOURCES = libinput-list-devices.c
libinput_list_devices_LDADD = ../src/libinput.la libshared.la $(LIBUDEV_LIBS)
libinput_list_devices_CFLAGS = $(LIBUDEV_CFLAGS)
dist_man1_MANS = libinput-list-devices.man

libinput_debug_events_SOURCES = $(event_debug_SOURCES)
libinput_debug_events_LDADD = $(event_debug_LDADD)
libinput_debug_events_CFLAGS = $(event_debug_CFLAGS)
dist_man1_MANS += libinput-debug-events.man

if BUILD_EVENTGUI
noinst_PROGRAMS += event-gui

event_gui_SOURCES = event-gui.c
event_gui_LDADD = ../src/libinput.la libshared.la $(CAIRO_LIBS) $(GTK_LIBS) $(LIBUDEV_LIBS)
event_gui_CFLAGS = $(CAIRO_CFLAGS) $(GTK_CFLAGS) $(LIBUDEV_CFLAGS)
event_gui_LDFLAGS = -no-install
endif

EXTRA_DIST = make-ptraccel-graphs.sh
