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
libshared_la_CFLAGS = $(AM_CFLAGS) $(LIBEVDEV_CFLAGS) $(LIBUDEV_CFLAGS) \
		      -DLIBINPUT_TOOL_PATH="\"@libexecdir@/libinput\""
libshared_la_LIBADD = $(LIBEVDEV_LIBS) $(LIBUDEV_LIBS) ../src/libinput.la

ptraccel_debug_SOURCES = ptraccel-debug.c
ptraccel_debug_LDADD = ../src/libfilter.la ../src/libinput.la
ptraccel_debug_LDFLAGS = -no-install

libinput_SOURCES = libinput-tool.c
libinput_LDADD = ../src/libinput.la libshared.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)
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

tools_PROGRAMS += libinput-measure
libinput_measure_SOURCES = libinput-measure.c $(shared_sources)
libinput_measure_LDADD = ../src/libinput.la libshared.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_measure_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS) \
		  -DLIBINPUT_TOOL_PATH="\"@libexecdir@/libinput\""
dist_man1_MANS += libinput-measure.1

tools_PROGRAMS += libinput-measure-touchpad-tap
libinput_measure_touchpad_tap_SOURCES = \
		     libinput-measure-touchpad-tap.c
libinput_measure_touchpad_tap_LDADD = ../src/libinput.la libshared.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
libinput_measure_touchpad_tap_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)
dist_man1_MANS += libinput-measure-touchpad-tap.1

if BUILD_DEBUG_GUI
tools_PROGRAMS += libinput-debug-gui
libinput_debug_gui_SOURCES = libinput-debug-gui.c
libinput_debug_gui_LDADD = ../src/libinput.la libshared.la $(CAIRO_LIBS) $(GTK_LIBS) $(LIBEVDEV_LIBS) $(LIBUDEV_LIBS)
libinput_debug_gui_CFLAGS = $(AM_CFLAGS) $(CAIRO_CFLAGS) $(GTK_CFLAGS) $(LIBEVDEV_CFLAGS) $(LIBUDEV_CFLAGS)
dist_man1_MANS += libinput-debug-gui.1
endif

EXTRA_DIST = make-ptraccel-graphs.sh install-compat-scripts.sh $(bin_SCRIPTS)

install-data-hook:
	(cd $(DESTDIR)$(bindir) && mv libinput-list-devices.compat libinput-list-devices)
	(cd $(DESTDIR)$(bindir) && mv libinput-debug-events.compat libinput-debug-events)

uninstall-local:
	rm -f $(DESTDIR)$(bindir)/libinput-list-devices
	rm -f $(DESTDIR)$(bindir)/libinput-debug-events

SUFFIXES = .man
.man.1: 
	$(AM_V_GEN)$(SED) -e 's|\@LIBINPUT_VERSION\@|$(PACKAGE_VERSION)|' < $< > $@
CLEANFILES = *.1
EXTRA_DIST += $(dist_man1_MANS:.1=.man)

