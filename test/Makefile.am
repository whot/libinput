if BUILD_TESTS
AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src \
              $(CHECK_CFLAGS) \
              $(LIBEVDEV_CFLAGS) \
              $(LIBUDEV_CFLAGS)

AM_CFLAGS = $(GCC_CFLAGS)
AM_CXXFLAGS = $(GCC_CXXFLAGS)

TEST_LIBS = liblitest.la $(CHECK_LIBS) $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS) $(top_builddir)/src/libinput.la
noinst_LTLIBRARIES = liblitest.la
liblitest_la_SOURCES = \
	../src/libinput-util.h \
	../src/libinput-util.c \
	litest.h \
	litest-int.h \
	litest-alps-semi-mt.c \
	litest-bcm5974.c \
	litest-generic-singletouch.c \
	litest-keyboard.c \
	litest-mouse.c \
	litest-synaptics.c \
	litest-synaptics-st.c \
	litest-synaptics-t440.c \
	litest-trackpoint.c \
	litest-wacom-touch.c \
	litest.c

run_tests = \
	test-udev \
	test-path \
	test-pointer \
	test-touch \
	test-log \
	test-touchpad \
	test-misc \
	test-keyboard
build_tests = \
	test-build-cxx \
	test-build-linker \
	test-build-pedantic-c99 \
	test-build-std-gnuc90

noinst_PROGRAMS = $(build_tests) $(run_tests)
TESTS = $(run_tests)

.NOTPARALLEL:

test_udev_SOURCES = udev.c
test_udev_LDADD = $(TEST_LIBS)
test_udev_LDFLAGS = -no-install

test_path_SOURCES = path.c
test_path_LDADD = $(TEST_LIBS)
test_path_LDFLAGS = -no-install

test_pointer_SOURCES = pointer.c
test_pointer_LDADD = $(TEST_LIBS)
test_pointer_LDFLAGS = -no-install

test_touch_SOURCES = touch.c
test_touch_LDADD = $(TEST_LIBS)
test_touch_LDFLAGS = -no-install

test_log_SOURCES = log.c
test_log_LDADD = $(TEST_LIBS)
test_log_LDFLAGS = -no-install

test_touchpad_SOURCES = touchpad.c
test_touchpad_LDADD = $(TEST_LIBS)
test_touchpad_LDFLAGS = -no-install

test_misc_SOURCES = misc.c
test_misc_LDADD = $(TEST_LIBS)
test_misc_LDFLAGS = -no-install

test_keyboard_SOURCES = keyboard.c
test_keyboard_LDADD = $(TEST_LIBS)
test_keyboard_LDFLAGS = -no-install

# build-test only
test_build_pedantic_c99_SOURCES = build-pedantic.c
test_build_pedantic_c99_CFLAGS = -std=c99 -pedantic -Werror

test_build_std_gnuc90_SOURCES = build-pedantic.c
test_build_std_gnuc90_CFLAGS = -std=gnu90 -Werror

# test for linking with the minimal linker flags
test_build_linker_SOURCES = build-pedantic.c
test_build_linker_CFLAGS = -I$(top_srcdir)/src
test_build_linker_LDADD = $(top_builddir)/src/libinput.la

# test including from C++
test_build_cxx_SOURCES = build-cxx.cc
test_build_cxx_CXXFLAGS = -Wall -Wextra -Wno-unused-parameter $(AM_CXXFLAGS)

if HAVE_VALGRIND
VALGRIND_FLAGS=--leak-check=full \
	       --quiet \
	       --error-exitcode=3 \
	       --suppressions=$(srcdir)/valgrind.suppressions

valgrind:
	$(MAKE) check-TESTS LOG_COMPILER="$(VALGRIND)" LOG_FLAGS="$(VALGRIND_FLAGS)" CK_FORK=no

check: valgrind

endif
endif
EXTRA_DIST=valgrind.suppressions
