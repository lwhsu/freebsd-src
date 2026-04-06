/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 En-Wei Wu
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
 * sta_assoc: trigger an IEEE 802.11 MLME association from a STA VAP.
 * Usage: sta_assoc <interface> <bssid>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>

#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>

#include <lib80211/lib80211_ioctl.h>

int
main(int argc, char *argv[])
{
	struct sockaddr_dl sdl;
	struct ieee80211req_mlme mlme;
	char *addrstr;
	int s, ret;

	if (argc != 3)
		errx(1, "usage: %s <interface> <bssid>", argv[0]);

	s = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (s == -1)
		err(1, "socket");

	/*
	 * link_addr(3) requires a leading ':' before the address string
	 * to parse it as a link-layer address.
	 */
	addrstr = malloc(strlen(argv[2]) + 2);
	if (addrstr == NULL)
		err(1, "malloc");
	addrstr[0] = ':';
	strcpy(addrstr + 1, argv[2]);

	sdl.sdl_len = sizeof(sdl);
	link_addr(addrstr, &sdl);
	free(addrstr);

	if (sdl.sdl_alen != IEEE80211_ADDR_LEN) {
		close(s);
		errx(1, "malformed link-level address: %s", argv[2]);
	}

	memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = IEEE80211_MLME_ASSOC;
	memcpy(mlme.im_macaddr, LLADDR(&sdl), IEEE80211_ADDR_LEN);

	ret = lib80211_set80211(s, argv[1], IEEE80211_IOC_MLME,
	    0, sizeof(mlme), &mlme);
	if (ret < 0) {
		close(s);
		err(1, "lib80211_set80211 IEEE80211_IOC_MLME");
	}

	close(s);
	return (0);
}
