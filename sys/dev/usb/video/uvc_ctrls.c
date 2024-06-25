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

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"
#include "uvc_buf.h"
#include "uvc_ctrl_internal.h"
#include "uvc_v4l2.h"

#define UVC_BITMASK	      0x7
#define UVC_BITSHIFT	      0x3
#define UVC_VALMASK	      0x1

static unsigned int
uvc_test_bit(const uint8_t *buf, int b)
{
	return (buf[b >> UVC_BITSHIFT] >> (b & UVC_BITMASK)) & UVC_VALMASK;
}

static int
uvc_ctrl_slot_request(enum uvc_ctrl_value_slot slot, uint8_t *request_code)
{
	switch (slot) {
	case UVC_CTRL_SLOT_CURRENT:
		*request_code = UVC_GET_CUR;
		return (0);
	case UVC_CTRL_SLOT_MINIMUM:
		*request_code = UVC_GET_MIN;
		return (0);
	case UVC_CTRL_SLOT_MAXIMUM:
		*request_code = UVC_GET_MAX;
		return (0);
	case UVC_CTRL_SLOT_RESOLUTION:
		*request_code = UVC_GET_RES;
		return (0);
	case UVC_CTRL_SLOT_DEFAULT:
		*request_code = UVC_GET_DEF;
		return (0);
	default:
		return (EINVAL);
	}
}

static uint64_t
uvc_ctrl_extract_bits(const uint8_t *data, uint8_t bit_offset, uint8_t bit_size)
{
	uint64_t value;
	uint8_t bit;

	value = 0;
	for (bit = 0; bit < bit_size; bit++) {
		if ((data[(bit_offset + bit) >> UVC_BITSHIFT] >>
		    ((bit_offset + bit) & UVC_BITMASK)) & UVC_VALMASK)
			value |= 1ULL << bit;
	}

	return (value);
}

static void
uvc_ctrl_insert_bits(uint8_t *data, uint8_t bit_offset, uint8_t bit_size,
    uint64_t value)
{
	uint8_t bit;

	for (bit = 0; bit < bit_size; bit++) {
		uint8_t *target;
		uint8_t mask;

		target = &data[(bit_offset + bit) >> UVC_BITSHIFT];
		mask = 1U << ((bit_offset + bit) & UVC_BITMASK);
		if ((value >> bit) & 1U)
			*target |= mask;
		else
			*target &= ~mask;
	}
}

static int
uvc_ctrl_menu_raw_to_index(const struct uvc_ctrl_menu *menu, uint32_t raw_value,
    int32_t *index)
{
	uint32_t i;

	if (menu == NULL || menu->menu_data == NULL)
		return (EINVAL);

	for (i = 0; i < menu->item_num; i++) {
		if (menu->menu_data[i].value == raw_value) {
			*index = i;
			return (0);
		}
	}

	return (EINVAL);
}

static int
uvc_ctrl_menu_index_to_raw(const struct uvc_ctrl_menu *menu, int32_t index,
    uint32_t *raw_value)
{
	if (menu == NULL || menu->menu_data == NULL)
		return (EINVAL);
	if (index < 0 || (uint32_t)index >= menu->item_num)
		return (EINVAL);

	*raw_value = menu->menu_data[index].value;
	return (0);
}

static int
uvc_ctrl_decode_value(const struct uvc_ctrl_binding *binding,
    const struct uvc_ctrl_menu *menu, const uint8_t *data, int32_t *value)
{
	uint64_t raw_value;

	raw_value = uvc_ctrl_extract_bits(data, binding->bit_offset,
	    binding->bit_size);

	switch (binding->v4l2_type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
		*value = raw_value != 0;
		return (0);
	case V4L2_CTRL_TYPE_MENU:
		return (uvc_ctrl_menu_raw_to_index(menu, raw_value, value));
	case V4L2_CTRL_TYPE_INTEGER:
		switch (binding->value_kind) {
		case UVC_CTRL_KIND_SIGNED:
			if (binding->bit_size < 32 &&
			    (raw_value & (1ULL << (binding->bit_size - 1))) != 0)
				raw_value |= ~((1ULL << binding->bit_size) - 1);
			*value = (int32_t)raw_value;
			return (0);
		default:
			*value = (int32_t)raw_value;
			return (0);
		}
	default:
		return (EINVAL);
	}
}

