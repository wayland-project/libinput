/*
 * Copyright © 2014-2015 Red Hat, Inc.
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

#include "config.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>

#include "evdev-mt-touchpad.h"

#define DEFAULT_TRACKPOINT_ACTIVITY_TIMEOUT ms2us(300)
#define DEFAULT_TRACKPOINT_EVENT_TIMEOUT ms2us(40)
#define DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_1 ms2us(200)
#define DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_2 ms2us(500)
#define THUMB_MOVE_TIMEOUT ms2us(300)
#define FAKE_FINGER_OVERFLOW (1 << 7)
#define THUMB_IGNORE_SPEED_THRESHOLD 20 /* mm/s */

static inline struct tp_history_point*
tp_motion_history_offset(struct tp_touch *t, int offset)
{
	int offset_index =
		(t->history.index - offset + TOUCHPAD_HISTORY_LENGTH) %
		TOUCHPAD_HISTORY_LENGTH;

	return &t->history.samples[offset_index];
}

struct normalized_coords
tp_filter_motion(struct tp_dispatch *tp,
		 const struct device_float_coords *unaccelerated,
		 uint64_t time)
{
	struct device_float_coords raw;
	const struct normalized_coords zero = { 0.0, 0.0 };

	if (device_float_is_zero(*unaccelerated))
		return zero;

	/* Convert to device units with x/y in the same resolution */
	raw = tp_scale_to_xaxis(tp, *unaccelerated);

	return filter_dispatch(tp->device->pointer.filter,
			       &raw, tp, time);
}

struct normalized_coords
tp_filter_motion_unaccelerated(struct tp_dispatch *tp,
			       const struct device_float_coords *unaccelerated,
			       uint64_t time)
{
	struct device_float_coords raw;
	const struct normalized_coords zero = { 0.0, 0.0 };

	if (device_float_is_zero(*unaccelerated))
		return zero;

	/* Convert to device units with x/y in the same resolution */
	raw = tp_scale_to_xaxis(tp, *unaccelerated);

	return filter_dispatch_constant(tp->device->pointer.filter,
					&raw, tp, time);
}

static inline void
tp_calculate_motion_speed(struct tp_dispatch *tp, struct tp_touch *t)
{
	const struct tp_history_point *last;
	struct device_coords delta;
	struct phys_coords mm;
	double distance;
	double speed;

	/* Don't do this on single-touch or semi-mt devices */
	if (!tp->has_mt || tp->semi_mt)
		return;

	/* This doesn't kick in until we have at least 4 events in the
	 * motion history. As a side-effect, this automatically handles the
	 * 2fg scroll where a finger is down and moving fast before the
	 * other finger comes down for the scroll.
	 *
	 * We do *not* reset the speed to 0 here though. The motion history
	 * is reset whenever a new finger is down, so we'd be resetting the
	 * speed and failing.
	 */
	if (t->history.count < 4)
		return;

	/* TODO: we probably need a speed history here so we can average
	 * across a few events */
	last = tp_motion_history_offset(t, 1);
	delta.x = abs(t->point.x - last->point.x);
	delta.y = abs(t->point.y - last->point.y);
	mm = evdev_device_unit_delta_to_mm(tp->device, &delta);

	distance = length_in_mm(mm);
	speed = distance/(t->time - last->time); /* mm/us */
	speed *= 1000000; /* mm/s */

	t->speed.last_speed = speed;
}

static inline void
tp_motion_history_push(struct tp_touch *t)
{
	int motion_index = (t->history.index + 1) % TOUCHPAD_HISTORY_LENGTH;

	if (t->history.count < TOUCHPAD_HISTORY_LENGTH)
		t->history.count++;

	t->history.samples[motion_index].point = t->point;
	t->history.samples[motion_index].time = t->time;
	t->history.index = motion_index;
}

/* Idea: if we got a tuple of *very* quick moves like {Left, Right,
 * Left}, or {Right, Left, Right}, it means touchpad jitters since no
 * human can move like that within thresholds.
 *
 * We encode left moves as zeroes, and right as ones. We also drop
 * the array to all zeroes when contraints are not satisfied. Then we
 * search for the pattern {1,0,1}. It can't match {Left, Right, Left},
 * but it does match {Left, Right, Left, Right}, so it's okay.
 *
 * This only looks at x changes, y changes are ignored.
 */
static inline void
tp_detect_wobbling(struct tp_dispatch *tp,
		   struct tp_touch *t,
		   uint64_t time)
{
	int dx, dy;
	uint64_t dtime;

	if (tp->nfingers_down != 1 ||
	    tp->nfingers_down != tp->old_nfingers_down)
		return;

	if (tp->hysteresis.enabled)
		return;

	if (!(tp->queued & TOUCHPAD_EVENT_MOTION)) {
		t->hysteresis.x_motion_history = 0;
		return;
	}

	if (t->last_point.x == 0) { /* first invocation */
		dx = 0;
		dy = 0;
	} else {
		dx = t->last_point.x - t->point.x;
		dy = t->last_point.y - t->point.y;
	}

	dtime = time - tp->hysteresis.last_motion_time;

	tp->hysteresis.last_motion_time = time;
	t->last_point = t->point;

	if ((dx == 0 && dy != 0) || dtime > ms2us(40)) {
		t->hysteresis.x_motion_history = 0;
		return;
	}

	t->hysteresis.x_motion_history >>= 1;
	if (dx > 0) { /* right move */
		static const char r_l_r = 0x5; /* {Right, Left, Right} */

		t->hysteresis.x_motion_history |= (1 << 2);
		if (t->hysteresis.x_motion_history == r_l_r) {
			tp->hysteresis.enabled = true;
			evdev_log_debug(tp->device,
					"hysteresis enabled. "
					"See %stouchpad_jitter.html for details\n",
					HTTP_DOC_LINK);
		}
	}
}

static inline void
tp_motion_hysteresis(struct tp_dispatch *tp,
		     struct tp_touch *t)
{
	if (!tp->hysteresis.enabled)
		return;

	if (t->history.count > 0)
		t->point = evdev_hysteresis(&t->point,
					    &t->hysteresis.center,
					    &tp->hysteresis.margin);

	t->hysteresis.center = t->point;
}

static inline void
tp_motion_history_reset(struct tp_touch *t)
{
	t->history.count = 0;
}

static inline struct tp_touch *
tp_current_touch(struct tp_dispatch *tp)
{
	return &tp->touches[min(tp->slot, tp->ntouches - 1)];
}

static inline struct tp_touch *
tp_get_touch(struct tp_dispatch *tp, unsigned int slot)
{
	assert(slot < tp->ntouches);
	return &tp->touches[slot];
}

static inline unsigned int
tp_fake_finger_count(struct tp_dispatch *tp)
{
	/* Only one of BTN_TOOL_DOUBLETAP/TRIPLETAP/... may be set at any
	 * time */
	if (__builtin_popcount(
		       tp->fake_touches & ~(FAKE_FINGER_OVERFLOW|0x1)) > 1)
		evdev_log_bug_kernel(tp->device,
				     "Invalid fake finger state %#x\n",
				     tp->fake_touches);

	if (tp->fake_touches & FAKE_FINGER_OVERFLOW)
		return FAKE_FINGER_OVERFLOW;
	else /* don't count BTN_TOUCH */
		return ffs(tp->fake_touches >> 1);
}

static inline bool
tp_fake_finger_is_touching(struct tp_dispatch *tp)
{
	return tp->fake_touches & 0x1;
}

static inline void
tp_fake_finger_set(struct tp_dispatch *tp,
		   unsigned int code,
		   bool is_press)
{
	unsigned int shift;

	switch (code) {
	case BTN_TOUCH:
		if (!is_press)
			tp->fake_touches &= ~FAKE_FINGER_OVERFLOW;
		shift = 0;
		break;
	case BTN_TOOL_FINGER:
		shift = 1;
		break;
	case BTN_TOOL_DOUBLETAP:
	case BTN_TOOL_TRIPLETAP:
	case BTN_TOOL_QUADTAP:
		shift = code - BTN_TOOL_DOUBLETAP + 2;
		break;
	/* when QUINTTAP is released we're either switching to 6 fingers
	   (flag stays in place until BTN_TOUCH is released) or
	   one of DOUBLE/TRIPLE/QUADTAP (will clear the flag on press) */
	case BTN_TOOL_QUINTTAP:
		if (is_press)
			tp->fake_touches |= FAKE_FINGER_OVERFLOW;
		return;
	default:
		return;
	}

	if (is_press) {
		tp->fake_touches &= ~FAKE_FINGER_OVERFLOW;
		tp->fake_touches |= 1 << shift;

	} else {
		tp->fake_touches &= ~(0x1 << shift);
	}
}

static inline void
tp_new_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	if (t->state == TOUCH_BEGIN ||
	    t->state == TOUCH_UPDATE ||
	    t->state == TOUCH_HOVERING)
		return;

	/* we begin the touch as hovering because until BTN_TOUCH happens we
	 * don't know if it's a touch down or not. And BTN_TOUCH may happen
	 * after ABS_MT_TRACKING_ID */
	tp_motion_history_reset(t);
	t->dirty = true;
	t->has_ended = false;
	t->was_down = false;
	t->palm.state = PALM_NONE;
	t->state = TOUCH_HOVERING;
	t->pinned.is_pinned = false;
	t->time = time;
	t->speed.last_speed = 0;
	t->speed.exceeded_count = 0;
	t->hysteresis.x_motion_history = 0;
	tp->queued |= TOUCHPAD_EVENT_MOTION;
}

static inline void
tp_begin_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	t->dirty = true;
	t->state = TOUCH_BEGIN;
	t->time = time;
	t->was_down = true;
	tp->nfingers_down++;
	t->palm.time = time;
	t->thumb.state = THUMB_STATE_MAYBE;
	t->thumb.first_touch_time = time;
	t->tap.is_thumb = false;
	t->tap.is_palm = false;
	assert(tp->nfingers_down >= 1);
	tp->hysteresis.last_motion_time = time;
}

/**
 * Schedule a touch to be ended, based on either the events or some
 * attributes of the touch (size, pressure). In some cases we need to
 * resurrect a touch that has ended, so this doesn't actually end the touch
 * yet. All the TOUCH_MAYBE_END touches get properly ended once the device
 * state has been processed once and we know how many zombie touches we
 * need.
 */
static inline void
tp_maybe_end_touch(struct tp_dispatch *tp,
		   struct tp_touch *t,
		   uint64_t time)
{
	switch (t->state) {
	case TOUCH_NONE:
	case TOUCH_MAYBE_END:
		return;
	case TOUCH_END:
		evdev_log_bug_libinput(tp->device,
				       "touch %d: already in TOUCH_END\n",
				       t->index);
		return;
	case TOUCH_HOVERING:
	case TOUCH_BEGIN:
	case TOUCH_UPDATE:
		break;
	}

	if (t->state != TOUCH_HOVERING) {
		assert(tp->nfingers_down >= 1);
		tp->nfingers_down--;
		t->state = TOUCH_MAYBE_END;
	} else {
		t->state = TOUCH_NONE;
	}

	t->dirty = true;
}

/**
 * Inverse to tp_maybe_end_touch(), restores a touch back to its previous
 * state.
 */
static inline void
tp_recover_ended_touch(struct tp_dispatch *tp,
		       struct tp_touch *t)
{
	t->dirty = true;
	t->state = TOUCH_UPDATE;
	tp->nfingers_down++;
}

/**
 * End a touch, even if the touch sequence is still active.
 * Use tp_maybe_end_touch() instead.
 */
static inline void
tp_end_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	if (t->state != TOUCH_MAYBE_END) {
		evdev_log_bug_libinput(tp->device,
				       "touch %d should be MAYBE_END, is %d\n",
				       t->index,
				       t->state);
		return;
	}

	t->dirty = true;
	t->palm.state = PALM_NONE;
	t->state = TOUCH_END;
	t->pinned.is_pinned = false;
	t->time = time;
	t->palm.time = 0;
	tp->queued |= TOUCHPAD_EVENT_MOTION;
}

/**
 * End the touch sequence on ABS_MT_TRACKING_ID -1 or when the BTN_TOOL_* 0 is received.
 */
static inline void
tp_end_sequence(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	t->has_ended = true;
	tp_maybe_end_touch(tp, t, time);
}

static void
tp_stop_actions(struct tp_dispatch *tp, uint64_t time)
{
	tp_edge_scroll_stop_events(tp, time);
	tp_gesture_cancel(tp, time);
	tp_tap_suspend(tp, time);
}

struct device_coords
tp_get_delta(struct tp_touch *t)
{
	struct device_coords delta;
	const struct device_coords zero = { 0.0, 0.0 };

	if (t->history.count <= 1)
		return zero;

	delta.x = tp_motion_history_offset(t, 0)->point.x -
		  tp_motion_history_offset(t, 1)->point.x;
	delta.y = tp_motion_history_offset(t, 0)->point.y -
		  tp_motion_history_offset(t, 1)->point.y;

	return delta;
}

