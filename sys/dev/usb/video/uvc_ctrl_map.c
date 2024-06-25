/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Li-Wen Hsu
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/videoio_uvc.h>

#include <dev/usb/usb.h>
#define USB_DEBUG_VAR uvc_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usbdi.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"
#include "uvc_ctrl_internal.h"

struct uvc_ctrl_leaf_recipe {
	uint32_t v4l2_id;
	const char *name;
	enum v4l2_ctrl_type type;
	uint8_t bit_offset;
	uint8_t bit_size;
	uint32_t mode_v4l2_id;
	uint32_t mode_manual_value;
	uint32_t suppressed_v4l2_ids[2];
};

struct uvc_ctrl_recipe {
	uint8_t bit_idx;
	uint8_t selector;
	uint16_t byte_size;
	uint32_t access_bits;
	uint32_t value_kind;
	uint8_t binding_count;
	const struct uvc_ctrl_menu_item *menu_items;
	uint8_t menu_count;
	struct uvc_ctrl_leaf_recipe leaves[2];
};

static const struct uvc_ctrl_menu_item uvc_exposure_mode_menu[] = {
	{ 2, "Auto Mode" },
	{ 1, "Manual Mode" },
	{ 4, "Shutter Priority Mode" },
	{ 8, "Aperture Priority Mode" },
};

static const struct uvc_ctrl_menu_item uvc_power_line_menu[] = {
	{ 0, "Disabled" },
	{ 1, "50 Hz" },
	{ 2, "60 Hz" },
};

/*
 * Leave the legacy camera-terminal relative-action controls out of the static
 * map for now.  The pre-cleanup driver only surfaced them through
 * VIDIOC_QUERYCTRL; it had no working G/S_CTRL path for them, and
 * V4L2_CID_ZOOM_CONTINUOUS was already noted as needing special get/set
 * semantics instead of a plain scalar mapping.  Re-adding them here would
 * advertise controls we still do not implement end-to-end.
 */
static const struct uvc_ctrl_recipe uvc_camera_terminal_ctrls[] = {
	{ 1, UVC_CT_AE_MODE_CONTROL, 1,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_READ_CUR |
	    UVC_CTRL_ACCESS_READ_DEF | UVC_CTRL_ACCESS_READ_RES |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_MASK, 1,
	    uvc_exposure_mode_menu, nitems(uvc_exposure_mode_menu),
	    { { V4L2_CID_EXPOSURE_AUTO, "Exposure, Auto",
		V4L2_CTRL_TYPE_MENU, 0, 4, 0, 0,
		{ V4L2_CID_EXPOSURE_ABSOLUTE, 0 } } } },
	{ 2, UVC_CT_AE_PRIORITY_CONTROL, 1,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_READ_CUR |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_BOOLEAN, 1, NULL, 0,
	    { { V4L2_CID_EXPOSURE_AUTO_PRIORITY, "Exposure, Auto Priority",
		V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 0, 0, { 0, 0 } } } },
	{ 3, UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL, 4,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_EXPOSURE_ABSOLUTE, "Exposure (Absolute)",
		V4L2_CTRL_TYPE_INTEGER, 0, 32, V4L2_CID_EXPOSURE_AUTO,
		V4L2_EXPOSURE_MANUAL, { 0, 0 } } } },
	{ 5, UVC_CT_FOCUS_ABSOLUTE_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_FOCUS_ABSOLUTE, "Focus (absolute)",
		V4L2_CTRL_TYPE_INTEGER, 0, 16, V4L2_CID_FOCUS_AUTO,
		0, { 0, 0 } } } },
	{ 7, UVC_CT_IRIS_ABSOLUTE_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_IRIS_ABSOLUTE, "Iris, Absolute",
		V4L2_CTRL_TYPE_INTEGER, 0, 16, 0, 0, { 0, 0 } } } },
	{ 9, UVC_CT_ZOOM_ABSOLUTE_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_ZOOM_ABSOLUTE, "Zoom, Absolute",
		V4L2_CTRL_TYPE_INTEGER, 0, 16, 0, 0, { 0, 0 } } } },
	{ 11, UVC_CT_PANTILT_ABSOLUTE_CONTROL, 8,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_SIGNED, 2, NULL, 0,
	    {
		{ V4L2_CID_PAN_ABSOLUTE, "Pan (Absolute)",
		    V4L2_CTRL_TYPE_INTEGER, 0, 32, 0, 0, { 0, 0 } },
		{ V4L2_CID_TILT_ABSOLUTE, "Tilt (Absolute)",
		    V4L2_CTRL_TYPE_INTEGER, 32, 32, 0, 0, { 0, 0 } },
	    } },
	{ 17, UVC_CT_FOCUS_AUTO_CONTROL, 1,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_READ_CUR |
	    UVC_CTRL_ACCESS_READ_DEF | UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_BOOLEAN, 1, NULL, 0,
	    { { V4L2_CID_FOCUS_AUTO, "Focus, Auto",
		V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 0, 0,
		{ V4L2_CID_FOCUS_ABSOLUTE, 0 } } } },
	{ 18, UVC_CT_PRIVACY_CONTROL, 1,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_READ_CUR |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_BOOLEAN, 1, NULL, 0,
	    { { V4L2_CID_PRIVACY, "Privacy", V4L2_CTRL_TYPE_BOOLEAN,
		0, 1, 0, 0, { 0, 0 } } } },
};

