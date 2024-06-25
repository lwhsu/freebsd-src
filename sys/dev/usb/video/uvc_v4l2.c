/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Dell Inc.
 *
 *	Alvin Chen <weike_chen@dell.com, vico.chern@qq.com>
 *	Zhichao Li <Zhichao1.Li@Dell.com>
 *	Pilar Liang <Pillar.Liang@Dellteam.com>
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
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/sdt.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/proc.h>

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/videoio_uvc.h>

#include <dev/usb/usb.h>
#define USB_DEBUG_VAR uvc_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_device.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"
#include "uvc_buf.h"
#include "uvc_v4l2.h"

int v4l2_not_allowed;

static SYSCTL_NODE(_hw_usb, OID_AUTO, v4l2, CTLFLAG_RW, 0, "USB v4l2");
SYSCTL_INT(_hw_usb_v4l2, OID_AUTO, v4l2_not_allowed, CTLFLAG_RWTUN,
		    &v4l2_not_allowed, 0, "V4L2 Permission");

static int uvc_v4l2_backend_detaching(struct uvc_drv_video *v);

static int
uvc_v4l2_acquire_pri(struct uvc_v4l2_cdev_priv *priv)
{
	if (uvc_v4l2_backend_detaching(priv->v))
		return (ENXIO);

	DPRINTF("=======v4l2 acquire pri %s-%s %d-%d mem:%lu===\n",
		curthread->td_proc->p_comm, curthread->td_proc->p_pptr->p_comm,
		curthread->td_proc->p_pid, curthread->td_proc->p_pptr->p_pid,
		priv->num);
	DPRINTF("pri:%lu video pri:%lu\n", priv->work_pri, priv->v->pri);

	if (priv->work_pri == UVC_V4L2_PRI_ACTIVE) {
		DPRINTF("%s already get\n", __func__);
		return (0);
	}

	if (atomic_cmpset_64(&priv->v->pri, 0, 1) == 0) {
		DPRINTF("%s busy\n", __func__);
		return (EBUSY);
	}

	priv->work_pri = UVC_V4L2_PRI_ACTIVE;

	return (0);
}

static void
uvc_v4l2_dismiss_pri(struct uvc_v4l2_cdev_priv *priv)
{
	DPRINTF("%s num:%lu\n", __func__, priv->num);
	if (priv->work_pri == UVC_V4L2_PRI_ACTIVE) {
		DPRINTF("%s free\n", __func__);
		atomic_store_64(&priv->v->pri, 0);
	}

	priv->work_pri = UVC_V4L2_PRI_PASSIVE;
}

static int
uvc_v4l2_has_pri(struct uvc_v4l2_cdev_priv *priv)
{
	return (priv->work_pri == UVC_V4L2_PRI_ACTIVE);
}

static int
uvc_v4l2_backend_detaching(struct uvc_drv_video *v)
{
	return (v == NULL || atomic_load_acq_64(&v->detaching) != 0);
}

static int
uvc_v4l2_get_bus_info(struct usb_device *udev, char *bus_info, size_t len)
{
	struct usb_device *next;
	uint8_t port_path[32];
	size_t nports;
	size_t off;
	int n;

	if (len == 0)
		return (EINVAL);
	if (udev == NULL)
		return (EINVAL);

	n = snprintf(bus_info, len, "usb-%u", usbd_get_bus_index(udev));
	if (n < 0)
		return (EINVAL);
	if ((size_t)n >= len)
		return (0);
	off = (size_t)n;

	nports = 0;
	for (next = udev; next->parent_hub != NULL &&
	    nports < sizeof(port_path) / sizeof(port_path[0]);
	    next = next->parent_hub) {
		port_path[nports++] = next->port_no;
	}

	while (nports > 0) {
		n = snprintf(bus_info + off, len - off, "-%u",
		    port_path[--nports]);
		if (n < 0)
			return (EINVAL);
		if ((size_t)n >= len - off)
			return (0);
		off += (size_t)n;
	}

	return (0);
}

