/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Dell Inc.
 *
 *	Alvin Chen <weike_chen@dell.com, vico.chern@qq.com>
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

#include <sys/cdefs.h>
#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/condvar.h>
#include <sys/stat.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/sbuf.h>

#include <sys/filedesc.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>

#define USB_DEBUG_VAR uvc_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_dev.h>
#include <dev/usb/usb_mbuf.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_dynamic.h>
#include <dev/usb/usb_util.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"
#include "uvc_buf.h"
#include "uvc_v4l2.h"

#define FRAME_DUMP 0
#if FRAME_DUMP
static void uvc_writefile(char *path, void *data, int len);
#endif

int
uvc_buf_reset_buf(struct video_queue *bq)
{
	struct video_buffer_entry *buf;

	mtx_lock(&bq->mtx);
	buf = STAILQ_FIRST(&bq->product);
	if (buf) {
		buf->vbuf.bytesused = 0;
	}
	mtx_unlock(&bq->mtx);

	return (0);
}

static int
uvc_buf_check_length(struct uvc_drv_video *v, struct video_buffer_entry *buf,
		     uint32_t len, uint32_t finish)
{
	struct v4l2_buffer *vbuf = &buf->vbuf;

	/*
	 * guvcview enables video stream twice on ThinOS, first time it calls
	 * VIDIOC_REQBUFS to alloc buf for data stream, then disable video
	 * but second time it does not call VIDIOC_REQBUFS to alloc mem which
	 * calls crash not call, we do not figure out why yet, so do not let go
	 * temply, we will figure out why it behaves this weird here
	 */

	if (vbuf->length == 0) {
		return (1);
	}

	if (vbuf->bytesused + len > vbuf->length) {
		return (1);
	}
	/*
	 * -------------------2208 release----------------------------------
	 * So far we found that sometime driver will upload incompleted frame
	 * when using YUYV format so we check whether the data fully occupy
	 * buffer, but NV12 format's data does not fully occupy buffer
	 * origenally, and now we not fully know all kinds of formats'
	 * speciality so we quit it now for 2208 relase.
	 * -------------------------end--------------------------------------
	 * Logitech BRIO gives wrong dwMaxFrameSize in its NV12 format
	 * fixed it at video probe, so uncomment "return" here
	 */
	if (v->cur_fmt && (v->cur_fmt->fcc != V4L2_PIX_FMT_MJPEG &&
			   v->cur_fmt->fcc != V4L2_PIX_FMT_H264)) {
		if (finish && (vbuf->bytesused + len < vbuf->length)) {
			return (1);
		}
	}

	return (0);
}

int
uvc_bulkbuf_sell_buf(struct video_queue *bq,
		struct usb_page_cache *pc, usb_frlength_t offset,
		usb_frlength_t len,
		usb_frlength_t actlen,
		usb_frlength_t maxFramelen)
{
	struct video_buffer_entry *buf;
	struct uvc_drv_video *v;
	unsigned char *ptr;
	uint32_t maxlen, nbytes;
	int ret = 0;

	mtx_lock(&bq->mtx);
	if (!VIDEO_QUEUE_IS_RUNNING(bq)) {
		ret = EINVAL;
		goto done;
	}

	v = bq->priv;

	buf = STAILQ_FIRST(&bq->product);
	if (!buf) {
		ret = EINVAL;
		v->bulk.skip_payload = 1;
	}

	if (!v->bulk.skip_payload && buf) {
		if (buf->status != VIDEO_BUFFER_STATE_ACTIVE) {
			if (v->bulk.last_fid ==
			    (v->bulk.header[1] & UVC_PL_HEADER_BIT_FID)) {
				v->bulk.skip_payload = 1;
				buf->vbuf.bytesused = 0;
				goto done;
			}
			buf->status = VIDEO_BUFFER_STATE_ACTIVE;
		}
	}

	if (v->bulk.last_fid != (v->bulk.header[1] & UVC_PL_HEADER_BIT_FID))
		v->bulk.last_fid = (v->bulk.header[1] & UVC_PL_HEADER_BIT_FID);

	if (len != 0 && !v->bulk.skip_payload) {
		maxlen = buf->vbuf.length - buf->vbuf.bytesused;
		nbytes = min(maxlen, len);

		ptr = (char *)(buf->mem) + buf->offset;
		usbd_copy_out(pc, offset, ptr + buf->vbuf.bytesused,
			nbytes);
		buf->vbuf.bytesused += nbytes;

		if (len > maxlen) {
			len -= nbytes;
			goto finished;
		}
	}

	if (actlen < maxFramelen ||
	    v->bulk.payload_size >= v->bulk.max_payload_size) {
		if (!v->bulk.skip_payload && buf)
			if (uvc_drv_check_video_context(v, buf->mem,
						buf->vbuf.bytesused)) {
				buf->vbuf.bytesused = 0;
				goto clean;
			}

		if (!v->bulk.skip_payload &&
		    ((v->bulk.header[1] & UVC_PL_HEADER_BIT_EOF) ||
			(v != NULL && v->sc != NULL &&
				(v->sc->quirks & UVC_QUIRK_NO_EOF))))
finished:
			if (buf->vbuf.bytesused > 0) {
				STAILQ_REMOVE_HEAD(&bq->product, link);
				buf->vbuf.sequence = bq->seq++;
				microtime(&buf->vbuf.timestamp);
				buf->status = VIDEO_BUFFER_STATE_DONE;
				STAILQ_INSERT_TAIL(&bq->consumer, buf, link);
				cv_broadcast(&bq->io_cv);
				if (SEL_WAITING(&bq->sel))
					selwakeup(&bq->sel);
			}

clean:
		v->bulk.header_size = 0;
		v->bulk.skip_payload = 0;
		v->bulk.payload_size = 0;
	}

done:
	mtx_unlock(&bq->mtx);
	return (ret);
}

