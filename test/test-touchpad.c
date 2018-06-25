/*
 * Copyright © 2014 Red Hat, Inc.
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

#include <config.h>

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <unistd.h>

#include "libinput-util.h"
#include "litest.h"

static inline bool
has_disable_while_typing(struct litest_device *device)
{
	return libinput_device_config_dwt_is_available(device->libinput_device);
}

static inline struct litest_device *
dwt_init_paired_keyboard(struct libinput *li,
			 struct litest_device *touchpad)
{
	enum litest_device_type which = LITEST_KEYBOARD;

	if (libevdev_get_id_vendor(touchpad->evdev) == VENDOR_ID_APPLE)
		which = LITEST_APPLE_KEYBOARD;

	if (libevdev_get_id_vendor(touchpad->evdev) == VENDOR_ID_CHICONY)
		which = LITEST_ACER_HAWAII_KEYBOARD;

	return litest_add_device(li, which);
}

START_TEST(touchpad_1fg_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 50, 20, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);

	while (event) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);

		ptrev = libinput_event_get_pointer_event(event);
		ck_assert_int_ge(libinput_event_pointer_get_dx(ptrev), 0);
		ck_assert_int_eq(libinput_event_pointer_get_dy(ptrev), 0);
		libinput_event_destroy(event);
		event = libinput_get_event(li);
	}
}
END_TEST

START_TEST(touchpad_2fg_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_DISABLED);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 20);
	litest_touch_down(dev, 1, 70, 20);
	litest_touch_move_to(dev, 0, 20, 20, 80, 80, 20, 0);
	litest_touch_move_to(dev, 1, 70, 20, 80, 50, 20, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	while (event) {
		ck_assert_int_ne(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		libinput_event_destroy(event);
		event = libinput_get_event(li);
	}
}
END_TEST

static void
test_2fg_scroll(struct litest_device *dev, double dx, double dy, int want_sleep)
{
	struct libinput *li = dev->libinput;

	litest_touch_down(dev, 0, 49, 50);
	litest_touch_down(dev, 1, 51, 50);

	litest_touch_move_two_touches(dev, 49, 50, 51, 50, dx, dy, 10, 0);

	/* Avoid a small scroll being seen as a tap */
	if (want_sleep) {
		libinput_dispatch(li);
		litest_timeout_tap();
		libinput_dispatch(li);
	}

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
}

START_TEST(touchpad_2fg_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	test_2fg_scroll(dev, 0.1, 40, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 10);
	test_2fg_scroll(dev, 0.1, -40, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -10);
	test_2fg_scroll(dev, 40, 0.1, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 10);
	test_2fg_scroll(dev, -40, 0.1, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -10);

	/* 2fg scroll smaller than the threshold should not generate events */
	test_2fg_scroll(dev, 0.1, 0.1, 1);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_scroll_diagonal)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	int i;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 45, 30);
	litest_touch_down(dev, 1, 55, 30);

	litest_touch_move_two_touches(dev, 45, 30, 55, 30, 10, 10, 10, 0);
	libinput_dispatch(li);
	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_POINTER_AXIS,
				      -1);
	litest_drain_events(li);

	/* get rid of any touch history still adding x deltas sideways */
	for (i = 0; i < 5; i++)
		litest_touch_move(dev, 0, 55, 41 + i);
	litest_drain_events(li);

	for (i = 6; i < 10; i++) {
		litest_touch_move(dev, 0, 55, 41 + i);
		libinput_dispatch(li);

		event = libinput_get_event(li);
		ptrev = litest_is_axis_event(event,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
				LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
		ck_assert(!libinput_event_pointer_has_axis(ptrev,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL));
		libinput_event_destroy(event);
	}

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
}
END_TEST

START_TEST(touchpad_2fg_scroll_slow_distance)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double width, height;
	double y_move = 100;

	if (!litest_has_2fg_scroll(dev))
		return;

	/* We want to move > 5 mm. */
	ck_assert_int_eq(libinput_device_get_size(dev->libinput_device,
						  &width,
						  &height), 0);
	y_move = 100.0/height * 7;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 49, 50);
	litest_touch_down(dev, 1, 51, 50);
	litest_touch_move_two_touches(dev, 49, 50, 51, 50, 0, y_move, 100, 10);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert_notnull(event);

	/* last event is value 0, tested elsewhere */
	while (libinput_next_event_type(li) != LIBINPUT_EVENT_NONE) {
		double axisval;
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);

		axisval = libinput_event_pointer_get_axis_value(ptrev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert(axisval > 0.0);

		/* this is to verify we test the right thing, if the value
		   is greater than scroll.threshold we triggered the wrong
		   condition */
		ck_assert(axisval < 5.0);

		libinput_event_destroy(event);
		event = libinput_get_event(li);
	}

	litest_assert_empty_queue(li);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(touchpad_2fg_scroll_source)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	test_2fg_scroll(dev, 0, 30, 0);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_POINTER_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);
		ck_assert_int_eq(libinput_event_pointer_get_axis_source(ptrev),
				 LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(touchpad_2fg_scroll_semi_mt)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 20);
	litest_touch_down(dev, 1, 30, 20);
	libinput_dispatch(li);
	litest_touch_move_two_touches(dev,
				      20, 20,
				      30, 20,
				      30, 40,
				      10, 1);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_2fg_scroll_return_to_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	/* start with motion */
	litest_touch_down(dev, 0, 70, 70);
	litest_touch_move_to(dev, 0, 70, 70, 49, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* 2fg scroll */
	litest_touch_down(dev, 1, 51, 50);
	litest_touch_move_two_touches(dev, 49, 50, 51, 50, 0, 20, 5, 0);
	litest_touch_up(dev, 1);
	libinput_dispatch(li);
	litest_timeout_finger_switch();
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_touch_move_to(dev, 0, 49, 70, 49, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* back to 2fg scroll, lifting the other finger */
	litest_touch_down(dev, 1, 51, 50);
	litest_touch_move_two_touches(dev, 49, 50, 51, 50, 0, 20, 5, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_finger_switch();
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* move with second finger */
	litest_touch_move_to(dev, 1, 51, 70, 51, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 1);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_scroll_natural_defaults)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_ge(libinput_device_config_scroll_has_natural_scroll(dev->libinput_device), 1);
	ck_assert_int_eq(libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device), 0);
	ck_assert_int_eq(libinput_device_config_scroll_get_default_natural_scroll_enabled(dev->libinput_device), 0);
}
END_TEST

START_TEST(touchpad_scroll_natural_enable_config)
{
	struct litest_device *dev = litest_current_device();
	enum libinput_config_status status;

	status = libinput_device_config_scroll_set_natural_scroll_enabled(dev->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	ck_assert_int_eq(libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device), 1);

	status = libinput_device_config_scroll_set_natural_scroll_enabled(dev->libinput_device, 0);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	ck_assert_int_eq(libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device), 0);
}
END_TEST

START_TEST(touchpad_scroll_natural_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	libinput_device_config_scroll_set_natural_scroll_enabled(dev->libinput_device, 1);

	test_2fg_scroll(dev, 0.1, 40, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -10);
	test_2fg_scroll(dev, 0.1, -40, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 10);
	test_2fg_scroll(dev, 40, 0.1, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -10);
	test_2fg_scroll(dev, -40, 0.1, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 10);

}
END_TEST

START_TEST(touchpad_scroll_natural_edge)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	libinput_device_config_scroll_set_natural_scroll_enabled(dev->libinput_device, 1);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -4);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 99, 80);
	litest_touch_move_to(dev, 0, 99, 80, 99, 20, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 4);
	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(touchpad_edge_scroll_vert)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 4);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 99, 80);
	litest_touch_move_to(dev, 0, 99, 80, 99, 20, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -4);
	litest_assert_empty_queue(li);
}
END_TEST

static int
touchpad_has_horiz_edge_scroll_size(struct litest_device *dev)
{
	double width, height;
	int rc;

	rc = libinput_device_get_size(dev->libinput_device, &width, &height);

	return rc == 0 && height >= 40;
}