static const struct uvc_ctrl_recipe uvc_processing_unit_ctrls[] = {
	{ 0, UVC_PU_BRIGHTNESS_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_SIGNED, 1, NULL, 0,
	    { { V4L2_CID_BRIGHTNESS, "Brightness",
		V4L2_CTRL_TYPE_INTEGER, 0, 16, 0, 0, { 0, 0 } } } },
	{ 1, UVC_PU_CONTRAST_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_CONTRAST, "Contrast",
		V4L2_CTRL_TYPE_INTEGER, 0, 16, 0, 0, { 0, 0 } } } },
	{ 2, UVC_PU_HUE_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_SIGNED, 1, NULL, 0,
	    { { V4L2_CID_HUE, "Hue", V4L2_CTRL_TYPE_INTEGER,
		0, 16, V4L2_CID_HUE_AUTO, 0, { 0, 0 } } } },
	{ 3, UVC_PU_SATURATION_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_SATURATION, "Saturation",
		V4L2_CTRL_TYPE_INTEGER, 0, 16, 0, 0, { 0, 0 } } } },
	{ 4, UVC_PU_SHARPNESS_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_SHARPNESS, "Sharpness",
		V4L2_CTRL_TYPE_INTEGER, 0, 16, 0, 0, { 0, 0 } } } },
	{ 5, UVC_PU_GAMMA_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_GAMMA, "Gamma", V4L2_CTRL_TYPE_INTEGER,
		0, 16, 0, 0, { 0, 0 } } } },
	{ 6, UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_WHITE_BALANCE_TEMPERATURE,
		"White Balance Temperature", V4L2_CTRL_TYPE_INTEGER,
		0, 16, V4L2_CID_AUTO_WHITE_BALANCE, 0, { 0, 0 } } } },
	{ 7, UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL, 4,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE | UVC_CTRL_ACCESS_AUTO_SYNC,
	    UVC_CTRL_KIND_SIGNED, 2, NULL, 0,
	    {
		{ V4L2_CID_BLUE_BALANCE, "White Balance Blue Component",
		    V4L2_CTRL_TYPE_INTEGER, 0, 16,
		    V4L2_CID_AUTO_WHITE_BALANCE, 0, { 0, 0 } },
		{ V4L2_CID_RED_BALANCE, "White Balance Red Component",
		    V4L2_CTRL_TYPE_INTEGER, 16, 16,
		    V4L2_CID_AUTO_WHITE_BALANCE, 0, { 0, 0 } },
	    } },
	{ 8, UVC_PU_BACKLIGHT_COMPENSATION_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_BACKLIGHT_COMPENSATION,
		"Backlight Compensation", V4L2_CTRL_TYPE_INTEGER,
		0, 16, 0, 0, { 0, 0 } } } },
	{ 9, UVC_PU_GAIN_CONTROL, 2,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_RANGE |
	    UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_UNSIGNED, 1, NULL, 0,
	    { { V4L2_CID_GAIN, "Gain", V4L2_CTRL_TYPE_INTEGER,
		0, 16, 0, 0, { 0, 0 } } } },
	{ 10, UVC_PU_POWER_LINE_FREQUENCY_CONTROL, 1,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_READ_CUR |
	    UVC_CTRL_ACCESS_READ_DEF | UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_ENUM, 1,
	    uvc_power_line_menu, nitems(uvc_power_line_menu),
	    { { V4L2_CID_POWER_LINE_FREQUENCY, "Power Line Frequency",
		V4L2_CTRL_TYPE_MENU, 0, 2, 0, 0, { 0, 0 } } } },
	{ 11, UVC_PU_HUE_AUTO_CONTROL, 1,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_READ_CUR |
	    UVC_CTRL_ACCESS_READ_DEF | UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_BOOLEAN, 1, NULL, 0,
	    { { V4L2_CID_HUE_AUTO, "Hue, Auto", V4L2_CTRL_TYPE_BOOLEAN,
		0, 1, 0, 0, { V4L2_CID_HUE, 0 } } } },
	{ 12, UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL, 1,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_READ_CUR |
	    UVC_CTRL_ACCESS_READ_DEF | UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_BOOLEAN, 1, NULL, 0,
	    { { V4L2_CID_AUTO_WHITE_BALANCE,
		"White Balance Temperature, Auto", V4L2_CTRL_TYPE_BOOLEAN,
		0, 1, 0, 0, { V4L2_CID_WHITE_BALANCE_TEMPERATURE, 0 } } } },
	{ 13, UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL, 1,
	    UVC_CTRL_ACCESS_WRITE_CUR | UVC_CTRL_ACCESS_READ_CUR |
	    UVC_CTRL_ACCESS_READ_DEF | UVC_CTRL_ACCESS_RESTORE,
	    UVC_CTRL_KIND_BOOLEAN, 1, NULL, 0,
	    { { V4L2_CID_AUTO_WHITE_BALANCE,
		"White Balance Component, Auto", V4L2_CTRL_TYPE_BOOLEAN,
		0, 1, 0, 0,
		{ V4L2_CID_BLUE_BALANCE, V4L2_CID_RED_BALANCE } } } },
};

