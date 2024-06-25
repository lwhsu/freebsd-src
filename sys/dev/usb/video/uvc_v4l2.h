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

#ifndef _DEV_USB_UVC_V4L2_H
#define _DEV_USB_UVC_V4L2_H

#include <dev/video/video_if.h>

#define UVC_V4L2_DEVICE_NAME		"video"

#ifndef LINUX_MAJOR
#define LINUX_MAJOR 81
#endif
#ifndef LINUX_MINOR
#define LINUX_MINOR 0
#endif

enum uvc_v4l2_work_mode {
	UVC_V4L2_MODE_READ	=	0x0,
	UVC_V4L2_MODE_MMAP,
	UVC_V4L2_MODE_MAX,
};

enum uvc_v4l2_work_pri {
	UVC_V4L2_PRI_PASSIVE	=	0x0,
	UVC_V4L2_PRI_ACTIVE,
	UVC_V4L2_PRI_MAX,
};

struct uvc_v4l2_cdev_priv {
	uint64_t	work_mode;
	uint64_t	work_pri;
	uint64_t	num;
	struct uvc_drv_video	*v;
};

struct uvc_v4l2 {
	struct video_dev *vd;
};

struct uvc_drv_video;
extern int uvc_v4l2_reg(struct uvc_drv_video *v);
extern void uvc_v4l2_unreg(struct uvc_drv_video *v);
extern void uvc_v4l2_interval_to_timeperframe(uint32_t interval_100ns,
	struct v4l2_fract *timeperframe);
extern uint32_t uvc_v4l2_timeperframe_to_interval(
	const struct v4l2_fract *timeperframe);

#endif /* end _DEV_USB_UVC_V4L2_H */
