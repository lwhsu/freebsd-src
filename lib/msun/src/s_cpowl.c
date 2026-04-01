/*-
 * Copyright (c) 2008 Stephen L. Moshier <steve@moshier.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Complex power function, long double precision.
 *
 * Uses the formula cpowl(a, z) = cexpl(z * clogl(a)) per C99 G.6.4.1,
 * leveraging the high-precision clogl() and cexpl() implementations.
 */

#include <complex.h>
#include <float.h>
#include <math.h>
#include "math_private.h"

long double complex
cpowl(long double complex a, long double complex z)
{
	long double x, y, re_a, im_a;

	x = creall(z);
	y = cimagl(z);
	re_a = creall(a);
	im_a = cimagl(a);

	/* Handle zero base. */
	if (re_a == 0.0L && im_a == 0.0L) {
		if (x == 0.0L && y == 0.0L)
			return (CMPLXL(1.0L, 0.0L));
		return (CMPLXL(0.0L, 0.0L));
	}

	/* Optimize: positive real base with real exponent. */
	if (im_a == 0.0L && re_a > 0.0L && y == 0.0L)
		return (CMPLXL(powl(re_a, x), 0.0L));

	/* General case per C99 G.6.4.1: cexpl(z * clogl(a)). */
	return (cexpl(z * clogl(a)));
}

#if (LDBL_MANT_DIG == 53)
__weak_reference(cpow, cpowl);
#endif