static void
tp_process_absolute(struct tp_dispatch *tp,
		    const struct input_event *e,
		    uint64_t time)
{
	struct tp_touch *t = tp_current_touch(tp);

	switch(e->code) {
	case ABS_MT_POSITION_X:
		evdev_device_check_abs_axis_range(tp->device,
						  e->code,
						  e->value);
		t->point.x = e->value;
		t->time = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_MT_POSITION_Y:
		evdev_device_check_abs_axis_range(tp->device,
						  e->code,
						  e->value);
		t->point.y = e->value;
		t->time = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_MT_SLOT:
		tp->slot = e->value;
		break;
	case ABS_MT_TRACKING_ID:
		if (e->value != -1)
			tp_new_touch(tp, t, time);
		else
			tp_end_sequence(tp, t, time);
		break;
	case ABS_MT_PRESSURE:
		t->pressure = e->value;
		t->time = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_OTHERAXIS;
		break;
	case ABS_MT_TOOL_TYPE:
		t->is_tool_palm = e->value == MT_TOOL_PALM;
		t->time = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_OTHERAXIS;
		break;
	case ABS_MT_TOUCH_MAJOR:
		t->major = e->value;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_OTHERAXIS;
		break;
	case ABS_MT_TOUCH_MINOR:
		t->minor = e->value;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_OTHERAXIS;
		break;
	}
}

static void
tp_process_absolute_st(struct tp_dispatch *tp,
		       const struct input_event *e,
		       uint64_t time)
{
	struct tp_touch *t = tp_current_touch(tp);

	switch(e->code) {
	case ABS_X:
		evdev_device_check_abs_axis_range(tp->device,
						  e->code,
						  e->value);
		t->point.x = e->value;
		t->time = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_Y:
		evdev_device_check_abs_axis_range(tp->device,
						  e->code,
						  e->value);
		t->point.y = e->value;
		t->time = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_PRESSURE:
		t->pressure = e->value;
		t->time = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_OTHERAXIS;
		break;
	}
}

static inline void
tp_restore_synaptics_touches(struct tp_dispatch *tp,
			     uint64_t time)
{
	unsigned int i;
	unsigned int nfake_touches;

	nfake_touches = tp_fake_finger_count(tp);
	if (nfake_touches < 3)
		return;

	if (tp->nfingers_down >= nfake_touches ||
	    tp->nfingers_down == tp->num_slots)
		return;

	/* Synaptics devices may end touch 2 on BTN_TOOL_TRIPLETAP
	 * and start it again on the next frame with different coordinates
	 * (#91352). We search the touches we have, if there is one that has
	 * just ended despite us being on tripletap, we move it back to
	 * update.
	 */
	for (i = 0; i < tp->num_slots; i++) {
		struct tp_touch *t = tp_get_touch(tp, i);

		if (t->state != TOUCH_MAYBE_END)
			continue;

		/* new touch, move it through begin to update immediately */
		tp_recover_ended_touch(tp, t);
	}
}

static void
tp_process_fake_touches(struct tp_dispatch *tp,
			uint64_t time)
{
	struct tp_touch *t;
	unsigned int nfake_touches;
	unsigned int i, start;

	nfake_touches = tp_fake_finger_count(tp);
	if (nfake_touches == FAKE_FINGER_OVERFLOW)
		return;

	if (tp->device->model_flags &
	    EVDEV_MODEL_SYNAPTICS_SERIAL_TOUCHPAD)
		tp_restore_synaptics_touches(tp, time);

	start = tp->has_mt ? tp->num_slots : 0;
	for (i = start; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);
		if (i < nfake_touches)
			tp_new_touch(tp, t, time);
		else
			tp_end_sequence(tp, t, time);
	}
}

static void
tp_process_trackpoint_button(struct tp_dispatch *tp,
			     const struct input_event *e,
			     uint64_t time)
{
	struct evdev_dispatch *dispatch;
	struct input_event event;
	struct input_event syn_report = {
		 .input_event_sec = 0,
		 .input_event_usec = 0,
		 .type = EV_SYN,
		 .code = SYN_REPORT,
		 .value = 0
	};

	if (!tp->buttons.trackpoint)
		return;

	dispatch = tp->buttons.trackpoint->dispatch;

	event = *e;
	syn_report.input_event_sec = e->input_event_sec;
	syn_report.input_event_usec = e->input_event_usec;

	switch (event.code) {
	case BTN_0:
		event.code = BTN_LEFT;
		break;
	case BTN_1:
		event.code = BTN_RIGHT;
		break;
	case BTN_2:
		event.code = BTN_MIDDLE;
		break;
	default:
		return;
	}

	dispatch->interface->process(dispatch,
				     tp->buttons.trackpoint,
				     &event, time);
	dispatch->interface->process(dispatch,
				     tp->buttons.trackpoint,
				     &syn_report, time);
}

static void
tp_process_key(struct tp_dispatch *tp,
	       const struct input_event *e,
	       uint64_t time)
{
	switch (e->code) {
		case BTN_LEFT:
		case BTN_MIDDLE:
		case BTN_RIGHT:
			tp_process_button(tp, e, time);
			break;
		case BTN_TOUCH:
		case BTN_TOOL_FINGER:
		case BTN_TOOL_DOUBLETAP:
		case BTN_TOOL_TRIPLETAP:
		case BTN_TOOL_QUADTAP:
		case BTN_TOOL_QUINTTAP:
			tp_fake_finger_set(tp, e->code, !!e->value);
			break;
		case BTN_0:
		case BTN_1:
		case BTN_2:
			tp_process_trackpoint_button(tp, e, time);
			break;
	}
}

static void
tp_unpin_finger(const struct tp_dispatch *tp, struct tp_touch *t)
{
	struct phys_coords mm;
	struct device_coords delta;

	if (!t->pinned.is_pinned)
		return;

	delta.x = abs(t->point.x - t->pinned.center.x);
	delta.y = abs(t->point.y - t->pinned.center.y);

	mm = evdev_device_unit_delta_to_mm(tp->device, &delta);

	/* 1.5mm movement -> unpin */
	if (hypot(mm.x, mm.y) >= 1.5) {
		t->pinned.is_pinned = false;
		return;
	}
}

static void
tp_pin_fingers(struct tp_dispatch *tp)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		t->pinned.is_pinned = true;
		t->pinned.center = t->point;
	}
}

bool
tp_touch_active(const struct tp_dispatch *tp, const struct tp_touch *t)
{
	return (t->state == TOUCH_BEGIN || t->state == TOUCH_UPDATE) &&
		t->palm.state == PALM_NONE &&
		!t->pinned.is_pinned &&
		t->thumb.state != THUMB_STATE_YES &&
		tp_button_touch_active(tp, t) &&
		tp_edge_scroll_touch_active(tp, t);
}

static inline bool
tp_palm_was_in_side_edge(const struct tp_dispatch *tp, const struct tp_touch *t)
{
	return t->palm.first.x < tp->palm.left_edge ||
	       t->palm.first.x > tp->palm.right_edge;
}

static inline bool
tp_palm_was_in_top_edge(const struct tp_dispatch *tp, const struct tp_touch *t)
{
	return t->palm.first.y < tp->palm.upper_edge;
}

static inline bool
tp_palm_in_side_edge(const struct tp_dispatch *tp, const struct tp_touch *t)
{
	return t->point.x < tp->palm.left_edge ||
	       t->point.x > tp->palm.right_edge;
}

static inline bool
tp_palm_in_top_edge(const struct tp_dispatch *tp, const struct tp_touch *t)
{
	return t->point.y < tp->palm.upper_edge;
}

static inline bool
tp_palm_in_edge(const struct tp_dispatch *tp, const struct tp_touch *t)
{
	return tp_palm_in_side_edge(tp, t) || tp_palm_in_top_edge(tp, t);
}

bool
tp_palm_tap_is_palm(const struct tp_dispatch *tp, const struct tp_touch *t)
{
	if (t->state != TOUCH_BEGIN)
		return false;

	if (!tp_palm_in_edge(tp, t))
		return false;

	evdev_log_debug(tp->device,
			"palm: touch %d: palm-tap detected\n",
			t->index);
	return true;
}

static bool
tp_palm_detect_dwt_triggered(struct tp_dispatch *tp,
			     struct tp_touch *t,
			     uint64_t time)
{
	if (tp->dwt.dwt_enabled &&
	    tp->dwt.keyboard_active &&
	    t->state == TOUCH_BEGIN) {
		t->palm.state = PALM_TYPING;
		t->palm.first = t->point;
		return true;
	} else if (!tp->dwt.keyboard_active &&
		   t->state == TOUCH_UPDATE &&
		   t->palm.state == PALM_TYPING) {
		/* If a touch has started before the first or after the last
		   key press, release it on timeout. Benefit: a palm rested
		   while typing on the touchpad will be ignored, but a touch
		   started once we stop typing will be able to control the
		   pointer (alas not tap, etc.).
		   */
		if (t->palm.time == 0 ||
		    t->palm.time > tp->dwt.keyboard_last_press_time) {
			t->palm.state = PALM_NONE;
			evdev_log_debug(tp->device,
					"palm: touch %d released, timeout after typing\n",
					t->index);
		}
	}

	return false;
}

static bool
tp_palm_detect_trackpoint_triggered(struct tp_dispatch *tp,
				    struct tp_touch *t,
				    uint64_t time)
{
	if (!tp->palm.monitor_trackpoint)
		return false;

	if (t->palm.state == PALM_NONE &&
	    t->state == TOUCH_BEGIN &&
	    tp->palm.trackpoint_active) {
		t->palm.state = PALM_TRACKPOINT;
		return true;
	} else if (t->palm.state == PALM_TRACKPOINT &&
		   t->state == TOUCH_UPDATE &&
		   !tp->palm.trackpoint_active) {

		if (t->palm.time == 0 ||
		    t->palm.time > tp->palm.trackpoint_last_event_time) {
			t->palm.state = PALM_NONE;
			evdev_log_debug(tp->device,
				       "palm: touch %d released, timeout after trackpoint\n", t->index);
		}
	}

	return false;
}

static bool
tp_palm_detect_tool_triggered(struct tp_dispatch *tp,
			      struct tp_touch *t,
			      uint64_t time)
{
	if (!tp->palm.use_mt_tool)
		return false;

	if (t->palm.state != PALM_NONE &&
	    t->palm.state != PALM_TOOL_PALM)
		return false;

	if (t->palm.state == PALM_NONE &&
	    t->is_tool_palm)
		t->palm.state = PALM_TOOL_PALM;
	else if (t->palm.state == PALM_TOOL_PALM &&
		 !t->is_tool_palm)
		t->palm.state = PALM_NONE;

	if (t->palm.state == PALM_TOOL_PALM)
		tp_stop_actions(tp, time);

	return t->palm.state == PALM_TOOL_PALM;
}

static inline bool
tp_palm_detect_move_out_of_edge(struct tp_dispatch *tp,
				struct tp_touch *t,
				uint64_t time)
{
	const int PALM_TIMEOUT = ms2us(200);
	int directions = 0;
	struct device_float_coords delta;
	int dirs;

	if (time < t->palm.time + PALM_TIMEOUT && !tp_palm_in_edge(tp, t)) {
		if (tp_palm_was_in_side_edge(tp, t))
			directions = NE|E|SE|SW|W|NW;
		else if (tp_palm_was_in_top_edge(tp, t))
			directions = S|SE|SW;

		if (directions) {
			delta = device_delta(t->point, t->palm.first);
			dirs = phys_get_direction(tp_phys_delta(tp, delta));
			if ((dirs & directions) && !(dirs & ~directions))
				return true;
		}
	}

	return false;
}

static inline bool
tp_palm_detect_multifinger(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	struct tp_touch *other;

	if (tp->nfingers_down < 2)
		return false;

	/* If we have at least one other active non-palm touch make this
	 * touch non-palm too. This avoids palm detection during two-finger
	 * scrolling.
	 *
	 * Note: if both touches start in the palm zone within the same
	 * frame the second touch will still be PALM_NONE and thus detected
	 * here as non-palm touch. This is too niche to worry about for now.
	 */
	tp_for_each_touch(tp, other) {
		if (other == t)
			continue;

		if (tp_touch_active(tp, other) &&
		    other->palm.state == PALM_NONE) {
			return true;
		}
	}

	return false;
}

static inline bool
tp_palm_detect_touch_size_triggered(struct tp_dispatch *tp,
				    struct tp_touch *t,
				    uint64_t time)
{
	if (!tp->palm.use_size)
		return false;

	/* If a finger size is large enough for palm, we stick with that and
	 * force the user to release and reset the finger */
	if (t->palm.state != PALM_NONE && t->palm.state != PALM_TOUCH_SIZE)
		return false;

	if (t->major > tp->palm.size_threshold ||
	    t->minor > tp->palm.size_threshold) {
		if (t->palm.state != PALM_TOUCH_SIZE)
			evdev_log_debug(tp->device,
					"palm: touch %d size exceeded\n",
					t->index);
		t->palm.state = PALM_TOUCH_SIZE;
		return true;
	}

	return false;
}

