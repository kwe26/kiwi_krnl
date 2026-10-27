/* math.h on the x87 FPU.  Intermediate results use 80-bit long double. */
#include <math.h>
#include <fenv.h>

static long double fl_rnd(long double x, unsigned short mode)
{
    unsigned short cw, ncw;
    long double r;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    ncw = (unsigned short)((cw & ~0x0C00) | mode);
    __asm__ volatile("fldcw %0" :: "m"(ncw));
    __asm__ volatile("frndint" : "=t"(r) : "0"(x));
    __asm__ volatile("fldcw %0" :: "m"(cw));
    return r;
}

double fabs(double x)
{
    long double r;
    __asm__("fabs" : "=t"(r) : "0"((long double)x));
    return (double)r;
}

double floor(double x) { return (double)fl_rnd(x, 0x0400); }
double ceil(double x)  { return (double)fl_rnd(x, 0x0800); }
double trunc(double x) { return (double)fl_rnd(x, 0x0C00); }
double copysign(double x, double y)
{
    union { double d; unsigned long long u; } a = { x }, b = { y };
    a.u = (a.u & ~(1ULL << 63)) | (b.u & (1ULL << 63));
    return a.d;
}

double rint(double x)
{
    long double r;
    __asm__("frndint" : "=t"(r) : "0"((long double)x));
    return (double)r;
}

long lrint(double x)
{
    long r;
    __asm__("fistpl %0" : "=m"(r) : "t"((long double)x) : "st");
    return r;
}

double round(double x)
{
    if (!isfinite(x))
        return x;
    double t = trunc(x);
    if (fabs(x - t) >= 0.5)
        t += copysign(1.0, x);
    return t;
}

double modf(double x, double *ip)
{
    double t = trunc(x);
    *ip = t;
    if (isinf(x))
        return copysign(0.0, x);
    return copysign(x - t, x);
}

double fmod(double x, double y)
{
    long double r;
    __asm__("1: fprem\n\t"
            "fnstsw %%ax\n\t"
            "sahf\n\t"
            "jp 1b"
            : "=t"(r) : "0"((long double)x), "u"((long double)y) : "ax", "cc");
    return (double)r;
}

double sqrt(double x)
{
    long double r;
    __asm__("fsqrt" : "=t"(r) : "0"((long double)x));
    return (double)r;
}

