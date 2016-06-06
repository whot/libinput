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

run_tests = \
	test-touchpad \
	test-touchpad-tap \
	test-touchpad-buttons \
	test-pad \
	test-tablet \
	test-device \
	test-gestures \
	test-pointer \
	test-touch \
	test-trackball \
	test-trackpoint \
	test-udev \
	test-path \
	test-log \
	test-misc \
	test-keyboard \
	test-litest-selftest

build_tests = \
	test-build-cxx \
	test-build-linker \
	test-build-pedantic-c99 \
	test-build-std-gnuc90

noinst_PROGRAMS = $(build_tests) $(run_tests)
noinst_SCRIPTS = symbols-leak-test
TESTS = $(run_tests) symbols-leak-test

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

test_tablet_SOURCES = tablet.c
test_tablet_LDADD = $(TEST_LIBS)
test_tablet_LDFLAGS = -static

test_pad_SOURCES = pad.c
test_pad_LDADD = $(TEST_LIBS)
test_pad_LDFLAGS = -static

test_touchpad_SOURCES = touchpad.c
test_touchpad_LDADD = $(TEST_LIBS)
test_touchpad_LDFLAGS = -no-install

test_touchpad_tap_SOURCES = touchpad-tap.c
test_touchpad_tap_LDADD = $(TEST_LIBS)
test_touchpad_tap_LDFLAGS = -no-install

test_touchpad_buttons_SOURCES = touchpad-buttons.c
test_touchpad_buttons_LDADD = $(TEST_LIBS)
test_touchpad_buttons_LDFLAGS = -no-install

test_trackpoint_SOURCES = trackpoint.c
test_trackpoint_LDADD = $(TEST_LIBS)
test_trackpoint_LDFLAGS = -no-install

test_trackball_SOURCES = trackball.c
test_trackball_LDADD = $(TEST_LIBS)
test_trackball_LDFLAGS = -no-install

test_misc_SOURCES = misc.c
test_misc_CFLAGS= $(AM_CFLAGS) -DLIBINPUT_LT_VERSION="\"$(LIBINPUT_LT_VERSION)\""
test_misc_LDADD = $(TEST_LIBS)
test_misc_LDFLAGS = -no-install

test_keyboard_SOURCES = keyboard.c
test_keyboard_LDADD = $(TEST_LIBS)
test_keyboard_LDFLAGS = -no-install

test_device_SOURCES = device.c
test_device_LDADD = $(TEST_LIBS)
test_device_LDFLAGS = -no-install

test_gestures_SOURCES = gestures.c
test_gestures_LDADD = $(TEST_LIBS)
test_gestures_LDFLAGS = -no-install

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

AM_TESTS_ENVIRONMENT= LITEST_VERBOSE=1; export LITEST_VERBOSE;

if HAVE_VALGRIND
VALGRIND_FLAGS=--leak-check=full \
	       --quiet \
	       --error-exitcode=3 \
	       --suppressions=$(srcdir)/valgrind.suppressions

valgrind: all
	$(MAKE) check-TESTS LOG_COMPILER="$(VALGRIND)" LOG_FLAGS="$(VALGRIND_FLAGS)" CK_FORK=no USING_VALGRIND=yes

check: valgrind

endif
endif
EXTRA_DIST=valgrind.suppressions
