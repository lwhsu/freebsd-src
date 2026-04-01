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
 * Complex power function, float precision.
 *
 * Uses the formula cpowf(a, z) = cexpf(z * clogf(a)) per C99 G.6.4.1,
 * leveraging the high-precision clogf() and cexpf() implementations.
 */

#include <complex.h>
#include <math.h>
#include "math_private.h"

float complex
cpowf(float complex a, float complex z)
{
	float x, y, re_a, im_a;

	x = crealf(z);
	y = cimagf(z);
	re_a = crealf(a);
	im_a = cimagf(a);

	/* Handle zero base. */
	if (re_a == 0.0f && im_a == 0.0f) {
		if (x == 0.0f && y == 0.0f)
			return (CMPLXF(1.0f, 0.0f));
		return (CMPLXF(0.0f, 0.0f));
	}

	/* Optimize: positive real base with real exponent. */
	if (im_a == 0.0f && re_a > 0.0f && y == 0.0f)
		return (CMPLXF(powf(re_a, x), 0.0f));

	/* General case per C99 G.6.4.1: cexpf(z * clogf(a)). */
	return (cexpf(z * clogf(a)));
}
