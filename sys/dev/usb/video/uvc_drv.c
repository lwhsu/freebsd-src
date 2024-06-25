/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Dell Inc.
 *
 *	Alvin Chen <weike_chen@dell.com, vico.chern@qq.com>
 *	Zhichao Li <Zhichao1.Li@Dell.com>
 *	Pillar Liang <Pillar.Liang@Dellteam.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * UVC spec:https://www.usb.org/sites/default/files/USB_Video_Class_1_5.zip
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/videoio_uvc.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_request.h>

#define USB_DEBUG_VAR uvc_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_core.h>
#include <dev/usb/usb_device.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"
#include "uvc_buf.h"
#include "uvc_ctrl_internal.h"
#include "uvc_negotiation.h"
#include "uvc_v4l2.h"

#ifdef USB_DEBUG
int uvc_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uvc, CTLFLAG_RW, 0, "USB uvc");
SYSCTL_INT(_hw_usb_uvc, OID_AUTO, debug, CTLFLAG_RWTUN,
		&uvc_debug, 0, "Debug level");
#endif

MALLOC_DEFINE(M_UVC, "uvc", "USB Video Camera");
#define	UVC_BULK_HS_BUFFER_SIZE   (64 * 1024) /* bytes */

static void uvc_drv_destroy_data(struct uvc_drv_data *data);
static void uvc_drv_destroy_ctrl(struct uvc_drv_ctrl *c);

/*
 * Controls
 * Formats
 */
static struct uvc_drv_format_desc uvc_fmts[] = {
	{
		.name	=	"YUV 4:2:2 (YUYV)",
		.guid	=	UVC_GUID_FORMAT_YUY2,
		.fcc	=	V4L2_PIX_FMT_YUYV,
	},
	{
		.name	=	"YUV 4:2:0 (NV12)",
		.guid	=	UVC_GUID_FORMAT_NV12,
		.fcc	=	V4L2_PIX_FMT_NV12,
	},
	{
		.name	=	"MJPEG",
		.guid	=	UVC_GUID_FORMAT_MJPEG,
		.fcc	=	V4L2_PIX_FMT_MJPEG,
	},
	{
		.name	=	"YVU 4:2:0 (YV12)",
		.guid	=	UVC_GUID_FORMAT_YV12,
		.fcc	=	V4L2_PIX_FMT_YVU420,
	},
	{
		.name	=	"YUV 4:2:0 (I420)",
		.guid	=	UVC_GUID_FORMAT_I420,
		.fcc	=	V4L2_PIX_FMT_YUV420,
	},
	{
		.name	=	"YUV 4:2:2 (UYVY)",
		.guid	=	UVC_GUID_FORMAT_UYVY,
		.fcc	=	V4L2_PIX_FMT_UYVY,
	},
	{
		.name	=	"Greyscale",
		.guid	=	UVC_GUID_FORMAT_Y800,
		.fcc	=	V4L2_PIX_FMT_GREY,
	},
	{
		.name	=	"RGB Bayer",
		.guid	=	UVC_GUID_FORMAT_BY8,
		.fcc	=	V4L2_PIX_FMT_SBGGR8,
	},
	{
		.name	=	"H.264",
		.guid	=	UVC_GUID_FORMAT_H264,
		.fcc	=	V4L2_PIX_FMT_H264,
	},

};

const unsigned char
vc_ext_h264ctrl[16] = {
	0x41, 0x76, 0x9e, 0xa2,
	0x4, 0xde, 0xe3, 0x47,
	0x8b, 0x2b, 0xf4, 0x34,
	0x1a, 0xff, 0x0, 0x3b
};

static uint8_t
usbd_iface_num_to_index(struct usb_device *udev, int num)
{
	struct usb_interface_descriptor *id;
	struct usb_interface *iface;
	struct usb_idesc_parse_state ips;
	uint8_t index = -1;

	memset(&ips, 0x0, sizeof(ips));
	while ((id = usb_idesc_foreach(udev->cdesc, &ips))) {
		iface = udev->ifaces + ips.iface_index;
		if (iface->idesc->bInterfaceNumber == num &&
			ips.iface_index_alt == 0) {
			index = ips.iface_index;
			break;
		}
	}

	return (index);
}

static uint8_t
usbd_iface_get_altsetting_num(struct usb_device *udev,
	struct usb_interface *iface)
{
	struct usb_descriptor *desc;
	int num = 1;
	uint8_t iface_num = iface->idesc->bInterfaceNumber;

	desc = (struct usb_descriptor *)(iface->idesc);
	while ((desc = usb_desc_foreach(udev->cdesc, desc))) {
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			if (desc->bDescriptorSubtype != iface_num)
				break;
			num++;
		}
	}

	return (num);
}

int
uvc_drv_do_request(struct usb_device *udev, uint8_t query, uint8_t unit,
	uint8_t iface_num, uint8_t cs, void *data, uint16_t size, int timeout)
{
	struct usb_device_request req;
	int err;
	uint8_t type = UT_CLASS | UT_INTERFACE;

	memset(&req, 0x00, sizeof(req));
	req.bRequest = query;
	type |= (query & 0x80) ? UT_READ : UT_WRITE;
	req.bmRequestType = type;
	USETW(req.wValue, cs << 8);
	USETW(req.wIndex, unit << 8 | iface_num);
	USETW(req.wLength, size);

	err = usbd_do_request_flags(udev, NULL, &req, data, 0, NULL, timeout);
	if (err)
		DPRINTF("%s query:%u error:%s\n", __func__, query,
		    usbd_errstr(err));
	return (err);
}

static void
uvc_drv_show_video_ctrl(struct uvc_data_request *req)
{
	DPRINTF("hint:%x formatindex:%u frameindex:%u itv:%u\n",
		UGETW(req->wHint), req->bFormatIndex, req->bFrameIndex,
		UGETDW(req->dwFrameInterval));
	DPRINTF("keyrate:%u prate:%u compq:%u compwinsize:%u\n",
		UGETW(req->wKeyFrameRate), UGETW(req->wPFrameRate),
		UGETW(req->wCompQuality), UGETW(req->wCompWindowSize));
	DPRINTF("delay:%u framesize:%u payloadsize:%u framinginfo:%x\n",
	    UGETW(req->wDelay), UGETDW(req->dwMaxFrameSize),
	    UGETDW(req->dwMaxPayloadSize), req->bFramingInfo);
}

static int
uvc_drv_xu_request_is_read(uint8_t request_code)
{
	return ((request_code & 0x80) != 0);
}

static int
uvc_drv_do_control_request(struct uvc_drv_video *v, uint8_t unit_id,
    uint8_t selector, uint8_t request_code, void *buffer, uint16_t data_len)
{
	int ret;
	uint8_t iface_num;

	mtx_lock(&v->mtx);
	iface_num = v->ctrl != NULL ? v->ctrl->iface_num : 0;
	mtx_unlock(&v->mtx);

	ret = uvc_drv_do_request(v->sc->udev, request_code, unit_id, iface_num,
	    selector, buffer, data_len, USB_DEFAULT_TIMEOUT);
	if (ret != 0)
		DPRINTF("%s %d ret:%d\n", __func__, __LINE__, ret);

	return (ret);
}

int
uvc_drv_control_request(struct uvc_drv_video *v, uint8_t unit_id,
    uint8_t selector, uint8_t request_code, void *buffer, uint16_t data_len)
{
	return (uvc_drv_do_control_request(v, unit_id, selector, request_code,
	    buffer, data_len));
}

static void
uvc_drv_note_h264_xu_write(struct uvc_drv_video *v,
    const struct uvc_drv_xu_request *req)
{
	/*
	 * Some cameras only switch their stream negotiation path into the
	 * H.264 transport mode after a successful H.264 extension-unit write.
	 */
	if (req->unit_id != v->ctrl->h264id)
		return;

	DPRINTF("--%s open h264\n", __func__);
	mtx_lock(&v->mtx);
	v->htsf = 1;
	mtx_unlock(&v->mtx);
}

int
uvc_drv_xu_query(struct uvc_drv_video *v, const struct uvc_drv_xu_request *req)
{
	uint8_t *tmp;
	int ret;

	DPRINTF("%s query:%x unit:%d selector:%d size:%d\n",
		__func__, req->request_code, req->unit_id, req->control_selector,
		req->data_len);

	tmp = malloc(req->data_len, M_UVC, M_WAITOK);
	if (tmp == NULL)
		return (ENOMEM);

	if (!uvc_drv_xu_request_is_read(req->request_code)) {
		ret = copyin(req->data, tmp, req->data_len);
		if (ret != 0)
			goto out;
	}

	ret = uvc_drv_do_control_request(v, req->unit_id, req->control_selector,
	    req->request_code, tmp, req->data_len);
	if (ret != 0)
		goto out;

	if (uvc_drv_xu_request_is_read(req->request_code))
		ret = copyout(tmp, req->data, req->data_len);
	else
		uvc_drv_note_h264_xu_write(v, req);

out:
	free(tmp, M_UVC);
	return (ret);
}

