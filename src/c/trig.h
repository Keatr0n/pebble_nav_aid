#pragma once
#include <stdbool.h>

// Self-contained float maths.
//
// The toolchain's libm is deliberately not used here. Its trig pulls in
// double-precision argument reduction with large stack buffers and constant
// tables, and on this target that combination faulted the app with the program
// counter landing inside libm's own atan tables. These routines are plain
// polynomial evaluations: no tables, no recursion, bounded stack, and accurate
// to well inside what navigation needs.

float nav_sinf(float x);   // x in radians
float nav_cosf(float x);
float nav_atan2f(float y, float x);
float nav_sqrtf(float v);
float nav_fmodf(float v, float m);
bool nav_isfinite(float v);
