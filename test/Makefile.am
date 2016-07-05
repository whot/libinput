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
	litest.h \
	litest-int.h \
	litest-device-alps-semi-mt.c \
	litest-device-alps-dualpoint.c \
	litest-device-anker-mouse-kbd.c \
	litest-device-apple-internal-keyboard.c \
	litest-device-apple-magicmouse.c \
	litest-device-asus-rog-gladius.c \
	litest-device-atmel-hover.c \
	litest-device-bcm5974.c \
	litest-device-cyborg-rat-5.c \
	litest-device-elantech-touchpad.c \
	litest-device-generic-singletouch.c \
	litest-device-huion-pentablet.c \
	litest-device-keyboard.c \
	litest-device-keyboard-all-codes.c \
	litest-device-keyboard-razer-blackwidow.c \
	litest-device-logitech-trackball.c \
	litest-device-nexus4-touch-screen.c \
	litest-device-magic-trackpad.c \
	litest-device-mouse.c \
	litest-device-mouse-roccat.c \
	litest-device-mouse-low-dpi.c \
	litest-device-mouse-wheel-click-angle.c \
	litest-device-ms-surface-cover.c \
	litest-device-protocol-a-touch-screen.c \
	litest-device-qemu-usb-tablet.c \
	litest-device-synaptics.c \
	litest-device-synaptics-hover.c \
	litest-device-synaptics-i2c.c \
	litest-device-synaptics-st.c \
	litest-device-synaptics-t440.c \
	litest-device-synaptics-x1-carbon-3rd.c \
	litest-device-trackpoint.c \
	litest-device-touch-screen.c \
	litest-device-touchscreen-fuzz.c \
	litest-device-wacom-bamboo-tablet.c \
	litest-device-wacom-cintiq-tablet.c \
	litest-device-wacom-cintiq-24hd.c \
	litest-device-wacom-cintiq-24hdt-pad.c \
	litest-device-wacom-ekr.c \
	litest-device-wacom-intuos-tablet.c \
	litest-device-wacom-intuos3-pad.c \
	litest-device-wacom-intuos5-pad.c \
	litest-device-wacom-isdv4-tablet.c \
	litest-device-wacom-touch.c \
	litest-device-wacom-intuos-finger.c \
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

parallel_tests = \
	test-touchpad.test \
	test-touchpad-tap.test \
	test-touchpad-buttons.test \
	test-pad.test \
	test-tablet.test \
	test-gestures.test \
	test-pointer.test \
	test-touch.test \
	test-trackball.test \
	test-trackpoint.test \
	test-udev.test \
	test-path.test \
	test-log.test \
	test-misc.test \
	test-keyboard.test \
	test-litest-selftest.test

serial_tests = \
	       test-device.test

run_tests = \
	    $(parallel_tests) \
	    $(serial_tests)

build_tests = \
	test-build-cxx.test \
	test-build-linker.test \
	test-build-pedantic-c99.test \
	test-build-std-gnuc90.test

noinst_PROGRAMS = $(build_tests) $(run_tests)
noinst_SCRIPTS = symbols-leak-test
TESTS = $(run_tests) symbols-leak-test

test_udev_test_SOURCES = udev.c
test_udev_test_LDADD = $(TEST_LIBS)
test_udev_test_LDFLAGS = -no-install

test_path_test_SOURCES = path.c
test_path_test_LDADD = $(TEST_LIBS)
test_path_test_LDFLAGS = -no-install

test_pointer_test_SOURCES = pointer.c
test_pointer_test_LDADD = $(TEST_LIBS)
test_pointer_test_LDFLAGS = -no-install

test_touch_test_SOURCES = touch.c
test_touch_test_LDADD = $(TEST_LIBS)
test_touch_test_LDFLAGS = -no-install

test_log_test_SOURCES = log.c
test_log_test_LDADD = $(TEST_LIBS)
test_log_test_LDFLAGS = -no-install

test_tablet_test_SOURCES = tablet.c
test_tablet_test_LDADD = $(TEST_LIBS)
test_tablet_test_LDFLAGS = -static

