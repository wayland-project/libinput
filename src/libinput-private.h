/*
 * Copyright © 2013 Jonas Ådahl
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

#ifndef LIBINPUT_PRIVATE_H
#define LIBINPUT_PRIVATE_H

#include "config.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>

#include "linux/input.h"

#include "libinput.h"
#include "libinput-util.h"
#include "libinput-version.h"

#if LIBINPUT_VERSION_MICRO >= 90
#define HTTP_DOC_LINK "https://wayland.freedesktop.org/libinput/doc/latest/"
#else
#define HTTP_DOC_LINK "https://wayland.freedesktop.org/libinput/doc/" LIBINPUT_VERSION "/"
#endif

struct libinput_source;

/* A coordinate pair in device coordinates */
struct device_coords {
	int x, y;
};

/*
 * A coordinate pair in device coordinates, capable of holding non discrete
 * values, this is necessary e.g. when device coordinates get averaged.
 */
struct device_float_coords {
	double x, y;
};

/* A dpi-normalized coordinate pair */
struct normalized_coords {
	double x, y;
};

/* A discrete step pair (mouse wheels) */
struct discrete_coords {
	int x, y;
};

/* A pair of coordinates normalized to a [0,1] or [-1, 1] range */
struct normalized_range_coords {
	double x, y;
};

/* A pair of angles in degrees */
struct wheel_angle {
	double x, y;
};

/* A pair of angles in degrees */
struct tilt_degrees {
	double x, y;
};

/* A threshold with an upper and lower limit */
struct threshold {
	int upper;
	int lower;
};

/* A pair of coordinates in mm */
struct phys_coords {
	double x;
	double y;
};

/* A pair of tilt flags */
struct wheel_tilt_flags {
	bool vertical, horizontal;
};

struct libinput_interface_backend {
	int (*resume)(struct libinput *libinput);
	void (*suspend)(struct libinput *libinput);
	void (*destroy)(struct libinput *libinput);
	int (*device_change_seat)(struct libinput_device *device,
				  const char *seat_name);
};

struct libinput {
	int epoll_fd;
	struct list source_destroy_list;

	struct list seat_list;

	struct {
		struct list list;
		struct libinput_source *source;
		int fd;
		uint64_t next_expiry;
	} timer;

	struct libinput_event **events;
	size_t events_count;
	size_t events_len;
	size_t events_in;
	size_t events_out;

	struct list tool_list;

	const struct libinput_interface *interface;
	const struct libinput_interface_backend *interface_backend;

	libinput_log_handler log_handler;
	enum libinput_log_priority log_priority;
	void *user_data;
	int refcount;

	struct list device_group_list;

	uint64_t last_event_time;

	bool quirks_initialized;
	struct quirks_context *quirks;
};

typedef void (*libinput_seat_destroy_func) (struct libinput_seat *seat);

struct libinput_seat {
	struct libinput *libinput;
	struct list link;
	struct list devices_list;
	void *user_data;
	int refcount;
	libinput_seat_destroy_func destroy;

	char *physical_name;
	char *logical_name;

	uint32_t slot_map;

	uint32_t button_count[KEY_CNT];
};

struct libinput_device_config_tap {
	int (*count)(struct libinput_device *device);
	enum libinput_config_status (*set_enabled)(struct libinput_device *device,
						   enum libinput_config_tap_state enable);
	enum libinput_config_tap_state (*get_enabled)(struct libinput_device *device);
	enum libinput_config_tap_state (*get_default)(struct libinput_device *device);

	enum libinput_config_status (*set_map)(struct libinput_device *device,
						   enum libinput_config_tap_button_map map);
	enum libinput_config_tap_button_map (*get_map)(struct libinput_device *device);
	enum libinput_config_tap_button_map (*get_default_map)(struct libinput_device *device);

