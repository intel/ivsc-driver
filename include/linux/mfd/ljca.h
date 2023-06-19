/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_USB_LJCA_H_
#define _LINUX_USB_LJCA_H_

#include <linux/types.h>

struct ljca_dev;
struct auxiliary_device;

struct ljca {
	struct auxiliary_device auxdev;
	u8 type;
	u8 id;
	struct ljca_dev *dev;
};

#define auxiliary_dev_to_ljca(auxiliary_dev) container_of(auxiliary_dev, struct ljca, auxdev)

#define LJCA_MAX_GPIO_NUM 64
struct ljca_gpio_info {
	unsigned int num;
	DECLARE_BITMAP(valid_pin_map, LJCA_MAX_GPIO_NUM);
};

struct ljca_i2c_info {
	u8 id;
	u8 capacity;
	u8 intr_pin;
};

struct ljca_spi_info {
	u8 id;
	u8 capacity;
};

/**
 * typedef ljca_event_cb_t - event callback function signature
 *
 * @context: the execution context of who registered this callback
 * @cmd: the command from device for this event
 * @evt_data: the event data payload
 * @len: the event data payload length
 *
 * The callback function is called in interrupt context and the data payload is
 * only valid during the call. If the user needs later access of the data, it
 * must copy it.
 */
typedef void (*ljca_event_cb_t)(void *context, u8 cmd, const void *evt_data, int len);

/**
 * ljca_register_event_cb - register a callback function to receive events
 *
 * @ljca: ljca device handle
 * @event_cb: callback function
 * @context: execution context of event callback
 *
 * Return: 0 in case of success, negative value in case of error
 */
int ljca_register_event_cb(struct ljca *ljca, ljca_event_cb_t event_cb, void *context);

/**
 * ljca_unregister_event_cb - unregister the callback function for an event
 *
 * @ljca: ljca device handle
 */
void ljca_unregister_event_cb(struct ljca *ljca);

/**
 * ljca_transfer - issue a LJCA command and wait for a response and the
 * associated data
 *
 * @ljca: ljca device handle
 * @cmd: the command to be sent to the device
 * @obuf: the buffer to be sent to the device; it can be NULL if the user
 *	doesn't need to transmit data with this command
 * @obuf_len: the size of the buffer to be sent to the device; it should
 *	be 0 when obuf is NULL
 * @ibuf: any data associated with the response will be copied here; it can be
 *	NULL if the user doesn't need the response data
 * @ibuf_len: must be initialized to the input buffer size; it will be modified
 *	to indicate the actual data transferred; it shouldn't be NULL as well
 *	when ibuf isn't NULL
 *
 * Return: 0 for success, negative value for errors
 */
int ljca_transfer(struct ljca *ljca, u8 cmd, const void *obuf, unsigned int obuf_len,
		  void *ibuf, unsigned int *ibuf_len);

/**
 * ljca_transfer_noack - issue a LJCA command without a response
 *
 * @ljca: ljca device handle
 * @cmd: the command to be sent to the device
 * @obuf: the buffer to be sent to the device; it can be NULL if the user
 *	doesn't need to transmit data with this command
 * @obuf_len: the size of the buffer to be sent to the device
 *
 * Return: 0 for success, negative value for errors
 */
int ljca_transfer_noack(struct ljca *ljca, u8 cmd, const void *obuf, unsigned int obuf_len);

#endif