int
uvc_drv_set_video(struct uvc_drv_video *video,
	struct uvc_data_request *req, struct uvc_data_format *fmt,
	struct uvc_data_frame *frm)
{
	int ret;
	struct uvc_softc *sc = video->sc;

	DPRINTF("set video %p\n", curthread);

	uvc_drv_show_video_ctrl(req);
	mtx_lock(&video->mtx);

	if (video->enable) {
		DPRINTF("video is enable\n");
		mtx_unlock(&video->mtx);
		return (EBUSY);
	}
	if (sc->quirks & UVC_QUIRK_COMMIT_IN_ADVANCE)
		ret = uvc_drv_set_video_ctrl(video, req, 0);
	else
		ret = uvc_drv_set_video_ctrl(video, req, 1);
	if (ret) {
		DPRINTF("video CONTROL COMMIT fault %d\n", ret);
		mtx_unlock(&video->mtx);
		return (ret);
	}

	video->cur_fmt = fmt;
	video->cur_frm = frm;
	memcpy(&video->req, req, sizeof(struct uvc_data_request));

	mtx_unlock(&video->mtx);
	return (0);
}

int
uvc_drv_check_video_context(struct uvc_drv_video *v, unsigned char *data,
	uint32_t len)
{
	if (!v->cur_fmt || v->cur_fmt->fcc != V4L2_PIX_FMT_MJPEG || v->htsf)
		return (0);

	if (len < 2)
		return (1);

	if (data[0] != 0xFF || data[1] != 0xD8)
		return (1);

	return (0);
}

int
uvc_drv_get_pixelaspect(void)
{
	DPRINTF("%s using square-pixel defaults\n", __func__);
	return (0);
}

int
uvc_drv_get_v4l2_fmt(struct uvc_drv_video *v, struct v4l2_format *vfmt)
{
	struct uvc_data_format *fmt = NULL;
	struct uvc_data_frame *frm = NULL;
	struct v4l2_pix_format *pix = &vfmt->fmt.pix;
	DPRINTF("v4l2-dev get format\n");
	if (v->cur_fmt == NULL || v->cur_frm == NULL) {
		DPRINTF("get cur format failed\n");
		return (EINVAL);
	}

	fmt = v->cur_fmt;
	frm = v->cur_frm;

	pix->pixelformat = fmt->fcc;
	pix->width = frm->width;
	pix->height = frm->height;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = fmt->bpp * frm->width / 8;
	pix->sizeimage = UGETDW(v->req.dwMaxFrameSize);
	pix->colorspace = fmt->colorspace;
	pix->priv = 0;

	DPRINTF("width:%d height:%d bytesperline:%d ", pix->width,
		pix->height, pix->bytesperline);
	DPRINTF("sizeimage:%d colorspace:%d\n", pix->sizeimage,
		pix->colorspace);

	return (0);
}

int
uvc_drv_enum_v4l2_framesizes(struct uvc_drv_video *v,
	struct v4l2_frmsizeenum *fs)
{
	struct uvc_data_format *loop, *fmt = NULL;
	struct uvc_data_frame *frm;
	int i;

	for (i = 0; i < v->data->nfmt; i++) {
		loop = v->data->fmt + i;
		if (loop->fcc == fs->pixel_format) {
			fmt = loop;
			break;
		}
	}

	if (fmt == NULL)
		return (EINVAL);

	if (fs->index >= fmt->nfrm)
		return (EINVAL);

	frm = fmt->frm + fs->index;
	fs->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fs->x.discrete.width = frm->width;
	fs->x.discrete.height = frm->height;

	return (0);
}

int
uvc_drv_enum_v4l2_fmt(struct uvc_drv_video *v, struct v4l2_fmtdesc *vfmt)
{
	struct uvc_data_format *fmt;

	if (v->data->nfmt <= vfmt->index)
		return (EINVAL);

	fmt = v->data->fmt + vfmt->index;
	vfmt->flags = 0;
	if (fmt->flags & UVC_FMT_FLAG_COMPRESSED)
		vfmt->flags |= V4L2_FMT_FLAG_COMPRESSED;
	strlcpy(vfmt->description, fmt->name, sizeof(vfmt->description));
	vfmt->pixelformat = fmt->fcc;
	DPRINTF("pixelformat:%x, fmt name: %s\n",
		vfmt->pixelformat,
		vfmt->description);
	return (0);
}

static int
uvc_drv_bulkfill_buf(struct uvc_drv_video *v, struct usb_page_cache *pc,
	usb_frlength_t maxFramelen, usb_frlength_t actlen)
{
	struct uvc_data_payload_header h;
	usb_frlength_t offset = 0;
	usb_frlength_t datalen = actlen;

	if (actlen < 2)
		return (EINVAL);

	v->bulk.payload_size += actlen;
	if (v->bulk.header_size == 0 && !v->bulk.skip_payload) {
		usbd_copy_out(pc, 0, &h, sizeof(h));
		if (h.len < sizeof(h) || h.len > actlen)
			return (EINVAL);

		if (actlen - h.len <= 0)
			return (EINVAL);

		usbd_copy_out(pc, 0, &(v->bulk.header), h.len);
		v->bulk.header_size = h.len;
		offset = h.len;
		datalen = actlen - h.len;
	}
	/* forgot why, so leave this here just in case */
	/* if (actlen < maxFramelen - 4)
	 *     finish = 1;
	 */

	uvc_bulkbuf_sell_buf(&v->bq, pc, offset,
		datalen, actlen, maxFramelen);
	return (0);
}

static int
uvc_drv_fill_buf(struct uvc_drv_video *v, struct usb_page_cache *pc, int i,
	usb_frlength_t size, usb_frlength_t len)
{
	struct uvc_data_payload_header h;
	usb_frlength_t offset = i * size;

	if (len < sizeof(h))
		return EINVAL;

	usbd_copy_out(pc, offset, &h, sizeof(h));

	if (h.len < sizeof(h) || h.len > len)
		return EINVAL;

	if (h.header & UVC_PL_HEADER_BIT_ERR || v->bad_frame) {
		if (h.header & UVC_PL_HEADER_BIT_EOF) {
			v->bad_frame = 0;
			uvc_buf_reset_buf(&v->bq);
		} else {
			v->bad_frame = 1;
		}
		DPRINTF("%d - %x\n", h.len, h.header);
		return EINVAL;
	}

	uvc_buf_sell_buf(&v->bq, pc, offset + h.len,
		len - h.len, h.header & UVC_PL_HEADER_BIT_EOF,
		h.header & UVC_PL_HEADER_BIT_FID);

	return 0;
}

static void
uvc_drv_bulkdata_clear_stall_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uvc_softc *sc = usbd_xfer_softc(xfer);
	struct usb_xfer *xfer_other;

	xfer_other = sc->video->data->xfer[0];
	if (xfer_other == NULL)
		xfer_other = sc->video->data->xfer[1];

	if (usbd_clear_stall_callback(xfer, xfer_other)) {
		usbd_transfer_start(xfer_other);
	}
}

static void
uvc_drv_bulkdata_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uvc_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int dir = UE_GET_DIR(xfer->endpoint->edesc->bEndpointAddress);
	int actlen, sumlen;

	if (xfer->endpoint == NULL || xfer->endpoint->edesc == NULL)
		return;
	dir = UE_GET_DIR(xfer->endpoint->edesc->bEndpointAddress);
	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);
	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (dir == UE_DIR_IN)
			uvc_drv_bulkfill_buf(sc->video, pc,
				usbd_xfer_max_len(xfer), actlen);

	case USB_ST_SETUP:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;
	default:
		DPRINTF("error=%s\n", usbd_errstr(error));
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			usbd_transfer_start(sc->video->data->xfer[2]);
		}

		break;
	}
}

static void
uvc_drv_data_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uvc_softc *sc = usbd_xfer_softc(xfer);
	int actlen, nframes, i, size;
	struct usb_page_cache *pc;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, &nframes);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		for (i = 0; i < nframes; i++) {
			size = usbd_xfer_frame_len(xfer, i);
			uvc_drv_fill_buf(sc->video, pc, i,
				usbd_xfer_max_framelen(xfer), size);
		}

	case USB_ST_SETUP:
tr_setup:
		for (i = 0; i < nframes; i++)
			usbd_xfer_set_frame_len(xfer, i,
				usbd_xfer_max_framelen(xfer));
		usbd_xfer_set_frames(xfer, UVC_N_ISOFRAMES);
		usbd_transfer_submit(xfer);
		break;
	default:
		DPRINTF("error=%s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
uvc_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usb_page_cache *pc;
	int actlen;
	int cmd;
	uint8_t data[16];

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);
	cmd = USB_GET_STATE(xfer);
	switch (cmd) {
	case USB_ST_TRANSFERRED: {
		pc = usbd_xfer_get_frame(xfer, 0);
		if ((actlen <= sizeof(data)) &&
		    (actlen > 0)) {
			usbd_copy_out(pc, 0, data, actlen);
		} else {
			/* ignore it */
			DPRINTF("ignored transfer, %d bytes\n", actlen);
		}
	}

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_framelen(xfer));
		usbd_transfer_submit(xfer);
		break;
	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static const struct usb_config uvc_intr_config[8] = {
	[0] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1, .short_xfer_ok = 1,},
		.bufsize = 0,	/* UHID_BSIZE, */
		.callback = &uvc_intr_callback,
	},
};

