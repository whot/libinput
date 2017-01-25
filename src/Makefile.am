lib_LTLIBRARIES = libinput.la
noinst_LTLIBRARIES = libinput-util.la \
		     libfilter.la

include_HEADERS =			\
	libinput.h

libinput_la_SOURCES =			\
	libinput.c			\
	libinput.h			\
	libinput-private.h		\
	evdev.c				\
	evdev.h				\
	evdev-lid.c			\
	evdev-middle-button.c		\
	evdev-mt-touchpad.c		\
	evdev-mt-touchpad.h		\
	evdev-mt-touchpad-tap.c		\
	evdev-mt-touchpad-buttons.c	\
	evdev-mt-touchpad-edge-scroll.c	\
	evdev-mt-touchpad-gestures.c	\
	evdev-tablet.c			\
	evdev-tablet.h			\
	evdev-tablet-pad.c		\
	evdev-tablet-pad.h		\
	evdev-tablet-pad-leds.c		\
	filter.c			\
	filter.h			\
	filter-private.h		\
	path-seat.h			\
	path-seat.c			\
	udev-seat.c			\
	udev-seat.h			\
	timer.c				\
	timer.h				\
	../include/linux/input.h

libinput_la_LIBADD = $(MTDEV_LIBS) \
		     $(LIBUDEV_LIBS) \
		     $(LIBEVDEV_LIBS) \
		     $(LIBWACOM_LIBS) \
		     libinput-util.la
libinput_la_LDFLAGS = $(GCOV_LDFLAGS) \
		      -version-info $(LIBINPUT_LT_VERSION) -shared \
		      -Wl,--version-script=$(srcdir)/libinput.sym

libinput_la_CFLAGS = -I$(top_srcdir)/include \
		     $(MTDEV_CFLAGS)	\
		     $(LIBUDEV_CFLAGS)	\
		     $(LIBEVDEV_CFLAGS)	\
		     $(LIBWACOM_CFLAGS) \
		     $(GCC_CFLAGS) \
		     $(GCOV_CFLAGS)
EXTRA_libinput_la_DEPENDENCIES = $(srcdir)/libinput.sym

libinput_util_la_SOURCES = \
	libinput-util.c		\
	libinput-util.h

libinput_util_la_LIBADD =
libinput_util_la_LDFLAGS = $(GCOV_LDFLAGS)
libinput_util_la_CFLAGS = -I$(top_srcdir)/include \
			  $(LIBUDEV_CFLAGS) \
			  $(GCC_CFLAGS) \
			  $(GCOV_CFLAGS)

libfilter_la_SOURCES = \
	filter.c \
	filter.h \
	filter-private.h
libfilter_la_LIBADD =
libfilter_la_CFLAGS =

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libinput.pc

AM_CFLAGS = $(GCC_CFLAGS)

DISTCLEANFILES = libinput-version.h
EXTRA_DIST = libinput-version.h.in libinput.sym