static int
uvc_v4l2_querycap(void *arg, struct v4l2_capability *cap)
{
	struct uvc_drv_video *v = arg;
	struct usb_device *udev;
	uint32_t caps;

	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);
	if (v->sc == NULL)
		return (ENXIO);

	memset(cap, 0x00, sizeof(*cap));
	strlcpy(cap->driver, UVC_DRIVER_NAME, sizeof(cap->driver));
	snprintf(cap->card, sizeof(cap->card), "%s", v->sc->name);

	udev = v->sc->udev;
	if (!udev)
		return (EINVAL);
	if (uvc_v4l2_get_bus_info(udev, cap->bus_info,
	    sizeof(cap->bus_info)) != 0)
		return (EINVAL);
	cap->version = UVC_DRIVER_VERSION;

	caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = caps;
	cap->reserved[0] = caps;
	return (0);
}

static int
uvc_v4l2_normalize_buf_type(enum v4l2_buf_type type,
    enum v4l2_buf_type *normalized)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		*normalized = type;
		return (0);
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		*normalized = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		return (0);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		*normalized = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		return (0);
	default:
		return (EINVAL);
	}
}

static int
uvc_v4l2_target_matches_type(enum v4l2_buf_type type, uint32_t target)
{
	switch (target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		return (type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? 0 : EINVAL);
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		return (type == V4L2_BUF_TYPE_VIDEO_OUTPUT ? 0 : EINVAL);
	default:
		return (EINVAL);
	}
}

static int
uvc_v4l2_fill_active_rect(struct uvc_drv_video *v, struct v4l2_selection *sel)
{
	if (v == NULL || sel == NULL)
		return (EINVAL);

	sel->r.left = 0;
	sel->r.top = 0;

	mtx_lock(&v->mtx);
	if (v->cur_frm == NULL) {
		mtx_unlock(&v->mtx);
		return (EINVAL);
	}
	sel->r.width = v->cur_frm->width;
	sel->r.height = v->cur_frm->height;
	mtx_unlock(&v->mtx);
	return (0);
}

static int
uvc_v4l2_g_selection(struct uvc_drv_video *v, struct v4l2_selection *p)
{
	enum v4l2_buf_type normalized_type;
	int ret;

	ret = uvc_v4l2_normalize_buf_type(p->type, &normalized_type);
	if (ret != 0)
		return (ret);
	if (normalized_type != v->type)
		return (EINVAL);

	ret = uvc_v4l2_target_matches_type(normalized_type, p->target);
	if (ret != 0)
		return (ret);

	return (uvc_v4l2_fill_active_rect(v, p));
}

