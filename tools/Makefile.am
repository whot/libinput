noinst_PROGRAMS = event-debug

AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src

event_debug_SOURCES = event-debug.c
event_debug_LDADD = ../src/libinput.la $(LIBUDEV_LIBS)
event_debug_LDFLAGS = -static
event_debug_CFLAGS = $(LIBUDEV_CFLAGS)

if BUILD_EVENTGUI
noinst_PROGRAMS += event-gui
bin_PROGRAMS = libinput-userstudy

event_gui_SOURCES = event-gui.c
event_gui_LDADD = ../src/libinput.la $(CAIRO_LIBS) $(GTK_LIBS) $(LIBUDEV_LIBS)
event_gui_CFLAGS = $(CAIRO_CFLAGS) $(GTK_CFLAGS) $(LIBUDEV_CFLAGS)
event_gui_LDFLAGS = -static

libinput_userstudy_SOURCES = userstudy.c
libinput_userstudy_LDADD = ../src/libinput.la $(CAIRO_LIBS) $(GTK_LIBS) $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_userstudy_CFLAGS = $(CAIRO_CFLAGS) $(GTK_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)
libinput_userstudy_LDFLAGS = -static
endif