START_TEST(touchpad_edge_scroll_horiz)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	if (!touchpad_has_horiz_edge_scroll_size(dev))
		return;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 20, 99);
	litest_touch_move_to(dev, 0, 20, 99, 70, 99, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 4);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 70, 99);
	litest_touch_move_to(dev, 0, 70, 99, 20, 99, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -4);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_horiz_clickpad)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 20, 99);
	litest_touch_move_to(dev, 0, 20, 99, 70, 99, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 4);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 70, 99);
	litest_touch_move_to(dev, 0, 70, 99, 20, 99, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -4);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_no_horiz)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (touchpad_has_horiz_edge_scroll_size(dev))
		return;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 20, 99);
	litest_touch_move_to(dev, 0, 20, 99, 70, 99, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_down(dev, 0, 70, 99);
	litest_touch_move_to(dev, 0, 70, 99, 20, 99, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_scroll_defaults)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libevdev *evdev = dev->evdev;
	enum libinput_config_scroll_method method, expected;
	enum libinput_config_status status;
	bool should_have_2fg = false;

	if (libevdev_get_num_slots(evdev) > 1 ||
	    (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_APPLE &&
	     libevdev_get_id_product(dev->evdev) == PRODUCT_ID_APPLE_APPLETOUCH))
		should_have_2fg = true;

	method = libinput_device_config_scroll_get_methods(device);
	ck_assert(method & LIBINPUT_CONFIG_SCROLL_EDGE);
	if (should_have_2fg)
		ck_assert(method & LIBINPUT_CONFIG_SCROLL_2FG);
	else
		ck_assert((method & LIBINPUT_CONFIG_SCROLL_2FG) == 0);

	if (should_have_2fg)
		expected = LIBINPUT_CONFIG_SCROLL_2FG;
	else
		expected = LIBINPUT_CONFIG_SCROLL_EDGE;

	method = libinput_device_config_scroll_get_method(device);
	ck_assert_int_eq(method, expected);
	method = libinput_device_config_scroll_get_default_method(device);
	ck_assert_int_eq(method, expected);

	status = libinput_device_config_scroll_set_method(device,
					  LIBINPUT_CONFIG_SCROLL_EDGE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_scroll_set_method(device,
					  LIBINPUT_CONFIG_SCROLL_2FG);

	if (should_have_2fg)
		ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	else
		ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(touchpad_edge_scroll_timeout)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double width = 0, height = 0;
	int nevents = 0;
	double mm; /* one mm in percent of the device */

	ck_assert_int_eq(libinput_device_get_size(dev->libinput_device,
						  &width,
						  &height), 0);
	mm = 100.0/height;

	/* timeout-based scrolling is disabled when software buttons are
	 * active, so switch to clickfinger. Not all test devices support
	 * that, hence the extra check. */
	if (libinput_device_config_click_get_methods(dev->libinput_device) &
	    LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER)
		litest_enable_clickfinger(dev);

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	/* move 0.5mm, enough to load up the motion history, but less than
	 * the scroll threshold of 2mm */
	litest_touch_down(dev, 0, 99, 20);
	libinput_dispatch(li);
	litest_timeout_hysteresis();
	libinput_dispatch(li);

	litest_touch_move_to(dev, 0, 99, 20, 99, 20 + mm/2, 8, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_timeout_edgescroll();
	libinput_dispatch(li);

	litest_assert_empty_queue(li);

	/* now move slowly up to the 2mm scroll threshold. we expect events */
	litest_touch_move_to(dev, 0, 99, 20 + mm/2, 99, 20 + mm * 2, 20, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_POINTER_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		double value;

		ptrev = litest_is_axis_event(event,
					     LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
					     0);
		value = libinput_event_pointer_get_axis_value(ptrev,
							      LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert_double_lt(value, 5.0);
		libinput_event_destroy(event);
		nevents++;
	}

	/* we sent 20 events but allow for some to be swallowed by rounding
	 * errors, the hysteresis, etc. */
	ck_assert_int_ge(nevents, 10);

	litest_assert_empty_queue(li);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(touchpad_edge_scroll_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 99, 10);
	litest_touch_move_to(dev, 0, 99, 10, 99, 70, 12, 0);
	/* moving outside -> no motion event */
	litest_touch_move_to(dev, 0, 99, 70, 20, 70, 12, 0);
	/* moving down outside edge once scrolling had started -> scroll */
	litest_touch_move_to(dev, 0, 20, 70, 40, 99, 12, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 4);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_no_edge_after_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	/* moving into the edge zone must not trigger scroll events */
	litest_touch_down(dev, 0, 20, 20);
	litest_touch_move_to(dev, 0, 20, 20, 99, 20, 12, 0);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 12, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_source)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_POINTER_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);
		ck_assert_int_eq(libinput_event_pointer_get_axis_source(ptrev),
				 LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(touchpad_edge_scroll_no_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 49, 50);
	litest_touch_down(dev, 1, 51, 50);
	litest_touch_move_two_touches(dev, 49, 50, 51, 50, 20, 30, 5, 0);
	libinput_dispatch(li);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_into_buttonareas)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_buttonareas(dev);
	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 40);
	litest_touch_move_to(dev, 0, 99, 40, 99, 95, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
	/* in the button zone now, make sure we still get events */
	litest_touch_move_to(dev, 0, 99, 95, 99, 100, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* and out of the zone again */
	litest_touch_move_to(dev, 0, 99, 100, 99, 70, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* still out of the zone */
	litest_touch_move_to(dev, 0, 99, 70, 99, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_edge_scroll_within_buttonareas)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_horiz_edge_scroll_size(dev))
		return;

	litest_enable_buttonareas(dev);
	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 99);

	/* within left button */
	litest_touch_move_to(dev, 0, 20, 99, 40, 99, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* over to right button */
	litest_touch_move_to(dev, 0, 40, 99, 60, 99, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* within right button */
	litest_touch_move_to(dev, 0, 60, 99, 80, 99, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_edge_scroll_buttonareas_click_stops_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double val;

	if (!touchpad_has_horiz_edge_scroll_size(dev))
		return;

	litest_enable_buttonareas(dev);
	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 95);
	litest_touch_move_to(dev, 0, 20, 95, 70, 95, 10, 5);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_button_click(dev, BTN_LEFT, true);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ptrev = litest_is_axis_event(event,
				     LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL,
				     LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
	val = libinput_event_pointer_get_axis_value(ptrev,
				    LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
	ck_assert(val == 0.0);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_RIGHT,
			       LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_event_destroy(event);

	/* within button areas -> no movement */
	litest_touch_move_to(dev, 0, 70, 95, 90, 95, 10, 0);
	litest_assert_empty_queue(li);

	litest_button_click(dev, BTN_LEFT, false);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_edge_scroll_clickfinger_click_stops_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double val;

	if (!touchpad_has_horiz_edge_scroll_size(dev))
		return;

	litest_enable_clickfinger(dev);
	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 95);
	litest_touch_move_to(dev, 0, 20, 95, 70, 95, 10, 5);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_button_click(dev, BTN_LEFT, true);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ptrev = litest_is_axis_event(event,
				     LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL,
				     LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
	val = libinput_event_pointer_get_axis_value(ptrev,
				    LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
	ck_assert(val == 0.0);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_LEFT,
			       LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_event_destroy(event);

	/* clickfinger releases pointer -> expect movement */
	litest_touch_move_to(dev, 0, 70, 95, 90, 95, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	litest_assert_empty_queue(li);

	litest_button_click(dev, BTN_LEFT, false);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_edge_scroll_into_area)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	/* move into area, move vertically, move back to edge */

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 50, 15, 2);
	litest_touch_move_to(dev, 0, 99, 50, 20, 50, 15, 2);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_AXIS);
	litest_touch_move_to(dev, 0, 20, 50, 20, 20, 15, 2);
	litest_touch_move_to(dev, 0, 20, 20, 99, 20, 15, 2);
	litest_assert_empty_queue(li);

	litest_touch_move_to(dev, 0, 99, 20, 99, 50, 15, 2);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

static bool
touchpad_has_palm_detect_size(struct litest_device *dev)
{
	double width, height;
	unsigned int vendor;
	unsigned int bustype;
	int rc;

	vendor = libinput_device_get_id_vendor(dev->libinput_device);
	bustype = libevdev_get_id_bustype(dev->evdev);
	if (vendor == VENDOR_ID_WACOM)
		return 0;
	if (bustype == BUS_BLUETOOTH)
		return 0;
	if (vendor == VENDOR_ID_APPLE)
		return 1;

	rc = libinput_device_get_size(dev->libinput_device, &width, &height);

	return rc == 0 && width >= 70;
}

static bool
touchpad_has_top_palm_detect_size(struct litest_device *dev)
{
	double width, height;
	int rc;

	if (!touchpad_has_palm_detect_size(dev))
		return false;

	rc = libinput_device_get_size(dev->libinput_device, &width, &height);

	return rc == 0 && height > 55;
}

START_TEST(touchpad_palm_detect_at_edge)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev) ||
	    !litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 99, 70, 5, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 50);
	litest_touch_move_to(dev, 0, 5, 50, 5, 70, 5, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_at_top)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_top_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 1);
	litest_touch_move_to(dev, 0, 20, 1, 70, 1, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_no_palm_detect_at_edge_for_edge_scrolling)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_enable_edge_scroll(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 99, 70, 5, 0);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_palm_detect_at_bottom_corners)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev) ||
	    !litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);

	litest_disable_tap(dev->libinput_device);

	/* Run for non-clickpads only: make sure the bottom corners trigger
	   palm detection too */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 95);
	litest_touch_move_to(dev, 0, 99, 95, 99, 99, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 95);
	litest_touch_move_to(dev, 0, 5, 95, 5, 99, 5, 0);
	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_palm_detect_at_top_corners)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev) ||
	    !litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);

	litest_disable_tap(dev->libinput_device);

	/* Run for non-clickpads only: make sure the bottom corners trigger
	   palm detection too */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 5);
	litest_touch_move_to(dev, 0, 99, 5, 99, 9, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 5);
	litest_touch_move_to(dev, 0, 5, 5, 5, 9, 5, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_palm_stays_palm)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev) ||
	    !litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 75, 99, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_top_palm_stays_palm)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_top_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 1);
	litest_touch_move_to(dev, 0, 20, 1, 90, 30, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_palm_becomes_pointer)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev) ||
	    !litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 0, 70, 20, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_top_palm_becomes_pointer)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_top_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 1);
	litest_touch_move_to(dev, 0, 50, 1, 50, 60, 20, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_no_palm_moving_into_edges)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	/* moving non-palm into the edge does not label it as palm */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 99, 50, 10, 0);

	litest_drain_events(li);

	litest_touch_move_to(dev, 0, 99, 50, 99, 90, 10, 0);
	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_no_palm_moving_into_top)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_top_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	/* moving non-palm into the edge does not label it as palm */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 0, 2, 10, 0);

	litest_drain_events(li);

	litest_touch_move_to(dev, 0, 0, 2, 50, 50, 10, 0);
	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_no_tap_top_edge)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_top_palm_detect_size(dev))
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_tap_hardbuttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 95, 5);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 5);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 95, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_tap_softbuttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_enable_buttonareas(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 1, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 10, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 90, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_tap_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 95, 5);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 5);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 95, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_no_palm_detect_2fg_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev) ||
	    !litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);

	litest_drain_events(li);

	/* first finger is palm, second finger isn't so we trigger 2fg
	 * scrolling */
	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 99, 40, 35, 12);
	litest_touch_move_to(dev, 0, 99, 40, 99, 50, 35, 12);
	litest_assert_empty_queue(li);
	litest_touch_down(dev, 1, 50, 50);
	litest_assert_empty_queue(li);

	litest_touch_move_two_touches(dev, 99, 50, 50, 50, 0, -20, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_palm_detect_both_edges)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev) ||
	    !litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);

	litest_drain_events(li);

	/* two fingers moving up/down in the left/right palm zone must not
	 * generate events */
	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 99, 40, 10, 0);
	litest_touch_move_to(dev, 0, 99, 40, 99, 50, 10, 0);
	litest_assert_empty_queue(li);
	/* This set generates events */
	litest_touch_down(dev, 1, 1, 50);
	litest_touch_move_to(dev, 1, 1, 50, 1, 40, 10, 0);
	litest_touch_move_to(dev, 1, 1, 40, 1, 50, 10, 0);
	litest_assert_empty_queue(li);

	litest_touch_move_two_touches(dev, 99, 50, 1, 50, 0, -20, 10, 0);
	litest_assert_empty_queue(li);
}
END_TEST

static inline bool
touchpad_has_tool_palm(struct litest_device *dev)
{
	return libevdev_has_event_code(dev->evdev, EV_ABS, ABS_MT_TOOL_TYPE);
}

START_TEST(touchpad_palm_detect_tool_palm)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_tool_palm(dev))
		return;

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 1);
	litest_drain_events(li);

	litest_event(dev, EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_move_to(dev, 0, 70, 70, 50, 40, 10, 1);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_tool_palm_on_off)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_tool_palm(dev))
		return;

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 1);
	litest_drain_events(li);

	litest_event(dev, EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_move_to(dev, 0, 70, 70, 50, 40, 10, 1);

	litest_assert_empty_queue(li);

	litest_event(dev, EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_move_to(dev, 0, 50, 40, 70, 70, 10, 1);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_palm_detect_tool_palm_tap_after)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_tool_palm(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_event(dev, EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM);
	litest_touch_down(dev, 0, 50, 50);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);

	litest_touch_move_to(dev, 0, 50, 50, 50, 80, 10, 0);
	libinput_dispatch(li);

	litest_assert_empty_queue(li);

	litest_push_event_frame(dev);
	litest_event(dev, EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER);
	litest_touch_up(dev, 0);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);
	litest_timeout_tap();
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 50, 50);
	libinput_dispatch(li);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();

	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_tool_palm_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_tool_palm(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_event(dev, EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM);
	litest_touch_down(dev, 0, 50, 50);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();

	litest_assert_empty_queue(li);
}
END_TEST

static inline bool
touchpad_has_palm_pressure(struct litest_device *dev)
{
	struct libevdev *evdev = dev->evdev;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_PRESSURE))
		return true;

	return false;
}

START_TEST(touchpad_palm_detect_pressure)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!touchpad_has_palm_pressure(dev))
		return;

	litest_disable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 99, axes);
	litest_touch_move_to(dev, 0, 50, 50, 80, 99, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_pressure_late)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!touchpad_has_palm_pressure(dev))
		return;

	litest_disable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 70, 80, 90, 10, 0);
	litest_drain_events(li);
	libinput_dispatch(li);
	litest_touch_move_to_extended(dev, 0, 80, 90, 50, 20,
				      axes, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_pressure_keep_palm)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!touchpad_has_palm_pressure(dev))
		return;

	litest_disable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 80, 90);
	litest_touch_move_to_extended(dev, 0, 80, 90, 50, 20,
				      axes, 10, 0);
	litest_touch_move_to(dev, 0, 50, 20, 80, 90, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_pressure_after_edge)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!touchpad_has_palm_pressure(dev) ||
	    !touchpad_has_palm_detect_size(dev) ||
	    !litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_disable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to_extended(dev, 0, 99, 50, 20, 50, axes, 20, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_pressure_after_dwt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!touchpad_has_palm_pressure(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_drain_events(li);

	/* within dwt timeout, dwt blocks events */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to_extended(touchpad, 0, 50, 50, 20, 50, axes, 20, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	/* after dwt timeout, pressure blocks events */
	litest_touch_move_to_extended(touchpad, 0, 20, 50, 50, 50, axes, 20, 0);
	litest_touch_up(touchpad, 0);

	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_palm_clickfinger_pressure)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!touchpad_has_palm_pressure(dev))
		return;

	litest_enable_clickfinger(dev);
	litest_disable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 95, axes);
	litest_touch_down(dev, 1, 50, 50);
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_LEFT, false);

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_clickfinger_pressure_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!touchpad_has_palm_pressure(dev))
		return;

	if (libevdev_get_num_slots(dev->evdev) < 3)
		return;

	litest_enable_clickfinger(dev);
	litest_disable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 95, axes);
	litest_touch_down(dev, 1, 50, 50);
	litest_touch_down(dev, 2, 50, 60);
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_LEFT, false);

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 2);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST


static inline bool
touchpad_has_touch_size(struct litest_device *dev)
{
	struct libevdev *evdev = dev->evdev;

	if (!libevdev_has_event_code(evdev, EV_ABS, ABS_MT_TOUCH_MAJOR))
		return false;

	if (libevdev_get_id_vendor(evdev) == VENDOR_ID_APPLE)
		return true;

	return false;
}

