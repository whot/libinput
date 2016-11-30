project('libinput', 'c', 'cpp',
	version : '1.5.2',
	license: 'MIT/Expat')

libinput_version = meson.project_version().split('.')
libinput_lt_version = '20.3.10'

cc = meson.get_compiler('c')
cppflags = ['-Wall', '-Wextra', '-Wno-unused-parameter', '-g', '-fvisibility=hidden']
cflags = cppflags + ['-Wmissing-prototypes', '-Wstrict-prototypes']
add_global_arguments(cflags,
		     language: 'c')
add_global_arguments(cppflags,
		     language: 'cpp')

pkgconfig = import('pkgconfig')
config_h = configuration_data()
config_h.set('_GNU_SOURCE', '1')

dep_udev = dependency('libudev')
dep_mtdev = dependency('mtdev', version: '>= 1.1.0')
dep_libevdev = dependency('libevdev', version: '>= 0.4')
dep_lm = cc.find_library('m')
dep_rt = cc.find_library('rt')

############ libwacom configuration ############

if get_option('libwacom')
	dep_libwacom = dependency('libwacom', version : '>= 0.12')

	code = '''
	#include <libwacom/libwacom.h>
	int main(void) { libwacom_get_paired_device(NULL); }
	'''
	result = cc.links(code,
			  args : '-lwacom',
			  name : 'libwacom_get_paired_device check',
			  dependencies : dep_libwacom)
	if result
		config_h.set('HAVE_LIBWACOM_GET_PAIRED_DEVICE', '1')
	endif
endif

############ udev bits ############

udev_dir = get_option('udev-dir')
if udev_dir == ''
	udev_dir = '@0@/udev'.format(get_option('libdir'))
endif
udev_rules_dir = '@0@/rules.d'.format(udev_dir)
udev_hwdb_dir = '@0@/hwdb.d'.format(udev_dir)

executable('libinput-device-group',
	   'udev/libinput-device-group.c',
	   dependencies : [dep_udev, dep_libwacom],
	   include_directories : include_directories('src'),
	   install : true,
	   install_dir : udev_dir)
executable('libinput-model-quirks',
	   'udev/libinput-model-quirks.c',
	   dependencies : dep_udev,
	   include_directories : include_directories('src'),
	   install : true,
	   install_dir : udev_dir)
install_data('udev/90-libinput-model-quirks.hwdb',
	     install_dir : udev_hwdb_dir)

udev_rules_config = configuration_data()
udev_rules_config.set('UDEV_TEST_PATH', '')
configure_file(input : 'udev/80-libinput-device-groups.rules.in',
	       output : '80-libinput-device-groups.rules',
	       install : true,
	       install_dir : udev_rules_dir,
	       configuration : udev_rules_config)
configure_file(input : 'udev/90-libinput-model-quirks.rules.in',
	       output : '90-libinput-model-quirks.rules',
	       install : true,
	       install_dir : udev_rules_dir,
	       configuration : udev_rules_config)

litest_udev_rules_config = configuration_data()
litest_udev_rules_config.set('UDEV_TEST_PATH', meson.build_root())
litest_groups_rules_file = configure_file(input : 'udev/80-libinput-device-groups.rules.in',
	       output : '80-libinput-device-groups-litest.rules',
	       install : false,
	       configuration : litest_udev_rules_config)
litest_model_quirks_file = configure_file(input : 'udev/90-libinput-model-quirks.rules.in',
	       output : '90-libinput-model-quirks-litest.rules',
	       install : false,
	       configuration : litest_udev_rules_config)

# FIXME: missing parse_hwdb.py test

############ libinput-util.a ############
src_libinput_util = [
		'src/libinput-util.c',
		'src/libinput-util.h'
]
libinput_util = static_library('libinput-util',
			       src_libinput_util,
			       dependencies : dep_udev)
dep_libinput_util = declare_dependency(link_with: libinput_util)

############ libfilter.a ############
src_libfilter = [
		'src/filter.c',
		'src/filter.h',
		'src/filter-private.h'
]
libfilter = static_library('filter', src_libfilter)
dep_libfilter = declare_dependency(link_with: libfilter)

