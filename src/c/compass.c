#include "nav.h"

// Watch compass handling for the traffic radar.
//
// Two conversions matter here. Pebble reports magnetic_heading measured
// counter-clockwise from north, while every bearing in this app is a normal
// clockwise compass bearing. And the watch reads magnetic while ADS-B targets
// arrive as true bearings, so the heading is carried to true with the same
// World Magnetic Model variation the rest of the app uses.

static bool s_subscribed;

static void compass_handler(CompassHeadingData heading) {
  g.compass_status = (int8_t)heading.compass_status;
  int32_t clockwise = TRIG_MAX_ANGLE - heading.magnetic_heading;
  clockwise %= TRIG_MAX_ANGLE;
  if (clockwise < 0) clockwise += TRIG_MAX_ANGLE;
  g.compass_deg = (int16_t)((clockwise * 360) / TRIG_MAX_ANGLE);
  pages_mark_dirty();
}

void compass_start(void) {
  if (s_subscribed) return;
  // Two degrees is finer than the radar can draw and keeps the redraws down.
  compass_service_set_heading_filter(DEG_TO_TRIGANGLE(2));
  compass_service_subscribe(compass_handler);
  s_subscribed = true;
}

void compass_stop(void) {
  if (!s_subscribed) return;
  compass_service_unsubscribe();
  s_subscribed = false;
}

// The magnetometer is only powered while the radar is actually pointed by it.
void compass_apply_mode(void) {
  if (g.cfg.traffic_orient == ORIENT_HEADING) {
    compass_start();
  } else {
    compass_stop();
  }
}

bool compass_usable(void) {
  return s_subscribed && g.compass_status >= CompassStatusCalibrating;
}

// Degrees TRUE that the top of the radar represents.
float compass_reference_deg(void) {
  switch (g.cfg.traffic_orient) {
    case ORIENT_NORTH:
      return 0.0f;

    case ORIENT_HEADING:
      if (compass_usable()) {
        float t = (float)g.compass_deg;
        if (g.decl_valid) t += (float)g.decl_x10 / 10.0f;  // magnetic -> true
        return geo_norm360(t);
      }
      // Fall through to track when the compass has nothing trustworthy yet.
      /* fallthrough */

    default:
      if (g.fix.valid && g.gs_smooth_kt > 20.0f) {
        return geo_norm360((float)g.fix.trk_x10 / 10.0f);
      }
      return 0.0f;
  }
}

const char *compass_mode_label(void) {
  switch (g.cfg.traffic_orient) {
    case ORIENT_NORTH:
      return "N UP";
    case ORIENT_HEADING:
      // When the magnetometer cannot deliver, the radar quietly uses track
      // instead. The label has to say so: telling a pilot "NO COMPASS" while
      // the picture is in fact track-up leaves them guessing what they are
      // looking at.
      if (!s_subscribed) return "TRK UP";
      if (g.compass_status == CompassStatusUnavailable) return "TRK (NO MAG)";
      if (g.compass_status < CompassStatusCalibrating) return "TRK (MAG CAL)";
      return "HDG UP";
    default:
      return (g.fix.valid && g.gs_smooth_kt > 20.0f) ? "TRK UP" : "N UP";
  }
}
