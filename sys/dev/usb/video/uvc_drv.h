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

#ifndef _DEV_USB_UVC_DRV_H
#define _DEV_USB_UVC_DRV_H

#include <sys/videoio_uvc.h>

#include "uvc_buf.h"

/* Vendor */
#define USB_VENDOR_ID_LOGITECH			0x046D
/* Driver */
#define UVC_DRIVER_NAME				"uvc"
/* Driver Version */
#define	UVC_VERSION(a, b, c)			(((a) << 16) + ((b) << 8) + (c))
#define	UVC_DRIVER_VERSION			UVC_VERSION(1, 0, 0)
#define	UVC_DEVICE_NAME				"video"
/* UVC Transfer */
#define	UVC_N_TRANSFER				2
#define	UVC_N_BULKTRANSFER			3
#define UVC_N_ISOFRAMES				0x8

#define	UVCINTR_N_TRANSFER			1

/* Format flags */
#define UVC_FMT_FLAG_COMPRESSED			0x00000001
#define UVC_FMT_FLAG_STREAM			0x00000002
#define UVC_VC_HEADER				0x01
#define UVC_VC_INPUT_TERMINAL			0x02
#define UVC_VC_OUTPUT_TERMINAL			0x03
#define UVC_VC_SELECTOR_UNIT			0x04
#define UVC_VC_PROCESSING_UNIT			0x05
#define UVC_VC_EXTENSION_UNIT			0x06
/* VideoStreaming class specific interface descriptor */
#define UVC_VS_UNDEFINED			0x00
#define UVC_VS_INPUT_HEADER			0x01
#define UVC_VS_OUTPUT_HEADER			0x02
#define UVC_VS_STILL_IMAGE_FRAME		0x03
#define UVC_VS_FORMAT_UNCOMPRESSED		0x04
#define UVC_VS_FRAME_UNCOMPRESSED		0x05
#define UVC_VS_FORMAT_MJPEG			0x06
#define UVC_VS_FRAME_MJPEG			0x07
#define UVC_VS_FORMAT_MPEG2TS			0x0a
#define UVC_VS_FORMAT_DV			0x0c
#define UVC_VS_COLORFORMAT			0x0d
#define UVC_VS_FORMAT_FRAME_BASED		0x10
#define UVC_VS_FRAME_FRAME_BASED		0x11
/* VideoStreaming interface controls */
#define UVC_VS_CONTROL_UNDEFINED		0x00
#define UVC_VS_PROBE_CONTROL			0x01
#define UVC_VS_COMMIT_CONTROL			0x02
#define UVC_VS_STILL_PROBE_CONTROL		0x03
#define UVC_VS_STILL_COMMIT_CONTROL		0x04
#define UVC_VS_STILL_IMAGE_TRIGGER_CONTROL	0x05
#define UVC_VS_STREAM_ERROR_CODE_CONTROL	0x06
#define UVC_VS_GENERATE_KEY_FRAME_CONTROL	0x07
#define UVC_VS_UPDATE_FRAME_SEGMENT_CONTROL	0x08
#define UVC_VS_SYNC_DELAY_CONTROL		0x09
/* Input Terminal types */
#define UVC_ITT_VENDOR_SPECIFIC			0x0200
#define UVC_ITT_CAMERA				0x0201
#define UVC_ITT_MEDIA_TRANSPORT_INPUT		0x0202
/* Request codes */
#define UVC_RC_UNDEFINED			0x00
#define UVC_SET_CUR				0x01
#define UVC_GET_CUR				0x81
#define UVC_GET_MIN				0x82
#define UVC_GET_MAX				0x83
#define UVC_GET_RES				0x84
#define UVC_GET_LEN				0x85
#define UVC_GET_INFO				0x86
#define UVC_GET_DEF				0x87

/* A.9.1. VideoControl Interface Control Selectors */
#define UVC_VC_CONTROL_UNDEFINED                        0x00
#define UVC_VC_VIDEO_POWER_MODE_CONTROL                 0x01
#define UVC_VC_REQUEST_ERROR_CODE_CONTROL               0x02