static inline bool
tp_palm_detect_edge(struct tp_dispatch *tp,
		    struct tp_touch *t,
		    uint64_t time)
{
	if (t->palm.state == PALM_EDGE) {
		if (tp_palm_detect_multifinger(tp, t, time)) {
			t->palm.state = PALM_NONE;
			evdev_log_debug(tp->device,
				  "palm: touch %d released, multiple fingers\n",
				  t->index);

		/* If labelled a touch as palm, we unlabel as palm when
		   we move out of the palm edge zone within the timeout, provided
		   the direction is within 45 degrees of the horizontal.
		 */
		} else if (tp_palm_detect_move_out_of_edge(tp, t, time)) {
			t->palm.state = PALM_NONE;
			evdev_log_debug(tp->device,
				  "palm: touch %d released, out of edge zone\n",
				  t->index);
		}
		return false;
	} else if (tp_palm_detect_multifinger(tp, t, time)) {
		return false;
	}

	/* palm must start in exclusion zone, it's ok to move into
	   the zone without being a palm */
	if (t->state != TOUCH_BEGIN || !tp_palm_in_edge(tp, t))
		return false;

	/* don't detect palm in software button areas, it's
	   likely that legitimate touches start in the area
	   covered by the exclusion zone */
	if (tp->buttons.is_clickpad &&
	    tp_button_is_inside_softbutton_area(tp, t))
		return false;

	if (tp_touch_get_edge(tp, t) & EDGE_RIGHT)
		return false;

	t->palm.state = PALM_EDGE;
	t->palm.time = time;
	t->palm.first = t->point;

	return true;
}

static bool
tp_palm_detect_pressure_triggered(struct tp_dispatch *tp,
				  struct tp_touch *t,
				  uint64_t time)
{
	if (!tp->palm.use_pressure)
		return false;

	if (t->palm.state != PALM_NONE &&
	    t->palm.state != PALM_PRESSURE)
		return false;

	if (t->pressure > tp->palm.pressure_threshold)
		t->palm.state = PALM_PRESSURE;

	return t->palm.state == PALM_PRESSURE;
}

static bool
tp_palm_detect_arbitration_triggered(struct tp_dispatch *tp,
				     struct tp_touch *t,
				     uint64_t time)
{
	if (!tp->arbitration.in_arbitration)
		return false;

	t->palm.state = PALM_ARBITRATION;

	return true;
}

static void
tp_palm_detect(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	const char *palm_state;
	enum touch_palm_state oldstate = t->palm.state;

	if (tp_palm_detect_pressure_triggered(tp, t, time))
		goto out;

	if (tp_palm_detect_arbitration_triggered(tp, t, time))
		goto out;

	if (tp_palm_detect_dwt_triggered(tp, t, time))
		goto out;

	if (tp_palm_detect_trackpoint_triggered(tp, t, time))
		goto out;

	if (tp_palm_detect_tool_triggered(tp, t, time))
		goto out;

	if (tp_palm_detect_touch_size_triggered(tp, t, time))
		goto out;

	if (tp_palm_detect_edge(tp, t, time))
		goto out;

	/* Pressure is highest priority because it cannot be released and
	 * overrides all other checks. So we check once before anything else
	 * in case pressure triggers on a non-palm touch. And again after
	 * everything in case one of the others released but we have a
	 * pressure trigger now.
	 */
	if (tp_palm_detect_pressure_triggered(tp, t, time))
		goto out;

	return;
out:

	if (oldstate == t->palm.state)
		return;

	switch (t->palm.state) {
	case PALM_EDGE:
		palm_state = "edge";
		break;
	case PALM_TYPING:
		palm_state = "typing";
		break;
	case PALM_TRACKPOINT:
		palm_state = "trackpoint";
		break;
	case PALM_TOOL_PALM:
		palm_state = "tool-palm";
		break;
	case PALM_PRESSURE:
		palm_state = "pressure";
		break;
	case PALM_TOUCH_SIZE:
		palm_state = "touch size";
		break;
	case PALM_ARBITRATION:
		palm_state = "arbitration";
		break;
	case PALM_NONE:
	default:
		abort();
		break;
	}
	evdev_log_debug(tp->device,
		  "palm: touch %d, palm detected (%s)\n",
		  t->index,
		  palm_state);
}

static inline const char*
thumb_state_to_str(enum tp_thumb_state state)
{
	switch(state){
	CASE_RETURN_STRING(THUMB_STATE_NO);
	CASE_RETURN_STRING(THUMB_STATE_YES);
	CASE_RETURN_STRING(THUMB_STATE_MAYBE);
	}

	return NULL;
}

static void
tp_thumb_detect(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	enum tp_thumb_state state = t->thumb.state;

	/* once a thumb, always a thumb, once ruled out always ruled out */
	if (!tp->thumb.detect_thumbs ||
	    t->thumb.state != THUMB_STATE_MAYBE)
		return;

	if (t->point.y < tp->thumb.upper_thumb_line) {
		/* if a potential thumb is above the line, it won't ever
		 * label as thumb */
		t->thumb.state = THUMB_STATE_NO;
		goto out;
	}

	/* If the thumb moves by more than 7mm, it's not a resting thumb */
	if (t->state == TOUCH_BEGIN)
		t->thumb.initial = t->point;
	else if (t->state == TOUCH_UPDATE) {
		struct device_float_coords delta;
		struct phys_coords mm;

		delta = device_delta(t->point, t->thumb.initial);
		mm = tp_phys_delta(tp, delta);
		if (length_in_mm(mm) > 7) {
			t->thumb.state = THUMB_STATE_NO;
			goto out;
		}
	}

	/* Note: a thumb at the edge of the touchpad won't trigger the
	 * threshold, the surface area is usually too small. So we have a
	 * two-stage detection: pressure and time within the area.
	 * A finger that remains at the very bottom of the touchpad becomes
	 * a thumb.
	 */
	if (t->pressure > tp->thumb.threshold)
		t->thumb.state = THUMB_STATE_YES;
	else if (t->point.y > tp->thumb.lower_thumb_line &&
		 tp->scroll.method != LIBINPUT_CONFIG_SCROLL_EDGE &&
		 t->thumb.first_touch_time + THUMB_MOVE_TIMEOUT < time)
		t->thumb.state = THUMB_STATE_YES;

	/* now what? we marked it as thumb, so:
	 *
	 * - pointer motion must ignore this touch
	 * - clickfinger must ignore this touch for finger count
	 * - software buttons are unaffected
	 * - edge scrolling unaffected
	 * - gestures: unaffected
	 * - tapping: honour thumb on begin, ignore it otherwise for now,
	 *   this gets a tad complicated otherwise
	 */
out:
	if (t->thumb.state != state)
		evdev_log_debug(tp->device,
			  "thumb state: touch %d, %s → %s\n",
			  t->index,
			  thumb_state_to_str(state),
			  thumb_state_to_str(t->thumb.state));
}

static void
tp_unhover_pressure(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	int i;
	unsigned int nfake_touches;
	unsigned int real_fingers_down = 0;

	nfake_touches = tp_fake_finger_count(tp);
	if (nfake_touches == FAKE_FINGER_OVERFLOW)
		nfake_touches = 0;

	for (i = 0; i < (int)tp->num_slots; i++) {
		t = tp_get_touch(tp, i);

		if (t->state == TOUCH_NONE)
			continue;

		if (t->dirty) {
			if (t->state == TOUCH_HOVERING) {
				if (t->pressure >= tp->pressure.high) {
					evdev_log_debug(tp->device,
							"pressure: begin touch %d\n",
							t->index);
					/* avoid jumps when landing a finger */
					tp_motion_history_reset(t);
					tp_begin_touch(tp, t, time);
				}
			/* don't unhover for pressure if we have too many
			 * fake fingers down, see comment below. Except
			 * for single-finger touches where the real touch
			 * decides for the rest.
			 */
			} else if (nfake_touches <= tp->num_slots ||
				   tp->num_slots == 1) {
				if (t->pressure < tp->pressure.low) {
					evdev_log_debug(tp->device,
							"pressure: end touch %d\n",
							t->index);
					tp_maybe_end_touch(tp, t, time);
				}
			}
		}

		if (t->state == TOUCH_BEGIN ||
		    t->state == TOUCH_UPDATE)
			real_fingers_down++;
	}

	if (nfake_touches <= tp->num_slots ||
	    tp->nfingers_down == 0)
		return;

	/* if we have more fake fingers down than slots, we assume
	 * _all_ fingers have enough pressure, even if some of the slotted
	 * ones don't. Anything else gets insane quickly.
	 */
	if (real_fingers_down > 0) {
		tp_for_each_touch(tp, t) {
			if (t->state == TOUCH_HOVERING) {
				/* avoid jumps when landing a finger */
				tp_motion_history_reset(t);
				tp_begin_touch(tp, t, time);

				if (tp->nfingers_down >= nfake_touches)
					break;
			}
		}
	}

	if (tp->nfingers_down > nfake_touches ||
	    real_fingers_down == 0) {
		for (i = tp->ntouches - 1; i >= 0; i--) {
			t = tp_get_touch(tp, i);

			if (t->state == TOUCH_HOVERING ||
			    t->state == TOUCH_NONE ||
			    t->state == TOUCH_MAYBE_END)
				continue;

			tp_maybe_end_touch(tp, t, time);

			if (real_fingers_down > 0  &&
			    tp->nfingers_down == nfake_touches)
				break;
		}
	}
}

static void
tp_unhover_size(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	int low = tp->touch_size.low,
	    high = tp->touch_size.high;
	int i;

	/* We require 5 slots for size handling, so we don't need to care
	 * about fake touches here */

	for (i = 0; i < (int)tp->num_slots; i++) {
		t = tp_get_touch(tp, i);

		if (t->state == TOUCH_NONE)
			continue;

		if (!t->dirty)
			continue;

		if (t->state == TOUCH_HOVERING) {
			if ((t->major > high && t->minor > low) ||
			    (t->major > low && t->minor > high)) {
				evdev_log_debug(tp->device,
						"touch-size: begin touch %d\n",
						t->index);
				/* avoid jumps when landing a finger */
				tp_motion_history_reset(t);
				tp_begin_touch(tp, t, time);
			}
		} else {
			if (t->major < low || t->minor < low) {
				evdev_log_debug(tp->device,
						"touch-size: end touch %d\n",
						t->index);
				tp_maybe_end_touch(tp, t, time);
			}
		}
	}
}

static void
tp_unhover_fake_touches(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	unsigned int nfake_touches;
	int i;

	if (!tp->fake_touches && !tp->nfingers_down)
		return;

	nfake_touches = tp_fake_finger_count(tp);
	if (nfake_touches == FAKE_FINGER_OVERFLOW)
		return;

	if (tp->nfingers_down == nfake_touches &&
	    ((tp->nfingers_down == 0 && !tp_fake_finger_is_touching(tp)) ||
	     (tp->nfingers_down > 0 && tp_fake_finger_is_touching(tp))))
		return;

	/* if BTN_TOUCH is set and we have less fingers down than fake
	 * touches, switch each hovering touch to BEGIN
	 * until nfingers_down matches nfake_touches
	 */
	if (tp_fake_finger_is_touching(tp) &&
	    tp->nfingers_down < nfake_touches) {
		tp_for_each_touch(tp, t) {
			if (t->state == TOUCH_HOVERING) {
				tp_begin_touch(tp, t, time);

				if (tp->nfingers_down >= nfake_touches)
					break;
			}
		}
	}

	/* if BTN_TOUCH is unset end all touches, we're hovering now. If we
	 * have too many touches also end some of them. This is done in
	 * reverse order.
	 */
	if (tp->nfingers_down > nfake_touches ||
	    !tp_fake_finger_is_touching(tp)) {
		for (i = tp->ntouches - 1; i >= 0; i--) {
			t = tp_get_touch(tp, i);

			if (t->state == TOUCH_HOVERING ||
			    t->state == TOUCH_NONE)
				continue;

			tp_maybe_end_touch(tp, t, time);

			if (tp_fake_finger_is_touching(tp) &&
			    tp->nfingers_down == nfake_touches)
				break;
		}
	}
}

static void
tp_unhover_touches(struct tp_dispatch *tp, uint64_t time)
{
	if (tp->pressure.use_pressure)
		tp_unhover_pressure(tp, time);
	else if (tp->touch_size.use_touch_size)
		tp_unhover_size(tp, time);
	else
		tp_unhover_fake_touches(tp, time);

}

