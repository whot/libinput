AC_PREREQ([2.64])

m4_define([libinput_major_version], [0])
m4_define([libinput_minor_version], [4])
m4_define([libinput_micro_version], [0])
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

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz])

# Before making a release, the LIBINPUT_LT_VERSION string should be
# modified.
# The string is of the form C:R:A.
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (eg removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A
LIBINPUT_LT_VERSION=3:0:0
AC_SUBST(LIBINPUT_LT_VERSION)

AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC_C99
AC_PROG_CXX # Only used by build C++ test

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

AC_CHECK_DECL(EPOLL_CLOEXEC, [],
	      [AC_MSG_ERROR("EPOLL_CLOEXEC is needed to compile libinput")],
	      [[#include <sys/epoll.h>]])
AC_CHECK_DECL(TFD_CLOEXEC,[],
	      [AC_MSG_ERROR("TFD_CLOEXEC is needed to compile libinput")],
	      [[#include <sys/timerfd.h>]])
AC_CHECK_DECL(CLOCK_MONOTONIC,[],
	      [AC_MSG_ERROR("CLOCK_MONOTONIC is needed to compile libinput")],
	      [[#include <time.h>]])

PKG_PROG_PKG_CONFIG()
PKG_CHECK_MODULES(MTDEV, [mtdev >= 1.1.0])
PKG_CHECK_MODULES(LIBUDEV, [libudev])
PKG_CHECK_MODULES(LIBEVDEV, [libevdev >= 0.4])
AC_CHECK_LIB([m], [atan2])
AC_CHECK_LIB([rt], [clock_gettime])

if test "x$GCC" = "xyes"; then
	GCC_CXXFLAGS="-Wall -Wextra -Wno-unused-parameter -g -fvisibility=hidden"
	GCC_CFLAGS="$GCC_CXXFLAGS -Wmissing-prototypes -Wstrict-prototypes"
fi
AC_SUBST(GCC_CFLAGS)
AC_SUBST(GCC_CXXFLAGS)

AC_PATH_PROG(DOXYGEN, [doxygen])
if test "x$DOXYGEN" = "x"; then
	AC_MSG_WARN([doxygen not found - required for documentation])
	have_doxygen="no"
else
	have_doxygen="yes"
fi
AM_CONDITIONAL([HAVE_DOXYGEN], [test "x$have_doxygen" = "xyes"])

AC_ARG_ENABLE(event-gui,
	      AS_HELP_STRING([--enable-event-gui], [Build the GUI event viewer (default=auto)]),
	      [build_eventgui="$enableval"],
	      [build_eventgui="auto"])
PKG_CHECK_EXISTS([cairo glib-2.0 gtk+-3.0], [HAVE_GUILIBS="yes"], [HAVE_GUILIBS="no"])

if test "x$build_eventgui" = "xauto"; then
	build_eventgui="$HAVE_GUILIBS"
fi
if test "x$build_eventgui" = "xyes"; then
	PKG_CHECK_MODULES(CAIRO, [cairo])
	PKG_CHECK_MODULES(GTK, [glib-2.0 gtk+-3.0])
fi
AM_CONDITIONAL(BUILD_EVENTGUI, [test "x$build_eventgui" = "xyes"])

AC_ARG_ENABLE(tests,
	      AS_HELP_STRING([--enable-tests], [Build the tests (default=auto)]),
	      [build_tests="$enableval"],
	      [build_tests="auto"])

PKG_CHECK_MODULES(CHECK, [check >= 0.9.9], [HAVE_CHECK="yes"], [HAVE_CHECK="no"])

if test "x$build_tests" = "xauto"; then
	build_tests="$HAVE_CHECK"
fi
if test "x$build_tests" = "xyes"; then
	if test "x$HAVE_CHECK" = "xno"; then
		AC_MSG_ERROR([Cannot build tests, check is missing])
	fi

	AC_PATH_PROG(VALGRIND, [valgrind])
fi

AM_CONDITIONAL(HAVE_VALGRIND, [test "x$VALGRIND" != "x"])
AM_CONDITIONAL(BUILD_TESTS, [test "x$build_tests" = "xyes"])

# Uses non-exported symbols and needs to be statically linked, otherwise we
# get linker errors.
if test "x$enable_static" != "xyes"; then
	AC_MSG_WARN([Static libraries disabled, cannot build the the
		     pointer acceleration debug tool.])
fi
build_ptraccel_debug="$enable_static"
AM_CONDITIONAL(BUILD_PTRACCEL_DEBUG, [test "x$build_ptraccel_debug" = "xyes"])

AC_CONFIG_FILES([Makefile
		 doc/Makefile
		 doc/libinput.doxygen
		 src/Makefile
		 src/libinput.pc
		 src/libinput-version.h
		 test/Makefile
		 tools/Makefile])
AC_OUTPUT

AC_MSG_RESULT([
	Prefix			${prefix}

	Build documentation		${have_doxygen}
	Build tests			${build_tests}
	Tests use valgrind		${VALGRIND}
	Build GUI event tool		${build_eventgui}
	Build ptraccel debug tool	${build_ptraccel_debug}
	])
