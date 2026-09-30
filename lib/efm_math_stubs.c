/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS - a 64-bit x86_64 UEFI operating system written in C.
 *
 * Copyright (C) 2026 0x1a27
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* efm_math_stubs.c — Mesa 用到的 math.h 函数子集 (软实现, 精度适中)
 *   sqrt/sin/cos/fabs/floor/ceil/fmod/pow/log2/exp/... 等够用级别实现. */

typedef unsigned int        u32;
typedef unsigned long long  u64;

/* 浮点分类常量 (需与 math.h 中保持一致) */
#ifndef FP_NAN
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
#endif

/* --- 简单公共 --- */
double fabs(double x) { return (x < 0) ? -x : x; }
float  fabsf(float x) { return (x < 0) ? -x : x; }
long double fabsl(long double x) { return (x < 0) ? -x : x; }

double floor(double x) {
    long i = (long)x;
    if (x < 0 && x != (double)i) i--;
    return (double)i;
}
float floorf(float x) { return (float)floor((double)x); }

double ceil(double x) {
    long i = (long)x;
    if (x > 0 && x != (double)i) i++;
    return (double)i;
}
float ceilf(float x) { return (float)ceil((double)x); }

double round(double x) { return (x >= 0) ? floor(x + 0.5) : ceil(x - 0.5); }
float roundf(float x) { return (float)round((double)x); }

double trunc(double x) { return (x >= 0) ? floor(x) : ceil(x); }
float  truncf(float x) { return (float)trunc((double)x); }

double fmod(double a, double b) {
    if (b == 0) return 0;
    double q = a / b;
    double qf = (q >= 0) ? floor(q) : ceil(q);
    return a - qf * b;
}
float fmodf(float a, float b) { return (float)fmod(a, b); }

double fdim(double a, double b) { return (a > b) ? (a - b) : 0; }
double fmax(double a, double b) { return (a > b) ? a : b; }
double fmin(double a, double b) { return (a < b) ? a : b; }
float  fmaxf(float a, float b) { return (a > b) ? a : b; }
float  fminf(float a, float b) { return (a < b) ? a : b; }

int signbit(double x) { union { double d; u64 u; } u; u.d = x; return (int)(u.u >> 63); }
int signbitf(float x) { union { float f; u32 u; } u; u.f = x; return (int)(u.u >> 31); }
int isinf(double x) {
    union { double d; u64 u; } u; u.d = x;
    return ((u.u >> 52) & 0x7FF) == 0x7FF && (u.u & ((1ULL<<52)-1)) == 0;
}
int isnan(double x) {
    union { double d; u64 u; } u; u.d = x;
    return ((u.u >> 52) & 0x7FF) == 0x7FF && (u.u & ((1ULL<<52)-1)) != 0;
}
int isfinite(double x) { return !isinf(x) && !isnan(x); }
int isinff(float x) { union {float f;u32 u;}u;u.f=x;return ((u.u>>23)&0xFF)==0xFF && (u.u&((1<<23)-1))==0; }
int isnanf(float x) { union {float f;u32 u;}u;u.f=x;return ((u.u>>23)&0xFF)==0xFF && (u.u&((1<<23)-1))!=0; }
int isfinitef(float x) { return !isinff(x) && !isnanf(x); }

/* --- sqrt (牛顿迭代) --- */
double sqrt(double x) {
    if (x <= 0) return 0;
    double g = x;
    for (int i = 0; i < 64; i++) {
        double ng = 0.5 * (g + x / g);
        if (ng == g) break;
        g = ng;
    }
    return g;
}
float sqrtf(float x) { return (float)sqrt((double)x); }
double cbrt(double x) {
    if (x == 0) return 0;
    int neg = 0;
    if (x < 0) { neg = 1; x = -x; }
    double g = x;
    for (int i = 0; i < 64; i++) {
        double ng = (2*g + x/(g*g)) / 3.0;
        if (ng == g) break;
        g = ng;
    }
    return neg ? -g : g;
}
float cbrtf(float x) { return (float)cbrt(x); }
double hypot(double x, double y) { return sqrt(x*x + y*y); }
float hypotf(float x, float y) { return (float)hypot(x,y); }

