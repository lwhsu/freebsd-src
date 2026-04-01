/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 The FreeBSD Foundation
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
 * Tests for cpow{,f,l}().
 */

#include <sys/param.h>

#include <complex.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#pragma	STDC CX_LIMITED_RANGE	OFF

/*
 * Test hooks for different precisions.
 */
static long double complex
_cpowf(long double complex a, long double complex z)
{

	return (cpowf((float complex)a, (float complex)z));
}

static long double complex
_cpow(long double complex a, long double complex z)
{

	return (cpow((double complex)a, (double complex)z));
}

/* cpow(0, 0) = 1; cpow(0, nonzero) = 0 */
ATF_TC_WITHOUT_HEAD(zero);
ATF_TC_BODY(zero, tc)
{

	/* cpow(0, 0) = 1 + 0i */
	CHECK_CFPEQUAL_CS(_cpow(CMPLXL(0.0, 0.0), CMPLXL(0.0, 0.0)),
	    CMPLXL(1.0, 0.0), CS_BOTH);
	CHECK_CFPEQUAL_CS(_cpowf(CMPLXL(0.0, 0.0), CMPLXL(0.0, 0.0)),
	    CMPLXL(1.0, 0.0), CS_BOTH);
	CHECK_CFPEQUAL_CS(cpowl(CMPLXL(0.0, 0.0), CMPLXL(0.0, 0.0)),
	    CMPLXL(1.0, 0.0), CS_BOTH);

	/* cpow(0, z) = 0 for nonzero z */
	CHECK_CFPEQUAL_CS(_cpow(CMPLXL(0.0, 0.0), CMPLXL(1.0, 0.0)),
	    CMPLXL(0.0, 0.0), CS_BOTH);
	CHECK_CFPEQUAL_CS(_cpowf(CMPLXL(0.0, 0.0), CMPLXL(1.0, 0.0)),
	    CMPLXL(0.0, 0.0), CS_BOTH);
	CHECK_CFPEQUAL_CS(_cpow(CMPLXL(0.0, 0.0), CMPLXL(2.0, 3.0)),
	    CMPLXL(0.0, 0.0), CS_BOTH);
	CHECK_CFPEQUAL_CS(_cpow(CMPLXL(0.0, 0.0), CMPLXL(0.0, 1.0)),
	    CMPLXL(0.0, 0.0), CS_BOTH);
}

/* Positive real base with real exponent: should match pow(). */
ATF_TC_WITHOUT_HEAD(real_base_real_exp);
ATF_TC_BODY(real_base_real_exp, tc)
{

	/* 2^3 = 8 */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(2.0, 0.0), CMPLXL(3.0, 0.0)),
	    CMPLXL(8.0, 0.0), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(2.0, 0.0), CMPLXL(3.0, 0.0)),
	    CMPLXL(8.0, 0.0), 3 * FLT_ULP(), FPE_ABS_ZERO);

	/* 4^0.5 = 2 */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(4.0, 0.0), CMPLXL(0.5, 0.0)),
	    CMPLXL(2.0, 0.0), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(4.0, 0.0), CMPLXL(0.5, 0.0)),
	    CMPLXL(2.0, 0.0), 3 * FLT_ULP(), FPE_ABS_ZERO);

	/* 10^2 = 100 */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(10.0, 0.0), CMPLXL(2.0, 0.0)),
	    CMPLXL(100.0, 0.0), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(10.0, 0.0), CMPLXL(2.0, 0.0)),
	    CMPLXL(100.0, 0.0), 3 * FLT_ULP(), FPE_ABS_ZERO);

	/* 2^10 = 1024 */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(2.0, 0.0), CMPLXL(10.0, 0.0)),
	    CMPLXL(1024.0, 0.0), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(2.0, 0.0), CMPLXL(10.0, 0.0)),
	    CMPLXL(1024.0, 0.0), 3 * FLT_ULP(), FPE_ABS_ZERO);
}

