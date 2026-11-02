#ifndef GOTH_LIBC_MATH_H
#define GOTH_LIBC_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_LOG2E 1.44269504088896340736
#define M_LOG10E 0.43429448190325182765

#define isnan(value) __builtin_isnan(value)
#define isinf(value) __builtin_isinf(value)
#define isfinite(value) __builtin_isfinite(value)
#define signbit(value) __builtin_signbit(value)

double fabs(double value);
double fmod(double left, double right);
double modf(double value, double *integer_part);
double frexp(double value, int *exponent);
double ldexp(double value, int exponent);
double floor(double value);
double ceil(double value);
double trunc(double value);
double round(double value);
double sqrt(double value);
double sin(double value);
double cos(double value);
double tan(double value);
double asin(double value);
double acos(double value);
double atan(double value);
double atan2(double y, double x);
double exp(double value);
double log(double value);
double log10(double value);
double log2(double value);
double pow(double base, double exponent);
double hypot(double x, double y);
double copysign(double magnitude, double sign);
double fmin(double left, double right);
double fmax(double left, double right);
double nan(const char *tag);

#ifdef __cplusplus
}
#endif

#endif