	enum libinput_config_status (*set_drag_enabled)(struct libinput_device *device,
							enum libinput_config_drag_state);
	enum libinput_config_drag_state (*get_drag_enabled)(struct libinput_device *device);
	enum libinput_config_drag_state (*get_default_drag_enabled)(struct libinput_device *device);

	enum libinput_config_status (*set_draglock_enabled)(struct libinput_device *device,
							    enum libinput_config_drag_lock_state);
	enum libinput_config_drag_lock_state (*get_draglock_enabled)(struct libinput_device *device);
	enum libinput_config_drag_lock_state (*get_default_draglock_enabled)(struct libinput_device *device);
};

struct libinput_device_config_calibration {
	int (*has_matrix)(struct libinput_device *device);
	enum libinput_config_status (*set_matrix)(struct libinput_device *device,
						  const float matrix[6]);
	int (*get_matrix)(struct libinput_device *device,
			  float matrix[6]);
	int (*get_default_matrix)(struct libinput_device *device,
							  float matrix[6]);
};

struct libinput_device_config_send_events {
	uint32_t (*get_modes)(struct libinput_device *device);
	enum libinput_config_status (*set_mode)(struct libinput_device *device,
						   enum libinput_config_send_events_mode mode);
	enum libinput_config_send_events_mode (*get_mode)(struct libinput_device *device);
	enum libinput_config_send_events_mode (*get_default_mode)(struct libinput_device *device);
};

struct libinput_device_config_accel {
	int (*available)(struct libinput_device *device);
	enum libinput_config_status (*set_speed)(struct libinput_device *device,
						 double speed);
	double (*get_speed)(struct libinput_device *device);
	double (*get_default_speed)(struct libinput_device *device);

	uint32_t (*get_profiles)(struct libinput_device *device);
	enum libinput_config_status (*set_profile)(struct libinput_device *device,
						   enum libinput_config_accel_profile);
	enum libinput_config_accel_profile (*get_profile)(struct libinput_device *device);
	enum libinput_config_accel_profile (*get_default_profile)(struct libinput_device *device);
};

struct libinput_device_config_natural_scroll {
	int (*has)(struct libinput_device *device);
	enum libinput_config_status (*set_enabled)(struct libinput_device *device,
						   int enabled);
	int (*get_enabled)(struct libinput_device *device);
	int (*get_default_enabled)(struct libinput_device *device);
};

struct libinput_device_config_left_handed {
	int (*has)(struct libinput_device *device);
	enum libinput_config_status (*set)(struct libinput_device *device, int left_handed);
	int (*get)(struct libinput_device *device);
	int (*get_default)(struct libinput_device *device);
};

struct libinput_device_config_scroll_method {
	uint32_t (*get_methods)(struct libinput_device *device);
	enum libinput_config_status (*set_method)(struct libinput_device *device,
						  enum libinput_config_scroll_method method);
	enum libinput_config_scroll_method (*get_method)(struct libinput_device *device);
	enum libinput_config_scroll_method (*get_default_method)(struct libinput_device *device);
	enum libinput_config_status (*set_button)(struct libinput_device *device,
						  uint32_t button);
	uint32_t (*get_button)(struct libinput_device *device);
	uint32_t (*get_default_button)(struct libinput_device *device);
};

struct libinput_device_config_click_method {
	uint32_t (*get_methods)(struct libinput_device *device);
	enum libinput_config_status (*set_method)(struct libinput_device *device,
						  enum libinput_config_click_method method);
	enum libinput_config_click_method (*get_method)(struct libinput_device *device);
	enum libinput_config_click_method (*get_default_method)(struct libinput_device *device);
};

struct libinput_device_config_middle_emulation {
	int (*available)(struct libinput_device *device);
	enum libinput_config_status (*set)(
			 struct libinput_device *device,
			 enum libinput_config_middle_emulation_state);
	enum libinput_config_middle_emulation_state (*get)(
			 struct libinput_device *device);
	enum libinput_config_middle_emulation_state (*get_default)(
			 struct libinput_device *device);
};

