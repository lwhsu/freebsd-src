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
 * Tests for powl().
 *
 * The NetBSD-derived t_pow.c tests pow() and powf() but not powl().
 * These tests verify that powl() provides full long double precision,
 * which was broken prior to the import of ld80/e_powl.c and
 * ld128/e_powl.c (see PR 227179).
 */

#include <sys/param.h>

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

/*
 * Precision tests: verify powl() returns results accurate to within
 * a few ULP of the expected value.
 */
ATF_TC_WITHOUT_HEAD(powl_precision);
ATF_TC_BODY(powl_precision, tc)
{
	/*
	 * powl(2, 4.5) -- the specific case from PR 227179.
	 * The old imprecise.c implementation returned
	 * 22.627416997969518774... (only ~double precision), while the
	 * correct long double value is 22.627416997969520780...
	 */
	CHECK_FPEQUAL_TOL(powl(2.0L, 4.5L),
	    22.627416997969520780220609168509L, 3 * LDBL_ULP(), FPE_ABS_ZERO);

	/* Exact integer results. */
	CHECK_FPEQUAL(powl(2.0L, 10.0L), 1024.0L);
	CHECK_FPEQUAL(powl(3.0L, 5.0L), 243.0L);
	CHECK_FPEQUAL(powl(10.0L, 3.0L), 1000.0L);

	/* Fractional exponents. */
	CHECK_FPEQUAL_TOL(powl(4.0L, 0.5L), 2.0L, LDBL_ULP(), FPE_ABS_ZERO);
	CHECK_FPEQUAL_TOL(powl(27.0L, 1.0L / 3.0L),
	    3.0L, 3 * LDBL_ULP(), FPE_ABS_ZERO);
	CHECK_FPEQUAL_TOL(powl(2.0L, 0.5L),
	    sqrtl(2.0L), LDBL_ULP(), FPE_ABS_ZERO);

	/* e^1 = e */
	CHECK_FPEQUAL_TOL(powl(M_El, 1.0L),
	    M_El, LDBL_ULP(), FPE_ABS_ZERO);

	/* Large exponents: verify no loss of precision. */
	CHECK_FPEQUAL_TOL(powl(2.0L, 32.0L),
	    4294967296.0L, LDBL_ULP(), FPE_ABS_ZERO);
	CHECK_FPEQUAL_TOL(powl(2.0L, -1.0L),
	    0.5L, LDBL_ULP(), FPE_ABS_ZERO);

	/*
	 * Base near 1 with large exponent -- this is the regime where
	 * precision issues are most likely.
	 */
	CHECK_FPEQUAL_TOL(powl(1.0L + LDBL_EPSILON, 1.0L),
	    1.0L + LDBL_EPSILON, LDBL_ULP(), FPE_ABS_ZERO);
}

/*
 * Special values: NaN, Inf, zero.
 */
ATF_TC_WITHOUT_HEAD(powl_nan);
ATF_TC_BODY(powl_nan, tc)
{

	/* powl(NaN, y) == NaN for y != 0 */
	ATF_CHECK(isnan(powl(NAN, 2.0L)));
	ATF_CHECK(isnan(powl(NAN, -1.0L)));

	/* powl(x, NaN) == NaN for x != 1 */
	ATF_CHECK(isnan(powl(2.0L, NAN)));
	ATF_CHECK(isnan(powl(-1.5L, NAN)));

	/* powl(1, NaN) == 1 */
	ATF_CHECK(powl(1.0L, NAN) == 1.0L);

	/* powl(NaN, 0) == 1 */
	ATF_CHECK(powl(NAN, 0.0L) == 1.0L);
	ATF_CHECK(powl(NAN, -0.0L) == 1.0L);
}

ATF_TC_WITHOUT_HEAD(powl_inf);
ATF_TC_BODY(powl_inf, tc)
{
	long double z;

	/* powl(+Inf, y>0) == +Inf */
	z = powl(INFINITY, 2.0L);
	ATF_CHECK(isinf(z) && !signbit(z));

	/* powl(+Inf, y<0) == +0.0 */
	z = powl(INFINITY, -2.0L);
	ATF_CHECK(z == 0.0L && !signbit(z));

	/* powl(-Inf, odd y>0) == -Inf */
	z = powl(-INFINITY, 3.0L);
	ATF_CHECK(isinf(z) && signbit(z));

	/* powl(-Inf, even y>0) == +Inf */
	z = powl(-INFINITY, 4.0L);
	ATF_CHECK(isinf(z) && !signbit(z));

	/* powl(x, +Inf): |x|>1 -> +Inf, |x|<1 -> +0 */
	z = powl(2.0L, INFINITY);
	ATF_CHECK(isinf(z) && !signbit(z));
	z = powl(0.5L, INFINITY);
	ATF_CHECK(z == 0.0L && !signbit(z));

	/* powl(x, -Inf): |x|>1 -> +0, |x|<1 -> +Inf */
	z = powl(2.0L, -INFINITY);
	ATF_CHECK(z == 0.0L && !signbit(z));
	z = powl(0.5L, -INFINITY);
	ATF_CHECK(isinf(z) && !signbit(z));

	/* powl(-1, +-Inf) == 1 */
	ATF_CHECK(powl(-1.0L, INFINITY) == 1.0L);
	ATF_CHECK(powl(-1.0L, -INFINITY) == 1.0L);
}

ATF_TC_WITHOUT_HEAD(powl_zero);
ATF_TC_BODY(powl_zero, tc)
{

	/* powl(x, +-0) == 1 for any x (including NaN) */
	ATF_CHECK(powl(2.0L, 0.0L) == 1.0L);
	ATF_CHECK(powl(2.0L, -0.0L) == 1.0L);
	ATF_CHECK(powl(0.0L, 0.0L) == 1.0L);
	ATF_CHECK(powl(INFINITY, 0.0L) == 1.0L);

	/* powl(+0, y>0 even) == +0 */
	ATF_CHECK(powl(0.0L, 4.0L) == 0.0L);
	ATF_CHECK(!signbit(powl(0.0L, 4.0L)));

	/* powl(-0, y>0 odd) == -0 */
	ATF_CHECK(powl(-0.0L, 3.0L) == 0.0L);
	ATF_CHECK(signbit(powl(-0.0L, 3.0L)));

	/* powl(+0, y>0 odd) == +0 */
	ATF_CHECK(powl(0.0L, 3.0L) == 0.0L);
	ATF_CHECK(!signbit(powl(0.0L, 3.0L)));
}

ATF_TC_WITHOUT_HEAD(powl_one);
ATF_TC_BODY(powl_one, tc)
{
	static const long double exponents[] = {
		0.0L, 1.0L, -1.0L, 2.5L, -99.0L, 1.0e10L, NAN, INFINITY,
		-INFINITY,
	};
	unsigned i;

	/* powl(1, y) == 1 for any y */
	for (i = 0; i < nitems(exponents); i++)
		ATF_CHECK(powl(1.0L, exponents[i]) == 1.0L);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, powl_precision);
	ATF_TP_ADD_TC(tp, powl_nan);
	ATF_TP_ADD_TC(tp, powl_inf);
	ATF_TP_ADD_TC(tp, powl_zero);
	ATF_TP_ADD_TC(tp, powl_one);

	return (atf_no_error());
}