START_TEST(touchpad_palm_clickfinger_size)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_TOUCH_MAJOR, 0 },
		{ ABS_MT_TOUCH_MINOR, 0 },
		{ ABS_MT_ORIENTATION, 0 },
		{ -1, 0 }
	};

	if (!touchpad_has_touch_size(dev))
		return;

	litest_enable_clickfinger(dev);
	litest_disable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 95, axes);
	litest_touch_down(dev, 1, 50, 50);
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_LEFT, false);

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_clickfinger_size_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_TOUCH_MAJOR, 0 },
		{ ABS_MT_TOUCH_MINOR, 0 },
		{ ABS_MT_ORIENTATION, 0 },
		{ -1, 0 }
	};

	if (!touchpad_has_touch_size(dev))
		return;

	if (libevdev_get_num_slots(dev->evdev) < 3)
		return;

	litest_enable_clickfinger(dev);
	litest_disable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 95, axes);
	litest_touch_down(dev, 1, 50, 50);
	litest_touch_down(dev, 2, 50, 60);
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_LEFT, false);

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 2);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST
START_TEST(touchpad_left_handed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_APPLE &&
	    libevdev_get_id_product(dev->evdev) == PRODUCT_ID_APPLE_APPLETOUCH)
		return;

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_button_click(dev, BTN_RIGHT, 1);
	litest_button_click(dev, BTN_RIGHT, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	if (libevdev_has_event_code(dev->evdev,
				    EV_KEY,
				    BTN_MIDDLE)) {
		litest_button_click(dev, BTN_MIDDLE, 1);
		litest_button_click(dev, BTN_MIDDLE, 0);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
	}
}
END_TEST

START_TEST(touchpad_left_handed_appletouch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	enum libinput_config_status status;

	ck_assert_int_eq(libinput_device_config_left_handed_is_available(d), 0);
	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	ck_assert_int_eq(libinput_device_config_left_handed_get(d), 0);
}
END_TEST

START_TEST(touchpad_left_handed_clickpad)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	if (!libinput_device_config_left_handed_is_available(d))
		return;

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 10, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 90, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 50, 50);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	if (!libinput_device_config_left_handed_is_available(d))
		return;

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 10, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	/* Clickfinger is unaffected by left-handed setting */
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 10, 90);
	litest_touch_down(dev, 1, 30, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_tapping)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	if (!libinput_device_config_left_handed_is_available(d))
		return;

	litest_enable_tap(dev->libinput_device);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);

	/* Tapping is unaffected by left-handed setting */
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_tapping_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	if (!libinput_device_config_left_handed_is_available(d))
		return;

	litest_enable_tap(dev->libinput_device);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);

	/* Tapping is unaffected by left-handed setting */
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_delayed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	if (!libinput_device_config_left_handed_is_available(d))
		return;

	litest_drain_events(li);
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click(dev, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* left-handed takes effect now */
	litest_button_click(dev, BTN_RIGHT, 1);
	libinput_dispatch(li);
	litest_timeout_middlebutton();
	libinput_dispatch(li);
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 0);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click(dev, BTN_RIGHT, 0);
	litest_button_click(dev, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_clickpad_delayed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	if (!libinput_device_config_left_handed_is_available(d))
		return;

	litest_drain_events(li);
	litest_touch_down(dev, 0, 10, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* left-handed takes effect now */
	litest_drain_events(li);
	litest_touch_down(dev, 0, 90, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 0);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

static void
hover_continue(struct litest_device *dev, unsigned int slot,
	       int x, int y)
{
	litest_event(dev, EV_ABS, ABS_MT_SLOT, slot);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
	litest_event(dev, EV_ABS, ABS_X, x);
	litest_event(dev, EV_ABS, ABS_Y, y);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 10);
	litest_event(dev, EV_ABS, ABS_TOOL_WIDTH, 6);
	/* WARNING: no SYN_REPORT! */
}

static void
hover_start(struct litest_device *dev, unsigned int slot,
	    int x, int y)
{
	static unsigned int tracking_id;

	litest_event(dev, EV_ABS, ABS_MT_SLOT, slot);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, ++tracking_id);
	hover_continue(dev, slot, x, y);
	/* WARNING: no SYN_REPORT! */
}

START_TEST(touchpad_semi_mt_hover_noevent)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;
	int x = 2400,
	    y = 2400;

	litest_drain_events(li);

	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int i;
	int x = 2400,
	    y = 2400;

	litest_drain_events(li);

	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_assert_empty_queue(li);

	litest_event(dev, EV_ABS, ABS_X, x + 100);
	litest_event(dev, EV_ABS, ABS_Y, y + 100);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 50);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	for (i = 0; i < 10; i++) {
		x -= 200;
		y += 200;
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	libinput_dispatch(li);

	ck_assert_int_ne(libinput_next_event_type(li),
			 LIBINPUT_EVENT_NONE);
	while ((event = libinput_get_event(li)) != NULL) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	/* go back to hover */
	hover_continue(dev, 0, x, y);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_down_hover_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i, j;
	int x = 1400,
	    y = 1400;

	litest_drain_events(li);

	/* hover */
	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	for (i = 0; i < 3; i++) {
		/* touch */
		litest_event(dev, EV_ABS, ABS_X, x + 100);
		litest_event(dev, EV_ABS, ABS_Y, y + 100);
		litest_event(dev, EV_ABS, ABS_PRESSURE, 50);
		litest_event(dev, EV_KEY, BTN_TOUCH, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);

		for (j = 0; j < 5; j++) {
			x += 200;
			y += 200;
			litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
			litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
			litest_event(dev, EV_ABS, ABS_X, x);
			litest_event(dev, EV_ABS, ABS_Y, y);
			litest_event(dev, EV_SYN, SYN_REPORT, 0);
		}

		libinput_dispatch(li);

		litest_assert_only_typed_events(li,
						LIBINPUT_EVENT_POINTER_MOTION);

		/* go back to hover */
		hover_continue(dev, 0, x, y);
		litest_event(dev, EV_ABS, ABS_PRESSURE, 0);
		litest_event(dev, EV_KEY, BTN_TOUCH, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);

		for (j = 0; j < 5; j++) {
			x -= 200;
			y -= 200;
			litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
			litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
			litest_event(dev, EV_ABS, ABS_X, x);
			litest_event(dev, EV_ABS, ABS_Y, y);
			litest_event(dev, EV_SYN, SYN_REPORT, 0);
		}

		litest_assert_empty_queue(li);
	}

	/* touch */
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);

	/* start a new touch to be sure */
	litest_push_event_frame(dev);
	litest_touch_down(dev, 0, 50, 50);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 50);
	litest_pop_event_frame(dev);
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 10);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_down_up)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;
	int x = 1400,
	    y = 1400;

	litest_drain_events(li);

	/* hover two fingers, then touch */
	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	hover_start(dev, 1, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);

	/* hover first finger, end second in same frame */
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);

	litest_event(dev, EV_ABS, ABS_PRESSURE, 50);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	/* now move the finger */
	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		x -= 100;
		y -= 100;
	}

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_2fg_noevent)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;
	int x = 2400,
	    y = 2400;

	litest_drain_events(li);

	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	hover_start(dev, 1, x + 500, y + 500);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x + 500);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y + 500);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_2fg_1fg_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;
	int x = 2400,
	    y = 2400;

	litest_drain_events(li);

	/* two slots active, but BTN_TOOL_FINGER only */
	hover_start(dev, 0, x, y);
	hover_start(dev, 1, x + 500, y + 500);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 50);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x + 500);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y + 500);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_event(dev, EV_ABS, ABS_PRESSURE, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_2fg_up)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_touch_down(dev, 0, 70, 50);
	litest_touch_down(dev, 1, 50, 50);

	litest_push_event_frame(dev);
	litest_touch_move(dev, 0, 72, 50);
	litest_touch_move(dev, 1, 52, 50);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_pop_event_frame(dev);

	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_drain_events(li);
}
END_TEST

START_TEST(touchpad_hover_noevent)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_hover_start(dev, 0, 50, 50);
	litest_hover_move_to(dev, 0, 50, 50, 70, 70, 10, 10);
	litest_hover_end(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_hover_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* hover the finger */
	litest_hover_start(dev, 0, 50, 50);

	litest_hover_move_to(dev, 0, 50, 50, 70, 70, 10, 10);

	litest_assert_empty_queue(li);

	/* touch the finger on the sensor */
	litest_touch_move_to(dev, 0, 70, 70, 50, 50, 10, 10);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* go back to hover */
	litest_hover_move_to(dev, 0, 50, 50, 70, 70, 10, 10);
	litest_hover_end(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_hover_down_hover_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;

	litest_drain_events(li);

	litest_hover_start(dev, 0, 50, 50);

	for (i = 0; i < 3; i++) {

		/* hover the finger */
		litest_hover_move_to(dev, 0, 50, 50, 70, 70, 10, 10);

		litest_assert_empty_queue(li);

		/* touch the finger */
		litest_touch_move_to(dev, 0, 70, 70, 50, 50, 10, 10);

		libinput_dispatch(li);

		litest_assert_only_typed_events(li,
						LIBINPUT_EVENT_POINTER_MOTION);
	}

	litest_hover_end(dev, 0);

	/* start a new touch to be sure */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 10);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_hover_down_up)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* hover two fingers, and a touch */
	litest_push_event_frame(dev);
	litest_hover_start(dev, 0, 50, 50);
	litest_hover_start(dev, 1, 50, 50);
	litest_touch_down(dev, 2, 50, 50);
	litest_pop_event_frame(dev);

	litest_assert_empty_queue(li);

	/* hover first finger, end second and third in same frame */
	litest_push_event_frame(dev);
	litest_hover_move(dev, 0, 55, 55);
	litest_hover_end(dev, 1);
	litest_touch_up(dev, 2);
	litest_pop_event_frame(dev);

	litest_assert_empty_queue(li);

	/* now move the finger */
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 10);

	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_hover_2fg_noevent)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* hover two fingers */
	litest_push_event_frame(dev);
	litest_hover_start(dev, 0, 25, 25);
	litest_hover_start(dev, 1, 50, 50);
	litest_pop_event_frame(dev);

	litest_hover_move_two_touches(dev, 25, 25, 50, 50, 50, 50, 10, 0);

	litest_push_event_frame(dev);
	litest_hover_end(dev, 0);
	litest_hover_end(dev, 1);
	litest_pop_event_frame(dev);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_hover_2fg_1fg_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;

	litest_drain_events(li);

	/* hover two fingers */
	litest_push_event_frame(dev);
	litest_hover_start(dev, 0, 25, 25);
	litest_touch_down(dev, 1, 50, 50);
	litest_pop_event_frame(dev);

	for (i = 0; i < 10; i++) {
		litest_push_event_frame(dev);
		litest_hover_move(dev, 0, 25 + 5 * i, 25 + 5 * i);
		litest_touch_move(dev, 1, 50 + 5 * i, 50 - 5 * i);
		litest_pop_event_frame(dev);
	}

	litest_push_event_frame(dev);
	litest_hover_end(dev, 0);
	litest_touch_up(dev, 1);
	litest_pop_event_frame(dev);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_hover_1fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_hover_start(dev, 0, 50, 50);
	litest_hover_end(dev, 0);

	libinput_dispatch(li);
	litest_assert_empty_queue(li);

}
END_TEST

static void
assert_btnevent_from_device(struct litest_device *device,
			    unsigned int button,
			    enum libinput_button_state state)
{
	struct libinput *li = device->libinput;
	struct libinput_event *e;

	libinput_dispatch(li);
	e = libinput_get_event(li);
	litest_is_button_event(e, button, state);

	litest_assert_ptr_eq(libinput_event_get_device(e), device->libinput_device);
	libinput_event_destroy(e);
}

