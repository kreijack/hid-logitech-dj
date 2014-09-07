/*
 *  HID driver for Logitech m560 mouse
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include "hid-ids.h"
#include "hid-logitech-dj.h"
#include "hid-logitech-hidpp.h"

/*
 * Logitech M560 protocol overview
 *
 * The Logitech M560 mouse, is designed for windows 8. When the middle and/or
 * the sides buttons are pressed, it sends some keyboard keys events
 * instead of buttons ones.
 * To complicate further the things, the middle button keys sequence
 * is different from the odd press and the even press.
 *
 * forward button -> Super_R
 * backward button -> Super_L+'d' (press only)
 * middle button -> 1st time: Alt_L+SuperL+XF86TouchpadOff (press only)
 *                  2nd time: left-click (press only)
 * NB: press-only means that when the button is pressed, the
 * KeyPress/ButtonPress and KeyRelease/ButtonRelease events are generated
 * together sequentially; instead when the button is released, no event is
 * generated !
 *
 * With the command
 *	10<xx>0a 3500af03 (where <xx> is the mouse id),
 * the mouse reacts differently:
 * - it never send a keyboard key event
 * - for the three mouse button it sends:
 *	middle button               press   11<xx>0a 3500af00...
 *	side 1 button (forward)     press   11<xx>0a 3500b000...
 *	side 2 button (backward)    press   11<xx>0a 3500ae00...
 *	middle/side1/side2 button   release 11<xx>0a 35000000...
 */
static u8 m560_config_command[] = {0x35, 0x00, 0xaf, 0x03};

struct m560_private_data {
	union {
		struct dj_report	prev_dj_report;
		u8 prev_data[sizeof(struct dj_report)];
	};
	bool do_config_command;
	struct delayed_work  work;
	struct dj_device *djdev;
	int btn_middle:1;
	int btn_forward:1;
	int btn_backward:1;
	unsigned long last_disconnection;
};

static bool reset_on_disconnection = false;
module_param(reset_on_disconnection, bool, 0644);
MODULE_PARM_DESC(reset_on_disconnection,
	"The connection is reset when a disconnection happened");

/* disconnection timeout to rescedule a config command */
#define DISCONNECTION_TIMEOUT 2

/* how the button are mapped in the report */
#define MOUSE_BTN_LEFT		0
#define MOUSE_BTN_RIGHT		1
#define MOUSE_BTN_MIDDLE	2
#define MOUSE_BTN_WHEEL_LEFT	3
#define MOUSE_BTN_WHEEL_RIGHT	4
#define MOUSE_BTN_FORWARD	5
#define MOUSE_BTN_BACKWARD	6

#define CONFIG_COMMAND_TIMEOUT	(3*HZ)
#define PACKET_TIMEOUT		(10*HZ)


/*
 * get_mydata - helper to convert from hidpp_device to mydata
 *
 * @hdev: hid device
 *
 * @return: return mydata if available, NULL otherwise
 */
static inline struct m560_private_data *get_mydata(struct hid_device *hdev)
{
	struct hidpp_device *hidpp_dev = hid_get_drvdata(hdev);
	return hidpp_dev ? hidpp_get_drvdata(hidpp_dev) : NULL;
}

/*
 * m560_send_config_command - send the config_command to the mouse
 *
 * @dev: hid device where the mouse belongs
 *
 * @return: >0 OK, <0 error
 */
static int m560_send_config_command(struct hid_device *hdev)
{
	struct dj_report *dj_report;
	int retval;
	struct dj_device *dj_device = hdev->driver_data;
	struct hid_device *djrcv_hdev = dj_device->dj_receiver_dev->hdev;

	dj_report = kzalloc(sizeof(struct dj_report), GFP_KERNEL);
	if (!dj_report)
		return -ENOMEM;

	dj_report->report_id = REPORT_ID_HIDPP_SHORT;
	dj_report->device_index = dj_device->device_index;
	dj_report->report_type = 0x0a;

	memcpy(dj_report->report_params, m560_config_command,
					sizeof(m560_config_command));
	retval = hid_hw_raw_request(djrcv_hdev,
		dj_report->report_id,
		(void *)dj_report, HIDPP_REPORT_SHORT_LENGTH,
		HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);

	kfree(dj_report);
	return retval;

}

/*
 * delayedwork_callback - handle the sending of the config_command.
 * It schedules another sending because sometime the mouse doesn't understand
 * the first request and returns an error. So until an ack is received, this
 * function continue to reschedule a sending each RESET_TIMEOUT seconds
 *
 * @work: work_struct struct
 */