static const struct uvc_ctrl_recipe *
uvc_ctrl_recipe_lookup(enum uvc_topo_type topo_type, uint8_t bit_idx)
{
	const struct uvc_ctrl_recipe *table;
	size_t count;
	size_t i;

	switch (topo_type) {
	case UVC_TOPO_TYPE_CAMERA_TERMINAL:
		table = uvc_camera_terminal_ctrls;
		count = nitems(uvc_camera_terminal_ctrls);
		break;
	case UVC_TOPO_TYPE_PROCESSING_UNIT:
		table = uvc_processing_unit_ctrls;
		count = nitems(uvc_processing_unit_ctrls);
		break;
	default:
		return (NULL);
	}

	for (i = 0; i < count; i++) {
		if (table[i].bit_idx == bit_idx)
			return (&table[i]);
	}

	return (NULL);
}

static enum uvc_topo_type
uvc_ctrl_topology_type(uint16_t node_type)
{
	switch (node_type) {
	case UVC_ITT_CAMERA:
		return (UVC_TOPO_TYPE_CAMERA_TERMINAL);
	case UVC_VC_PROCESSING_UNIT:
		return (UVC_TOPO_TYPE_PROCESSING_UNIT);
	default:
		return (UVC_TOPO_TYPE_UNKNOWN);
	}
}

