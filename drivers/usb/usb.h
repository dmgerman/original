#ifndef __LINUX_USB_H
#define __LINUX_USB_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/version.h>


/* USB constants */

/*
 * Device and/or Interface Class codes
 */
#define USB_CLASS_PER_INTERFACE		0	/* for DeviceClass */
#define USB_CLASS_AUDIO			1
#define USB_CLASS_COMM			2
#define USB_CLASS_HID			3
#define USB_CLASS_PRINTER		7
#define USB_CLASS_MASS_STORAGE		8
#define USB_CLASS_HUB			9
#define USB_CLASS_DATA			10
#define USB_CLASS_VENDOR_SPEC		0xff

/*
 * Descriptor types
 */
#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05

#define USB_DT_HUB			0x29
#define USB_DT_HID			0x21
#define USB_DT_REPORT			0x22
#define USB_DT_PHYSICAL			0x23

/*
 * Descriptor sizes per descriptor type
 */
#define USB_DT_DEVICE_SIZE		18
#define USB_DT_CONFIG_SIZE		9
#define USB_DT_INTERFACE_SIZE		9
#define USB_DT_ENDPOINT_SIZE		7
#define USB_DT_ENDPOINT_AUDIO_SIZE	9	/* Audio extension */
#define USB_DT_HUB_NONVAR_SIZE		7

/*
 * USB Request Type and Endpoint Directions
 */
#define USB_DIR_OUT			0
#define USB_DIR_IN			0x80

#define USB_ENDPOINT_NUMBER_MASK	0x0f	/* in bEndpointAddress */
#define USB_ENDPOINT_DIR_MASK		0x80

#define USB_ENDPOINT_XFERTYPE_MASK	0x03	/* in bmAttributes */
#define USB_ENDPOINT_XFER_CONTROL	0
#define USB_ENDPOINT_XFER_ISOC		1
#define USB_ENDPOINT_XFER_BULK		2
#define USB_ENDPOINT_XFER_INT		3

/*
 * USB Packet IDs (PIDs)
 */
#define USB_PID_OUT			0xe1
#define USB_PID_IN			0x69
#define USB_PID_SETUP			0x2d

/*
 * Standard requests
 */
#define USB_REQ_GET_STATUS		0x00
#define USB_REQ_CLEAR_FEATURE		0x01
/* 0x02 is reserved */
#define USB_REQ_SET_FEATURE		0x03
/* 0x04 is reserved */
#define USB_REQ_SET_ADDRESS		0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_GET_INTERFACE		0x0A
#define USB_REQ_SET_INTERFACE		0x0B
#define USB_REQ_SYNCH_FRAME		0x0C

/*
 * HIDD requests
 */
#define USB_REQ_GET_REPORT		0x01
#define USB_REQ_GET_IDLE		0x02
#define USB_REQ_GET_PROTOCOL		0x03
#define USB_REQ_SET_REPORT		0x09
#define USB_REQ_SET_IDLE		0x0A
#define USB_REQ_SET_PROTOCOL		0x0B

#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03

#define USB_HID_RPT_INPUT		0x01
#define USB_HID_RPT_OUTPUT		0x02
#define USB_HID_RPT_FEATURE		0x03

/*
 * Request target types.
 */
#define USB_RT_DEVICE			0x00
#define USB_RT_INTERFACE		0x01
#define USB_RT_ENDPOINT			0x02

#define USB_RT_HUB			(USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define USB_RT_PORT			(USB_TYPE_CLASS | USB_RECIP_OTHER)

#define USB_RT_HIDD			(USB_TYPE_CLASS | USB_RECIP_INTERFACE)

/* /proc/bus/usb/xxx/yyy ioctl codes */

struct usb_proc_ctrltransfer {
	__u8 requesttype;
	__u8 request;
	__u16 value;
	__u16 index;
	__u16 length;
	__u32 timeout;  /* in milliseconds */
 	void *data;
};

struct usb_proc_bulktransfer {
	unsigned int ep;
	unsigned int len;
	unsigned int timeout; /* in milliseconds */
	void *data;
};

struct usb_proc_old_ctrltransfer {
	__u8 requesttype;
	__u8 request;
	__u16 value;
	__u16 index;
	__u16 length;
	/* pointer to data */
	void *data;
};

struct usb_proc_old_bulktransfer {
	unsigned int ep;
	unsigned int len;
	void *data;
};

