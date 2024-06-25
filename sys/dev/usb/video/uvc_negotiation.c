/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/mutex.h>

#include <dev/usb/usb.h>
#define USB_DEBUG_VAR uvc_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usbdi.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"
#include "uvc_negotiation.h"

struct uvc_frame_choice_rank {
	int exact_match;
	uint64_t aspect_penalty;
	uint64_t area_delta;
	uint64_t edge_delta;
};

static int
uvc_negotiation_request_size(const struct uvc_drv_video *video)
{
	if (video->ctrl->revision >= 0x0150)
		return (sizeof(struct uvc_data_request));
	return ((video->ctrl->revision >= 0x0110) ? 34 : 26);
}

static struct uvc_data_format *
uvc_negotiation_find_format_by_index(const struct uvc_drv_video *video,
    uint8_t format_index)
{
	uint8_t i;

	for (i = 0; i < video->data->nfmt; i++) {
		if (video->data->fmt[i].index == format_index)
			return (&video->data->fmt[i]);
	}

	return (NULL);
}

static struct uvc_data_frame *
uvc_negotiation_find_frame_by_index(const struct uvc_data_format *fmt,
    uint8_t frame_index)
{
	uint64_t i;

	for (i = 0; i < fmt->nfrm; i++) {
		if (fmt->frm[i].index == frame_index)
			return (&fmt->frm[i]);
	}

	return (NULL);
}

static uint32_t
uvc_negotiation_frame_size(const struct uvc_data_format *fmt,
    const struct uvc_data_frame *frm, uint32_t current_size)
{
	uint32_t computed_size;

	if (fmt == NULL || frm == NULL)
		return (current_size);
	if (fmt->flags & UVC_FMT_FLAG_COMPRESSED)
		return (current_size);

	computed_size = frm->width * frm->height / 8 * fmt->bpp;
	return (computed_size);
}

static void
uvc_negotiation_sanitize_request(struct uvc_drv_video *video,
    struct uvc_data_request *req)
{
	struct uvc_data_format *fmt;
	struct uvc_data_frame *frm;
	uint32_t sizeimage;

	fmt = uvc_negotiation_find_format_by_index(video, req->bFormatIndex);
	if (fmt == NULL)
		return;

	frm = uvc_negotiation_find_frame_by_index(fmt, req->bFrameIndex);
	if (frm == NULL)
		return;

	sizeimage = uvc_negotiation_frame_size(fmt, frm,
	    UGETDW(req->dwMaxFrameSize));
	if (sizeimage != UGETDW(req->dwMaxFrameSize))
		USETDW(req->dwMaxFrameSize, sizeimage);
}

int
uvc_drv_set_video_ctrl(struct uvc_drv_video *video,
    struct uvc_data_request *req, int probe)
{
	int ret;

	mtx_assert(&video->mtx, MA_OWNED);
	mtx_unlock(&video->mtx);
	ret = uvc_drv_do_request(video->sc->udev, UVC_SET_CUR, 0,
	    video->data->iface_num,
	    probe ? UVC_VS_PROBE_CONTROL : UVC_VS_COMMIT_CONTROL,
	    req, uvc_negotiation_request_size(video), USB_DEFAULT_TIMEOUT);
	mtx_lock(&video->mtx);
	return (ret);
}

int
uvc_drv_get_video_ctrl(struct uvc_drv_video *video, struct uvc_data_request *req,
    int probe, uint8_t query)
{
	struct uvc_data_request tmp;
	int ret;

	memset(&tmp, 0, sizeof(tmp));

	mtx_assert(&video->mtx, MA_OWNED);
	mtx_unlock(&video->mtx);
	ret = uvc_drv_do_request(video->sc->udev, query, 0,
	    video->data->iface_num,
	    probe ? UVC_VS_PROBE_CONTROL : UVC_VS_COMMIT_CONTROL,
	    &tmp, uvc_negotiation_request_size(video), USB_DEFAULT_TIMEOUT);
	mtx_lock(&video->mtx);
	if (ret != 0)
		return (ret);

	if (query != UVC_GET_MIN && query != UVC_GET_MAX)
		uvc_negotiation_sanitize_request(video, &tmp);

	memcpy(req, &tmp, sizeof(*req));
	return (0);
}