START_TEST(touchpad_trackpoint_buttons)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;

	const struct buttons {
		unsigned int device_value;
		unsigned int real_value;
	} buttons[] = {
		{ BTN_0, BTN_LEFT },
		{ BTN_1, BTN_RIGHT },
		{ BTN_2, BTN_MIDDLE },
	};
	const struct buttons *b;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);
	libinput_device_config_scroll_set_method(trackpoint->libinput_device,
					 LIBINPUT_CONFIG_SCROLL_NO_SCROLL);

	litest_drain_events(li);

	ARRAY_FOR_EACH(buttons, b) {
		litest_button_click_debounced(touchpad, li, b->device_value, true);
		assert_btnevent_from_device(trackpoint,
					    b->real_value,
					    LIBINPUT_BUTTON_STATE_PRESSED);

		litest_button_click_debounced(touchpad, li, b->device_value, false);

		assert_btnevent_from_device(trackpoint,
					    b->real_value,
					    LIBINPUT_BUTTON_STATE_RELEASED);
	}

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_mb_scroll)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);

	litest_drain_events(li);
	litest_button_click(touchpad, BTN_2, true); /* middle */
	libinput_dispatch(li);
	litest_timeout_buttonscroll();
	libinput_dispatch(li);
	litest_event(trackpoint, EV_REL, REL_Y, -2);
	litest_event(trackpoint, EV_SYN, SYN_REPORT, 0);
	litest_event(trackpoint, EV_REL, REL_Y, -2);
	litest_event(trackpoint, EV_SYN, SYN_REPORT, 0);
	litest_event(trackpoint, EV_REL, REL_Y, -2);
	litest_event(trackpoint, EV_SYN, SYN_REPORT, 0);
	litest_event(trackpoint, EV_REL, REL_Y, -2);
	litest_event(trackpoint, EV_SYN, SYN_REPORT, 0);
	litest_button_click(touchpad, BTN_2, false);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_mb_click)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;
	enum libinput_config_status status;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);
	status = libinput_device_config_scroll_set_method(
				  trackpoint->libinput_device,
				  LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_button_click_debounced(touchpad, li, BTN_2, true); /* middle */
	litest_button_click_debounced(touchpad, li, BTN_2, false);

	assert_btnevent_from_device(trackpoint,
				    BTN_MIDDLE,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(trackpoint,
				    BTN_MIDDLE,
				    LIBINPUT_BUTTON_STATE_RELEASED);
	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_buttons_softbuttons)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);

	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 95, 90);
	litest_button_click_debounced(touchpad, li, BTN_LEFT, true);
	litest_button_click_debounced(touchpad, li, BTN_1, true);
	litest_button_click_debounced(touchpad, li, BTN_LEFT, false);
	litest_touch_up(touchpad, 0);
	litest_button_click_debounced(touchpad, li, BTN_1, false);

	assert_btnevent_from_device(touchpad,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(touchpad,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_touch_down(touchpad, 0, 95, 90);
	litest_button_click_debounced(touchpad, li, BTN_LEFT, true);
	litest_button_click_debounced(touchpad, li, BTN_1, true);
	litest_button_click_debounced(touchpad, li, BTN_1, false);
	litest_button_click_debounced(touchpad, li, BTN_LEFT, false);
	litest_touch_up(touchpad, 0);

	assert_btnevent_from_device(touchpad,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);
	assert_btnevent_from_device(touchpad,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_buttons_2fg_scroll)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;
	struct libinput_event *e;
	struct libinput_event_pointer *pev;
	double val;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);

	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 49, 70);
	litest_touch_down(touchpad, 1, 51, 70);
	litest_touch_move_two_touches(touchpad, 49, 70, 51, 70, 0, -40, 10, 0);

	libinput_dispatch(li);
	litest_wait_for_event(li);

	/* Make sure we get scroll events but _not_ the scroll release */
	while ((e = libinput_get_event(li))) {
		ck_assert_int_eq(libinput_event_get_type(e),
				 LIBINPUT_EVENT_POINTER_AXIS);
		pev = libinput_event_get_pointer_event(e);
		val = libinput_event_pointer_get_axis_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert(val != 0.0);
		libinput_event_destroy(e);
	}

	litest_button_click_debounced(touchpad, li, BTN_1, true);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);

	litest_touch_move_to(touchpad, 0, 40, 30, 40, 70, 10, 0);
	litest_touch_move_to(touchpad, 1, 60, 30, 60, 70, 10, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	while ((e = libinput_get_event(li))) {
		ck_assert_int_eq(libinput_event_get_type(e),
				 LIBINPUT_EVENT_POINTER_AXIS);
		pev = libinput_event_get_pointer_event(e);
		val = libinput_event_pointer_get_axis_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert(val != 0.0);
		libinput_event_destroy(e);
	}

	litest_button_click_debounced(touchpad, li, BTN_1, false);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);

	/* the movement lags behind the touch movement, so the first couple
	   events can be downwards even though we started scrolling up. do a
	   short scroll up, drain those events, then we can use
	   litest_assert_scroll() which tests for the trailing 0/0 scroll
	   for us.
	   */
	litest_touch_move_to(touchpad, 0, 40, 70, 40, 60, 10, 0);
	litest_touch_move_to(touchpad, 1, 60, 70, 60, 60, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
	litest_touch_move_to(touchpad, 0, 40, 60, 40, 30, 10, 0);
	litest_touch_move_to(touchpad, 1, 60, 60, 60, 30, 10, 0);

	litest_touch_up(touchpad, 0);
	litest_touch_up(touchpad, 1);

	libinput_dispatch(li);

	litest_assert_scroll(li,
			     LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
			     -1);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_no_trackpoint)
{
	struct litest_device *touchpad = litest_current_device();
	struct libinput *li = touchpad->libinput;

	litest_drain_events(li);
	litest_button_click(touchpad, BTN_0, true); /* left */
	litest_button_click(touchpad, BTN_0, false);
	litest_assert_empty_queue(li);

	litest_button_click(touchpad, BTN_1, true); /* right */
	litest_button_click(touchpad, BTN_1, false);
	litest_assert_empty_queue(li);

	litest_button_click(touchpad, BTN_2, true); /* middle */
	litest_button_click(touchpad, BTN_2, false);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_initial_state)
{
	struct litest_device *dev;
	struct libinput *libinput1, *libinput2;
	struct libinput_event *ev1, *ev2;
	struct libinput_event_pointer *p1, *p2;
	int axis = _i; /* looped test */
	int x = 40, y = 60;

	dev = litest_current_device();
	libinput1 = dev->libinput;

	litest_disable_tap(dev->libinput_device);

	litest_touch_down(dev, 0, x, y);
	litest_touch_up(dev, 0);

	/* device is now on some x/y value */
	litest_drain_events(libinput1);

	libinput2 = litest_create_context();
	libinput_path_add_device(libinput2,
				 libevdev_uinput_get_devnode(dev->uinput));
	litest_drain_events(libinput2);

	if (axis == ABS_X)
		x = 30;
	else
		y = 30;
	litest_touch_down(dev, 0, x, y);
	litest_touch_move_to(dev, 0, x, y, 80, 80, 10, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(libinput1);
	libinput_dispatch(libinput2);

	litest_wait_for_event(libinput1);
	litest_wait_for_event(libinput2);

	while (libinput_next_event_type(libinput1)) {
		ev1 = libinput_get_event(libinput1);
		ev2 = libinput_get_event(libinput2);

		p1 = litest_is_motion_event(ev1);
		p2 = litest_is_motion_event(ev2);

		ck_assert_int_eq(libinput_event_get_type(ev1),
				 libinput_event_get_type(ev2));

		ck_assert_int_eq(libinput_event_pointer_get_dx(p1),
				 libinput_event_pointer_get_dx(p2));
		ck_assert_int_eq(libinput_event_pointer_get_dy(p1),
				 libinput_event_pointer_get_dy(p2));
		libinput_event_destroy(ev1);
		libinput_event_destroy(ev2);
	}

	libinput_unref(libinput2);
}
END_TEST

START_TEST(touchpad_dwt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* within timeout - no events */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();
	libinput_dispatch(li);

	/* after timeout  - motion events*/
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_ext_and_int_keyboard)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard, *yubikey;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	litest_disable_tap(touchpad->libinput_device);

	/* Yubikey is initialized first */
	yubikey = litest_add_device(li, LITEST_YUBIKEY);
	litest_drain_events(li);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* within timeout - no events */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();
	libinput_dispatch(li);

	/* after timeout  - motion events*/
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
	litest_delete_device(yubikey);
}
END_TEST

