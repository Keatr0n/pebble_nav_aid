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

// What the top of the radar actually is, once the fallbacks have had their
// say: heading needs a usable magnetometer, track needs a fix and enough
// speed for it to mean anything, and north is what is left. The reference
// angle and the label both come from this, so the picture and the words
// underneath it cannot disagree.
static OrientMode effective_orient(void) {
  if (g.cfg.traffic_orient == ORIENT_NORTH) return ORIENT_NORTH;
  if (g.cfg.traffic_orient == ORIENT_HEADING && compass_usable()) {
    return ORIENT_HEADING;
  }
  if (g.fix.valid && g.gs_smooth_kt > 20.0f) return ORIENT_TRACK;
  return ORIENT_NORTH;
}

// Degrees TRUE that the top of the radar represents.
float compass_reference_deg(void) {
  switch (effective_orient()) {
    case ORIENT_HEADING: {
      float t = (float)g.compass_deg;
      if (g.decl_valid) t += (float)g.decl_x10 / 10.0f;  // magnetic -> true
      return geo_norm360(t);
    }

    case ORIENT_TRACK:
      return geo_norm360((float)g.fix.trk_x10 / 10.0f);

    default:
      return 0.0f;
  }
}

// The label always names what the top of the radar actually is, with the
// reason for any fallback in brackets. Telling a pilot "HDG UP" while the
// picture is in fact track-up leaves them guessing what they are looking at.
//
// The reason is not decoration. Every mode can degrade to north-up, so
// without it a stationary watch prints the same string for two of the three
// SELECT positions and the cycle looks broken -- which is how this was
// found. Naming why each mode gave up keeps all three distinct in every
// combination of fix and magnetometer; test/test_compass.c pins that.
const char *compass_mode_label(void) {
  bool selected_heading = g.cfg.traffic_orient == ORIENT_HEADING;

  switch (effective_orient()) {
    case ORIENT_HEADING:
      return "HDG UP";

    case ORIENT_TRACK:
      if (!selected_heading) return "TRK UP";
      return g.compass_status == CompassStatusUnavailable ? "TRK (NO MAG)"
                                                          : "TRK (MAG CAL)";

    default:
      if (g.cfg.traffic_orient == ORIENT_NORTH) return "N UP";
      if (selected_heading) {
        // The magnetometer is the reason we are not in the selected mode,
        // even though track has since failed us too.
        return g.compass_status == CompassStatusUnavailable ? "N (NO MAG)"
                                                            : "N (MAG CAL)";
      }
      return g.fix.valid ? "N (SLOW)" : "N (NO GPS)";
  }
}
