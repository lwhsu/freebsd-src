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
 * Complex power function.
 *
 * Uses the formula cpow(a, z) = cexp(z * clog(a)) per C99 G.6.4.1,
 * leveraging the high-precision clog() and cexp() implementations.
 */

#include <complex.h>
#include <math.h>
#include "math_private.h"

double complex
cpow(double complex a, double complex z)
{
	double x, y, re_a, im_a;

	x = creal(z);
	y = cimag(z);
	re_a = creal(a);
	im_a = cimag(a);

	/* Handle zero base. */
	if (re_a == 0.0 && im_a == 0.0) {
		if (x == 0.0 && y == 0.0)
			return (CMPLX(1.0, 0.0));
		return (CMPLX(0.0, 0.0));
	}

	/* Optimize: positive real base with real exponent. */
	if (im_a == 0.0 && re_a > 0.0 && y == 0.0)
		return (CMPLX(pow(re_a, x), 0.0));

	/* General case per C99 G.6.4.1: cexp(z * clog(a)). */
	return (cexp(z * clog(a)));
}