struct usb_proc_setinterface {
	unsigned int interface;
	unsigned int altsetting;
};

#define USB_PROC_CONTROL           _IOWR('U', 0, struct usb_proc_ctrltransfer)
#define USB_PROC_BULK              _IOWR('U', 2, struct usb_proc_bulktransfer)
#define USB_PROC_OLD_CONTROL       _IOWR('U', 0, struct usb_proc_old_ctrltransfer)
#define USB_PROC_OLD_BULK          _IOWR('U', 2, struct usb_proc_old_bulktransfer)
#define USB_PROC_RESETEP           _IOR('U', 3, unsigned int)
#define USB_PROC_SETINTERFACE      _IOR('U', 4, struct usb_proc_setinterface)
#define USB_PROC_SETCONFIGURATION  _IOR('U', 5, unsigned int)




#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/list.h>
#include <linux/sched.h>

#define USB_MAJOR 180

extern int usb_hub_init(void);
extern int usb_kbd_init(void);
extern int usb_cpia_init(void);
extern int usb_dc2xx_init(void);
extern int usb_mouse_init(void);
extern int usb_printer_init(void);

extern void usb_hub_cleanup(void);
extern void usb_mouse_cleanup(void);

/* for 2.2-kernels */
 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
static __inline__ void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}
#define LIST_HEAD_INIT(name) { &(name), &(name) }

typedef struct wait_queue wait_queue_t;

typedef struct wait_queue *wait_queue_head_t;
#define DECLARE_WAITQUEUE(wait, current) \
	struct wait_queue wait = { current, NULL }
#define DECLARE_WAIT_QUEUE_HEAD(wait)\
	wait_queue_head_t wait

#define init_waitqueue_head(x) *x=NULL
#define init_MUTEX(x) *(x)=MUTEX
#define DECLARE_MUTEX(name) struct semaphore name=MUTEX
#define DECLARE_MUTEX_LOCKED(name) struct semaphore name=MUTEX_LOCKED


#define __set_current_state(state_value)                        \
	do { current->state = state_value; } while (0)
#ifdef __SMP__
#define set_current_state(state_value)          \
	set_mb(current->state, state_value)
#else
#define set_current_state(state_value)          \
	__set_current_state(state_value)
#endif

#endif // 2.2.x


static __inline__ void wait_ms(unsigned int ms)
{
	current->state = TASK_UNINTERRUPTIBLE;
	schedule_timeout(1 + ms * HZ / 1000);
}

typedef struct {
	__u8 requesttype;
	__u8 request;
	__u16 value;
	__u16 index;
	__u16 length;
} devrequest __attribute__ ((packed));

/* USB-status codes:
 * USB_ST* maps to -E* and should go away in the future
*/

#define USB_ST_NOERROR		0
#define USB_ST_CRC		(-EILSEQ)
#define USB_ST_BITSTUFF		(-EPROTO)
#define USB_ST_NORESPONSE	(-ETIMEDOUT)			/* device not responding/handshaking */
#define USB_ST_DATAOVERRUN	(-EOVERFLOW)
#define USB_ST_DATAUNDERRUN	(-EREMOTEIO)
#define USB_ST_BUFFEROVERRUN	(-ECOMM)
#define USB_ST_BUFFERUNDERRUN	(-ENOSR)
#define USB_ST_INTERNALERROR	(-EPROTO) 			/* unknown error */
#define USB_ST_SHORT_PACKET    	(-EREMOTEIO)
#define USB_ST_PARTIAL_ERROR  	(-EXDEV)			/* ISO transfer only partially completed */
#define USB_ST_URB_KILLED     	(-ENOENT)			/* URB canceled by user */
#define USB_ST_URB_PENDING       (-EINPROGRESS)
#define USB_ST_REMOVED		(-ENODEV) 			/* device not existing or removed */
#define USB_ST_TIMEOUT		(-ETIMEDOUT)			/* communication timed out, also in urb->status**/
#define USB_ST_NOTSUPPORTED	(-ENOSYS)			
#define USB_ST_BANDWIDTH_ERROR	(-ENOSPC)			/* too much bandwidth used */
#define USB_ST_URB_INVALID_ERROR  (-EINVAL)			/* invalid value/transfer type */
#define USB_ST_URB_REQUEST_ERROR  (-ENXIO)			/* invalid endpoint */
#define USB_ST_STALL		(-EPIPE) 			/* pipe stalled, also in urb->status*/

