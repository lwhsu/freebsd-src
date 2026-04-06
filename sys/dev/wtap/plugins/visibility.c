/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2010-2011 Monthadar Al Jaberi, TerraNet AB
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

#include <sys/param.h>
#include <sys/jail.h>

#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>

#include "visibility.h"

static struct cdevsw vis_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_ioctl =	vis_ioctl,
	.d_name =	"visctl",
};

void
visibility_init(struct wtap_plugin *plugin)
{
	struct visibility_plugin *vis_plugin;

	vis_plugin = (struct visibility_plugin *) plugin;
	plugin->wp_sdev = make_dev(&vis_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "visctl");
	plugin->wp_sdev->si_drv1 = vis_plugin;
	mtx_init(&vis_plugin->pl_mtx, "visibility_plugin mtx",
	    NULL, MTX_DEF | MTX_RECURSE);
	printf("Using visibility wtap plugin...\n");
}

void
visibility_deinit(struct wtap_plugin *plugin)
{
	struct visibility_plugin *vis_plugin;

	vis_plugin = (struct visibility_plugin *) plugin;
	destroy_dev(plugin->wp_sdev);
	mtx_destroy(&vis_plugin->pl_mtx);
	free(vis_plugin, M_WTAP_PLUGIN);
	printf("Removing visibility wtap plugin...\n");
}

/*
 * Broadcast a packet to all nodes that have a link from p->id.
 * The visibility map uses VIS_MAP_NWORDS uint32_t words; each bit
 * represents one potential destination node.
 *
 * We need to use a mutex when reading or modifying the visibility map.
 */
void
visibility_work(struct wtap_plugin *plugin, struct packet *p)
{
	struct visibility_plugin *vis_plugin =
	    (struct visibility_plugin *) plugin;
	struct wtap_hal *hal = (struct wtap_hal *)vis_plugin->base.wp_hal;
	struct vis_map *map;
	int bpw = VIS_MAP_BITS_PER_WORD;

	KASSERT(mtod(p->m, const char *) != (const char *) 0xdeadc0de ||
	    mtod(p->m, const char *) != NULL,
	    ("[%s] got a corrupt packet from master queue, p->m=%p, p->id=%d\n",
	    __func__, p->m, p->id));
	DWTAP_PRINTF("[%d] BROADCASTING m=%p\n", p->id, p->m);
	mtx_lock(&vis_plugin->pl_mtx);
	map = &vis_plugin->pl_node[p->id];
	mtx_unlock(&vis_plugin->pl_mtx);

	/*
	 * This is O(n*n) which is not optimal for large number of nodes.
	 * Another way of doing it is creating groups of nodes that hear
	 * each other.  At least for this simple static node plugin.
	 */
	for (int i = 0; i < VIS_MAP_NWORDS; i++) {
		uint32_t word = map->map[i];

		for (int j = 0; j < bpw; j++) {
			if (word & 0x1) {
				int k = i * bpw + j;

				if (hal->hal_devs[k] != NULL &&
				    hal->hal_devs[k]->up == 1) {
					struct wtap_softc *sc = hal->hal_devs[k];
					struct mbuf *m = m_dup(p->m, M_NOWAIT);

					DWTAP_PRINTF("[%d] duplicated old_m=%p"
					    " to new_m=%p\n", p->id, p->m, m);
					wtap_inject(sc, m);
				}
			}
			word >>= 1;
		}
	}
}

static void
add_link(struct visibility_plugin *vis_plugin, struct link *l)
{
	struct vis_map *map;
	int bpw = VIS_MAP_BITS_PER_WORD;
	int index = l->id2 / bpw;
	int bit = l->id2 % bpw;

	mtx_lock(&vis_plugin->pl_mtx);
	map = &vis_plugin->pl_node[l->id1];
	map->map[index] |= (1u << bit);
	mtx_unlock(&vis_plugin->pl_mtx);
}

static void
del_link(struct visibility_plugin *vis_plugin, struct link *l)
{
	struct vis_map *map;
	int bpw = VIS_MAP_BITS_PER_WORD;
	int index = l->id2 / bpw;
	int bit = l->id2 % bpw;

	mtx_lock(&vis_plugin->pl_mtx);
	map = &vis_plugin->pl_node[l->id1];
	map->map[index] &= ~(1u << bit);
	mtx_unlock(&vis_plugin->pl_mtx);
}

static int
get_link(struct visibility_plugin *vis_plugin, struct vis_map_req *req)
{
	struct wtap_hal *hal = vis_plugin->base.wp_hal;

	if (req->id < 0 || req->id >= MAX_NBR_WTAP ||
	    !isset(hal->hal_devs_set, req->id))
		return (-1);

	mtx_lock(&vis_plugin->pl_mtx);
	memcpy(&req->map, &vis_plugin->pl_node[req->id], sizeof(req->map));
	mtx_unlock(&vis_plugin->pl_mtx);

	return (0);
}

int
vis_ioctl(struct cdev *sdev, u_long cmd, caddr_t data,
    int fflag, struct thread *td)
{
	struct visibility_plugin *vis_plugin =
	    (struct visibility_plugin *) sdev->si_drv1;
	struct wtap_hal *hal = vis_plugin->base.wp_hal;
	struct link l;
	struct vis_map_req *req;
	int op;
	int error = 0;

	CURVNET_SET(CRED_TO_VNET(curthread->td_ucred));
	switch (cmd) {
	case VISIOCTLSETOPEN:
		op = *(int *)data;
		if (op == 0)
			medium_close(hal->hal_md);
		else
			medium_open(hal->hal_md);
		break;
	case VISIOCTLSETLINK:
		l = *(struct link *)data;
		if (l.op == 0)
			del_link(vis_plugin, &l);
		else
			add_link(vis_plugin, &l);
		break;
	case VISIOCTLGETOPEN:
		memcpy(data, &hal->hal_md->open, sizeof(int));
		break;
	case VISIOCTLGETMAP:
		req = (struct vis_map_req *)data;
		if (get_link(vis_plugin, req) < 0)
			error = EINVAL;
		break;
	default:
		DWTAP_PRINTF("Unknown WTAP IOCTL\n");
		error = EINVAL;
	}

	CURVNET_RESTORE();
	return (error);
}