static struct usb_config uvc_bulk_config[3] = {
	[0] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1, .short_xfer_ok = 1,},
		.bufsize = UVC_BULK_HS_BUFFER_SIZE,
		.callback = &uvc_drv_bulkdata_callback,
		.timeout = 0,	/* overwritten later */
	},
	[1] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1, .short_xfer_ok = 1,},
		.bufsize = UVC_BULK_HS_BUFFER_SIZE,
		.callback = &uvc_drv_bulkdata_callback,
		.timeout = 0,	/* overwritten later */
	},
	[2] = {
		.type = UE_CONTROL,
		.endpoint = 0,
		.direction = UE_DIR_ANY,
		.flags = {.pipe_bof = 1, .short_xfer_ok = 1,},
		.bufsize = sizeof(struct usb_device_request),
		.callback = &uvc_drv_bulkdata_clear_stall_callback,
		.timeout = 20000,	/* overwritten later */
		.interval = 50,
		.usb_mode = USB_MODE_HOST,
	},
};

static const struct usb_config uvc_config[8] = {
	[0] = {
	.type = UE_ISOCHRONOUS,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = 0,
	.frames = UVC_N_ISOFRAMES,
	.flags = {.short_xfer_ok = 1,},
	.callback = uvc_drv_data_callback,
	},
	[1] = {
	.type = UE_ISOCHRONOUS,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = 0,
	.frames = UVC_N_ISOFRAMES,
	.flags = {.short_xfer_ok = 1,},
	.callback = uvc_drv_data_callback,
	},
	[2] = {
	.type = UE_ISOCHRONOUS,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = 0,
	.frames = UVC_N_ISOFRAMES,
	.flags = {.short_xfer_ok = 1,},
	.callback = uvc_drv_data_callback,
	},
	[3] = {
	.type = UE_ISOCHRONOUS,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = 0,
	.frames = UVC_N_ISOFRAMES,
	.flags = {.short_xfer_ok = 1,},
	.callback = uvc_drv_data_callback,
	},
	[4] = {
	.type = UE_ISOCHRONOUS,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = 0,
	.frames = UVC_N_ISOFRAMES,
	.flags = {.short_xfer_ok = 1,},
	.callback = uvc_drv_data_callback,
	},
	[5] = {
	.type = UE_ISOCHRONOUS,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = 0,
	.frames = UVC_N_ISOFRAMES,
	.flags = {.short_xfer_ok = 1,},
	.callback = uvc_drv_data_callback,
	},
	[6] = {
	.type = UE_ISOCHRONOUS,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = 0,
	.frames = UVC_N_ISOFRAMES,
	.flags = {.short_xfer_ok = 1,},
	.callback = uvc_drv_data_callback,
	},
	[7] = {
	.type = UE_ISOCHRONOUS,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = 0,
	.frames = UVC_N_ISOFRAMES,
	.flags = {.short_xfer_ok = 1,},
	.callback = uvc_drv_data_callback,
	},
};

int
uvc_drv_start_video(struct uvc_drv_video *video)
{
	struct uvc_softc *sc = video->sc;
	struct usb_descriptor *desc;
	struct usb_interface_descriptor *idesc;
	struct usb_endpoint_descriptor *ed;
	struct usb_endpoint_ss_comp_descriptor *essd;
	int ret, num, found, mps, ps;
	int i;
	uint16_t n_setup = 0;
	int alt_reset = 0;

	mtx_lock(&video->mtx);

	if (video->enable) {
		DPRINTF("video is on\n");
		mtx_unlock(&video->mtx);
		return (EBUSY);
	}

	video->bad_frame = 0;

	ret = video_queue_enable(&video->bq);
	if (ret) {
		DPRINTF("buffer queue enable fault\n");
		mtx_unlock(&video->mtx);
		return (ret);
	}

	if (video->data->num_altsetting == 1) {
		n_setup = UVC_N_BULKTRANSFER;
		uvc_drv_get_video_ctrl(video, &video->req, 1, UVC_GET_CUR);
		ret = uvc_drv_set_video_ctrl(video, &video->req, 0);
		if (ret) {
			kthread_suspend(curthread, 300);
			ret = uvc_drv_set_video_ctrl(video, &video->req, 0);
		}
		mps = UGETDW(video->req.dwMaxPayloadSize);
		if (!mps) {
			DPRINTF("Max Packet Size is %u\n", mps);
			ret = EINVAL;
			goto fail;
		}
		video->bulk.max_payload_size = mps;
		video->bulk.last_fid = 3;

		/* setup xfer */
		if (sc->quirks & UVC_QUIRK_LARGER_TRANSFER_BUF) {
			uvc_bulk_config[0].bufsize = 125 * 1024;
			uvc_bulk_config[1].bufsize = 125 * 1024;
		}
		mtx_unlock(&video->mtx);
		ret = usbd_transfer_setup(sc->udev, &video->data->iface_index,
			video->data->xfer, uvc_bulk_config,
			UVC_N_BULKTRANSFER, sc, &video->mtx);

		mtx_lock(&video->mtx);
		if (ret != 0)
			goto fail;
		/* start xfer */
		for (i = 0; i < UVC_N_TRANSFER; i++)
			usbd_transfer_start(video->data->xfer[i]);
		goto done;
	}

	/* choose setting */
	n_setup = UVC_N_TRANSFER;
	/* 1. check */
	num = usbd_iface_get_altsetting_num(sc->udev, video->data->iface);
	DPRINTF("streaming setting num:%u\n", num);
	if (num <= 1) {
		DPRINTF("WARNING: _TO_BE_IMPLEMENT_ BULK ep or bad Intf\n");
		ret = EINVAL;
		goto fail;
	}
	/* 2. loop ep */
	mps = UGETDW(video->req.dwMaxPayloadSize);
	if (!mps) {
		DPRINTF("Max Packet Size is %u\n", mps);
		ret = EINVAL;
		goto fail;
	}
	DPRINTF("payload --- %d\n", mps);
	found = 0;

	desc = (struct usb_descriptor *)(video->data->iface->idesc);
	while ((desc = usb_desc_foreach(sc->udev->cdesc, desc))) {
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			if (desc->bDescriptorSubtype != video->data->iface_num)
				break;

			idesc = (struct usb_interface_descriptor *)desc;
			num = idesc->bAlternateSetting;
			continue;
		}
		if (((desc->bDescriptorType != UDESC_ENDPOINT) &&
		      (desc->bDescriptorType != UDESC_ENDPOINT_SS_COMP)))
			continue;
		ed = (struct usb_endpoint_descriptor *)desc;
		if (desc->bDescriptorType == UDESC_ENDPOINT_SS_COMP) {
			essd = (struct usb_endpoint_ss_comp_descriptor *)desc;
			ps = UGETW(essd->wBytesPerInterval);
		}
		if (desc->bDescriptorType == UDESC_ENDPOINT) {
			ps = UGETW(ed->wMaxPacketSize);
			ps = (ps & 0x07ff) * (1 + ((ps >> 11) & 3));
		}

		if (ps >= mps && !video->htsf) {
			found = 1;
			break;
		}
	}
	if (video->htsf)
		found = 1;
	if (!found) {
		DPRINTF("don't have fit ep\n");
		ret = EINVAL;
		goto fail;
	}
	DPRINTF("max packet size:%u ep size:%u\n", mps, ps);

	/* 3. set */
	if (video->htsf) {
		uvc_drv_get_video_ctrl(video, &video->req, 1, UVC_GET_CUR);
		ret = uvc_drv_set_video_ctrl(video, &video->req, 0);
		DPRINTF("uvc_drv_set_video_ctrl ret:%d\n", ret);
	} else {
		if (!(sc->quirks & UVC_QUIRK_COMMIT_IN_ADVANCE))
			uvc_drv_set_video_ctrl(video, &video->req, 0);
	}
	DPRINTF("streaming interface setting:%u\n", num);

	mtx_unlock(&video->mtx);

	usbd_set_alt_interface_index(sc->udev, video->data->iface_index, num);
	alt_reset = 1;

	/* setup xfer */
	ret = usbd_transfer_setup(sc->udev, &video->data->iface_index,
	    video->data->xfer, uvc_config, UVC_N_TRANSFER, sc, &video->mtx);

	mtx_lock(&video->mtx);
	if (ret != 0)
		goto fail;
	/* start xfer */
	for (i = 0; i < UVC_N_TRANSFER; i++)
		usbd_transfer_start(video->data->xfer[i]);
done:
	video->enable = 1;
	mtx_unlock(&video->mtx);
	return 0;
fail:
	mtx_unlock(&video->mtx);
	if (n_setup != 0)
		usbd_transfer_unsetup(video->data->xfer, n_setup);
	if (alt_reset)
		usbd_set_alt_interface_index(sc->udev, video->data->iface_index,
		    0);
	mtx_lock(&video->mtx);
	video_queue_disable(&video->bq);
	mtx_unlock(&video->mtx);
	return (ret);
}

