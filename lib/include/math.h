/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS compatibility header, part of the EFMOS libc layer
 * (GPLv3; full license text in LICENSE).
 * Copyright (C) 2026 0x1a27
 */

#ifndef _EFMLIBC_MATH_H
#define _EFMLIBC_MATH_H
#ifdef __cplusplus
#include_next <math.h>
#else
#include <sys/types.h>
/* 常用数学常数 (C99/glibc 全集, Mesa nir_builtin_builder 等引用) */
#define M_E        2.71828182845904523536
#define M_LOG2E    1.44269504088896340736
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440
#define INFINITY   (1.0 / 0.0)
#define NAN        (0.0 / 0.0)
#define HUGE_VAL   (1.0 / 0.0)
#define HUGE_VALF  (1.0f / 0.0f)
#define HUGE_VALL  (1.0L / 0.0L)

/* 浮点分类 (C99) */
#define FP_NAN         0
#define FP_INFINITE    1
#define FP_ZERO        2
#define FP_SUBNORMAL   3
#define FP_NORMAL      4
int    __fpclassify(double x);
int    __fpclassifyf(float x);
#define fpclassify(x) (__builtin_types_compatible_p(__typeof__(x), float) \
                       ? __fpclassifyf(x) : __fpclassify((double)(x)))
#define isnormal(x)   (fpclassify(x) == FP_NORMAL)
/* fabs */
double  fabs(double x);
float   fabsf(float x);
long double fabsl(long double x);
/* floor/ceil/round/trunc */
double  floor(double x);      float  floorf(float x);
double  ceil(double x);       float  ceilf(float x);
double  round(double x);      float  roundf(float x);
double  trunc(double x);      float  truncf(float x);
/* fmod/hypot */
double  fmod(double a, double b);   float  fmodf(float a, float b);
double  hypot(double x, double y);  float  hypotf(float x, float y);
/* sqrt/cbrt */
double  sqrt(double x);      float  sqrtf(float x);
double  cbrt(double x);      float  cbrtf(float x);
/* exp/log/pow */
double  exp(double x);       float  expf(float x);
double  exp2(double x);      float  exp2f(float x);
double  exp10(double x);     float  exp10f(float x);
double  expm1(double x);     float  expm1f(float x);
double  log1p(double x);     float  log1pf(float x);
double  log(double x);       float  logf(float x);
double  log2(double x);      float  log2f(float x);
double  log10(double x);     float  log10f(float x);
double  pow(double b, double e); float powf(float b, float e);
/* trig */
double  sin(double x);       float  sinf(float x);
double  cos(double x);       float  cosf(float x);
double  tan(double x);       float  tanf(float x);
double  asin(double x);      float  asinf(float x);
double  acos(double x);      float  acosf(float x);
double  atan(double x);      float  atanf(float x);
double  atan2(double y, double x);  float atan2f(float y, float x);
/* hyperbolic */
double  sinh(double x);      float  sinhf(float x);
double  cosh(double x);      float  coshf(float x);
double  tanh(double x);      float  tanhf(float x);
/* rounding/ldexp/frexp/modf */
double  ldexp(double x, int n);    float ldexpf(float x, int n);
double  frexp(double x, int *exp); float frexpf(float x, int *exp);
double  modf(double x, double *i); float modff(float x, float *i);
double  rint(double x);      float  rintf(float x);
double  nearbyint(double x); float  nearbyintf(float x);
long    lrint(double x);     long   lrintf(float x);
long long llrint(double x);  long long llrintf(float x);
long    lround(double x);    long   lroundf(float x);
long long llround(double x); long long llroundf(float x);
/* classify */
int     isinf(double x);     int    isinff(float x);
int     isnan(double x);     int    isnanf(float x);
int     isfinite(double x);  int    isfinitef(float x);
int     signbit(double x);   int    signbitf(float x);
/* misc */
double  fmax(double a, double b);  float  fmaxf(float a, float b);
double  fmin(double a, double b);  float  fminf(float a, float b);
double  fma(double a, double b, double c);  float fmaf(float a, float b, float c);
double  fdim(double a, double b);
double  copysign(double x, double y);  float copysignf(float x, float y);
double  nan(const char *tagp);         float nanf(const char *tagp);
double  nextafter(double x, double y); float nextafterf(float x, float y);
double  erf(double x);           float  erff(float x);
double  erfc(double x);          float  erfcf(float x);
double  lgamma(double x);
#endif
#endif
