#include "trig.h"

#define NAV_PI      3.14159265358979f
#define NAV_TWO_PI  6.28318530717959f
#define NAV_HALF_PI 1.57079632679490f

bool nav_isfinite(float v) {
  // NaN fails the self-comparison; the infinities fail the subtraction.
  return (v == v) && (v - v == 0.0f);
}

// Minimax-style Taylor kernels, valid for |x| <= pi/4.
static float kernel_sin(float x) {
  float x2 = x * x;
  return x * (1.0f + x2 * (-1.66666667e-1f + x2 * (8.33333333e-3f +
             x2 * (-1.98412698e-4f + x2 * 2.75573192e-6f))));
}

static float kernel_cos(float x) {
  float x2 = x * x;
  return 1.0f + x2 * (-0.5f + x2 * (4.16666667e-2f + x2 * (-1.38888889e-3f +
         x2 * 2.48015873e-5f)));
}

// Reduce to the octant the kernels cover, then pick the right kernel and sign.
static void reduce(float x, float *out, int *quadrant) {
  float k = x / NAV_HALF_PI;
  int n = (int)(k >= 0.0f ? k + 0.5f : k - 0.5f);
  *out = x - (float)n * NAV_HALF_PI;
  *quadrant = ((n % 4) + 4) % 4;
}

float nav_sinf(float x) {
  if (!nav_isfinite(x)) return 0.0f;
  float r;
  int q;
  reduce(x, &r, &q);
  switch (q) {
    case 0:  return kernel_sin(r);
    case 1:  return kernel_cos(r);
    case 2:  return -kernel_sin(r);
    default: return -kernel_cos(r);
  }
}

float nav_cosf(float x) {
  if (!nav_isfinite(x)) return 1.0f;
  float r;
  int q;
  reduce(x, &r, &q);
  switch (q) {
    case 0:  return kernel_cos(r);
    case 1:  return -kernel_sin(r);
    case 2:  return -kernel_cos(r);
    default: return kernel_sin(r);
  }
}

// Odd polynomial for atan on |z| <= 1.
static float kernel_atan(float z) {
  float z2 = z * z;
  return z * (0.99999329f + z2 * (-0.33298950f + z2 * (0.19946536f +
             z2 * (-0.13908534f + z2 * (0.09642000f + z2 * (-0.05590098f +
             z2 * (0.02186100f + z2 * -0.00405400f)))))));
}

static float atan_any(float z) {
  float a = z < 0.0f ? -z : z;
  float r;
  if (a <= 1.0f) {
    r = kernel_atan(a);
  } else {
    // atan(a) = pi/2 - atan(1/a) keeps the polynomial inside its valid range.
    r = NAV_HALF_PI - kernel_atan(1.0f / a);
  }
  return z < 0.0f ? -r : r;
}

float nav_atan2f(float y, float x) {
  if (!nav_isfinite(y) || !nav_isfinite(x)) return 0.0f;
  if (x == 0.0f && y == 0.0f) return 0.0f;
  if (x == 0.0f) return y > 0.0f ? NAV_HALF_PI : -NAV_HALF_PI;

  float a = atan_any(y / x);
  if (x > 0.0f) return a;
  return y >= 0.0f ? a + NAV_PI : a - NAV_PI;
}

float nav_sqrtf(float v) {
  if (!nav_isfinite(v) || v <= 0.0f) return 0.0f;
  // Newton-Raphson. Seeding near the value keeps this to a handful of passes,
  // and the iteration count is capped so it can never spin.
  float x = v > 1.0f ? v * 0.5f : 1.0f;
  for (int i = 0; i < 30; i++) {
    float next = 0.5f * (x + v / x);
    if (next == x) break;
    x = next;
  }
  return x;
}

float nav_fmodf(float v, float m) {
  if (!nav_isfinite(v) || !nav_isfinite(m) || m == 0.0f) return 0.0f;
  float am = m < 0.0f ? -m : m;
  float n = v / am;
  // Truncate toward zero, matching fmodf's sign convention.
  float t = (float)(long)n;
  float r = v - t * am;
  return r;
}
