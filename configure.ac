AC_PREREQ([2.64])

m4_define([libinput_major_version], [1])
m4_define([libinput_minor_version], [7])
m4_define([libinput_micro_version], [902])
m4_define([libinput_version],
          [libinput_major_version.libinput_minor_version.libinput_micro_version])

AC_INIT([libinput],
        [libinput_version],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=Wayland&component=libinput&version=libinput_version],
        [libinput],
        [http://www.freedesktop.org/wiki/Software/libinput/])

AC_SUBST([LIBINPUT_VERSION_MAJOR], [libinput_major_version])
AC_SUBST([LIBINPUT_VERSION_MINOR], [libinput_minor_version])
AC_SUBST([LIBINPUT_VERSION_MICRO], [libinput_micro_version])
AC_SUBST([LIBINPUT_VERSION], [libinput_version])

AC_DEFINE([LIBINPUT_VERSION_MAJOR], [libinput_major_version], "libinput major version number")
AC_DEFINE([LIBINPUT_VERSION_MINOR], [libinput_minor_version], "libinput minor version number")
AC_DEFINE([LIBINPUT_VERSION_MICRO], [libinput_micro_version], "libinput micro version number")

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([.])

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz])

# Before making a release, the LIBINPUT_LT_VERSION string should be
# modified.
# The string is of the form C:R:A.
# a) If binary compatibility has been broken (eg removed or changed interfaces)
#    change to C+1:0:0. DO NOT DO THIS! Use symbol versioning instead and
#    do b) instead.
# b) If interfaces have been changed or added, but binary compatibility has
#    been preserved, change to C+1:0:A+1
# c) If the interface is the same as the previous version, change to C:R+1:A
LIBINPUT_LT_VERSION=23:0:13
AC_SUBST(LIBINPUT_LT_VERSION)

AM_SILENT_RULES([yes])
AC_USE_SYSTEM_EXTENSIONS

# Check for programs
AC_PROG_CC_C99
AC_PROG_CXX # Only used by build C++ test
AC_PROG_GREP
AC_PROG_SED

# Only used for testing the hwdb
AM_PATH_PYTHON([3.0],, [:])
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != :])

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