static inline void
tp_position_fake_touches(struct tp_dispatch *tp)
{
	struct tp_touch *t;
	struct tp_touch *topmost = NULL;
	unsigned int start, i;

	if (tp_fake_finger_count(tp) <= tp->num_slots ||
	    tp->nfingers_down == 0)
		return;

	/* We have at least one fake touch down. Find the top-most real
	 * touch and copy its coordinates over to to all fake touches.
	 * This is more reliable than just taking the first touch.
	 */
	for (i = 0; i < tp->num_slots; i++) {
		t = tp_get_touch(tp, i);
		if (t->state == TOUCH_END ||
		    t->state == TOUCH_NONE)
			continue;

		if (topmost == NULL || t->point.y < topmost->point.y)
			topmost = t;
	}

	if (!topmost) {
		evdev_log_bug_libinput(tp->device,
				       "Unable to find topmost touch\n");
		return;
	}

	start = tp->has_mt ? tp->num_slots : 1;
	for (i = start; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);
		if (t->state == TOUCH_NONE)
			continue;

		t->point = topmost->point;
		t->pressure = topmost->pressure;
		if (!t->dirty)
			t->dirty = topmost->dirty;
	}
}

static inline bool
tp_need_motion_history_reset(struct tp_dispatch *tp)
{
	bool rc = false;

	/* Changing the numbers of fingers can cause a jump in the
	 * coordinates, always reset the motion history for all touches when
	 * that happens.
	 */
	if (tp->nfingers_down != tp->old_nfingers_down)
		return true;

	/* Quirk: if we had multiple events without x/y axis
	   information, the next x/y event is going to be a jump. So we
	   reset that touch to non-dirty effectively swallowing that event
	   and restarting with the next event again.
	 */
	if (tp->device->model_flags & EVDEV_MODEL_LENOVO_T450_TOUCHPAD) {
		if (tp->queued & TOUCHPAD_EVENT_MOTION) {
			if (tp->quirks.nonmotion_event_count > 10) {
				tp->queued &= ~TOUCHPAD_EVENT_MOTION;
				rc = true;
			}
			tp->quirks.nonmotion_event_count = 0;
		}

		if ((tp->queued & (TOUCHPAD_EVENT_OTHERAXIS|TOUCHPAD_EVENT_MOTION)) ==
		    TOUCHPAD_EVENT_OTHERAXIS)
			tp->quirks.nonmotion_event_count++;
	}

	return rc;
}

static bool
tp_detect_jumps(const struct tp_dispatch *tp, struct tp_touch *t)
{
	struct device_coords delta;
	struct phys_coords mm;
	const int JUMP_THRESHOLD_MM = 20;
	struct tp_history_point *last;

	/* We haven't seen pointer jumps on Wacom tablets yet, so exclude
	 * those.
	 */
	if (tp->device->model_flags & EVDEV_MODEL_WACOM_TOUCHPAD)
		return false;

	if (t->history.count == 0)
		return false;

	/* called before tp_motion_history_push, so offset 0 is the most
	 * recent coordinate */
	last = tp_motion_history_offset(t, 0);
	delta.x = abs(t->point.x - last->point.x);
	delta.y = abs(t->point.y - last->point.y);
	mm = evdev_device_unit_delta_to_mm(tp->device, &delta);

	return hypot(mm.x, mm.y) > JUMP_THRESHOLD_MM;
}

static void
tp_detect_thumb_while_moving(struct tp_dispatch *tp)
{
	struct tp_touch *t;
	struct tp_touch *first = NULL,
			*second = NULL;
	struct device_coords distance;
	struct phys_coords mm;

	tp_for_each_touch(tp, t) {
		if (t->state != TOUCH_BEGIN)
			first = t;
		else
			second = t;

		if (first && second)
			break;
	}

	assert(first);
	assert(second);

	if (tp->scroll.method == LIBINPUT_CONFIG_SCROLL_2FG) {
		/* If the second finger comes down next to the other one, we
		 * assume this is a scroll motion.
		 */
		distance.x = abs(first->point.x - second->point.x);
		distance.y = abs(first->point.y - second->point.y);
		mm = evdev_device_unit_delta_to_mm(tp->device, &distance);

		if (mm.x <= 25 && mm.y <= 15)
			return;
	}

	/* Finger are too far apart or 2fg scrolling is disabled, mark
	 * second finger as thumb */
	evdev_log_debug(tp->device,
			"touch %d is speed-based thumb\n",
			second->index);
	second->thumb.state = THUMB_STATE_YES;
}

static void
tp_pre_process_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;

	tp_process_fake_touches(tp, time);
	tp_unhover_touches(tp, time);

	tp_for_each_touch(tp, t) {
		if (t->state == TOUCH_MAYBE_END)
			tp_end_touch(tp, t, time);
	}

}

static void
tp_process_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	bool restart_filter = false;
	bool want_motion_reset;
	bool have_new_touch = false;
	unsigned int speed_exceeded_count = 0;

	tp_position_fake_touches(tp);

	want_motion_reset = tp_need_motion_history_reset(tp);

	tp_for_each_touch(tp, t) {
		if (t->state == TOUCH_NONE)
			continue;

		if (want_motion_reset) {
			tp_motion_history_reset(t);
			t->quirks.reset_motion_history = true;
		} else if (t->quirks.reset_motion_history) {
			tp_motion_history_reset(t);
			t->quirks.reset_motion_history = false;
		}

		if (!t->dirty) {
			/* A non-dirty touch must be below the speed limit */
			if (t->speed.exceeded_count > 0)
				t->speed.exceeded_count--;

			speed_exceeded_count = max(speed_exceeded_count,
						   t->speed.exceeded_count);
			continue;
		}

		if (tp_detect_jumps(tp, t)) {
			if (!tp->semi_mt)
				evdev_log_bug_kernel(tp->device,
					       "Touch jump detected and discarded.\n"
					       "See %stouchpad_jumping_cursor.html for details\n",
					       HTTP_DOC_LINK);
			tp_motion_history_reset(t);
		}

		tp_thumb_detect(tp, t, time);
		tp_palm_detect(tp, t, time);
		tp_detect_wobbling(tp, t, time);
		tp_motion_hysteresis(tp, t);
		tp_motion_history_push(t);

		/* Touch speed handling: if we'are above the threshold,
		 * count each event that we're over the threshold up to 10
		 * events. Count down when we are below the speed.
		 *
		 * Take the touch with the highest speed excess, if it is
		 * above a certain threshold (5, see below), assume a
		 * dropped finger is a thumb.
		 *
		 * Yes, this relies on the touchpad to keep sending us
		 * events even if the finger doesn't move, otherwise we
		 * never count down. Let's see how far we get with that.
		 */
		if (t->speed.last_speed > THUMB_IGNORE_SPEED_THRESHOLD) {
			if (t->speed.exceeded_count < 10)
				t->speed.exceeded_count++;
		} else if (t->speed.exceeded_count > 0) {
				t->speed.exceeded_count--;
		}

		speed_exceeded_count = max(speed_exceeded_count,
					   t->speed.exceeded_count);

		tp_calculate_motion_speed(tp, t);

		tp_unpin_finger(tp, t);

		if (t->state == TOUCH_BEGIN) {
			have_new_touch = true;
			restart_filter = true;
		}
	}

	/* If we have one touch that exceeds the speed and we get a new
	 * touch down while doing that, the second touch is a thumb */
	if (have_new_touch &&
	    tp->nfingers_down == 2 &&
	    speed_exceeded_count > 5)
		tp_detect_thumb_while_moving(tp);

	if (restart_filter)
		filter_restart(tp->device->pointer.filter, tp, time);

	tp_button_handle_state(tp, time);
	tp_edge_scroll_handle_state(tp, time);

	/*
	 * We have a physical button down event on a clickpad. To avoid
	 * spurious pointer moves by the clicking finger we pin all fingers.
	 * We unpin fingers when they move more then a certain threshold to
	 * to allow drag and drop.
	 */
	if ((tp->queued & TOUCHPAD_EVENT_BUTTON_PRESS) &&
	    tp->buttons.is_clickpad)
		tp_pin_fingers(tp);

	tp_gesture_handle_state(tp, time);
}

static void
tp_post_process_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {

		if (!t->dirty)
			continue;

		if (t->state == TOUCH_END) {
			if (t->has_ended)
				t->state = TOUCH_NONE;
			else
				t->state = TOUCH_HOVERING;
		} else if (t->state == TOUCH_BEGIN) {
			t->state = TOUCH_UPDATE;
		}

		t->dirty = false;
	}

	tp->old_nfingers_down = tp->nfingers_down;
	tp->buttons.old_state = tp->buttons.state;

	tp->queued = TOUCHPAD_EVENT_NONE;

	tp_tap_post_process_state(tp);
}

static void
tp_post_events(struct tp_dispatch *tp, uint64_t time)
{
	int filter_motion = 0;

	/* Only post (top) button events while suspended */
	if (tp->device->is_suspended) {
		tp_post_button_events(tp, time);
		return;
	}

	filter_motion |= tp_tap_handle_state(tp, time);
	filter_motion |= tp_post_button_events(tp, time);

	if (filter_motion ||
	    tp->palm.trackpoint_active ||
	    tp->dwt.keyboard_active) {
		tp_edge_scroll_stop_events(tp, time);
		tp_gesture_cancel(tp, time);
		return;
	}

	if (tp_edge_scroll_post_events(tp, time) != 0)
		return;

	tp_gesture_post_events(tp, time);
}

static void
tp_handle_state(struct tp_dispatch *tp,
		uint64_t time)
{
	tp_pre_process_state(tp, time);
	tp_process_state(tp, time);
	tp_post_events(tp, time);
	tp_post_process_state(tp, time);

	tp_clickpad_middlebutton_apply_config(tp->device);
}

static inline void
tp_debug_touch_state(struct tp_dispatch *tp,
		     struct evdev_device *device)
{
	char buf[1024] = {0};
	struct tp_touch *t;
	size_t i = 0;

	tp_for_each_touch(tp, t) {
		if (i >= tp->nfingers_down)
			break;
		sprintf(&buf[strlen(buf)],
			"slot %zd: %04d/%04d p%03d %s |",
			i++,
			t->point.x,
			t->point.y,
			t->pressure,
			tp_touch_active(tp, t) ? "" : "inactive");
	}
	evdev_log_debug(device, "touch state: %s\n", buf);
}

static void
tp_interface_process(struct evdev_dispatch *dispatch,
		     struct evdev_device *device,
		     struct input_event *e,
		     uint64_t time)
{
	struct tp_dispatch *tp = tp_dispatch(dispatch);

	switch (e->type) {
	case EV_ABS:
		if (tp->has_mt)
			tp_process_absolute(tp, e, time);
		else
			tp_process_absolute_st(tp, e, time);
		break;
	case EV_KEY:
		tp_process_key(tp, e, time);
		break;
	case EV_SYN:
		tp_handle_state(tp, time);
#if 0
		tp_debug_touch_state(tp, device);
#endif
		break;
	}
}

static void
tp_remove_sendevents(struct tp_dispatch *tp)
{
	struct evdev_paired_keyboard *kbd;

	libinput_timer_cancel(&tp->palm.trackpoint_timer);
	libinput_timer_cancel(&tp->dwt.keyboard_timer);

	if (tp->buttons.trackpoint &&
	    tp->palm.monitor_trackpoint)
		libinput_device_remove_event_listener(
					&tp->palm.trackpoint_listener);

	list_for_each(kbd, &tp->dwt.paired_keyboard_list, link) {
		libinput_device_remove_event_listener(&kbd->listener);
	}

	if (tp->lid_switch.lid_switch)
		libinput_device_remove_event_listener(
					&tp->lid_switch.listener);

	if (tp->tablet_mode_switch.tablet_mode_switch)
		libinput_device_remove_event_listener(
					&tp->tablet_mode_switch.listener);
}

static void
tp_interface_remove(struct evdev_dispatch *dispatch)
{
	struct tp_dispatch *tp = tp_dispatch(dispatch);

	tp_remove_tap(tp);
	tp_remove_buttons(tp);
	tp_remove_sendevents(tp);
	tp_remove_edge_scroll(tp);
	tp_remove_gesture(tp);
}

static void
tp_interface_destroy(struct evdev_dispatch *dispatch)
{
	struct tp_dispatch *tp = tp_dispatch(dispatch);

	libinput_timer_cancel(&tp->arbitration.arbitration_timer);
	libinput_timer_destroy(&tp->arbitration.arbitration_timer);
	libinput_timer_destroy(&tp->palm.trackpoint_timer);
	libinput_timer_destroy(&tp->dwt.keyboard_timer);
	libinput_timer_destroy(&tp->tap.timer);
	libinput_timer_destroy(&tp->gesture.finger_count_switch_timer);
	free(tp->touches);
	free(tp);
}

static void
tp_release_fake_touches(struct tp_dispatch *tp)
{
	tp->fake_touches = 0;
}