/* Integer powers of complex numbers with known results. */
ATF_TC_WITHOUT_HEAD(integer_powers);
ATF_TC_BODY(integer_powers, tc)
{

	/* (1+i)^2 = 2i */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(1.0, 1.0), CMPLXL(2.0, 0.0)),
	    CMPLXL(0.0, 2.0), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(1.0, 1.0), CMPLXL(2.0, 0.0)),
	    CMPLXL(0.0, 2.0), 3 * FLT_ULP(), FPE_ABS_ZERO);

	/* (1+i)^4 = -4 */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(1.0, 1.0), CMPLXL(4.0, 0.0)),
	    CMPLXL(-4.0, 0.0), 5 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(1.0, 1.0), CMPLXL(4.0, 0.0)),
	    CMPLXL(-4.0, 0.0), 5 * FLT_ULP(), FPE_ABS_ZERO);

	/* i^2 = -1 */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(0.0, 1.0), CMPLXL(2.0, 0.0)),
	    CMPLXL(-1.0, 0.0), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(0.0, 1.0), CMPLXL(2.0, 0.0)),
	    CMPLXL(-1.0, 0.0), 3 * FLT_ULP(), FPE_ABS_ZERO);

	/* i^4 = 1 */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(0.0, 1.0), CMPLXL(4.0, 0.0)),
	    CMPLXL(1.0, 0.0), 5 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(0.0, 1.0), CMPLXL(4.0, 0.0)),
	    CMPLXL(1.0, 0.0), 5 * FLT_ULP(), FPE_ABS_ZERO);

	/* (-1)^2 = 1 */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(-1.0, 0.0), CMPLXL(2.0, 0.0)),
	    CMPLXL(1.0, 0.0), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(-1.0, 0.0), CMPLXL(2.0, 0.0)),
	    CMPLXL(1.0, 0.0), 3 * FLT_ULP(), FPE_ABS_ZERO);
}

/*
 * Precision tests near |a| = 1, where the old Cephes implementation
 * suffered from catastrophic cancellation in log(|a|).
 */
ATF_TC_WITHOUT_HEAD(precision_near_one);
ATF_TC_BODY(precision_near_one, tc)
{

	/*
	 * e^(i*pi/4) raised to the 8th power = e^(i*2*pi) = 1.
	 * a = cos(pi/4) + i*sin(pi/4) = (sqrt(2)/2, sqrt(2)/2), |a| = 1.
	 */
	CHECK_CFPEQUAL_TOL(_cpow(
	    CMPLXL(M_SQRT2 / 2.0, M_SQRT2 / 2.0), CMPLXL(8.0, 0.0)),
	    CMPLXL(1.0, 0.0), 5 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(
	    CMPLXL(M_SQRT2 / 2.0, M_SQRT2 / 2.0), CMPLXL(8.0, 0.0)),
	    CMPLXL(1.0, 0.0), 5 * FLT_ULP(), FPE_ABS_ZERO);

	/*
	 * e^(i*pi/3) raised to the 6th power = e^(i*2*pi) = 1.
	 * a = (0.5, sqrt(3)/2), |a| = 1.
	 */
	CHECK_CFPEQUAL_TOL(_cpow(
	    CMPLXL(0.5, sqrtl(3.0L) / 2.0L), CMPLXL(6.0, 0.0)),
	    CMPLXL(1.0, 0.0), 5 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(
	    CMPLXL(0.5, sqrtl(3.0L) / 2.0L), CMPLXL(6.0, 0.0)),
	    CMPLXL(1.0, 0.0), 5 * FLT_ULP(), FPE_ABS_ZERO);

	/*
	 * e^(i*pi/6) raised to the 12th power = e^(i*2*pi) = 1.
	 * a = (sqrt(3)/2, 0.5), |a| = 1.
	 */
	CHECK_CFPEQUAL_TOL(_cpow(
	    CMPLXL(sqrtl(3.0L) / 2.0L, 0.5), CMPLXL(12.0, 0.0)),
	    CMPLXL(1.0, 0.0), 10 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(
	    CMPLXL(sqrtl(3.0L) / 2.0L, 0.5), CMPLXL(12.0, 0.0)),
	    CMPLXL(1.0, 0.0), 10 * FLT_ULP(), FPE_ABS_ZERO);
}