int
uvc_drv_stop_video(struct uvc_drv_video *video, int close)
{
	uint8_t ep_addr = 0;
	struct usb_endpoint *pep = NULL;

	DPRINTF("uvc_drv_stop_video\n");

	if (video->data->num_altsetting > 1) {
		usbd_transfer_unsetup(video->data->xfer, UVC_N_TRANSFER);
		usbd_set_alt_interface_index(video->sc->udev,
			video->data->iface_index, 0);
	} else {
		if (video->data->xfer[0])
			ep_addr =
			video->data->xfer[0]->endpoint->edesc->bEndpointAddress;
		usbd_transfer_unsetup(video->data->xfer, UVC_N_BULKTRANSFER);
		/*
		 * uvc spec does not tell how to stop a bulk camera
		 * which will leave the indicator on.
		 * windows system send a clear halt request to it
		 * mimic the same, then the light off
		 */
		while (ep_addr &&
		       (pep = usb_endpoint_foreach(video->sc->udev, pep))) {
			if (pep->edesc->bEndpointAddress == ep_addr) {
				usb_reset_ep(video->sc->udev, pep);
			}
		}
	}

	mtx_lock(&video->mtx);
	video_queue_disable(&video->bq);
	video->enable = 0;
	if (close)
		video->htsf = 0;
	mtx_unlock(&video->mtx);

	return 0;
}

int
uvc_drv_set_streampar(struct uvc_drv_video *v, struct v4l2_streamparm *p)
{
	struct uvc_data_request probe;
	struct v4l2_fract timeperframe;
	uint32_t interval;
	int ret;

	DPRINTF("%s\n", __func__);
	timeperframe = p->parm.capture.timeperframe;

	interval = uvc_v4l2_timeperframe_to_interval(&timeperframe);
	DPRINTF("Setting frame interval to %u/%u (%u).\n",
		timeperframe.numerator, timeperframe.denominator, interval);

	probe = v->req;
	USETDW(probe.dwFrameInterval,
		uvc_drv_try_frame_interval(v->cur_frm, interval));

	ret = uvc_drv_probe_video(v, &probe);
	if (ret != 0) {
		return ret;
	}
	v->req = probe;
	uvc_v4l2_interval_to_timeperframe(UGETDW(probe.dwFrameInterval),
	    &timeperframe);
	p->parm.capture.timeperframe = timeperframe;
	return 0;
}

static void
uvc_init_intrxfer(struct uvc_drv_video *video)
{
	struct uvc_softc *sc = video->sc;
	struct usb_descriptor *desc;
	struct usb_interface_descriptor *idesc;
	struct usb_endpoint_descriptor *ed;
	int hit_intr = 0, ret;
	uint8_t iface_index;

	return; /* not used now */

	desc = (struct usb_descriptor *)NULL;
	while ((desc = usb_desc_foreach(sc->udev->cdesc, desc))) {
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			idesc = (struct usb_interface_descriptor *)desc;
			iface_index = idesc->bInterfaceNumber;
			continue;
		}
		if (desc->bDescriptorType != UDESC_ENDPOINT)
			continue;
		ed = (struct usb_endpoint_descriptor *)desc;
		if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			hit_intr = 1;
			break;
		}
	}

	if (!hit_intr) {
		return;
	}

	ret = usbd_transfer_setup(sc->udev, &iface_index, video->intr_xfer,
			uvc_intr_config, UVCINTR_N_TRANSFER, sc, &video->mtx);
	if (ret)
		return;

	mtx_lock(&video->mtx);
	usbd_transfer_start(video->intr_xfer[0]);
	mtx_unlock(&video->mtx);
}

static struct uvc_drv_video *
uvc_drv_init_video(struct uvc_softc *sc, struct uvc_drv_ctrl *ctrl,
	struct uvc_drv_data *data)
{
	struct uvc_drv_video *v;
	struct uvc_data_format *fmt;
	struct uvc_data_frame *frm;
	struct uvc_data_request *req;
	int ret, i;

	v = malloc(sizeof(*v), M_UVC, M_ZERO | M_WAITOK);
	if (v == NULL)
		return v;

	mtx_init(&v->mtx, device_get_nameunit(sc->dev),
		NULL, MTX_DEF | MTX_RECURSE);
	v->sc = sc;
	v->type = sc->type;
	v->unit = device_get_unit(sc->dev);
	v->ctrl = ctrl;
	v->data = data;
	v->last_fid = -1;
	atomic_store_64(&v->users, 0);
	atomic_store_64(&v->pri, 0);
	atomic_store_64(&v->detaching, 0);
	atomic_store_64(&v->teardown_done, 0);

	if (data->num_altsetting > 1)
		usbd_set_alt_interface_index(sc->udev, data->iface_index, 0);

	/* fill video data request */
	req = &v->req;
	mtx_lock(&v->mtx);
	ret = uvc_drv_get_video_ctrl(v, req, 1, UVC_GET_DEF);
	if (ret) {
		DPRINTF("video UVC_GET_DEF fault %d\n", ret);
		ret = uvc_drv_get_video_ctrl(v, req, 1, UVC_GET_CUR);
		mtx_unlock(&v->mtx);
		if (ret) {
			DPRINTF("video UVC_GET_CUR fault %d\n", ret);
			goto done;
		}
		for (i = v->data->nfmt; i > 0; i--) {
			fmt = v->data->fmt + i - 1;
			if (fmt->index == req->bFormatIndex) {
				break;
			}
		}

		if (fmt->nfrm == 0) {
			DPRINTF("No frame desc found for default format.\n");
			goto done;
		}

		DPRINTF("format:%u-%s\n", fmt->index, fmt->name);

		for (i = fmt->nfrm; i > 0; i--) {
			frm = fmt->frm + i - 1;
			if (frm->index == req->bFrameIndex) {
				break;
			}
		}

		DPRINTF("width:%u height:%u\n", frm->width, frm->height);
		req->bFormatIndex = fmt->index;
		req->bFrameIndex = frm->index;

		ret = uvc_drv_set_video(v, req, fmt, frm);
		if (ret) {
			DPRINTF("uvc drv set video fault %d\n", ret);
			goto done;
		}

	} else {
		/*do not care whether it successes or not*/
		uvc_drv_set_video_ctrl(v, req, 1);
		mtx_unlock(&v->mtx);
	}

	/*
	 * Update this here because C922 Pro from Richard
	 * 1. UVC_GET_DEF don't fill max payload size
	 * 2. UVC_GET_DEF inherbs config set before
	 */
	mtx_lock(&v->mtx);
	ret = uvc_drv_get_video_ctrl(v, req, 1, UVC_GET_CUR);
	mtx_unlock(&v->mtx);
	if (ret) {
		DPRINTF("uvc drv UVC_GET_CUR fault %d\n", ret);
		goto done;
	}

	uvc_drv_show_video_ctrl(req);

	if (uvc_drv_init_cur_fmt_frm(v, req))
		goto done;

	video_queue_init(&v->bq, v, V4L2_BUF_TYPE_VIDEO_CAPTURE,
	    V4L2_MEMORY_MMAP, VIDEO_MAX_BUFFERS);

	ret = uvc_v4l2_reg(v);
	if (ret) {
		DPRINTF("uvc v4l2 reg error\n");
		goto done;
	}
	uvc_init_intrxfer(v);

	return v;
done:
	free(v, M_UVC);
	return NULL;
}

static void
uvc_drv_destroy_video(struct uvc_drv_video *v)
{
	if (v) {
		/* thread stop */
		if (v->data) {
			usbd_transfer_unsetup(v->data->xfer,
			    v->data->num_altsetting > 1 ?
			    UVC_N_TRANSFER : UVC_N_BULKTRANSFER);
		}

		video_queue_disable(&v->bq);
		video_queue_free_bufs(&v->bq);
	}
}

void
uvc_drv_free_video(struct uvc_drv_video *v)
{
	if (v == NULL)
		return;

	uvc_drv_destroy_data(v->data);
	uvc_drv_destroy_ctrl(v->ctrl);
	mtx_destroy(&v->mtx);
	free(v, M_UVC);
}

static void
uvc_drv_show_interval(struct uvc_data_interval *itv)
{
	DPRINTF("%u	\n", itv->val);
}

static void
uvc_drv_show_frame(struct uvc_data_frame *frm)
{
	struct uvc_data_interval *itv;
	int i;

	DPRINTF("interval num:%u\n", frm->interval_type);
	DPRINTF("frm index:%u width:%u height:%u\n", frm->index,
		frm->width, frm->height);

	for (i = 0; i < frm->interval_type; i++) {
		itv = frm->interval + i;
		uvc_drv_show_interval(itv);
	}
	DPRINTF("\n");
}

static void
uvc_drv_show_format(struct uvc_data_format *fmt)
{
	struct uvc_data_frame *frm;
	int i;

	DPRINTF("frame num:%lu flags:0x%lx\n", fmt->nfrm, fmt->flags);
	DPRINTF("format index:%u name:%s fcc:%u bpp:%u colorspace:%u\n",
		fmt->index, fmt->name, fmt->fcc, fmt->bpp, fmt->colorspace);

	for (i = 0; i < fmt->nfrm; i++) {
		frm = fmt->frm + i;
		uvc_drv_show_frame(frm);
	}
}