static int
uvc_v4l2_cropcap(void *arg, struct v4l2_cropcap *p)
{
	struct uvc_drv_video *v = arg;
	struct v4l2_selection s = {};
	uint32_t ret = 0;

	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	p->pixelaspect.numerator = 1;
	p->pixelaspect.denominator = 1;

	s.type = p->type;

	if (s.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		s.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (s.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		s.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	ret = uvc_drv_get_pixelaspect();
	if (ret != 0 && ret != ENOTTY)
		return (ret);
	if (V4L2_TYPE_IS_OUTPUT(p->type))
		s.target = V4L2_SEL_TGT_COMPOSE_BOUNDS;
	else
		s.target = V4L2_SEL_TGT_CROP_BOUNDS;

	ret = uvc_v4l2_g_selection(v, &s);
	if (ret)
		return (ret);
	p->bounds = s.r;
	/* Obtain the default rectangle. */
	if (s.target == V4L2_SEL_TGT_COMPOSE_BOUNDS)
		s.target = V4L2_SEL_TGT_COMPOSE_DEFAULT;
	else
		s.target = V4L2_SEL_TGT_CROP_DEFAULT;
	ret = uvc_v4l2_g_selection(v, &s);
	if (ret)
		return (ret);

	p->defrect = s.r;

	return (0);
}

static uint32_t
uvc_v4l2_gcd_u32(uint32_t a, uint32_t b)
{
	uint32_t remainder;

	while (b != 0) {
		remainder = a % b;
		a = b;
		b = remainder;
	}

	return (a == 0 ? 1 : a);
}

void
uvc_v4l2_interval_to_timeperframe(uint32_t interval_100ns,
	struct v4l2_fract *timeperframe)
{
	uint32_t divisor;

	if (timeperframe == NULL)
		return;

	if (interval_100ns == 0) {
		timeperframe->numerator = 0;
		timeperframe->denominator = 1;
		return;
	}

	divisor = uvc_v4l2_gcd_u32(interval_100ns, 10000000U);
	timeperframe->numerator = interval_100ns / divisor;
	timeperframe->denominator = 10000000U / divisor;
}

static struct uvc_data_format *
uvc_v4l2_find_format_by_fourcc(struct uvc_drv_video *v, uint32_t pixel_format)
{
	struct uvc_data_format *fmt;
	int i;

	for (i = 0; i < v->data->nfmt; i++) {
		fmt = &v->data->fmt[i];
		if (fmt->fcc == pixel_format)
			return (fmt);
	}

	return (NULL);
}

static int
uvc_v4l2_find_frame_interval(struct uvc_data_format *fmt,
    struct v4l2_frmivalenum *itv, struct uvc_data_frame **frame_out,
    uint32_t *interval_index_out)
{
	struct uvc_data_frame *frm;
	uint32_t ordinal;
	uint32_t slots;
	int i;

	ordinal = 0;
	for (i = 0; i < fmt->nfrm; i++) {
		frm = &fmt->frm[i];
		if (frm->width != itv->width || frm->height != itv->height)
			continue;

		slots = (frm->interval_type != 0) ? frm->interval_type : 1;
		if (ordinal + slots > itv->index) {
			*frame_out = frm;
			*interval_index_out = itv->index - ordinal;
			return (0);
		}
		ordinal += slots;
	}

	return (EINVAL);
}

static int
uvc_v4l2_enum_frameintervals(void *arg, struct v4l2_frmivalenum *itv)
{
	struct uvc_drv_video *v = arg;
	struct uvc_data_format *fmt;
	struct uvc_data_frame *frm;
	uint32_t interval_index;

	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	fmt = uvc_v4l2_find_format_by_fourcc(v, itv->pixel_format);
	if (fmt == NULL)
		return (EINVAL);
	if (uvc_v4l2_find_frame_interval(fmt, itv, &frm, &interval_index) != 0)
		return (EINVAL);

	if (frm->interval_type != 0) {
		itv->type = V4L2_FRMIVAL_TYPE_DISCRETE;
		uvc_v4l2_interval_to_timeperframe(
		    frm->interval[interval_index].val, &itv->x.discrete);
		return (0);
	}

	itv->type = V4L2_FRMIVAL_TYPE_STEPWISE;
	uvc_v4l2_interval_to_timeperframe(frm->interval[0].val,
	    &itv->x.stepwise.min);
	uvc_v4l2_interval_to_timeperframe(frm->interval[1].val,
	    &itv->x.stepwise.max);
	uvc_v4l2_interval_to_timeperframe(frm->interval[2].val,
	    &itv->x.stepwise.step);
	return (0);
}

uint32_t
uvc_v4l2_timeperframe_to_interval(const struct v4l2_fract *timeperframe)
{
	uint64_t scaled;

	if (timeperframe == NULL || timeperframe->denominator == 0)
		return (UINT32_MAX);

	scaled = (uint64_t)timeperframe->numerator * 10000000ULL;
	scaled += timeperframe->denominator / 2;
	scaled /= timeperframe->denominator;
	if (scaled > UINT32_MAX)
		return (UINT32_MAX);

	return ((uint32_t)scaled);
}

static int
uvc_v4l2_g_parm(void *arg, struct v4l2_streamparm *parm)
{
	struct uvc_drv_video *video = arg;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(video))
		return (ENXIO);

	memset(parm, 0, sizeof(*parm));
	parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.capturemode = 0;
	parm->parm.capture.extendedmode = 0;
	parm->parm.capture.readbuffers = 0;
	uvc_v4l2_interval_to_timeperframe(UGETDW(video->req.dwFrameInterval),
	    &parm->parm.capture.timeperframe);

	return (0);
}

/* Stub function. */
static int
uvc_v4l2_enum_input(void *arg, struct v4l2_input *input)
{
	struct uvc_drv_video *v = arg;
	struct uvc_drv_ctrl *ctrl;
	uint32_t index = input->index;
	struct uvc_topo_node *it = NULL; /* input terminal */

	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);
	ctrl = v->ctrl;

	/* No selector support here; index 0 is the only valid value. */
	if (index)
		return (EINVAL);

	it = STAILQ_FIRST(&ctrl->topo_nodes);
	if (!it)
		return (EINVAL);

	memset(input, 0, sizeof(*input));
	input->index = index;
	strlcpy(input->name, it->node_name, sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return (0);
}

static void
uvc_v4l2_dtor(void *data)
{
	struct uvc_v4l2_cdev_priv *priv = data;
	struct uvc_drv_video *v;
	int free_now;

	v = priv->v;
	mtx_lock(&v->mtx);
	if (priv->work_pri == UVC_V4L2_PRI_ACTIVE)
		atomic_store_64(&v->pri, 0);
	atomic_subtract_64(&v->users, 1);
	free_now = (atomic_load_acq_64(&v->detaching) != 0 &&
	    atomic_load_acq_64(&v->teardown_done) != 0 &&
	    atomic_load_acq_64(&v->users) == 0);
	mtx_unlock(&v->mtx);

	free(data, M_UVC);
	if (free_now)
		uvc_drv_free_video(v);

	DPRINTF("%s\n", __func__);
}

static int
uvc_v4l2_queryctrl(void *arg, struct v4l2_queryctrl *qc)
{
	struct uvc_drv_video *v = arg;
	int ret = EINVAL;

	if (qc == NULL)
		return (ret);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	ret = uvc_query_v4l2_ctrl(v, qc);
	return (ret);
}

static int
uvc_v4l2_querymenu(void *arg, struct v4l2_querymenu *qm)
{
	struct uvc_drv_video *v = arg;
	int ret = EINVAL;

	if (qm == NULL)
		return (ret);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	ret = uvc_query_v4l2_menu(v, qm);
	return (ret);
}

static int
uvc_v4l2_open(void *arg, int flags, int fmt, struct thread *td)
{
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = arg;
	int ret, free_now;

	if (v4l2_not_allowed != 0) {
		return (ENXIO);		/* failure */
	}

	mtx_lock(&v->mtx);
	if (atomic_load_acq_64(&v->detaching) != 0) {
		mtx_unlock(&v->mtx);
		return (ENXIO);
	}
	atomic_add_64(&v->users, 1);
	mtx_unlock(&v->mtx);

	DPRINTF("===v4l2 open %s-%s %d-%d mem:%lu pri:%lu===\n",
		curthread->td_proc->p_comm, curthread->td_proc->p_pptr->p_comm,
		curthread->td_proc->p_pid, curthread->td_proc->p_pptr->p_pid,
		v->users, v->pri);

	DPRINTF("====%s====%d %d %s\n", __func__, curthread->td_proc->p_pid,
		curthread->td_proc->p_pptr->p_pid,
		curthread->td_proc->p_pptr->p_comm);
	DPRINTF("enable:%lu\n", v->enable);

	priv = (struct uvc_v4l2_cdev_priv *)
		malloc(sizeof(*priv), M_UVC, M_ZERO | M_WAITOK);
	if (!priv) {
		DPRINTF("%s %d------>Error.\n", __func__, __LINE__);
		mtx_lock(&v->mtx);
		atomic_subtract_64(&v->users, 1);
		free_now = (atomic_load_acq_64(&v->detaching) != 0 &&
		    atomic_load_acq_64(&v->teardown_done) != 0 &&
		    atomic_load_acq_64(&v->users) == 0);
		mtx_unlock(&v->mtx);
		if (free_now)
			uvc_drv_free_video(v);
		return (ENOMEM);
	}

	priv->work_mode = UVC_V4L2_MODE_READ;
	priv->work_pri = UVC_V4L2_PRI_PASSIVE;
	priv->v = v;
	priv->num = v->users;
	ret = devfs_set_cdevpriv(priv, uvc_v4l2_dtor);
	if (ret != 0) {
		mtx_lock(&v->mtx);
		atomic_subtract_64(&v->users, 1);
		free_now = (atomic_load_acq_64(&v->detaching) != 0 &&
		    atomic_load_acq_64(&v->teardown_done) != 0 &&
		    atomic_load_acq_64(&v->users) == 0);
		mtx_unlock(&v->mtx);
		free(priv, M_UVC);
		if (free_now)
			uvc_drv_free_video(v);
		return (ret);
	}
	return (0);
}

static int
uvc_v4l2_close(void *arg, int flags, int fmt, struct thread *td)
{
	struct uvc_drv_video *v = arg;
	struct uvc_v4l2_cdev_priv *priv;
	int ret;

	ret = devfs_get_cdevpriv((void **)&priv);
	if (ret != 0) {
		DPRINTF("error===================================\n");
		return (ret);
	}

	DPRINTF("===v4l2 begin to close %s-%s %d-%d num:%lu mem:%lu===\n",
		curthread->td_proc->p_comm, curthread->td_proc->p_pptr->p_comm,
		curthread->td_proc->p_pid, curthread->td_proc->p_pptr->p_pid,
		priv->num, v->users);
	DPRINTF("=vedio device pri:%lu--thispri:%lu\n", v->pri, priv->work_pri);
	DPRINTF("----%s----\n", __func__);

	if (uvc_v4l2_has_pri(priv)) {
		ret = uvc_drv_stop_video(v, 1);
		if (ret)
			DPRINTF("close stop video fault\n");
		video_queue_free_bufs(&v->bq);
		uvc_v4l2_dismiss_pri(priv);
	}
	/* atomic_subtract_64(&v->users, 1); */

	return (0);
}

static int
uvc_v4l2_get_cdevpriv(struct uvc_v4l2_cdev_priv **privp)
{
	int error;

	error = devfs_get_cdevpriv((void **)privp);
	if (error != 0)
		DPRINTF("%s err:%d\n", __func__, error);

	return (error);
}

static int
uvc_v4l2_acquire_owner(struct uvc_v4l2_cdev_priv **privp)
{
	int error;

	error = uvc_v4l2_get_cdevpriv(privp);
	if (error != 0)
		return (error);

	return (uvc_v4l2_acquire_pri(*privp));
}

static int
uvc_v4l2_require_owner(struct uvc_v4l2_cdev_priv **privp)
{
	int error;

	error = uvc_v4l2_get_cdevpriv(privp);
	if (error != 0)
		return (error);
	if (uvc_v4l2_backend_detaching((*privp)->v))
		return (ENXIO);
	if (!uvc_v4l2_has_pri(*privp))
		return (EBUSY);

	return (0);
}

static int
uvc_v4l2_s_parm(void *arg, struct v4l2_streamparm *parm)
{
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = arg;
	int ret;

	ret = uvc_v4l2_acquire_owner(&priv);
	if (ret != 0)
		return (ret);
	(void)priv;
	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (uvc_drv_set_streampar(v, parm));
}

static int
uvc_v4l2_g_input(void *arg, int *input)
{
	struct uvc_drv_video *v = arg;

	if (v == NULL || input == NULL)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	*input = 0;
	return (0);
}

static int
uvc_v4l2_s_input(void *arg, int input)
{
	struct uvc_drv_video *v = arg;

	if (v == NULL)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	/*
	 * UVC exposes a single camera input today. Preserve the historical
	 * behavior that selecting input 0 succeeds so V4L2 userland can
	 * continue through setup.
	 */
	if (input != 0)
		return (EINVAL);

	return (0);
}

static int
uvc_v4l2_enum_fmt(void *arg, struct v4l2_fmtdesc *f_d)
{
	struct uvc_drv_video *v = arg;

	if (f_d->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (uvc_drv_enum_v4l2_fmt(v, f_d));
}

static int
uvc_v4l2_enum_framesizes(void *arg, struct v4l2_frmsizeenum *fs)
{
	if (uvc_v4l2_backend_detaching(arg))
		return (ENXIO);
	return (uvc_drv_enum_v4l2_framesizes(arg, fs));
}

static int
uvc_v4l2_g_fmt(void *arg, struct v4l2_format *fmt)
{
	struct uvc_drv_video *v = arg;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (uvc_drv_get_v4l2_fmt(v, fmt));
}

static int
uvc_v4l2_try_fmt(void *arg, struct v4l2_format *fmt)
{
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = arg;
	struct uvc_data_request req;
	int ret;

	ret = uvc_v4l2_acquire_owner(&priv);
	if (ret != 0)
		return (ret);
	(void)priv;
	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (uvc_drv_try_v4l2_fmt(v, fmt, &req, NULL, NULL));
}

static int
uvc_v4l2_s_fmt(void *arg, struct v4l2_format *fmt)
{
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = arg;
	struct uvc_data_format *rfmt;
	struct uvc_data_frame *rfrm;
	struct uvc_data_request req;
	int ret;

	ret = uvc_v4l2_acquire_owner(&priv);
	if (ret != 0)
		return (ret);
	(void)priv;
	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	ret = uvc_drv_try_v4l2_fmt(v, fmt, &req, &rfmt, &rfrm);
	if (ret != 0)
		return (ret);

	return (uvc_drv_set_video(v, &req, rfmt, rfrm));
}

static int
uvc_v4l2_streamon(void *arg, enum v4l2_buf_type type)
{
	struct uvc_v4l2_cdev_priv *priv;
	int ret;

	ret = uvc_v4l2_require_owner(&priv);
	if (ret != 0)
		return (ret);
	(void)priv;
	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(arg))
		return (ENXIO);

	return (uvc_drv_start_video(arg));
}

static int
uvc_v4l2_streamoff(void *arg, enum v4l2_buf_type type)
{
	struct uvc_v4l2_cdev_priv *priv;
	int ret;

	ret = uvc_v4l2_require_owner(&priv);
	if (ret != 0)
		return (ret);
	(void)priv;
	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(arg))
		return (ENXIO);

	return (uvc_drv_stop_video(arg, 0));
}