/* A.9.2. Terminal Control Selectors */
#define UVC_TE_CONTROL_UNDEFINED                        0x00

/* A.9.3. Selector Unit Control Selectors */
#define UVC_SU_CONTROL_UNDEFINED                        0x00
#define UVC_SU_INPUT_SELECT_CONTROL                     0x01

/* A.9.4. Camera Terminal Control Selectors */
#define UVC_CT_CONTROL_UNDEFINED                        0x00
#define UVC_CT_SCANNING_MODE_CONTROL                    0x01
#define UVC_CT_AE_MODE_CONTROL                          0x02
#define UVC_CT_AE_PRIORITY_CONTROL                      0x03
#define UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL           0x04
#define UVC_CT_EXPOSURE_TIME_RELATIVE_CONTROL           0x05
#define UVC_CT_FOCUS_ABSOLUTE_CONTROL                   0x06
#define UVC_CT_FOCUS_RELATIVE_CONTROL                   0x07
#define UVC_CT_FOCUS_AUTO_CONTROL                       0x08
#define UVC_CT_IRIS_ABSOLUTE_CONTROL                    0x09
#define UVC_CT_IRIS_RELATIVE_CONTROL                    0x0a
#define UVC_CT_ZOOM_ABSOLUTE_CONTROL                    0x0b
#define UVC_CT_ZOOM_RELATIVE_CONTROL                    0x0c
#define UVC_CT_PANTILT_ABSOLUTE_CONTROL                 0x0d
#define UVC_CT_PANTILT_RELATIVE_CONTROL                 0x0e
#define UVC_CT_ROLL_ABSOLUTE_CONTROL                    0x0f
#define UVC_CT_ROLL_RELATIVE_CONTROL                    0x10
#define UVC_CT_PRIVACY_CONTROL                          0x11

/* A.9.5. Processing Unit Control Selectors */
#define UVC_PU_CONTROL_UNDEFINED                        0x00
#define UVC_PU_BACKLIGHT_COMPENSATION_CONTROL           0x01
#define UVC_PU_BRIGHTNESS_CONTROL                       0x02
#define UVC_PU_CONTRAST_CONTROL                         0x03
#define UVC_PU_GAIN_CONTROL                             0x04
#define UVC_PU_POWER_LINE_FREQUENCY_CONTROL             0x05
#define UVC_PU_HUE_CONTROL                              0x06
#define UVC_PU_SATURATION_CONTROL                       0x07
#define UVC_PU_SHARPNESS_CONTROL                        0x08
#define UVC_PU_GAMMA_CONTROL                            0x09
#define UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL        0x0a
#define UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL   0x0b
#define UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL          0x0c
#define UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL     0x0d
#define UVC_PU_DIGITAL_MULTIPLIER_CONTROL               0x0e
#define UVC_PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL         0x0f
#define UVC_PU_HUE_AUTO_CONTROL                         0x10
#define UVC_PU_ANALOG_VIDEO_STANDARD_CONTROL            0x11
#define UVC_PU_ANALOG_LOCK_STATUS_CONTROL               0x12

/* --------------------------------------------------------------------------
 * UVC constants
 */

enum uvc_topology_term_flags {
	UVC_TERM_FLAG_INPUT		= 0x0000,
	UVC_TERM_FLAG_OUTPUT		= 0x8000,
	UVC_TERM_FLAG_DIRECTION_MASK	= 0x8000,
	UVC_TERM_FLAG_TYPE_MASK		= 0x7fff,
};

// 2.3 Video Function Topology
enum uvc_topo_type {
	UVC_TOPO_TYPE_UNKNOWN,
	UVC_TOPO_TYPE_INPUT_TERMINAL,
	UVC_TOPO_TYPE_OUTPUT_TERMINAL,
	UVC_TOPO_TYPE_SELECTOR_UNIT,
	UVC_TOPO_TYPE_PROCESSING_UNIT,
	UVC_TOPO_TYPE_ENCODING_UNIT,
	UVC_TOPO_TYPE_EXTENSION_UNIT,
	UVC_TOPO_TYPE_CAMERA_TERMINAL,
	UVC_TOPO_TYPE_MEDIA_TRANSPORT_TERMINAL
};