static int
uvc_ctrl_copy_menu(struct uvc_ctrl_meta *meta,
    const struct uvc_ctrl_recipe *recipe)
{
	size_t bytes;

	if (recipe->menu_count == 0)
		return (0);

	bytes = recipe->menu_count * sizeof(*recipe->menu_items);
	meta->menus[0].menu_data = malloc(bytes, M_UVC, M_WAITOK);
	if (meta->menus[0].menu_data == NULL)
		return (ENOMEM);

	memcpy(meta->menus[0].menu_data, recipe->menu_items, bytes);
	meta->menus[0].item_num = recipe->menu_count;
	return (0);
}

int
uvc_ctrl_init_control(struct uvc_ctrl_state *ctrl)
{
	struct uvc_ctrl_meta *meta;
	const struct uvc_ctrl_recipe *recipe;
	enum uvc_topo_type topo_type;
	uint16_t node_type;
	uint8_t i;
	int error;

	node_type = uvc_topo_node_entity_type(ctrl->topo_node);
	topo_type = uvc_ctrl_topology_type(node_type);
	recipe = uvc_ctrl_recipe_lookup(topo_type, ctrl->bit_index);
	if (recipe == NULL)
		return (EINVAL);

	meta = &ctrl->meta;
	memset(meta, 0, sizeof(*meta));
	meta->bit_index = ctrl->bit_index;
	meta->topo_type = topo_type;
	meta->selector = recipe->selector;
	meta->byte_size = recipe->byte_size;
	meta->access_bits = recipe->access_bits;
	meta->value_kind = recipe->value_kind;
	meta->binding_count = recipe->binding_count;

	ctrl->shadow_data = malloc(recipe->byte_size * UVC_CTRL_SLOT_COUNT + 1,
	    M_UVC, M_ZERO | M_WAITOK);
	if (ctrl->shadow_data == NULL)
		return (ENOMEM);

	for (i = 0; i < recipe->binding_count; i++) {
		struct uvc_ctrl_binding *binding;

		binding = malloc(sizeof(*binding), M_UVC, M_ZERO | M_WAITOK);
		if (binding == NULL) {
			error = ENOMEM;
			goto fail;
		}

		binding->meta = meta;
		binding->v4l2_id = recipe->leaves[i].v4l2_id;
		strlcpy(binding->v4l2_name, recipe->leaves[i].name,
		    sizeof(binding->v4l2_name));
		binding->v4l2_type = recipe->leaves[i].type;
		binding->value_kind = recipe->value_kind;
		binding->bit_offset = recipe->leaves[i].bit_offset;
		binding->bit_size = recipe->leaves[i].bit_size;
		binding->mode_v4l2_id = recipe->leaves[i].mode_v4l2_id;
		binding->mode_manual_value = recipe->leaves[i].mode_manual_value;
		binding->suppressed_v4l2_ids[0] =
		    recipe->leaves[i].suppressed_v4l2_ids[0];
		binding->suppressed_v4l2_ids[1] =
		    recipe->leaves[i].suppressed_v4l2_ids[1];
		meta->bindings[i] = binding;
	}

	error = uvc_ctrl_copy_menu(meta, recipe);
	if (error != 0)
		goto fail;

	ctrl->state_bits |= UVC_CTRL_STATE_READY;
	return (0);

fail:
	uvc_ctrl_destroy_mappings(ctrl);
	if (ctrl->shadow_data != NULL) {
		free(ctrl->shadow_data, M_UVC);
		ctrl->shadow_data = NULL;
	}
	return (error);
}

static int
uvc_ctrl_dynamic_type_supported(uint32_t v4l2_type)
{
	switch (v4l2_type) {
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
		return (1);
	default:
		return (0);
	}
}

static int
uvc_ctrl_dynamic_kind_supported(uint32_t data_kind)
{
	switch (data_kind) {
	case FBSD_UVC_XU_DATA_RAW:
	case FBSD_UVC_XU_DATA_SIGNED:
	case FBSD_UVC_XU_DATA_UNSIGNED:
	case FBSD_UVC_XU_DATA_BOOLEAN:
	case FBSD_UVC_XU_DATA_ENUM:
	case FBSD_UVC_XU_DATA_BITMASK:
		return (1);
	default:
		return (0);
	}
}