/*
 * USB device number allocation bitmap. There's one bitmap
 * per USB tree.
 */
struct usb_devmap {
	unsigned long devicemap[128 / (8*sizeof(unsigned long))];
};

#define USB_MAXBUS		64

struct usb_busmap {
	unsigned long busmap[USB_MAXBUS / (8*sizeof(unsigned long))];
};

/*
 * This is a USB device descriptor.
 *
 * USB device information
 */

/* Everything but the endpoint maximums are aribtrary */
#define USB_MAXCONFIG		8
#define USB_ALTSETTINGALLOC     4
#define USB_MAXALTSETTING	128  /* Hard limit */
#define USB_MAXINTERFACES	32
#define USB_MAXENDPOINTS	32

/* All standard descriptors have these 2 fields in common */
struct usb_descriptor_header {
	__u8  bLength;
	__u8  bDescriptorType;
} __attribute__ ((packed));

/* Device descriptor */
struct usb_device_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdUSB;
	__u8  bDeviceClass;
	__u8  bDeviceSubClass;
	__u8  bDeviceProtocol;
	__u8  bMaxPacketSize0;
	__u16 idVendor;
	__u16 idProduct;
	__u16 bcdDevice;
	__u8  iManufacturer;
	__u8  iProduct;
	__u8  iSerialNumber;
	__u8  bNumConfigurations;
} __attribute__ ((packed));

/* Endpoint descriptor */
struct usb_endpoint_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bEndpointAddress;
	__u8  bmAttributes;
	__u16 wMaxPacketSize;
	__u8  bInterval;
	__u8  bRefresh;
	__u8  bSynchAddress;

   	unsigned char *extra;   /* Extra descriptors */
	int extralen;
} __attribute__ ((packed));

/* HID descriptor */
struct usb_hid_class_descriptor {
	__u8  bDescriptorType;
	__u16 wDescriptorLength;
} __attribute__ ((packed));

struct usb_hid_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdHID;
	__u8  bCountryCode;
	__u8  bNumDescriptors;

	struct usb_hid_class_descriptor desc[1];
} __attribute__ ((packed));

/* Interface descriptor */
struct usb_interface_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bInterfaceNumber;
	__u8  bAlternateSetting;
	__u8  bNumEndpoints;
	__u8  bInterfaceClass;
	__u8  bInterfaceSubClass;
	__u8  bInterfaceProtocol;
	__u8  iInterface;

  	struct usb_endpoint_descriptor *endpoint;

	unsigned char *extra;
	int extralen;
} __attribute__ ((packed));

struct usb_interface {
	struct usb_interface_descriptor *altsetting;

	int act_altsetting;		/* active alternate setting */
	int num_altsetting;		/* number of alternate settings */
	int max_altsetting;             /* total memory allocated */
 
	struct usb_driver *driver;	/* driver */
	void *private_data;
};

/* Configuration descriptor information.. */
struct usb_config_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 wTotalLength;
	__u8  bNumInterfaces;
	__u8  bConfigurationValue;
	__u8  iConfiguration;
	__u8  bmAttributes;
	__u8  MaxPower;

	struct usb_interface *interface;
} __attribute__ ((packed));

/* String descriptor */
struct usb_string_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 wData[1];
} __attribute__ ((packed));

struct usb_device;

struct usb_driver {
	const char *name;

	void * (*probe)(struct usb_device *, unsigned int);
	void (*disconnect)(struct usb_device *, void *);

	struct list_head driver_list;

	struct file_operations *fops;
	int minor;
};

/*
 * Pointer to a device endpoint interrupt function -greg
 *   Parameters:
 *     int status - This needs to be defined.  Right now each HCD
 *         passes different transfer status bits back.  Don't use it
 *         until we come up with a common meaning.
 *     void *buffer - This is a pointer to the data used in this
 *         USB transfer.
 *     int length - This is the number of bytes transferred in or out
 *         of the buffer by this transfer.  (-1 = unknown/unsupported)
 *     void *dev_id - This is a user defined pointer set when the IRQ
 *         is requested that is passed back.
 *
 *   Special Cases:
 *     if (status == USB_ST_REMOVED), don't trust buffer or len.
 */
typedef int (*usb_device_irq)(int, void *, int, void *);