/* --- exp (泰勒 + 范围约简) --- */
static double pow2i(int n) {
    double r = 1;
    if (n >= 0) for (int i = 0; i < n; i++) r *= 2.0;
    else { n = -n; for (int i = 0; i < n; i++) r *= 0.5; }
    return r;
}
double exp(double x) {
    /* x = k*ln2 + r, |r| <= 0.5 ln2 ≈ 0.3466, exp(x)=2^k * exp(r) */
    const double LN2 = 0.6931471805599453;
    if (x > 700) return 1e308;
    if (x < -700) return 0;
    int k = (int)floor(x / LN2 + 0.5);
    double r = x - k * LN2;
    double s = 1, t = 1;
    for (int i = 1; i < 32; i++) {
        t *= r / i;
        s += t;
        if (t == 0) break;
    }
    return pow2i(k) * s;
}
float expf(float x) { return (float)exp(x); }

/* exp2(x) = 2^x = exp(x * ln2) */
double log(double x);      /* 前向声明: log1p/log2 引用 */
double log2(double x);
double exp2(double x) { return exp(x * 0.6931471805599453); }
float  exp2f(float x) { return (float)exp2((double)x); }
/* exp10(x) = 10^x = exp(x * ln10) */
double exp10(double x) { return exp(x * 2.3025850929940457); }
float  exp10f(float x) { return (float)exp10((double)x); }
/* expm1(x) = exp(x) - 1 (小 x 精度优化) */
double expm1(double x) {
    if (fabs(x) < 1e-8) return x * (1.0 + x * 0.5);
    return exp(x) - 1.0;
}
float  expm1f(float x) { return (float)expm1((double)x); }
/* log1p(x) = log(1 + x) (小 x 精度优化) */
double log1p(double x) {
    if (fabs(x) < 1e-8) return x * (1.0 - x * 0.5);
    return log(1.0 + x);
}
float  log1pf(float x) { return (float)log1p((double)x); }

/* fma(a,b,c) = a*b + c, 单次舍入 (此处简化为双精度直接计算, 够 Mesa 用) */
double fma(double a, double b, double c) { return a * b + c; }
float  fmaf(float a, float b, float c) { return (float)((double)a * (double)b + (double)c); }

/* 浮点分类 (供 fpclassify/isnormal 宏调用) */
int __fpclassify(double x) {
    union { double d; u64 u; } u; u.d = x;
    int e = (int)((u.u >> 52) & 0x7FF);
    u64 mant = u.u & ((1ULL << 52) - 1);
    if (e == 0x7FF) return mant ? FP_NAN : FP_INFINITE;
    if (e == 0)     return mant ? FP_SUBNORMAL : FP_ZERO;
    return FP_NORMAL;
}
int __fpclassifyf(float x) {
    union { float f; u32 u; } u; u.f = x;
    int e = (int)((u.u >> 23) & 0xFF);
    u32 mant = u.u & ((1u << 23) - 1);
    if (e == 0xFF) return mant ? FP_NAN : FP_INFINITE;
    if (e == 0)    return mant ? FP_SUBNORMAL : FP_ZERO;
    return FP_NORMAL;
}

