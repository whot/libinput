noinst_PROGRAMS = ptraccel-debug
bin_PROGRAMS = libinput-list-devices libinput-debug-events libinput

AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src \
              -I$(top_builddir)/src  # for libinput-version.h
AM_CFLAGS = $(GCC_CFLAGS)
AM_CXXFLAGS = $(GCC_CXXFLAGS)

shared_sources = \
		 shared.c \
		 shared.h

ptraccel_debug_SOURCES = ptraccel-debug.c
ptraccel_debug_LDADD = ../src/libfilter.la ../src/libinput.la
ptraccel_debug_LDFLAGS = -no-install

libinput_list_devices_SOURCES = libinput-list-devices.c $(shared_sources)
libinput_list_devices_LDADD = ../src/libinput.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_list_devices_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS) \
			       -DTOOLS_BUILD_STANDALONE=1
dist_man1_MANS = libinput-list-devices.1

libinput_debug_events_SOURCES = libinput-debug-events.c $(shared_sources)
libinput_debug_events_LDADD = ../src/libinput.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_debug_events_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS) \
			       -DTOOLS_BUILD_STANDALONE=1
dist_man1_MANS += libinput-debug-events.1

libinput_SOURCES = \
		   libinput-tool.c \
		   libinput-tool.h \
		   libinput-debug-events.c \
		   libinput-list-devices.c \
		   $(shared_sources)
libinput_LDADD = ../src/libinput.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)
dist_man1_MANS += libinput.1

if BUILD_EVENTGUI
noinst_PROGRAMS += event-gui

event_gui_SOURCES = event-gui.c $(shared_sources)
event_gui_LDADD = ../src/libinput.la $(CAIRO_LIBS) $(GTK_LIBS) \
		  $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
event_gui_CFLAGS = $(CAIRO_CFLAGS) $(GTK_CFLAGS) \
		   $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS) $(AM_CFLAGS)
event_gui_LDFLAGS = -no-install
endif

EXTRA_DIST = make-ptraccel-graphs.sh