START_TEST(touchpad_dwt_enable_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* finger down after last key event, but
	   we're still within timeout - no events */
	msleep(10);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();
	libinput_dispatch(li);

	/* same touch after timeout  - motion events*/
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_touch_hold)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	msleep(1); /* make sure touch starts after key press */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* touch still down - no events */
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_touch_move_to(touchpad, 0, 70, 50, 30, 50, 5, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* touch still down - no events */
	litest_timeout_dwt_short();
	libinput_dispatch(li);
	litest_touch_move_to(touchpad, 0, 30, 50, 50, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_key_hold)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_key_hold_timeout)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	litest_timeout_dwt_long();
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_empty_queue(li);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	/* key is up, but still within timeout */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	/* expire timeout */
	litest_timeout_dwt_long();
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_key_hold_timeout_existing_touch_cornercase)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	/* Note: this tests for the current behavior of a cornercase, and
	 * the behaviour is essentially a bug. If this test fails it may be
	 * because the buggy behavior was fixed.
	 */

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	litest_timeout_dwt_long();
	libinput_dispatch(li);

	/* Touch starting after re-issuing the dwt timeout */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);

	litest_assert_empty_queue(li);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	/* key is up, but still within timeout */
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 5, 1);
	litest_assert_empty_queue(li);

	/* Expire dwt timeout. Because the touch started after re-issuing
	 * the last timeout, it looks like the touch started after the last
	 * key press. Such touches are enabled for pointer motion by
	 * libinput when dwt expires.
	 * This is buggy behavior and not what a user would typically
	 * expect. But it's hard to trigger in real life too.
	 */
	litest_timeout_dwt_long();
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	/* If the below check for motion event fails because no events are
	 * in the pipe, the buggy behavior was fixed and this test case
	 * can be removed */
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_key_hold_timeout_existing_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	libinput_dispatch(li);
	litest_timeout_dwt_long();
	libinput_dispatch(li);

	litest_assert_empty_queue(li);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	/* key is up, but still within timeout */
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 5, 1);
	litest_assert_empty_queue(li);

	/* expire timeout, but touch started before release */
	litest_timeout_dwt_long();
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_type)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	int i;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	for (i = 0; i < 5; i++) {
		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		libinput_dispatch(li);
	}

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_long();
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_type_short_timeout)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	int i;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	for (i = 0; i < 5; i++) {
		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		libinput_dispatch(li);
	}

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_modifier_no_dwt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	unsigned int modifiers[] = {
		KEY_LEFTCTRL,
		KEY_RIGHTCTRL,
		KEY_LEFTALT,
		KEY_RIGHTALT,
		KEY_LEFTSHIFT,
		KEY_RIGHTSHIFT,
		KEY_FN,
		KEY_CAPSLOCK,
		KEY_TAB,
		KEY_COMPOSE,
		KEY_RIGHTMETA,
		KEY_LEFTMETA,
	};
	unsigned int *key;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	ARRAY_FOR_EACH(modifiers, key) {
		litest_keyboard_key(keyboard, *key, true);
		litest_keyboard_key(keyboard, *key, false);
		libinput_dispatch(li);

		litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

		litest_touch_down(touchpad, 0, 50, 50);
		litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
		litest_touch_up(touchpad, 0);
		litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	}

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_modifier_combo_no_dwt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	unsigned int modifiers[] = {
		KEY_LEFTCTRL,
		KEY_RIGHTCTRL,
		KEY_LEFTALT,
		KEY_RIGHTALT,
		KEY_LEFTSHIFT,
		KEY_RIGHTSHIFT,
		KEY_FN,
		KEY_CAPSLOCK,
		KEY_TAB,
		KEY_COMPOSE,
		KEY_RIGHTMETA,
		KEY_LEFTMETA,
	};
	unsigned int *key;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	ARRAY_FOR_EACH(modifiers, key) {
		litest_keyboard_key(keyboard, *key, true);
		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		litest_keyboard_key(keyboard, KEY_B, true);
		litest_keyboard_key(keyboard, KEY_B, false);
		litest_keyboard_key(keyboard, *key, false);
		libinput_dispatch(li);

		litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

		litest_touch_down(touchpad, 0, 50, 50);
		litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
		litest_touch_up(touchpad, 0);
		litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	}

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_modifier_combo_dwt_after)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	unsigned int modifiers[] = {
		KEY_LEFTCTRL,
		KEY_RIGHTCTRL,
		KEY_LEFTALT,
		KEY_RIGHTALT,
		KEY_LEFTSHIFT,
		KEY_RIGHTSHIFT,
		KEY_FN,
		KEY_CAPSLOCK,
		KEY_TAB,
		KEY_COMPOSE,
		KEY_RIGHTMETA,
		KEY_LEFTMETA,
	};
	unsigned int *key;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	ARRAY_FOR_EACH(modifiers, key) {
		litest_keyboard_key(keyboard, *key, true);
		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		litest_keyboard_key(keyboard, *key, false);
		libinput_dispatch(li);

		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		libinput_dispatch(li);
		litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

		litest_touch_down(touchpad, 0, 50, 50);
		litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
		litest_touch_up(touchpad, 0);
		litest_assert_empty_queue(li);

		litest_timeout_dwt_long();
		libinput_dispatch(li);
	}

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_modifier_combo_dwt_remains)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	unsigned int modifiers[] = {
		KEY_LEFTCTRL,
		KEY_RIGHTCTRL,
		KEY_LEFTALT,
		KEY_RIGHTALT,
		KEY_LEFTSHIFT,
		KEY_RIGHTSHIFT,
		KEY_FN,
		KEY_CAPSLOCK,
		KEY_TAB,
		KEY_COMPOSE,
		KEY_RIGHTMETA,
		KEY_LEFTMETA,
	};
	unsigned int *key;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	ARRAY_FOR_EACH(modifiers, key) {
		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		libinput_dispatch(li);

		/* this can't really be tested directly. The above key
		 * should enable dwt, the next key continues and extends the
		 * timeout as usual (despite the modifier combo). but
		 * testing for timeout differences is fickle, so all we can
		 * test though is that dwt is still on after the modifier
		 * combo and does not get disabled immediately.
		 */
		litest_keyboard_key(keyboard, *key, true);
		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		litest_keyboard_key(keyboard, *key, false);
		libinput_dispatch(li);

		litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

		litest_touch_down(touchpad, 0, 50, 50);
		litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
		litest_touch_up(touchpad, 0);
		litest_assert_empty_queue(li);

		litest_timeout_dwt_long();
		libinput_dispatch(li);
	}

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_fkeys_no_dwt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	unsigned int key;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	for (key = KEY_F1; key < KEY_CNT; key++) {
		if (!libinput_device_keyboard_has_key(keyboard->libinput_device,
						      key))
			continue;

		litest_keyboard_key(keyboard, key, true);
		litest_keyboard_key(keyboard, key, false);
		libinput_dispatch(li);

		litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

		litest_touch_down(touchpad, 0, 50, 50);
		litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
		litest_touch_up(touchpad, 0);
		litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	}

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_tap)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_enable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_up(touchpad, 0);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_timeout_dwt_short();
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_tap_drag)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_enable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	msleep(1); /* make sure touch starts after key press */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_up(touchpad, 0);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_timeout_dwt_short();
	libinput_dispatch(li);
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_click)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_button_click(touchpad, BTN_LEFT, true);
	litest_button_click(touchpad, BTN_LEFT, false);
	libinput_dispatch(li);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_keyboard_key(keyboard, KEY_A, false);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_edge_scroll)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	litest_enable_edge_scroll(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 99, 20);
	libinput_dispatch(li);
	litest_timeout_edgescroll();
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	/* edge scroll timeout is 300ms atm, make sure we don't accidentally
	   exit the DWT timeout */
	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_move_to(touchpad, 0, 99, 20, 99, 80, 60, 10);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_touch_move_to(touchpad, 0, 99, 80, 99, 20, 60, 10);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_edge_scroll_interrupt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	struct libinput_event_pointer *stop_event;

	if (!has_disable_while_typing(touchpad))
		return;

	litest_enable_edge_scroll(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 99, 20);
	libinput_dispatch(li);
	litest_timeout_edgescroll();
	litest_touch_move_to(touchpad, 0, 99, 20, 99, 30, 10, 10);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);

	/* scroll stop event */
	litest_wait_for_event(li);
	stop_event = litest_is_axis_event(libinput_get_event(li),
					  LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
					  LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
	libinput_event_destroy(libinput_event_pointer_get_base_event(stop_event));
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_timeout_dwt_long();

	/* Known bad behavior: a touch starting to edge-scroll before dwt
	 * kicks in will stop to scroll but be recognized as normal
	 * pointer-moving touch once the timeout expires. We'll fix that
	 * when we need to.
	 */
	litest_touch_move_to(touchpad, 0, 99, 30, 99, 80, 10, 5);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_config_default_on)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	enum libinput_config_dwt_state state;

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_WACOM ||
	    libevdev_get_id_bustype(dev->evdev) == BUS_BLUETOOTH) {
		ck_assert(!libinput_device_config_dwt_is_available(device));
		return;
	}

	ck_assert(libinput_device_config_dwt_is_available(device));
	state = libinput_device_config_dwt_get_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DWT_ENABLED);
	state = libinput_device_config_dwt_get_default_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DWT_ENABLED);

	status = libinput_device_config_dwt_set_enabled(device,
					LIBINPUT_CONFIG_DWT_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_dwt_set_enabled(device,
					LIBINPUT_CONFIG_DWT_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_dwt_set_enabled(device, 3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(touchpad_dwt_config_default_off)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	enum libinput_config_dwt_state state;

	ck_assert(!libinput_device_config_dwt_is_available(device));
	state = libinput_device_config_dwt_get_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DWT_DISABLED);
	state = libinput_device_config_dwt_get_default_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DWT_DISABLED);

	status = libinput_device_config_dwt_set_enabled(device,
					LIBINPUT_CONFIG_DWT_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_dwt_set_enabled(device,
					LIBINPUT_CONFIG_DWT_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_dwt_set_enabled(device, 3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

static inline void
disable_dwt(struct litest_device *dev)
{
	enum libinput_config_status status,
				    expected = LIBINPUT_CONFIG_STATUS_SUCCESS;
	status = libinput_device_config_dwt_set_enabled(dev->libinput_device,
						LIBINPUT_CONFIG_DWT_DISABLED);
	litest_assert_int_eq(status, expected);
}

static inline void
enable_dwt(struct litest_device *dev)
{
	enum libinput_config_status status,
				    expected = LIBINPUT_CONFIG_STATUS_SUCCESS;
	status = libinput_device_config_dwt_set_enabled(dev->libinput_device,
						LIBINPUT_CONFIG_DWT_ENABLED);
	litest_assert_int_eq(status, expected);
}

START_TEST(touchpad_dwt_disabled)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	disable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_disable_during_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_empty_queue(li);

	disable_dwt(touchpad);

	/* touch already down -> keeps being ignored */
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 70, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_disable_before_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	disable_dwt(touchpad);
	libinput_dispatch(li);

	/* touch down during timeout -> still discarded */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_disable_during_key_release)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	disable_dwt(touchpad);
	libinput_dispatch(li);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* touch down during timeout, wait, should generate events */
	litest_touch_down(touchpad, 0, 50, 50);
	libinput_dispatch(li);
	litest_timeout_dwt_long();
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_disable_during_key_hold)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	disable_dwt(touchpad);
	libinput_dispatch(li);

	/* touch down during timeout, wait, should generate events */
	litest_touch_down(touchpad, 0, 50, 50);
	libinput_dispatch(li);
	litest_timeout_dwt_long();
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_enable_during_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	disable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	enable_dwt(touchpad);

	/* touch already down -> still sends events */
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 70, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_enable_before_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	disable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	enable_dwt(touchpad);
	libinput_dispatch(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_enable_during_tap)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	litest_enable_tap(touchpad->libinput_device);
	disable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	libinput_dispatch(li);
	enable_dwt(touchpad);
	libinput_dispatch(li);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_remove_kbd_while_active)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	litest_enable_tap(touchpad->libinput_device);
	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);

	litest_touch_down(touchpad, 0, 50, 50);
	libinput_dispatch(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_drain_events(li);

	litest_delete_device(keyboard);
	litest_drain_events(li);

	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

}
END_TEST

START_TEST(touchpad_dwt_apple)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *apple_keyboard;
	struct libinput *li = touchpad->libinput;

	ck_assert(has_disable_while_typing(touchpad));

	apple_keyboard = litest_add_device(li, LITEST_APPLE_KEYBOARD);
	litest_drain_events(li);

	litest_keyboard_key(apple_keyboard, KEY_A, true);
	litest_keyboard_key(apple_keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_delete_device(apple_keyboard);
}
END_TEST

START_TEST(touchpad_dwt_acer_hawaii)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard, *hawaii_keyboard;
	struct libinput *li = touchpad->libinput;

	ck_assert(has_disable_while_typing(touchpad));

	/* Only the hawaii keyboard can trigger DWT */
	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	hawaii_keyboard = litest_add_device(li, LITEST_ACER_HAWAII_KEYBOARD);
	litest_drain_events(li);

	litest_keyboard_key(hawaii_keyboard, KEY_A, true);
	litest_keyboard_key(hawaii_keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
	litest_delete_device(hawaii_keyboard);
}
END_TEST

START_TEST(touchpad_dwt_multiple_keyboards)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *k1, *k2;
	struct libinput *li = touchpad->libinput;

	ck_assert(has_disable_while_typing(touchpad));

	enable_dwt(touchpad);

	k1 = litest_add_device(li, LITEST_KEYBOARD);
	k2 = litest_add_device(li, LITEST_KEYBOARD);

	litest_keyboard_key(k1, KEY_A, true);
	litest_keyboard_key(k1, KEY_A, false);
	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();

	litest_keyboard_key(k2, KEY_A, true);
	litest_keyboard_key(k2, KEY_A, false);
	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();

	litest_delete_device(k1);
	litest_delete_device(k2);
}
END_TEST

START_TEST(touchpad_dwt_multiple_keyboards_bothkeys)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *k1, *k2;
	struct libinput *li = touchpad->libinput;

	ck_assert(has_disable_while_typing(touchpad));

	enable_dwt(touchpad);

	k1 = litest_add_device(li, LITEST_KEYBOARD);
	k2 = litest_add_device(li, LITEST_KEYBOARD);

	litest_keyboard_key(k1, KEY_A, true);
	litest_keyboard_key(k1, KEY_A, false);
	litest_keyboard_key(k2, KEY_B, true);
	litest_keyboard_key(k2, KEY_B, false);
	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(k1);
	litest_delete_device(k2);
}
END_TEST

START_TEST(touchpad_dwt_multiple_keyboards_bothkeys_modifier)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *k1, *k2;
	struct libinput *li = touchpad->libinput;

	ck_assert(has_disable_while_typing(touchpad));

	enable_dwt(touchpad);

	k1 = litest_add_device(li, LITEST_KEYBOARD);
	k2 = litest_add_device(li, LITEST_KEYBOARD);

	litest_keyboard_key(k1, KEY_RIGHTCTRL, true);
	litest_keyboard_key(k1, KEY_RIGHTCTRL, false);
	litest_keyboard_key(k2, KEY_B, true);
	litest_keyboard_key(k2, KEY_B, false);
	litest_drain_events(li);

	/* If the keyboard is a single physical device, the above should
	 * trigger the modifier behavior for dwt. But libinput views it as
	 * two separate devices and this is such a niche case that it
	 * doesn't matter. So we test for the easy behavior:
	 * ctrl+B across two devices is *not* a dwt modifier combo
	 */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(k1);
	litest_delete_device(k2);
}
END_TEST

START_TEST(touchpad_dwt_multiple_keyboards_remove)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboards[2];
	struct libinput *li = touchpad->libinput;
	int which = _i; /* ranged test */
	struct litest_device *removed, *remained;

	ck_assert_int_le(which, 1);

	ck_assert(has_disable_while_typing(touchpad));

	enable_dwt(touchpad);

	keyboards[0] = litest_add_device(li, LITEST_KEYBOARD);
	keyboards[1] = litest_add_device(li, LITEST_KEYBOARD);

	litest_keyboard_key(keyboards[0], KEY_A, true);
	litest_keyboard_key(keyboards[0], KEY_A, false);
	litest_keyboard_key(keyboards[1], KEY_B, true);
	litest_keyboard_key(keyboards[1], KEY_B, false);
	litest_drain_events(li);

	litest_timeout_dwt_short();

	removed = keyboards[which % 2];
	remained = keyboards[(which + 1) % 2];

	litest_delete_device(removed);
	litest_keyboard_key(remained, KEY_C, true);
	litest_keyboard_key(remained, KEY_C, false);
	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(remained);
}
END_TEST

static int
has_thumb_detect(struct litest_device *dev)
{
	double w, h;

	if (!libevdev_has_event_code(dev->evdev, EV_ABS, ABS_MT_PRESSURE))
		return 0;

	if (libinput_device_get_size(dev->libinput_device, &w, &h) != 0)
		return 0;

	return h >= 50.0;
}

START_TEST(touchpad_thumb_begin_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 99, axes);
	litest_touch_move_to(dev, 0, 50, 99, 80, 99, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_thumb_update_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	litest_disable_tap(dev->libinput_device);
	litest_enable_clickfinger(dev);

	if (!has_thumb_detect(dev))
		return;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 59, 99);
	litest_touch_move_extended(dev, 0, 59, 99, axes);
	litest_touch_move_to(dev, 0, 60, 99, 80, 99, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_thumb_moving)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	litest_disable_tap(dev->libinput_device);
	litest_enable_clickfinger(dev);

	if (!has_thumb_detect(dev))
		return;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 99);
	litest_touch_move_to(dev, 0, 50, 99, 60, 99, 10, 0);
	litest_touch_move_extended(dev, 0, 65, 99, axes);
	litest_touch_move_to(dev, 0, 65, 99, 80, 99, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_thumb_moving_empty_slots)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_disable_tap(dev->libinput_device);
	litest_enable_2fg_scroll(dev);

	if (libevdev_get_num_slots(dev->evdev) < 3)
		return;

	litest_drain_events(li);

	/* exceed the speed movement threshold in slot 0 */
	litest_touch_down(dev, 0, 50, 20);
	litest_touch_move_to(dev, 0, 50, 20, 70, 99, 15, 0);
	litest_touch_up(dev, 0);

	litest_drain_events(li);

	/* scroll in slots 1 and 2 */
	litest_touch_down(dev, 1, 50, 50);
	litest_touch_down(dev, 2, 90, 50);
	libinput_dispatch(li);
	for (int i = 0, y = 50; i < 10; i++, y++) {
		litest_touch_move_to(dev, 1, 50, y, 50, y + 1, 1, 0);
		litest_touch_move_to(dev, 2, 50, y, 50, y + 1, 1, 0);
	}
	libinput_dispatch(li);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 2);
	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 2);

}
END_TEST