/* -------------------------------------------------------------------------------------* 
 * New USB Structures                                                                   *
 * -------------------------------------------------------------------------------------*/


#define USB_DISABLE_SPD           1
#define USB_ISO_ASAP              2
#define USB_URB_EARLY_COMPLETE    4

typedef struct
{
	unsigned int offset;
	unsigned int length;		// expected length
	unsigned int actual_length;
	unsigned int status;
} iso_packet_descriptor_t, *piso_packet_descriptor_t;

struct urb;
typedef void (*usb_complete_t)(struct urb *);

typedef struct urb
{
	void *hcpriv;			// private data for host controller
	struct list_head urb_list;	// list pointer to all active urbs 
	struct urb* next;		// pointer to next URB	
	struct usb_device *dev;		// pointer to associated USB device
	unsigned int pipe;		// pipe information
	int status;			// returned status
	unsigned int transfer_flags;	// USB_DISABLE_SPD | USB_ISO_ASAP | USB_URB_EARLY_COMPLETE
	void *transfer_buffer;		// associated data buffer
	int transfer_buffer_length;	// data buffer length
	int actual_length;              // actual data buffer length	
	unsigned char* setup_packet;	// setup packet (control only)
	//
	int start_frame;		// start frame (iso/irq only)
	int number_of_packets;		// number of packets in this request (iso/irq only)
	int interval;                   // polling interval (irq only)
	int error_count;		// number of errors in this transfer (iso only)
	//
	void *context;			// context for completion routine
	usb_complete_t complete;	// pointer to completion routine
	//
	iso_packet_descriptor_t	 iso_frame_desc[0];
} urb_t, *purb_t;

#define FILL_CONTROL_URB(a,aa,b,c,d,e,f,g) \
    do {\
	(a)->dev=aa;\
	(a)->pipe=b;\
	(a)->setup_packet=c;\
	(a)->transfer_buffer=d;\
	(a)->transfer_buffer_length=e;\
	(a)->complete=f;\
	(a)->context=g;\
    } while (0)

#define FILL_BULK_URB(a,aa,b,c,d,e,f) \
    do {\
	(a)->dev=aa;\
	(a)->pipe=b;\
	(a)->transfer_buffer=c;\
	(a)->transfer_buffer_length=d;\
	(a)->complete=e;\
	(a)->context=f;\
    } while (0)
    
#define FILL_INT_URB(a,aa,b,c,d,e,f,g) \
    do {\
	(a)->dev=aa;\
	(a)->pipe=b;\
	(a)->transfer_buffer=c;\
	(a)->transfer_buffer_length=d;\
	(a)->complete=e;\
	(a)->context=f;\
	(a)->interval=g;\
	(a)->start_frame=-1;\
    } while (0)

purb_t usb_alloc_urb(int iso_packets);
void usb_free_urb (purb_t purb);
int usb_submit_urb(purb_t purb);
int usb_unlink_urb(purb_t purb);
int usb_internal_control_msg(struct usb_device *usb_dev, unsigned int pipe, devrequest *cmd,  void *data, int len, int timeout);
int usb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, void *data, int len, unsigned long *rval, int timeout);

/*-------------------------------------------------------------------*
 *                         COMPATIBILITY STUFF                       *
 *-------------------------------------------------------------------*/
typedef struct
{
  wait_queue_head_t *wakeup;

  usb_device_irq handler;
  void* stuff;
  /* more to follow */
} api_wrapper_data;

struct irq_wrapper_data {
	void *context;
	usb_device_irq handler;
};

/* ------------------------------------------------------------------------------------- */

struct usb_operations {
	int (*allocate)(struct usb_device *);
	int (*deallocate)(struct usb_device *);
	int (*get_frame_number) (struct usb_device *usb_dev);
	int (*submit_urb) (struct urb* purb);
	int (*unlink_urb) (struct urb* purb);
};

/*
 * Allocated per bus we have
 */
struct usb_bus {
	int busnum;			/* Bus number (in order of reg) */

	struct usb_devmap devmap;       /* Device map */
	struct usb_operations *op;      /* Operations (specific to the HC) */
	struct usb_device *root_hub;    /* Root hub */
	struct list_head bus_list;
	void *hcpriv;                   /* Host Controller private data */

