/*
 * Copyright © 2017 Red Hat, Inc.
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

#ifndef _EVFD_H_
#define _EVFD_H_

#include "config.h"

/**
 * Adds a device to an evdev context. The device provided in the path
 * is used to initialize the libinput device. Once initialized, the fd
 * referring to the device will be closed and substituted with the
 * event_fd provided.
 *
 * Subsequently, libinput reads events of type struct input_event from the
 * fd as if this was a normal device fd.
 *
 * @param libinput The context
 * @param path Path to a device node
 * @param event_fd The fd to listen on for events.
 */
struct libinput_device *
libinput_evfd_add_device(struct libinput *libinput,
			 const char *path,
			 int event_fd);

/**
 * Creates a context for the evdev interface.
 * This interface should never be used by any real application. It only
 * exists for testing.
 */
struct libinput *
libinput_evfd_create_context(const struct libinput_interface *interface,
			     void *user_data);

#endif