int
uvc_buf_sell_buf(struct video_queue *bq,
		struct usb_page_cache *pc, usb_frlength_t offset,
		usb_frlength_t len, uint32_t finish, uint8_t fid)
{
	struct video_buffer_entry *buf;
	unsigned char *ptr;
	struct uvc_drv_video *video;
	int ret = 0;

	mtx_lock(&bq->mtx);
	if (!VIDEO_QUEUE_IS_RUNNING(bq)) {
		ret = EINVAL;
		goto done;
	}
	video = bq->priv;

	buf = STAILQ_FIRST(&bq->product);
	if (buf) {
		if (buf->status != VIDEO_BUFFER_STATE_ACTIVE) {
			if (video->last_fid == fid) {
				buf->vbuf.bytesused = 0;
				goto done;
			}
			buf->status = VIDEO_BUFFER_STATE_ACTIVE;
		}
		if (uvc_buf_check_length(video, buf, len, finish)) {
			buf->vbuf.bytesused = 0;
			goto done;
		}
		if (video->last_fid != fid)
			video->last_fid = fid;

		if (len != 0) {
			ptr = (char *)(buf->mem) + buf->offset;
			usbd_copy_out(pc, offset, ptr + buf->vbuf.bytesused,
				len);
			buf->vbuf.bytesused += len;
		}

		if (!finish)
			goto done;

		if (uvc_drv_check_video_context(video,
		    (char *)buf->mem + buf->offset, buf->vbuf.bytesused)) {
			buf->vbuf.bytesused = 0;
			goto done;
		}

		if (buf->vbuf.bytesused > 0) {

#if FRAME_DUMP
			char path[PATH_MAX];
			sprintf(path, "/tmp/%x_%4lu.data", (short)bq->video,
			    bq->seq);
			uvc_writefile(path,
				      (void *)((char *)buf->mem + buf->offset),
				      buf->vbuf.bytesused);
#endif
			STAILQ_REMOVE_HEAD(&bq->product, link);
			buf->vbuf.sequence = bq->seq++;
			microtime(&buf->vbuf.timestamp);
			buf->status = VIDEO_BUFFER_STATE_DONE;
			STAILQ_INSERT_TAIL(&bq->consumer, buf, link);
			cv_broadcast(&bq->io_cv);
			if (SEL_WAITING(&bq->sel))
				selwakeup(&bq->sel);
		}
	}

done:
	mtx_unlock(&bq->mtx);
	return (ret);
}

#if FRAME_DUMP
static void
uvc_writefile(char *path, void *data, int len)
{
	struct thread *td;
	struct uio auio;
	struct iovec aiov;
	int error, fd = -1;

	td = curthread;

	pwd_ensure_dirs();

	KLG("write: %s; %p; %d\n", path, data, len);
	error = kern_openat(td, AT_FDCWD, path, UIO_SYSSPACE,
	    O_CREAT | O_RDWR, 0666);
	if (error) {
		KLG("open %s error: %d\n", path, error);
		goto out;
	}
	fd = td->td_retval[0];

	aiov.iov_base = data;
	aiov.iov_len = len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = len;
	auio.uio_segflg = UIO_SYSSPACE;
	error = kern_writev(td, fd, &auio);
	if (error) {
		KLG("write %s error: %d\n", path, error);
		goto out;
	}
out:
	if (fd >= 0)
		kern_close(td, fd);
}
#endif