	unsigned int bandwidth_allocated; /* on this Host Controller; */
					  /* applies to Int. and Isoc. pipes; */
					  /* measured in microseconds/frame; */
					  /* range is 0..900, where 900 = */
					  /* 90% of a 1-millisecond frame */
	int bandwidth_int_reqs;		/* number of Interrupt requesters */
	int bandwidth_isoc_reqs;	/* number of Isoc. requesters */

	/* procfs entry */
	struct proc_dir_entry *proc_entry;
};

#define USB_MAXCHILDREN (8)	/* This is arbitrary */

struct usb_device {
	int devnum;			/* Device number on USB bus */
	int slow;			/* Slow device? */

	atomic_t refcnt;		/* Reference count */

	int maxpacketsize;		/* Maximum packet size; encoded as 0,1,2,3 = 8,16,32,64 */
	unsigned int toggle[2];		/* one bit for each endpoint ([0] = IN, [1] = OUT) */
	unsigned int halted[2];		/* endpoint halts; one bit per endpoint # & direction; */
					/* [0] = IN, [1] = OUT */
	struct usb_config_descriptor *actconfig;/* the active configuration */
	int epmaxpacketin[16];		/* INput endpoint specific maximums */
	int epmaxpacketout[16];		/* OUTput endpoint specific maximums */

	struct usb_device *parent;
	struct usb_bus *bus;		/* Bus we're part of */

	struct usb_device_descriptor descriptor;/* Descriptor */
	struct usb_config_descriptor *config;	/* All of the configs */

	char *string;			/* pointer to the last string read from the device */
	int string_langid;		/* language ID for strings */
  
	void *hcpriv;			/* Host Controller private data */
	
	/* procfs entry */
	struct proc_dir_entry *proc_entry;

	/*
	 * Child devices - these can be either new devices
	 * (if this is a hub device), or different instances
	 * of this same device.
	 *
	 * Each instance needs its own set of data structures.
	 */

	int maxchild;			/* Number of ports if hub */
	struct usb_device *children[USB_MAXCHILDREN];
};

extern int usb_register(struct usb_driver *);
extern void usb_deregister(struct usb_driver *);

/* used these for multi-interface device registration */
extern void usb_driver_claim_interface(struct usb_driver *driver, struct usb_interface *iface, void* priv);
extern int usb_interface_claimed(struct usb_interface *iface);
extern void usb_driver_release_interface(struct usb_driver *driver, struct usb_interface *iface);

extern struct usb_bus *usb_alloc_bus(struct usb_operations *);
extern void usb_free_bus(struct usb_bus *);
extern void usb_register_bus(struct usb_bus *);
extern void usb_deregister_bus(struct usb_bus *);

extern struct usb_device *usb_alloc_dev(struct usb_device *parent, struct usb_bus *);
extern void usb_free_dev(struct usb_device *);
extern void usb_inc_dev_use(struct usb_device *);
#define usb_dec_dev_use usb_free_dev
extern void usb_release_bandwidth(struct usb_device *, int);

extern int usb_control_msg(struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size, int timeout);

extern int usb_request_irq(struct usb_device *, unsigned int, usb_device_irq, int, void *, void **);
extern int usb_release_irq(struct usb_device *dev, void *handle, unsigned int pipe);

extern void *usb_request_bulk(struct usb_device *, unsigned int, usb_device_irq, void *, int, void *);
extern int usb_terminate_bulk(struct usb_device *, void *);

extern void usb_init_root_hub(struct usb_device *dev);
extern void usb_connect(struct usb_device *dev);
extern void usb_disconnect(struct usb_device **);

extern void usb_destroy_configuration(struct usb_device *dev);

int usb_get_current_frame_number (struct usb_device *usb_dev);

