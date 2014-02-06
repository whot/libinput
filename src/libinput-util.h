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

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])
#define ARRAY_FOR_EACH(_arr, _elem) \
	for (int i = 0; (_elem = &_arr[i]) && i < ARRAY_LENGTH(_arr); i++)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/*
 * This fixed point implementation is a verbatim copy from wayland-util.h from
 * the Wayland project, with the wl_ prefix renamed li_.
 */

static inline li_fixed_t li_fixed_from_int(int i)
{
	return i * 256;
}

static inline li_fixed_t
li_fixed_from_double(double d)
{
	union {
		double d;
		int64_t i;
	} u;

	u.d = d + (3LL << (51 - 8));

	return u.i;
}

#define LIBINPUT_EXPORT __attribute__ ((visibility("default")))

static inline void *
zalloc(size_t size)
{
	return calloc(1, size);
}

#endif /* LIBINPUT_UTIL_H */