static void
uvc_drv_show_data(struct uvc_drv_data *data)
{
	struct uvc_data_format *fmt;
	int i;

	DPRINTF("streaming interface index:%u num:%u\n"
		"format num:%u frame num:%d interval:%d\n",
		data->iface_index, data->iface_num,
		data->nfmt, data->nfrm, data->nitv);

	for (i = 0; i < data->nfmt; i++) {
		fmt = data->fmt + i;
		uvc_drv_show_format(fmt);
	}
}

static int
uvc_drv_init_data_fmt(struct uvc_softc *sc, struct uvc_drv_data *data)
{
	struct usb_descriptor *desc;
	struct uvc_vs_frame_desc *fdesc;
	struct uvc_vs_frame_based_desc *fbdesc;
	uint32_t size;
	uint16_t nfmt, nfrm, nitv;

	nfmt = nfrm = nitv = 0;
	desc = NULL;
	while ((desc = usbd_find_descriptor(sc->udev, desc, data->iface_index,
		UDESC_CS_INTERFACE, 0xFF, 0, 0)) != NULL) {
		switch (desc->bDescriptorSubtype) {
		case UVC_VS_INPUT_HEADER:
			sc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			break;
		case UVC_VS_OUTPUT_HEADER:
			sc->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
			break;
		case UVC_VS_COLORFORMAT:
		case UVC_VS_STILL_IMAGE_FRAME:
			break;
		case UVC_VS_FORMAT_MJPEG:
		case UVC_VS_FORMAT_FRAME_BASED:
			nfmt++;
			break;
		case UVC_VS_FORMAT_UNCOMPRESSED:
			if (sc->quirks & UVC_QUIRK_DROP_UNCOMPRESSED_FORMAT)
				break;
			nfmt++;
			break;
		case UVC_VS_FRAME_UNCOMPRESSED:
			if (sc->quirks & UVC_QUIRK_DROP_UNCOMPRESSED_FORMAT)
				break;
		case UVC_VS_FRAME_MJPEG:
			fdesc = (struct uvc_vs_frame_desc *)desc;
			nfrm++;
			nitv += (fdesc->bFrameIntervalType > 0) ?
				fdesc->bFrameIntervalType : 3;
			break;
		case UVC_VS_FRAME_FRAME_BASED:
			fbdesc = (struct uvc_vs_frame_based_desc *)desc;
			nfrm++;
			nitv += (fbdesc->bFrameIntervalType > 0) ?
				fbdesc->bFrameIntervalType : 3;
			break;
		/* not support */
		default:
			DPRINTF("ignoring unsupported VS descriptor subtype %u\n",
			    desc->bDescriptorSubtype);
			break;

		}
	}

	if (nfmt == 0)
		return EINVAL;

	size =  nfmt * sizeof(struct uvc_data_format) +
		nfrm * sizeof(struct uvc_data_frame) +
		nitv * sizeof(struct uvc_data_interval);
	DPRINTF("format:%u frame:%u interval:%u size:%d\n",
		nfmt, nfrm, nitv, size);
	data->fmt = malloc(size, M_UVC, M_ZERO | M_WAITOK);
	if (!data->fmt) {
		DPRINTF("Memory Leak.\n");
		return ENOMEM;
	}

	data->nfmt = nfmt;
	data->nfrm = nfrm;
	data->nitv = nitv;
	return 0;
}

static uint8_t
uvc_drv_get_colorspace(uint8_t pri)
{
	switch (pri) {
	case 1:
		return (V4L2_COLORSPACE_SRGB);
	case 2:
		return (V4L2_COLORSPACE_470_SYSTEM_M);
	case 3:
		return (V4L2_COLORSPACE_470_SYSTEM_BG);
	case 4:
		return (V4L2_COLORSPACE_SMPTE170M);
	case 5:
		return (V4L2_COLORSPACE_SMPTE240M);
	default:
		return (0);
	}
}

static struct uvc_drv_format_desc *
uvc_drv_format_by_guid(const uint8_t guid[16])
{
	unsigned int len = nitems(uvc_fmts);
	unsigned int i;

	for (i = 0; i < len; ++i) {
		if (memcmp(guid, uvc_fmts[i].guid, 16) == 0)
			return &uvc_fmts[i];
	}

	return NULL;
}

static int
uvc_drv_parse_data(struct uvc_softc *sc, struct uvc_drv_data *data)
{
	struct uvc_data_format *fmt;
	struct uvc_data_frame *frm;
	struct uvc_data_interval *itv;

	struct usb_descriptor *desc;
	struct uvc_drv_format_desc *fmtdes;
	struct uvc_vs_color_desc *cld;
	struct uvc_vs_uncompressed_format_desc *raw;
	struct uvc_vs_frame_based_format_desc *fbraw;
	struct uvc_vs_frame_desc *frmd;
	struct uvc_vs_frame_based_desc *fbrmd;
	struct usb_endpoint_descriptor *ed;
	struct usb_endpoint_ss_comp_descriptor *essd;
	uint32_t interval;
	int ret, i, ps;

	if (data->iface->idesc->bInterfaceSubClass != UISUBCLASS_STREAMING) {
		return EINVAL;
	}

	if (data->iface->idesc->bAlternateSetting == 0)
		data->num_altsetting = 1;

	desc = (struct usb_descriptor *)(data->iface->idesc);
	while ((desc = usb_desc_foreach(sc->udev->cdesc, desc))) {
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			if (desc->bDescriptorSubtype != data->iface_num)
				break;
			data->num_altsetting++;
		}
		ed = (struct usb_endpoint_descriptor *)desc;
		if (desc->bDescriptorType == UDESC_ENDPOINT_SS_COMP) {
			essd = (struct usb_endpoint_ss_comp_descriptor *)desc;
			ps = UGETW(essd->wBytesPerInterval);
			if (ps > data->maxpsize) {
				data->maxpsize = ps;
			}
		}
		if (desc->bDescriptorType == UDESC_ENDPOINT) {
			ps = UGETW(ed->wMaxPacketSize);
			ps = (ps & 0x07ff) * (1 + ((ps >> 11) & 3));
			if (ps > data->maxpsize) {
				data->maxpsize = ps;
			}
		}

	}

	if (data->num_altsetting > 1)
		/* cs interface descriptors in altsetting zero */
		usbd_set_alt_interface_index(sc->udev, data->iface_index, 0);

	ret = uvc_drv_init_data_fmt(sc, data);
	if (ret)
		return ret;

	frm = (struct uvc_data_frame *)&data->fmt[data->nfmt];
	itv = (struct uvc_data_interval *)&frm[data->nfrm];
	fmt = data->fmt - 1;

	desc = NULL;
	while ((desc = usbd_find_descriptor(sc->udev, desc, data->iface_index,
		UDESC_CS_INTERFACE, 0xFF, 0, 0)) != NULL) {
		switch (desc->bDescriptorSubtype) {
		case UVC_VS_COLORFORMAT:
			cld = (struct uvc_vs_color_desc *)desc;
			fmt->colorspace
				= uvc_drv_get_colorspace(cld->bColorPris);
			break;
		case UVC_VS_FORMAT_UNCOMPRESSED:
			if (sc->quirks & UVC_QUIRK_DROP_UNCOMPRESSED_FORMAT)
				break;
		case UVC_VS_FORMAT_MJPEG:
			fmt++;
			/* MJPEG/RAW is same */
			raw = (struct uvc_vs_uncompressed_format_desc *)desc;
			fmt->frm = frm;
			fmt->index = raw->bFormatIndex;

			if (desc->bDescriptorSubtype ==
				UVC_VS_FORMAT_MJPEG) {
				fmt->fcc = V4L2_PIX_FMT_MJPEG;
				fmt->bpp = 0;
				fmt->flags = UVC_FMT_FLAG_COMPRESSED;
				strlcpy(fmt->name, "MJPEG", sizeof(fmt->name));
			} else {
				fmtdes = uvc_drv_format_by_guid(raw->bGuidFmt);
				if (fmtdes) {
					strlcpy(fmt->name, fmtdes->name,
						sizeof(fmt->name));
					fmt->fcc = fmtdes->fcc;
				} else {
					strlcpy(fmt->name, "RAW",
						sizeof(fmt->name));
					fmt->fcc = 0;
				}
				fmt->bpp = raw->bBpp;
			}
			break;
		case UVC_VS_FORMAT_FRAME_BASED:
			fmt++;
			fbraw = (struct uvc_vs_frame_based_format_desc *)desc;
			fmt->frm = frm;
			fmt->index = fbraw->bFormatIndex;
			fmtdes = uvc_drv_format_by_guid(fbraw->bGuidFmt);
			if (fmtdes) {
				strlcpy(fmt->name, fmtdes->name,
					sizeof(fmt->name));
				fmt->fcc = fmtdes->fcc;
			} else {
				strlcpy(fmt->name,
					(const char *)(fbraw->bGuidFmt),
					sizeof(fmt->name));
				fmt->fcc = 0;
			}
			fmt->bpp = fbraw->bBitsPerpixel;
			if (fbraw->bVariableSize)
				fmt->flags = UVC_FMT_FLAG_COMPRESSED;
			break;
		case UVC_VS_FRAME_UNCOMPRESSED:
			if (sc->quirks & UVC_QUIRK_DROP_UNCOMPRESSED_FORMAT)
				break;
		case UVC_VS_FRAME_MJPEG:
			frmd = (struct uvc_vs_frame_desc *)desc;
			frm->index = frmd->bFrameIndex;
			frm->width = UGETW(frmd->wWidth);
			frm->height = UGETW(frmd->wHeight);
			frm->cap = frmd->bCapabilities;
			frm->min_bit_rate = UGETDW(frmd->dMinBitRate);
			frm->max_bit_rate = UGETDW(frmd->dMaxBitRate);
			frm->default_interval
				= UGETDW(frmd->dDefaultFrameInterval);
			frm->max_buffer_size
				= UGETDW(frmd->dMaxFrameBufferSize);
			if (!(fmt->flags & UVC_FMT_FLAG_COMPRESSED))
				frm->max_buffer_size = fmt->bpp *
					frm->width * frm->height / 8;
			frm->interval_type = frmd->bFrameIntervalType;
			frm->interval = itv;
			for (i = 0; i < frm->interval_type; i++) {
				interval = UGETDW(frmd->dFrameInterval[i]);
				itv->val = interval ? interval : 1;
				itv++;
			}
			if (frm->interval_type == 0)
				itv += 3;
			fmt->nfrm++;
			frm++;
			break;
		case UVC_VS_FRAME_FRAME_BASED:
			fbrmd = (struct uvc_vs_frame_based_desc *)desc;
			frm->index = fbrmd->bFrameIndex;
			frm->width = UGETW(fbrmd->wWidth);
			frm->height = UGETW(fbrmd->wHeight);
			frm->cap = fbrmd->bCapabilities;
			frm->min_bit_rate = UGETDW(fbrmd->dMinBitRate);
			frm->max_bit_rate = UGETDW(fbrmd->dMaxBitRate);
			frm->default_interval
				= UGETDW(fbrmd->dDefaultFrameInterval);
			/*
			 * Frame-based formats may report a transport payload size
			 * rather than a decoded frame size. Leave this field unset
			 * until the backend grows a format-specific calculation.
			 */
			frm->max_buffer_size = 0;

			if (!(fmt->flags & UVC_FMT_FLAG_COMPRESSED))
				frm->max_buffer_size = fmt->bpp *
					frm->width * frm->height / 8;
			frm->interval_type = fbrmd->bFrameIntervalType;
			frm->interval = itv;
			for (i = 0; i < frm->interval_type; i++) {
				interval = UGETDW(fbrmd->dFrameInterval[i]);
				itv->val = interval ? interval : 1;
				itv++;
			}
			if (frm->interval_type == 0)
				itv += 3;
			fmt->nfrm++;
			frm++;
			break;
		default:
			break;
		}
	}

	return 0;
}

