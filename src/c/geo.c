#include "nav.h"
#include "geodesy.h"
#include "trig.h"

static float e6(int32_t v) { return (float)v / 1000000.0f; }

float geo_norm360(float deg) { return geodesy_norm360(deg); }

float geo_rel_bearing(float brg, float trk) { return geodesy_rel_bearing(brg, trk); }

float geo_dist_nm(int32_t lat1_e6, int32_t lon1_e6, int32_t lat2_e6, int32_t lon2_e6) {
  return geodesy_dist_nm(e6(lat1_e6), e6(lon1_e6), e6(lat2_e6), e6(lon2_e6));
}

float geo_brg_deg(int32_t lat1_e6, int32_t lon1_e6, int32_t lat2_e6, int32_t lon2_e6) {
  return geodesy_brg_deg(e6(lat1_e6), e6(lon1_e6), e6(lat2_e6), e6(lon2_e6));
}

// Variation east means magnetic reads less than true.
int32_t geo_to_magnetic(float true_deg) {
  float v = true_deg;
  if (g.cfg.magnetic && g.decl_valid) v -= (float)g.decl_x10 / 10.0f;
  v = geodesy_norm360(v);
  int32_t r = (int32_t)(v + 0.5f);
  if (r >= 360) r -= 360;
  return r;
}

// Pressure altitude from QNH, then the standard 118.8 ft per degree of ISA
// deviation. Matters more than pilots expect at hot inland strips.
float geo_density_alt_ft(int32_t elev_ft, int16_t temp_c, int16_t altim_hpa_x10) {
  float qnh = (float)altim_hpa_x10 / 10.0f;
  if (qnh < 800.0f || qnh > 1100.0f) qnh = 1013.25f;
  float pa = (float)elev_ft + (1013.25f - qnh) * 27.0f;
  float isa = 15.0f - 1.98f * (pa / 1000.0f);
  return pa + 118.8f * ((float)temp_c - isa);
}
