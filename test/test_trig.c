#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "trig.h"

static double maxe(const char *name, double e, double limit) {
  printf("%-12s max err %.3e   %s\n", name, e, e < limit ? "PASS" : "*** FAIL ***");
  return e < limit ? 0 : 1;
}

int main(void) {
  int fails = 0;
  double es = 0, ec = 0, ea = 0, eq = 0, em = 0;

  for (int i = 0; i < 400000; i++) {
    double t = ((double)i / 400000.0) * 40.0 - 20.0;  // -20..20 rad
    double d;
    d = fabs(nav_sinf((float)t) - sin(t)); if (d > es) es = d;
    d = fabs(nav_cosf((float)t) - cos(t)); if (d > ec) ec = d;
  }
  for (int i = 0; i < 200000; i++) {
    double ang = ((double)i / 200000.0) * 2 * M_PI - M_PI;
    double r = 1e-6 + (double)(i % 1000);
    double y = r * sin(ang), x = r * cos(ang);
    double got = nav_atan2f((float)y, (float)x);
    double want = atan2(y, x);
    double d = fabs(got - want);
    if (d > M_PI) d = fabs(d - 2 * M_PI);
    if (d > ea) ea = d;
  }
  for (int i = 1; i < 300000; i++) {
    double v = (double)i * 0.001;
    double d = fabs(nav_sqrtf((float)v) - sqrt(v)) / sqrt(v); if (d > eq) eq = d;
  }
  for (int i = 0; i < 100000; i++) {
    double v = ((double)i / 100000.0) * 4000.0 - 2000.0;
    double d = fabs(nav_fmodf((float)v, 360.0f) - fmod(v, 360.0)); if (d > em) em = d;
  }

  // Thresholds are set by what navigation needs, not by how close to double
  // precision a float can get. A 1e-5 error in a sine is a few metres over an
  // ocean crossing; 1e-3 rad of arctangent is 0.06 of a degree of bearing,
  // and the display only ever shows whole degrees.
  fails += maxe("sin", es, 1e-5);
  fails += maxe("cos", ec, 1e-5);
  fails += maxe("atan2(rad)", ea, 1e-3);
  fails += maxe("sqrt(rel)", eq, 1e-6);
  fails += maxe("fmod", em, 1e-2);

  printf("\nedge cases:\n");
  printf("  atan2(0,0)=%.4f  sqrt(-1)=%.4f  sqrt(0)=%.4f\n", nav_atan2f(0,0), nav_sqrtf(-1), nav_sqrtf(0));
  printf("  isfinite(nan)=%d isfinite(inf)=%d isfinite(1.5)=%d\n",
         nav_isfinite(NAN), nav_isfinite(INFINITY), nav_isfinite(1.5f));
  printf("  sin(inf)=%.2f cos(nan)=%.2f fmod(inf,360)=%.2f\n",
         nav_sinf(INFINITY), nav_cosf(NAN), nav_fmodf(INFINITY, 360.0f));
  printf("\n%s\n", fails ? "SOME TESTS FAILED" : "ALL TRIG TESTS PASSED");
  return fails;
}