static void
uvc_drv_destroy_data(struct uvc_drv_data *data)
{
	if (data) {
		if (data->fmt) {
			free(data->fmt, M_UVC);
		}
		free(data, M_UVC);
	}
}

static struct uvc_drv_data*
uvc_drv_init_data(struct usb_interface *iface, uint8_t iface_index,
	uint8_t iface_num)
{
	struct uvc_drv_data *pd;

	pd = malloc(sizeof(*pd), M_UVC, M_ZERO | M_WAITOK);
	if (pd) {
		pd->iface = iface;
		pd->iface_index = iface_index;
		pd->iface_num = iface_num;
	}
	return pd;
}

static void uvc_free_topo_node(struct uvc_topo_node *topo_node)
{
	if (topo_node->src_ids != NULL) {
		free(topo_node->src_ids , M_UVC);
	}

	if (topo_node->controls_mask != NULL) {
		free(topo_node->controls_mask , M_UVC);
	}

	if (topo_node->node_info != NULL) {
		free(topo_node->node_info , M_UVC);
	}

	if (topo_node->controls != NULL) {
		free(topo_node->controls, M_UVC);
	}

	free(topo_node, M_UVC);
}

static struct uvc_topo_node *
uvc_alloc_topo_node(uint16_t node_info_size,
					uint16_t ctrls_mask_size, /* size in bytes */
					uint16_t src_ids_num)
{
	struct uvc_topo_node *topo_node = NULL;
	uint8_t *node_info = NULL;
	uint8_t *ctrls_mask = NULL;
	uint8_t *src_ids = NULL;

	topo_node = malloc(sizeof(struct uvc_topo_node), M_UVC, M_ZERO | M_WAITOK);
	if (topo_node == NULL) {
		goto mem_fail;
	}

	if (node_info_size != 0) {
		node_info = malloc(node_info_size, M_UVC, M_ZERO | M_WAITOK);
		if (node_info == NULL) {
			goto mem_fail;
		}
	}

	if (ctrls_mask_size != 0) {
		ctrls_mask = malloc(ctrls_mask_size, M_UVC, M_ZERO | M_WAITOK);
		if (ctrls_mask == NULL) {
			goto mem_fail;
		}
	}

	if (src_ids_num != 0) {
		src_ids = malloc(src_ids_num, M_UVC, M_ZERO | M_WAITOK);
		if (src_ids == NULL) {
			goto mem_fail;
		}
	}

	topo_node->node_info = node_info;

	topo_node->controls_mask = ctrls_mask;
	topo_node->controls_mask_len = ctrls_mask_size;

	topo_node->src_ids = src_ids;
	topo_node->src_ids_num = src_ids_num;

	return topo_node;

mem_fail:
	printf("%s:%d Lack of mem\n", __func__, __LINE__);

	if (src_ids != NULL) {
		free(src_ids , M_UVC);
	}

	if (ctrls_mask != NULL) {
		free(ctrls_mask, M_UVC);
	}

	if (node_info != NULL) {
		free(node_info, M_UVC);
	}

	if (topo_node != NULL) {
		free(topo_node, M_UVC);
	}

	return NULL;
}


static int
uvc_drv_parse_standard_ctrl(struct uvc_softc *sc,
	struct usb_descriptor *desc, struct uvc_drv_ctrl *ctrl)
{
	struct uvc_vc_header_desc *hdesc;
	struct uvc_vc_input_terminal_desc *it_desc;
	struct uvc_vc_output_terminal_desc *ot_desc;
	struct uvc_vc_selector_unit_desc *su_desc;
	struct uvc_vc_processing_unit_desc *pu_desc;
	struct uvc_vc_extension_unit_desc *xu_desc;
	struct uvc_topo_node *topo_node;
	struct usb_interface *iface;
	int i, vsnum, ret = 0;

	uint16_t node_info_size = 0;
	uint16_t ctrls_mask_size = 0;
	uint16_t src_ids_num = 0;

	uint8_t idx;

	if (desc->bDescriptorType != UDESC_CS_INTERFACE)
		return 0;