static void
uvc_negotiation_reset_probe_fields(struct uvc_data_request *req,
    const struct uvc_data_request *reqmin,
    const struct uvc_data_request *reqmax)
{
	USETW(req->wKeyFrameRate, UGETW(reqmin->wKeyFrameRate));
	USETW(req->wPFrameRate, UGETW(reqmin->wPFrameRate));
	USETW(req->wCompQuality, UGETW(reqmax->wCompQuality));
	USETW(req->wCompWindowSize, UGETW(reqmin->wCompWindowSize));
}

static int
uvc_negotiation_load_probe_bounds(struct uvc_drv_video *video,
    struct uvc_data_request *req, struct uvc_data_request *reqmin,
    struct uvc_data_request *reqmax, int *have_bounds)
{
	int ret;

	*have_bounds = 0;
	if (video->sc->quirks & UVC_QUIRK_PROBE_MINMAX)
		return (0);

	ret = uvc_drv_get_video_ctrl(video, reqmin, 1, UVC_GET_MIN);
	if (ret != 0)
		return (ret);

	ret = uvc_drv_get_video_ctrl(video, reqmax, 1, UVC_GET_MAX);
	if (ret != 0)
		return (ret);

	*have_bounds = 1;
	USETW(req->wCompQuality, UGETW(reqmax->wCompQuality));
	return (0);
}

static int
uvc_negotiation_probe_payload_fits(struct uvc_drv_video *video,
    const struct uvc_data_request *req)
{
	if (video->data->num_altsetting == 1)
		return (1);

	return (UGETDW(req->dwMaxPayloadSize) <= video->data->maxpsize);
}

static int
uvc_negotiation_retry_with_bounds(struct uvc_drv_video *video,
    struct uvc_data_request *req, const struct uvc_data_request *reqmin,
    const struct uvc_data_request *reqmax, int have_bounds)
{
	if ((video->sc->quirks & UVC_QUIRK_PROBE_MINMAX) || !have_bounds)
		return (ENOSPC);

	uvc_negotiation_reset_probe_fields(req, reqmin, reqmax);
	return (0);
}

int
uvc_drv_probe_video(struct uvc_drv_video *video, struct uvc_data_request *req)
{
	struct uvc_data_request reqmin;
	struct uvc_data_request reqmax;
	int ret;
	int attempt;
	int have_bounds;

	memset(&reqmin, 0, sizeof(reqmin));
	memset(&reqmax, 0, sizeof(reqmax));

	mtx_lock(&video->mtx);
	ret = uvc_negotiation_load_probe_bounds(video, req, &reqmin, &reqmax,
	    &have_bounds);

	for (attempt = 0; ret == 0 && attempt < 3; attempt++) {
		ret = uvc_drv_set_video_ctrl(video, req, 1);
		if (ret != 0)
			break;

		ret = uvc_drv_get_video_ctrl(video, req, 1, UVC_GET_CUR);
		if (ret != 0)
			break;

		if (uvc_negotiation_probe_payload_fits(video, req)) {
			ret = 0;
			break;
		}

		ret = uvc_negotiation_retry_with_bounds(video, req, &reqmin, &reqmax,
		    have_bounds);
		if (ret != 0)
			break;
	}
	mtx_unlock(&video->mtx);

	return (ret);
}