static int
uvc_v4l2_reqbufs(void *arg, struct v4l2_requestbuffers *rb)
{
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = arg;
	int ret;

	ret = uvc_v4l2_acquire_owner(&priv);
	if (ret != 0)
		return (ret);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);
	if (rb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    rb->memory != V4L2_MEMORY_MMAP)
		return (EINVAL);

	ret = video_queue_reqbufs(&v->bq, &rb->count,
	    UGETDW(v->req.dwMaxFrameSize));
	if (ret == 0) {
		if (!(v->cur_fmt->flags & UVC_FMT_FLAG_COMPRESSED))
			video_queue_set_drop_flag(&v->bq);
		priv->work_mode = rb->count ? UVC_V4L2_MODE_MMAP :
		    UVC_V4L2_MODE_READ;
	}

	return (ret);
}

static int
uvc_v4l2_querybuf(void *arg, struct v4l2_buffer *buf)
{
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = arg;
	int ret;

	ret = uvc_v4l2_require_owner(&priv);
	if (ret != 0)
		return (ret);
	(void)priv;
	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (video_queue_querybuf(&v->bq, buf));
}

static int
uvc_v4l2_qbuf(void *arg, struct v4l2_buffer *buf)
{
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = arg;
	int ret;

	ret = uvc_v4l2_require_owner(&priv);
	if (ret != 0)
		return (ret);
	(void)priv;
	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    buf->memory != V4L2_MEMORY_MMAP)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (video_queue_qbuf(&v->bq, buf));
}

