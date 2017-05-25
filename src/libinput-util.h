/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2013-2015 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBINPUT_UTIL_H
#define LIBINPUT_UTIL_H

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libinput.h"

#define VENDOR_ID_APPLE 0x5ac
#define VENDOR_ID_CHICONY 0x4f2
#define VENDOR_ID_LOGITECH 0x46d
#define VENDOR_ID_WACOM 0x56a
#define VENDOR_ID_SYNAPTICS_SERIAL 0x002
#define PRODUCT_ID_APPLE_KBD_TOUCHPAD 0x273
#define PRODUCT_ID_APPLE_APPLETOUCH 0x21a
#define PRODUCT_ID_SYNAPTICS_SERIAL 0x007

/* The HW DPI rate we normalize to before calculating pointer acceleration */
#define DEFAULT_MOUSE_DPI 1000

#define ANSI_HIGHLIGHT		"\x1B[0;1;39m"
#define ANSI_RED		"\x1B[0;31m"
#define ANSI_GREEN		"\x1B[0;32m"
#define ANSI_YELLOW		"\x1B[0;33m"
#define ANSI_BLUE		"\x1B[0;34m"
#define ANSI_MAGENTA		"\x1B[0;35m"
#define ANSI_CYAN		"\x1B[0;36m"
#define ANSI_BRIGHT_RED		"\x1B[0;31;1m"
#define ANSI_BRIGHT_GREEN	"\x1B[0;32;1m"
#define ANSI_BRIGHT_YELLOW	"\x1B[0;33;1m"
#define ANSI_BRIGHT_BLUE	"\x1B[0;34;1m"
#define ANSI_BRIGHT_MAGENTA	"\x1B[0;35;1m"
#define ANSI_BRIGHT_CYAN	"\x1B[0;36;1m"
#define ANSI_NORMAL		"\x1B[0m"

#define CASE_RETURN_STRING(a) case a: return #a

/*
 * This list data structure is a verbatim copy from wayland-util.h from the
 * Wayland project; except that wl_ prefix has been removed.
 */

struct list {
	struct list *prev;
	struct list *next;
};

void list_init(struct list *list);
void list_insert(struct list *list, struct list *elm);
void list_remove(struct list *elm);
bool list_empty(const struct list *list);

#define container_of(ptr, type, member)					\
	(__typeof__(type) *)((char *)(ptr) -				\
		 offsetof(__typeof__(type), member))

#define list_first_entry(head, pos, member)				\
	container_of((head)->next, __typeof__(*pos), member)

#define list_for_each(pos, head, member)				\
	for (pos = 0, pos = list_first_entry(head, pos, member);	\
	     &pos->member != (head);					\
	     pos = list_first_entry(&pos->member, pos, member))

#define list_for_each_safe(pos, tmp, head, member)			\
	for (pos = 0, tmp = 0, 						\
	     pos = list_first_entry(head, pos, member),			\
	     tmp = list_first_entry(&pos->member, tmp, member);		\
	     &pos->member != (head);					\
	     pos = tmp,							\
	     tmp = list_first_entry(&pos->member, tmp, member))

#define NBITS(b) (b * 8)
#define LONG_BITS (sizeof(long) * 8)
#define NLONGS(x) (((x) + LONG_BITS - 1) / LONG_BITS)
#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])
#define ARRAY_FOR_EACH(_arr, _elem) \
	for (size_t _i = 0; _i < ARRAY_LENGTH(_arr) && (_elem = &_arr[_i]); _i++)
#define AS_MASK(v) (1 << (v))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define streq(s1, s2) (strcmp((s1), (s2)) == 0)
#define strneq(s1, s2, n) (strncmp((s1), (s2), (n)) == 0)

#define NCHARS(x) ((size_t)(((x) + 7) / 8))

#ifdef DEBUG_TRACE
#define debug_trace(...) \
	do { \
	printf("%s:%d %s() - ", __FILE__, __LINE__, __func__); \
	printf(__VA_ARGS__); \
	} while (0)
#else
#define debug_trace(...) { }
#endif

#define LIBINPUT_EXPORT __attribute__ ((visibility("default")))

static inline void *
zalloc(size_t size)
{
	return calloc(1, size);
}

/* This bitfield helper implementation is taken from from libevdev-util.h,
 * except that it has been modified to work with arrays of unsigned chars
 */

static inline bool
bit_is_set(const unsigned char *array, int bit)
{
	return !!(array[bit / 8] & (1 << (bit % 8)));
}

	static inline void
