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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>

#include <dev/video/video_if.h>

MALLOC_DEFINE(M_VIDEO, "video", "Generic video core");

static d_open_t video_dev_open;
static d_read_t video_dev_read;
static d_write_t video_dev_write;
static d_close_t video_dev_close;
static d_ioctl_t video_dev_ioctl;
static d_mmap_t video_dev_mmap;
static d_poll_t video_dev_poll;
static void video_dev_unregister_cb(void *arg);

static struct cdevsw video_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	video_dev_open,
	.d_read =	video_dev_read,
	.d_write =	video_dev_write,
	.d_close =	video_dev_close,
	.d_ioctl =	video_dev_ioctl,
	.d_mmap =	video_dev_mmap,
	.d_poll =	video_dev_poll,
	.d_name =	"video",
};

static int
video_dev_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct video_dev *vd;

	vd = dev->si_drv1;
	if (vd == NULL || vd->hw_if == NULL || vd->hw_if->open == NULL)
		return (ENXIO);

	return (vd->hw_if->open(vd->priv, flags, fmt, td));
}

static int
video_dev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct video_dev *vd;

	vd = dev->si_drv1;
	if (vd == NULL || vd->hw_if == NULL || vd->hw_if->read == NULL)
		return (ENXIO);

	return (vd->hw_if->read(vd->priv, uio, ioflag));
}

static int
video_dev_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct video_dev *vd;

	vd = dev->si_drv1;
	if (vd == NULL || vd->hw_if == NULL || vd->hw_if->write == NULL)
		return (ENXIO);

	return (vd->hw_if->write(vd->priv, uio, ioflag));
}

static int
video_dev_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct video_dev *vd;

	vd = dev->si_drv1;
	if (vd == NULL || vd->hw_if == NULL || vd->hw_if->close == NULL)
		return (0);

	return (vd->hw_if->close(vd->priv, flags, fmt, td));
}

static int
video_dev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct video_dev *vd;

	vd = dev->si_drv1;
	if (vd == NULL || vd->hw_if == NULL)
		return (ENOTTY);

	switch (cmd) {
	case VIDIOC_QUERYCAP:
		if (vd->hw_if->querycap == NULL)
			return (ENOTTY);
		return (vd->hw_if->querycap(vd->priv,
		    (struct v4l2_capability *)data));
	case VIDIOC_G_PARM:
		if (vd->hw_if->g_parm == NULL)
			return (ENOTTY);
		return (vd->hw_if->g_parm(vd->priv,
		    (struct v4l2_streamparm *)data));
	case VIDIOC_S_PARM:
		if (vd->hw_if->s_parm == NULL)
			return (ENOTTY);
		return (vd->hw_if->s_parm(vd->priv,
		    (struct v4l2_streamparm *)data));
	case VIDIOC_ENUMINPUT:
		if (vd->hw_if->enum_input == NULL)
			return (ENOTTY);
		return (vd->hw_if->enum_input(vd->priv,
		    (struct v4l2_input *)data));
	case VIDIOC_G_INPUT:
		if (vd->hw_if->g_input == NULL)
			return (ENOTTY);
		return (vd->hw_if->g_input(vd->priv, (int *)data));
	case VIDIOC_S_INPUT:
		if (vd->hw_if->s_input == NULL)
			return (ENOTTY);
		return (vd->hw_if->s_input(vd->priv, *(int *)data));
	case VIDIOC_CROPCAP:
		if (vd->hw_if->cropcap == NULL)
			return (ENOTTY);
		return (vd->hw_if->cropcap(vd->priv,
		    (struct v4l2_cropcap *)data));
	case VIDIOC_ENUM_FMT:
		if (vd->hw_if->enum_fmt == NULL)
			return (ENOTTY);
		return (vd->hw_if->enum_fmt(vd->priv,
		    (struct v4l2_fmtdesc *)data));
	case VIDIOC_ENUM_FRAMESIZES:
		if (vd->hw_if->enum_framesizes == NULL)
			return (ENOTTY);
		return (vd->hw_if->enum_framesizes(vd->priv,
		    (struct v4l2_frmsizeenum *)data));
	case VIDIOC_ENUM_FRAMEINTERVALS:
		if (vd->hw_if->enum_frameintervals == NULL)
			return (ENOTTY);
		return (vd->hw_if->enum_frameintervals(vd->priv,
		    (struct v4l2_frmivalenum *)data));
	case VIDIOC_G_FMT:
		if (vd->hw_if->g_fmt == NULL)
			return (ENOTTY);
		return (vd->hw_if->g_fmt(vd->priv,
		    (struct v4l2_format *)data));
	case VIDIOC_TRY_FMT:
		if (vd->hw_if->try_fmt == NULL)
			return (ENOTTY);
		return (vd->hw_if->try_fmt(vd->priv,
		    (struct v4l2_format *)data));
	case VIDIOC_S_FMT:
		if (vd->hw_if->s_fmt == NULL)
			return (ENOTTY);
		return (vd->hw_if->s_fmt(vd->priv,
		    (struct v4l2_format *)data));
	case VIDIOC_STREAMON:
		if (vd->hw_if->streamon == NULL)
			return (ENOTTY);
		return (vd->hw_if->streamon(vd->priv,
		    *(enum v4l2_buf_type *)data));
	case VIDIOC_STREAMOFF:
		if (vd->hw_if->streamoff == NULL)
			return (ENOTTY);
		return (vd->hw_if->streamoff(vd->priv,
		    *(enum v4l2_buf_type *)data));
	case VIDIOC_REQBUFS:
		if (vd->hw_if->reqbufs == NULL)
			return (ENOTTY);
		return (vd->hw_if->reqbufs(vd->priv,
		    (struct v4l2_requestbuffers *)data));
	case VIDIOC_QUERYBUF:
		if (vd->hw_if->querybuf == NULL)
			return (ENOTTY);
		return (vd->hw_if->querybuf(vd->priv,
		    (struct v4l2_buffer *)data));
	case VIDIOC_QBUF:
		if (vd->hw_if->qbuf == NULL)
			return (ENOTTY);
		return (vd->hw_if->qbuf(vd->priv,
		    (struct v4l2_buffer *)data));
	case VIDIOC_DQBUF:
		if (vd->hw_if->dqbuf == NULL)
			return (ENOTTY);
		return (vd->hw_if->dqbuf(vd->priv,
		    (struct v4l2_buffer *)data, (fflag & O_NONBLOCK) != 0));
	case VIDIOC_QUERYCTRL:
		if (vd->hw_if->queryctrl == NULL)
			return (ENOTTY);
		return (vd->hw_if->queryctrl(vd->priv,
		    (struct v4l2_queryctrl *)data));
	case VIDIOC_QUERYMENU:
		if (vd->hw_if->querymenu == NULL)
			return (ENOTTY);
		return (vd->hw_if->querymenu(vd->priv,
		    (struct v4l2_querymenu *)data));
	case VIDIOC_G_CTRL:
		if (vd->hw_if->g_ctrl == NULL)
			return (ENOTTY);
		return (vd->hw_if->g_ctrl(vd->priv,
		    (struct v4l2_control *)data));
	case VIDIOC_S_CTRL:
		if (vd->hw_if->s_ctrl == NULL)
			return (ENOTTY);
		return (vd->hw_if->s_ctrl(vd->priv,
		    (struct v4l2_control *)data));
	case VIDIOC_LOG_STATUS:
		if (vd->hw_if->log_status == NULL)
			return (ENOTTY);
		return (vd->hw_if->log_status(vd->priv));
	default:
		break;
	}

	if (vd->hw_if->ioctl == NULL)
		return (ENOTTY);

	return (vd->hw_if->ioctl(vd->priv, cmd, data, fflag, td));
}