static struct uvc_ctrl_state *
uvc_ctrl_find_xu_control(struct uvc_drv_ctrl *ctrls, const uint8_t unit_guid[16],
    uint8_t selector)
{
	struct uvc_topo_node *topo_node, *tmp;
	struct uvc_xu_node_info *node_info;
	struct uvc_ctrl_state *ctrl;
	uint32_t i;

	STAILQ_FOREACH_SAFE(topo_node, &ctrls->topo_nodes, link, tmp) {
		if (uvc_topo_node_entity_type(topo_node) != UVC_VC_EXTENSION_UNIT)
			continue;

		node_info = topo_node->node_info;
		if (node_info == NULL)
			continue;
		if (memcmp(node_info->guidExtensionCode, unit_guid, 16) != 0)
			continue;
		if (selector == 0 || selector > node_info->bNumControls)
			return (NULL);

		for (i = 0; i < topo_node->controls_num; i++) {
			ctrl = &topo_node->controls[i];
			if (ctrl->bit_index + 1 == selector)
				return (ctrl);
		}
		return (NULL);
	}

	return (NULL);
}

static int
uvc_ctrl_v4l2_id_exists(struct uvc_drv_ctrl *ctrls, uint32_t v4l2_id)
{
	struct uvc_topo_node *topo_node, *tmp;
	struct uvc_ctrl_state *ctrl;
	uint32_t i, j;

	STAILQ_FOREACH_SAFE(topo_node, &ctrls->topo_nodes, link, tmp) {
		for (i = 0; i < topo_node->controls_num; i++) {
			ctrl = &topo_node->controls[i];
			for (j = 0; j < UVC_CTRL_SLOT_LIMIT; j++) {
				if (ctrl->meta.bindings[j] == NULL)
					continue;
				if (ctrl->meta.bindings[j]->v4l2_id == v4l2_id)
					return (1);
			}
		}
	}

	return (0);
}

static int
uvc_ctrl_dynamic_slot_alloc(const struct uvc_ctrl_state *ctrl)
{
	uint8_t i;

	for (i = 0; i < UVC_CTRL_SLOT_LIMIT; i++) {
		if (ctrl->meta.bindings[i] == NULL)
			return (i);
	}

	return (-1);
}

static int
uvc_ctrl_probe_xu_access(struct uvc_drv_video *video, uint8_t node_id,
    uint8_t selector, uint16_t byte_size, uint32_t *access_bits)
{
	uint8_t *probe;
	uint8_t info;
	int ret;

	*access_bits = 0;

	ret = uvc_drv_control_request(video, node_id, selector, UVC_GET_INFO,
	    &info, sizeof(info));
	if (ret != 0)
		return (ret);

	if ((info & 0x01) != 0)
		*access_bits |= UVC_CTRL_ACCESS_READ_CUR;
	if ((info & 0x02) != 0)
		*access_bits |= UVC_CTRL_ACCESS_WRITE_CUR;

	if ((*access_bits & UVC_CTRL_ACCESS_READ_CUR) == 0)
		return (0);

	probe = malloc(byte_size, M_UVC, M_WAITOK);
	if (probe == NULL)
		return (ENOMEM);
	memset(probe, 0, byte_size);
	if (uvc_drv_control_request(video, node_id, selector, UVC_GET_MIN, probe,
	    byte_size) == 0)
		*access_bits |= UVC_CTRL_ACCESS_READ_MIN;
	if (uvc_drv_control_request(video, node_id, selector, UVC_GET_MAX, probe,
	    byte_size) == 0)
		*access_bits |= UVC_CTRL_ACCESS_READ_MAX;
	if (uvc_drv_control_request(video, node_id, selector, UVC_GET_RES, probe,
	    byte_size) == 0)
		*access_bits |= UVC_CTRL_ACCESS_READ_RES;
	if (uvc_drv_control_request(video, node_id, selector, UVC_GET_DEF, probe,
	    byte_size) == 0)
		*access_bits |= UVC_CTRL_ACCESS_READ_DEF;

	free(probe, M_UVC);
	return (0);
}