static int
uvc_ctrl_integer_value_check(const struct uvc_ctrl_binding *binding,
    int32_t value)
{
	int64_t min_value, max_value;
	uint64_t max_unsigned;

	if (binding->bit_size == 0 || binding->bit_size > 32)
		return (EINVAL);

	switch (binding->value_kind) {
	case UVC_CTRL_KIND_SIGNED:
		if (binding->bit_size == 32)
			return (0);
		max_value = (1LL << (binding->bit_size - 1)) - 1;
		min_value = -(1LL << (binding->bit_size - 1));
		if ((int64_t)value < min_value || (int64_t)value > max_value)
			return (ERANGE);
		return (0);
	default:
		if (value < 0)
			return (ERANGE);
		if (binding->bit_size == 32)
			return (0);
		max_unsigned = (1ULL << binding->bit_size) - 1;
		if ((uint64_t)(uint32_t)value > max_unsigned)
			return (ERANGE);
		return (0);
	}
}

static int
uvc_ctrl_encode_value(const struct uvc_ctrl_binding *binding,
    const struct uvc_ctrl_menu *menu, int32_t value, uint8_t *data)
{
	uint64_t raw_value;
	int ret;

	switch (binding->v4l2_type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
		raw_value = value != 0;
		break;
	case V4L2_CTRL_TYPE_MENU: {
		uint32_t menu_value;

		ret = uvc_ctrl_menu_index_to_raw(menu, value, &menu_value);
		if (ret != 0)
			return (ret);
		raw_value = menu_value;
		break;
	}
	case V4L2_CTRL_TYPE_INTEGER:
		ret = uvc_ctrl_integer_value_check(binding, value);
		if (ret != 0)
			return (ret);
		raw_value = (uint32_t)value;
		break;
	default:
		return (EINVAL);
	}

	if (binding->bit_size < 64 &&
	    raw_value >= (1ULL << binding->bit_size) &&
	    binding->v4l2_type != V4L2_CTRL_TYPE_INTEGER)
		return (ERANGE);

	uvc_ctrl_insert_bits(data, binding->bit_offset, binding->bit_size,
	    raw_value);
	return (0);
}

static int
uvc_ctrl_load_slot(struct uvc_drv_video *video, uint8_t node_id,
    uint8_t selector, uint16_t byte_size, enum uvc_ctrl_value_slot slot,
    uint8_t *data)
{
	uint8_t request_code;
	int ret;

	ret = uvc_ctrl_slot_request(slot, &request_code);
	if (ret != 0)
		return (ret);

	return (uvc_drv_control_request(video, node_id, selector, request_code,
	    data, byte_size));
}

static int
uvc_ctrl_query_value(struct uvc_drv_video *video, uint8_t node_id,
    uint8_t selector, uint16_t byte_size,
    const struct uvc_ctrl_binding *binding, const struct uvc_ctrl_menu *menu,
    enum uvc_ctrl_value_slot slot, int32_t *value)
{
	uint8_t *data;
	int ret;

	if (byte_size == 0)
		return (EINVAL);

	data = malloc(byte_size, M_UVC, M_WAITOK);
	if (data == NULL)
		return (ENOMEM);

	ret = uvc_ctrl_load_slot(video, node_id, selector, byte_size, slot, data);
	if (ret != 0)
		goto out;

	ret = uvc_ctrl_decode_value(binding, menu, data, value);
out:
	free(data, M_UVC);
	return (ret);
}