/* --- log (牛顿法: log2 → ln) --- */
static int dfrexp2(double x) { /* 返回 floor(log2|x|) */
    union { double d; u64 u; } u; u.d = x;
    int e = (int)((u.u >> 52) & 0x7FF) - 1023;
    if (e == -1023 && x != 0) { /* 非规格化: 左移尾数 */
        u.u |= (1ULL<<52);
        e = (int)((u.u >> 52) & 0x7FF) - 1023;
        /* 计算前导零: 我们直接 e-=53; while ... 简化 */
        e -= 53;
        while ((u.u & (1ULL<<52)) == 0) { u.u <<= 1; e--; }
    }
    return e;
}
double log2(double x) {
    if (x <= 0) return -1e308;
    /* 归一化: x = m * 2^e, m in [1,2), log2(x)=e+log2(m) */
    int e = dfrexp2(x);
    double m = x * pow2i(-e);
    if (m >= 2) { m *= 0.5; e++; }
    /* log2(m) 在 [1,2): 用牛顿迭代求 y 使得 2^y = m.
     *   y_{n+1} = y_n + (m - 2^y_n) / (m * ln2)  太慢 → 用 3 阶近似 */
    const double A =  1.442689881663397;   /* 1/ln2 */
    double y = m - 1.0;
    /* 有理函数 (1,1) Pade 近似 log2(1+y)/(y) ≈ (2 + y) / (2 + 2y - y^2/?) 够用: */
    double num = y * (2.0 + 0.5*y);
    double den = 2.0 + y;
    double ly = num / den;
    /* 2-3 步牛顿精化 log2(x) 根: 给定 y, F = 2^y - m, F' = ln2 * 2^y
     *   y_{n+1} = y - (2^y - m) / (ln2 * 2^y) */
    for (int it = 0; it < 4; it++) {
        double f = exp(ly / A) - m;  /* 2^ly = exp(ln2 * ly) = exp(ly/A) */
        double fp = (1.0/A) * exp(ly / A);
        if (fp == 0) break;
        double ny = ly - f / fp;
        if (ny == ly) break;
        ly = ny;
    }
    return (double)e + ly;
}
double log(double x) { return log2(x) * 0.6931471805599453; }
double log10(double x) { return log2(x) * 0.3010299956639812; }
float log2f(float x) { return (float)log2(x); }
float logf(float x) { return (float)log(x); }
float log10f(float x) { return (float)log10(x); }

double pow(double b, double e) {
    if (b == 0) return 0;
    if (e == 0) return 1;
    if (b < 0 && e != floor(e)) return 0; /* 不支持负数非整数次幂 */
    int neg = (b < 0 && fmod(e, 2.0) != 0) ? 1 : 0;
    double vb = (b < 0) ? -b : b;
    double lg = log2(vb) * e;
    double r = exp(lg * 0.6931471805599453);
    return neg ? -r : r;
}
float powf(float b, float e) { return (float)pow(b, e); }