struct libinput_device_config_dwt {
	int (*is_available)(struct libinput_device *device);
	enum libinput_config_status (*set_enabled)(
			 struct libinput_device *device,
			 enum libinput_config_dwt_state enable);
	enum libinput_config_dwt_state (*get_enabled)(
			 struct libinput_device *device);
	enum libinput_config_dwt_state (*get_default_enabled)(
			 struct libinput_device *device);
};

struct libinput_device_config_rotation {
	int (*is_available)(struct libinput_device *device);
	enum libinput_config_status (*set_angle)(
			 struct libinput_device *device,
			 unsigned int degrees_cw);
	unsigned int (*get_angle)(struct libinput_device *device);
	unsigned int (*get_default_angle)(struct libinput_device *device);
};

struct libinput_device_config {
	struct libinput_device_config_tap *tap;
	struct libinput_device_config_calibration *calibration;
	struct libinput_device_config_send_events *sendevents;
	struct libinput_device_config_accel *accel;
	struct libinput_device_config_natural_scroll *natural_scroll;
	struct libinput_device_config_left_handed *left_handed;
	struct libinput_device_config_scroll_method *scroll_method;
	struct libinput_device_config_click_method *click_method;
	struct libinput_device_config_middle_emulation *middle_emulation;
	struct libinput_device_config_dwt *dwt;
	struct libinput_device_config_rotation *rotation;
};

struct libinput_device_group {
	int refcount;
	void *user_data;
	char *identifier; /* unique identifier or NULL for singletons */

	struct list link;
};

struct libinput_device {
	struct libinput_seat *seat;
	struct libinput_device_group *group;
	struct list link;
	struct list event_listeners;
	void *user_data;
	int refcount;
	struct libinput_device_config config;
};

enum libinput_tablet_tool_axis {
	LIBINPUT_TABLET_TOOL_AXIS_X = 1,
	LIBINPUT_TABLET_TOOL_AXIS_Y = 2,
	LIBINPUT_TABLET_TOOL_AXIS_DISTANCE = 3,
	LIBINPUT_TABLET_TOOL_AXIS_PRESSURE = 4,
	LIBINPUT_TABLET_TOOL_AXIS_TILT_X = 5,
	LIBINPUT_TABLET_TOOL_AXIS_TILT_Y = 6,
	LIBINPUT_TABLET_TOOL_AXIS_ROTATION_Z = 7,
	LIBINPUT_TABLET_TOOL_AXIS_SLIDER = 8,
	LIBINPUT_TABLET_TOOL_AXIS_REL_WHEEL = 9,
};

#define LIBINPUT_TABLET_TOOL_AXIS_MAX LIBINPUT_TABLET_TOOL_AXIS_REL_WHEEL

struct tablet_axes {
	struct device_coords point;
	struct normalized_coords delta;
	double distance;
	double pressure;
	struct tilt_degrees tilt;
	double rotation;
	double slider;
	double wheel;
	int wheel_discrete;
};

struct libinput_tablet_tool {
	struct list link;
	uint32_t serial;
	uint32_t tool_id;
	enum libinput_tablet_tool_type type;
	unsigned char axis_caps[NCHARS(LIBINPUT_TABLET_TOOL_AXIS_MAX + 1)];
	unsigned char buttons[NCHARS(KEY_MAX) + 1];
	int refcount;
	void *user_data;

	/* The pressure threshold assumes a pressure_offset of 0 */
	struct threshold pressure_threshold;
	int pressure_offset; /* in device coordinates */
	bool has_pressure_offset;
};

struct libinput_tablet_pad_mode_group {
	struct libinput_device *device;
	struct list link;
	int refcount;
	void *user_data;

	unsigned int index;
	unsigned int num_modes;
	unsigned int current_mode;

	uint32_t button_mask;
	uint32_t ring_mask;
	uint32_t strip_mask;

	uint32_t toggle_button_mask;

	void (*destroy)(struct libinput_tablet_pad_mode_group *group);
};

