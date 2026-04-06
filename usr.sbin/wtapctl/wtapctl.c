/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2023 The FreeBSD Foundation
 *
 * This software was developed by En-Wei Wu under sponsorship from
 * the FreeBSD Foundation.
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

#include <sys/param.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>

#include "wtapctl.h"
#include "if_wtapioctl.h"
#include "plugins/visibility_ioctl.h"

static int dev_fd = -1;
static int vis_fd = -1;
static struct cmd *cmds = NULL;

static void
usage(void)
{
	fprintf(stderr,
	    "usage: wtapctl device create [id]\n"
	    "       wtapctl device delete <id>\n"
	    "       wtapctl device list\n"
	    "       wtapctl vis open | close\n"
	    "       wtapctl vis add <id_from> <id_to>\n"
	    "       wtapctl vis delete <id_from> <id_to>\n"
	    "       wtapctl vis show <id>\n");
	exit(1);
}

static void
device_create(const struct cmd *cmd __unused, int id)
{
	int orig_id = id;

	if (ioctl(dev_fd, WTAPIOCTLCRT, &id) < 0)
		errx(1, "error creating wtap with id=%d", orig_id);

	/* Print the assigned name (useful when id was auto-assigned). */
	printf("wtap%d\n", id);
}

static void
device_delete(const struct cmd *cmd __unused, int id)
{
	if (ioctl(dev_fd, WTAPIOCTLDEL, &id) < 0)
		errx(1, "error deleting wtap with id=%d", id);
}

static void
device_list(const struct cmd *cmd __unused)
{
	uint32_t devs_set[WTAP_HAL_BITMAP_SIZE];
	int bpw = (int)(sizeof(uint32_t) * NBBY);
	int id;

	if (ioctl(dev_fd, WTAPIOCTLLIST, devs_set) < 0)
		errx(1, "error getting wtap device list");

	for (id = 0; id < MAX_NBR_WTAP; id++) {
		int word = id / bpw;
		int bit  = id % bpw;

		if (devs_set[word] & (1u << bit))
			printf("wtap%d\n", id);
	}
}

static void
vis_toggle_medium(const struct cmd *cmd)
{
	int op = (strcmp(cmd->c_name, "open") == 0) ? 1 : 0;

	if (ioctl(vis_fd, VISIOCTLSETOPEN, &op) < 0)
		errx(1, "error %s medium", op ? "opening" : "closing");
}

static void
vis_link_op(const struct cmd *cmd, int id1, int id2)
{
	struct link l;

	l.op  = (strcmp(cmd->c_name, "add") == 0) ? 1 : 0;
	l.id1 = id1;
	l.id2 = id2;

	if (ioctl(vis_fd, VISIOCTLSETLINK, &l) < 0)
		errx(1, "error performing link operation");
}

static void
vis_link_show(const struct cmd *cmd __unused, int id)
{
	struct vis_map_req req;
	int is_open;
	int bpw = (int)(sizeof(uint32_t) * NBBY);

	if (id < 0 || id >= MAX_NBR_WTAP)
		errx(1, "device id must be between 0 and %d", MAX_NBR_WTAP - 1);

	req.id = id;

	if (ioctl(vis_fd, VISIOCTLGETOPEN, &is_open) < 0)
		errx(1, "error getting medium state");

	if (ioctl(vis_fd, VISIOCTLGETMAP, &req) < 0)
		errx(1, "error getting link map for id=%d", id);

	printf("medium: %s\n", is_open ? "open" : "closed");
	printf("wtap%d ->", id);

	for (int i = 0; i < VIS_MAP_NWORDS; i++) {
		uint32_t word = req.map[i];

		for (int j = 0; j < bpw; j++) {
			if (word & (1u << j))
				printf(" wtap%d", i * bpw + j);
		}
	}
	printf("\n");
}

static struct cmd *
find_command(const char *cmdstr)
{
	int i;

	for (i = 0; cmds[i].c_name != NULL; i++) {
		if (strcmp(cmds[i].c_name, cmdstr) == 0)
			return (&cmds[i]);
	}
	return (NULL);
}

static void
run_command(const struct cmd *cmd, int argc, const char *argv[])
{
	int arg;

	switch (cmd->c_arg_flag) {
	case ARG_NOARG:
		if (argc != 0)
			usage();
		cmd->c_func_noarg(cmd);
		break;
	case ARG_NEXTARGOPT:
		if (argc > 1)
			usage();
		arg = (argc == 1) ? atoi(argv[0]) : -1;
		cmd->c_func_arg(cmd, arg);
		break;
	case ARG_NEXTARG:
		if (argc != 1)
			usage();
		cmd->c_func_arg(cmd, atoi(argv[0]));
		break;
	case ARG_NEXTARG2:
		if (argc != 2)
			usage();
		cmd->c_func_arg2(cmd, atoi(argv[0]), atoi(argv[1]));
		break;
	default:
		errx(1, "internal fault");
	}
}

static struct cmd dev_cmds[] = {
	DEF_CMD_ARGOPT("create", device_create),
	DEF_CMD_ARG("delete",    device_delete),
	DEF_CMD_NOARG("list",    device_list),
	CMD_SENTINEL
};

static struct cmd vis_cmds[] = {
	DEF_CMD_NOARG("open",    vis_toggle_medium),
	DEF_CMD_NOARG("close",   vis_toggle_medium),
	DEF_CMD_ARG2("add",      vis_link_op),
	DEF_CMD_ARG2("delete",   vis_link_op),
	DEF_CMD_ARG("show",      vis_link_show),
	CMD_SENTINEL
};

int
main(int argc, const char *argv[])
{
	struct cmd *cmd;

	if (argc < 3)
		usage();

	if (strcmp(argv[1], "device") == 0)
		cmds = dev_cmds;
	else if (strcmp(argv[1], "vis") == 0)
		cmds = vis_cmds;
	else
		usage();

	cmd = find_command(argv[2]);
	if (cmd == NULL)
		usage();

	argc -= 3;
	argv += 3;

	dev_fd = open(WTAP_DEV_NODE, O_RDWR);
	if (dev_fd < 0)
		errx(1, "error opening %s", WTAP_DEV_NODE);

	vis_fd = open(VIS_DEV_NODE, O_RDWR);
	if (vis_fd < 0)
		errx(1, "error opening %s", VIS_DEV_NODE);

	run_command(cmd, argc, argv);

	return (0);
}
