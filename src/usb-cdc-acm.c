/*
Copyright (c) 2016 Stoyan Shopov (stoyan.shopov@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/* usb communication device class (cdc), abstract control model (acm) implementation
 * for use with the libopencm3 library */

/* notes: refer to the usb communication device class specification,
 * PSTN subclass specification, version 1.2 for details:
 * http://www.usb.org/developers/docs/devclass_docs/CDC1.2_WMC1.1_012011.zip
 *
 * a document with nice diagrams of the required usb descriptors,
 * along with code samples, can be found here:
 * https://www.xmos.com/download/private/AN00124%3A-USB-CDC-Class-as-Virtual-Serial-Port%282.0.1rc1%29.pdf
 *
 * sample descriptors for a simple usb cdc acm device can also be found in
 * document 'CDC120-20101103-track.pdf' in the usb communication device class
 * specification archive, in section 5.3
 *
 * the usb descriptor set for a cdc acm usb device is somewhat complicated;
 * i have tried to make a diagram of the usb descriptor set hierarchy for
 * such a device:

                      +-----------------+
                      |device descriptor|
                      +--------+--------+
                               v
                  +------------+-----------+
                  |configuration descriptor|
          +-----------------------------------------+
          |                                         |
          |                                         |
+---------v----------+                   +----------v---------+
|communications      |                   |data                |
|interface descriptor|                   |interface descriptor|
+---------+----------+                   +----------+---------+
          |                                         |
          +--------------+                          +---------------+
          |              |                          |               |
          |   +----------v----------+               |     +---------v---------+
          |   |header               |               |     |data IN            |
          |   |functional descriptor|               |     |endpoint descriptor|
          |   +---------------------+               |     +-------------------+
          +--------------+                          +---------------+
          |              |           +                              |
          |   +----------v-----------+                    +---------v---------+
          |   |abstract control model|                    |data OUT           |
          |   |functional descriptor +                    |endpoint descriptor|
          |   +----------------------                     +-------------------+
          +--------------+
          |              |
          |   +----------v----------+
          |   |union                |
          |   |functional descriptor|
          |   +---------------------+
          +--------------+
          |              |
          |   +----------v----------+
          |   |call management      |
          |   |functional descriptor|
          |   +---------------------+
          |
          |
   +------v------------+
   |notification IN    |
   |endpoint descriptor|
   +-------------------+

 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/usb/usbstd.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

/* usb cdcacm device configuration */
enum
{
	/*! \todo	for some reason, values smaller than 32
	 *		(e.g. 8, 16) do not work and the usb device
	 *		does not enumerate properly; maybe investigate
	 *		this */
	USB_CONTROL_ENDPOINT_SIZE			= 32,
	USB_CDCACM_DATA_IN_ENDPOINT_ADDRESS		= 0x81,
	USB_CDCACM_DATA_OUT_ENDPOINT_ADDRESS		= 0x1,
	USB_CDCACM_COMMUNICATION_IN_ENDPOINT_ADDRESS	= 0x82,
	USB_CDCACM_PACKET_SIZE				= 64,
	USB_CDCACM_POLLING_INTERVAL_MS			= 1,
	USB_CDCACM_CONTROL_INTERFACE_NUMBER		= 0,
	USB_CDCACM_DATA_INTERFACE_NUMBER		= 1,
};


/* usb descriptors */
static const struct usb_device_descriptor usb_device_descriptor =
{
	.bLength		=	USB_DT_DEVICE_SIZE,
	.bDescriptorType	=	USB_DT_DEVICE,
	.bcdUSB			=	0x200,
	.bDeviceClass		=	USB_CLASS_VENDOR,
	.bDeviceSubClass	=	0,
	.bDeviceProtocol	=	0,
	.bMaxPacketSize0	=	USB_CONTROL_ENDPOINT_SIZE,
	.idVendor		=	0x1ad4,
	.idProduct		=	0xb000,
	.bcdDevice		=	0x0100,
	.iManufacturer		=	0,
	.iProduct		=	0,
	.iSerialNumber		=	0,
	.bNumConfigurations	=	1,
};

/* communications class interface notification endpoint; this interrupt IN endpoint is
 * meant to be used as a notification for communication line state changes to the
 * usb host; it is not really appropriate/useful for a virtual serial port device */
static const struct usb_endpoint_descriptor usb_cdcacm_communication_endpoint[] = 
{
	{
		.bLength			=	USB_DT_ENDPOINT_SIZE,
		.bDescriptorType		=	USB_DT_ENDPOINT,
		.bEndpointAddress		=	USB_CDCACM_COMMUNICATION_IN_ENDPOINT_ADDRESS,
		.bmAttributes			=	USB_ENDPOINT_ATTR_INTERRUPT,
		.wMaxPacketSize			=	USB_CDCACM_PACKET_SIZE,
		.bInterval			=	USB_CDCACM_POLLING_INTERVAL_MS,
	},
};