test_pad_test_SOURCES = pad.c
test_pad_test_LDADD = $(TEST_LIBS)
test_pad_test_LDFLAGS = -static

test_touchpad_test_SOURCES = touchpad.c
test_touchpad_test_LDADD = $(TEST_LIBS)
test_touchpad_test_LDFLAGS = -no-install

test_touchpad_tap_test_SOURCES = touchpad-tap.c
test_touchpad_tap_test_LDADD = $(TEST_LIBS)
test_touchpad_tap_test_LDFLAGS = -no-install

test_touchpad_buttons_test_SOURCES = touchpad-buttons.c
test_touchpad_buttons_test_LDADD = $(TEST_LIBS)
test_touchpad_buttons_test_LDFLAGS = -no-install

test_trackpoint_test_SOURCES = trackpoint.c
test_trackpoint_test_LDADD = $(TEST_LIBS)
test_trackpoint_test_LDFLAGS = -no-install

test_trackball_test_SOURCES = trackball.c
test_trackball_test_LDADD = $(TEST_LIBS)
test_trackball_test_LDFLAGS = -no-install

test_misc_test_SOURCES = misc.c
test_misc_test_CFLAGS= $(AM_CFLAGS) -DLIBINPUT_LT_VERSION="\"$(LIBINPUT_LT_VERSION)\""
test_misc_test_LDADD = $(TEST_LIBS)
test_misc_test_LDFLAGS = -no-install

test_keyboard_test_SOURCES = keyboard.c
test_keyboard_test_LDADD = $(TEST_LIBS)
test_keyboard_test_LDFLAGS = -no-install

test_device_test_SOURCES = device.c
test_device_test_LDADD = $(TEST_LIBS)
test_device_test_LDFLAGS = -no-install

test_gestures_test_SOURCES = gestures.c
test_gestures_test_LDADD = $(TEST_LIBS)
test_gestures_test_LDFLAGS = -no-install

test_litest_selftest_test_SOURCES = litest-selftest.c litest.c litest-int.h litest.h
test_litest_selftest_test_CFLAGS = -DLITEST_DISABLE_BACKTRACE_LOGGING -DLITEST_NO_MAIN $(liblitest_la_CFLAGS)
test_litest_selftest_test_LDADD = $(TEST_LIBS)
test_litest_selftest_test_LDFLAGS = -no-install
if HAVE_LIBUNWIND
test_litest_selftest_test_LDADD += $(LIBUNWIND_LIBS) -ldl
test_litest_selftest_test_CFLAGS += $(LIBUNWIND_CFLAGS)
endif

# build-test only
test_build_pedantic_c99_test_SOURCES = build-pedantic.c
test_build_pedantic_c99_test_CFLAGS = -std=c99 -pedantic -Werror

test_build_std_gnuc90_test_SOURCES = build-pedantic.c
test_build_std_gnuc90_test_CFLAGS = -std=gnu90 -Werror

# test for linking with the minimal linker flags
test_build_linker_test_SOURCES = build-pedantic.c
test_build_linker_test_CFLAGS = -I$(top_srcdir)/src
test_build_linker_test_LDADD = $(top_builddir)/src/libinput.la $(top_builddir)/src/libinput-util.la

# test including from C++
test_build_cxx_test_SOURCES = build-cxx.cc
test_build_cxx_test_CXXFLAGS = -Wall -Wextra -Wno-unused-parameter $(AM_CXXFLAGS)

AM_TESTS_ENVIRONMENT= LITEST_VERBOSE=1; export LITEST_VERBOSE;

test-device.log: $(parallel_tests:.test=.log)

if HAVE_VALGRIND
VALGRIND_FLAGS=--leak-check=full \
	       --quiet \
	       --error-exitcode=3 \
	       --suppressions=$(srcdir)/valgrind.suppressions

valgrind: all
	$(MAKE) check-TESTS TEST_SUITE_LOG="test-suite-valgrind.log" LOG_COMPILER="$(VALGRIND)" LOG_FLAGS="$(VALGRIND_FLAGS)" CK_FORK=no USING_VALGRIND=yes

check: valgrind

endif
endif
EXTRA_DIST=valgrind.suppressions