static void
tp_clear_state(struct tp_dispatch *tp)
{
	uint64_t now = libinput_now(tp_libinput_context(tp));
	struct tp_touch *t;

	/* Unroll the touchpad state.
	 * Release buttons first. If tp is a clickpad, the button event
	 * must come before the touch up. If it isn't, the order doesn't
	 * matter anyway
	 *
	 * Then cancel all timeouts on the taps, triggering the last set
	 * of events.
	 *
	 * Then lift all touches so the touchpad is in a neutral state.
	 *
	 */
	tp_release_all_buttons(tp, now);
	tp_release_all_taps(tp, now);

	tp_for_each_touch(tp, t) {
		tp_end_sequence(tp, t, now);
	}
	tp_release_fake_touches(tp);

	tp_handle_state(tp, now);
}

static void
tp_suspend(struct tp_dispatch *tp,
	   struct evdev_device *device,
	   enum suspend_trigger trigger)
{
	if (tp->suspend_reason & trigger)
		return;

	if (tp->suspend_reason != 0)
		goto out;

	tp_clear_state(tp);

	/* On devices with top softwarebuttons we don't actually suspend the
	 * device, to keep the "trackpoint" buttons working. tp_post_events()
	 * will only send events for the trackpoint while suspended.
	 */
	if (tp->buttons.has_topbuttons) {
		evdev_notify_suspended_device(device);
		/* Enlarge topbutton area while suspended */
		tp_init_top_softbuttons(tp, device, 3.0);
	} else {
		evdev_device_suspend(device);
	}

out:
	tp->suspend_reason |= trigger;
}

static void
tp_interface_suspend(struct evdev_dispatch *dispatch,
		     struct evdev_device *device)
{
	struct tp_dispatch *tp = tp_dispatch(dispatch);

	tp_clear_state(tp);
}

static inline void
tp_sync_touch(struct tp_dispatch *tp,
	      struct evdev_device *device,
	      struct tp_touch *t,
	      int slot)
{
	struct libevdev *evdev = device->evdev;

	if (!libevdev_fetch_slot_value(evdev,
				       slot,
				       ABS_MT_POSITION_X,
				       &t->point.x))
		t->point.x = libevdev_get_event_value(evdev, EV_ABS, ABS_X);
	if (!libevdev_fetch_slot_value(evdev,
				       slot,
				       ABS_MT_POSITION_Y,
				       &t->point.y))
		t->point.y = libevdev_get_event_value(evdev, EV_ABS, ABS_Y);

	if (!libevdev_fetch_slot_value(evdev,
				       slot,
				       ABS_MT_PRESSURE,
				       &t->pressure))
		t->pressure = libevdev_get_event_value(evdev,
						       EV_ABS,
						       ABS_PRESSURE);

	libevdev_fetch_slot_value(evdev,
				  slot,
				  ABS_MT_TOUCH_MAJOR,
				  &t->major);
	libevdev_fetch_slot_value(evdev,
				  slot,
				  ABS_MT_TOUCH_MINOR,
				  &t->minor);
}

static void
tp_sync_slots(struct tp_dispatch *tp,
	      struct evdev_device *device)
{
	/* Always sync the first touch so we get ABS_X/Y synced on
	 * single-touch touchpads */
	tp_sync_touch(tp, device, &tp->touches[0], 0);
	for (unsigned int i = 1; i < tp->num_slots; i++)
		tp_sync_touch(tp, device, &tp->touches[i], i);
}

static void
tp_resume(struct tp_dispatch *tp,
	  struct evdev_device *device,
	  enum suspend_trigger trigger)
{
	tp->suspend_reason &= ~trigger;
	if (tp->suspend_reason != 0)
		return;

	if (tp->buttons.has_topbuttons) {
		/* tap state-machine is offline while suspended, reset state */
		tp_clear_state(tp);
		/* restore original topbutton area size */
		tp_init_top_softbuttons(tp, device, 1.0);
		evdev_notify_resumed_device(device);
	} else {
		evdev_device_resume(device);
	}

	tp_sync_slots(tp, device);
}

static void
tp_trackpoint_timeout(uint64_t now, void *data)
{
	struct tp_dispatch *tp = data;

	if (tp->palm.trackpoint_active) {
		tp_tap_resume(tp, now);
		tp->palm.trackpoint_active = false;
	}
	tp->palm.trackpoint_event_count = 0;
}

static void
tp_trackpoint_event(uint64_t time, struct libinput_event *event, void *data)
{
	struct tp_dispatch *tp = data;

	/* Buttons do not count as trackpad activity, as people may use
	   the trackpoint buttons in combination with the touchpad. */
	if (event->type == LIBINPUT_EVENT_POINTER_BUTTON)
		return;

	tp->palm.trackpoint_last_event_time = time;
	tp->palm.trackpoint_event_count++;


	/* Require at least three events before enabling palm detection */
	if (tp->palm.trackpoint_event_count < 3) {
		libinput_timer_set(&tp->palm.trackpoint_timer,
				   time + DEFAULT_TRACKPOINT_EVENT_TIMEOUT);
		return;
	}

	if (!tp->palm.trackpoint_active) {
		tp_stop_actions(tp, time);
		tp->palm.trackpoint_active = true;
	}

	libinput_timer_set(&tp->palm.trackpoint_timer,
			   time + DEFAULT_TRACKPOINT_ACTIVITY_TIMEOUT);
}

static void
tp_keyboard_timeout(uint64_t now, void *data)
{
	struct tp_dispatch *tp = data;

	if (tp->dwt.dwt_enabled &&
	    long_any_bit_set(tp->dwt.key_mask,
			     ARRAY_LENGTH(tp->dwt.key_mask))) {
		libinput_timer_set(&tp->dwt.keyboard_timer,
				   now + DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_2);
		tp->dwt.keyboard_last_press_time = now;
		evdev_log_debug(tp->device, "palm: keyboard timeout refresh\n");
		return;
	}

	tp_tap_resume(tp, now);

	tp->dwt.keyboard_active = false;

	evdev_log_debug(tp->device, "palm: keyboard timeout\n");
}

static inline bool
tp_key_is_modifier(unsigned int keycode)
{
	switch (keycode) {
	/* Ignore modifiers to be responsive to ctrl-click, alt-tab, etc. */
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:
	case KEY_LEFTALT:
	case KEY_RIGHTALT:
	case KEY_LEFTSHIFT:
	case KEY_RIGHTSHIFT:
	case KEY_FN:
	case KEY_CAPSLOCK:
	case KEY_TAB:
	case KEY_COMPOSE:
	case KEY_RIGHTMETA:
	case KEY_LEFTMETA:
		return true;
	default:
		return false;
	}
}

static inline bool
tp_key_ignore_for_dwt(unsigned int keycode)
{
	/* Ignore keys not part of the "typewriter set", i.e. F-keys,
	 * multimedia keys, numpad, etc.
	 */

	if (tp_key_is_modifier(keycode))
		return false;

	return keycode >= KEY_F1;
}

static void
tp_keyboard_event(uint64_t time, struct libinput_event *event, void *data)
{
	struct tp_dispatch *tp = data;
	struct libinput_event_keyboard *kbdev;
	unsigned int timeout;
	unsigned int key;
	bool is_modifier;

	if (event->type != LIBINPUT_EVENT_KEYBOARD_KEY)
		return;

	kbdev = libinput_event_get_keyboard_event(event);
	key = libinput_event_keyboard_get_key(kbdev);

	/* Only trigger the timer on key down. */
	if (libinput_event_keyboard_get_key_state(kbdev) !=
	    LIBINPUT_KEY_STATE_PRESSED) {
		long_clear_bit(tp->dwt.key_mask, key);
		long_clear_bit(tp->dwt.mod_mask, key);
		return;
	}

	if (!tp->dwt.dwt_enabled)
		return;

	if (tp_key_ignore_for_dwt(key))
		return;

	/* modifier keys don't trigger disable-while-typing so things like
	 * ctrl+zoom or ctrl+click are possible */
	is_modifier = tp_key_is_modifier(key);
	if (is_modifier) {
		long_set_bit(tp->dwt.mod_mask, key);
		return;
	}

	if (!tp->dwt.keyboard_active) {
		/* This is the first non-modifier key press. Check if the
		 * modifier mask is set. If any modifier is down we don't
		 * trigger dwt because it's likely to be combination like
		 * Ctrl+S or similar */

		if (long_any_bit_set(tp->dwt.mod_mask,
				     ARRAY_LENGTH(tp->dwt.mod_mask)))
		    return;

		tp_stop_actions(tp, time);
		tp->dwt.keyboard_active = true;
		timeout = DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_1;
	} else {
		timeout = DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_2;
	}

	tp->dwt.keyboard_last_press_time = time;
	long_set_bit(tp->dwt.key_mask, key);
	libinput_timer_set(&tp->dwt.keyboard_timer,
			   time + timeout);
}

static bool
tp_want_dwt(struct evdev_device *touchpad,
	    struct evdev_device *keyboard)
{
	unsigned int vendor_tp = evdev_device_get_id_vendor(touchpad);
	unsigned int vendor_kbd = evdev_device_get_id_vendor(keyboard);
	unsigned int product_tp = evdev_device_get_id_product(touchpad);
	unsigned int product_kbd = evdev_device_get_id_product(keyboard);

	/* External touchpads with the same vid/pid as the keyboard are
	   considered a happy couple */
	if (touchpad->tags & EVDEV_TAG_EXTERNAL_TOUCHPAD)
		return vendor_tp == vendor_kbd && product_tp == product_kbd;
	else if (keyboard->tags & EVDEV_TAG_INTERNAL_KEYBOARD)
		return true;

	/* keyboard is not tagged as internal keyboard and it's not part of
	 * a combo */
	return false;
}

static void
tp_dwt_pair_keyboard(struct evdev_device *touchpad,
		     struct evdev_device *keyboard)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)touchpad->dispatch;
	struct evdev_paired_keyboard *kbd;
	size_t count = 0;

	if ((keyboard->tags & EVDEV_TAG_KEYBOARD) == 0)
		return;

	if (!tp_want_dwt(touchpad, keyboard))
		return;

	list_for_each(kbd, &tp->dwt.paired_keyboard_list, link) {
		count++;
		if (count > 3) {
			evdev_log_info(touchpad,
				       "too many internal keyboards for dwt\n");
			break;
		}
	}

	kbd = zalloc(sizeof(*kbd));
	kbd->device = keyboard;
	libinput_device_add_event_listener(&keyboard->base,
					   &kbd->listener,
					   tp_keyboard_event, tp);
	list_insert(&tp->dwt.paired_keyboard_list, &kbd->link);
	evdev_log_debug(touchpad,
			"palm: dwt activated with %s<->%s\n",
			touchpad->devname,
			keyboard->devname);
}

static void
tp_pair_trackpoint(struct evdev_device *touchpad,
			struct evdev_device *trackpoint)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)touchpad->dispatch;
	unsigned int bus_tp = libevdev_get_id_bustype(touchpad->evdev),
		     bus_trp = libevdev_get_id_bustype(trackpoint->evdev);
	bool tp_is_internal, trp_is_internal;

	if ((trackpoint->tags & EVDEV_TAG_TRACKPOINT) == 0)
		return;

	tp_is_internal = bus_tp != BUS_USB && bus_tp != BUS_BLUETOOTH;
	trp_is_internal = bus_trp != BUS_USB && bus_trp != BUS_BLUETOOTH;

	if (tp->buttons.trackpoint == NULL &&
	    tp_is_internal && trp_is_internal) {
		/* Don't send any pending releases to the new trackpoint */
		tp->buttons.active_is_topbutton = false;
		tp->buttons.trackpoint = trackpoint;
		if (tp->palm.monitor_trackpoint)
			libinput_device_add_event_listener(&trackpoint->base,
						&tp->palm.trackpoint_listener,
						tp_trackpoint_event, tp);
	}
}

static void
tp_lid_switch_event(uint64_t time, struct libinput_event *event, void *data)
{
	struct tp_dispatch *tp = data;
	struct libinput_event_switch *swev;

	if (libinput_event_get_type(event) != LIBINPUT_EVENT_SWITCH_TOGGLE)
		return;

	swev = libinput_event_get_switch_event(event);
	if (libinput_event_switch_get_switch(swev) != LIBINPUT_SWITCH_LID)
		return;

	switch (libinput_event_switch_get_switch_state(swev)) {
	case LIBINPUT_SWITCH_STATE_OFF:
		tp_resume(tp, tp->device, SUSPEND_LID);
		evdev_log_debug(tp->device, "lid: resume touchpad\n");
		break;
	case LIBINPUT_SWITCH_STATE_ON:
		tp_suspend(tp, tp->device, SUSPEND_LID);
		evdev_log_debug(tp->device, "lid: suspending touchpad\n");
		break;
	}
}

