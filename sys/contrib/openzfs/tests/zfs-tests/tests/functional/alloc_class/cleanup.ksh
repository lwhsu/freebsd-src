#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017, Intel Corporation.
# Copyright (c) 2018, Delphix
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

verify_runnable "global"

default_cleanup_noexit
disk_cleanup

log_pass