static int
uvc_ctrl_prepare_xu_control(struct uvc_drv_video *video,
    struct uvc_ctrl_state *ctrl, uint8_t selector, uint32_t data_kind,
    struct uvc_ctrl_meta *meta, uint8_t **shadow_data)
{
	uint8_t lenbuf[2];
	uint16_t byte_size;
	int ret;

	memset(meta, 0, sizeof(*meta));
	meta->bit_index = ctrl->bit_index;
	meta->topo_type = UVC_TOPO_TYPE_EXTENSION_UNIT;
	meta->selector = selector;
	meta->value_kind = data_kind;

	ret = uvc_drv_control_request(video, ctrl->topo_node->node_id, selector,
	    UVC_GET_LEN, lenbuf, sizeof(lenbuf));
	if (ret != 0)
		return (ret);

	byte_size = lenbuf[0] | (lenbuf[1] << 8);
	if (byte_size == 0 || byte_size > FBSD_UVC_XU_MAX_DATA)
		return (ENXIO);

	meta->byte_size = byte_size;
	*shadow_data = malloc(byte_size * UVC_CTRL_SLOT_COUNT + 1, M_UVC,
	    M_ZERO | M_WAITOK);
	if (*shadow_data == NULL)
		return (ENOMEM);

	ret = uvc_ctrl_probe_xu_access(video, ctrl->topo_node->node_id, selector,
	    byte_size, &meta->access_bits);
	if (ret != 0) {
		free(*shadow_data, M_UVC);
		*shadow_data = NULL;
		memset(meta, 0, sizeof(*meta));
		return (ret);
	}

	return (0);
}

static int
uvc_ctrl_copy_dynamic_menu(const struct fbsd_uvc_xu_map *map,
    struct uvc_ctrl_menu *menu)
{
	struct fbsd_uvc_xu_menu_entry *src;
	struct uvc_ctrl_menu_item *dst;
	uint64_t menu_ptr;
	size_t bytes;
	uint32_t i;
	int ret;

	if (map->menu_num == 0)
		return (0);

	menu_ptr = map->menu_ptr;
	if (menu_ptr == 0)
		return (EINVAL);
	if (map->menu_num > UINT32_MAX / sizeof(*src))
		return (EINVAL);

	bytes = map->menu_num * sizeof(*src);
	src = malloc(bytes, M_UVC, M_WAITOK);
	if (src == NULL)
		return (ENOMEM);
	ret = copyin((void *)(uintptr_t)menu_ptr, src, bytes);
	if (ret != 0) {
		free(src, M_UVC);
		return (ret);
	}

	dst = malloc(map->menu_num * sizeof(*dst), M_UVC, M_ZERO | M_WAITOK);
	if (dst == NULL) {
		free(src, M_UVC);
		return (ENOMEM);
	}

	for (i = 0; i < map->menu_num; i++) {
		dst[i].value = src[i].item_value;
		strlcpy(dst[i].name, (const char *)src[i].item_name,
		    sizeof(dst[i].name));
	}

	free(src, M_UVC);
	menu->item_num = map->menu_num;
	menu->menu_data = dst;
	return (0);
}