static void delayedwork_callback(struct work_struct *work)
{
	struct m560_private_data *mydata =
		container_of(work, struct m560_private_data, work.work);
	struct hid_device *hdev = mydata->djdev->hdev;

	if (!mydata->do_config_command)
		return;

	if (schedule_delayed_work(&mydata->work, CONFIG_COMMAND_TIMEOUT) == 0) {
		dbg_hid(
		  "%s: did not schedule the work item, was already queued\n",
		  __func__);
	}

	m560_send_config_command(hdev);
}

/*
 * start_config_command - start sending config_command
 *
 * @mydata: pointer to private driver data
 */
static inline void start_config_command(struct m560_private_data *mydata)
{
	mydata->do_config_command = true;
	if (schedule_delayed_work(&mydata->work, HZ/2) == 0) {
		struct hid_device *hdev = mydata->djdev->hdev;
		hid_err(hdev,
		   "%s: did not schedule the work item, was already queued\n",
		   __func__);
	}
}

/*
 * stop_config_command - stop sending config_command
 *
 * @mydata: pointer to private driver data
 */
static inline void stop_config_command(struct m560_private_data *mydata)
{
	mydata->do_config_command = false;
}

static void m560_device_connect(struct hidpp_device *hidpp_dev, bool connected)
{
	struct m560_private_data *mydata = hidpp_get_drvdata(hidpp_dev);

	if (!reset_on_disconnection)
		return;

	if (!mydata)
		return;

	if (!connected) {
		printk(KERN_ERR "%s@%d: DISCONNECTED", __FILE__, __LINE__);
		mydata->last_disconnection = jiffies;
		return;
	}

	if (time_after(jiffies,
		mydata->last_disconnection+DISCONNECTION_TIMEOUT*HZ)) {
			printk(KERN_ERR "%s@%d: TIMEOUT: %ld",
				__FILE__, __LINE__,
				(long)jiffies-(long)(
					mydata->last_disconnection+
					DISCONNECTION_TIMEOUT*HZ));
			start_config_command(mydata);
			mydata->last_disconnection = jiffies;
	} else {
			printk(KERN_ERR "%s@%d: NO TIMEOUT: %ld",
				__FILE__, __LINE__,
				(long)jiffies-(long)(
					mydata->last_disconnection+
					DISCONNECTION_TIMEOUT*HZ));
	}

}

/*
 * m560_djdevice_probe - perform the probing of the device.
 *
 * @hdev: hid device
 * @id: hid device id
 */
static int m560_djdevice_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{

	int ret;
	struct m560_private_data *mydata = NULL;
	struct hidpp_device *hidpp_dev = NULL;
	struct dj_device *dj_device = hdev->driver_data;

	if (strcmp(hdev->name, "M560"))
		return -ENODEV;

	hidpp_dev = devm_hidpp_allocate(hdev);

	if (!hidpp_dev) {
		hid_err(hdev, "cannot allocate hidpp_dev\n");
		return -ENOMEM;
	}
	mydata = kzalloc(sizeof(struct m560_private_data), GFP_KERNEL);
	if (!mydata) {
		return -ENOMEM;
	}

	hid_set_drvdata(hdev, hidpp_dev);
	mydata->djdev = dj_device;
	hidpp_dev->device_connect = m560_device_connect;

	/* force it so input_mapping doesn't pass anything */
	mydata->do_config_command = true;

	ret = hid_parse(hdev);
	if (!ret)
		ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	if (ret) {
		kfree(mydata);
		return ret;
	}

	/* we can't set before */
	hidpp_set_drvdata(hidpp_dev, mydata);
	/* try init */
	hid_device_io_start(hdev);

	INIT_DELAYED_WORK(&mydata->work, delayedwork_callback);

	start_config_command(mydata);

	return 0;
}

static inline void set_btn_bit(u8 *data, int bit)
{
	int bytenr = bit / 8;
	int bitmask = 1 << (bit & 0x07);

	data[bytenr] |= bitmask;
}

static inline int get_btn_bit(u8 *data, int bit)
{
	int bytenr = bit / 8;
	int bitmask = 1 << (bit & 0x07);

	return !!(data[bytenr] & bitmask);
}

static inline void clear_btn_bit(u8 *data, int bit)
{
	int bytenr = bit / 8;
	int bitmask = 1 << (bit & 0x07);

	data[bytenr] &= ~bitmask;
}

static int m560_dj_raw_event(struct hid_device *hdev,
			struct hid_report *report, u8 *data, int size)
{
	struct m560_private_data *mydata = get_mydata(hdev);
	struct hidpp_device *hidpp_dev = hid_get_drvdata(hdev);