/* ------------------------------------------------------------------------
 * GUIDs
 */
#define UVC_GUID_LOGITECH_DEV_INFO \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x1e}
#define UVC_GUID_LOGITECH_USER_HW \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x1f}
#define UVC_GUID_LOGITECH_VIDEO \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x50}
#define UVC_GUID_LOGITECH_MOTOR \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x56}

#define UVC_GUID_FORMAT_MJPEG \
	{ 'M',  'J',  'P',  'G', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YUY2 \
	{ 'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_NV12 \
	{ 'N',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YV12 \
	{ 'Y',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_I420 \
	{ 'I',  '4',  '2',  '0', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_UYVY \
	{ 'U',  'Y',  'V',  'Y', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y800 \
	{ 'Y',  '8',  '0',  '0', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BY8 \
	{ 'B',  'Y',  '8',  ' ', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_H264 \
	{ 'H',  '2',  '6',  '4', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}


struct uvc_softc;
struct uvc_v4l2;
struct uvc_buf_queue;

/* Video Descriptor */
struct uvc_vc_header_desc {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uWord	wRevision;
	uWord	wTotalLen;
	uDWord	dClockFreq;
	uByte	bIfaceNums;
	uByte   bIfaceList[0];		/*  Interface Index Lists   */
} __packed;

struct uvc_vc_input_terminal_desc {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bTerminalID;
	uWord	wTerminalType;
	uByte	bAssocTerminal;
	uByte	bITerminal;
} __packed;

struct uvc_vc_output_terminal_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubType;
	uByte bTerminalID;
	uWord wTerminalType;
	uByte bAssocTerminal;
	uByte bSourceID;
	uByte bITerminal;
} __packed;

struct uvc_vc_selector_unit_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubType;
	uByte bUnitID;
	uByte bNrInPins;
	uByte baSourceID[0];
	uByte iSelector;
} __packed;

struct uvc_vc_processing_unit_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubType;
	uByte bUnitID;
	uByte bSourceID;
	uWord wMaxMultiplier;
	uByte bControlSize;
	uByte bmControls[2];
	uByte iProcessing;
	uByte bmVideoStandards;
} __packed;

struct uvc_vc_extension_unit_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubType;
	uByte bUnitID;
	uByte guidExtensionCode[16];
	uByte bNumControls;
	uByte bNrInPins;
	uByte baSourceID[0];
	uByte bControlSize;
	uByte bmControls[0];
	uByte iExtension;
} __packed;

struct uvc_vs_in_header_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubtype;
	uByte bFormatNum;
	uWord wTotalLen;
	uByte bEp;
	uByte bInfo;
	uByte bTerminalLink;
	uByte bStillCaptureMethod;
	uByte bTriggerSupport;
	uByte bTriggerUsage;
	uByte bControlSize;
	uByte bControls[0];         /* Controls */
} __packed;

struct uvc_vs_color_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubtype;
	uByte bColorPris;
	uByte bTranChars;
	uByte bMatrix;
};

struct uvc_vs_uncompressed_format_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubtype;
	uByte bFormatIndex;
	uByte bFrameNum;
	uByte bGuidFmt[16];
	uByte bBpp;
	uByte bDefaultFrameIndex;
	uByte bAspectRadiox;
	uByte bAspectRadioy;
	uByte bInterlaceFlags;
	uByte bCopyProtect;
} __packed;

struct uvc_vs_frame_based_format_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubtype;
	uByte bFormatIndex;
	uByte bFrameNum;
	uByte bGuidFmt[16];
	uByte bBitsPerpixel;
	uByte bDefaultFrameIndex;
	uByte bAspectRadiox;
	uByte bAspectRadioy;
	uByte bInterlaceFlags;
	uByte bCopyProtect;
	uByte bVariableSize;
} __packed;

struct uvc_vs_frame_desc {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bFrameIndex;
	uByte	bCapabilities;
	uWord	wWidth;
	uWord	wHeight;
	uDWord	dMinBitRate;
	uDWord	dMaxBitRate;
	uDWord	dMaxFrameBufferSize;
	uDWord	dDefaultFrameInterval;
	uByte	bFrameIntervalType;
	uDWord	dFrameInterval[0];
} __packed;

