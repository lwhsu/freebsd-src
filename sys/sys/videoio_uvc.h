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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_VIDEOIO_UVC_H_
#define	_SYS_VIDEOIO_UVC_H_

#include <sys/types.h>

/*
 * Native FreeBSD extension-unit access for /dev/video* devices.
 *
 * data_ptr carries a userspace virtual address encoded as uint64_t so the
 * ioctl argument layout stays identical across ILP32 and LP64 processes.
 */
#define	FBSD_UVC_XU_DIR_READ	0x00000001U
#define	FBSD_UVC_XU_DIR_WRITE	0x00000002U
/*
 * Native XU payloads are bounded by the USB control-transfer length field.
 */
#define	FBSD_UVC_XU_MAX_DATA	UINT16_MAX
#define	FBSD_UVC_XU_MENU_NAME_LEN	32

#define	FBSD_UVC_XU_DATA_RAW		0
#define	FBSD_UVC_XU_DATA_SIGNED		1
#define	FBSD_UVC_XU_DATA_UNSIGNED	2
#define	FBSD_UVC_XU_DATA_BOOLEAN	3
#define	FBSD_UVC_XU_DATA_ENUM		4
#define	FBSD_UVC_XU_DATA_BITMASK	5
#define	FBSD_UVC_XU_DATA_RECT		6

struct fbsd_uvc_xu_menu_entry {
	uint32_t	item_value;
	uint8_t		item_name[FBSD_UVC_XU_MENU_NAME_LEN];
};

struct fbsd_uvc_xu_query {
	uint32_t	direction;
	uint32_t	data_len;
	uint64_t	data_ptr;
	uint32_t	unit_id;
	uint32_t	control_selector;
	uint32_t	request_code;
};

struct fbsd_uvc_xu_map {
	uint32_t	v4l2_id;
	uint32_t	v4l2_type;
	uint32_t	data_kind;
	uint32_t	menu_num;
	uint64_t	menu_ptr;
	uint8_t		unit_guid[16];
	uint8_t		control_name[32];
	uint8_t		control_selector;
	uint8_t		control_offset_bits;
	uint8_t		control_size_bits;
	uint8_t		_pad0;
};

#define	FBSD_UVCIOC_XU_QUERY	_IOWR('U', 0x30, struct fbsd_uvc_xu_query)
#define	FBSD_UVCIOC_XU_MAP	_IOWR('U', 0x31, struct fbsd_uvc_xu_map)

#endif /* !_SYS_VIDEOIO_UVC_H_ */
