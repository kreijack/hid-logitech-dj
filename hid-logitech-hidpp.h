#ifndef __HID_LOGITECH_HIDPP_H
#define __HID_LOGITECH_HIDPP_H

/*
 *  HIDPP protocol for Logitech Unifying receivers
 *
 *  Copyright (c) 2011 Logitech (c)
 *  Copyright (c) 2012-2013 Google (c)
 *  Copyright (c) 2013-2014 Red Hat Inc.
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/kfifo.h>
#include "compat.h"

#ifndef HID_GROUP_LOGITECH_DJ_DEVICE_GENERIC
#define HID_GROUP_LOGITECH_DJ_DEVICE_GENERIC	0x0101
#endif

#ifndef HID_GROUP_LOGITECH_DJ_DEVICE_WTP
#define HID_GROUP_LOGITECH_DJ_DEVICE_WTP	0x0102
#endif

#ifndef HID_GROUP_LOGITECH_DJ_DEVICE_MOUSE
#define HID_GROUP_LOGITECH_DJ_DEVICE_MOUSE	0x0103
#endif

#ifndef HID_GROUP_LOGITECH_DJ_DEVICE_MOUSE
#define HID_GROUP_LOGITECH_DJ_DEVICE_MOUSE	0x0103
#endif

#define REPORT_ID_HIDPP_SHORT			0x10
#define REPORT_ID_HIDPP_LONG			0x11

#define HIDPP_REPORT_SHORT_LENGTH		7
#define HIDPP_REPORT_LONG_LENGTH		20

/*
 * There are two hidpp protocols in use, the first version hidpp10 is known
 * as register access protocol or RAP, the second version hidpp20 is known as
 * feature access protocol or FAP
 *
 * Most older devices (including the Unifying usb receiver) use the RAP protocol
 * where as most newer devices use the FAP protocol. Both protocols are
 * compatible with the underlying transport, which could be usb, Unifiying, or
 * bluetooth. The message lengths are defined by the hid vendor specific report
 * descriptor for the HIDPP_SHORT report type (total message lenth 7 bytes) and
 * the HIDPP_LONG report type (total message length 20 bytes)
 *
 * The RAP protocol uses both report types, whereas the FAP only uses HIDPP_LONG
 * messages. The Unifying receiver itself responds to RAP messages (device index
 * is 0xFF for the receiver), and all messages (short or long) with a device
 * index between 1 and 6 are passed untouched to the corresponding paired
 * Unifying device.
 *
 * The paired device can be RAP or FAP, it will receive the message untouched
 * from the Unifiying receiver.
 */

struct fap {
	u8 feature_index;
	u8 funcindex_clientid;
	u8 params[HIDPP_REPORT_LONG_LENGTH - 4U];
};

struct rap {
	u8 sub_id;
	u8 reg_address;
	u8 params[HIDPP_REPORT_LONG_LENGTH - 4U];
};

struct hidpp_report {
	u8 report_id;
	u8 device_index;
	union {
		struct fap fap;
		struct rap rap;
		u8 rawbytes[sizeof(struct fap)];
	};
} __packed;

struct hidpp_device {
	void *driver_data;

	void (*device_connect)(struct hidpp_device *hidpp_dev, bool connected);

	/* private */
	struct hid_device *hid_dev;
	struct work_struct work;
	struct mutex send_mutex;
	struct kfifo delayed_work_fifo;
	spinlock_t delayed_work_lock;
	void *send_receive_buf;
	wait_queue_head_t wait;
	bool answer_available;
};

static inline struct hid_device *hidpp_get_hiddev(struct hidpp_device *hidpp)
{
	return hidpp->hid_dev;
}

static inline void *hidpp_get_drvdata(struct hidpp_device *hidpp_dev)
{
	return hidpp_dev->driver_data;
}

static inline void hidpp_set_drvdata(struct hidpp_device *hidpp_dev, void *data)
{
	hidpp_dev->driver_data = data;
}

extern int hidpp_send_fap_command_sync(struct hidpp_device *hidpp_dev,
	u8 feat_index, u8 funcindex_clientid, u8 *params, int param_count,
	struct hidpp_report *response);
