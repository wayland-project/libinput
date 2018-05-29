/*
 * Copyright © 2015 Red Hat, Inc.
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

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>
#include <linux/input.h>

#include "libinput-util.h"

static inline const char *
prop_value(struct udev_device *device,
	   const char *prop_name)
{
	struct udev_device *parent;
	const char *prop_value = NULL;

	parent = device;
	while (parent && !prop_value) {
		prop_value = udev_device_get_property_value(parent, prop_name);
		parent = udev_device_get_parent(parent);
	}

	return prop_value;
}

static void
handle_touchpad_alps(struct udev_device *device)
{
	const char *product;
	int bus, vid, pid, version;

	product = prop_value(device, "PRODUCT");
	if (!product)
		return;

	if (sscanf(product, "%x/%x/%x/%x", &bus, &vid, &pid, &version) != 4)
		return;

	/* ALPS' firmware version is the version */
	if (version)
		printf("LIBINPUT_MODEL_FIRMWARE_VERSION=%x\n", version);
}

static void
handle_touchpad_synaptics(struct udev_device *device)
{
	const char *product, *props;
	int bus, vid, pid, version;
	int prop;

	product = prop_value(device, "PRODUCT");
	if (!product)
		return;

	if (sscanf(product, "%x/%x/%x/%x", &bus, &vid, &pid, &version) != 4)
		return;

	if (bus != BUS_I8042 || vid != 0x2 || pid != 0x7)
		return;

	props = prop_value(device, "PROP");
	if (sscanf(props, "%x", &prop) != 1)
		return;
	if (prop & (1 << INPUT_PROP_SEMI_MT))
		printf("LIBINPUT_MODEL_JUMPING_SEMI_MT=1\n");
}

static void
handle_touchpad(struct udev_device *device)
{
	const char *name = NULL;

	name = prop_value(device, "NAME");
	if (!name)
		return;

	if (strstr(name, "AlpsPS/2 ALPS") != NULL)
		handle_touchpad_alps(device);
	if (strstr(name, "Synaptics ") != NULL)
		handle_touchpad_synaptics(device);
}

static void
handle_pointingstick(struct udev_device *device)
{
	const char *name = NULL;

	name = prop_value(device, "NAME");
	if (!name)
		return;

	if (strstr(name, "AlpsPS/2 ALPS") != NULL)
		handle_touchpad_alps(device);
}

int main(int argc, char **argv)
{
	int rc = 1;
	struct udev *udev = NULL;
	struct udev_device *device = NULL;
	const char *syspath;

	if (argc != 2)
		return 1;

	syspath = argv[1];

	udev = udev_new();
	if (!udev)
		goto out;

	device = udev_device_new_from_syspath(udev, syspath);
	if (!device)
		goto out;

	if (prop_value(device, "ID_INPUT_TOUCHPAD"))
		handle_touchpad(device);
	if (prop_value(device, "ID_INPUT_POINTINGSTICK"))
		handle_pointingstick(device);

	rc = 0;

out:
	if (device)
		udev_device_unref(device);
	if (udev)
		udev_unref(udev);

	return rc;
}