START_TEST(touchpad_thumb_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	libinput_device_config_click_set_method(dev->libinput_device,
						LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 99);
	litest_touch_down(dev, 1, 60, 99);
	litest_touch_move_extended(dev, 0, 55, 99, axes);
	litest_button_click(dev, BTN_LEFT, true);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

	litest_button_click(dev, BTN_LEFT, false);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 99);
	litest_touch_down(dev, 1, 60, 99);
	litest_touch_move_extended(dev, 1, 65, 99, axes);
	litest_button_click(dev, BTN_LEFT, true);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_LEFT,
			       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_thumb_btnarea)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	libinput_device_config_click_set_method(dev->libinput_device,
						LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 99);
	litest_touch_move_extended(dev, 0, 95, 99, axes);
	litest_button_click(dev, BTN_LEFT, true);

	/* button areas work as usual with a thumb */

	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_RIGHT,
			       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_thumb_tap_begin)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_enable_clickfinger(dev);
	litest_drain_events(li);

	/* touch down is a thumb */
	litest_touch_down_extended(dev, 0, 50, 99, axes);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();

	litest_assert_empty_queue(li);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_tap_touch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_enable_clickfinger(dev);
	litest_drain_events(li);

	/* event after touch down is thumb */
	litest_touch_down(dev, 0, 50, 80);
	litest_touch_move_extended(dev, 0, 51, 99, axes);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();
	litest_assert_empty_queue(li);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_tap_hold)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_enable_clickfinger(dev);
	litest_drain_events(li);

	/* event in state HOLD is thumb */
	litest_touch_down(dev, 0, 50, 99);
	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);
	litest_touch_move_extended(dev, 0, 51, 99, axes);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_tap_hold_2ndfg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_enable_clickfinger(dev);
	litest_drain_events(li);

	/* event in state HOLD is thumb */
	litest_touch_down(dev, 0, 50, 99);
	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);
	litest_touch_move_extended(dev, 0, 51, 99, axes);

	litest_assert_empty_queue(li);

	/* one finger is a thumb, now get second finger down */
	litest_touch_down(dev, 1, 60, 50);
	litest_assert_empty_queue(li);

	/* release thumb */
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	/* timeout -> into HOLD, no event on release */
	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);
	litest_touch_up(dev, 1);
	litest_assert_empty_queue(li);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_tap_hold_2ndfg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* event in state HOLD is thumb */
	litest_touch_down(dev, 0, 50, 99);
	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);
	litest_touch_move_extended(dev, 0, 51, 99, axes);

	litest_assert_empty_queue(li);

	/* one finger is a thumb, now get second finger down */
	litest_touch_down(dev, 1, 60, 50);
	litest_assert_empty_queue(li);

	/* release thumb */
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	/* release second finger, within timeout, ergo event */
	litest_touch_up(dev, 1);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_LEFT,
			       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);

	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_LEFT,
			       LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 99);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_move_and_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 75 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* trigger thumb detection by pressure after a slight movement */
	litest_touch_down(dev, 0, 50, 99);
	litest_touch_move(dev, 0, 51, 99);
	litest_touch_move_extended(dev, 0, 55, 99, axes);
	libinput_dispatch(li);

	litest_assert_empty_queue(li);

	/* thumb is resting, check if tapping still works */
	litest_touch_down(dev, 1, 50, 50);
	litest_touch_up(dev, 1);
	libinput_dispatch(li);
	litest_timeout_tap();

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_tool_tripletap_touch_count)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	/* Synaptics touchpads sometimes end one touch point while
	 * simultaneously setting BTN_TOOL_TRIPLETAP.
	 * https://bugs.freedesktop.org/show_bug.cgi?id=91352
	 */
	litest_drain_events(li);
	litest_enable_clickfinger(dev);

	/* touch 1 down */
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, 1);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, 1200);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, 3200);
	litest_event(dev, EV_ABS, ABS_MT_PRESSURE, 78);
	litest_event(dev, EV_ABS, ABS_X, 1200);
	litest_event(dev, EV_ABS, ABS_Y, 3200);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 78);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	msleep(2);

	/* touch 2 down */
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, 1);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, 2200);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, 3200);
	litest_event(dev, EV_ABS, ABS_MT_PRESSURE, 73);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	msleep(2);

	/* touch 3 down, coordinate jump + ends slot 1 */
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, 4000);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, 4000);
	litest_event(dev, EV_ABS, ABS_MT_PRESSURE, 78);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_X, 4000);
	litest_event(dev, EV_ABS, ABS_Y, 4000);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 78);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	msleep(2);

	/* slot 2 reactivated:
	 * Note, slot is activated close enough that we don't accidentally
	 * trigger the clickfinger distance check, remains to be seen if
	 * that is true for real-world interaction.
	 */
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, 4000);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, 4000);
	litest_event(dev, EV_ABS, ABS_MT_PRESSURE, 78);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, 3);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, 3500);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, 3500);
	litest_event(dev, EV_ABS, ABS_MT_PRESSURE, 73);
	litest_event(dev, EV_ABS, ABS_X, 4000);
	litest_event(dev, EV_ABS, ABS_Y, 4000);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 78);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	msleep(2);

	/* now a click should trigger middle click */
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_wait_for_event(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_MIDDLE,
			       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_MIDDLE,
			       LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);

	/* release everything */
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
}
END_TEST

START_TEST(touchpad_slot_swap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	int first, second;

	/* Synaptics touchpads sometimes end the wrong touchpoint on finger
	 * up, causing the remaining slot to continue with the other slot's
	 * coordinates.
	 * https://bugs.freedesktop.org/show_bug.cgi?id=91352
	 */
	litest_drain_events(li);

	for (first = 0; first <= 1; first++) {
		second = 1 - first;

		litest_touch_down(dev, 0, 50, 50);
		libinput_dispatch(li);
		litest_touch_down(dev, 1, 70, 70);
		libinput_dispatch(li);

		litest_touch_move_to(dev, first, 50, 50, 50, 30, 10, 1);
		litest_drain_events(li);

		/* release touch 0, continue other slot with 0's coords */
		litest_push_event_frame(dev);
		litest_touch_up(dev, first);
		litest_touch_move(dev, second, 50, 30.1);
		litest_pop_event_frame(dev);
		libinput_dispatch(li);
		litest_touch_move_to(dev, second, 50, 30, 50, 11, 10, 1);
		libinput_dispatch(li);
		event = libinput_get_event(li);
		do {
			ptrev = litest_is_motion_event(event);
			ck_assert_double_eq(libinput_event_pointer_get_dx(ptrev), 0.0);
			ck_assert_double_lt(libinput_event_pointer_get_dy(ptrev), 1.0);

			libinput_event_destroy(event);
			event = libinput_get_event(li);
		} while (event);
		litest_assert_empty_queue(li);

		litest_touch_up(dev, second);
	}
}
END_TEST

START_TEST(touchpad_finger_always_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li;

	/* Set BTN_TOOL_FINGER before a new context is initialized */
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	li = litest_create_context();
	libinput_path_add_device(li,
				 libevdev_uinput_get_devnode(dev->uinput));
	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 50, 10, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	libinput_unref(li);
}
END_TEST

START_TEST(touchpad_time_usec)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 50, 5, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert_notnull(event);

	while (event) {
		uint64_t utime;

		ptrev = litest_is_motion_event(event);
		utime = libinput_event_pointer_get_time_usec(ptrev);

		ck_assert_int_eq(libinput_event_pointer_get_time(ptrev),
				 (uint32_t) (utime / 1000));
		libinput_event_destroy(event);
		event = libinput_get_event(li);
	}
}
END_TEST

START_TEST(touchpad_jump_finger_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_drain_events(li);

	litest_disable_log_handler(li);
	litest_touch_move_to(dev, 0, 90, 30, 20, 80, 1, 0);
	litest_assert_empty_queue(li);
	litest_restore_log_handler(li);

	litest_touch_move_to(dev, 0, 20, 80, 21, 81, 10, 0);
	litest_touch_up(dev, 0);

	/* expect lots of little events, no big jump */
	libinput_dispatch(li);
	event = libinput_get_event(li);
	do {
		double dx, dy;

		ptrev = litest_is_motion_event(event);
		dx = libinput_event_pointer_get_dx(ptrev);
		dy = libinput_event_pointer_get_dy(ptrev);
		ck_assert_int_lt(abs((int)dx), 20);
		ck_assert_int_lt(abs((int)dy), 20);

		libinput_event_destroy(event);
		event = libinput_get_event(li);
	} while (event != NULL);
}
END_TEST

START_TEST(touchpad_disabled_on_mouse)
{
	struct litest_device *dev = litest_current_device();
	struct litest_device *mouse;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(
			     dev->libinput_device,
			     LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	mouse = litest_add_device(li, LITEST_MOUSE);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_ADDED);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(mouse);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_disabled_on_mouse_suspend_mouse)
{
	struct litest_device *dev = litest_current_device();
	struct litest_device *mouse;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(
			     dev->libinput_device,
			     LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	mouse = litest_add_device(li, LITEST_MOUSE);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_ADDED);

	/* Disable external mouse -> expect touchpad events */
	status = libinput_device_config_send_events_set_mode(
			     mouse->libinput_device,
			     LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(mouse);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_disabled_double_mouse)
{
	struct litest_device *dev = litest_current_device();
	struct litest_device *mouse1, *mouse2;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(
			     dev->libinput_device,
			     LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	mouse1 = litest_add_device(li, LITEST_MOUSE);
	mouse2 = litest_add_device(li, LITEST_MOUSE_LOW_DPI);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_ADDED);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(mouse1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(mouse2);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_disabled_double_mouse_one_suspended)
{
	struct litest_device *dev = litest_current_device();
	struct litest_device *mouse1, *mouse2;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(
			     dev->libinput_device,
			     LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	mouse1 = litest_add_device(li, LITEST_MOUSE);
	mouse2 = litest_add_device(li, LITEST_MOUSE_LOW_DPI);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_ADDED);

	/* Disable one external mouse -> don't expect touchpad events */
	status = libinput_device_config_send_events_set_mode(
			     mouse1->libinput_device,
			     LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(mouse1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(mouse2);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_move_to(dev, 0, 20, 30, 90, 30, 10, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

static inline bool
touchpad_has_pressure(struct litest_device *dev)
{
	struct libevdev *evdev = dev->evdev;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_PRESSURE))
		return true;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_PRESSURE) &&
	    !libevdev_has_event_code(evdev, EV_ABS, ABS_MT_SLOT))
		return true;

	return false;
}

START_TEST(touchpad_pressure)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 1 },
		{ ABS_PRESSURE, 1 },
		{ -1, 0 }
	};
	double pressure; /* in percent */
	double threshold = 12.0;

	if (!touchpad_has_pressure(dev))
		return;

	litest_drain_events(li);

	for (pressure = 1; pressure <= threshold + 1; pressure++) {
		litest_axis_set_value(axes, ABS_MT_PRESSURE, pressure);
		litest_axis_set_value(axes, ABS_PRESSURE, pressure);
		litest_touch_down_extended(dev, 0, 50, 50, axes);
		litest_touch_move_to_extended(dev, 0,
					      50, 50,
					      80, 80,
					      axes, 10, 1);
		litest_touch_up(dev, 0);
		if (pressure < threshold)
			litest_assert_empty_queue(li);
		else
			litest_assert_only_typed_events(li,
							LIBINPUT_EVENT_POINTER_MOTION);

	}
}
END_TEST