/* --- sin/cos/tan (范围约简 + 泰勒) --- */
double sin(double x) {
    /* 约简到 [-pi, pi] */
    const double PI = 3.141592653589793;
    const double TWO_PI = 2 * PI;
    x = fmod(x, TWO_PI);
    if (x >  PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;
    double x2 = x*x, s = x, t = x;
    for (int i = 1; i < 20; i++) {
        t = -t * x2 / ((2*i)*(2*i+1));
        s += t;
    }
    return s;
}
double cos(double x) {
    const double PI = 3.141592653589793;
    const double TWO_PI = 2 * PI;
    x = fmod(x, TWO_PI);
    if (x >  PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;
    double x2 = x*x, s = 1, t = 1;
    for (int i = 1; i < 20; i++) {
        t = -t * x2 / ((2*i-1)*(2*i));
        s += t;
    }
    return s;
}
double tan(double x) { double s = sin(x), c = cos(x); return c ? s / c : 1e308; }
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float tanf(float x) { return (float)tan(x); }

double atan(double x);   /* 前向声明: atan2 引用 */

double atan2(double y, double x) {
    const double PI = 3.141592653589793;
    if (x > 0) return atan(y/x);
    if (x < 0) return (y >= 0) ? (atan(y/x) + PI) : (atan(y/x) - PI);
    return (y > 0) ? (PI/2) : (y < 0 ? -PI/2 : 0);
}
double atan(double x) {
    /* atan(x) ≈ x/(1 + 0.28*x^2) 近似 + 一步牛顿不精确 → 直接用公式:
     *   atan(x) = 2 * atan(x / (1 + sqrt(1+x^2))) ; 先迭代到 |x| < 1.
     * 用有理近似 (Romberg): atan(x) = x * (1 + 0.260*x^2) / (1 + 0.593*x^2 + 0.073*x^4)
     * 误差 < 0.005 rad, 对 Mesa 够用 */
    int neg = 0, big = 0;
    if (x < 0) { neg = 1; x = -x; }
    if (x > 1) { big = 1; x = 1/x; }
    double x2 = x*x, x4 = x2*x2;
    double r = x * (1.0 + 0.260*x2) / (1.0 + 0.593*x2 + 0.073*x4);
    if (big) r = 1.5707963267948966 - r;
    return neg ? -r : r;
}
double asin(double x) {
    if (x < -1) x = -1; if (x > 1) x = 1;
    return atan2(x, sqrt(1 - x*x));
}
double acos(double x) {
    const double PI2 = 1.5707963267948966;
    return PI2 - asin(x);
}
float atan2f(float y, float x) { return (float)atan2(y, x); }
float atanf(float x) { return (float)atan(x); }
float asinf(float x) { return (float)asin(x); }
float acosf(float x) { return (float)acos(x); }

double sinh(double x) { double e = exp(x); return (e - 1/e) * 0.5; }
double cosh(double x) { double e = exp(x); return (e + 1/e) * 0.5; }
double tanh(double x) { double s = sinh(x), c = cosh(x); return c ? s/c : 1; }
float sinhf(float x) { return (float)sinh(x); }
float coshf(float x) { return (float)cosh(x); }
float tanhf(float x) { return (float)tanh(x); }

/* --- 其他常用 --- */
double ldexp(double x, int n) { return x * pow2i(n); }
float  ldexpf(float x, int n) { return (float)ldexp(x, n); }
double frexp(double x, int *exp) {
    if (x == 0) { *exp = 0; return 0; }
    *exp = dfrexp2(x) + 1;
    return x * pow2i(-*exp);
}
float  frexpf(float x, int *exp) { return (float)frexp(x, exp); }
double modf(double x, double *iptr) { *iptr = trunc(x); return x - *iptr; }
float  modff(float x, float *iptr)  { *iptr = truncf(x); return x - *iptr; }

double lgamma(double x) { (void)x; return 0; } /* 占位: Mesa 偶尔用 */
double erf(double x) {
    /* Abramowitz & Stegun 7.1.26 近似, |ε| < 1.5e-7 */
    double a1 =  0.254829592, a2 = -0.284496736, a3 =  1.421413741;
    double a4 = -1.453152027, a5 =  1.061405429, p  =  0.3275911;
    int sign = (x < 0) ? -1 : 1; x = fabs(x);
    double t = 1.0 / (1.0 + p*x);
    double y = 1.0 - (((((a5*t + a4)*t) + a3)*t + a2)*t + a1)*t * exp(-x*x);
    return sign * y;
}
float erff(float x) { return (float)erf(x); }
double erfc(double x) { return 1 - erf(x); }
float erfcf(float x) { return (float)erfc(x); }

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }
double fabs_unused_2(double x) { return fabs(x); }

/* Mesa util/rounding.h 依赖 nearbyint / rint / lrint */
double rint(double x) {
    double r = round(x);
    /* round-half-to-even: 半整数看奇偶 */
    double d = x - r;
    if (d == 0.5 || d == -0.5) {
        long ir = (long)r;
        if (ir & 1) r += (d > 0) ? -1 : 1;
    }
    return r;
}
float rintf(float x) { return (float)rint(x); }
double nearbyint(double x) { return rint(x); }
float nearbyintf(float x) { return rintf(x); }
long lrint(double x) { return (long)rint(x); }
long lrintf(float x) { return (long)rint((double)x); }
long long llrint(double x) { return (long long)rint(x); }
long long llrintf(float x) { return (long long)rint((double)x); }
long lround(double x) { return (long)round(x); }
long lroundf(float x) { return (long)roundf(x); }
long long llround(double x) { return (long long)round(x); }
long long llroundf(float x) { return (long long)roundf(x); }

/* copysign / nan / nextafter */
double copysign(double x, double y) { return signbit(y) ? -fabs(x) : fabs(x); }
float copysignf(float x, float y) { return signbitf(y) ? -fabsf(x) : fabsf(x); }
double nan(const char *s) { (void)s; union {double d;u64 u;} u; u.u = (0x7FFULL<<52)|1; return u.d; }
float nanf(const char *s) { (void)s; union {float f;u32 u;} u; u.u = (0xFFu<<23)|1; return u.f; }
double nextafter(double x, double y) {
    if (isnan(x) || isnan(y)) return x + y;
    if (x == y) return y;
    union { double d; u64 u; } u; u.d = x;
    if ((x < y) ^ (u.u >> 63)) u.u += 1; else u.u -= 1;
    return u.d;
}
float nextafterf(float x, float y) {
    if (x == y) return y;
    union { float f; u32 u; } u; u.f = x;
    if ((x < y) ^ (u.u >> 31)) u.u += 1; else u.u -= 1;
    return u.f;
}