############ libinput.so ############
install_headers('src/libinput.h')
src_libinput = [
	'src/libinput.c',
	'src/libinput.h',
	'src/libinput-private.h',
	'src/evdev.c',
	'src/evdev.h',
	'src/evdev-middle-button.c',
	'src/evdev-mt-touchpad.c',
	'src/evdev-mt-touchpad.h',
	'src/evdev-mt-touchpad-tap.c',
	'src/evdev-mt-touchpad-buttons.c',
	'src/evdev-mt-touchpad-edge-scroll.c',
	'src/evdev-mt-touchpad-gestures.c',
	'src/evdev-tablet.c',
	'src/evdev-tablet.h',
	'src/evdev-tablet-pad.c',
	'src/evdev-tablet-pad.h',
	'src/evdev-tablet-pad-leds.c',
	'src/filter.c',
	'src/filter.h',
	'src/filter-private.h',
	'src/path.h',
	'src/path.c',
	'src/udev-seat.c',
	'src/udev-seat.h',
	'src/timer.c',
	'src/timer.h',
	'include/linux/input.h'
]
deps_libinput = [
	dep_mtdev,
	dep_udev,
	dep_libevdev,
	dep_lm,
	dep_rt,
	dep_libwacom,
	dep_libinput_util
]

libinput_version_h_config = configuration_data()
libinput_version_h_config.set('LIBINPUT_VERSION_MAJOR', libinput_version[0])
libinput_version_h_config.set('LIBINPUT_VERSION_MINOR', libinput_version[1])
libinput_version_h_config.set('LIBINPUT_VERSION_MICRO', libinput_version[2])
libinput_version_h_config.set('LIBINPUT_VERSION', meson.project_version())

libinput_version_h = configure_file(
		input : 'src/libinput-version.h.in',
		output : 'libinput-version.h',
		configuration : libinput_version_h_config,
		install: false,
)

mapfile = 'src/libinput.sym'
version_flag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)
lib_libinput = shared_library('input',
		src_libinput,
		include_directories : include_directories('.'),
		dependencies : deps_libinput,
		version : libinput_lt_version,
		link_args : version_flag,
		link_depends : mapfile
		)

dep_libinput = declare_dependency(
		link_with : lib_libinput,
		dependencies : deps_libinput)

pkgconfig.generate(
	filebase: 'libinput',
	name: 'Libinput',
	description: 'Input device library',
	version: meson.project_version(),
	libraries: lib_libinput
)

############ documentation ############

doxygen = find_program('doxygen',
		       required : false)
if doxygen.found()
	doxygen_version_cmd = run_command(doxygen.path(), '--version')
	if doxygen_version_cmd.returncode() != 0
		error('Command "doxygen --version" failed.')
	endif
	doxygen_version = doxygen_version_cmd.stdout()
	if doxygen_version.version_compare('< 1.6.0')
		error('doxygen needs to be at least version 1.6.0 (have @0@)'.format(doxygen_version))
	endif
	dot = find_program('dot')
	grep = find_program('grep')
	dot_version_cmd = run_command(dot.path(), '-V')
	if dot_version_cmd.returncode() != 0
		error('Command "dot -V" failed.')
	endif
	# dot -V output is (to stderr):
	# 	dot - graphviz version 2.38.0 (20140413.2041)
	dot_version = dot_version_cmd.stderr().split(' ')
	if dot_version[4].version_compare('< 2.26')
		error('Graphviz dot needs to be at least version 2.26 (have @0@)'.format(dot_version[4]))
	endif

	# FIXME: need to actually build the docs
endif

############ tools ############
tools_shared_sources = [ 'tools/shared.c',
			 'tools/shared.h' ]
event_debug_sources = [ 'tools/event-debug.c' ] + tools_shared_sources
executable('event-debug',
	   event_debug_sources,
	   dependencies : dep_libinput,
	   include_directories : include_directories('src'),
	   install : false
	   )

libinput_debug_events_sources = event_debug_sources
executable('libinput-debug-events',
	   libinput_debug_events_sources,
	   dependencies : dep_libinput,
	   include_directories : include_directories('src'),
	   install : true
	   )

libinput_list_devices_sources = [ 'tools/libinput-list-devices.c' ] + tools_shared_sources
executable('libinput-list-devices',
	   libinput_list_devices_sources,
	   dependencies : [ dep_libinput ],
	   include_directories : include_directories('src'),
	   install : true
	   )