struct libinput_event {
	enum libinput_event_type type;
	struct libinput_device *device;
};

struct libinput_event_listener {
	struct list link;
	void (*notify_func)(uint64_t time, struct libinput_event *ev, void *notify_func_data);
	void *notify_func_data;
};

typedef void (*libinput_source_dispatch_t)(void *data);

#define log_debug(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define log_info(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_INFO, __VA_ARGS__)
#define log_error(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define log_bug_kernel(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define log_bug_libinput(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_ERROR, "libinput bug: " __VA_ARGS__)
#define log_bug_client(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_ERROR, "client bug: " __VA_ARGS__)

#define log_debug_ratelimit(li_, r_, ...) log_msg_ratelimit((li_), (r_), LIBINPUT_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define log_info_ratelimit(li_, r_, ...) log_msg_ratelimit((li_), (r_), LIBINPUT_LOG_PRIORITY_INFO, __VA_ARGS__)
#define log_error_ratelimit(li_, r_, ...) log_msg_ratelimit((li_), (r_), LIBINPUT_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define log_bug_kernel_ratelimit(li_, r_, ...) log_msg_ratelimit((li_), (r_), LIBINPUT_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define log_bug_libinput_ratelimit(li_, r_, ...) log_msg_ratelimit((li_), (r_), LIBINPUT_LOG_PRIORITY_ERROR, "libinput bug: " __VA_ARGS__)
#define log_bug_client_ratelimit(li_, r_, ...) log_msg_ratelimit((li_), (r_), LIBINPUT_LOG_PRIORITY_ERROR, "client bug: " __VA_ARGS__)

void
log_msg_ratelimit(struct libinput *libinput,
		  struct ratelimit *ratelimit,
		  enum libinput_log_priority priority,
		  const char *format, ...)
	LIBINPUT_ATTRIBUTE_PRINTF(4, 5);

void
log_msg(struct libinput *libinput,
	enum libinput_log_priority priority,
	const char *format, ...)
	LIBINPUT_ATTRIBUTE_PRINTF(3, 4);

void
log_msg_va(struct libinput *libinput,
	   enum libinput_log_priority priority,
	   const char *format,
	   va_list args)
	LIBINPUT_ATTRIBUTE_PRINTF(3, 0);

int
libinput_init(struct libinput *libinput,
	      const struct libinput_interface *interface,
	      const struct libinput_interface_backend *interface_backend,
	      void *user_data);

void
libinput_init_quirks(struct libinput *libinput);

struct libinput_source *
libinput_add_fd(struct libinput *libinput,
		int fd,
		libinput_source_dispatch_t dispatch,
		void *data);

void
libinput_remove_source(struct libinput *libinput,
		       struct libinput_source *source);

int
open_restricted(struct libinput *libinput,
		const char *path, int flags);

void
close_restricted(struct libinput *libinput, int fd);

bool
ignore_litest_test_suite_device(struct udev_device *device);

void
libinput_seat_init(struct libinput_seat *seat,
		   struct libinput *libinput,
		   const char *physical_name,
		   const char *logical_name,
		   libinput_seat_destroy_func destroy);

void
libinput_device_init(struct libinput_device *device,
		     struct libinput_seat *seat);

struct libinput_device_group *
libinput_device_group_create(struct libinput *libinput,
			     const char *identifier);

struct libinput_device_group *
libinput_device_group_find_group(struct libinput *libinput,
				 const char *identifier);

void
libinput_device_set_device_group(struct libinput_device *device,
				 struct libinput_device_group *group);

void
libinput_device_init_event_listener(struct libinput_event_listener *listener);

void
libinput_device_add_event_listener(struct libinput_device *device,
				   struct libinput_event_listener *listener,
				   void (*notify_func)(
						uint64_t time,
						struct libinput_event *event,
						void *notify_func_data),
				   void *notify_func_data);

void
libinput_device_remove_event_listener(struct libinput_event_listener *listener);

