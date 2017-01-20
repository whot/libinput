if BUILD_TESTS
AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src \
              $(CHECK_CFLAGS) \
              $(LIBEVDEV_CFLAGS) \
              $(LIBUDEV_CFLAGS) \
              -I$(top_builddir)/src # for libinput-version.h

AM_CFLAGS = $(GCC_CFLAGS) $(GCOV_CFLAGS)
AM_CXXFLAGS = $(GCC_CXXFLAGS)

TEST_LIBS = liblitest.la $(CHECK_LIBS) $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS) $(top_builddir)/src/libinput.la
noinst_LTLIBRARIES = liblitest.la
liblitest_la_SOURCES = \
	litest.h \
	litest-int.h \
	litest-device-acer-hawaii-keyboard.c \
	litest-device-acer-hawaii-touchpad.c \
	litest-device-alps-semi-mt.c \
	litest-device-alps-dualpoint.c \
	litest-device-anker-mouse-kbd.c \
	litest-device-apple-internal-keyboard.c \
	litest-device-apple-magicmouse.c \
	litest-device-asus-rog-gladius.c \
	litest-device-atmel-hover.c \
	litest-device-bcm5974.c \
	litest-device-calibrated-touchscreen.c \
	litest-device-cyborg-rat-5.c \
	litest-device-elantech-touchpad.c \
	litest-device-generic-singletouch.c \
	litest-device-huion-pentablet.c \
	litest-device-keyboard.c \
	litest-device-keyboard-all-codes.c \
	litest-device-keyboard-razer-blackwidow.c \
	litest-device-lid-switch.c \
	litest-device-logitech-trackball.c \
	litest-device-nexus4-touch-screen.c \
	litest-device-magic-trackpad.c \
	litest-device-mouse.c \
	litest-device-mouse-wheel-tilt.c \
	litest-device-mouse-roccat.c \
	litest-device-mouse-low-dpi.c \
	litest-device-mouse-wheel-click-angle.c \
	litest-device-mouse-wheel-click-count.c \
	litest-device-ms-surface-cover.c \
	litest-device-protocol-a-touch-screen.c \
	litest-device-qemu-usb-tablet.c \
	litest-device-synaptics.c \
	litest-device-synaptics-hover.c \
	litest-device-synaptics-i2c.c \
	litest-device-synaptics-rmi4.c \
	litest-device-synaptics-st.c \
	litest-device-synaptics-t440.c \
	litest-device-synaptics-x1-carbon-3rd.c \
	litest-device-trackpoint.c \
	litest-device-touch-screen.c \
	litest-device-touchscreen-fuzz.c \
	litest-device-wacom-bamboo-16fg-pen.c \
	litest-device-wacom-cintiq-12wx-pen.c \
	litest-device-wacom-cintiq-13hdt-finger.c \
	litest-device-wacom-cintiq-13hdt-pad.c \
	litest-device-wacom-cintiq-13hdt-pen.c \
	litest-device-wacom-cintiq-24hd-pen.c \
	litest-device-wacom-cintiq-24hdt-pad.c \
	litest-device-wacom-ekr.c \
	litest-device-wacom-hid4800-pen.c \
	litest-device-wacom-intuos3-pad.c \
	litest-device-wacom-intuos5-finger.c \
	litest-device-wacom-intuos5-pad.c \
	litest-device-wacom-intuos5-pen.c \
	litest-device-wacom-isdv4-e6-pen.c \
	litest-device-wacom-isdv4-e6-finger.c \
	litest-device-waltop-tablet.c \
	litest-device-wheel-only.c \
	litest-device-xen-virtual-pointer.c \
	litest-device-vmware-virtual-usb-mouse.c \
	litest-device-yubikey.c \
	litest.c
liblitest_la_LIBADD = $(top_builddir)/src/libinput-util.la
liblitest_la_CFLAGS = $(AM_CFLAGS) \
	      -DLIBINPUT_MODEL_QUIRKS_UDEV_RULES_FILE="\"$(abs_top_builddir)/udev/90-libinput-model-quirks-litest.rules\"" \
	      -DLIBINPUT_MODEL_QUIRKS_UDEV_HWDB_FILE="\"$(abs_top_srcdir)/udev/90-libinput-model-quirks.hwdb\"" \
	      -DLIBINPUT_TEST_DEVICE_RULES_FILE="\"$(abs_top_srcdir)/udev/80-libinput-test-device.rules\""
