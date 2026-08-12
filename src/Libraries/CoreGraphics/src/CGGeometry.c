/*
 * CGGeometry - the value-type math of CoreGraphics.
 *
 * OpenOSX implementation (not Apple source). Compiled against the SDK's
 * <CoreGraphics/CGGeometry.h>, so CGPoint/CGSize/CGRect/CGAffineTransform have
 * exactly the layouts apps were built against; this file only supplies the
 * out-of-line functions. The many CGRectGetMinX-style accessors are static
 * inline in the header and are deliberately not here.
 *
 * Pure and deterministic, which is why it comes first: AppKit's NSGeometry is
 * a thin re-export of this, and every one of these is checkable against a
 * known value. Semantics follow the documented Quartz behaviour, including the
 * null/infinite rectangle special cases that trip up naive implementations.
 */
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGAffineTransform.h>   /* CGAffineTransform is not in CGGeometry.h */
#include <math.h>

/* The SDK header provides static-inline shadows for exactly these three,
 * macro'd to __-prefixed inline functions (CGAffineTransform.h:123/133/143).
 * Undefine the macros so our out-of-line definitions below emit the real
 * exported symbols instead of redefining the SDK's inlines. Every other
 * function here is a plain CG_EXTERN in the header and needs no such undef. */
#undef CGAffineTransformMake
#undef CGPointApplyAffineTransform
#undef CGSizeApplyAffineTransform

const CGPoint CGPointZero = { 0.0, 0.0 };
const CGSize  CGSizeZero  = { 0.0, 0.0 };
const CGRect  CGRectZero  = { { 0.0, 0.0 }, { 0.0, 0.0 } };

/* CGRectNull is the empty rect at (INF, INF); CGRectInfinite spans the plane.
 * Apps test against these by identity of behaviour, not bit pattern, so the
 * predicates below must recognise them. */
const CGRect CGRectNull = {
    { (CGFloat)INFINITY, (CGFloat)INFINITY },
    { 0.0, 0.0 }
};
const CGRect CGRectInfinite = {
    { (CGFloat)(-CGFLOAT_MAX / 2), (CGFloat)(-CGFLOAT_MAX / 2) },
    { (CGFloat)CGFLOAT_MAX, (CGFloat)CGFLOAT_MAX }
};

const CGAffineTransform CGAffineTransformIdentity = { 1, 0, 0, 1, 0, 0 };

/* ---- rectangle normalisation ---- */

CGRect CGRectStandardize(CGRect r)
{
    if (r.size.width < 0) { r.origin.x += r.size.width; r.size.width = -r.size.width; }
    if (r.size.height < 0) { r.origin.y += r.size.height; r.size.height = -r.size.height; }
    return r;
}

bool CGRectIsNull(CGRect r)
{
    return isinf(r.origin.x) || isinf(r.origin.y);
}

bool CGRectIsInfinite(CGRect r)
{
    return r.size.width >= CGFLOAT_MAX && r.size.height >= CGFLOAT_MAX;
}

bool CGRectIsEmpty(CGRect r)
{
    return CGRectIsNull(r) || r.size.width <= 0 || r.size.height <= 0;
}

CGRect CGRectIntegral(CGRect r)
{
    r = CGRectStandardize(r);
    CGFloat minx = floor(r.origin.x), miny = floor(r.origin.y);
    CGFloat maxx = ceil(r.origin.x + r.size.width);
    CGFloat maxy = ceil(r.origin.y + r.size.height);
    return (CGRect){ { minx, miny }, { maxx - minx, maxy - miny } };
}

CGRect CGRectInset(CGRect r, CGFloat dx, CGFloat dy)
{
    if (CGRectIsNull(r)) return CGRectNull;
    r = CGRectStandardize(r);
    r.origin.x += dx; r.origin.y += dy;
    r.size.width -= 2 * dx; r.size.height -= 2 * dy;
    if (r.size.width < 0 || r.size.height < 0) return CGRectNull;
    return r;
}

CGRect CGRectOffset(CGRect r, CGFloat dx, CGFloat dy)
{
    if (CGRectIsNull(r)) return CGRectNull;
    r = CGRectStandardize(r);
    r.origin.x += dx; r.origin.y += dy;
    return r;
}

/* ---- combining ---- */