ptraccel_debug_sources = [ 'tools/ptraccel-debug.c' ]
executable('ptraccel-debug',
	   ptraccel_debug_sources,
	   dependencies : [ dep_libfilter, dep_libinput ],
	   include_directories : include_directories('src'),
	   install : false
	   )

if get_option('event-gui')
	dep_gtk = dependency('gtk+-3.0')
	dep_cairo = dependency('cairo')
	dep_glib = dependency('glib-2.0')

	event_gui_sources = [ 'tools/event-gui.c' ] + tools_shared_sources
	deps_event_gui = [
			dep_gtk,
			dep_cairo,
			dep_glib,
			dep_libevdev,
			dep_libinput
			]
	executable('event-gui',
		   event_gui_sources,
		   dependencies : deps_event_gui,
		   include_directories : include_directories('src'),
		   install : false
		   )
endif

install_man('tools/libinput-list-devices.1',
	    'tools/libinput-debug-events.1')


############ tests ############

if get_option('enable-tests')
	dep_check = dependency('check', version : '>= 0.9.10')
	valgrind = find_program('valgrind')
	addr2line = find_program('addr2line')

	if addr2line.found()
		config_h.set('HAVE_ADDR2LINE', '1')
		config_h.set('ADDR2LINE', '"@0@"'.format(addr2line.path()[0]))
	endif

	dep_libunwind = dependency('libunwind', required: false)
	if dep_libunwind.found()
		config_h.set('HAVE_LIBUNWIND', '1')
	endif


	lib_litest_sources = [
		'test/litest.h',
		'test/litest-int.h',
		'test/litest-device-alps-semi-mt.c',
		'test/litest-device-alps-dualpoint.c',
		'test/litest-device-anker-mouse-kbd.c',
		'test/litest-device-apple-internal-keyboard.c',
		'test/litest-device-apple-magicmouse.c',
		'test/litest-device-asus-rog-gladius.c',
		'test/litest-device-atmel-hover.c',
		'test/litest-device-bcm5974.c',
		'test/litest-device-calibrated-touchscreen.c',
		'test/litest-device-cyborg-rat-5.c',
		'test/litest-device-elantech-touchpad.c',
		'test/litest-device-generic-singletouch.c',
		'test/litest-device-huion-pentablet.c',
		'test/litest-device-keyboard.c',
		'test/litest-device-keyboard-all-codes.c',
		'test/litest-device-keyboard-razer-blackwidow.c',
		'test/litest-device-logitech-trackball.c',
		'test/litest-device-nexus4-touch-screen.c',
		'test/litest-device-magic-trackpad.c',
		'test/litest-device-mouse.c',
		'test/litest-device-mouse-roccat.c',
		'test/litest-device-mouse-low-dpi.c',
		'test/litest-device-mouse-wheel-click-angle.c',
		'test/litest-device-mouse-wheel-click-count.c',
		'test/litest-device-ms-surface-cover.c',
		'test/litest-device-protocol-a-touch-screen.c',
		'test/litest-device-qemu-usb-tablet.c',
		'test/litest-device-synaptics.c',
		'test/litest-device-synaptics-hover.c',
		'test/litest-device-synaptics-i2c.c',
		'test/litest-device-synaptics-st.c',
		'test/litest-device-synaptics-t440.c',
		'test/litest-device-synaptics-x1-carbon-3rd.c',
		'test/litest-device-trackpoint.c',
		'test/litest-device-touch-screen.c',
		'test/litest-device-touchscreen-fuzz.c',
		'test/litest-device-wacom-bamboo-tablet.c',
		'test/litest-device-wacom-cintiq-tablet.c',
		'test/litest-device-wacom-cintiq-13hdt-finger.c',
		'test/litest-device-wacom-cintiq-13hdt-pad.c',
		'test/litest-device-wacom-cintiq-13hdt-pen.c',
		'test/litest-device-wacom-cintiq-24hd.c',
		'test/litest-device-wacom-cintiq-24hdt-pad.c',
		'test/litest-device-wacom-ekr.c',
		'test/litest-device-wacom-hid4800-pen.c',
		'test/litest-device-wacom-intuos-tablet.c',
		'test/litest-device-wacom-intuos3-pad.c',
		'test/litest-device-wacom-intuos5-pad.c',
		'test/litest-device-wacom-isdv4-tablet.c',
		'test/litest-device-wacom-touch.c',
		'test/litest-device-wacom-intuos-finger.c',
		'test/litest-device-waltop-tablet.c',
		'test/litest-device-wheel-only.c',
		'test/litest-device-xen-virtual-pointer.c',
		'test/litest-device-vmware-virtual-usb-mouse.c',
		'test/litest-device-yubikey.c',
		'test/litest.c'
	]

	dep_dl = cc.find_library('dl')
	deps_litest = [
		dep_libinput,
		dep_check,
		dep_libunwind,
		dep_udev,
		dep_libevdev,
		dep_dl,
		dep_lm
	]
	def_quirks_rules_file = '-DLIBINPUT_MODEL_QUIRKS_UDEV_RULES_FILE="@0@"'.format(litest_model_quirks_file)
	def_quirks_hwdb_file = '-DLIBINPUT_MODEL_QUIRKS_UDEV_HWDB_FILE="@0@/udev/90-libinput-model-quirks.hwdb"'.format(meson.source_root())
	def_test_rules_file = '-DLIBINPUT_TEST_DEVICE_RULES_FILE="@0@/udev/80-libinput-test-device.rules"'.format(meson.source_root())
	defs_litest = [def_quirks_rules_file,
		       def_quirks_hwdb_file,
		       def_test_rules_file]

	lib_litest = static_library('litest',
				    lib_litest_sources,
				    include_directories : include_directories('src'),
				    dependencies : deps_litest,
				    c_args : defs_litest)
	dep_litest = declare_dependency(link_with : lib_litest,
					dependencies : deps_litest)

	libinput_test_runner_sources = [
		'test/udev.c',
		'test/path.c',
		'test/pointer.c',
		'test/touch.c',
		'test/log.c',
		'test/tablet.c',
		'test/pad.c',
		'test/touchpad.c',
		'test/touchpad-tap.c',
		'test/touchpad-buttons.c',
		'test/trackpoint.c',
		'test/trackball.c',
		'test/misc.c',
		'test/keyboard.c',
		'test/device.c',
		'test/gestures.c'
	]
	def_LT_VERSION = '-DLIBINPUT_LT_VERSION="@0@"'.format(libinput_lt_version)
	libinput_test_runner = executable('libinput-test-suite-runner',
					  libinput_test_runner_sources,
					  include_directories : include_directories('src'),
					  dependencies : dep_litest,
					  c_args : [ def_LT_VERSION ],
					  install : false)
	test('libinput-test-suite-runner',
	     libinput_test_runner,
	     env : [ 'LITEST_VERBOSE=1' ])

	def_no_main = '-DLITEST_NO_MAIN'
	def_disable_backtrace = '-DLITEST_DISABLE_BACKTRACE_LOGGING'
	defs_litest_selftest = [
		def_no_main,
		def_disable_backtrace
	] + defs_litest
	test_litest_selftest_sources = [
		'test/litest-selftest.c',
		'test/litest.c',
		'test/litest-int.h',
		'test/litest.h'
	]
	deps_litest_selftest = [dep_litest]
	test_litest_selftest = executable('test-litest-selftest',
					  test_litest_selftest_sources,
					  include_directories : include_directories('src'),
					  dependencies : deps_litest_selftest,
					  c_args : defs_litest_selftest,
					  install : false)
	test('test-litest-selftest', test_litest_selftest)
	# build-test only
        executable('test-build-pedantic',
		   'test/build-pedantic.c',
		   include_directories : include_directories('src'),
		   c_args : ['-std=c99', '-pedantic', '-Werror'],
		   install : false)
	# build-test only
        executable('test-build-std-gnuc90',
		   'test/build-pedantic.c',
		   include_directories : include_directories('src'),
		   c_args : ['-std=gnu90', '-Werror'],
		   install : false)
	# test for linking with the minimal linker flags
        executable('test-build-linker',
		   'test/build-pedantic.c',
		   include_directories : include_directories('src'),
		   dependencies : [ dep_libinput, dep_libinput_util ],
		   install : false)
	# test including from C++
	executable('test-build-cxx',
		   'test/build-cxx.cc',
		   include_directories : include_directories('src'),
		   install : false)
endif
############ output files ############
configure_file(output: 'config.h', install: false, configuration: config_h)
