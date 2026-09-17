#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "geodesy.h"

#define R 3440.065

static double ref_dist(double a, double b, double c, double d) {
  double D = M_PI / 180, p1 = a*D, p2 = c*D, dp = (c-a)*D, dl = (d-b)*D;
  double x = sin(dp/2)*sin(dp/2) + cos(p1)*cos(p2)*sin(dl/2)*sin(dl/2);
  return 2*atan2(sqrt(x), sqrt(1-x))*R;
}
static double ref_brg(double a, double b, double c, double d) {
  double D = M_PI/180, p1=a*D, p2=c*D, dl=(d-b)*D;
  double y = sin(dl)*cos(p2), x = cos(p1)*sin(p2)-sin(p1)*cos(p2)*cos(dl);
  double r = atan2(y,x)/D; if (r < 0) r += 360; return r;
}
static double brgdiff(double a, double b) { double d = fabs(a-b); if (d>180) d = 360-d; return d; }

int main(void) {
  struct { const char *n; double a,b,c,d; } legs[] = {
    {"YSBK->YSHL",      -33.9236,150.9908,-34.5611,150.7890},
    {"YSBK->YBTH",      -33.9236,150.9908,-33.4068,149.6512},
    {"YMMB->YMAV",      -37.9778,145.0998,-38.0394,144.4694},
    {"YBAS->YAYE",      -23.8067,133.9022,-25.1861,130.9756},
    {"YSSY->YBBN",      -33.9461,151.1770,-27.3842,153.1175},
    {"YSSY->YPPH",      -33.9461,151.1770,-31.9403,115.9669},
    {"YPDN->YMHB",      -12.4083,130.8728,-42.8361,147.5100},
    {"circuit 0.8nm",   -33.9236,150.9908,-33.9100,150.9908},
    {"very short 40m",  -33.9236,150.9908,-33.92396,150.9908},
    {"antimeridian",     -17.7500,179.9000,-17.7500,-179.9000},
    {"equator cross",     -1.0000,100.0000,  1.0000,102.0000},
  };
  int n = sizeof(legs)/sizeof(legs[0]);
  double worst_d_rel = 0, worst_b = 0;
  printf("%-18s %10s %10s %9s %8s %8s %7s\n","leg","ref nm","got nm","err%","ref brg","got brg","dBrg");
  for (int i = 0; i < n; i++) {
    double rd = ref_dist(legs[i].a,legs[i].b,legs[i].c,legs[i].d);
    double rb = ref_brg(legs[i].a,legs[i].b,legs[i].c,legs[i].d);
    float gd = geodesy_dist_nm(legs[i].a,legs[i].b,legs[i].c,legs[i].d);
    float gb = geodesy_brg_deg(legs[i].a,legs[i].b,legs[i].c,legs[i].d);
    // Below a tenth of a mile the display rounds to 0.0 anyway, so judge the
    // absolute miss rather than a percentage of almost nothing.
    double abs_err = fabs(gd - rd);
    double er = rd > 0.01 ? abs_err/rd*100 : 0;
    if (abs_err < 0.01) er = 0;
    double eb = brgdiff(gb, rb);
    if (er > worst_d_rel) worst_d_rel = er;
    if (eb > worst_b) worst_b = eb;
    printf("%-18s %10.3f %10.3f %9.4f %8.2f %8.2f %7.3f\n", legs[i].n, rd, gd, er, rb, gb, eb);
  }

  // Random sweep over the whole globe.
  srandom(42);
  double sw_d = 0, sw_b = 0;
  for (int i = 0; i < 200000; i++) {
    double a = ((double)random()/RAND_MAX)*170-85, b = ((double)random()/RAND_MAX)*360-180;
    double c = ((double)random()/RAND_MAX)*170-85, d = ((double)random()/RAND_MAX)*360-180;
    double rd = ref_dist(a,b,c,d), rb = ref_brg(a,b,c,d);
    float gd = geodesy_dist_nm(a,b,c,d), gb = geodesy_brg_deg(a,b,c,d);
    if (rd > 1.0) { double e = fabs(gd-rd)/rd*100; if (e > sw_d) sw_d = e; }
    if (rd > 1.0) { double e = brgdiff(gb,rb); if (e > sw_b) sw_b = e; }
  }
  printf("\nglobal random sweep (200k pairs): worst dist err %.4f%%, worst brg err %.3f deg\n", sw_d, sw_b);

  printf("\nnormalisation edge cases (these used to hang the watch):\n");
  printf("  norm360(inf)=%.1f  norm360(nan)=%.1f  norm360(-725)=%.1f  norm360(1085)=%.1f\n",
         geodesy_norm360(INFINITY), geodesy_norm360(NAN), geodesy_norm360(-725.0f), geodesy_norm360(1085.0f));
  printf("  rel_bearing(inf,0)=%.1f  rel(350,10)=%.1f  rel(10,350)=%.1f\n",
         geodesy_rel_bearing(INFINITY,0), geodesy_rel_bearing(350,10), geodesy_rel_bearing(10,350));
  printf("  coincident points dist=%.4f brg=%.1f\n",
         geodesy_dist_nm(-33.9,151.0,-33.9,151.0), geodesy_brg_deg(-33.9,151.0,-33.9,151.0));

  int ok = (worst_d_rel < 0.05) && (worst_b < 0.1) && (sw_d < 0.5) && (sw_b < 0.5);
  printf("\n%s\n", ok ? "GEODESY TESTS PASSED" : "*** GEODESY TESTS FAILED ***");
  return ok ? 0 : 1;
}