	switch (desc->bDescriptorSubtype) {
	case UVC_VC_HEADER: {

		hdesc = (struct uvc_vc_header_desc *)desc;
		vsnum = hdesc->bLength >= 12 ? hdesc->bIfaceNums : 0;

		if (hdesc->bLength < 12 + vsnum) {
			DPRINTF("WARNING: to_be_implement or bad interface.\n");
			return EINVAL;
		}

		ctrl->revision = UGETW(hdesc->wRevision);

		ctrl->clock_freq = UGETDW(hdesc->dClockFreq);

		for (i = 0; i < vsnum; i++) {
			idx = usbd_iface_num_to_index(sc->udev,
					hdesc->bIfaceList[i]);
			if (idx == (uint8_t)(-1)) {
				DPRINTF("bad vs interface\n");
				return EINVAL;
			}

			iface = usbd_get_iface(sc->udev, idx);
			if (!iface) {
				DPRINTF("bad vs interface\n");
				return EINVAL;
			}
			if (sc->data != NULL) {
				DPRINTF("ignoring extra streaming interface %u\n",
				    hdesc->bIfaceList[i]);
				continue;
			}

			sc->data = uvc_drv_init_data(iface, idx,
				iface->idesc->bInterfaceNumber);
			if (sc->data == NULL)
				return ENOMEM;
			ret = uvc_drv_parse_data(sc, sc->data);
			if (ret)
				return ret;
			if (uvc_debug >= 5)
				uvc_drv_show_data(sc->data);
		}
		break;
	}
	case UVC_VC_SELECTOR_UNIT: {

		su_desc = (struct uvc_vc_selector_unit_desc *)desc;
		if (su_desc->bLength < sizeof(*su_desc)) {
			DPRINTF("bad interface. UVC_VC_INPUT_TERMINAL\n");
			return EINVAL;
		}

		ctrl->sid = *((char *)desc + 3);
		DPRINTF("selector unit id:%d\n", ctrl->sid);

		node_info_size = 0;
		ctrls_mask_size = 0;
		src_ids_num = su_desc->bLength >= 5 ? su_desc->bNrInPins : 0;

		if (su_desc->bLength < 5 || su_desc->bLength < src_ids_num + 6) {
			DPRINTF("bad interface. UVC_VC_SELECTOR_UNIT\n");
			return EINVAL;
		}

		topo_node = uvc_alloc_topo_node(node_info_size,
									ctrls_mask_size, src_ids_num);
		if (!topo_node)
			return ENOMEM;

		topo_node->node_id = su_desc->bUnitID;
		topo_node->node_type = su_desc->bDescriptorSubType;

		memcpy(topo_node->src_ids,
		    (uint8_t *)su_desc + 5, src_ids_num);

		if (su_desc->iSelector != 0) {
			DPRINTF("WARNING: need to get usb string by id\n");
		} else {
			snprintf(topo_node->node_name, 64, "Selector %u", topo_node->node_id);
		}

		STAILQ_INSERT_TAIL(&ctrl->topo_nodes, topo_node, link);
		break;
	}
	case UVC_VC_INPUT_TERMINAL: {
		uint16_t terminal_type;
		struct uvc_ct_node_info *node_info;

		it_desc = (struct uvc_vc_input_terminal_desc *)desc;
		terminal_type = UGETW(it_desc->wTerminalType);

		if (it_desc->bLength < sizeof(*it_desc)) {
			DPRINTF("bad interface. UVC_VC_INPUT_TERMINAL\n");
			return EINVAL;
		}

		if (it_desc->wTerminalType[1] == 0) {
			DPRINTF("bad interface. type is unit\n");
			return EINVAL;
		}

		if (terminal_type == UVC_ITT_CAMERA) {
			node_info_size = sizeof(struct uvc_ct_node_info);
			memcpy((void *)&ctrls_mask_size, (uint8_t *)it_desc + 14, 1);
		} else {
			node_info_size = 0;
			ctrls_mask_size = 0;
		}
		src_ids_num = 0;

		if (it_desc->bLength < sizeof(*it_desc) ||
		    (terminal_type == UVC_ITT_CAMERA &&
		    it_desc->bLength < 15 + ctrls_mask_size)) {
			DPRINTF("%s:%d bad interface. \n", __func__, __LINE__);
			return EINVAL;
		}

		topo_node = uvc_alloc_topo_node(node_info_size,
									ctrls_mask_size, src_ids_num);
		if (!topo_node)
			return ENOMEM;

		topo_node->node_id = it_desc->bTerminalID;
		topo_node->node_type = terminal_type | UVC_TERM_FLAG_INPUT;

		if (terminal_type == UVC_ITT_CAMERA) {
			node_info = (struct uvc_ct_node_info *)topo_node->node_info;
			memcpy((void *)&node_info->wObjectiveFocalLengthMin,
				(uint8_t *)it_desc + 8, 2);
			memcpy((void *)&node_info->wObjectiveFocalLengthMax,
				(uint8_t *)it_desc + 10, 2);
			memcpy((void *)&node_info->wOcularFocalLength,
				(uint8_t *)it_desc + 12, 2);

			node_info->bControlSize = ctrls_mask_size;
			node_info->bmControls = topo_node->controls_mask;
			memcpy((void *)node_info->bmControls,
				(uint8_t *)it_desc + 15, ctrls_mask_size);
		}

		if (it_desc->bITerminal != 0) {
			DPRINTF("WARNING: need to get usb string by id\n");
		} else if (terminal_type == UVC_ITT_CAMERA) {
			snprintf(topo_node->node_name, 64, "Camera %u", topo_node->node_id);
		} else
			snprintf(topo_node->node_name, 64, "Input %u", topo_node->node_id);

		STAILQ_INSERT_TAIL(&ctrl->topo_nodes, topo_node, link);
		break;
	}
	case UVC_VC_OUTPUT_TERMINAL: {

		ot_desc = (struct uvc_vc_output_terminal_desc *)desc;
		if (ot_desc->bLength < sizeof(*ot_desc)) {
			DPRINTF("bad interface. UVC_VC_OUTPUT_TERMINAL\n");
			return EINVAL;
		}

		if (ot_desc->wTerminalType[1] == 0) {
			DPRINTF("bad interface. type is unit\n");
			return EINVAL;
		}

		node_info_size = 0;
		ctrls_mask_size = 0;
		src_ids_num = 1;

		topo_node = uvc_alloc_topo_node(node_info_size, ctrls_mask_size, src_ids_num);
		if (!topo_node)
			return ENOMEM;

		topo_node->node_id = ot_desc->bTerminalID;
		topo_node->node_type = UGETW(ot_desc->wTerminalType) |
		    UVC_TERM_FLAG_OUTPUT;

		memcpy((void *)topo_node->src_ids,
		    (uint8_t *)ot_desc + 7, 1);

		if (ot_desc->bITerminal != 0) {
			DPRINTF("WARNING: need to get usb string by id\n");
		} else
			snprintf(topo_node->node_name, 64, "Output %u", topo_node->node_id);

		STAILQ_INSERT_TAIL(&ctrl->topo_nodes, topo_node, link);
		break;
	}
	case UVC_VC_PROCESSING_UNIT: {
		uint8_t p = ctrl->revision >= 0x0110 ? 10 : 9;

		pu_desc = (struct uvc_vc_processing_unit_desc *)desc;

		ctrls_mask_size = pu_desc->bLength >= 8 ? pu_desc->bControlSize : 0;
		if (pu_desc->bLength < ctrls_mask_size + p) {
			DPRINTF("bad interface. UVC_VC_PROCESSING_UNIT\n");
			return EINVAL;
		}

		node_info_size = sizeof(struct uvc_pu_node_info);
		src_ids_num = 1;

		topo_node = uvc_alloc_topo_node(node_info_size,
									ctrls_mask_size, src_ids_num);
		if (!topo_node)
			return ENOMEM;

		topo_node->node_id = pu_desc->bUnitID;
		topo_node->node_type = pu_desc->bDescriptorSubType;

		struct uvc_pu_node_info* node_info = (struct uvc_pu_node_info*)topo_node->node_info;
		node_info->wMaxMultiplier = UGETW(pu_desc->wMaxMultiplier);

		node_info->bControlSize = ctrls_mask_size;
		node_info->bmControls = topo_node->controls_mask;
		memcpy((void *)node_info->bmControls,
		       (uint8_t *)pu_desc + 8, ctrls_mask_size);

		memcpy(topo_node->src_ids, (uint8_t *)pu_desc + 4, src_ids_num);

		if (ctrl->revision >= 0x0110)
			node_info->bmVideoStandards =
				pu_desc->bmVideoStandards;

		if (pu_desc->iProcessing != 0) {
			DPRINTF("WARNING: need to get usb string by id\n");
		} else
			snprintf(topo_node->node_name, 64, "Processing %u",
			    topo_node->node_id);

		STAILQ_INSERT_TAIL(&ctrl->topo_nodes, topo_node, link);
		break;
	}
	case UVC_VC_EXTENSION_UNIT: {
		xu_desc = (struct uvc_vc_extension_unit_desc *)desc;

		node_info_size = sizeof(struct uvc_xu_node_info);
		src_ids_num = xu_desc->bLength >= 22 ? xu_desc->bNrInPins : 0;
		ctrls_mask_size = xu_desc->bLength >= 24 ?
			*(&xu_desc->bControlSize + src_ids_num) : 0;

		if (xu_desc->bLength < ctrls_mask_size + src_ids_num + 24) {
			DPRINTF("bad interface. UVC_VC_EXTENSION_UNIT\n");
			return (EINVAL);
		}

		DPRINTF("extension unit id:%d\n", *((char *)desc + 3));
		if (!memcmp((char *)desc + 4, vc_ext_h264ctrl,
			sizeof(vc_ext_h264ctrl))) {
			DPRINTF("this is h264 extension.\n");
			ctrl->h264id = *((char *)desc + 3);
		}

		topo_node = uvc_alloc_topo_node(node_info_size,
									ctrls_mask_size, src_ids_num);
		if (!topo_node)
			return (ENOMEM);

		topo_node->node_id = xu_desc->bUnitID;
		topo_node->node_type = xu_desc->bDescriptorSubType;

		struct uvc_xu_node_info *node_info;

		node_info = (struct uvc_xu_node_info *)topo_node->node_info;

		memcpy((void *)&node_info->guidExtensionCode,
		       (uint8_t *)xu_desc + 4, 16);
		node_info->bNumControls = xu_desc->bNumControls;

		node_info->bControlSize = ctrls_mask_size;
		node_info->bmControls = topo_node->controls_mask;
		memcpy((void *)node_info->bmControls,
		       (uint8_t *)xu_desc + 23 + src_ids_num, ctrls_mask_size);

		memcpy((void *)topo_node->src_ids,
		       (uint8_t *)xu_desc + 22, src_ids_num);

		if (*(&xu_desc->iExtension + ctrls_mask_size + src_ids_num) != 0)
			DPRINTF("WARNING: need to get usb string by id\n");
		else
			snprintf(topo_node->node_name, 64, "Extension %u",
			    topo_node->node_id);

		STAILQ_INSERT_TAIL(&ctrl->topo_nodes, topo_node, link);
		break;
	}
	}