START_TEST(touchpad_pressure_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 5 },
		{ ABS_PRESSURE, 5 },
		{ -1, 0 }
	};

	if (!touchpad_has_pressure(dev))
		return;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 30, 50);
	litest_touch_down_extended(dev, 1, 50, 50, axes);
	libinput_dispatch(li);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 10, 1);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);
	litest_touch_move_to_extended(dev, 1,
				      50, 50,
				      80, 80,
				      axes, 10, 1);
	litest_assert_empty_queue(li);
	litest_touch_move_to(dev, 0, 80, 80, 20, 50, 10, 1);
	litest_touch_move_to_extended(dev, 1,
				      80, 80,
				      50, 50,
				      axes, 10, 1);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_pressure_2fg_st)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 5 },
		{ ABS_PRESSURE, 5 },
		{ -1, 0 }
	};

	if (!touchpad_has_pressure(dev))
		return;

	/* This is a bit of a weird test. We expect two fingers to be down as
	 * soon as doubletap is set, regardless of pressure. But we don't
	 * have 2fg scrolling on st devices and 2 fingers down on a touchpad
	 * without 2fg scrolling simply does not generate events. But that's
	 * the same result as if the fingers were ignored because of
	 * pressure and we cannot know the difference.
	 * So this test only keeps your CPU warm, not much else.
	 */
	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 50, axes);
	libinput_dispatch(li);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_touch_move_to_extended(dev, 0,
				      50, 50,
				      80, 80,
				      axes, 10, 1);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_pressure_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 5 },
		{ ABS_PRESSURE, 5 },
		{ -1, 0 }
	};

	if (!touchpad_has_pressure(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 50, axes);
	libinput_dispatch(li);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_pressure_tap_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 5 },
		{ ABS_PRESSURE, 5 },
		{ -1, 0 }
	};

	if (!touchpad_has_pressure(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* tap but too light */
	litest_touch_down_extended(dev, 0, 40, 50, axes);
	litest_touch_down_extended(dev, 1, 50, 50, axes);
	libinput_dispatch(li);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_pressure_tap_2fg_1fg_light)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 5 },
		{ ABS_PRESSURE, 5 },
		{ -1, 0 }
	};

	if (!touchpad_has_pressure(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* double-tap with one finger too light */
	litest_touch_down(dev, 0, 40, 50);
	litest_touch_down_extended(dev, 1, 50, 50, axes);
	libinput_dispatch(li);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_LEFT,
			       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);

	litest_timeout_tap();
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_LEFT,
			       LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(touchpad_pressure_btntool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 5 },
		{ ABS_PRESSURE, 5 },
		{ -1, 0 }
	};

	/* we only have tripletap, can't test 4 slots because nothing will
	 * happen */
	if (libevdev_get_num_slots(dev->evdev) != 2)
		return;

	if (!touchpad_has_pressure(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* Two light touches down, doesn't count */
	litest_touch_down_extended(dev, 0, 40, 50, axes);
	litest_touch_down_extended(dev, 1, 45, 50, axes);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	/* Tripletap but since no finger is logically down, it doesn't count */
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	/* back to two fingers */
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	/* make one finger real */
	litest_touch_move_to(dev, 0, 40, 50, 41, 52, 10, 10);
	litest_drain_events(li);

	/* tripletap should now be 3 fingers tap */
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_pressure_semi_mt_2fg_goes_light)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_PRESSURE, 2 },
		{ -1, 0 }
	};

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 40, 50);
	litest_touch_down(dev, 1, 60, 50);
	litest_touch_move_two_touches(dev, 40, 50, 60, 50, 0, -20, 10, 0);

	/* This should trigger a scroll end event */
	litest_push_event_frame(dev);
	litest_touch_move_extended(dev, 0, 40, 31, axes);
	litest_touch_move_extended(dev, 1, 60, 31, axes);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);

	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 0);

	litest_push_event_frame(dev);
	litest_touch_move_extended(dev, 0, 40, 35, axes);
	litest_touch_move_extended(dev, 1, 60, 35, axes);
	litest_pop_event_frame(dev);

	litest_push_event_frame(dev);
	litest_touch_move_extended(dev, 0, 40, 40, axes);
	litest_touch_move_extended(dev, 1, 60, 40, axes);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_touch_size)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_TOUCH_MAJOR, 0 },
		{ ABS_MT_TOUCH_MINOR, 0 },
		{ ABS_MT_ORIENTATION, 0 },
		{ -1, 0 }
	};

	if (!touchpad_has_touch_size(dev))
		return;

	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_MT_TOUCH_MAJOR, 1);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MINOR, 1);
	litest_touch_down_extended(dev, 0, 50, 50, axes);
	litest_touch_move_to_extended(dev, 0,
				      50, 50,
				      80, 80,
				      axes, 10, 1);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_axis_set_value(axes, ABS_MT_TOUCH_MAJOR, 15);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MINOR, 15);
	litest_touch_down_extended(dev, 0, 50, 50, axes);
	litest_touch_move_to_extended(dev, 0,
				      50, 50,
				      80, 80,
				      axes, 10, 1);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_touch_size_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_TOUCH_MAJOR, 0 },
		{ ABS_MT_TOUCH_MINOR, 0 },
		{ ABS_MT_ORIENTATION, 0 },
		{ -1, 0 }
	};

	if (!touchpad_has_touch_size(dev))
		return;

	litest_drain_events(li);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MAJOR, 15);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MINOR, 15);
	litest_touch_down_extended(dev, 0, 50, 50, axes);
	litest_touch_move_to_extended(dev, 0,
				      50, 50,
				      80, 80,
				      axes, 10, 1);

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

	litest_axis_set_value(axes, ABS_MT_TOUCH_MAJOR, 1);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MINOR, 1);
	litest_touch_down_extended(dev, 1, 70, 70, axes);
	litest_touch_move_to_extended(dev, 1,
				      70, 70,
				      80, 90,
				      axes, 10, 1);
	litest_assert_empty_queue(li);

	litest_axis_set_value(axes, ABS_MT_TOUCH_MAJOR, 15);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MINOR, 15);
	litest_touch_move_to_extended(dev, 0,
				      80, 80,
				      50, 50,
				      axes, 10, 1);

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_palm_detect_touch_size)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_TOUCH_MAJOR, 0 },
		{ ABS_MT_TOUCH_MINOR, 0 },
		{ -1, 0 }
	};

	if (!touchpad_has_touch_size(dev) ||
	    litest_touchpad_is_external(dev))
		return;

	litest_drain_events(li);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MAJOR, 30);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MINOR, 30);
	litest_touch_down_extended(dev, 0, 50, 50, axes);
	litest_touch_move_to_extended(dev, 0,
				      50, 50,
				      80, 80,
				      axes, 10, 1);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

	litest_axis_set_value_unchecked(axes, ABS_MT_TOUCH_MAJOR, 90);
	litest_axis_set_value(axes, ABS_MT_TOUCH_MINOR, 90);
	litest_touch_move_to_extended(dev, 0,
				      80, 80,
				      50, 50,
				      axes, 10, 1);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_speed_ignore_finger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (litest_has_clickfinger(dev))
		litest_enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 20);
	litest_touch_move_to(dev, 0, 20, 20, 85, 80, 20, 0);
	litest_touch_down(dev, 1, 20, 80);
	litest_touch_move_two_touches(dev, 85, 80, 20, 80, -20, -20, 10, 0);
	libinput_dispatch(li);

	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_speed_allow_nearby_finger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	if (litest_has_clickfinger(dev))
		litest_enable_clickfinger(dev);

	litest_enable_2fg_scroll(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 20);
	litest_touch_move_to(dev, 0, 20, 20, 80, 80, 20, 0);
	litest_drain_events(li);
	litest_touch_down(dev, 1, 79, 80);
	litest_touch_move_two_touches(dev, 80, 80, 79, 80, -20, -20, 10, 0);
	libinput_dispatch(li);

	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_speed_ignore_finger_edgescroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_edge_scroll(dev);
	if (litest_has_clickfinger(dev))
		litest_enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 20);
	litest_touch_move_to(dev, 0, 20, 20, 60, 80, 20, 0);
	litest_drain_events(li);
	litest_touch_down(dev, 1, 59, 80);
	litest_touch_move_two_touches(dev, 60, 80, 59, 80, -20, -20, 10, 0);
	libinput_dispatch(li);

	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_touch_up(dev, 1);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

enum suspend {
	SUSPEND_EXT_MOUSE = 1,
	SUSPEND_SENDEVENTS,
	SUSPEND_LID,
	SUSPEND_TABLETMODE,
	SUSPEND_COUNT,
};