static int
uvc_ctrl_store_value(struct uvc_drv_video *video, uint8_t node_id,
    uint8_t selector, uint16_t byte_size,
    const struct uvc_ctrl_binding *binding, const struct uvc_ctrl_menu *menu,
    uint32_t access_bits, int32_t value)
{
	uint8_t *data;
	int ret;

	if (byte_size == 0)
		return (EINVAL);

	data = malloc(byte_size, M_UVC, M_WAITOK);
	if (data == NULL)
		return (ENOMEM);

	if (binding->bit_offset != 0 ||
	    binding->bit_size != byte_size * 8U) {
		if ((access_bits & UVC_CTRL_ACCESS_READ_CUR) == 0) {
			ret = EINVAL;
			goto out;
		}
		ret = uvc_ctrl_load_slot(video, node_id, selector, byte_size,
		    UVC_CTRL_SLOT_CURRENT, data);
		if (ret != 0)
			goto out;
	} else {
		memset(data, 0, byte_size);
	}

	ret = uvc_ctrl_encode_value(binding, menu, value, data);
	if (ret != 0)
		goto out;

	ret = uvc_drv_control_request(video, node_id, selector, UVC_SET_CUR,
	    data, byte_size);
out:
	free(data, M_UVC);
	return (ret);
}

void
uvc_ctrl_destroy_mappings(struct uvc_ctrl_state *ctrl)
{
	uint8_t i = 0;

	if (!ctrl)
		return;

	for (i = 0; i < UVC_CTRL_SLOT_LIMIT; i++) {
		if (ctrl->meta.bindings[i] != NULL) {
			DPRINTF("removing v4l2 mapping '%s'\n",
				ctrl->meta.bindings[i]->v4l2_name);

			free(ctrl->meta.bindings[i], M_UVC);
			ctrl->meta.bindings[i] = NULL;
		}

		if (ctrl->meta.menus[i].menu_data != NULL) {
			free(ctrl->meta.menus[i].menu_data, M_UVC);
			ctrl->meta.menus[i].menu_data = NULL;
		}
	}

	ctrl->meta.binding_count = 0;

	return;
}

static unsigned int
uvc_ctrl_count_control(const uint8_t *bmCtrls, uint8_t bCtrlSize)
{
	int i = 0;
	unsigned int count = 0;
	const uint8_t *data = bmCtrls;

	if (data == NULL || bCtrlSize == 0)
		return (0);

	for (i = 0; i < bCtrlSize * 8; i++) {
		if ((data[i >> UVC_BITSHIFT] >>
		     (i & UVC_BITMASK)) & UVC_VALMASK)
			count++;
	}
	return (count);
}

int
uvc_ctrl_init_dev(struct uvc_softc *sc, struct uvc_drv_ctrl *ctrls)
{
	struct uvc_topo_node *topo_node, *tmp;
	struct uvc_ctrl_state *ctrl = NULL;
	uint8_t bCtrlSize = 0;
	uint32_t nctrls = 0;
	uint8_t *bmCtrls = NULL;
	uint8_t i = 0;

	struct uvc_xu_node_info *node_info_xu = NULL;
	struct uvc_pu_node_info *node_info_pu = NULL;
	struct uvc_ct_node_info *node_info_ct = NULL;

	if (!ctrls) {
		return (EINVAL);
	}

	STAILQ_FOREACH_SAFE(topo_node, &ctrls->topo_nodes, link, tmp) {
		if (uvc_topo_node_entity_type(topo_node) == UVC_VC_EXTENSION_UNIT) {
			node_info_xu =
			    (struct uvc_xu_node_info *)topo_node->node_info;

			bmCtrls = node_info_xu->bmControls;
			bCtrlSize = node_info_xu->bControlSize;
		} else if (uvc_topo_node_entity_type(topo_node) ==
		    UVC_VC_PROCESSING_UNIT) {
			node_info_pu =
			    (struct uvc_pu_node_info *)topo_node->node_info;

			bmCtrls = node_info_pu->bmControls;
			bCtrlSize = node_info_pu->bControlSize;
		} else if (uvc_topo_node_entity_type(topo_node) == UVC_ITT_CAMERA) {
			node_info_ct =
			    (struct uvc_ct_node_info *)topo_node->node_info;

			bmCtrls = node_info_ct->bmControls;
			bCtrlSize = node_info_ct->bControlSize;
		} else
			continue;

		nctrls = uvc_ctrl_count_control(bmCtrls, bCtrlSize);
		if (nctrls == 0)
			continue;
		topo_node->controls = malloc(nctrls * sizeof(*ctrl), M_UVC,
		    M_ZERO | M_WAITOK);
		if (!topo_node->controls)
			return (ENOMEM);
		topo_node->controls_num = nctrls;

		ctrl = topo_node->controls;
		for (i = 0; i < bCtrlSize * 8; i++) {
			if (uvc_test_bit(bmCtrls, i) == 0)
				continue;

			ctrl->topo_node = topo_node;
			ctrl->bit_index = i;

			uvc_ctrl_init_control(ctrl);

			ctrl++;
		}
	}

	return (0);
}