static int
uvc_v4l2_dqbuf(void *arg, struct v4l2_buffer *buf, int nonblock)
{
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = arg;
	int ret;

	ret = uvc_v4l2_require_owner(&priv);
	if (ret != 0)
		return (ret);
	(void)priv;
	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    buf->memory != V4L2_MEMORY_MMAP)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (video_queue_dqbuf(&v->bq, buf, nonblock));
}

static int
uvc_v4l2_g_ctrl(void *arg, struct v4l2_control *control)
{
	struct uvc_drv_video *v = arg;

	if (control == NULL)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (uvc_get_v4l2_ctrl(v, control));
}

static int
uvc_v4l2_s_ctrl(void *arg, struct v4l2_control *control)
{
	struct uvc_drv_video *v = arg;

	if (control == NULL)
		return (EINVAL);
	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	return (uvc_set_v4l2_ctrl(v, control));
}

static int
uvc_v4l2_log_status(void *arg)
{
	(void)arg;
	return (ENOTTY);
}

static int
uvc_v4l2_xu_request_is_read(uint32_t request_code)
{
	return ((request_code & 0x80U) != 0);
}

static int
uvc_v4l2_xu_query_to_request(const struct fbsd_uvc_xu_query *query,
    struct uvc_drv_xu_request *request)
{
	uint32_t direction_mask;