AC_CHECK_DECL(static_assert, [],
	      [AC_DEFINE(static_assert(...), [/* */], [noop static_assert() replacement]),
              AC_MSG_RESULT([no])],
	      [[#include <assert.h>]])

PKG_PROG_PKG_CONFIG()
PKG_CHECK_MODULES(MTDEV, [mtdev >= 1.1.0])
PKG_CHECK_MODULES(LIBUDEV, [libudev])
PKG_CHECK_MODULES(LIBEVDEV, [libevdev >= 1.3])

AC_CHECK_LIB([m], [atan2])
AC_CHECK_LIB([rt], [clock_gettime])

if test "x$GCC" = "xyes"; then
	GCC_CXXFLAGS="-Wall -Wextra -Wno-unused-parameter -g -fvisibility=hidden"
	GCC_CFLAGS="$GCC_CXXFLAGS -Wmissing-prototypes -Wstrict-prototypes"
fi
AC_SUBST(GCC_CFLAGS)
AC_SUBST(GCC_CXXFLAGS)

######################
# udev configuration #
######################
udev_dir_default="$libdir/udev"
AC_ARG_WITH(udev-dir,
            AS_HELP_STRING([--with-udev-dir=DIR],
                           [udev base directory [[default=$udev_dir_default]]]),
            [],
            [with_udev_dir="yes"])
AS_CASE($with_udev_dir,
	[no|""], [AC_MSG_ERROR([You must define a udev base directory])],
	[yes], [udevdir="$udev_dir_default"],
	[udevdir="$with_udev_dir"])
UDEV_DIR=${udevdir}
AC_SUBST(UDEV_DIR)

################################
# enable/disable documentation #
################################
AC_ARG_ENABLE([documentation],
	      [AC_HELP_STRING([--enable-documentation],
		              [Enable building the documentation (default=yes)])],
	      [build_documentation="$enableval"],
	      [build_documentation="yes"])

if test "x$build_documentation" = "xyes"; then
	AC_PATH_PROG(DOXYGEN, doxygen)
	if test "x$DOXYGEN" = "x"; then
		AC_MSG_ERROR([Documentation build requested but doxygen not found. Install doxygen or disable the documentation using --disable-documentation])
	fi

	AC_MSG_CHECKING([for compatible doxygen version])
	doxygen_version=`$DOXYGEN --version`
	AS_VERSION_COMPARE([$doxygen_version], [1.8.3],
			   [AC_MSG_RESULT([no])
			    DOXYGEN=""],
			   [AC_MSG_RESULT([yes])],
			   [AC_MSG_RESULT([yes])])
	if test "x$DOXYGEN" = "x"; then
		AC_MSG_ERROR([Doxygen $doxygen_version too old. Doxygen 1.8.3+ required for documentation build. Install required doxygen version or disable the documentation using --disable-documentation])
	fi

	AC_PATH_PROG(DOT, dot)
	if test "x$DOT" = "x"; then
		AC_MSG_ERROR([Documentation build requested but graphviz's dot not found. Install graphviz or disable the documentation using --disable-documentation])
	fi

	AC_MSG_CHECKING([for compatible dot version])
	dot_version=`$DOT -V 2>&1|$GREP -oP '(?<=version\W)@<:@0-9.@:>@*(?=\W(.*))'`
	AS_VERSION_COMPARE([$dot_version], [2.26.0],
			   [AC_MSG_RESULT([no])
			    DOT=""],
			   [AC_MSG_RESULT([yes])],
			   [AC_MSG_RESULT([yes])])
	if test "x$DOT" = "x"; then
		AC_MSG_ERROR([Graphviz dot $dot_version too old. Graphviz 2.26+ required for documentation build. Install required graphviz version or disable the documentation using --disable-documentation])
	fi
fi

########################################
# enable/disable the debug-gui feature #
########################################
AC_ARG_ENABLE(debug-gui,
	      AS_HELP_STRING([--enable-debug-gui], [Enable the 'debug-gui' feature in the libinput tool (default=yes)]),
	      [build_debug_gui="$enableval"],
	      [build_debug_gui="yes"])

if test "x$build_debug_gui" = "xyes"; then
	PKG_CHECK_MODULES(CAIRO, [cairo])
	PKG_CHECK_MODULES(GTK, [glib-2.0 gtk+-3.0])
	AC_DEFINE(BUILD_DEBUG_GUI, 1, [Build debug-gui support])
fi


########################
# enable/disable tests #
########################
AC_ARG_ENABLE(tests,
	      AS_HELP_STRING([--enable-tests], [Build the tests (default=yes)]),
	      [build_tests="$enableval"],
	      [build_tests="yes"])


if test "x$build_tests" = "xyes"; then
	PKG_CHECK_MODULES(CHECK, [check >= 0.9.10])

	AC_PATH_PROG(VALGRIND, [valgrind])

	AC_ARG_WITH(libunwind,
		    AS_HELP_STRING([--without-libunwind],[Do not use libunwind]))

	if test "x$with_libunwind" != "xno"; then
		PKG_CHECK_MODULES(LIBUNWIND, [libunwind])
		AC_DEFINE(HAVE_LIBUNWIND, 1, [Have libunwind support])
		HAVE_LIBUNWIND="yes"
	fi

	AC_PATH_PROG(ADDR2LINE, [addr2line])
	if test "x$ADDR2LINE" != "x"; then
		AC_DEFINE_UNQUOTED(HAVE_ADDR2LINE, 1, [addr2line found])
		AC_DEFINE_UNQUOTED(ADDR2LINE, ["$ADDR2LINE"], [Path to addr2line])
	fi
fi

AC_ARG_ENABLE(test-run,
	      AS_HELP_STRING([--enable-test-run], [For internal use only]),
	      [run_tests="$enableval"],
	      [run_tests="yes"])

# Used by the udev rules so we can use callouts during testing without
# installing everything first. Default is the empty string so the installed
# rule will use udev's default path. Override is in udev/Makefile.am
AC_SUBST(UDEV_TEST_PATH, "")
AC_PATH_PROG(SED, [sed])

###########################
# enable/disable libwacom #
###########################

AC_ARG_ENABLE(libwacom,
	      AS_HELP_STRING([--enable-libwacom],
			     [Use libwacom for tablet identification (default=enabled)]),
	      [use_libwacom="$enableval"],
	      [use_libwacom="yes"])
if test "x$use_libwacom" = "xyes"; then
	PKG_CHECK_MODULES(LIBWACOM, [libwacom >= 0.20], [HAVE_LIBWACOM="yes"])
	AC_DEFINE(HAVE_LIBWACOM, 1, [Build with libwacom])

	OLD_LIBS=$LIBS
	OLD_CFLAGS=$CFLAGS
	LIBS="$LIBS $LIBWACOM_LIBS"
	CFLAGS="$CFLAGS $LIBWACOM_CFLAGS"
	AC_MSG_CHECKING([if libwacom_get_paired_device is available])
	AC_LINK_IFELSE(
		       [AC_LANG_PROGRAM([[#include <libwacom/libwacom.h>]],
					[[libwacom_get_paired_device(NULL)]])],
		       [AC_MSG_RESULT([yes])
			AC_DEFINE(HAVE_LIBWACOM_GET_PAIRED_DEVICE, [1],
				  [libwacom_get_paired_device() is available])
		        [libwacom_have_get_paired_device=yes]],
		       [AC_MSG_RESULT([no])
		        [libwacom_have_get_paired_device=no]])
	LIBS=$OLD_LIBS
	CFLAGS=$OLD_CFLAGS
fi

AM_CONDITIONAL(HAVE_LIBWACOM_GET_PAIRED_DEVICE,
	       [test "x$libwacom_have_get_paired_device" == "xyes"])


#######################
# enable/disable gcov #
#######################

AC_ARG_ENABLE([gcov],
              [AS_HELP_STRING([--enable-gcov],
                              [Enable to enable coverage testing (default:disabled)])],
	      [enable_gcov="$enableval"],
              [enable_gcov=no])
if test "x$enable_gcov" != "xno"; then
       GCOV_CFLAGS="-fprofile-arcs -ftest-coverage"
       GCOV_LDFLAGS="-fprofile-arcs -ftest-coverage"
       enable_gcov=yes
fi

AM_CONDITIONAL([GCOV_ENABLED], [test "x$enable_gcov" != "xno"])
AC_SUBST([GCOV_CFLAGS])
AC_SUBST([GCOV_LDFLAGS])

AM_CONDITIONAL(HAVE_VALGRIND, [test "x$VALGRIND" != "x"])
AM_CONDITIONAL(BUILD_TESTS, [test "x$build_tests" = "xyes"])
AM_CONDITIONAL(RUN_TESTS, [test "x$run_tests" = "xyes"])
AM_CONDITIONAL(BUILD_DOCS, [test "x$build_documentation" = "xyes"])
AM_CONDITIONAL(HAVE_LIBUNWIND, [test "x$HAVE_LIBUNWIND" = "xyes"])
AM_CONDITIONAL(BUILD_DEBUG_GUI, [test "x$build_debug_gui" = "xyes"])

AC_CONFIG_FILES([Makefile
		 doc/Makefile
		 doc/libinput.doxygen
		 src/Makefile
		 src/libinput.pc
		 src/libinput-uninstalled.pc
		 src/libinput-version.h
		 test/Makefile
		 tools/Makefile
		 udev/Makefile
		 udev/80-libinput-device-groups.rules
		 udev/90-libinput-model-quirks.rules])
AC_CONFIG_FILES([test/symbols-leak-test],
		[chmod +x test/symbols-leak-test])
AC_OUTPUT

AC_MSG_RESULT([
	Prefix			${prefix}
	udev base dir		${UDEV_DIR}

	libwacom enabled	${use_libwacom}
	Build documentation	${build_documentation}
	Build tests		${build_tests}
	Tests use valgrind	${VALGRIND}
	Tests use libunwind	${HAVE_LIBUNWIND}
	Build GUI event tool	${build_debug_gui}
	Enable gcov profiling	${enable_gcov}
	])
