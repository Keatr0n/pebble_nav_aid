#include "geodesy.h"
#include "trig.h"

#define GEO_PI 3.14159265358979f

static float d2r(float deg) { return deg * (GEO_PI / 180.0f); }
static float r2d(float rad) { return rad * (180.0f / GEO_PI); }

// Normalising by repeated subtraction is a trap: fed an infinity it never
// terminates, and a watchdog reset in flight looks exactly like the app
// vanishing. This is bounded, and answers non-finite input rather than
// spinning on it.
float geodesy_norm360(float deg) {
  if (!nav_isfinite(deg)) return 0.0f;
  deg = nav_fmodf(deg, 360.0f);
  if (deg < 0.0f) deg += 360.0f;
  return deg;
}

float geodesy_rel_bearing(float brg, float trk) {
  float d = brg - trk;
  if (!nav_isfinite(d)) return 0.0f;
  d = nav_fmodf(d, 360.0f);
  if (d > 180.0f) d -= 360.0f;
  if (d < -180.0f) d += 360.0f;
  return d;
}

// Haversine. Chosen over the flat-earth approximation because it stays honest
// on the long legs -- a 1700 NM crossing comes out right, where the flat
// approximation is most of a degree out on track and drifts on distance.
float geodesy_dist_nm(float lat1, float lon1, float lat2, float lon2) {
  float p1 = d2r(lat1);
  float p2 = d2r(lat2);
  float dp = d2r(lat2 - lat1);
  float dl = d2r(lon2 - lon1);

  float sdp = nav_sinf(dp * 0.5f);
  float sdl = nav_sinf(dl * 0.5f);
  float a = sdp * sdp + nav_cosf(p1) * nav_cosf(p2) * sdl * sdl;
  if (a < 0.0f) a = 0.0f;
  if (a > 1.0f) a = 1.0f;
  return 2.0f * nav_atan2f(nav_sqrtf(a), nav_sqrtf(1.0f - a)) * EARTH_RADIUS_NM;
}

float geodesy_brg_deg(float lat1, float lon1, float lat2, float lon2) {
  float p1 = d2r(lat1);
  float p2 = d2r(lat2);
  float dl = d2r(lon2 - lon1);

  float y = nav_sinf(dl) * nav_cosf(p2);
  float x = nav_cosf(p1) * nav_sinf(p2) - nav_sinf(p1) * nav_cosf(p2) * nav_cosf(dl);
  // Exactly coincident points leave the arctangent nothing to work from.
  if (y == 0.0f && x == 0.0f) return 0.0f;
  return geodesy_norm360(r2d(nav_atan2f(y, x)));
}