	direction_mask = FBSD_UVC_XU_DIR_READ | FBSD_UVC_XU_DIR_WRITE;
	if ((query->direction & ~direction_mask) != 0)
		return (EINVAL);
	if (query->direction != FBSD_UVC_XU_DIR_READ &&
	    query->direction != FBSD_UVC_XU_DIR_WRITE)
		return (EINVAL);
	if (query->data_len == 0 || query->data_len > FBSD_UVC_XU_MAX_DATA)
		return (ENXIO);
	if (query->data_ptr == 0)
		return (EINVAL);
	if (query->unit_id > 0xff || query->control_selector > 0xff ||
	    query->request_code > 0xff)
		return (EINVAL);
	if (uvc_v4l2_xu_request_is_read(query->request_code) !=
	    (query->direction == FBSD_UVC_XU_DIR_READ))
		return (EINVAL);

	request->unit_id = query->unit_id;
	request->control_selector = query->control_selector;
	request->request_code = query->request_code;
	request->data_len = query->data_len;
	request->data = (void *)(uintptr_t)query->data_ptr;

	return (0);
}

static int
uvc_v4l2_xu_map_validate(const struct fbsd_uvc_xu_map *map)
{
	if (map->v4l2_id == 0 || map->control_selector == 0 ||
	    map->control_size_bits == 0)
		return (EINVAL);
	if (map->control_size_bits > 32)
		return (EINVAL);
	if (map->v4l2_type == V4L2_CTRL_TYPE_MENU) {
		if (map->menu_num == 0 || map->menu_ptr == 0)
			return (EINVAL);
	} else if (map->menu_num != 0 || map->menu_ptr != 0) {
		return (EINVAL);
	}

	return (0);
}

