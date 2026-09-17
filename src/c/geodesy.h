#pragma once

// Great-circle navigation maths, in plain floats with no platform types, so
// the same code that runs on the watch can be exercised on a host against a
// double-precision reference.

#define EARTH_RADIUS_NM 3440.065f

float geodesy_norm360(float deg);
float geodesy_rel_bearing(float brg, float trk);  // -180..180, right positive
float geodesy_dist_nm(float lat1, float lon1, float lat2, float lon2);
float geodesy_brg_deg(float lat1, float lon1, float lat2, float lon2);