static void
tp_tablet_mode_switch_event(uint64_t time,
			    struct libinput_event *event,
			    void *data)
{
	struct tp_dispatch *tp = data;
	struct libinput_event_switch *swev;

	if (libinput_event_get_type(event) != LIBINPUT_EVENT_SWITCH_TOGGLE)
		return;

	swev = libinput_event_get_switch_event(event);
	if (libinput_event_switch_get_switch(swev) !=
	    LIBINPUT_SWITCH_TABLET_MODE)
		return;

	switch (libinput_event_switch_get_switch_state(swev)) {
	case LIBINPUT_SWITCH_STATE_OFF:
		tp_resume(tp, tp->device, SUSPEND_TABLET_MODE);
		evdev_log_debug(tp->device, "tablet-mode: resume touchpad\n");
		break;
	case LIBINPUT_SWITCH_STATE_ON:
		tp_suspend(tp, tp->device, SUSPEND_TABLET_MODE);
		evdev_log_debug(tp->device, "tablet-mode: suspending touchpad\n");
		break;
	}
}

static void
tp_pair_lid_switch(struct evdev_device *touchpad,
		   struct evdev_device *lid_switch)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)touchpad->dispatch;

	if ((lid_switch->tags & EVDEV_TAG_LID_SWITCH) == 0)
		return;

	if (touchpad->tags & EVDEV_TAG_EXTERNAL_TOUCHPAD)
		return;

	if (tp->lid_switch.lid_switch == NULL) {
		evdev_log_debug(touchpad,
				"lid_switch: activated for %s<->%s\n",
				touchpad->devname,
				lid_switch->devname);

		libinput_device_add_event_listener(&lid_switch->base,
						   &tp->lid_switch.listener,
						   tp_lid_switch_event, tp);
		tp->lid_switch.lid_switch = lid_switch;
	}
}

static void
tp_pair_tablet_mode_switch(struct evdev_device *touchpad,
			   struct evdev_device *tablet_mode_switch)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)touchpad->dispatch;

	if ((tablet_mode_switch->tags & EVDEV_TAG_TABLET_MODE_SWITCH) == 0)
		return;

	if (tp->tablet_mode_switch.tablet_mode_switch)
		return;

	if (touchpad->tags & EVDEV_TAG_EXTERNAL_TOUCHPAD)
		return;

	evdev_log_debug(touchpad,
			"tablet_mode_switch: activated for %s<->%s\n",
			touchpad->devname,
			tablet_mode_switch->devname);

	libinput_device_add_event_listener(&tablet_mode_switch->base,
				&tp->tablet_mode_switch.listener,
				tp_tablet_mode_switch_event, tp);
	tp->tablet_mode_switch.tablet_mode_switch = tablet_mode_switch;

	if (evdev_device_switch_get_state(tablet_mode_switch,
					  LIBINPUT_SWITCH_TABLET_MODE)
		    == LIBINPUT_SWITCH_STATE_ON) {
		tp_suspend(tp, touchpad, SUSPEND_TABLET_MODE);
	}
}

static void
tp_interface_device_added(struct evdev_device *device,
			  struct evdev_device *added_device)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)device->dispatch;

	tp_pair_trackpoint(device, added_device);
	tp_dwt_pair_keyboard(device, added_device);
	tp_pair_lid_switch(device, added_device);
	tp_pair_tablet_mode_switch(device, added_device);

	if (tp->sendevents.current_mode !=
	    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE)
		return;

	if (added_device->tags & EVDEV_TAG_EXTERNAL_MOUSE)
		tp_suspend(tp, device, SUSPEND_EXTERNAL_MOUSE);
}

static void
tp_interface_device_removed(struct evdev_device *device,
			    struct evdev_device *removed_device)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)device->dispatch;
	struct evdev_paired_keyboard *kbd, *tmp;

	if (removed_device == tp->buttons.trackpoint) {
		/* Clear any pending releases for the trackpoint */
		if (tp->buttons.active && tp->buttons.active_is_topbutton) {
			tp->buttons.active = 0;
			tp->buttons.active_is_topbutton = false;
		}
		if (tp->palm.monitor_trackpoint)
			libinput_device_remove_event_listener(
						&tp->palm.trackpoint_listener);
		tp->buttons.trackpoint = NULL;
	}

	list_for_each_safe(kbd, tmp, &tp->dwt.paired_keyboard_list, link) {
		if (kbd->device == removed_device) {
			evdev_paired_keyboard_destroy(kbd);
			tp->dwt.keyboard_active = false;
		}
	}

	if (removed_device == tp->lid_switch.lid_switch) {
		libinput_device_remove_event_listener(
					&tp->lid_switch.listener);
		tp->lid_switch.lid_switch = NULL;
		tp_resume(tp, device, SUSPEND_LID);
	}

	if (removed_device == tp->tablet_mode_switch.tablet_mode_switch) {
		libinput_device_remove_event_listener(
					&tp->tablet_mode_switch.listener);
		tp->tablet_mode_switch.tablet_mode_switch = NULL;
		tp_resume(tp, device, SUSPEND_TABLET_MODE);
	}

	if (tp->sendevents.current_mode ==
		    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE) {
		struct libinput_device *dev;
		bool found = false;

		list_for_each(dev, &device->base.seat->devices_list, link) {
			struct evdev_device *d = evdev_device(dev);
			if (d != removed_device &&
			    (d->tags & EVDEV_TAG_EXTERNAL_MOUSE)) {
				found = true;
				break;
			}
		}
		if (!found)
			tp_resume(tp, device, SUSPEND_EXTERNAL_MOUSE);
	}
}

static inline void
evdev_tag_touchpad_internal(struct evdev_device *device)
{
	device->tags |= EVDEV_TAG_INTERNAL_TOUCHPAD;
	device->tags &= ~EVDEV_TAG_EXTERNAL_TOUCHPAD;
}

static inline void
evdev_tag_touchpad_external(struct evdev_device *device)
{
	device->tags |= EVDEV_TAG_EXTERNAL_TOUCHPAD;
	device->tags &= ~EVDEV_TAG_INTERNAL_TOUCHPAD;
}

static void
evdev_tag_touchpad(struct evdev_device *device,
		   struct udev_device *udev_device)
{
	int bustype, vendor;
	const char *prop;

	prop = udev_device_get_property_value(udev_device,
					      "ID_INPUT_TOUCHPAD_INTEGRATION");
	if (prop) {
		if (streq(prop, "internal")) {
			evdev_tag_touchpad_internal(device);
			return;
		} else if (streq(prop, "external")) {
			evdev_tag_touchpad_external(device);
			return;
		} else {
			evdev_log_info(device,
				       "tagged with unknown value %s\n",
				       prop);
		}
	}

	/* simple approach: touchpads on USB or Bluetooth are considered
	 * external, anything else is internal. Exception is Apple -
	 * internal touchpads are connected over USB and it doesn't have
	 * external USB touchpads anyway.
	 */
	bustype = libevdev_get_id_bustype(device->evdev);
	vendor = libevdev_get_id_vendor(device->evdev);

	switch (bustype) {
	case BUS_USB:
		if (device->model_flags & EVDEV_MODEL_APPLE_TOUCHPAD)
			 evdev_tag_touchpad_internal(device);
		break;
	case BUS_BLUETOOTH:
		evdev_tag_touchpad_external(device);
		break;
	default:
		evdev_tag_touchpad_internal(device);
		break;
	}

	switch (vendor) {
	/* Logitech does not have internal touchpads */
	case VENDOR_ID_LOGITECH:
		evdev_tag_touchpad_external(device);
		break;
	}

	/* Wacom makes touchpads, but not internal ones */
	if (device->model_flags & EVDEV_MODEL_WACOM_TOUCHPAD)
		evdev_tag_touchpad_external(device);

	if ((device->tags &
	    (EVDEV_TAG_EXTERNAL_TOUCHPAD|EVDEV_TAG_INTERNAL_TOUCHPAD)) == 0) {
		evdev_log_bug_libinput(device,
				       "Internal or external? Please file a bug.\n");
		evdev_tag_touchpad_external(device);
	}
}

static void
tp_arbitration_timeout(uint64_t now, void *data)
{
	struct tp_dispatch *tp = data;

	if (tp->arbitration.in_arbitration)
		tp->arbitration.in_arbitration = false;
}

static void
tp_interface_toggle_touch(struct evdev_dispatch *dispatch,
			  struct evdev_device *device,
			  bool enable,
			  uint64_t time)
{
	struct tp_dispatch *tp = tp_dispatch(dispatch);
	bool arbitrate = !enable;

	if (arbitrate == tp->arbitration.in_arbitration)
		return;

	if (arbitrate) {
		libinput_timer_cancel(&tp->arbitration.arbitration_timer);
		tp_clear_state(tp);
		tp->arbitration.in_arbitration = true;
	} else {
		/* if in-kernel arbitration is in use and there is a touch
		 * and a pen in proximity, lifting the pen out of proximity
		 * causes a touch being for the touch. On a hand-lift the
		 * proximity out precedes the touch up by a few ms, so we
		 * get what looks like a tap. Fix this by delaying
		 * arbitration by just a little bit so that any touch in
		 * event is caught as palm touch. */
		libinput_timer_set(&tp->arbitration.arbitration_timer,
				   time + ms2us(90));
	}
}

static struct evdev_dispatch_interface tp_interface = {
	.process = tp_interface_process,
	.suspend = tp_interface_suspend,
	.remove = tp_interface_remove,
	.destroy = tp_interface_destroy,
	.device_added = tp_interface_device_added,
	.device_removed = tp_interface_device_removed,
	.device_suspended = tp_interface_device_removed, /* treat as remove */
	.device_resumed = tp_interface_device_added,   /* treat as add */
	.post_added = NULL,
	.toggle_touch = tp_interface_toggle_touch,
	.get_switch_state = NULL,
};

static void
tp_init_touch(struct tp_dispatch *tp,
	      struct tp_touch *t,
	      unsigned int index)
{
	t->tp = tp;
	t->has_ended = true;
	t->index = index;
}

static inline void
tp_disable_abs_mt(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;
	unsigned int code;

	for (code = ABS_MT_SLOT; code <= ABS_MAX; code++)
		libevdev_disable_event_code(evdev, EV_ABS, code);
}

static bool
tp_init_slots(struct tp_dispatch *tp,
	      struct evdev_device *device)
{
	const struct input_absinfo *absinfo;
	struct map {
		unsigned int code;
		int ntouches;
	} max_touches[] = {
		{ BTN_TOOL_QUINTTAP, 5 },
		{ BTN_TOOL_QUADTAP, 4 },
		{ BTN_TOOL_TRIPLETAP, 3 },
		{ BTN_TOOL_DOUBLETAP, 2 },
	};
	struct map *m;
	unsigned int i, n_btn_tool_touches = 1;

	absinfo = libevdev_get_abs_info(device->evdev, ABS_MT_SLOT);
	if (absinfo) {
		tp->num_slots = absinfo->maximum + 1;
		tp->slot = absinfo->value;
		tp->has_mt = true;
	} else {
		tp->num_slots = 1;
		tp->slot = 0;
		tp->has_mt = false;
	}

	tp->semi_mt = libevdev_has_property(device->evdev, INPUT_PROP_SEMI_MT);

	/* Semi-mt devices are not reliable for true multitouch data, so we
	 * simply pretend they're single touch touchpads with BTN_TOOL bits.
	 * Synaptics:
	 * Terrible resolution when two fingers are down,
	 * causing scroll jumps. The single-touch emulation ABS_X/Y is
	 * accurate but the ABS_MT_POSITION touchpoints report the bounding
	 * box and that causes jumps. See https://bugzilla.redhat.com/1235175
	 * Elantech:
	 * On three-finger taps/clicks, one slot doesn't get a coordinate
	 * assigned. See https://bugs.freedesktop.org/show_bug.cgi?id=93583
	 * Alps:
	 * If three fingers are set down in the same frame, one slot has the
	 * coordinates 0/0 and may not get updated for several frames.
	 * See https://bugzilla.redhat.com/show_bug.cgi?id=1295073
	 *
	 * The HP Pavilion DM4 touchpad has random jumps in slots, including
	 * for single-finger movement. See fdo bug 91135
	 */
	if (tp->semi_mt ||
	    device->model_flags & EVDEV_MODEL_HP_PAVILION_DM4_TOUCHPAD) {
		tp->num_slots = 1;
		tp->slot = 0;
		tp->has_mt = false;
	}

	if (!tp->has_mt)
		tp_disable_abs_mt(device);

	ARRAY_FOR_EACH(max_touches, m) {
		if (libevdev_has_event_code(device->evdev,
					    EV_KEY,
					    m->code)) {
			n_btn_tool_touches = m->ntouches;
			break;
		}
	}

	tp->ntouches = max(tp->num_slots, n_btn_tool_touches);
	tp->touches = zalloc(tp->ntouches * sizeof(struct tp_touch));

	for (i = 0; i < tp->ntouches; i++)
		tp_init_touch(tp, &tp->touches[i], i);

	tp_sync_slots(tp, device);

	/* Some touchpads don't reset BTN_TOOL_FINGER on touch up and only
	 * change to/from it when BTN_TOOL_DOUBLETAP is set. This causes us
	 * to ignore the first touches events until a two-finger gesture is
	 * performed.
	 */
	if (libevdev_get_event_value(device->evdev, EV_KEY, BTN_TOOL_FINGER))
		tp_fake_finger_set(tp, BTN_TOOL_FINGER, 1);