CGRect CGRectUnion(CGRect a, CGRect b)
{
    if (CGRectIsNull(a)) return CGRectStandardize(b);
    if (CGRectIsNull(b)) return CGRectStandardize(a);
    a = CGRectStandardize(a); b = CGRectStandardize(b);
    CGFloat minx = fmin(a.origin.x, b.origin.x);
    CGFloat miny = fmin(a.origin.y, b.origin.y);
    CGFloat maxx = fmax(a.origin.x + a.size.width, b.origin.x + b.size.width);
    CGFloat maxy = fmax(a.origin.y + a.size.height, b.origin.y + b.size.height);
    return (CGRect){ { minx, miny }, { maxx - minx, maxy - miny } };
}

CGRect CGRectIntersection(CGRect a, CGRect b)
{
    if (CGRectIsNull(a) || CGRectIsNull(b)) return CGRectNull;
    a = CGRectStandardize(a); b = CGRectStandardize(b);
    CGFloat minx = fmax(a.origin.x, b.origin.x);
    CGFloat miny = fmax(a.origin.y, b.origin.y);
    CGFloat maxx = fmin(a.origin.x + a.size.width, b.origin.x + b.size.width);
    CGFloat maxy = fmin(a.origin.y + a.size.height, b.origin.y + b.size.height);
    if (maxx <= minx || maxy <= miny) return CGRectNull;
    return (CGRect){ { minx, miny }, { maxx - minx, maxy - miny } };
}

void CGRectDivide(CGRect r, CGRect *slice, CGRect *remainder,
                  CGFloat amount, CGRectEdge edge)
{
    r = CGRectStandardize(r);
    CGRect s, rem = r;
    if (amount < 0) amount = 0;
    switch (edge) {
    case CGRectMinXEdge:
        if (amount > r.size.width) amount = r.size.width;
        s = (CGRect){ r.origin, { amount, r.size.height } };
        rem.origin.x += amount; rem.size.width -= amount; break;
    case CGRectMinYEdge:
        if (amount > r.size.height) amount = r.size.height;
        s = (CGRect){ r.origin, { r.size.width, amount } };
        rem.origin.y += amount; rem.size.height -= amount; break;
    case CGRectMaxXEdge:
        if (amount > r.size.width) amount = r.size.width;
        s = (CGRect){ { r.origin.x + r.size.width - amount, r.origin.y },
                      { amount, r.size.height } };
        rem.size.width -= amount; break;
    case CGRectMaxYEdge:
    default:
        if (amount > r.size.height) amount = r.size.height;
        s = (CGRect){ { r.origin.x, r.origin.y + r.size.height - amount },
                      { r.size.width, amount } };
        rem.size.height -= amount; break;
    }
    if (slice) *slice = s;
    if (remainder) *remainder = rem;
}

/* ---- predicates ---- */

bool CGPointEqualToPoint(CGPoint a, CGPoint b)
{ return a.x == b.x && a.y == b.y; }

bool CGSizeEqualToSize(CGSize a, CGSize b)
{ return a.width == b.width && a.height == b.height; }

bool CGRectEqualToRect(CGRect a, CGRect b)
{
    if (CGRectIsNull(a) && CGRectIsNull(b)) return true;
    a = CGRectStandardize(a); b = CGRectStandardize(b);
    return CGPointEqualToPoint(a.origin, b.origin) &&
           CGSizeEqualToSize(a.size, b.size);
}

bool CGRectContainsPoint(CGRect r, CGPoint p)
{
    if (CGRectIsEmpty(r)) return false;
    r = CGRectStandardize(r);
    return p.x >= r.origin.x && p.x < r.origin.x + r.size.width &&
           p.y >= r.origin.y && p.y < r.origin.y + r.size.height;
}

bool CGRectContainsRect(CGRect a, CGRect b)
{
    if (CGRectIsEmpty(a) || CGRectIsEmpty(b)) return false;
    return CGRectEqualToRect(CGRectUnion(a, b), CGRectStandardize(a));
}

bool CGRectIntersectsRect(CGRect a, CGRect b)
{
    return !CGRectIsNull(CGRectIntersection(a, b));
}

/* ---- affine transforms ---- */

CGAffineTransform CGAffineTransformMake(CGFloat a, CGFloat b, CGFloat c,
                                        CGFloat d, CGFloat tx, CGFloat ty)
{ return (CGAffineTransform){ a, b, c, d, tx, ty }; }