int
uvc_ctrl_add_xu_mapping(struct uvc_drv_video *video, const struct fbsd_uvc_xu_map *map)
{
	struct uvc_ctrl_meta prepared_meta;
	struct uvc_ctrl_menu prepared_menu;
	struct uvc_ctrl_binding *binding = NULL;
	struct uvc_ctrl_state *ctrl = NULL;
	uint8_t *shadow_data = NULL;
	int slot = -1;
	int need_prepare = 0;
	int ret;

	if (!uvc_ctrl_dynamic_type_supported(map->v4l2_type) ||
	    !uvc_ctrl_dynamic_kind_supported(map->data_kind))
		return (EINVAL);
	if (map->control_selector == 0 || map->control_size_bits == 0)
		return (EINVAL);
	if (map->control_size_bits > 32)
		return (EINVAL);
	if (map->v4l2_type == V4L2_CTRL_TYPE_MENU) {
		if (map->menu_num == 0)
			return (EINVAL);
	} else if (map->menu_num != 0 || map->menu_ptr != 0) {
		return (EINVAL);
	}

	memset(&prepared_meta, 0, sizeof(prepared_meta));
	memset(&prepared_menu, 0, sizeof(prepared_menu));

	mtx_lock(&video->ctrl->mtx);
	if (uvc_ctrl_v4l2_id_exists(video->ctrl, map->v4l2_id)) {
		ret = EEXIST;
		goto done;
	}

	ctrl = uvc_ctrl_find_xu_control(video->ctrl, map->unit_guid,
	    map->control_selector);
	if (ctrl == NULL) {
		ret = EINVAL;
		goto done;
	}

	need_prepare = !UVC_CTRL_STATE_IS_READY(ctrl);
	mtx_unlock(&video->ctrl->mtx);

	if (need_prepare) {
		ret = uvc_ctrl_prepare_xu_control(video, ctrl, map->control_selector,
		    map->data_kind, &prepared_meta, &shadow_data);
		if (ret != 0)
			return (ret);
	}

	binding = malloc(sizeof(*binding), M_UVC, M_ZERO | M_WAITOK);
	if (binding == NULL) {
		ret = ENOMEM;
		goto cleanup_prepare;
	}

	ret = uvc_ctrl_copy_dynamic_menu(map, &prepared_menu);
	if (ret != 0)
		goto cleanup_binding;

	mtx_lock(&video->ctrl->mtx);
	if (uvc_ctrl_v4l2_id_exists(video->ctrl, map->v4l2_id)) {
		ret = EEXIST;
		goto done;
	}

	ctrl = uvc_ctrl_find_xu_control(video->ctrl, map->unit_guid,
	    map->control_selector);
	if (ctrl == NULL) {
		ret = EINVAL;
		goto done;
	}

	if ((uint32_t)map->control_offset_bits + map->control_size_bits >
	    (need_prepare ? prepared_meta.byte_size : ctrl->meta.byte_size) * 8U) {
		ret = EOVERFLOW;
		goto done;
	}

	slot = uvc_ctrl_dynamic_slot_alloc(ctrl);
	if (slot < 0) {
		ret = ENOMEM;
		goto done;
	}

	if (!UVC_CTRL_STATE_IS_READY(ctrl)) {
		if (!need_prepare) {
			ret = EINVAL;
			goto done;
		}
		ctrl->meta = prepared_meta;
		ctrl->shadow_data = shadow_data;
		shadow_data = NULL;
		ctrl->state_bits |= UVC_CTRL_STATE_READY;
	} else if (need_prepare && shadow_data != NULL) {
		free(shadow_data, M_UVC);
		shadow_data = NULL;
	}

	binding->meta = &ctrl->meta;
	binding->v4l2_id = map->v4l2_id;
	strlcpy(binding->v4l2_name, (const char *)map->control_name,
	    sizeof(binding->v4l2_name));
	binding->v4l2_type = map->v4l2_type;
	binding->value_kind = map->data_kind;
	binding->bit_offset = map->control_offset_bits;
	binding->bit_size = map->control_size_bits;

	ctrl->meta.bindings[slot] = binding;
	ctrl->meta.menus[slot] = prepared_menu;
	memset(&prepared_menu, 0, sizeof(prepared_menu));
	if (ctrl->meta.binding_count < slot + 1)
		ctrl->meta.binding_count = slot + 1;
	ret = 0;

done:
	mtx_unlock(&video->ctrl->mtx);
cleanup_binding:
	if (ret != 0 && binding != NULL)
		free(binding, M_UVC);
cleanup_prepare:
	if (ret != 0 && shadow_data != NULL)
		free(shadow_data, M_UVC);
	if (ret != 0 && prepared_menu.menu_data != NULL)
		free(prepared_menu.menu_data, M_UVC);
	return (ret);
}