static uint32_t
uvc_negotiation_choose_discrete_interval(const struct uvc_data_frame *frame,
    uint32_t interval)
{
	uint32_t best_value;
	uint64_t best_delta;
	uint64_t i;

	best_value = frame->interval[0].val;
	best_delta = UINT64_MAX;

	for (i = 0; i < frame->interval_type; i++) {
		uint32_t candidate;
		uint64_t delta;

		candidate = frame->interval[i].val;
		delta = (interval > candidate) ?
		    (uint64_t)(interval - candidate) :
		    (uint64_t)(candidate - interval);
		if (delta < best_delta ||
		    (delta == best_delta && candidate < best_value)) {
			best_delta = delta;
			best_value = candidate;
		}
	}

	return (best_value);
}

static uint32_t
uvc_negotiation_round_stepwise_interval(const struct uvc_data_frame *frame,
    uint32_t interval)
{
	uint32_t min_interval;
	uint32_t max_interval;
	uint32_t step;
	uint64_t offset;

	min_interval = frame->interval[0].val;
	max_interval = frame->interval[1].val;
	step = frame->interval[2].val;

	if (interval <= min_interval || step == 0)
		return (min_interval);
	if (interval >= max_interval)
		return (max_interval);

	offset = (uint64_t)(interval - min_interval) + step / 2;
	return (min_interval + (offset / step) * step);
}

uint32_t
uvc_drv_try_frame_interval(struct uvc_data_frame *frame, uint32_t interval)
{
	if (frame->interval_type != 0)
		return (uvc_negotiation_choose_discrete_interval(frame, interval));

	return (uvc_negotiation_round_stepwise_interval(frame, interval));
}

static struct uvc_data_format *
uvc_negotiation_select_format(struct uvc_drv_video *video, uint32_t pixelformat)
{
	struct uvc_data_format *fallback;
	int i;

	fallback = video->cur_fmt;
	if (fallback == NULL && video->data->nfmt > 0)
		fallback = &video->data->fmt[0];

	for (i = 0; i < video->data->nfmt; i++) {
		if (video->data->fmt[i].fcc == pixelformat)
			return (&video->data->fmt[i]);
	}

	return (fallback);
}

static struct uvc_frame_choice_rank
uvc_negotiation_rank_frame(const struct uvc_data_frame *frm, uint32_t width,
    uint32_t height)
{
	struct uvc_frame_choice_rank rank;
	uint64_t lhs;
	uint64_t rhs;
	uint64_t frame_area;
	uint64_t requested_area;

	rank.exact_match = (frm->width == width && frm->height == height);
	lhs = (uint64_t)frm->width * height;
	rhs = (uint64_t)frm->height * width;
	rank.aspect_penalty = (lhs > rhs) ? (lhs - rhs) : (rhs - lhs);
	frame_area = (uint64_t)frm->width * frm->height;
	requested_area = (uint64_t)width * height;
	rank.area_delta = (frame_area > requested_area) ?
	    (frame_area - requested_area) : (requested_area - frame_area);
	rank.edge_delta = (frm->width > width ? frm->width - width :
	    width - frm->width) +
	    (frm->height > height ? frm->height - height :
	    height - frm->height);
	return (rank);
}

static int
uvc_negotiation_frame_rank_better(const struct uvc_frame_choice_rank *candidate,
    const struct uvc_frame_choice_rank *best)
{
	if (candidate->exact_match != best->exact_match)
		return (candidate->exact_match > best->exact_match);
	if (candidate->aspect_penalty != best->aspect_penalty)
		return (candidate->aspect_penalty < best->aspect_penalty);
	if (candidate->area_delta != best->area_delta)
		return (candidate->area_delta < best->area_delta);
	return (candidate->edge_delta < best->edge_delta);
}

