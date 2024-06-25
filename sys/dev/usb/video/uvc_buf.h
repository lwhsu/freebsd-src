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

#ifndef _DEV_USB_UVC_BUF_H
#define _DEV_USB_UVC_BUF_H

#include <dev/video/video_var.h>

struct uvc_drv_video;
struct usb_page_cache;
extern int
uvc_buf_sell_buf(struct video_queue *bq, struct usb_page_cache *pc,
	usb_frlength_t offset, usb_frlength_t len, uint32_t finish,
	uint8_t fid);
extern int
uvc_bulkbuf_sell_buf(struct video_queue *bq,
		struct usb_page_cache *pc, usb_frlength_t offset,
		usb_frlength_t len,
		usb_frlength_t actlen,
		usb_frlength_t maxFramelen);
extern int uvc_buf_reset_buf(struct video_queue *bq);

#endif /* end _DEV_USB_UVC_BUF_H */