if HAVE_LIBUNWIND
liblitest_la_LIBADD += $(LIBUNWIND_LIBS) -ldl
liblitest_la_CFLAGS += $(LIBUNWIND_CFLAGS)
endif

run_tests = \
	    test-litest-selftest \
	    libinput-test-suite-runner

build_tests = \
	test-build-cxx \
	test-build-linker \
	test-build-pedantic-c99 \
	test-build-std-gnuc90

noinst_PROGRAMS = $(build_tests) $(run_tests)
noinst_SCRIPTS = symbols-leak-test

if RUN_TESTS
TESTS = $(run_tests) symbols-leak-test
endif

libinput_test_suite_runner_SOURCES = test-udev.c \
				     test-path.c \
				     test-pointer.c \
				     test-touch.c \
				     test-log.c \
				     test-tablet.c \
				     test-pad.c \
				     test-touchpad.c \
				     test-touchpad-tap.c \
				     test-touchpad-buttons.c \
				     test-trackpoint.c \
				     test-trackball.c \
				     test-misc.c \
				     test-keyboard.c \
				     test-device.c \
				     test-gestures.c \
				     test-lid.c

libinput_test_suite_runner_CFLAGS = $(AM_CFLAGS) -DLIBINPUT_LT_VERSION="\"$(LIBINPUT_LT_VERSION)\""
libinput_test_suite_runner_LDADD = $(TEST_LIBS)
libinput_test_suite_runner_LDFLAGS = -no-install

test_litest_selftest_SOURCES = litest-selftest.c litest.c litest-int.h litest.h
test_litest_selftest_CFLAGS = -DLITEST_DISABLE_BACKTRACE_LOGGING -DLITEST_NO_MAIN $(liblitest_la_CFLAGS)
test_litest_selftest_LDADD = $(TEST_LIBS)
test_litest_selftest_LDFLAGS = -no-install
if HAVE_LIBUNWIND
test_litest_selftest_LDADD += $(LIBUNWIND_LIBS) -ldl
test_litest_selftest_CFLAGS += $(LIBUNWIND_CFLAGS)
endif

# build-test only
test_build_pedantic_c99_SOURCES = build-pedantic.c
test_build_pedantic_c99_CFLAGS = -std=c99 -pedantic -Werror

test_build_std_gnuc90_SOURCES = build-pedantic.c
test_build_std_gnuc90_CFLAGS = -std=gnu90 -Werror

# test for linking with the minimal linker flags
test_build_linker_SOURCES = build-pedantic.c
test_build_linker_CFLAGS = -I$(top_srcdir)/src
test_build_linker_LDADD = $(top_builddir)/src/libinput.la $(top_builddir)/src/libinput-util.la

# test including from C++
test_build_cxx_SOURCES = build-cxx.cc
test_build_cxx_CXXFLAGS = -Wall -Wextra -Wno-unused-parameter $(AM_CXXFLAGS)

if HAVE_VALGRIND
VALGRIND_FLAGS=--leak-check=full \
	       --quiet \
	       --error-exitcode=3 \
	       --suppressions=$(srcdir)/valgrind.suppressions

valgrind: check-am
	$(MAKE) check-TESTS TEST_SUITE_LOG="test-suite-valgrind.log" LOG_COMPILER="$(VALGRIND)" LOG_FLAGS="$(VALGRIND_FLAGS)" CK_FORK=no USING_VALGRIND=yes

check: valgrind

DISTCLEANFILES=test-suite-valgrind.log

endif
endif
EXTRA_DIST=valgrind.suppressions

if GCOV_ENABLED

CLEANFILES = gcov-reports/*.gcov gcov-reports/summary.txt *.gcno *.gcda

gcov: generate-gcov-report.sh check-TESTS
	$(AM_V_GEN)$(srcdir)/generate-gcov-report.sh gcov-reports $(top_builddir)/src $(builddir)
else
gcov:
	@echo "Run ./configure --enable-gcov to produce gcov reports" && false
endif