static struct uvc_data_frame *
uvc_negotiation_select_frame(struct uvc_data_format *fmt, uint32_t width,
    uint32_t height)
{
	struct uvc_data_frame *best;
	struct uvc_frame_choice_rank best_rank;
	uint64_t i;

	if (fmt == NULL || fmt->nfrm == 0)
		return (NULL);

	best = &fmt->frm[0];
	best_rank = uvc_negotiation_rank_frame(best, width, height);

	for (i = 1; i < fmt->nfrm; i++) {
		struct uvc_data_frame *candidate;
		struct uvc_frame_choice_rank candidate_rank;

		candidate = &fmt->frm[i];
		candidate_rank = uvc_negotiation_rank_frame(candidate, width, height);
		if (uvc_negotiation_frame_rank_better(&candidate_rank, &best_rank)) {
			best = candidate;
			best_rank = candidate_rank;
		}
	}

	return (best);
}

static void
uvc_negotiation_fill_pix_format(struct v4l2_pix_format *pix,
    const struct uvc_data_format *fmt, const struct uvc_data_frame *frm,
    const struct uvc_data_request *req)
{
	pix->pixelformat = fmt->fcc;
	pix->width = frm->width;
	pix->height = frm->height;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = fmt->bpp * frm->width / 8;
	pix->sizeimage = UGETDW(req->dwMaxFrameSize);
	pix->colorspace = fmt->colorspace;
	pix->priv = 0;
}

int
uvc_drv_try_v4l2_fmt(struct uvc_drv_video *video, struct v4l2_format *vfmt,
    struct uvc_data_request *req, struct uvc_data_format **rfmt,
    struct uvc_data_frame **rfrm)
{
	struct uvc_data_format *fmt;
	struct uvc_data_frame *frm;
	uint32_t desired_interval;
	int attempt;
	int ret;

	fmt = uvc_negotiation_select_format(video, vfmt->fmt.pix.pixelformat);
	if (fmt == NULL)
		return (EINVAL);

	frm = uvc_negotiation_select_frame(fmt, vfmt->fmt.pix.width,
	    vfmt->fmt.pix.height);
	if (frm == NULL)
		return (EINVAL);

	vfmt->fmt.pix.pixelformat = fmt->fcc;
	desired_interval = uvc_drv_try_frame_interval(frm, frm->default_interval);
	ret = EIO;

	for (attempt = 0; attempt < 3; attempt++) {
		memset(req, 0, sizeof(*req));
		mtx_lock(&video->mtx);
		ret = uvc_drv_get_video_ctrl(video, req, 1, UVC_GET_CUR);
		if (ret == 0) {
			USETW(req->wHint, 0x1);
			req->bFormatIndex = fmt->index;
			req->bFrameIndex = frm->index;
			USETDW(req->dwFrameInterval, desired_interval);
		}
		mtx_unlock(&video->mtx);
		if (ret != 0)
			continue;

		ret = uvc_drv_probe_video(video, req);
		if (ret == 0)
			break;
	}
	if (ret != 0)
		return (ret);

	uvc_negotiation_fill_pix_format(&vfmt->fmt.pix, fmt, frm, req);

	if (rfmt != NULL)
		*rfmt = fmt;
	if (rfrm != NULL)
		*rfrm = frm;

	return (0);
}

int
uvc_drv_init_cur_fmt_frm(struct uvc_drv_video *v, struct uvc_data_request *req)
{
	struct uvc_data_format *fmt;
	struct uvc_data_frame *frm;

	if (v == NULL || v->data == NULL || req == NULL || v->data->nfmt == 0)
		return (EINVAL);

	fmt = uvc_negotiation_find_format_by_index(v, req->bFormatIndex);
	if (fmt == NULL)
		fmt = &v->data->fmt[0];
	if (fmt->nfrm == 0)
		return (EINVAL);

	/*
	 * Some devices leave the probe reply at frame index 0 even though the
	 * parsed descriptor list is 1-based. Fall back to the first parsed frame
	 * so the runtime state stays aligned with the negotiated format.
	 */
	frm = uvc_negotiation_find_frame_by_index(fmt, req->bFrameIndex);
	if (frm == NULL)
		frm = &fmt->frm[0];

	v->cur_fmt = fmt;
	v->cur_frm = frm;
	return (0);
}