CGAffineTransform CGAffineTransformMakeTranslation(CGFloat tx, CGFloat ty)
{ return (CGAffineTransform){ 1, 0, 0, 1, tx, ty }; }

CGAffineTransform CGAffineTransformMakeScale(CGFloat sx, CGFloat sy)
{ return (CGAffineTransform){ sx, 0, 0, sy, 0, 0 }; }

CGAffineTransform CGAffineTransformMakeRotation(CGFloat angle)
{
    CGFloat s = sin(angle), c = cos(angle);
    return (CGAffineTransform){ c, s, -s, c, 0, 0 };
}

bool CGAffineTransformIsIdentity(CGAffineTransform t)
{
    return t.a == 1 && t.b == 0 && t.c == 0 && t.d == 1 && t.tx == 0 && t.ty == 0;
}

bool CGAffineTransformEqualToTransform(CGAffineTransform a, CGAffineTransform b)
{
    return a.a == b.a && a.b == b.b && a.c == b.c &&
           a.d == b.d && a.tx == b.tx && a.ty == b.ty;
}

/* [a b 0; c d 0; tx ty 1] row-vector convention, matching Quartz. */
CGAffineTransform CGAffineTransformConcat(CGAffineTransform t1, CGAffineTransform t2)
{
    return (CGAffineTransform){
        t1.a * t2.a + t1.b * t2.c,
        t1.a * t2.b + t1.b * t2.d,
        t1.c * t2.a + t1.d * t2.c,
        t1.c * t2.b + t1.d * t2.d,
        t1.tx * t2.a + t1.ty * t2.c + t2.tx,
        t1.tx * t2.b + t1.ty * t2.d + t2.ty,
    };
}

CGAffineTransform CGAffineTransformTranslate(CGAffineTransform t, CGFloat tx, CGFloat ty)
{ return CGAffineTransformConcat(CGAffineTransformMakeTranslation(tx, ty), t); }

CGAffineTransform CGAffineTransformScale(CGAffineTransform t, CGFloat sx, CGFloat sy)
{ return CGAffineTransformConcat(CGAffineTransformMakeScale(sx, sy), t); }

CGAffineTransform CGAffineTransformRotate(CGAffineTransform t, CGFloat angle)
{ return CGAffineTransformConcat(CGAffineTransformMakeRotation(angle), t); }

CGAffineTransform CGAffineTransformInvert(CGAffineTransform t)
{
    CGFloat det = t.a * t.d - t.b * t.c;
    if (det == 0) return t;                 /* Quartz returns the input */
    CGFloat idet = 1 / det;
    return (CGAffineTransform){
        t.d * idet, -t.b * idet, -t.c * idet, t.a * idet,
        (t.c * t.ty - t.d * t.tx) * idet,
        (t.b * t.tx - t.a * t.ty) * idet,
    };
}

CGPoint CGPointApplyAffineTransform(CGPoint p, CGAffineTransform t)
{
    return (CGPoint){ t.a * p.x + t.c * p.y + t.tx,
                      t.b * p.x + t.d * p.y + t.ty };
}

CGSize CGSizeApplyAffineTransform(CGSize s, CGAffineTransform t)
{
    return (CGSize){ t.a * s.width + t.c * s.height,
                     t.b * s.width + t.d * s.height };
}

CGRect CGRectApplyAffineTransform(CGRect r, CGAffineTransform t)
{
    CGPoint c[4] = {
        CGPointApplyAffineTransform((CGPoint){ r.origin.x, r.origin.y }, t),
        CGPointApplyAffineTransform((CGPoint){ r.origin.x + r.size.width, r.origin.y }, t),
        CGPointApplyAffineTransform((CGPoint){ r.origin.x, r.origin.y + r.size.height }, t),
        CGPointApplyAffineTransform((CGPoint){ r.origin.x + r.size.width, r.origin.y + r.size.height }, t),
    };
    CGFloat minx = c[0].x, maxx = c[0].x, miny = c[0].y, maxy = c[0].y;
    for (int i = 1; i < 4; i++) {
        minx = fmin(minx, c[i].x); maxx = fmax(maxx, c[i].x);
        miny = fmin(miny, c[i].y); maxy = fmax(maxy, c[i].y);
    }
    return (CGRect){ { minx, miny }, { maxx - minx, maxy - miny } };
}
