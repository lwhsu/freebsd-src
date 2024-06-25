/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _DEV_USB_VIDEO_UVC_NEGOTIATION_H_
#define _DEV_USB_VIDEO_UVC_NEGOTIATION_H_

#include "uvc_drv.h"

int uvc_drv_do_request(struct usb_device *udev, uint8_t query, uint8_t unit,
    uint8_t iface_num, uint8_t cs, void *data, uint16_t size, int timeout);
int uvc_drv_set_video_ctrl(struct uvc_drv_video *video,
    struct uvc_data_request *req, int probe);
int uvc_drv_get_video_ctrl(struct uvc_drv_video *video,
    struct uvc_data_request *req, int probe, uint8_t query);
int uvc_drv_probe_video(struct uvc_drv_video *video,
    struct uvc_data_request *req);
uint32_t uvc_drv_try_frame_interval(struct uvc_data_frame *frame,
    uint32_t interval);
int uvc_drv_init_cur_fmt_frm(struct uvc_drv_video *v,
    struct uvc_data_request *req);

#endif /* _DEV_USB_VIDEO_UVC_NEGOTIATION_H_ */
