if BUILD_TESTS
AM_CPPFLAGS = -I$(top_srcdir)/src $(CHECK_CFLAGS) $(LIBEVDEV_CFLAGS)

TEST_LIBS = liblitest.la $(CHECK_LIBS) $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS) $(top_builddir)/src/libinput.la -lm
noinst_LTLIBRARIES = liblitest.la
liblitest_la_SOURCES = \
	$(top_srcdir)/src/libinput-util.h \
	$(top_srcdir)/src/libinput-util.c \
	litest.h \
	litest-int.h \
	litest-synaptics.c \
	litest-synaptics.h \
	litest.c

run_tests = test-udev
build_tests = test-build-pedantic-c99 test-build-std-gnuc90

noinst_PROGRAMS = $(build_tests) $(run_tests)
TESTS = $(run_tests)

test_udev_SOURCES = udev.c
test_udev_CFLAGS = $(AM_CPPFLAGS)
test_udev_LDADD = $(TEST_LIBS)
test_udev_LDFLAGS = -static

# build-test only
test_build_pedantic_c99_SOURCES = build-pedantic.c
test_build_pedantic_c99_CFLAGS = $(AM_CPPFLAGS) -std=c99 -pedantic -Werror

test_build_std_gnuc90_SOURCES = build-pedantic.c
test_build_std_gnuc90_CFLAGS = $(AM_CPPFLAGS) -std=gnu90 -Werror

endif
