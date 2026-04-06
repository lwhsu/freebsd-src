/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2011 Monthadar Al Jaberi, TerraNet AB
 * All rights reserved.
 *
 * Copyright (c) 2023 The FreeBSD Foundation
 *
 * Portions of this software were developed by En-Wei Wu
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

/*
 * Ioctl-related definitions for the Wireless TAP visibility plugin.
 */

#ifndef _VISIBILITY_IOCTL_H
#define _VISIBILITY_IOCTL_H

#include <sys/param.h>

#ifndef MAX_NBR_WTAP
#define MAX_NBR_WTAP	64
#endif

/*
 * Number of uint32_t words required to represent MAX_NBR_WTAP bits.
 * Distinct from the bits-per-word constant used for bit arithmetic.
 */
#ifndef VIS_MAP_NWORDS
#define VIS_MAP_NWORDS	(MAX_NBR_WTAP / (sizeof(uint32_t) * NBBY))
#endif

/*
 * Number of bits in each vis_map.map[] element.
 * Use this constant (not VIS_MAP_NWORDS) when computing word/bit indices.
 */
#define VIS_MAP_BITS_PER_WORD	(sizeof(uint32_t) * NBBY)

struct link {
	int	op;	/* 0 = remove link, 1 = add link */
	int	id1;
	int	id2;
};

struct vis_map_req {
	int		id;
	uint32_t	map[VIS_MAP_NWORDS];
};

/* Set medium state: arg is int (0 = close, 1 = open). */
#define VISIOCTLSETOPEN	_IOW('V', 1, int)
/* Add or remove a directed link. */
#define VISIOCTLSETLINK	_IOW('V', 2, struct link)
/* Read medium open/close state: arg is int. */
#define VISIOCTLGETOPEN	_IOR('V', 3, int)
/* Read the link map for a given node id. */
#define VISIOCTLGETMAP	_IOWR('V', 4, struct vis_map_req)

#endif
