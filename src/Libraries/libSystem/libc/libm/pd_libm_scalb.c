/*
 * scalb / scalbf: x * 2**n, with n passed as a floating-point value.
 *
 * The obsolete SVID spelling of scalbn. openlibm dropped it, but Darwin still
 * exports it and real third-party binaries still import it (jq, for one), so
 * it lives here with the other functions openlibm does not carry.
 *
 * Semantics follow SUSv2 / FreeBSD: a non-integral or out-of-range n is an
 * error, and NaN propagates. Clamping rather than casting a huge n keeps the
 * result at the correct infinity or zero instead of wrapping the int.
 */
#include <math.h>

double scalb(double x, double n)
{
    if (isnan(x) || isnan(n))
        return x + n;                  /* propagate, quieting a signalling NaN */
    if (!isfinite(n)) {
        if (n > 0.0)
            return x * n;              /* +inf: overflow to +/-inf, 0*inf -> NaN */
        return x / (-n);               /* -inf: underflow to +/-0, inf/inf -> NaN */
    }
    if (n != floor(n))                 /* non-integral exponent is undefined */
        return (x - x) / (x - x);      /* NaN, raising invalid */
    if (n > 65000.0)
        n = 65000.0;                   /* clamp well past DBL_MAX_EXP */
    else if (n < -65000.0)
        n = -65000.0;
    return scalbn(x, (int)n);
}

/*
 * No scalbf here on purpose: our <math.h> declares only the double form
 * (include/math.h), and nothing in the measured corpus imports _scalbf. Add
 * the prototype alongside it if a binary ever turns up that needs it.
 */