static long double log2l_(long double x)
{
    long double r;
    __asm__("fld1\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x));
    return r;
}

static long double exp2l_(long double t)
{
    if (t != t)
        return t;
    if (t > 16500)
        return __builtin_infl();
    if (t < -16500)
        return 0;
    long double i, f, r;
    __asm__("frndint" : "=t"(i) : "0"(t));
    f = t - i;
    __asm__("f2xm1" : "=t"(r) : "0"(f));
    r += 1.0L;
    __asm__("fscale" : "=t"(r) : "0"(r), "u"(i));
    return r;
}

double exp2(double x) { return (double)exp2l_(x); }
double exp(double x)  { return (double)exp2l_((long double)x * 1.44269504088896340735992468100189214L); }
double log2(double x) { return (double)log2l_(x); }

double log(double x)
{
    long double r;
    __asm__("fldln2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"((long double)x));
    return (double)r;
}

double log10(double x)
{
    long double r;
    __asm__("fldlg2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"((long double)x));
    return (double)r;
}

double log1p(double x)
{
    if (fabs(x) < 0.29) {
        long double r;
        __asm__("fldln2\n\tfxch\n\tfyl2xp1" : "=t"(r) : "0"((long double)x));
        return (double)r;
    }
    return log(1.0 + x);
}

double expm1(double x)
{
    if (fabs(x) < 0.5) {
        long double t = (long double)x * 1.44269504088896340735992468100189214L, r;
        __asm__("f2xm1" : "=t"(r) : "0"(t));
        return (double)r;
    }
    return (double)(exp2l_((long double)x * 1.44269504088896340735992468100189214L) - 1.0L);
}

static int is_int(double y) { return isfinite(y) && trunc(y) == y; }
static int is_odd(double y) { return is_int(y) && fabs(y) < 9007199254740992.0 && fmod(y, 2.0) != 0; }

double pow(double x, double y)
{
    if (y == 0 || x == 1)
        return 1;
    if (isnan(x) || isnan(y))
        return NAN;
    if (isinf(y)) {
        double ax = fabs(x);
        if (ax == 1) return 1;
        return (ax > 1) == (y > 0) ? INFINITY : 0;
    }
    if (x == 0) {
        if (y < 0) return is_odd(y) ? copysign(INFINITY, x) : INFINITY;
        return is_odd(y) ? x : 0;
    }
    if (isinf(x)) {
        if (x > 0) return y > 0 ? INFINITY : 0;
        if (y > 0) return is_odd(y) ? -INFINITY : INFINITY;
        return is_odd(y) ? -0.0 : 0;
    }
    double sign = 1;
    if (x < 0) {
        if (!is_int(y)) return NAN;
        if (is_odd(y)) sign = -1;
        x = -x;
    }
    if (is_int(y) && fabs(y) <= 64) {
        long double r = 1, b = x;
        unsigned n = (unsigned)fabs(y);
        while (n) {
            if (n & 1) r *= b;
            b *= b;
            n >>= 1;
        }
        return sign * (double)(y < 0 ? 1.0L / r : r);
    }
    return sign * (double)exp2l_((long double)y * log2l_(x));
}

double cbrt(double x)
{
    if (x == 0 || !isfinite(x))
        return x;
    long double a = fabs(x);
    long double r = exp2l_(log2l_(a) / 3.0L);
    r = r - (r * r * r - a) / (3.0L * r * r);
    return copysign((double)r, x);
}

double hypot(double x, double y)
{
    if (isinf(x) || isinf(y))
        return INFINITY;
    long double a = x, b = y, r;
    __asm__("fsqrt" : "=t"(r) : "0"(a * a + b * b));
    return (double)r;
}

double sin(double x)
{
    long double r;
    __asm__("fsin" : "=t"(r) : "0"((long double)x));
    return (double)r;
}

double cos(double x)
{
    long double r;
    __asm__("fcos" : "=t"(r) : "0"((long double)x));
    return (double)r;
}

double tan(double x)
{
    long double r;
    __asm__("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"((long double)x));
    return (double)r;
}

double atan2(double y, double x)
{
    long double r;
    __asm__("fpatan" : "=t"(r) : "0"((long double)x), "u"((long double)y) : "st(1)");
    return (double)r;
}

double atan(double x) { return atan2(x, 1.0); }

double asin(double x)
{
    if (fabs(x) > 1) return NAN;
    return atan2(x, sqrt((1.0 - x) * (1.0 + x)));
}

double acos(double x)
{
    if (fabs(x) > 1) return NAN;
    return atan2(sqrt((1.0 - x) * (1.0 + x)), x);
}

double sinh(double x)
{
    double a = fabs(x);
    if (a < 22) {
        double e = expm1(a);
        return copysign(0.5 * (e + e / (e + 1)), x);
    }
    return copysign(0.5 * exp(a), x);
}

double cosh(double x)
{
    double e = exp(fabs(x));
    return 0.5 * (e + 1 / e);
}

double tanh(double x)
{
    double a = fabs(x);
    if (a > 22)
        return copysign(1.0, x);
    double e = expm1(2 * a);
    return copysign(e / (e + 2), x);
}

double asinh(double x)
{
    double a = fabs(x);
    if (a > 1e10)
        return copysign(log(a) + M_LN2, x);
    return copysign(log1p(a + a * a / (1 + sqrt(1 + a * a))), x);
}

double acosh(double x)
{
    if (x < 1) return NAN;
    if (x > 1e10) return log(x) + M_LN2;
    return log(x + sqrt(x * x - 1));
}

double atanh(double x)
{
    if (fabs(x) > 1) return NAN;
    return copysign(0.5 * log1p(2 * fabs(x) / (1 - fabs(x))), x);
}

double fmin(double a, double b)
{
    if (isnan(a)) return b;
    if (isnan(b)) return a;
    if (a == b) return signbit(a) ? a : b;
    return a < b ? a : b;
}

double fmax(double a, double b)
{
    if (isnan(a)) return b;
    if (isnan(b)) return a;
    if (a == b) return signbit(a) ? b : a;
    return a > b ? a : b;
}

double ldexp(double x, int e)
{
    long double r;
    __asm__("fscale" : "=t"(r) : "0"((long double)x), "u"((long double)e));
    return (double)r;
}

double frexp(double x, int *e)
{
    if (x == 0 || !isfinite(x)) {
        *e = 0;
        return x;
    }
    long double sig, ex;
    __asm__("fxtract" : "=t"(sig), "=u"(ex) : "0"((long double)x));
    *e = (int)ex + 1;
    return (double)(sig / 2);
}

int fegetround(void)
{
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    return cw & 0x0C00;
}

int fesetround(int mode)
{
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw = (unsigned short)((cw & ~0x0C00) | (mode & 0x0C00));
    __asm__ volatile("fldcw %0" :: "m"(cw));
    return 0;
}