set_bit(unsigned char *array, int bit)
{
	array[bit / 8] |= (1 << (bit % 8));
}

	static inline void
clear_bit(unsigned char *array, int bit)
{
	array[bit / 8] &= ~(1 << (bit % 8));
}

static inline void
msleep(unsigned int ms)
{
	usleep(ms * 1000);
}

static inline bool
long_bit_is_set(const unsigned long *array, int bit)
{
	return !!(array[bit / LONG_BITS] & (1LL << (bit % LONG_BITS)));
}

static inline void
long_set_bit(unsigned long *array, int bit)
{
	array[bit / LONG_BITS] |= (1LL << (bit % LONG_BITS));
}

static inline void
long_clear_bit(unsigned long *array, int bit)
{
	array[bit / LONG_BITS] &= ~(1LL << (bit % LONG_BITS));
}

static inline void
long_set_bit_state(unsigned long *array, int bit, int state)
{
	if (state)
		long_set_bit(array, bit);
	else
		long_clear_bit(array, bit);
}

static inline bool
long_any_bit_set(unsigned long *array, size_t size)
{
	unsigned long i;

	assert(size > 0);

	for (i = 0; i < size; i++)
		if (array[i] != 0)
			return true;
	return false;
}

static inline double
deg2rad(int degree)
{
	return M_PI * degree / 180.0;
}

struct matrix {
	float val[3][3]; /* [row][col] */
};

static inline void
matrix_init_identity(struct matrix *m)
{
	memset(m, 0, sizeof(*m));
	m->val[0][0] = 1;
	m->val[1][1] = 1;
	m->val[2][2] = 1;
}

static inline void
matrix_from_farray6(struct matrix *m, const float values[6])
{
	matrix_init_identity(m);
	m->val[0][0] = values[0];
	m->val[0][1] = values[1];
	m->val[0][2] = values[2];
	m->val[1][0] = values[3];
	m->val[1][1] = values[4];
	m->val[1][2] = values[5];
}

static inline void
matrix_init_scale(struct matrix *m, float sx, float sy)
{
	matrix_init_identity(m);
	m->val[0][0] = sx;
	m->val[1][1] = sy;
}

static inline void
matrix_init_translate(struct matrix *m, float x, float y)
{
	matrix_init_identity(m);
	m->val[0][2] = x;
	m->val[1][2] = y;
}

static inline void
matrix_init_rotate(struct matrix *m, int degrees)
{
	double s, c;

	s = sin(deg2rad(degrees));
	c = cos(deg2rad(degrees));

	matrix_init_identity(m);
	m->val[0][0] = c;
	m->val[0][1] = -s;
	m->val[1][0] = s;
	m->val[1][1] = c;
}

static inline bool
matrix_is_identity(const struct matrix *m)
{
	return (m->val[0][0] == 1 &&
		m->val[0][1] == 0 &&
		m->val[0][2] == 0 &&
		m->val[1][0] == 0 &&
		m->val[1][1] == 1 &&
		m->val[1][2] == 0 &&
		m->val[2][0] == 0 &&
		m->val[2][1] == 0 &&
		m->val[2][2] == 1);
}

static inline void
matrix_mult(struct matrix *dest,
	    const struct matrix *m1,
	    const struct matrix *m2)
{
	struct matrix m; /* allow for dest == m1 or dest == m2 */
	int row, col, i;

	for (row = 0; row < 3; row++) {
		for (col = 0; col < 3; col++) {
			double v = 0;
			for (i = 0; i < 3; i++) {
				v += m1->val[row][i] * m2->val[i][col];
			}
			m.val[row][col] = v;
		}
	}

	memcpy(dest, &m, sizeof(m));
}

static inline void
matrix_mult_vec(const struct matrix *m, int *x, int *y)
{
	int tx, ty;

	tx = *x * m->val[0][0] + *y * m->val[0][1] + m->val[0][2];
	ty = *x * m->val[1][0] + *y * m->val[1][1] + m->val[1][2];

	*x = tx;
	*y = ty;
}

static inline void
matrix_to_farray6(const struct matrix *m, float out[6])
{
	out[0] = m->val[0][0];
	out[1] = m->val[0][1];
	out[2] = m->val[0][2];
	out[3] = m->val[1][0];
	out[4] = m->val[1][1];
	out[5] = m->val[1][2];
}

static inline void
matrix_to_relative(struct matrix *dest, const struct matrix *src)
{
	matrix_init_identity(dest);
	dest->val[0][0] = src->val[0][0];
	dest->val[0][1] = src->val[0][1];
	dest->val[1][0] = src->val[1][0];
	dest->val[1][1] = src->val[1][1];
}