static const struct usb_endpoint_descriptor usb_cdcacm_data_endpoints[] = 
{
	{
		.bLength			=	USB_DT_ENDPOINT_SIZE,
		.bDescriptorType		=	USB_DT_ENDPOINT,
		.bEndpointAddress		=	USB_CDCACM_DATA_IN_ENDPOINT_ADDRESS,
		.bmAttributes			=	USB_ENDPOINT_ATTR_BULK,
		.wMaxPacketSize			=	USB_CDCACM_PACKET_SIZE,
		.bInterval			=	USB_CDCACM_POLLING_INTERVAL_MS,
	},
	{
		.bLength			=	USB_DT_ENDPOINT_SIZE,
		.bDescriptorType		=	USB_DT_ENDPOINT,
		.bEndpointAddress		=	USB_CDCACM_DATA_OUT_ENDPOINT_ADDRESS,
		.bmAttributes			=	USB_ENDPOINT_ATTR_BULK,
		.wMaxPacketSize			=	USB_CDCACM_PACKET_SIZE,
		.bInterval			=	USB_CDCACM_POLLING_INTERVAL_MS,
	},
};

static const struct __attribute__((packed))
{
	struct usb_cdc_header_descriptor		h;
	struct usb_cdc_acm_descriptor			acm;
	struct usb_cdc_union_descriptor			u;
	struct usb_cdc_call_management_descriptor	c;
}
usb_cdcacm_functional_descriptors = 
{
	.h =
	{
		.bFunctionLength	= sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType	= CS_INTERFACE,
		.bDescriptorSubtype	= USB_CDC_TYPE_HEADER,
		.bcdCDC			= 0x110,
	},
	.acm = 
	{
		.bFunctionLength	= sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType	= CS_INTERFACE,
		.bDescriptorSubtype	= USB_CDC_TYPE_ACM,
		.bmCapabilities		= 0,	/* no commands supported */
	},
	.u =
	{
		.bFunctionLength	= sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType	= CS_INTERFACE,
		.bDescriptorSubtype	= USB_CDC_TYPE_UNION,
		.bControlInterface	= USB_CDCACM_CONTROL_INTERFACE_NUMBER,
		.bSubordinateInterface0	= USB_CDCACM_DATA_INTERFACE_NUMBER,
	},
	.c =
	{
		.bFunctionLength	= sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType	= CS_INTERFACE,
		.bmCapabilities		= 0,	/* no call management cababilities */
		.bDataInterface		= USB_CDCACM_DATA_INTERFACE_NUMBER,
	},
};

static const struct usb_interface_descriptor cdcacm_communications_interface =
{
	.bLength		=	USB_DT_INTERFACE_SIZE,
	.bDescriptorType	=	USB_DT_INTERFACE,
	.bInterfaceNumber	=	USB_CDCACM_CONTROL_INTERFACE_NUMBER,
	.bAlternateSetting	=	0,
	.bNumEndpoints		=	1,	/* one notification IN endpoint */
	.bInterfaceClass	=	USB_CLASS_CDC,
	.bInterfaceSubClass	=	USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol	=	0,
	.iInterface		=	0,
	.endpoint		=	usb_cdcacm_communication_endpoint,
	.extra			=	& usb_cdcacm_functional_descriptors,
	.extralen		=	sizeof usb_cdcacm_functional_descriptors,
};
static const struct usb_interface_descriptor cdcacm_data_interface =
{
	.bLength		=	USB_DT_INTERFACE_SIZE,
	.bDescriptorType	=	USB_DT_INTERFACE,
	.bInterfaceNumber	=	USB_CDCACM_DATA_INTERFACE_NUMBER,
	.bAlternateSetting	=	0,
	.bNumEndpoints		=	2,	/* two data endpoints, for usb data IN/OUT transfers */
	.bInterfaceClass	=	USB_CLASS_DATA,
	.bInterfaceSubClass	=	0,
	.bInterfaceProtocol	=	0,
	.iInterface		=	0,
	.endpoint		=	usb_cdcacm_data_endpoints,
	.extra			=	0,
	.extralen		=	0,
};


static const struct usb_interface usb_interfaces[] =
{
	{
		.num_altsetting	=	1,
		.altsetting	=	& cdcacm_communications_interface,
	},
	{
		.num_altsetting	=	1,
		.altsetting	=	& cdcacm_data_interface,
	},
};

static const struct usb_config_descriptor usb_config_descriptor =
{
	.bLength		=	USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType	=	USB_DT_CONFIGURATION,
	/* the wTotalLength field below is automatically computed on-the-fly
	 * by the libopencm3 library before sending the configuration descriptor
	 * to the host; it is not updated here, so this data structure can be
	 * defined as 'const' */
	/* .wTotalLength	= xxx*/
	.bNumInterfaces		=	2,
	.bConfigurationValue	=	1,
	.iConfiguration		=	0,
	.bmAttributes		=	USB_CONFIG_ATTR_DEFAULT,
	.bMaxPower		=	50,	/* in 2 mA units */
	.interface		=	usb_interfaces,
};

