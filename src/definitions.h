#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <libopencm3/usb/usbd.h>

extern usbd_device * usbd_dev;

/* common constants */
enum
{
	USB_CDCACM_DATA_IN_ENDPOINT_ADDRESS		= 0x81,
	USB_CDCACM_DATA_OUT_ENDPOINT_ADDRESS		= 0x1,
	USB_CDCACM_PACKET_SIZE				= 64,
};

#endif // CONSTANTS_H