struct uvc_vs_frame_based_desc {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bFrameIndex;
	uByte	bCapabilities;
	uWord	wWidth;
	uWord	wHeight;
	uDWord	dMinBitRate;
	uDWord	dMaxBitRate;
	uDWord	dDefaultFrameInterval;
	uByte	bFrameIntervalType;
	uDWord	dwBytesPerLine;
	uDWord	dFrameInterval[0];
} __packed;

struct uvc_data_request {
	uWord wHint;
	uByte bFormatIndex;
	uByte bFrameIndex;
	uDWord dwFrameInterval;
	uWord wKeyFrameRate;
	uWord wPFrameRate;
	uWord wCompQuality;
	uWord wCompWindowSize;
	uWord wDelay;
	uDWord dwMaxFrameSize;
	uDWord dwMaxPayloadSize;
	uDWord dwClockFrequency;
	uByte bFramingInfo;
	uByte bPreferedVersion;
	uByte bMinVersion;
	uByte bMaxVersion;
	uByte bUsage;
	uByte bBitDepthLuma;
	uByte bmSettings;
	uByte bMaxNumberOfRefFramesPlus1;
	uWord bmRateControlModes;
	uByte bmLayoutPerStream[8];
} __packed;

struct uvc_data_payload_header {
	uint8_t	len;
#define	UVC_PL_HEADER_BIT_FID		(1)
#define	UVC_PL_HEADER_BIT_EOF		(1 << 1)
#define	UVC_PL_HEADER_BIT_PTS		(1 << 2)
#define	UVC_PL_HEADER_BIT_SCR		(1 << 3)
#define	UVC_PL_HEADER_BIT_RES		(1 << 4)
#define	UVC_PL_HEADER_BIT_STI		(1 << 5)
#define	UVC_PL_HEADER_BIT_ERR		(1 << 6)
#define	UVC_PL_HEADER_BIT_EOH		(1 << 7)
	uint8_t	header;
#if 0
	uint32_t pts;
	uint8_t scr[6];
#endif
} __attribute__ ((packed));

/*
 * Dynamic controls
 *
 * The internal control model lives in uvc_ctrl_internal.h so the public
 * driver header does not carry internal vocabulary or layout details.
 */
struct uvc_ctrl_state;
struct uvc_ct_node_info {
	uint16_t wObjectiveFocalLengthMin;
	uint16_t wObjectiveFocalLengthMax;
	uint16_t wOcularFocalLength;
	uint8_t bControlSize;
	uint8_t *bmControls;
}; // Camera Terminal

struct uvc_media_node_info {
	uint8_t bControlSize;
	uint8_t *bmControls;
	uint8_t bTransportModeSize;
	uint8_t *bmTransportModes;
}; // MEDIA Transport Terminal

struct uvc_pu_node_info {
	uint16_t wMaxMultiplier;
	uint8_t bControlSize;
	uint8_t *bmControls;
	uint8_t bmVideoStandards;
}; // Processing Unit

struct uvc_xu_node_info {
	uint8_t guidExtensionCode[16];
	uint8_t bNumControls;
	uint8_t bControlSize;
	uint8_t *bmControls;
	uint8_t *bmControlsType;
}; // Extension Unit

struct uvc_topo_node {
	STAILQ_ENTRY(uvc_topo_node) link;

	char node_name[64];

	uint16_t node_type;

	uint32_t flags;

	uint8_t node_id;
	// e.g.:
	// If Video Function Topology like this:
	//  o---o
	//  | 2 |
	//  o---o
	//    \___ o---o
	//     ___ | 5 |
	//    /    o---o
	//  o---o
	//  | 3 |
	//  o---o
	//
	// node 5 has two source ids,
	// one is node 2, another is node 3.
	uint16_t src_ids_num;
	uint8_t *src_ids;

	// mask of controls
	uint16_t controls_mask_len;
	uint8_t *controls_mask;