static int
uvc_v4l2_ioctl(void *arg, u_long cmd, caddr_t data, int fflag,
	struct thread *td)
{
	struct uvc_drv_video *v = arg;
	struct uvc_drv_xu_request request;
	const struct fbsd_uvc_xu_query *query;
	int ret = ENOTTY;

	(void)fflag;
	(void)td;

	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	switch (cmd) {
	case FBSD_UVCIOC_XU_QUERY:
		query = (const struct fbsd_uvc_xu_query *)data;
		ret = uvc_v4l2_xu_query_to_request(query, &request);
		if (ret != 0)
			return (ret);
		ret = uvc_drv_xu_query(v, &request);
		break;
	case FBSD_UVCIOC_XU_MAP:
		ret = priv_check(td, PRIV_DRIVER);
		if (ret != 0)
			return (ret);
		ret = uvc_v4l2_xu_map_validate(
		    (const struct fbsd_uvc_xu_map *)data);
		if (ret != 0)
			return (ret);
		ret = uvc_ctrl_add_xu_mapping(v,
		    (const struct fbsd_uvc_xu_map *)data);
		break;

	default:
		DPRINTF("unsupported ioctl 0x%lx\n", cmd);
		ret = ENOTTY;
		break;
	}

	return (ret);
}

static int
uvc_v4l2_mmap(void *arg, vm_ooffset_t offset, vm_paddr_t *paddr,
	int nprot, vm_memattr_t *memattr)
{
	struct uvc_drv_video *v = arg;
	int error;

	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	/* DPRINTF("paddr:%p offset:%ld\n", paddr, offset); */
	error = video_queue_mmap(&v->bq, paddr, offset);
	return (error);
}