static const char * usb_strings[] =
{
};
static uint8_t usb_control_buffer[128];


static int usbd_cdcacm_control_callback(usbd_device *usbd_dev,
		struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
		usbd_control_complete_callback *complete)
{
#if 0
	if (req->bmRequestType != (USB_REQ_TYPE_IN | USB_REQ_TYPE_INTERFACE)
			|| req->bRequest != USB_REQ_GET_DESCRIPTOR
			|| req->wValue != (USB_DT_REPORT << 8)
			|| req->wIndex != USB_CDCACM_INTERFACE_NUMBER)
		return USBD_REQ_NEXT_CALLBACK;

	buf[0] = (uint8_t *) usb_cdcacm_report_descriptor;
	len[0] = sizeof usb_cdcacm_report_descriptor;
#endif
	return USBD_REQ_HANDLED;
}
volatile static bool is_usb_device_configured;
static void usbd_cdcacm_set_config_callback(usbd_device * usbd_dev, uint16_t wValue)
{
	usbd_ep_setup(usbd_dev, USB_CDCACM_COMMUNICATION_IN_ENDPOINT_ADDRESS, USB_ENDPOINT_ATTR_INTERRUPT, USB_CDCACM_PACKET_SIZE, 0);
	usbd_ep_setup(usbd_dev, USB_CDCACM_DATA_IN_ENDPOINT_ADDRESS, USB_ENDPOINT_ATTR_BULK, USB_CDCACM_PACKET_SIZE, 0);
	usbd_ep_setup(usbd_dev, USB_CDCACM_DATA_OUT_ENDPOINT_ADDRESS, USB_ENDPOINT_ATTR_BULK, USB_CDCACM_PACKET_SIZE, 0);
	usbd_register_control_callback(usbd_dev,
			USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
			USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
			usbd_cdcacm_control_callback);
	is_usb_device_configured = true;
}

static void rcc_clock_setup_in_hse_8mhz_out_48mhz(void)
{
	rcc_osc_on(RCC_HSE);
	rcc_wait_for_osc_ready(RCC_HSE);
	rcc_set_sysclk_source(RCC_HSE);

	rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
	rcc_set_ppre(RCC_CFGR_PPRE_NODIV);

	flash_set_ws(FLASH_ACR_LATENCY_024_048MHZ);

	/* 8MHz * 12 / 2 = 48MHz */
	rcc_set_pll_multiplication_factor(RCC_CFGR_PLLMUL_MUL12);

	RCC_CFGR &= ~RCC_CFGR_PLLSRC;

	rcc_osc_on(RCC_PLL);
	rcc_wait_for_osc_ready(RCC_PLL);
	rcc_set_sysclk_source(RCC_PLL);

	rcc_apb1_frequency = 48000000;
	rcc_ahb_frequency = 48000000;
}

enum
{
	USB_CONNECT_PORT	= GPIOA,
	USB_CONNECT_PIN		= GPIO8,
};

int main(void)
{
	usbd_device * usbd_dev;
	rcc_periph_clock_enable(RCC_GPIOA);

	gpio_mode_setup(USB_CONNECT_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, USB_CONNECT_PIN);
	gpio_set_output_options(USB_CONNECT_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_HIGH, USB_CONNECT_PIN);

	rcc_set_usbclk_source(RCC_PLL);
	rcc_clock_setup_in_hse_8mhz_out_48mhz();
	gpio_set(USB_CONNECT_PORT, USB_CONNECT_PIN);
	usbd_dev = usbd_init(& st_usbfs_v2_usb_driver, & usb_device_descriptor, & usb_config_descriptor,
			usb_strings, sizeof usb_strings / sizeof * usb_strings,
			usb_control_buffer, sizeof usb_control_buffer);
	usbd_register_set_config_callback(usbd_dev, usbd_cdcacm_set_config_callback);
	/* simple loopback test loop */
	while (1)
	{
		int i = 0;
		char buf[64];
		if (is_usb_device_configured && (i = usbd_ep_read_packet(usbd_dev, USB_CDCACM_DATA_OUT_ENDPOINT_ADDRESS, buf, sizeof buf)))
		{
			while (!usbd_ep_write_packet(usbd_dev, USB_CDCACM_DATA_IN_ENDPOINT_ADDRESS, buf, i));
			while (!usbd_ep_write_packet(usbd_dev, USB_CDCACM_DATA_IN_ENDPOINT_ADDRESS, ">>>", 3));
		}
		usbd_poll(usbd_dev);
	}
}