void
notify_added_device(struct libinput_device *device);

void
notify_removed_device(struct libinput_device *device);

void
keyboard_notify_key(struct libinput_device *device,
		    uint64_t time,
		    uint32_t key,
		    enum libinput_key_state state);

void
pointer_notify_motion(struct libinput_device *device,
		      uint64_t time,
		      const struct normalized_coords *delta,
		      const struct device_float_coords *raw);

void
pointer_notify_motion_absolute(struct libinput_device *device,
			       uint64_t time,
			       const struct device_coords *point);

void
pointer_notify_button(struct libinput_device *device,
		      uint64_t time,
		      int32_t button,
		      enum libinput_button_state state);

void
pointer_notify_axis(struct libinput_device *device,
		    uint64_t time,
		    uint32_t axes,
		    enum libinput_pointer_axis_source source,
		    const struct normalized_coords *delta,
		    const struct discrete_coords *discrete);

void
touch_notify_touch_down(struct libinput_device *device,
			uint64_t time,
			int32_t slot,
			int32_t seat_slot,
			const struct device_coords *point);

void
touch_notify_touch_motion(struct libinput_device *device,
			  uint64_t time,
			  int32_t slot,
			  int32_t seat_slot,
			  const struct device_coords *point);

void
touch_notify_touch_up(struct libinput_device *device,
		      uint64_t time,
		      int32_t slot,
		      int32_t seat_slot);

void
touch_notify_frame(struct libinput_device *device,
		   uint64_t time);

void
gesture_notify_swipe(struct libinput_device *device,
		     uint64_t time,
		     enum libinput_event_type type,
		     int finger_count,
		     const struct normalized_coords *delta,
		     const struct normalized_coords *unaccel);

void
gesture_notify_swipe_end(struct libinput_device *device,
			 uint64_t time,
			 int finger_count,
			 int cancelled);

void
gesture_notify_pinch(struct libinput_device *device,
		     uint64_t time,
		     enum libinput_event_type type,
		     int finger_count,
		     const struct normalized_coords *delta,
		     const struct normalized_coords *unaccel,
		     double scale,
		     double angle);

void
gesture_notify_pinch_end(struct libinput_device *device,
			 uint64_t time,
			 int finger_count,
			 double scale,
			 int cancelled);

void
tablet_notify_axis(struct libinput_device *device,
		   uint64_t time,
		   struct libinput_tablet_tool *tool,
		   enum libinput_tablet_tool_tip_state tip_state,
		   unsigned char *changed_axes,
		   const struct tablet_axes *axes);

void
tablet_notify_proximity(struct libinput_device *device,
			uint64_t time,
			struct libinput_tablet_tool *tool,
			enum libinput_tablet_tool_proximity_state state,
			unsigned char *changed_axes,
			const struct tablet_axes *axes);

void
tablet_notify_tip(struct libinput_device *device,
		  uint64_t time,
		  struct libinput_tablet_tool *tool,
		  enum libinput_tablet_tool_tip_state tip_state,
		  unsigned char *changed_axes,
		  const struct tablet_axes *axes);

void
tablet_notify_button(struct libinput_device *device,
		     uint64_t time,
		     struct libinput_tablet_tool *tool,
		     enum libinput_tablet_tool_tip_state tip_state,
		     const struct tablet_axes *axes,
		     int32_t button,
		     enum libinput_button_state state);

void
tablet_pad_notify_button(struct libinput_device *device,
			 uint64_t time,
			 int32_t button,
			 enum libinput_button_state state,
			 struct libinput_tablet_pad_mode_group *group);
void
tablet_pad_notify_ring(struct libinput_device *device,
		       uint64_t time,
		       unsigned int number,
		       double value,
		       enum libinput_tablet_pad_ring_axis_source source,
		       struct libinput_tablet_pad_mode_group *group);
