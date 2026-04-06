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

#ifndef _WTAPCTL_H_
#define _WTAPCTL_H_

/*
 * Command descriptor.  c_arg_flag selects how many additional arguments
 * the command expects and which union member to call.
 */
struct cmd {
	const char	*c_name;
	int		 c_arg_flag;
#define ARG_NOARG	0	/* no extra argument */
#define ARG_NEXTARGOPT	1	/* optional integer argument */
#define ARG_NEXTARG	2	/* exactly one integer argument */
#define ARG_NEXTARG2	3	/* exactly two integer arguments */
	union {
		void (*c_func_noarg)(const struct cmd *);
		void (*c_func_arg)(const struct cmd *, int);
		void (*c_func_arg2)(const struct cmd *, int, int);
	} c_u;
#define c_func_noarg	c_u.c_func_noarg
#define c_func_arg	c_u.c_func_arg
#define c_func_arg2	c_u.c_func_arg2
};

#define CMD_SENTINEL	{ NULL, 0, { NULL } }

#define DEF_CMD_NOARG(name, func) {	\
	.c_name = (name),		\
	.c_arg_flag = ARG_NOARG,	\
	.c_func_noarg = (func)		\
}

#define DEF_CMD_ARGOPT(name, func) {	\
	.c_name = (name),		\
	.c_arg_flag = ARG_NEXTARGOPT,	\
	.c_func_arg = (func)		\
}

#define DEF_CMD_ARG(name, func) {	\
	.c_name = (name),		\
	.c_arg_flag = ARG_NEXTARG,	\
	.c_func_arg = (func)		\
}

#define DEF_CMD_ARG2(name, func) {	\
	.c_name = (name),		\
	.c_arg_flag = ARG_NEXTARG2,	\
	.c_func_arg2 = (func)		\
}

#define WTAP_DEV_NODE	"/dev/wtapctl"
#define VIS_DEV_NODE	"/dev/visctl"

#endif /* _WTAPCTL_H_ */