static void
uvc_ctrl_fill_menu_query(struct v4l2_queryctrl *query,
    const struct uvc_ctrl_menu *menu)
{
	uint32_t i;

	query->minimum = 0;
	query->maximum = menu->item_num - 1;
	query->step = 1;

	for (i = 0; i < menu->item_num; i++) {
		if (menu->menu_data[i].value == query->default_value) {
			query->default_value = i;
			break;
		}
	}
}

static int
uvc_ctrl_describe_binding(struct uvc_drv_video *video,
    const struct uvc_ctrl_binding *binding,
    const struct uvc_ctrl_menu *menu, uint32_t access_bits, uint8_t node_id,
    uint8_t selector, uint16_t byte_size, struct v4l2_queryctrl *query)
{
	int32_t value;

	memset(query, 0, sizeof(*query));

	if (binding == NULL)
		return (EINVAL);

	query->id = binding->v4l2_id;
	query->type = binding->v4l2_type;
	strlcpy(query->name, binding->v4l2_name, sizeof(query->name));

	query->flags = 0;
	if ((access_bits & UVC_CTRL_ACCESS_WRITE_CUR) == 0)
		query->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	if ((access_bits & UVC_CTRL_ACCESS_READ_CUR) == 0)
		query->flags |= V4L2_CTRL_FLAG_WRITE_ONLY;
	query->step = 1;

	switch (binding->v4l2_type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
		query->minimum = 0;
		query->maximum = 1;
		query->step = 1;
		if ((access_bits & UVC_CTRL_ACCESS_READ_DEF) != 0 &&
		    uvc_ctrl_query_value(video, node_id, selector, byte_size,
		    binding, menu, UVC_CTRL_SLOT_DEFAULT, &value) == 0)
			query->default_value = value;
		return (0);

	case V4L2_CTRL_TYPE_MENU:
		if (menu->menu_data == NULL)
			return (EINVAL);
		if ((access_bits & UVC_CTRL_ACCESS_READ_DEF) != 0 &&
		    uvc_ctrl_query_value(video, node_id, selector, byte_size,
		    binding, menu, UVC_CTRL_SLOT_DEFAULT, &value) == 0)
			query->default_value = value;
		uvc_ctrl_fill_menu_query(query, menu);
		return (0);

	case V4L2_CTRL_TYPE_INTEGER:
		if ((access_bits & UVC_CTRL_ACCESS_READ_MIN) != 0 &&
		    uvc_ctrl_query_value(video, node_id, selector, byte_size,
		    binding, menu, UVC_CTRL_SLOT_MINIMUM, &value) == 0)
			query->minimum = value;
		if ((access_bits & UVC_CTRL_ACCESS_READ_MAX) != 0 &&
		    uvc_ctrl_query_value(video, node_id, selector, byte_size,
		    binding, menu, UVC_CTRL_SLOT_MAXIMUM, &value) == 0)
			query->maximum = value;
		if ((access_bits & UVC_CTRL_ACCESS_READ_RES) != 0 &&
		    uvc_ctrl_query_value(video, node_id, selector, byte_size,
		    binding, menu, UVC_CTRL_SLOT_RESOLUTION, &value) == 0 &&
		    value > 0)
			query->step = value;
		if ((access_bits & UVC_CTRL_ACCESS_READ_DEF) != 0 &&
		    uvc_ctrl_query_value(video, node_id, selector, byte_size,
		    binding, menu, UVC_CTRL_SLOT_DEFAULT, &value) == 0)
			query->default_value = value;
		return (0);

	default:
		return (EINVAL);
	}
}

