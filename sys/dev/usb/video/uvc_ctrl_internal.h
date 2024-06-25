/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026
 */

#ifndef _DEV_USB_VIDEO_UVC_CTRL_INTERNAL_H_
#define _DEV_USB_VIDEO_UVC_CTRL_INTERNAL_H_

#include <contrib/v4l/videodev2.h>

#define UVC_CTRL_SLOT_LIMIT 5

enum uvc_ctrl_value_slot {
	UVC_CTRL_SLOT_CURRENT = 0,
	UVC_CTRL_SLOT_BACKUP,
	UVC_CTRL_SLOT_MINIMUM,
	UVC_CTRL_SLOT_MAXIMUM,
	UVC_CTRL_SLOT_RESOLUTION,
	UVC_CTRL_SLOT_DEFAULT,
	UVC_CTRL_SLOT_COUNT,
};

enum uvc_ctrl_value_kind {
	UVC_CTRL_KIND_RAW = 0,
	UVC_CTRL_KIND_SIGNED,
	UVC_CTRL_KIND_UNSIGNED,
	UVC_CTRL_KIND_BOOLEAN,
	UVC_CTRL_KIND_ENUM,
	UVC_CTRL_KIND_MASK,
};

enum uvc_ctrl_access_bits {
	UVC_CTRL_ACCESS_WRITE_CUR = 0x0001,
	UVC_CTRL_ACCESS_READ_CUR = 0x0002,
	UVC_CTRL_ACCESS_READ_MIN = 0x0004,
	UVC_CTRL_ACCESS_READ_MAX = 0x0008,
	UVC_CTRL_ACCESS_READ_RES = 0x0010,
	UVC_CTRL_ACCESS_READ_DEF = 0x0020,
	UVC_CTRL_ACCESS_RESTORE = 0x0040,
	UVC_CTRL_ACCESS_AUTO_SYNC = 0x0080,
	UVC_CTRL_ACCESS_ASYNC = 0x0100,
};

#define UVC_CTRL_ACCESS_RANGE \
	(UVC_CTRL_ACCESS_READ_CUR | UVC_CTRL_ACCESS_READ_MIN | \
	UVC_CTRL_ACCESS_READ_MAX | UVC_CTRL_ACCESS_READ_RES | \
	UVC_CTRL_ACCESS_READ_DEF)

enum uvc_ctrl_state_bits {
	UVC_CTRL_STATE_DIRTY = 0x01,
	UVC_CTRL_STATE_PAYLOAD_LOADED = 0x02,
	UVC_CTRL_STATE_NEEDS_SYNC = 0x04,
	UVC_CTRL_STATE_CACHE_VALID = 0x08,
	UVC_CTRL_STATE_READY = 0x10,
};

struct uvc_ctrl_menu_item {
	uint32_t value;
	char name[32];
};

struct uvc_ctrl_menu {
	uint32_t item_num;
	struct uvc_ctrl_menu_item *menu_data;
};

struct uvc_ctrl_binding;

struct uvc_ctrl_meta {
	uint8_t bit_index;
	enum uvc_topo_type topo_type;
	uint8_t selector;
	uint16_t byte_size;
	uint32_t access_bits;
	uint32_t value_kind;
	uint8_t binding_count;
	struct uvc_ctrl_binding *bindings[UVC_CTRL_SLOT_LIMIT];
	struct uvc_ctrl_menu menus[UVC_CTRL_SLOT_LIMIT];
	uint8_t selected_binding;
};

struct uvc_ctrl_binding {
	struct uvc_ctrl_meta *meta;
	uint32_t v4l2_id;
	uint8_t v4l2_name[32];
	enum v4l2_ctrl_type v4l2_type;
	uint32_t value_kind;
	uint8_t bit_offset;
	uint8_t bit_size;
	uint32_t mode_v4l2_id;
	uint32_t mode_manual_value;
	uint32_t suppressed_v4l2_ids[2];
	int32_t (*decode)(struct uvc_ctrl_binding *binding, uint8_t query,
	    const uint8_t *data);
	void (*encode)(struct uvc_ctrl_binding *binding, int32_t value,
	    uint8_t *data);
};

struct uvc_ctrl_state {
	struct uvc_topo_node *topo_node;
	struct uvc_ctrl_meta meta;
	uint8_t bit_index;
	uint8_t state_bits;
	uint8_t *shadow_data;
	struct uvc_drv_video *owner;
};

#define UVC_CTRL_STATE_IS_READY(ctrl) \
	(((ctrl)->state_bits & UVC_CTRL_STATE_READY) != 0)

#endif /* _DEV_USB_VIDEO_UVC_CTRL_INTERNAL_H_ */