void
tablet_pad_notify_strip(struct libinput_device *device,
			uint64_t time,
			unsigned int number,
			double value,
			enum libinput_tablet_pad_strip_axis_source source,
			struct libinput_tablet_pad_mode_group *group);
void
switch_notify_toggle(struct libinput_device *device,
		     uint64_t time,
		     enum libinput_switch sw,
		     enum libinput_switch_state state);

static inline uint64_t
libinput_now(struct libinput *libinput)
{
	struct timespec ts = { 0, 0 };

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		log_error(libinput, "clock_gettime failed: %s\n", strerror(errno));
		return 0;
	}

	return s2us(ts.tv_sec) + ns2us(ts.tv_nsec);
}

static inline struct device_float_coords
device_delta(const struct device_coords a, const struct device_coords b)
{
	struct device_float_coords delta;

	delta.x = a.x - b.x;
	delta.y = a.y - b.y;

	return delta;
}

static inline struct device_float_coords
device_average(const struct device_coords a, const struct device_coords b)
{
	struct device_float_coords average;

	average.x = (a.x + b.x) / 2.0;
	average.y = (a.y + b.y) / 2.0;

	return average;
}

static inline struct device_float_coords
device_float_delta(const struct device_float_coords a, const struct device_float_coords b)
{
	struct device_float_coords delta;

	delta.x = a.x - b.x;
	delta.y = a.y - b.y;

	return delta;
}

static inline struct device_float_coords
device_float_average(const struct device_float_coords a, const struct device_float_coords b)
{
	struct device_float_coords average;

	average.x = (a.x + b.x) / 2.0;
	average.y = (a.y + b.y) / 2.0;

	return average;
}

static inline bool
device_float_is_zero(const struct device_float_coords coords)
{
	return coords.x == 0.0 && coords.y == 0.0;
}

static inline double
normalized_length(const struct normalized_coords norm)
{
	return hypot(norm.x, norm.y);
}

static inline bool
normalized_is_zero(const struct normalized_coords norm)
{
	return norm.x == 0.0 && norm.y == 0.0;
}

static inline double
length_in_mm(const struct phys_coords mm)
{
	return hypot(mm.x, mm.y);
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

static inline uint32_t
xy_get_direction(double x, double y)
{
	uint32_t dir = UNDEFINED_DIRECTION;
	int d1, d2;
	double r;

	if (fabs(x) < 2.0 && fabs(y) < 2.0) {
		if (x > 0.0 && y > 0.0)
			dir = S | SE | E;
		else if (x > 0.0 && y < 0.0)
			dir = N | NE | E;
		else if (x < 0.0 && y > 0.0)
			dir = S | SW | W;
		else if (x < 0.0 && y < 0.0)
			dir = N | NW | W;
		else if (x > 0.0)
			dir = NE | E | SE;
		else if (x < 0.0)
			dir = NW | W | SW;
		else if (y > 0.0)
			dir = SE | S | SW;
		else if (y < 0.0)
			dir = NE | N | NW;
	} else {
		/* Calculate r within the interval  [0 to 8)
		 *
		 * r = [0 .. 2π] where 0 is North
		 * d_f = r / 2π  ([0 .. 1))
		 * d_8 = 8 * d_f
		 */
		r = atan2(y, x);
		r = fmod(r + 2.5*M_PI, 2*M_PI);
		r *= 4*M_1_PI;

		/* Mark one or two close enough octants */
		d1 = (int)(r + 0.9) % 8;
		d2 = (int)(r + 0.1) % 8;

		dir = (1 << d1) | (1 << d2);
	}

	return dir;
}

static inline uint32_t
phys_get_direction(const struct phys_coords mm)
{
	return xy_get_direction(mm.x, mm.y);
}

/**
 * Get the direction for the given set of coordinates.
 * assumption: coordinates are normalized to one axis resolution.
 */
static inline uint32_t
device_float_get_direction(const struct device_float_coords coords)
{
	return xy_get_direction(coords.x, coords.y);
}
#endif /* LIBINPUT_PRIVATE_H */