/*
 * Calling this entity a "pipe" is glorifying it. A USB pipe
 * is something embarrassingly simple: it basically consists
 * of the following information:
 *  - device number (7 bits)
 *  - endpoint number (4 bits)
 *  - current Data0/1 state (1 bit)
 *  - direction (1 bit)
 *  - speed (1 bit)
 *  - max packet size (2 bits: 8, 16, 32 or 64)
 *  - pipe type (2 bits: control, interrupt, bulk, isochronous)
 *
 * That's 18 bits. Really. Nothing more. And the USB people have
 * documented these eighteen bits as some kind of glorious
 * virtual data structure.
 *
 * Let's not fall in that trap. We'll just encode it as a simple
 * unsigned int. The encoding is:
 *
 *  - max size:		bits 0-1	(00 = 8, 01 = 16, 10 = 32, 11 = 64)
 *  - direction:	bit 7		(0 = Host-to-Device [Out], 1 = Device-to-Host [In])
 *  - device:		bits 8-14
 *  - endpoint:		bits 15-18
 *  - Data0/1:		bit 19
 *  - speed:		bit 26		(0 = Full, 1 = Low Speed)
 *  - pipe type:	bits 30-31	(00 = isochronous, 01 = interrupt, 10 = control, 11 = bulk)
 *
 * Why? Because it's arbitrary, and whatever encoding we select is really
 * up to us. This one happens to share a lot of bit positions with the UHCI
 * specification, so that much of the uhci driver can just mask the bits
 * appropriately.
 */

#define PIPE_ISOCHRONOUS		0
#define PIPE_INTERRUPT			1
#define PIPE_CONTROL			2
#define PIPE_BULK			3

#define USB_ISOCHRONOUS		0
#define USB_INTERRUPT		1
#define USB_CONTROL		2
#define USB_BULK		3

#define usb_maxpacket(dev, pipe, out)	(out \
				? (dev)->epmaxpacketout[usb_pipeendpoint(pipe)] \
				: (dev)->epmaxpacketin [usb_pipeendpoint(pipe)] )
#define usb_packetid(pipe)	(((pipe) & USB_DIR_IN) ? USB_PID_IN : USB_PID_OUT)

#define usb_pipeout(pipe)	((((pipe) >> 7) & 1) ^ 1)
#define usb_pipein(pipe)	(((pipe) >> 7) & 1)
#define usb_pipedevice(pipe)	(((pipe) >> 8) & 0x7f)
#define usb_pipe_endpdev(pipe)	(((pipe) >> 8) & 0x7ff)
#define usb_pipeendpoint(pipe)	(((pipe) >> 15) & 0xf)
#define usb_pipedata(pipe)	(((pipe) >> 19) & 1)
#define usb_pipeslow(pipe)	(((pipe) >> 26) & 1)
#define usb_pipetype(pipe)	(((pipe) >> 30) & 3)
#define usb_pipeisoc(pipe)	(usb_pipetype((pipe)) == PIPE_ISOCHRONOUS)
#define usb_pipeint(pipe)	(usb_pipetype((pipe)) == PIPE_INTERRUPT)
#define usb_pipecontrol(pipe)	(usb_pipetype((pipe)) == PIPE_CONTROL)
#define usb_pipebulk(pipe)	(usb_pipetype((pipe)) == PIPE_BULK)

#define PIPE_DEVEP_MASK		0x0007ff00

/* The D0/D1 toggle bits */
#define usb_gettoggle(dev, ep, out) (((dev)->toggle[out] >> ep) & 1)
#define	usb_dotoggle(dev, ep, out)  ((dev)->toggle[out] ^= (1 << ep))
#define usb_settoggle(dev, ep, out, bit) ((dev)->toggle[out] = ((dev)->toggle[out] & ~(1 << ep)) | ((bit) << ep))

/* Endpoint halt control/status */
#define usb_endpoint_out(ep_dir)	(((ep_dir >> 7) & 1) ^ 1)
#define usb_endpoint_halt(dev, ep, out) ((dev)->halted[out] |= (1 << (ep)))
#define usb_endpoint_running(dev, ep, out) ((dev)->halted[out] &= ~(1 << (ep)))
#define usb_endpoint_halted(dev, ep, out) ((dev)->halted[out] & (1 << (ep)))

static inline unsigned int __create_pipe(struct usb_device *dev, unsigned int endpoint)
{
	return (dev->devnum << 8) | (endpoint << 15) | (dev->slow << 26) | dev->maxpacketsize;
}

static inline unsigned int __default_pipe(struct usb_device *dev)
{
	return (dev->slow << 26);
}