static int
video_dev_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
	struct video_dev *vd;

	vd = dev->si_drv1;
	if (vd == NULL || vd->hw_if == NULL || vd->hw_if->mmap == NULL)
		return (ENXIO);

	return (vd->hw_if->mmap(vd->priv, offset, paddr, nprot, memattr));
}

static int
video_dev_poll(struct cdev *dev, int events, struct thread *td)
{
	struct video_dev *vd;

	vd = dev->si_drv1;
	if (vd == NULL || vd->hw_if == NULL || vd->hw_if->poll == NULL)
		return (ENXIO);

	return (vd->hw_if->poll(vd->priv, events, td));
}

static void
video_dev_unregister_cb(void *arg)
{
	struct video_dev *vd = arg;

	free(vd, M_VIDEO);
}

int
video_dev_register(struct video_dev **vdp, const struct video_hw_if *hw_if,
    void *priv, uid_t uid, gid_t gid, int mode, const char *name, u_long unit,
    u_int linux_major, u_long linux_minor)
{
	struct make_dev_args mda;
	struct video_dev *vd;
	int error;

	if (vdp == NULL || hw_if == NULL || name == NULL)
		return (EINVAL);

	vd = malloc(sizeof(*vd), M_VIDEO, M_WAITOK | M_ZERO);
	vd->priv = priv;
	vd->hw_if = hw_if;

	make_dev_args_init(&mda);
	mda.mda_devsw = &video_cdevsw;
	mda.mda_uid = uid;
	mda.mda_gid = gid;
	mda.mda_mode = mode;
	mda.mda_si_drv1 = vd;

	error = make_dev_s(&mda, &vd->cdev, "%s%lu", name, unit);
	if (error != 0) {
		free(vd, M_VIDEO);
		return (error);
	}

	if (linux_major != 0)
		make_dev_alias(vd->cdev, "char/%u:%lu", linux_major,
		    linux_minor);

	*vdp = vd;
	return (0);
}

void
video_dev_unregister(struct video_dev *vd)
{

	if (vd == NULL)
		return;
	if (vd->cdev != NULL)
		destroy_dev_sched_cb(vd->cdev, video_dev_unregister_cb, vd);
	else
		free(vd, M_VIDEO);
}