	if (hidpp_raw_event(hidpp_dev, data, size))
		return 1;

	/* check if the data is a mouse related report */
	if (data[0] != REPORT_TYPE_MOUSE && data[2] != 0x0a)
		return 1;

	/* check if the report is the ack of the config_command */
	if (data[0] == 0x11 && data[2] == 0x0a &&
	    size >= (3+sizeof(m560_config_command)) &&
	    !memcmp(data+3, m560_config_command,
		sizeof(m560_config_command))) {

			stop_config_command(mydata);
			return true;
	}

	if (data[0] == 0x11 && data[2] == 0x0a && data[06] == 0x00) {
		/*
		 * m560 mouse button report
		 *
		 * data[0] = 0x11
		 * data[1] = deviceid
		 * data[2] = 0x0a
		 * data[5] = button (0xaf->middle, 0xb0->forward,
		 * 		     0xaf ->backward, 0x00->release all)
		 * data[6] = 0x00
		 */

		int btn, i, maxsize;

		/* check if the event is a button */
		btn = data[5];
		if (btn != 0x00 && btn != 0xb0 && btn != 0xae && btn != 0xaf)
			return true;

		if (btn == 0xaf)
			mydata->btn_middle = 1;
		else if (btn == 0xb0)
			mydata->btn_forward = 1;
		else if (btn == 0xae)
			mydata->btn_backward = 1;
		else if (btn == 0x00) {
			mydata->btn_backward = 0;
			mydata->btn_forward = 0;
			mydata->btn_middle = 0;
		}

		/* replace the report with the old one */
		if (size > sizeof(mydata->prev_data))
			maxsize = sizeof(mydata->prev_data);
		else
			maxsize = size;
		for (i = 0 ; i < maxsize ; i++)
			data[i] = mydata->prev_data[i];

	} else if (data[0] == REPORT_TYPE_MOUSE) {
		/*
		 * standard mouse report
		 *
		 * data[0] = type (0x02)
		 * data[1..2] = buttons
		 * data[3..5] = xy
		 * data[6] = wheel
		 * data[7] = horizontal wheel
		 */

		/* horizontal wheel handling */
		if (get_btn_bit(data+1,MOUSE_BTN_WHEEL_LEFT))
			data[1+6] = -1;
		if (get_btn_bit(data+1,MOUSE_BTN_WHEEL_RIGHT))
			data[1+6] =  1;

		clear_btn_bit(data+1, MOUSE_BTN_WHEEL_LEFT);
		clear_btn_bit(data+1, MOUSE_BTN_WHEEL_RIGHT);

		/* copy the type and buttons status */
		memcpy(mydata->prev_data, data, 3);
	}

	/* add the extra buttons */
	if (mydata->btn_middle)
		set_btn_bit(data+1, MOUSE_BTN_MIDDLE);
	if (mydata->btn_forward)
		set_btn_bit(data+1, MOUSE_BTN_FORWARD);
	if (mydata->btn_backward)
		set_btn_bit(data+1, MOUSE_BTN_BACKWARD);

	return 1;
}

/*
 * This function performs the cleanup when the device is removed
 */
static void m560_djdevice_remove(struct hid_device *hdev)
{
	struct m560_private_data *mydata = get_mydata(hdev);

	if (!mydata)
		return;

	cancel_delayed_work_sync(&mydata->work);
	hid_hw_stop(hdev);
	kfree(mydata);
}

/*
 * This function avoids that any event different from the mouse ones
 * goes to the upper level
 */
static int m560_djdevice_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if (field->application != HID_GD_MOUSE)
		return -1;
	return 0;
}

static const struct hid_device_id m560_dj_device[] = {
	{ HID_DEVICE(BUS_USB, HID_GROUP_LOGITECH_DJ_DEVICE_MOUSE,
		USB_VENDOR_ID_LOGITECH, DJ_DEVICE_ID_M560)},
	{}
};

MODULE_DEVICE_TABLE(hid, m560_dj_device);

struct hid_driver hid_logitech_dj_device_driver_m560 = {
	.name = "m560",
	.id_table = m560_dj_device,
	.probe = m560_djdevice_probe,
	.remove = m560_djdevice_remove,
	.input_mapping = m560_djdevice_input_mapping,
	.raw_event = m560_dj_raw_event,
};

module_hid_driver(hid_logitech_dj_device_driver_m560)

MODULE_AUTHOR("Goffredo Baroncelli <kreijack@inwind.it>");
MODULE_DESCRIPTION("Logitech Wireless Mouse m560");
MODULE_LICENSE("GPL");