static struct uvc_ctrl_state *
uvc_search_control_sub(struct uvc_topo_node *topo_node, uint32_t v4l2_id)
{
	unsigned int i;
	unsigned int j;

	struct uvc_ctrl_state *ctrl = NULL;

	if (topo_node == NULL) {
		return (NULL);
	}

	for (i = 0; i < topo_node->controls_num; ++i) {
		ctrl = &topo_node->controls[i];

		if (!UVC_CTRL_STATE_IS_READY(ctrl))
			continue;

		for (j = 0; j < UVC_CTRL_SLOT_LIMIT; j++) {
			if (ctrl->meta.bindings[j] == NULL)
				continue;

			if (ctrl->meta.bindings[j]->v4l2_id == v4l2_id) {
				ctrl->meta.selected_binding = j;
				return (ctrl);
			}
		}
	}

	return (NULL);
}

static struct uvc_ctrl_state *
uvc_search_control(struct uvc_drv_ctrl *ctrls, uint32_t v4l2_id)
{
	struct uvc_ctrl_state *ret_ctrl = NULL;
	struct uvc_topo_node *topo_node = NULL, *tmp = NULL;

	if (v4l2_id & V4L2_CTRL_FLAG_NEXT_CTRL) {
		DPRINTF("Not support V4L2_CTRL_FLAG_NEXT_CTRL (0x%08x )\n",
		    v4l2_id);
		return (NULL);
	}

	/* Mask the query flags. */
	v4l2_id &= V4L2_CTRL_ID_MASK;

	/* Find the control. */
	STAILQ_FOREACH_SAFE(topo_node, &ctrls->topo_nodes, link, tmp) {
		ret_ctrl = uvc_search_control_sub(topo_node, v4l2_id);
		if (ret_ctrl != NULL) {
			return (ret_ctrl);
		}
	}

	DPRINTF("Control 0x%08x not found.\n", v4l2_id);
	return (NULL);
}

int
uvc_query_v4l2_ctrl(struct uvc_drv_video *video, struct v4l2_queryctrl *query)
{
	struct uvc_ctrl_state *ctrl;
	const struct uvc_ctrl_binding *binding;
	const struct uvc_ctrl_menu *menu;
	uint32_t access_bits;
	uint8_t node_id;
	uint8_t selector;
	uint16_t byte_size;
	int ret = EINVAL;

	mtx_lock(&video->ctrl->mtx);

	ctrl = uvc_search_control(video->ctrl, query->id);
	if (ctrl == NULL) {
		ret = EINVAL;
		goto done;
	}

	binding = ctrl->meta.bindings[ctrl->meta.selected_binding];
	if (binding == NULL) {
		ret = EINVAL;
		goto done;
	}
	menu = &ctrl->meta.menus[ctrl->meta.selected_binding];
	access_bits = ctrl->meta.access_bits;
	node_id = ctrl->topo_node->node_id;
	selector = ctrl->meta.selector;
	byte_size = ctrl->meta.byte_size;

	mtx_unlock(&video->ctrl->mtx);
	return (uvc_ctrl_describe_binding(video, binding, menu,
	    access_bits, node_id, selector, byte_size, query));
done:
	mtx_unlock(&video->ctrl->mtx);
	return (ret);
}