	return true;
}

static uint32_t
tp_accel_config_get_profiles(struct libinput_device *libinput_device)
{
	return LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;
}

static enum libinput_config_status
tp_accel_config_set_profile(struct libinput_device *libinput_device,
			    enum libinput_config_accel_profile profile)
{
	return LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
}

static enum libinput_config_accel_profile
tp_accel_config_get_profile(struct libinput_device *libinput_device)
{
	return LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;
}

static enum libinput_config_accel_profile
tp_accel_config_get_default_profile(struct libinput_device *libinput_device)
{
	return LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;
}

static bool
tp_init_accel(struct tp_dispatch *tp)
{
	struct evdev_device *device = tp->device;
	int res_x, res_y;
	struct motion_filter *filter;

	res_x = tp->device->abs.absinfo_x->resolution;
	res_y = tp->device->abs.absinfo_y->resolution;

	/*
	 * Not all touchpads report the same amount of units/mm (resolution).
	 * Normalize motion events to the default mouse DPI as base
	 * (unaccelerated) speed. This also evens out any differences in x
	 * and y resolution, so that a circle on the
	 * touchpad does not turn into an elipse on the screen.
	 */
	tp->accel.x_scale_coeff = (DEFAULT_MOUSE_DPI/25.4) / res_x;
	tp->accel.y_scale_coeff = (DEFAULT_MOUSE_DPI/25.4) / res_y;
	tp->accel.xy_scale_coeff = 1.0 * res_x/res_y;

	if (tp->device->model_flags & EVDEV_MODEL_LENOVO_X230 ||
	    tp->device->model_flags & EVDEV_MODEL_LENOVO_X220_TOUCHPAD_FW81)
		filter = create_pointer_accelerator_filter_lenovo_x230(tp->device->dpi);
	else if (libevdev_get_id_bustype(device->evdev) == BUS_BLUETOOTH)
		filter = create_pointer_accelerator_filter_touchpad(device->dpi, ms2us(50), ms2us(10));
	else
		filter = create_pointer_accelerator_filter_touchpad(device->dpi, 0, 0);

	if (!filter)
		return false;

	evdev_device_init_pointer_acceleration(tp->device, filter);

	/* we override the profile hooks for accel configuration with hooks
	 * that don't allow selection of profiles */
	device->pointer.config.get_profiles = tp_accel_config_get_profiles;
	device->pointer.config.set_profile = tp_accel_config_set_profile;
	device->pointer.config.get_profile = tp_accel_config_get_profile;
	device->pointer.config.get_default_profile = tp_accel_config_get_default_profile;

	return true;
}

static uint32_t
tp_scroll_get_methods(struct tp_dispatch *tp)
{
	uint32_t methods = LIBINPUT_CONFIG_SCROLL_EDGE;

	/* Any movement with more than one finger has random cursor
	 * jumps. Don't allow for 2fg scrolling on this device, see
	 * fdo bug 91135 */
	if (tp->device->model_flags & EVDEV_MODEL_HP_PAVILION_DM4_TOUCHPAD)
		return LIBINPUT_CONFIG_SCROLL_EDGE;

	if (tp->ntouches >= 2)
		methods |= LIBINPUT_CONFIG_SCROLL_2FG;

	return methods;
}

static uint32_t
tp_scroll_config_scroll_method_get_methods(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp_scroll_get_methods(tp);
}

static enum libinput_config_status
tp_scroll_config_scroll_method_set_method(struct libinput_device *device,
		        enum libinput_config_scroll_method method)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;
	uint64_t time = libinput_now(tp_libinput_context(tp));

	if (method == tp->scroll.method)
		return LIBINPUT_CONFIG_STATUS_SUCCESS;

	tp_edge_scroll_stop_events(tp, time);
	tp_gesture_stop_twofinger_scroll(tp, time);

	tp->scroll.method = method;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_scroll_method
tp_scroll_config_scroll_method_get_method(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp->scroll.method;
}

static enum libinput_config_scroll_method
tp_scroll_get_default_method(struct tp_dispatch *tp)
{
	uint32_t methods;
	enum libinput_config_scroll_method method;

	methods = tp_scroll_get_methods(tp);

	if (methods & LIBINPUT_CONFIG_SCROLL_2FG)
		method = LIBINPUT_CONFIG_SCROLL_2FG;
	else
		method = LIBINPUT_CONFIG_SCROLL_EDGE;

	if ((methods & method) == 0)
		evdev_log_bug_libinput(tp->device,
				       "invalid default scroll method %d\n",
				       method);
	return method;
}

static enum libinput_config_scroll_method
tp_scroll_config_scroll_method_get_default_method(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp_scroll_get_default_method(tp);
}

static void
tp_init_scroll(struct tp_dispatch *tp, struct evdev_device *device)
{
	tp_edge_scroll_init(tp, device);

	evdev_init_natural_scroll(device);

	tp->scroll.config_method.get_methods = tp_scroll_config_scroll_method_get_methods;
	tp->scroll.config_method.set_method = tp_scroll_config_scroll_method_set_method;
	tp->scroll.config_method.get_method = tp_scroll_config_scroll_method_get_method;
	tp->scroll.config_method.get_default_method = tp_scroll_config_scroll_method_get_default_method;
	tp->scroll.method = tp_scroll_get_default_method(tp);
	tp->device->base.config.scroll_method = &tp->scroll.config_method;

	 /* In mm for touchpads with valid resolution, see tp_init_accel() */
	tp->device->scroll.threshold = 0.0;
	tp->device->scroll.direction_lock_threshold = 5.0;
}

static int
tp_dwt_config_is_available(struct libinput_device *device)
{
	return 1;
}

static enum libinput_config_status
tp_dwt_config_set(struct libinput_device *device,
	   enum libinput_config_dwt_state enable)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	switch(enable) {
	case LIBINPUT_CONFIG_DWT_ENABLED:
	case LIBINPUT_CONFIG_DWT_DISABLED:
		break;
	default:
		return LIBINPUT_CONFIG_STATUS_INVALID;
	}

	tp->dwt.dwt_enabled = (enable == LIBINPUT_CONFIG_DWT_ENABLED);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_dwt_state
tp_dwt_config_get(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp->dwt.dwt_enabled ?
		LIBINPUT_CONFIG_DWT_ENABLED :
		LIBINPUT_CONFIG_DWT_DISABLED;
}

static bool
tp_dwt_default_enabled(struct tp_dispatch *tp)
{
	return true;
}

static enum libinput_config_dwt_state
tp_dwt_config_get_default(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp_dwt_default_enabled(tp) ?
		LIBINPUT_CONFIG_DWT_ENABLED :
		LIBINPUT_CONFIG_DWT_DISABLED;
}

static inline bool
tp_is_tpkb_combo_below(struct evdev_device *device)
{
	const char *prop;
	enum tpkbcombo_layout layout = TPKBCOMBO_LAYOUT_UNKNOWN;

	prop = udev_device_get_property_value(device->udev_device,
					      "LIBINPUT_ATTR_TPKBCOMBO_LAYOUT");
	if (!prop)
		return false;

	return parse_tpkbcombo_layout_poperty(prop, &layout) &&
		layout == TPKBCOMBO_LAYOUT_BELOW;
}

static inline bool
tp_is_tablet(struct evdev_device *device)
{
	return device->tags & EVDEV_TAG_TABLET_TOUCHPAD;
}

static void
tp_init_dwt(struct tp_dispatch *tp,
	    struct evdev_device *device)
{
	if (device->tags & EVDEV_TAG_EXTERNAL_TOUCHPAD &&
	    !tp_is_tpkb_combo_below(device))
		return;

	tp->dwt.config.is_available = tp_dwt_config_is_available;
	tp->dwt.config.set_enabled = tp_dwt_config_set;
	tp->dwt.config.get_enabled = tp_dwt_config_get;
	tp->dwt.config.get_default_enabled = tp_dwt_config_get_default;
	tp->dwt.dwt_enabled = tp_dwt_default_enabled(tp);
	device->base.config.dwt = &tp->dwt.config;

	return;
}

static inline void
tp_init_palmdetect_edge(struct tp_dispatch *tp,
			struct evdev_device *device)
{
	double width, height;
	struct phys_coords mm = { 0.0, 0.0 };
	struct device_coords edges;

	if (device->tags & EVDEV_TAG_EXTERNAL_TOUCHPAD &&
	    !tp_is_tpkb_combo_below(device))
		return;

	evdev_device_get_size(device, &width, &height);

	/* Enable edge palm detection on touchpads >= 70 mm. Anything
	   smaller probably won't need it, until we find out it does */
	if (width < 70.0)
		return;

	/* palm edges are 8% of the width on each side */
	mm.x = min(8, width * 0.08);
	edges = evdev_device_mm_to_units(device, &mm);
	tp->palm.left_edge = edges.x;

	mm.x = width - min(8, width * 0.08);
	edges = evdev_device_mm_to_units(device, &mm);
	tp->palm.right_edge = edges.x;

	if (!tp->buttons.has_topbuttons && height > 55) {
		/* top edge is 5% of the height */
		mm.y = height * 0.05;
		edges = evdev_device_mm_to_units(device, &mm);
		tp->palm.upper_edge = edges.y;
	}
}

static int
tp_read_palm_pressure_prop(struct tp_dispatch *tp,
			   const struct evdev_device *device)
{
	struct udev_device *udev_device = device->udev_device;
	const char *prop;
	int threshold;
	const int default_palm_threshold = 130;

	prop = udev_device_get_property_value(udev_device,
			      "LIBINPUT_ATTR_PALM_PRESSURE_THRESHOLD");
	if (!prop)
		return default_palm_threshold;

	threshold = parse_palm_pressure_property(prop);

	return threshold > 0 ? threshold : default_palm_threshold;
}

static inline void
tp_init_palmdetect_pressure(struct tp_dispatch *tp,
			    struct evdev_device *device)
{
	if (!libevdev_has_event_code(device->evdev, EV_ABS, ABS_MT_PRESSURE)) {
		tp->palm.use_pressure = false;
		return;
	}

	tp->palm.pressure_threshold = tp_read_palm_pressure_prop(tp, device);
	tp->palm.use_pressure = true;

	evdev_log_debug(device,
			"palm: pressure threshold is %d\n",
			tp->palm.pressure_threshold);
}

static inline void
tp_init_palmdetect_size(struct tp_dispatch *tp,
			struct evdev_device *device)
{
	const char *prop;
	int threshold;

	prop = udev_device_get_property_value(device->udev_device,
					      "LIBINPUT_ATTR_PALM_SIZE_THRESHOLD");
	if (!prop)
		return;

	threshold = parse_palm_size_property(prop);
	if (threshold == 0) {
		evdev_log_bug_client(device,
				     "palm: ignoring invalid threshold %s\n",
				     prop);
		return;
	}

	tp->palm.use_size = true;
	tp->palm.size_threshold = threshold;
}

static inline void
tp_init_palmdetect_arbitration(struct tp_dispatch *tp,
			       struct evdev_device *device)
{
	char timer_name[64];

	snprintf(timer_name,
		 sizeof(timer_name),
		  "%s arbitration",
		  evdev_device_get_sysname(device));
	libinput_timer_init(&tp->arbitration.arbitration_timer,
			    tp_libinput_context(tp),
			    timer_name,
			    tp_arbitration_timeout, tp);
	tp->arbitration.in_arbitration = false;
}

static void
tp_init_palmdetect(struct tp_dispatch *tp,
		   struct evdev_device *device)
{

	tp->palm.right_edge = INT_MAX;
	tp->palm.left_edge = INT_MIN;
	tp->palm.upper_edge = INT_MIN;

	tp_init_palmdetect_arbitration(tp, device);

	if (device->tags & EVDEV_TAG_EXTERNAL_TOUCHPAD &&
	    !tp_is_tpkb_combo_below(device) &&
	    !tp_is_tablet(device))
		return;

	if (!tp_is_tablet(device))
		tp->palm.monitor_trackpoint = true;

	if (libevdev_has_event_code(device->evdev,
				    EV_ABS,
				    ABS_MT_TOOL_TYPE))
		tp->palm.use_mt_tool = true;

	if (!tp_is_tablet(device))
		tp_init_palmdetect_edge(tp, device);
	tp_init_palmdetect_pressure(tp, device);
	tp_init_palmdetect_size(tp, device);
}

static void
tp_init_sendevents(struct tp_dispatch *tp,
		   struct evdev_device *device)
{
	char timer_name[64];

	snprintf(timer_name,
		 sizeof(timer_name),
		  "%s trackpoint",
		  evdev_device_get_sysname(device));
	libinput_timer_init(&tp->palm.trackpoint_timer,
			    tp_libinput_context(tp),
			    timer_name,
			    tp_trackpoint_timeout, tp);

