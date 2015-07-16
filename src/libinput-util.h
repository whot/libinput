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

#include <unistd.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "libinput.h"

#define VENDOR_ID_APPLE 0x5ac
#define VENDOR_ID_WACOM 0x56a
#define VENDOR_ID_SYNAPTICS_SERIAL 0x002
#define PRODUCT_ID_SYNAPTICS_SERIAL 0x007

/* The HW DPI rate we normalize to before calculating pointer acceleration */
#define DEFAULT_MOUSE_DPI 1000

void
set_logging_enabled(int enabled);

void
log_info(const char *format, ...);

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
int list_empty(const struct list *list);

#ifdef __GNUC__
#define container_of(ptr, sample, member)				\
	(__typeof__(sample))((char *)(ptr)	-			\
		 ((char *)&(sample)->member - (char *)(sample)))
#else
#define container_of(ptr, sample, member)				\
	(void *)((char *)(ptr)	-				        \
		 ((char *)&(sample)->member - (char *)(sample)))
#endif

#define list_for_each(pos, head, member)				\
	for (pos = 0, pos = container_of((head)->next, pos, member);	\
	     &pos->member != (head);					\
	     pos = container_of(pos->member.next, pos, member))

#define list_for_each_safe(pos, tmp, head, member)			\
	for (pos = 0, tmp = 0, 						\
	     pos = container_of((head)->next, pos, member),		\
	     tmp = container_of((pos)->member.next, tmp, member);	\
	     &pos->member != (head);					\
	     pos = tmp,							\
	     tmp = container_of(pos->member.next, tmp, member))

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

static inline void
msleep(unsigned int ms)
{
	usleep(ms * 1000);
}

static inline int
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

static inline int
matrix_is_identity(struct matrix *m)
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
matrix_mult_vec(struct matrix *m, int *x, int *y)
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
double parse_trackpoint_accel_property(const char *prop);
bool parse_dimension_property(const char *prop, size_t *width, size_t *height);

#endif /* LIBINPUT_UTIL_H */