	uint32_t controls_num;
	struct uvc_ctrl_state *controls;

	// data pointed by node_info is specified by topo_type
	void *node_info;
};

static inline uint16_t
uvc_topo_node_entity_type(const struct uvc_topo_node *node)
{
	return (node->node_type & UVC_TERM_FLAG_TYPE_MASK);
}

static inline int
uvc_topo_node_is_unit(const struct uvc_topo_node *node)
{
	return ((node->node_type & 0xff00) == 0);
}

static inline int
uvc_topo_node_is_terminal(const struct uvc_topo_node *node)
{
	return (!uvc_topo_node_is_unit(node));
}

static inline int
uvc_topo_node_is_input_terminal(const struct uvc_topo_node *node)
{
	return (uvc_topo_node_is_terminal(node) &&
	    (node->node_type & UVC_TERM_FLAG_DIRECTION_MASK) ==
	    UVC_TERM_FLAG_INPUT);
}

static inline int
uvc_topo_node_is_output_terminal(const struct uvc_topo_node *node)
{
	return (uvc_topo_node_is_terminal(node) &&
	    (node->node_type & UVC_TERM_FLAG_DIRECTION_MASK) ==
	    UVC_TERM_FLAG_OUTPUT);
}

struct uvc_data_interval {
	uint32_t val;
};

struct uvc_data_frame {
/* begin interval for this frame */
	struct uvc_data_interval *interval;
	/* interval nums, desc string */
	uint8_t interval_type;

/* attribute */
	uint8_t index;
	uint8_t cap;
	uint8_t nouse;
	uint32_t min_bit_rate;
	uint32_t max_bit_rate;
	uint32_t default_interval;
	uint32_t max_buffer_size;

/* important */
	uint16_t width;
	uint16_t height;
};

struct uvc_data_format {
/* begin frame for this format */
	struct uvc_data_frame *frm;
	uint64_t nfrm;
	/* for COMPRESSED or not */
	uint64_t flags;

/* attributes */
	uint32_t fcc;
	uint8_t index;
	uint8_t bpp;
	uint8_t colorspace;
	uint8_t unuse;
	char name[64];
};

struct uvc_drv_data {
/* interface */
	struct usb_interface	*iface;
	/* for device configure iface_num + unit */
	uint8_t	iface_num;
	/* for usb framework */
	uint8_t	iface_index;
	/*for distinguishing bulk and iso uvc*/
	uint8_t num_altsetting;

	uint16_t maxpsize;

/* transfer */
	//struct usb_xfer	*xfer[UVC_N_TRANSFER];
	struct usb_xfer	*xfer[UVC_N_BULKTRANSFER];

/* data format */
	uint8_t nfmt;
	uint8_t nfrm;
	uint8_t nitv;
	uint8_t unuse[3];
	struct uvc_data_format *fmt;
};

struct uvc_drv_ctrl {
/* interface */
	struct usb_interface	*iface;
	/* for device configure iface_num + unit */
	uint8_t	iface_num;
	/* for usb framework */
	uint8_t	iface_index;
	struct mtx	mtx;

/* desc infomation */
	STAILQ_HEAD(, uvc_topo_node) topo_nodes;
	uint8_t	sid;
	uint8_t	h264id;
	uint8_t unuse;
	uint16_t	revision;
	uint16_t	clock_freq;
};

struct uvc_drv_video {
	uint64_t		unit;
	uint64_t		users;
	uint64_t		pri;
	uint64_t		detaching;
	uint64_t		teardown_done;
	uint64_t		htsf;

	struct uvc_softc	*sc;
	struct uvc_drv_ctrl	*ctrl;
	struct uvc_drv_data	*data;
	struct usb_xfer		*intr_xfer[UVCINTR_N_TRANSFER];

	struct mtx mtx;
	struct uvc_data_request	req;
	struct uvc_data_format *cur_fmt;
	struct uvc_data_frame *cur_frm;
	uint64_t enable;
	uint64_t bad_frame;
	enum v4l2_buf_type type;
	uint8_t last_fid;