/* Create various pipes... */
#define usb_sndctrlpipe(dev,endpoint)	((PIPE_CONTROL << 30) | __create_pipe(dev,endpoint))
#define usb_rcvctrlpipe(dev,endpoint)	((PIPE_CONTROL << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_sndisocpipe(dev,endpoint)	((PIPE_ISOCHRONOUS << 30) | __create_pipe(dev,endpoint))
#define usb_rcvisocpipe(dev,endpoint)	((PIPE_ISOCHRONOUS << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_sndbulkpipe(dev,endpoint)	((PIPE_BULK << 30) | __create_pipe(dev,endpoint))
#define usb_rcvbulkpipe(dev,endpoint)	((PIPE_BULK << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_sndintpipe(dev,endpoint)	((PIPE_INTERRUPT << 30) | __create_pipe(dev,endpoint))
#define usb_rcvintpipe(dev,endpoint)	((PIPE_INTERRUPT << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_snddefctrl(dev)		((PIPE_CONTROL << 30) | __default_pipe(dev))
#define usb_rcvdefctrl(dev)		((PIPE_CONTROL << 30) | __default_pipe(dev) | USB_DIR_IN)

/*
 * Send and receive control messages..
 */
int usb_new_device(struct usb_device *dev);
int usb_set_address(struct usb_device *dev);
int usb_get_descriptor(struct usb_device *dev, unsigned char desctype, unsigned
char descindex, void *buf, int size);
int usb_get_device_descriptor(struct usb_device *dev);
int usb_get_status (struct usb_device *dev, int type, int target, void *data);
int usb_get_protocol(struct usb_device *dev);
int usb_set_protocol(struct usb_device *dev, int protocol);
int usb_set_interface(struct usb_device *dev, int interface, int alternate);
int usb_set_idle(struct usb_device *dev, int duration, int report_id);
int usb_set_configuration(struct usb_device *dev, int configuration);
int usb_get_report(struct usb_device *dev, unsigned char type, unsigned char id, unsigned char index, void *buf, int size);
char *usb_string(struct usb_device *dev, int index);
int usb_clear_halt(struct usb_device *dev, int endp);

/*
 * Some USB bandwidth allocation constants.
 */
#define BW_HOST_DELAY	1000L		/* nanoseconds */
#define BW_HUB_LS_SETUP	333L		/* nanoseconds */
                        /* 4 full-speed bit times (est.) */

#define FRAME_TIME_BITS         12000L		/* frame = 1 millisecond */
#define FRAME_TIME_MAX_BITS_ALLOC	(90L * FRAME_TIME_BITS / 100L)
#define FRAME_TIME_USECS	1000L
#define FRAME_TIME_MAX_USECS_ALLOC	(90L * FRAME_TIME_USECS / 100L)

#define BitTime(bytecount)  (7 * 8 * bytecount / 6)  /* with integer truncation */
		/* Trying not to use worst-case bit-stuffing
                   of (7/6 * 8 * bytecount) = 9.33 * bytecount */
		/* bytecount = data payload byte count */

#define NS_TO_US(ns)	((ns + 500L) / 1000L)
			/* convert & round nanoseconds to microseconds */

/*
 * Debugging helpers..
 */
void usb_show_device_descriptor(struct usb_device_descriptor *);
void usb_show_config_descriptor(struct usb_config_descriptor *);
void usb_show_interface_descriptor(struct usb_interface_descriptor *);
void usb_show_hid_descriptor(struct usb_hid_descriptor * desc);
void usb_show_endpoint_descriptor(struct usb_endpoint_descriptor *);
void usb_show_device(struct usb_device *);
void usb_show_string(struct usb_device *dev, char *id, int index);

#ifdef USB_DEBUG
#define PRINTD(format, args...) printk(KERN_DEBUG "usb: " format "\n" , ## args);
#else /* NOT DEBUGGING */
#define PRINTD(fmt, arg...) do {} while (0)
#endif /* USB_DEBUG */
/* A simple way to change one line from DEBUG to NOT DEBUG: */
#define XPRINTD(fmt, arg...)	do {} while (0)

/*
 * procfs stuff
 */

#ifdef CONFIG_USB_PROC
void proc_usb_add_bus(struct usb_bus *bus);
void proc_usb_remove_bus(struct usb_bus *bus);
void proc_usb_add_device(struct usb_device *dev);
void proc_usb_remove_device(struct usb_device *dev);
#else
extern inline void proc_usb_add_bus(struct usb_bus *bus) {}
extern inline void proc_usb_remove_bus(struct usb_bus *bus) {}
extern inline void proc_usb_add_device(struct usb_device *dev) {}
extern inline void proc_usb_remove_device(struct usb_device *dev) {}
#endif

#endif  /* __KERNEL__ */

#endif