	return (ret);
}

static void
uvc_drv_show_ctrl(struct uvc_drv_ctrl *ctrl)
{
	KASSERT(ctrl != NULL, ("Input Error"));

	DPRINTF("ctrl interface idx:%u num:%u\n", ctrl->iface_index,
	    ctrl->iface_num);
	DPRINTF("revision:0x%x clock_freq:%u\n", ctrl->revision,
	    ctrl->clock_freq);
	DPRINTF("select unit id:%d\n", ctrl->sid);
}

static int
uvc_drv_parse_vendor_ctrl(struct uvc_softc *sc, struct usb_descriptor *desc,
	struct uvc_drv_ctrl *ctrl)
{
	switch (UGETW(sc->udev->ddesc.idVendor)) {
	case USB_VENDOR_ID_LOGITECH:
		if (desc->bDescriptorType != 0x41 ||
			desc->bDescriptorSubtype != 0x01)
			break;
		DPRINTF("WARNING: __TO_BE_IMPLEMENT__\n");
		return 1;
	default:
		break;
	}
	return 0;
}

static int
uvc_drv_parse_ctrl(struct uvc_softc *sc, struct uvc_drv_ctrl *ctrl)
{
	struct usb_descriptor *desc = NULL;
	int ret;

	while ((desc = usbd_find_descriptor(sc->udev, desc,
		ctrl->iface_index, 0, 0, 0, 0)) != NULL) {
		if (uvc_drv_parse_vendor_ctrl(sc, desc, ctrl))
			continue;
		ret = uvc_drv_parse_standard_ctrl(sc, desc, ctrl);
		if (ret) {
			return ret;
		}
	}

	if (uvc_debug >= 5)
		uvc_drv_show_ctrl(ctrl);
	return 0;
}

static struct uvc_drv_ctrl *
uvc_drv_init_ctrl(struct usb_interface *iface, uint8_t iface_index,
	uint8_t iface_num)
{
	struct uvc_drv_ctrl *pc;

	pc = malloc(sizeof(*pc), M_UVC, M_ZERO | M_WAITOK);
	if (pc) {
		pc->iface = iface;
		pc->iface_index = iface_index;
		pc->iface_num = iface_num;
		mtx_init(&pc->mtx, "uvc ctrl lock", NULL, MTX_DEF);
		STAILQ_INIT(&pc->topo_nodes);
	}
	return pc;
}

static void
uvc_drv_destroy_ctrl(struct uvc_drv_ctrl *c)
{
	struct uvc_topo_node *topo_node, *tmp;
	uint8_t i = 0;

	if (c) {
		STAILQ_FOREACH_SAFE(topo_node, &c->topo_nodes, link, tmp) {
			for (i = 0; i < topo_node->controls_num; ++i) {
				struct uvc_ctrl_state *ctrl = &topo_node->controls[i];

				if (!UVC_CTRL_STATE_IS_READY(ctrl))
					continue;

				uvc_ctrl_destroy_mappings(ctrl);
				free(ctrl->shadow_data, M_UVC);
			}

			uvc_free_topo_node(topo_node);
		}
		free(c, M_UVC);
	}
}

static int
uvc_drv_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return ENXIO;

	if ((uaa->info.bInterfaceClass == UICLASS_VIDEO) &&
			(uaa->info.bInterfaceSubClass == UISUBCLASS_CTRL))
		return BUS_PROBE_SPECIFIC;

	/* Streaming interface should be flagged as attached. */

	return ENXIO;
}

static int
uvc_drv_detach(device_t self)
{
	struct uvc_softc *sc = device_get_softc(self);
	struct uvc_drv_video *v;
	int free_now;

	DPRINTF("%s-%d-%p\n", __func__, __LINE__, self);
	/* Delete sysfs state. */
		/* userspace device-node teardown is handled by the V4L2 wrapper */

	v = sc->video;
	if (v != NULL) {
		mtx_lock(&v->mtx);
		atomic_store_64(&v->detaching, 1);
		mtx_unlock(&v->mtx);

		/* destroy video hard code only one */
		uvc_drv_destroy_video(v);
	}

	/* driver cleanup */
	if (v != NULL) {
		/* entry close */
		uvc_v4l2_unreg(v);
		sc->video = NULL;
		mtx_lock(&v->mtx);
		atomic_store_64(&v->teardown_done, 1);
		free_now = (atomic_load_acq_64(&v->users) == 0);
		mtx_unlock(&v->mtx);
		if (free_now)
			uvc_drv_free_video(v);
	}

	DPRINTF("%s-%d-%p\n", __func__, __LINE__, self);
	return 0;
}

struct usb_vid_pid {
	uint16_t	idVendor;
	uint16_t	idProduct;
};

static struct usb_vid_pid usb_webcam_power_save_lut[] = {
	{0x1bcf, 0x28c4},       /* 5470 MTC Webcam */
	{0x1bcf, 0x2b98},	/* 5470 MTC Webcam */
	{0x0c45, 0x671e},       /* 5470 MTC Webcam */
	{0x0bda, 0x5520},	/* 5470 MTC CNFHH46P27540009CCD0 IntegratedWebcamHD */
	{0x0bda, 0x5675},	/* 5470 MTC AzureWave IntegratedWebcamHD */
	{0x0b0e, 0x3013},	/* jabra panacast 50 */
};

static int
usb_webcam_power_save(device_t dev, const char *product)
{
	int	i;
	uint32_t vid, pid;

	device_get_usb_vidpid(dev, &vid, &pid);
	for (i = 0; i < nitems(usb_webcam_power_save_lut); i++) {
		if (vid == usb_webcam_power_save_lut[i].idVendor &&
		    pid == usb_webcam_power_save_lut[i].idProduct)
			return 1;
	}

	if (product && !strncmp(product, "IntegratedWebcam", 16)) {
		return 1;
	}

	return 0;
}

static int
uvc_drv_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct uvc_softc *sc = device_get_softc(dev);
	const char *str;
	int ret;

	DPRINTF("%s-%p\n", __func__, dev);
	device_set_usb_desc(dev);
	sc->udev = uaa->device;
	sc->dev = dev;
	sc->iface = uaa->iface;

	str = usb_get_product(uaa->device);
	if (!str || !strncmp(str, "product", strlen("product")))
		snprintf(sc->name, 64, "UVC Camera");
	else
		snprintf(sc->name, 64, "%s", str);

	sc->quirks |= uvc_quirks_lookup_local(dev);

	sc->ctrl = uvc_drv_init_ctrl(uaa->iface, uaa->info.bIfaceIndex,
		uaa->info.bIfaceNum);
	if (!sc->ctrl) {
		DPRINTF("UVC alloc ctrl fault.\n");
		ret = ENOMEM;
		goto detach;
	}

	ret = uvc_drv_parse_ctrl(sc, sc->ctrl);
	if (ret) {
		DPRINTF("UVC Parse Ctrl Error.\n");
		goto detach;
	}
	ret = uvc_ctrl_init_dev(sc, sc->ctrl);
	if (ret) {
		DPRINTF("UVC control init error.\n");
		goto detach;
	}

	/* Hard-coded here for now; there may be more than one. */
	sc->video = uvc_drv_init_video(sc, sc->ctrl, sc->data);
	if (!sc->video) {
		ret = EINVAL;
		goto detach;
	}

	/* userspace device-node setup is handled by the V4L2 wrapper */

	/* Power saving. */
	if (usb_webcam_power_save(dev, str)) {
		usbd_set_power_mode(sc->udev, USB_POWER_MODE_SAVE);
	}
	if (sc->quirks & UVC_QUIRK_DISABLE_HUB_U1U2 &&
	    sc->udev->parent_hub &&
	    sc->udev->speed >= USB_SPEED_SUPER) {
		usbd_req_set_hub_u1_timeout(sc->udev->parent_hub, NULL,
					    sc->udev->port_no, 0);
		usbd_req_set_hub_u2_timeout(sc->udev->parent_hub, NULL,
					    sc->udev->port_no, 0);
	}
	return 0;
detach:
	uvc_drv_detach(dev);
	return ret;
}

static device_method_t uvc_methods[] = {
	DEVMETHOD(device_probe, uvc_drv_probe),
	DEVMETHOD(device_attach, uvc_drv_attach),
	DEVMETHOD(device_detach, uvc_drv_detach),

	DEVMETHOD_END
};

static driver_t uvc_driver = {
	.name = UVC_DRIVER_NAME,
	.methods = uvc_methods,
	.size = sizeof(struct uvc_softc),
};

DRIVER_MODULE(uvc, uhub, uvc_driver, NULL, 0);
MODULE_DEPEND(uvc, usb, 1, 1, 1);
MODULE_VERSION(uvc, 1);

/* A match on these entries will load uvc */
static const STRUCT_USB_HOST_ID __used uvc_devs[] = {
	{USB_IFACE_CLASS(UICLASS_VIDEO),
	 USB_IFACE_SUBCLASS(UISUBCLASS_CTRL),},
};

USB_PNP_HOST_INFO(uvc_devs);
