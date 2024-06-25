/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Li-Wen Hsu
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"

struct uvc_local_quirk_entry {
	uint16_t vendor_id;
	uint16_t product_id;
	uint8_t quirk_mask;
	const char *device_name;
	const char *freebsd_rationale;
};

/*
 * Keep this table limited to devices with FreeBSD-local evidence.
 * The previous carried-forward probe-minmax list was removed because this
 * tree does not currently retain per-device validation notes for it.
 */
static const struct uvc_local_quirk_entry uvc_local_quirks[] = {
	{
		.vendor_id = 0x05a6,
		.product_id = 0x0b01,
		.quirk_mask = UVC_QUIRK_NO_EOF,
		.device_name = "Cisco-Linksys ROOMKIT0B01",
		.freebsd_rationale = "Keep the EOF workaround local to UVC",
	},
};

uint8_t
uvc_quirks_lookup_local(device_t dev)
{
	uint32_t vid, pid;
	size_t i;

	device_get_usb_vidpid(dev, &vid, &pid);
	for (i = 0; i < nitems(uvc_local_quirks); i++) {
		if (uvc_local_quirks[i].vendor_id == vid &&
		    uvc_local_quirks[i].product_id == pid)
			return (uvc_local_quirks[i].quirk_mask);
	}

	return (0);
}