int
uvc_query_v4l2_menu(struct uvc_drv_video *video,
		    struct v4l2_querymenu *qm)
{
	struct uvc_ctrl_state *ctrl;
	uint8_t binding_idx = 0;
	int ret = 0;
	int id = qm->id;
	int index = qm->index;

	memset(qm, 0, sizeof(*qm));
	qm->id = id;
	qm->index = index;

	mtx_lock(&video->ctrl->mtx);

	ctrl = uvc_search_control(video->ctrl, qm->id);
	if (ctrl == NULL) {
		ret = EINVAL;
		goto done;
	}

	binding_idx = ctrl->meta.selected_binding;

	if (ctrl->meta.menus[binding_idx].menu_data == NULL) {
		ret = EINVAL;
		goto done;
	}

	if ((uint32_t)qm->index >= ctrl->meta.menus[binding_idx].item_num) {
		ret = EINVAL;
		goto done;
	}

	strlcpy(qm->name,
		ctrl->meta.menus[binding_idx].menu_data[qm->index].name,
		sizeof(qm->name));

done:
	mtx_unlock(&video->ctrl->mtx);
	return (ret);
}

int
uvc_get_v4l2_ctrl(struct uvc_drv_video *video, struct v4l2_control *control)
{
	const struct uvc_ctrl_binding *binding;
	const struct uvc_ctrl_menu *menu;
	struct uvc_ctrl_state *ctrl;
	uint8_t node_id;
	uint8_t selector;
	uint16_t byte_size;
	int ret;

	mtx_lock(&video->ctrl->mtx);
	ctrl = uvc_search_control(video->ctrl, control->id);
	if (ctrl == NULL) {
		ret = EINVAL;
		goto done;
	}

	if ((ctrl->meta.access_bits & UVC_CTRL_ACCESS_READ_CUR) == 0) {
		ret = ENOTTY;
		goto done;
	}

	binding = ctrl->meta.bindings[ctrl->meta.selected_binding];
	if (binding == NULL) {
		ret = EINVAL;
		goto done;
	}
	menu = &ctrl->meta.menus[ctrl->meta.selected_binding];
	node_id = ctrl->topo_node->node_id;
	selector = ctrl->meta.selector;
	byte_size = ctrl->meta.byte_size;

	mtx_unlock(&video->ctrl->mtx);
	return (uvc_ctrl_query_value(video, node_id, selector, byte_size,
	    binding, menu, UVC_CTRL_SLOT_CURRENT, &control->value));
done:
	mtx_unlock(&video->ctrl->mtx);
	return (ret);
}

int
uvc_set_v4l2_ctrl(struct uvc_drv_video *video, struct v4l2_control *control)
{
	const struct uvc_ctrl_binding *binding;
	const struct uvc_ctrl_menu *menu;
	struct uvc_ctrl_state *ctrl;
	uint32_t access_bits;
	uint8_t node_id;
	uint8_t selector;
	uint16_t byte_size;
	int ret;

	mtx_lock(&video->ctrl->mtx);
	ctrl = uvc_search_control(video->ctrl, control->id);
	if (ctrl == NULL) {
		ret = EINVAL;
		goto done;
	}

	if ((ctrl->meta.access_bits & UVC_CTRL_ACCESS_WRITE_CUR) == 0) {
		ret = ENOTTY;
		goto done;
	}

	binding = ctrl->meta.bindings[ctrl->meta.selected_binding];
	if (binding == NULL) {
		ret = EINVAL;
		goto done;
	}
	menu = &ctrl->meta.menus[ctrl->meta.selected_binding];
	node_id = ctrl->topo_node->node_id;
	selector = ctrl->meta.selector;
	byte_size = ctrl->meta.byte_size;
	access_bits = ctrl->meta.access_bits;

	mtx_unlock(&video->ctrl->mtx);
	return (uvc_ctrl_store_value(video, node_id, selector, byte_size,
	    binding, menu, access_bits, control->value));
done:
	mtx_unlock(&video->ctrl->mtx);
	return (ret);
}
