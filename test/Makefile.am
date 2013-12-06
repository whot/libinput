if BUILD_TESTS
AM_CPPFLAGS = -I$(top_srcdir)/src $(CHECK_CFLAGS)

run_tests =
build_tests = test-build-pedantic-c99 test-build-std-gnuc90

noinst_PROGRAMS = $(build_tests) $(run_tests)
TESTS = $(run_tests)

# build-test only
test_build_pedantic_c99_SOURCES = build-pedantic.c
test_build_pedantic_c99_CFLAGS = $(AM_CPPFLAGS) -std=c99 -pedantic -Werror

test_build_std_gnuc90_SOURCES = build-pedantic.c
test_build_std_gnuc90_CFLAGS = $(AM_CPPFLAGS) -std=gnu90 -Werror

endif