	struct {
		uint8_t header[256];
		uint8_t last_fid;
		uint32_t header_size;
		uint32_t skip_payload;
		uint32_t payload_size;
		uint32_t max_payload_size;
	} bulk;

	struct uvc_v4l2 *v4l2;
	struct video_queue bq;
};

struct uvc_drv_format_desc {
	char *name;
	uint8_t guid[16];
	uint32_t fcc;
};

struct uvc_softc {
	char	name[64];
	device_t		dev;
	struct usb_device	*udev;
	struct usb_interface	*iface;

	struct uvc_drv_ctrl	*ctrl;
	/* TODO: not only one */
	struct uvc_drv_data	*data;
	struct uvc_drv_video	*video;
	enum v4l2_buf_type	type;
	//struct uvc_buf_queue	*bq;
#define UVC_QUIRK_DROP_UNCOMPRESSED_FORMAT	0x01
#define UVC_QUIRK_PROBE_MINMAX		0x02
#define UVC_QUIRK_LARGER_TRANSFER_BUF	0x04
#define UVC_QUIRK_COMMIT_IN_ADVANCE	0x08
#define UVC_QUIRK_DISABLE_HUB_U1U2	0x10
#define UVC_QUIRK_NO_EOF		0x40
	uint8_t			quirks;
};

extern int uvc_debug;

struct uvc_drv_xu_request {
	uint8_t		unit_id;
	uint8_t		control_selector;
	uint8_t		request_code;
	uint16_t	data_len;
	void		*data;
};

MALLOC_DECLARE(M_UVC);

int uvc_drv_check_video_context(struct uvc_drv_video *v, unsigned char *data,
	uint32_t len);
int uvc_drv_get_v4l2_fmt(struct uvc_drv_video *v, struct v4l2_format *f);
int uvc_drv_enum_v4l2_fmt(struct uvc_drv_video *v, struct v4l2_fmtdesc *vfmt);
int uvc_drv_enum_v4l2_framesizes(struct uvc_drv_video *v,
	struct v4l2_frmsizeenum *fs);
int uvc_drv_try_v4l2_fmt(struct uvc_drv_video *video, struct v4l2_format *vfmt,
	struct uvc_data_request *req, struct uvc_data_format **rfmt,
	struct uvc_data_frame **rfrm);
int uvc_drv_set_video(struct uvc_drv_video *video,
	struct uvc_data_request *req, struct uvc_data_format *fmt,
	struct uvc_data_frame *frm);
int uvc_drv_control_request(struct uvc_drv_video *v, uint8_t unit_id,
	uint8_t selector, uint8_t request_code, void *buffer, uint16_t data_len);
int uvc_drv_xu_query(struct uvc_drv_video *v,
	const struct uvc_drv_xu_request *req);
int uvc_drv_start_video(struct uvc_drv_video *video);
int uvc_drv_stop_video(struct uvc_drv_video *video, int close);
int uvc_drv_set_streampar(struct uvc_drv_video *v, struct v4l2_streamparm *a);
void uvc_drv_free_video(struct uvc_drv_video *v);
int uvc_drv_get_pixelaspect(void);
//uvc controls
int uvc_ctrl_init_control(struct uvc_ctrl_state *ctrl);
int uvc_ctrl_init_dev(struct uvc_softc *sc, struct uvc_drv_ctrl *ctrls);
void uvc_ctrl_destroy_mappings(struct uvc_ctrl_state *ctrl);
int uvc_ctrl_add_xu_mapping(struct uvc_drv_video *video,
	const struct fbsd_uvc_xu_map *map);
int uvc_query_v4l2_ctrl(struct uvc_drv_video *video,
			struct v4l2_queryctrl *v4l2_ctrl);
int uvc_query_v4l2_menu(struct uvc_drv_video *video,
			struct v4l2_querymenu *qm);
int uvc_get_v4l2_ctrl(struct uvc_drv_video *video,
	struct v4l2_control *control);
int uvc_set_v4l2_ctrl(struct uvc_drv_video *video,
	struct v4l2_control *control);
uint8_t uvc_quirks_lookup_local(device_t dev);
#endif /* end _DEV_USB_UVC_DRV_H */
