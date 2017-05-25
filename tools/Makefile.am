noinst_PROGRAMS = ptraccel-debug
bin_PROGRAMS = libinput
toolsdir = $(libexecdir)/libinput
tools_PROGRAMS =
bin_SCRIPTS = libinput-list-devices.compat libinput-debug-events.compat
noinst_LTLIBRARIES = libshared.la

AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src \
              -I$(top_builddir)/src # for libinput-version.h
AM_CFLAGS = $(GCC_CFLAGS)
AM_CXXFLAGS = $(GCC_CXXFLAGS)

libshared_la_SOURCES = \
		       shared.c \
		       shared.h
libshared_la_CFLAGS = $(AM_CFLAGS) $(LIBEVDEV_CFLAGS)
libshared_la_LIBADD = $(LIBEVDEV_LIBS)

ptraccel_debug_SOURCES = ptraccel-debug.c
ptraccel_debug_LDADD = ../src/libfilter.la ../src/libinput.la
ptraccel_debug_LDFLAGS = -no-install

libinput_SOURCES = \
		   libinput-tool.c \
		   libinput-tool.h
libinput_LDADD = ../src/libinput.la libshared.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS) \
		  -DLIBINPUT_TOOL_PATH="\"@libexecdir@/libinput\""
dist_man1_MANS = libinput.1

tools_PROGRAMS += libinput-list-devices
libinput_list_devices_SOURCES = libinput-list-devices.c $(shared_sources)
libinput_list_devices_LDADD = ../src/libinput.la libshared.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_list_devices_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)
dist_man1_MANS += libinput-list-devices.1

tools_PROGRAMS += libinput-debug-events
libinput_debug_events_SOURCES = libinput-debug-events.c $(shared_sources)
libinput_debug_events_LDADD = ../src/libinput.la libshared.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_debug_events_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)
dist_man1_MANS += libinput-debug-events.1

if BUILD_DEBUG_GUI
tools_PROGRAMS += libinput-debug-gui
libinput_debug_gui_SOURCES = libinput-debug-gui.c
libinput_debug_gui_LDADD = ../src/libinput.la libshared.la $(CAIRO_LIBS) $(GTK_LIBS) $(LIBEVDEV_LIBS)
libinput_debug_gui_CFLAGS = $(AM_CFLAGS) $(CAIRO_CFLAGS) $(GTK_CFLAGS) $(LIBEVDEV_CFLAGS)
dist_man1_MANS += libinput-debug-gui.1
endif

EXTRA_DIST = make-ptraccel-graphs.sh $(bin_SCRIPTS)

install-data-hook:
	(cd $(DESTDIR)$(bindir) && mv libinput-list-devices.compat libinput-list-devices)
	(cd $(DESTDIR)$(bindir) && mv libinput-debug-events.compat libinput-debug-events)

uninstall-local:
	rm -f $(DESTDIR)$(bindir)/libinput-list-devices
	rm -f $(DESTDIR)$(bindir)/libinput-debug-events