	snprintf(timer_name,
		 sizeof(timer_name),
		 "%s keyboard",
		 evdev_device_get_sysname(device));
	libinput_timer_init(&tp->dwt.keyboard_timer,
			    tp_libinput_context(tp),
			    timer_name,
			    tp_keyboard_timeout, tp);
}

static int
tp_read_thumb_pressure_prop(struct tp_dispatch *tp,
			    const struct evdev_device *device)
{
	struct udev_device *udev_device = device->udev_device;
	const char *prop;
	int threshold;
	const int default_thumb_threshold = 0;

	prop = udev_device_get_property_value(udev_device,
			      "LIBINPUT_ATTR_THUMB_PRESSURE_THRESHOLD");
	if (!prop)
		return default_thumb_threshold;

	threshold = parse_thumb_pressure_property(prop);

	return threshold > 0 ? threshold : default_thumb_threshold;
}

static void
tp_init_thumb(struct tp_dispatch *tp)
{
	struct evdev_device *device = tp->device;
	const struct input_absinfo *abs;
	double w = 0.0, h = 0.0;
	struct device_coords edges;
	struct phys_coords mm = { 0.0, 0.0 };
	int threshold;

	if (!tp->buttons.is_clickpad)
		return;

	/* if the touchpad is less than 50mm high, skip thumb detection.
	 * it's too small to meaningfully interact with a thumb on the
	 * touchpad */
	evdev_device_get_size(device, &w, &h);
	if (h < 50)
		return;

	tp->thumb.detect_thumbs = true;
	tp->thumb.threshold = INT_MAX;

	/* detect thumbs by pressure in the bottom 15mm, detect thumbs by
	 * lingering in the bottom 8mm */
	mm.y = h * 0.85;
	edges = evdev_device_mm_to_units(device, &mm);
	tp->thumb.upper_thumb_line = edges.y;

	mm.y = h * 0.92;
	edges = evdev_device_mm_to_units(device, &mm);
	tp->thumb.lower_thumb_line = edges.y;

	abs = libevdev_get_abs_info(device->evdev, ABS_MT_PRESSURE);
	if (!abs)
		goto out;

	threshold = tp_read_thumb_pressure_prop(tp, device);
	if (threshold == 0)
		goto out;

	tp->thumb.threshold = threshold;

out:
	evdev_log_debug(device,
			"thumb: enabled thumb detection%s\n",
			tp->thumb.threshold != INT_MAX ? " (+pressure)" : "");
}

static bool
tp_pass_sanity_check(struct tp_dispatch *tp,
		     struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;

	if (!libevdev_has_event_code(evdev, EV_ABS, ABS_X))
		goto error;

	if (!libevdev_has_event_code(evdev, EV_KEY, BTN_TOUCH))
		goto error;

	if (!libevdev_has_event_code(evdev, EV_KEY, BTN_TOOL_FINGER))
		goto error;

	return true;

error:
	evdev_log_bug_kernel(device,
			     "device failed touchpad sanity checks\n");
	return false;
}

static void
tp_init_default_resolution(struct tp_dispatch *tp,
			   struct evdev_device *device)
{
	const int touchpad_width_mm = 69, /* 1 under palm detection */
		  touchpad_height_mm = 50;
	int xres, yres;

	if (!device->abs.is_fake_resolution)
		return;

	/* we only get here if
	 * - the touchpad provides no resolution
	 * - the udev hwdb didn't override the resoluion
	 * - no ATTR_SIZE_HINT is set
	 *
	 * The majority of touchpads that triggers all these conditions
	 * are old ones, so let's assume a small touchpad size and assume
	 * that.
	 */
	evdev_log_info(device,
		       "no resolution or size hints, assuming a size of %dx%dmm\n",
		       touchpad_width_mm,
		       touchpad_height_mm);

	xres = device->abs.dimensions.x/touchpad_width_mm;
	yres = device->abs.dimensions.y/touchpad_height_mm;
	libevdev_set_abs_resolution(device->evdev, ABS_X, xres);
	libevdev_set_abs_resolution(device->evdev, ABS_Y, yres);
	libevdev_set_abs_resolution(device->evdev, ABS_MT_POSITION_X, xres);
	libevdev_set_abs_resolution(device->evdev, ABS_MT_POSITION_Y, yres);
	device->abs.is_fake_resolution = false;
}

static inline void
tp_init_hysteresis(struct tp_dispatch *tp)
{
	int xmargin, ymargin;
	const struct input_absinfo *ax = tp->device->abs.absinfo_x,
				   *ay = tp->device->abs.absinfo_y;

	if (ax->fuzz)
		xmargin = ax->fuzz;
	else
		xmargin = ax->resolution/4;

	if (ay->fuzz)
		ymargin = ay->fuzz;
	else
		ymargin = ay->resolution/4;

	tp->hysteresis.margin.x = xmargin;
	tp->hysteresis.margin.y = ymargin;
	tp->hysteresis.enabled = (ax->fuzz || ay->fuzz);
	if (tp->hysteresis.enabled)
		evdev_log_debug(tp->device,
				"hysteresis enabled. "
				"See %stouchpad_jitter.html for details\n",
				HTTP_DOC_LINK);
}

static void
tp_init_pressure(struct tp_dispatch *tp,
		 struct evdev_device *device)
{
	const struct input_absinfo *abs;
	unsigned int code;
	const char *prop;
	int hi, lo;

	code = tp->has_mt ? ABS_MT_PRESSURE : ABS_PRESSURE;
	if (!libevdev_has_event_code(device->evdev, EV_ABS, code)) {
		tp->pressure.use_pressure = false;
		return;
	}

	abs = libevdev_get_abs_info(device->evdev, code);
	assert(abs);

	prop = udev_device_get_property_value(device->udev_device,
					      "LIBINPUT_ATTR_PRESSURE_RANGE");
	if (prop) {
		if (!parse_range_property(prop, &hi, &lo)) {
			evdev_log_bug_client(device,
				     "discarding invalid pressure range '%s'\n",
				     prop);
			return;
		}

		if (hi == 0 && lo == 0) {
			evdev_log_info(device,
			       "pressure-based touch detection disabled\n");
			return;
		}
	} else {
		unsigned int range = abs->maximum - abs->minimum;

		/* Approximately the synaptics defaults */
		hi = abs->minimum + 0.12 * range;
		lo = abs->minimum + 0.10 * range;
	}

	if (hi > abs->maximum || hi < abs->minimum ||
	    lo > abs->maximum || lo < abs->minimum) {
		evdev_log_bug_libinput(device,
			       "discarding out-of-bounds pressure range %d:%d\n",
			       hi, lo);
		return;
	}

	tp->pressure.use_pressure = true;
	tp->pressure.high = hi;
	tp->pressure.low = lo;

	evdev_log_debug(device,
			"using pressure-based touch detection (%d:%d)\n",
			lo,
			hi);
}

static bool
tp_init_touch_size(struct tp_dispatch *tp,
		   struct evdev_device *device)
{
	const char *prop;
	int lo, hi;

	if (!libevdev_has_event_code(device->evdev,
				     EV_ABS,
				     ABS_MT_TOUCH_MAJOR)) {
		return false;
	}

	prop = udev_device_get_property_value(device->udev_device,
					      "LIBINPUT_ATTR_TOUCH_SIZE_RANGE");
	if (!prop)
		return false;

	if (libevdev_get_num_slots(device->evdev) < 5) {
		evdev_log_bug_libinput(device,
			       "Expected 5+ slots for touch size detection\n");
		return false;
	}

	if (!parse_range_property(prop, &hi, &lo)) {
		evdev_log_bug_client(device,
				     "discarding invalid touch size range '%s'\n",
				     prop);
		return false;
	}

	if (hi == 0 && lo == 0) {
		evdev_log_info(device,
			       "touch size based touch detection disabled\n");
		return false;
	}

	/* Thresholds apply for both major or minor */
	tp->touch_size.low = lo;
	tp->touch_size.high = hi;
	tp->touch_size.use_touch_size = true;

	evdev_log_debug(device,
			"using size-based touch detection (%d:%d)\n",
			hi, lo);

	return true;
}

static int
tp_init(struct tp_dispatch *tp,
	struct evdev_device *device)
{
	bool use_touch_size = false;

	tp->base.dispatch_type = DISPATCH_TOUCHPAD;
	tp->base.interface = &tp_interface;
	tp->device = device;
	list_init(&tp->dwt.paired_keyboard_list);

	if (!tp_pass_sanity_check(tp, device))
		return false;

	tp_init_default_resolution(tp, device);

	if (!tp_init_slots(tp, device))
		return false;

	evdev_device_init_abs_range_warnings(device);
	use_touch_size = tp_init_touch_size(tp, device);

	if (!use_touch_size)
		tp_init_pressure(tp, device);

	/* Set the dpi to that of the x axis, because that's what we normalize
	   to when needed*/
	device->dpi = device->abs.absinfo_x->resolution * 25.4;

	tp_init_hysteresis(tp);

	if (!tp_init_accel(tp))
		return false;

	tp_init_tap(tp);
	tp_init_buttons(tp, device);
	tp_init_dwt(tp, device);
	tp_init_palmdetect(tp, device);
	tp_init_sendevents(tp, device);
	tp_init_scroll(tp, device);
	tp_init_gesture(tp);
	tp_init_thumb(tp);

	device->seat_caps |= EVDEV_DEVICE_POINTER;
	if (tp->gesture.enabled)
		device->seat_caps |= EVDEV_DEVICE_GESTURE;

	return true;
}

static uint32_t
tp_sendevents_get_modes(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	uint32_t modes = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;

	if (evdev->tags & EVDEV_TAG_INTERNAL_TOUCHPAD)
		modes |= LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;

	return modes;
}

static void
tp_suspend_conditional(struct tp_dispatch *tp,
		       struct evdev_device *device)
{
	struct libinput_device *dev;

	list_for_each(dev, &device->base.seat->devices_list, link) {
		struct evdev_device *d = evdev_device(dev);
		if (d->tags & EVDEV_TAG_EXTERNAL_MOUSE) {
			tp_suspend(tp, device, SUSPEND_EXTERNAL_MOUSE);
			break;
		}
	}
}

static enum libinput_config_status
tp_sendevents_set_mode(struct libinput_device *device,
		       enum libinput_config_send_events_mode mode)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	/* DISABLED overrides any DISABLED_ON_ */
	if ((mode & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED) &&
	    (mode & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE))
	    mode &= ~LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;

	if (mode == tp->sendevents.current_mode)
		return LIBINPUT_CONFIG_STATUS_SUCCESS;

	switch(mode) {
	case LIBINPUT_CONFIG_SEND_EVENTS_ENABLED:
		tp_resume(tp, evdev, SUSPEND_SENDEVENTS);
		tp_resume(tp, evdev, SUSPEND_EXTERNAL_MOUSE);
		break;
	case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED:
		tp_suspend(tp, evdev, SUSPEND_SENDEVENTS);
		tp_resume(tp, evdev, SUSPEND_EXTERNAL_MOUSE);
		break;
	case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE:
		tp_suspend_conditional(tp, evdev);
		tp_resume(tp, evdev, SUSPEND_SENDEVENTS);
		break;
	default:
		return LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
	}

	tp->sendevents.current_mode = mode;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_send_events_mode
tp_sendevents_get_mode(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	struct tp_dispatch *dispatch = (struct tp_dispatch*)evdev->dispatch;

	return dispatch->sendevents.current_mode;
}

static enum libinput_config_send_events_mode
tp_sendevents_get_default_mode(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

static void
tp_change_to_left_handed(struct evdev_device *device)
{
	struct tp_dispatch *tp = (struct tp_dispatch *)device->dispatch;

	if (device->left_handed.want_enabled == device->left_handed.enabled)
		return;

	if (tp->buttons.state & 0x3) /* BTN_LEFT|BTN_RIGHT */
		return;

	/* tapping and clickfinger aren't affected by left-handed config,
	 * so checking physical buttons is enough */

	device->left_handed.enabled = device->left_handed.want_enabled;
}

struct evdev_dispatch *
evdev_mt_touchpad_create(struct evdev_device *device)
{
	struct tp_dispatch *tp;
	bool want_left_handed = true;

	evdev_tag_touchpad(device, device->udev_device);

	tp = zalloc(sizeof *tp);

	if (!tp_init(tp, device)) {
		tp_interface_destroy(&tp->base);
		return NULL;
	}

	device->base.config.sendevents = &tp->sendevents.config;

	tp->sendevents.current_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	tp->sendevents.config.get_modes = tp_sendevents_get_modes;
	tp->sendevents.config.set_mode = tp_sendevents_set_mode;
	tp->sendevents.config.get_mode = tp_sendevents_get_mode;
	tp->sendevents.config.get_default_mode = tp_sendevents_get_default_mode;

	if (device->model_flags & EVDEV_MODEL_APPLE_TOUCHPAD_ONEBUTTON)
		want_left_handed = false;
	if (want_left_handed)
		evdev_init_left_handed(device, tp_change_to_left_handed);

	return &tp->base;
}
