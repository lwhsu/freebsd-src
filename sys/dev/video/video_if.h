/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Li-Wen Hsu
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_VIDEO_VIDEO_IF_H_
#define _DEV_VIDEO_VIDEO_IF_H_

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ioccom.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

struct thread;
struct uio;

struct video_dev;

struct video_hw_if {
	int (*open)(void *priv, int flags, int fmt, struct thread *td);
	int (*read)(void *priv, struct uio *uio, int ioflag);
	int (*write)(void *priv, struct uio *uio, int ioflag);
	int (*close)(void *priv, int flags, int fmt, struct thread *td);
	int (*querycap)(void *priv, struct v4l2_capability *cap);
	int (*g_parm)(void *priv, struct v4l2_streamparm *parm);
	int (*s_parm)(void *priv, struct v4l2_streamparm *parm);
	int (*enum_input)(void *priv, struct v4l2_input *input);
	int (*g_input)(void *priv, int *input);
	int (*s_input)(void *priv, int input);
	int (*cropcap)(void *priv, struct v4l2_cropcap *cropcap);
	int (*enum_fmt)(void *priv, struct v4l2_fmtdesc *fmtdesc);
	int (*enum_framesizes)(void *priv, struct v4l2_frmsizeenum *framesizes);
	int (*enum_frameintervals)(void *priv,
	    struct v4l2_frmivalenum *frameintervals);
	int (*g_fmt)(void *priv, struct v4l2_format *format);
	int (*try_fmt)(void *priv, struct v4l2_format *format);
	int (*s_fmt)(void *priv, struct v4l2_format *format);
	int (*streamon)(void *priv, enum v4l2_buf_type type);
	int (*streamoff)(void *priv, enum v4l2_buf_type type);
	int (*reqbufs)(void *priv, struct v4l2_requestbuffers *reqbufs);
	int (*querybuf)(void *priv, struct v4l2_buffer *buffer);
	int (*qbuf)(void *priv, struct v4l2_buffer *buffer);
	int (*dqbuf)(void *priv, struct v4l2_buffer *buffer, int nonblock);
	int (*queryctrl)(void *priv, struct v4l2_queryctrl *queryctrl);
	int (*querymenu)(void *priv, struct v4l2_querymenu *querymenu);
	int (*g_ctrl)(void *priv, struct v4l2_control *control);
	int (*s_ctrl)(void *priv, struct v4l2_control *control);
	int (*log_status)(void *priv);
	int (*ioctl)(void *priv, u_long cmd, caddr_t data, int fflag,
	    struct thread *td);
	int (*mmap)(void *priv, vm_ooffset_t offset, vm_paddr_t *paddr, int nprot,
	    vm_memattr_t *memattr);
	int (*poll)(void *priv, int events, struct thread *td);
};

struct video_dev {
	struct cdev *cdev;
	void *priv;
	const struct video_hw_if *hw_if;
};

int video_dev_register(struct video_dev **vdp, const struct video_hw_if *hw_if,
    void *priv, uid_t uid, gid_t gid, int mode, const char *name, u_long unit,
    u_int linux_major, u_long linux_minor);
void video_dev_unregister(struct video_dev *vd);

#endif
