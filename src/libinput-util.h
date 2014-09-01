/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef LIBINPUT_UTIL_H
#define LIBINPUT_UTIL_H

#include <unistd.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "libinput.h"

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

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

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

enum directions {
	N  = 1 << 0,
	NE = 1 << 1,
	E  = 1 << 2,
	SE = 1 << 3,
	S  = 1 << 4,
	SW = 1 << 5,
	W  = 1 << 6,
	NW = 1 << 7,
	UNDEFINED_DIRECTION = 0xff
};

static inline int
vector_get_direction(int dx, int dy)
{
	int dir = UNDEFINED_DIRECTION;
	int d1, d2;
	double r;

	if (abs(dx) < 2 && abs(dy) < 2) {
		if (dx > 0 && dy > 0)
			dir = S | SE | E;
		else if (dx > 0 && dy < 0)
			dir = N | NE | E;
		else if (dx < 0 && dy > 0)
			dir = S | SW | W;
		else if (dx < 0 && dy < 0)
			dir = N | NW | W;
		else if (dx > 0)
			dir = NE | E | SE;
		else if (dx < 0)
			dir = NW | W | SW;
		else if (dy > 0)
			dir = SE | S | SW;
		else if (dy < 0)
			dir = NE | N | NW;
	} else {
		/* Calculate r within the interval  [0 to 8)
		 *
		 * r = [0 .. 2π] where 0 is North
		 * d_f = r / 2π  ([0 .. 1))
		 * d_8 = 8 * d_f
		 */
		r = atan2(dy, dx);
		r = fmod(r + 2.5*M_PI, 2*M_PI);
		r *= 4*M_1_PI;

		/* Mark one or two close enough octants */
		d1 = (int)(r + 0.9) % 8;
		d2 = (int)(r + 0.1) % 8;

		dir = (1 << d1) | (1 << d2);
	}

	return dir;
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

#endif /* LIBINPUT_UTIL_H */