extern int hidpp_send_rap_command_sync(struct hidpp_device *hidpp_dev,
	u8 report_id, u8 sub_id, u8 reg_address, u8 *params, int param_count,
	struct hidpp_report *response);

extern int hidpp_raw_event(struct hidpp_device *hidpp_dev, u8 *data, int size);

extern struct hidpp_device *devm_hidpp_allocate(struct hid_device *hid_dev);

#define HIDPP_ERROR				0x8f
#define HIDPP_ERROR_SUCCESS			0x00
#define HIDPP_ERROR_INVALID_SUBID		0x01
#define HIDPP_ERROR_INVALID_ADRESS		0x02
#define HIDPP_ERROR_INVALID_VALUE		0x03
#define HIDPP_ERROR_CONNECT_FAIL		0x04
#define HIDPP_ERROR_TOO_MANY_DEVICES		0x05
#define HIDPP_ERROR_ALREADY_EXISTS		0x06
#define HIDPP_ERROR_BUSY			0x07
#define HIDPP_ERROR_UNKNOWN_DEVICE		0x08
#define HIDPP_ERROR_RESOURCE_ERROR		0x09
#define HIDPP_ERROR_REQUEST_UNAVAILABLE		0x0a
#define HIDPP_ERROR_INVALID_PARAM_VALUE		0x0b
#define HIDPP_ERROR_WRONG_PIN_CODE		0x0c

/* -------------------------------------------------------------------------- */
/* HIDP++ 1.0 commands                                                        */
/* -------------------------------------------------------------------------- */

extern int hidpp_enable_notifications(struct hidpp_device *hidpp_dev,
	bool wireless_notifications, bool software_present);

extern char *hidpp_get_unifying_name(struct hidpp_device *hidpp_dev,
	int device_index);

/* -------------------------------------------------------------------------- */
/* 0x0000: Root                                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ROOT					0x0000
#define HIDPP_PAGE_ROOT_IDX				0x00

extern int hidpp_root_get_feature(struct hidpp_device *hidpp_dev, u16 feature,
	u8 *feature_index, u8 *feature_type);
extern int hidpp_root_get_protocol_version(struct hidpp_device *hidpp_dev,
	u8 *protocol_major, u8 *protocol_minor);

/* -------------------------------------------------------------------------- */
/* 0x0005: GetDeviceNameType                                                  */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_GET_DEVICE_NAME_TYPE			0x0005

extern char *hidpp_get_device_name(struct hidpp_device *hidpp_dev,
	u8 *name_length);

/* -------------------------------------------------------------------------- */
/* 0x6100: TouchPadRawXY                                                      */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_TOUCHPAD_RAW_XY			0x6100

#define EVENT_TOUCHPAD_RAW_XY				0x00

#define TOUCHPAD_RAW_XY_ORIGIN_LOWER_LEFT		0x01
#define TOUCHPAD_RAW_XY_ORIGIN_UPPER_LEFT		0x03

struct hidpp_touchpad_raw_info {
	u16 x_size;
	u16 y_size;
	u8 z_range;
	u8 area_range;
	u8 timestamp_unit;
	u8 maxcontacts;
	u8 origin;
	u16 res;
};

struct hidpp_touchpad_raw_xy_finger {
	u8 contact_type;
	u8 contact_status;
	u16 x;
	u16 y;
	u8 z;
	u8 area;
	u8 finger_id;
};

struct hidpp_touchpad_raw_xy {
	u16 timestamp;
	struct hidpp_touchpad_raw_xy_finger fingers[2];
	u8 spurious_flag;
	u8 end_of_frame;
	u8 finger_count;
	u8 button;
};

extern int hidpp_touchpad_get_raw_info(struct hidpp_device *hidpp_dev,
	u8 feature_index, struct hidpp_touchpad_raw_info *raw_info);

extern int hidpp_touchpad_set_raw_report_state(struct hidpp_device *hidpp_dev,
		u8 feature_index, bool send_raw_reports,
		bool sensor_enhanced_settings);

extern void hidpp_touchpad_raw_xy_event(struct hidpp_device *hidpp_dev,
		u8 *data, struct hidpp_touchpad_raw_xy *raw_xy);

#endif
