udevdir=$(UDEV_DIR)
udev_PROGRAMS = libinput-device-group \
		libinput-model-quirks

litest_rules = 80-libinput-device-groups-litest.rules \
	       90-libinput-model-quirks-litest.rules
noinst_SCRIPTS = $(litest_rules)

libinput_device_group_SOURCES = libinput-device-group.c
libinput_device_group_CFLAGS = -I$(top_srcdir)/src \
			       $(LIBUDEV_CFLAGS) \
			       $(GCC_CFLAGS)
libinput_device_group_LDADD = $(LIBUDEV_LIBS)

if HAVE_LIBWACOM_GET_PAIRED_DEVICE
libinput_device_group_CFLAGS += $(LIBWACOM_CFLAGS)
libinput_device_group_LDADD += $(LIBWACOM_LIBS)
endif

libinput_model_quirks_SOURCES = libinput-model-quirks.c
libinput_model_quirks_CFLAGS = \
			       -I$(top_srcdir)/src \
			       $(LIBUDEV_CFLAGS) \
			       $(GCC_CFLAGS)
libinput_model_quirks_LDADD = $(LIBUDEV_LIBS)

udev_rulesdir=$(UDEV_DIR)/rules.d
dist_udev_rules_DATA = \
	80-libinput-device-groups.rules \
	90-libinput-model-quirks.rules

udev_hwdbdir=$(UDEV_DIR)/hwdb.d
dist_udev_hwdb_DATA = \
	90-libinput-model-quirks.hwdb

%-litest.rules: %.rules.in
	$(SED) -e "s|\@UDEV_TEST_PATH\@|$(abs_builddir)/|" < $^ > $@

CLEANFILES = $(litest_rules)
DISTCLEANFILES = \
		 80-libinput-device-groups.rules \
		 90-libinput-model-quirks.rules 
EXTRA_DIST = 80-libinput-test-device.rules