static void
assert_touchpad_moves(struct litest_device *tp)
{
	struct libinput *li = tp->libinput;

	litest_touch_down(tp, 0, 50, 50);
	litest_touch_move_to(tp, 0, 50, 50, 60, 80, 20, 0);
	litest_touch_up(tp, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}

static void
assert_touchpad_does_not_move(struct litest_device *tp)
{
	struct libinput *li = tp->libinput;

	litest_touch_down(tp, 0, 20, 20);
	litest_touch_move_to(tp, 0, 20, 20, 60, 80, 20, 0);
	litest_touch_up(tp, 0);
	litest_assert_empty_queue(li);
}

START_TEST(touchpad_suspend_abba)
{
	struct litest_device *tp = litest_current_device();
	struct litest_device *lid, *tabletmode, *extmouse;
	struct libinput *li = tp->libinput;
	enum suspend first = _i; /* ranged test */
	enum suspend other;

	if (first == SUSPEND_EXT_MOUSE && litest_touchpad_is_external(tp))
		return;

	lid = litest_add_device(li, LITEST_LID_SWITCH);
	tabletmode = litest_add_device(li, LITEST_THINKPAD_EXTRABUTTONS);
	extmouse = litest_add_device(li, LITEST_MOUSE);

	litest_disable_tap(tp->libinput_device);

	/* ABBA test for touchpad internal suspend:
	 *  reason A on
	 *  reason B on
	 *  reason B off
	 *  reason A off
	 */
	for (other = SUSPEND_EXT_MOUSE; other < SUSPEND_COUNT; other++) {
		if (other == first)
			continue;

		if (other == SUSPEND_EXT_MOUSE && litest_touchpad_is_external(tp))
			return;

		/* That transition is tested elsewhere and has a different
		 * behavior */
		if ((other == SUSPEND_SENDEVENTS && first == SUSPEND_EXT_MOUSE) ||
		    (first == SUSPEND_SENDEVENTS && other == SUSPEND_EXT_MOUSE))
			continue;

		litest_drain_events(li);
		assert_touchpad_moves(tp);

		/* First reason for suspend: on */
		switch (first) {
		case  SUSPEND_EXT_MOUSE:
			litest_sendevents_ext_mouse(tp);
			break;
		case  SUSPEND_TABLETMODE:
			litest_switch_action(tabletmode,
					     LIBINPUT_SWITCH_TABLET_MODE,
					     LIBINPUT_SWITCH_STATE_ON);
			break;
		case  SUSPEND_LID:
			litest_switch_action(lid,
					     LIBINPUT_SWITCH_LID,
					     LIBINPUT_SWITCH_STATE_ON);
			break;
		case  SUSPEND_SENDEVENTS:
			litest_sendevents_off(tp);
			break;
		default:
			ck_abort();
		}

		litest_drain_events(li);

		assert_touchpad_does_not_move(tp);

		/* Second reason to suspend: on/off while first reason remains */
		switch (other) {
		case SUSPEND_EXT_MOUSE:
			litest_sendevents_ext_mouse(tp);
			litest_sendevents_on(tp);
			break;
		case SUSPEND_LID:
			litest_switch_action(lid,
					     LIBINPUT_SWITCH_LID,
					     LIBINPUT_SWITCH_STATE_ON);
			litest_drain_events(li);
			litest_switch_action(lid,
					     LIBINPUT_SWITCH_LID,
					     LIBINPUT_SWITCH_STATE_OFF);
			litest_drain_events(li);
			break;
		case SUSPEND_TABLETMODE:
			litest_switch_action(tabletmode,
					     LIBINPUT_SWITCH_TABLET_MODE,
					     LIBINPUT_SWITCH_STATE_ON);
			litest_drain_events(li);
			litest_switch_action(tabletmode,
					     LIBINPUT_SWITCH_TABLET_MODE,
					     LIBINPUT_SWITCH_STATE_OFF);
			litest_drain_events(li);
			break;
		case SUSPEND_SENDEVENTS:
			litest_sendevents_off(tp);
			litest_sendevents_on(tp);
			break;
		default:
			ck_abort();
		}

		assert_touchpad_does_not_move(tp);

		/* First reason for suspend: off */
		switch (first) {
		case  SUSPEND_EXT_MOUSE:
			litest_sendevents_on(tp);
			break;
		case  SUSPEND_TABLETMODE:
			litest_switch_action(tabletmode,
					     LIBINPUT_SWITCH_TABLET_MODE,
					     LIBINPUT_SWITCH_STATE_OFF);
			break;
		case  SUSPEND_LID:
			litest_switch_action(lid,
					     LIBINPUT_SWITCH_LID,
					     LIBINPUT_SWITCH_STATE_OFF);
			break;
		case  SUSPEND_SENDEVENTS:
			litest_sendevents_on(tp);
			break;
		default:
			ck_abort();
		}

		litest_drain_events(li);
		assert_touchpad_moves(tp);
	}

	litest_delete_device(lid);
	litest_delete_device(tabletmode);
	litest_delete_device(extmouse);
}
END_TEST

START_TEST(touchpad_suspend_abab)
{
	struct litest_device *tp = litest_current_device();
	struct litest_device *lid, *tabletmode, *extmouse;
	struct libinput *li = tp->libinput;
	enum suspend first = _i; /* ranged test */
	enum suspend other;

	if (first == SUSPEND_EXT_MOUSE && litest_touchpad_is_external(tp))
		return;

	lid = litest_add_device(li, LITEST_LID_SWITCH);
	tabletmode = litest_add_device(li, LITEST_THINKPAD_EXTRABUTTONS);
	extmouse = litest_add_device(li, LITEST_MOUSE);

	litest_disable_tap(tp->libinput_device);

	/* ABAB test for touchpad internal suspend:
	 *  reason A on
	 *  reason B on
	 *  reason A off
	 *  reason B off
	 */
	for (other = SUSPEND_EXT_MOUSE; other < SUSPEND_COUNT; other++) {
		if (other == first)
			continue;

		if (other == SUSPEND_EXT_MOUSE && litest_touchpad_is_external(tp))
			return;

		/* That transition is tested elsewhere and has a different
		 * behavior */
		if ((other == SUSPEND_SENDEVENTS && first == SUSPEND_EXT_MOUSE) ||
		    (first == SUSPEND_SENDEVENTS && other == SUSPEND_EXT_MOUSE))
			continue;

		litest_drain_events(li);
		assert_touchpad_moves(tp);

		/* First reason for suspend: on */
		switch (first) {
		case  SUSPEND_EXT_MOUSE:
			litest_sendevents_ext_mouse(tp);
			break;
		case  SUSPEND_TABLETMODE:
			litest_switch_action(tabletmode,
					     LIBINPUT_SWITCH_TABLET_MODE,
					     LIBINPUT_SWITCH_STATE_ON);
			break;
		case  SUSPEND_LID:
			litest_switch_action(lid,
					     LIBINPUT_SWITCH_LID,
					     LIBINPUT_SWITCH_STATE_ON);
			break;
		case  SUSPEND_SENDEVENTS:
			litest_sendevents_off(tp);
			break;
		default:
			ck_abort();
		}

		litest_drain_events(li);

		assert_touchpad_does_not_move(tp);

		/* Second reason to suspend: on */
		switch (other) {
		case SUSPEND_EXT_MOUSE:
			litest_sendevents_ext_mouse(tp);
			break;
		case SUSPEND_LID:
			litest_switch_action(lid,
					     LIBINPUT_SWITCH_LID,
					     LIBINPUT_SWITCH_STATE_ON);
			litest_drain_events(li);
			break;
		case SUSPEND_TABLETMODE:
			litest_switch_action(tabletmode,
					     LIBINPUT_SWITCH_TABLET_MODE,
					     LIBINPUT_SWITCH_STATE_ON);
			litest_drain_events(li);
			break;
		case SUSPEND_SENDEVENTS:
			litest_sendevents_off(tp);
			break;
		default:
			ck_abort();
		}

		assert_touchpad_does_not_move(tp);

		/* First reason for suspend: off */
		switch (first) {
		case  SUSPEND_EXT_MOUSE:
			litest_sendevents_on(tp);
			break;
		case  SUSPEND_TABLETMODE:
			litest_switch_action(tabletmode,
					     LIBINPUT_SWITCH_TABLET_MODE,
					     LIBINPUT_SWITCH_STATE_OFF);
			break;
		case  SUSPEND_LID:
			litest_switch_action(lid,
					     LIBINPUT_SWITCH_LID,
					     LIBINPUT_SWITCH_STATE_OFF);
			break;
		case  SUSPEND_SENDEVENTS:
			litest_sendevents_on(tp);
			break;
		default:
			ck_abort();
		}

		litest_drain_events(li);
		assert_touchpad_does_not_move(tp);

		/* Second reason to suspend: off */
		switch (other) {
		case SUSPEND_EXT_MOUSE:
			litest_sendevents_on(tp);
			break;
		case SUSPEND_LID:
			litest_switch_action(lid,
					     LIBINPUT_SWITCH_LID,
					     LIBINPUT_SWITCH_STATE_OFF);
			litest_drain_events(li);
			break;
		case SUSPEND_TABLETMODE:
			litest_switch_action(tabletmode,
					     LIBINPUT_SWITCH_TABLET_MODE,
					     LIBINPUT_SWITCH_STATE_OFF);
			litest_drain_events(li);
			break;
		case SUSPEND_SENDEVENTS:
			litest_sendevents_on(tp);
			break;
		default:
			ck_abort();
		}

		litest_drain_events(li);
		assert_touchpad_moves(tp);
	}

	litest_delete_device(lid);
	litest_delete_device(tabletmode);
	litest_delete_device(extmouse);
}
END_TEST

TEST_COLLECTION(touchpad)
{
	struct range suspends = { SUSPEND_EXT_MOUSE, SUSPEND_COUNT };
	struct range axis_range = {ABS_X, ABS_Y + 1};
	struct range twice = {0, 2 };

	litest_add("touchpad:motion", touchpad_1fg_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:motion", touchpad_2fg_no_motion, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);

	litest_add("touchpad:scroll", touchpad_2fg_scroll, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_diagonal, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_slow_distance, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_return_to_motion, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_source, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_semi_mt, LITEST_SEMI_MT, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_scroll_natural_defaults, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_scroll_natural_enable_config, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_scroll_natural_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_scroll_natural_edge, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_scroll_defaults, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_vert, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_horiz, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:scroll", touchpad_edge_scroll_horiz_clickpad, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_horiz, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_edge_after_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_source, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_edge_scroll_into_buttonareas, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_within_buttonareas, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_buttonareas_click_stops_scroll, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_clickfinger_click_stops_scroll, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_into_area, LITEST_TOUCHPAD, LITEST_ANY);

	litest_add("touchpad:palm", touchpad_palm_detect_at_edge, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_at_top, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_at_bottom_corners, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_at_top_corners, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_palm_becomes_pointer, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_top_palm_becomes_pointer, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_palm_stays_palm, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_top_palm_stays_palm, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_no_palm_moving_into_edges, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_no_palm_moving_into_top, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_no_tap_top_edge, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_tap_hardbuttons, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_tap_softbuttons, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_tap_clickfinger, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_no_palm_detect_at_edge_for_edge_scrolling, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:palm", touchpad_no_palm_detect_2fg_scroll, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_both_edges, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_tool_palm, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_tool_palm_on_off, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_tool_palm_tap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_tool_palm_tap_after, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_touch_size, LITEST_APPLE_CLICKPAD, LITEST_ANY);

	litest_add("touchpad:palm", touchpad_palm_detect_pressure, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_pressure_late, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_pressure_keep_palm, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_pressure_after_edge, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:palm", touchpad_palm_detect_pressure_after_dwt, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);

	litest_add("touchpad:palm", touchpad_palm_clickfinger_pressure, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_clickfinger_pressure_2fg, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_clickfinger_size, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_clickfinger_size_2fg, LITEST_CLICKPAD, LITEST_ANY);

	litest_add("touchpad:left-handed", touchpad_left_handed, LITEST_TOUCHPAD|LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add_for_device("touchpad:left-handed", touchpad_left_handed_appletouch, LITEST_APPLETOUCH);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickpad, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickfinger, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:left-handed", touchpad_left_handed_tapping, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:left-handed", touchpad_left_handed_tapping_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:left-handed", touchpad_left_handed_delayed, LITEST_TOUCHPAD|LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickpad_delayed, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);

	/* Semi-MT hover tests aren't generic, they only work on this device and
	 * ignore the semi-mt capability (it doesn't matter for the tests) */
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_noevent, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_down, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_down_up, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_down_hover_down, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_2fg_noevent, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_2fg_1fg_down, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_2fg_up, LITEST_SYNAPTICS_HOVER_SEMI_MT);

	litest_add("touchpad:hover", touchpad_hover_noevent, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_down, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_down_up, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_down_hover_down, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_2fg_noevent, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_2fg_1fg_down, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_1fg_tap, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);

	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_buttons, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_mb_scroll, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_mb_click, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_buttons_softbuttons, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_buttons_2fg_scroll, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_no_trackpoint, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);

	litest_add_ranged("touchpad:state", touchpad_initial_state, LITEST_TOUCHPAD, LITEST_ANY, &axis_range);

	litest_add("touchpad:dwt", touchpad_dwt, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_ext_and_int_keyboard, LITEST_SYNAPTICS_I2C);
	litest_add("touchpad:dwt", touchpad_dwt_enable_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_touch_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold_timeout_existing_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold_timeout_existing_touch_cornercase, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_type, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_type_short_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_modifier_no_dwt, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_modifier_combo_no_dwt, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_modifier_combo_dwt_after, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_modifier_combo_dwt_remains, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_fkeys_no_dwt, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_tap_drag, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_click, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_edge_scroll, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:dwt", touchpad_dwt_edge_scroll_interrupt, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:dwt", touchpad_dwt_config_default_on, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_config_default_off, LITEST_ANY, LITEST_TOUCHPAD);
	litest_add("touchpad:dwt", touchpad_dwt_disabled, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_disable_during_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_disable_before_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_disable_during_key_release, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_disable_during_key_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_enable_during_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_enable_before_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_enable_during_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_remove_kbd_while_active, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_apple, LITEST_BCM5974);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_acer_hawaii, LITEST_ACER_HAWAII_TOUCHPAD);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_multiple_keyboards, LITEST_SYNAPTICS_I2C);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_multiple_keyboards_bothkeys, LITEST_SYNAPTICS_I2C);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_multiple_keyboards_bothkeys_modifier, LITEST_SYNAPTICS_I2C);
	litest_add_ranged_for_device("touchpad:dwt", touchpad_dwt_multiple_keyboards_remove, LITEST_SYNAPTICS_I2C, &twice);

	litest_add("touchpad:thumb", touchpad_thumb_begin_no_motion, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_update_no_motion, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_moving, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_moving_empty_slots, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:thumb", touchpad_thumb_clickfinger, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_btnarea, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_tap_begin, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_tap_touch, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_tap_hold, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_tap_hold_2ndfg, LITEST_CLICKPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:thumb", touchpad_thumb_tap_hold_2ndfg_tap, LITEST_CLICKPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:thumb", touchpad_thumb_move_and_tap, LITEST_CLICKPAD, LITEST_ANY);

	litest_add_for_device("touchpad:bugs", touchpad_tool_tripletap_touch_count, LITEST_SYNAPTICS_TOPBUTTONPAD);
	litest_add_for_device("touchpad:bugs", touchpad_slot_swap, LITEST_SYNAPTICS_TOPBUTTONPAD);
	litest_add_for_device("touchpad:bugs", touchpad_finger_always_down, LITEST_SYNAPTICS_TOPBUTTONPAD);

	litest_add("touchpad:time", touchpad_time_usec, LITEST_TOUCHPAD, LITEST_ANY);

	litest_add_for_device("touchpad:jumps", touchpad_jump_finger_motion, LITEST_SYNAPTICS_CLICKPAD_X220);

	litest_add_for_device("touchpad:sendevents", touchpad_disabled_on_mouse, LITEST_SYNAPTICS_CLICKPAD_X220);
	litest_add_for_device("touchpad:sendevents", touchpad_disabled_on_mouse_suspend_mouse, LITEST_SYNAPTICS_CLICKPAD_X220);
	litest_add_for_device("touchpad:sendevents", touchpad_disabled_double_mouse, LITEST_SYNAPTICS_CLICKPAD_X220);
	litest_add_for_device("touchpad:sendevents", touchpad_disabled_double_mouse_one_suspended, LITEST_SYNAPTICS_CLICKPAD_X220);

	litest_add("touchpad:pressure", touchpad_pressure, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:pressure", touchpad_pressure_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:pressure", touchpad_pressure_2fg_st, LITEST_TOUCHPAD|LITEST_SINGLE_TOUCH, LITEST_ANY);
	litest_add("touchpad:pressure", touchpad_pressure_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:pressure", touchpad_pressure_tap_2fg, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:pressure", touchpad_pressure_tap_2fg_1fg_light, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:pressure", touchpad_pressure_btntool, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:pressure", touchpad_pressure_semi_mt_2fg_goes_light, LITEST_SEMI_MT, LITEST_ANY);

	litest_add("touchpad:touch-size", touchpad_touch_size, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:touch-size", touchpad_touch_size_2fg, LITEST_APPLE_CLICKPAD, LITEST_ANY);

	litest_add("touchpad:speed", touchpad_speed_ignore_finger, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("touchpad:speed", touchpad_speed_allow_nearby_finger, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("touchpad:speed", touchpad_speed_ignore_finger_edgescroll, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);

	litest_add_ranged("touchpad:suspend", touchpad_suspend_abba, LITEST_TOUCHPAD, LITEST_ANY, &suspends);
	litest_add_ranged("touchpad:suspend", touchpad_suspend_abab, LITEST_TOUCHPAD, LITEST_ANY, &suspends);
}