/**
 * Simple wrapper for asprintf that ensures the passed in-pointer is set
 * to NULL upon error.
 * The standard asprintf() call does not guarantee the passed in pointer
 * will be NULL'ed upon failure, whereas this wrapper does.
 *
 * @param strp pointer to set to newly allocated string.
 * This pointer should be passed to free() to release when done.
 * @param fmt the format string to use for printing.
 * @return The number of bytes printed (excluding the null byte terminator)
 * upon success or -1 upon failure. In the case of failure the pointer is set
 * to NULL.
 */
static inline int
xasprintf(char **strp, const char *fmt, ...)
	LIBINPUT_ATTRIBUTE_PRINTF(2, 3);

static inline int
xasprintf(char **strp, const char *fmt, ...)
{
	int rc = 0;
	va_list args;

	va_start(args, fmt);
	rc = vasprintf(strp, fmt, args);
	va_end(args);
	if ((rc == -1) && strp)
		*strp = NULL;

	return rc;
}

enum ratelimit_state {
	RATELIMIT_EXCEEDED,
	RATELIMIT_THRESHOLD,
	RATELIMIT_PASS,
};

struct ratelimit {
	uint64_t interval;
	uint64_t begin;
	unsigned int burst;
	unsigned int num;
};

void ratelimit_init(struct ratelimit *r, uint64_t ival_ms, unsigned int burst);
enum ratelimit_state ratelimit_test(struct ratelimit *r);

int parse_mouse_dpi_property(const char *prop);
int parse_mouse_wheel_click_angle_property(const char *prop);
int parse_mouse_wheel_click_count_property(const char *prop);
double parse_trackpoint_accel_property(const char *prop);
bool parse_dimension_property(const char *prop, size_t *width, size_t *height);
bool parse_calibration_property(const char *prop, float calibration[6]);
bool parse_pressure_range_property(const char *prop, int *hi, int *lo);

enum tpkbcombo_layout {
	TPKBCOMBO_LAYOUT_UNKNOWN,
	TPKBCOMBO_LAYOUT_BELOW,
};
bool parse_tpkbcombo_layout_poperty(const char *prop,
				    enum tpkbcombo_layout *layout);

enum switch_reliability {
	RELIABILITY_UNKNOWN,
	RELIABILITY_RELIABLE,
	RELIABILITY_WRITE_OPEN,
};

bool
parse_switch_reliability_property(const char *prop,
				  enum switch_reliability *reliability);

static inline uint64_t
us(uint64_t us)
{
	return us;
}

static inline uint64_t
ns2us(uint64_t ns)
{
	return us(ns / 1000);
}

static inline uint64_t
ms2us(uint64_t ms)
{
	return us(ms * 1000);
}

static inline uint64_t
s2us(uint64_t s)
{
	return ms2us(s * 1000);
}

static inline uint32_t
us2ms(uint64_t us)
{
	return (uint32_t)(us / 1000);
}

static inline uint64_t
tv2us(const struct timeval *tv)
{
	return s2us(tv->tv_sec) + tv->tv_usec;
}

static inline bool
safe_atoi(const char *str, int *val)
{
	char *endptr;
	long v;

	errno = 0;
	v = strtol(str, &endptr, 10);
	if (errno > 0)
		return false;
	if (str == endptr)
		return false;
	if (*str != '\0' && *endptr != '\0')
		return false;

	if (v > INT_MAX || v < INT_MIN)
		return false;

	*val = v;
	return true;
}

static inline bool
safe_atod(const char *str, double *val)
{
	char *endptr;
	double v;
	locale_t c_locale;

	/* Create a "C" locale to force strtod to use '.' as separator */
	c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
	if (c_locale == (locale_t)0)
		return false;

	errno = 0;
	v = strtod_l(str, &endptr, c_locale);
	freelocale(c_locale);
	if (errno > 0)
		return false;
	if (str == endptr)
		return false;
	if (*str != '\0' && *endptr != '\0')
		return false;
	if (isnan(v) || isinf(v))
		return false;

	*val = v;
	return true;
}

char **strv_from_string(const char *string, const char *separator);

static inline void
strv_free(char **strv) {
	char **s = strv;

	if (!strv)
		return;

	while (*s != NULL) {
		free(*s);
		*s = (char*)0x1; /* detect use-after-free */
		s++;
	}

	free (strv);
}

#endif /* LIBINPUT_UTIL_H */