/* Complex exponents: i^i = e^(-pi/2). */
ATF_TC_WITHOUT_HEAD(complex_exponent);
ATF_TC_BODY(complex_exponent, tc)
{
	long double expected_re;

	/* i^i = e^(i * log(i)) = e^(i * i*pi/2) = e^(-pi/2) */
	expected_re = expl(-M_PI / 2.0L);

	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(0.0, 1.0), CMPLXL(0.0, 1.0)),
	    CMPLXL(expected_re, 0.0), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(0.0, 1.0), CMPLXL(0.0, 1.0)),
	    CMPLXL(expected_re, 0.0), 3 * FLT_ULP(), FPE_ABS_ZERO);

	/* 2^i = e^(i*ln2) = cos(ln2) + i*sin(ln2) */
	CHECK_CFPEQUAL_TOL(_cpow(CMPLXL(2.0, 0.0), CMPLXL(0.0, 1.0)),
	    CMPLXL(cosl(M_LN2), sinl(M_LN2)), 3 * DBL_ULP(), FPE_ABS_ZERO);
	CHECK_CFPEQUAL_TOL(_cpowf(CMPLXL(2.0, 0.0), CMPLXL(0.0, 1.0)),
	    CMPLXL(cosl(M_LN2), sinl(M_LN2)), 3 * FLT_ULP(), FPE_ABS_ZERO);
}

/* Long double precision tests. */
ATF_TC_WITHOUT_HEAD(cpowl_precision);
ATF_TC_BODY(cpowl_precision, tc)
{

	/* (1+i)^2 = 2i */
	CHECK_CFPEQUAL_TOL(cpowl(CMPLXL(1.0L, 1.0L), CMPLXL(2.0L, 0.0L)),
	    CMPLXL(0.0L, 2.0L), 3 * LDBL_ULP(), FPE_ABS_ZERO);

	/* e^(i*pi/4) to the 8th = 1 */
	CHECK_CFPEQUAL_TOL(
	    cpowl(CMPLXL(sqrtl(2.0L) / 2.0L, sqrtl(2.0L) / 2.0L),
		CMPLXL(8.0L, 0.0L)),
	    CMPLXL(1.0L, 0.0L), 5 * LDBL_ULP(), FPE_ABS_ZERO);

	/*
	 * i^i = e^(-pi/2).  The compound operation (cexpl of clogl)
	 * accumulates some error, so we allow a wider tolerance here.
	 */
	CHECK_CFPEQUAL_TOL(
	    cpowl(CMPLXL(0.0L, 1.0L), CMPLXL(0.0L, 1.0L)),
	    CMPLXL(expl(-M_PI / 2.0L), 0.0L), 600 * LDBL_ULP(), FPE_ABS_ZERO);

	/* 2^10 = 1024 */
	CHECK_CFPEQUAL_TOL(
	    cpowl(CMPLXL(2.0L, 0.0L), CMPLXL(10.0L, 0.0L)),
	    CMPLXL(1024.0L, 0.0L), 3 * LDBL_ULP(), FPE_ABS_ZERO);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, zero);
	ATF_TP_ADD_TC(tp, real_base_real_exp);
	ATF_TP_ADD_TC(tp, integer_powers);
	ATF_TP_ADD_TC(tp, precision_near_one);
	ATF_TP_ADD_TC(tp, complex_exponent);
	ATF_TP_ADD_TC(tp, cpowl_precision);

	return (atf_no_error());
}
