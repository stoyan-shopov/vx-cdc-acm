#include <stdint.h>
#include <libopencm3/usb/usbstd.h>

#include "definitions.h"
#include "engine.h"
#include "sf-arch.h"

int sfgetc(void)
{
static uint8_t inbuf[USB_CDCACM_PACKET_SIZE];
static int idx, inbuf_len;

	while (idx == inbuf_len)
	{
		usbd_poll(usbd_dev);
		idx = 0;
		inbuf_len = usbd_ep_read_packet(usbd_dev, USB_CDCACM_DATA_OUT_ENDPOINT_ADDRESS, inbuf, sizeof inbuf);
	}
	return inbuf[idx ++];
}

static uint8_t outbuf[USB_CDCACM_PACKET_SIZE

/* \todo	!!! ugly workaround !!!
 *		this is a workaround to avoid having to send packets with size
 *		equal to the endpoint size;
 *		in such cases, sending a short (zero-length) packet is necessary
 *		so that the usb host knows there is no more data to be received;
 *		however, with the current implementation of 'usbd_ep_write_packet()'
 *		in libopencm3, there is no way of knowing if a zero-length packet
 *		has failed or not, because 'usbd_ep_write_packet()' returns zero
 *		on failure, or the length of the packet sent on success - which in
 *		this case is zero as well!!!
 *
 *		maybe it is better for 'usbd_ep_write_packet()' to return -1 on
 *		failure...
 *
 *		for the moment - make sure that sending short packets is never necessary
 *		by making the host input buffer smaller than the corresponding
 *		endpoint size
 */
	- 1
];
static int outbuf_idx;
int sfsync(void)
{
	while (!usbd_ep_write_packet(usbd_dev, USB_CDCACM_DATA_IN_ENDPOINT_ADDRESS, outbuf, outbuf_idx))
		usbd_poll(usbd_dev);
	outbuf_idx = 0;
}

int sfputc(int c)
{
	outbuf[outbuf_idx ++] = c;
	if (outbuf_idx == sizeof outbuf)
		sfsync();
}
