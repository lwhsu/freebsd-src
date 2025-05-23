#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/reservation/reservation.shlib

#
# DESCRIPTION:
#
# When a reservation property of a filesystem, regular volume
# or sparse volume is set to 'none' the space previously consumed by the
# reservation should be released back to the pool
#
# STRATEGY:
# 1) Create a filesystem, regular volume and sparse volume
# 2) Get the space used and available in the pool
# 3) Set a reservation on the filesystem less than the space available.
# 4) Verify that the 'reservation' property for the filesystem has
# the correct value.
# 5) Reset the reservation value back to zero (or 'none')
# 6) Verify that the space used and available totals for the pool have
# changed by the expected amounts (within tolerances).
# 7) Repeat steps 3-6 for a regular volume, sparse volume
#

verify_runnable "both"

log_assert "Verify space released when reservation on a dataset is set "\
	"to 'none'"

function cleanup
{
	for obj in $OBJ_LIST; do
		datasetexists $obj && destroy_dataset $obj -f
	done
}

log_onexit cleanup

space_avail=`get_prop available $TESTPOOL`

if ! is_global_zone ; then
	OBJ_LIST=""
else
	OBJ_LIST="$TESTPOOL/$TESTVOL $TESTPOOL/$TESTVOL2"
	((vol_set_size = space_avail / 4))
	vol_set_size=$(floor_volsize $vol_set_size)
	((sparse_vol_set_size = space_avail * 4))
	sparse_vol_set_size=$(floor_volsize $sparse_vol_set_size)


	log_must zfs create -V $vol_set_size $TESTPOOL/$TESTVOL
	log_must zfs set reservation=none $TESTPOOL/$TESTVOL
	log_must zfs create -s -V $sparse_vol_set_size $TESTPOOL/$TESTVOL2
fi

space_avail=`get_prop available $TESTPOOL`
space_used=`get_prop used $TESTPOOL`

# Calculate a large but valid reservation value.
resv_size_set=`expr $space_avail - $RESV_DELTA`

for obj in $TESTPOOL/$TESTFS $OBJ_LIST ; do

	#
	# For regular (non-sparse) volumes the upper limit is determined
	# not by the space available in the pool but rather by the size
	# of the volume itself.
	#
	[[ $obj == $TESTPOOL/$TESTVOL ]] && \
	    ((resv_size_set = vol_set_size - RESV_DELTA))

	log_must zfs set reservation=$resv_size_set $obj

	resv_size_get=`get_prop reservation $obj`
	if [[ $resv_size_set != $resv_size_get ]]; then
		log_fail "Reservation not the expected value "\
			"($resv_size_set != $resv_size_get)"
	fi

	log_must zfs set reservation=none $obj

	new_space_avail=`get_prop available $TESTPOOL`
	new_space_used=`get_prop used $TESTPOOL`

	log_must within_limits $space_used $new_space_used $RESV_TOLERANCE
	log_must within_limits $space_avail $new_space_avail $RESV_TOLERANCE
done

log_pass "Space correctly released when dataset reservation set to 'none'"
