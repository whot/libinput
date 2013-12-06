AC_PREREQ([2.64])

m4_define([libinput_major_version], [0])
m4_define([libinput_minor_version], [0])
m4_define([libinput_micro_version], [90])
m4_define([libinput_version],
          [libinput_major_version.libinput_minor_version.libinput_micro_version])

AC_INIT([libinput],
        [libinput_version],
        [http://nobugtracker],
        [libinput],
        [http://nohomepage])

AC_SUBST([LIBINPUT_VERSION_MAJOR], [libinput_major_version])
AC_SUBST([LIBINPUT_VERSION_MINOR], [libinput_minor_version])
AC_SUBST([LIBINPUT_VERSION_MICRO], [libinput_micro_version])
AC_SUBST([LIBINPUT_VERSION], [libinput_version])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz])

AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC_C99

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

if test "x$GCC" = "xyes"; then
	GCC_CFLAGS="-Wall -Wextra -Wno-unused-parameter -g -Wstrict-prototypes -Wmissing-prototypes -fvisibility=hidden"
fi
AC_SUBST(GCC_CFLAGS)

AC_PATH_PROG(DOXYGEN, [doxygen])
if test "x$DOXYGEN" = "x"; then
	AC_MSG_WARN([doxygen not found - required for documentation])
	have_doxygen="no"
else
	have_doxygen="yes"
fi
AM_CONDITIONAL([HAVE_DOXYGEN], [test "x$have_doxygen" = "xyes"])

AC_ARG_ENABLE(tests,
	      AS_HELP_STRING([--enable-tests], [Build the tests (default=auto)]),
	      [build_tests="$enableval"],
	      [build_tests="auto"])
PKG_CHECK_MODULES(CHECK, [check >= 0.9.9], [HAVE_CHECK="yes"], [HAVE_CHECK="no"])
if test "x$build_tests" = "xauto"; then
	build_tests="$HAVE_CHECK"
fi
if test "x$build_tests" = "xyes" -a "x$HAVE_CHECK" = "xno"; then
	AC_MSG_ERROR([Cannot build tests, check is missing])
fi

AM_CONDITIONAL(BUILD_TESTS, [test "x$build_tests" = "xyes"])

AC_CONFIG_FILES([Makefile
		 doc/Makefile
		 doc/libinput.doxygen
		 src/Makefile
		 src/libinput.pc
		 src/libinput-version.h
		 test/Makefile])
AC_OUTPUT