static int
uvc_v4l2_poll(void *arg, int events, struct thread *td)
{
	int ret;
	struct uvc_drv_video *v = arg;

	if (uvc_v4l2_backend_detaching(v))
		return (ENXIO);

	ret =  video_queue_poll(&v->bq, events, td);

	return (ret);
}

static const struct video_hw_if uvc_v4l2_ops = {
	.open = uvc_v4l2_open,
	.close = uvc_v4l2_close,
	.querycap = uvc_v4l2_querycap,
	.g_parm = uvc_v4l2_g_parm,
	.s_parm = uvc_v4l2_s_parm,
	.enum_input = uvc_v4l2_enum_input,
	.g_input = uvc_v4l2_g_input,
	.s_input = uvc_v4l2_s_input,
	.cropcap = uvc_v4l2_cropcap,
	.enum_fmt = uvc_v4l2_enum_fmt,
	.enum_framesizes = uvc_v4l2_enum_framesizes,
	.enum_frameintervals = uvc_v4l2_enum_frameintervals,
	.g_fmt = uvc_v4l2_g_fmt,
	.try_fmt = uvc_v4l2_try_fmt,
	.s_fmt = uvc_v4l2_s_fmt,
	.streamon = uvc_v4l2_streamon,
	.streamoff = uvc_v4l2_streamoff,
	.reqbufs = uvc_v4l2_reqbufs,
	.querybuf = uvc_v4l2_querybuf,
	.qbuf = uvc_v4l2_qbuf,
	.dqbuf = uvc_v4l2_dqbuf,
	.queryctrl = uvc_v4l2_queryctrl,
	.querymenu = uvc_v4l2_querymenu,
	.g_ctrl = uvc_v4l2_g_ctrl,
	.s_ctrl = uvc_v4l2_s_ctrl,
	.log_status = uvc_v4l2_log_status,
	.ioctl = uvc_v4l2_ioctl,
	.mmap = uvc_v4l2_mmap,
	.poll = uvc_v4l2_poll,
};

void
uvc_v4l2_unreg(struct uvc_drv_video *v)
{
	DPRINTF("%s\n", __func__);
	/* destroy v4l2 */
	if (v->v4l2) {
		video_dev_unregister(v->v4l2->vd);
		free(v->v4l2, M_UVC);
	}
	/* remove */
	v->v4l2 = NULL;
}

int
uvc_v4l2_reg(struct uvc_drv_video *v)
{
	struct uvc_v4l2 *v4l2;
	int ret;

	DPRINTF("%s\n", __func__);
	v4l2 = malloc(sizeof(*v4l2), M_UVC, M_ZERO | M_WAITOK);
	if (!v4l2)
		return (ENOMEM);

	ret = video_dev_register(&v4l2->vd, &uvc_v4l2_ops, v, UID_ROOT,
	    GID_VIDEO, 0666, UVC_V4L2_DEVICE_NAME, v->unit, LINUX_MAJOR,
	    LINUX_MINOR + v->unit);
	if (ret) {
		DPRINTF("failed to create v4l2 char device: %d.\n", ret);
		free(v4l2, M_UVC);
		return (ret);
	}
	v->v4l2 = v4l2;

	return (0);
}
