// The radar's orientation modes, on the host.
//
// SELECT cycles three of them, but each can fall back when the thing it is
// pointed by is missing: no magnetometer, no fix, or too slow for GPS track
// to mean a direction. The footer label is the only way a pilot can tell
// which of the three they are in, so every fallback must still say which
// mode is selected and what the picture is actually referenced to -- and no
// two SELECT positions may print the same string under the same conditions.

#include "nav.h"
#include <stdlib.h>

AppState g;
GColor ui_bg, ui_fg, ui_dim, ui_accent, ui_warn, ui_good;

// compass.c's collaborators, stubbed.
static bool s_sub_called;
void pages_mark_dirty(void) {}
void compass_service_set_heading_filter(int32_t filter) { (void)filter; }
void compass_service_subscribe(CompassHeadingHandler h) { (void)h; s_sub_called = true; }
void compass_service_unsubscribe(void) { s_sub_called = false; }
float geo_norm360(float d) {
  while (d < 0.0f) d += 360.0f;
  while (d >= 360.0f) d -= 360.0f;
  return d;
}

static int failures;

static void set_state(uint8_t orient, bool fix, float gs, int8_t status,
                      int16_t compass_deg, int16_t trk_x10) {
  memset(&g, 0, sizeof(g));
  g.cfg.traffic_orient = orient;
  g.fix.valid = fix;
  g.gs_smooth_kt = gs;
  g.compass_status = status;
  g.compass_deg = compass_deg;
  g.fix.trk_x10 = trk_x10;
  compass_apply_mode();
}

static void expect(const char *what, const char *got, const char *want) {
  bool ok = strcmp(got, want) == 0;
  if (!ok) failures++;
  printf("  %s %-28s got %-14s want %s\n", ok ? "ok  " : "FAIL", what, got, want);
}

static void expect_ref(const char *what, float got, float want) {
  bool ok = (got - want) < 0.05f && (want - got) < 0.05f;
  if (!ok) failures++;
  printf("  %s %-28s got %-14.1f want %.1f\n", ok ? "ok  " : "FAIL", what, got, want);
}

int main(void) {
  printf("labels, flying with a good fix and a calibrated compass:\n");
  set_state(ORIENT_TRACK, true, 110.0f, CompassStatusCalibrated, 90, 450);
  expect("track", compass_mode_label(), "TRK UP");
  expect_ref("track reference", compass_reference_deg(), 45.0f);

  set_state(ORIENT_HEADING, true, 110.0f, CompassStatusCalibrated, 90, 450);
  expect("heading", compass_mode_label(), "HDG UP");
  expect_ref("heading reference", compass_reference_deg(), 90.0f);

  set_state(ORIENT_NORTH, true, 110.0f, CompassStatusCalibrated, 90, 450);
  expect("north", compass_mode_label(), "N UP");
  expect_ref("north reference", compass_reference_deg(), 0.0f);

  printf("\nsat on the ground, compass uncalibrated: all three degrade to\n"
         "north-up, and the label must not pretend otherwise -- but it must\n"
         "still say which mode SELECT is sitting in\n");
  for (int i = 0; i < 3; i++) {
    set_state((uint8_t)i, true, 0.0f, CompassStatusDataInvalid, 90, 450);
    expect_ref("reference is north", compass_reference_deg(), 0.0f);
  }
  set_state(ORIENT_TRACK, true, 0.0f, CompassStatusDataInvalid, 90, 450);
  expect("stationary track", compass_mode_label(), "N (SLOW)");
  set_state(ORIENT_HEADING, true, 0.0f, CompassStatusDataInvalid, 90, 450);
  expect("stationary heading", compass_mode_label(), "N (MAG CAL)");
  set_state(ORIENT_NORTH, true, 0.0f, CompassStatusDataInvalid, 90, 450);
  expect("stationary north", compass_mode_label(), "N UP");

  printf("\nno two SELECT positions may read alike, in any conditions:\n");
  static const int8_t statuses[] = {
    CompassStatusUnavailable, CompassStatusDataInvalid,
    CompassStatusCalibrating, CompassStatusCalibrated
  };
  static const float speeds[] = { 0.0f, 19.0f, 21.0f, 110.0f };
  int combos = 0;
  for (int f = 0; f < 2; f++) {
    for (size_t sp = 0; sp < sizeof(speeds) / sizeof(*speeds); sp++) {
      for (size_t st = 0; st < sizeof(statuses) / sizeof(*statuses); st++) {
        const char *label[3];
        for (int m = 0; m < 3; m++) {
          set_state((uint8_t)m, f == 1, speeds[sp], statuses[st], 90, 450);
          label[m] = compass_mode_label();
        }
        combos++;
        for (int a = 0; a < 3; a++) {
          for (int b = a + 1; b < 3; b++) {
            if (strcmp(label[a], label[b]) == 0) {
              printf("  FAIL fix=%d gs=%.0f status=%d: modes %d and %d "
                     "both read %s\n", f, (double)speeds[sp], statuses[st],
                     a, b, label[a]);
              failures++;
            }
          }
        }
      }
    }
  }
  printf("  ok   %d combinations of fix, speed and compass state\n", combos);

  printf("\nno GPS at all:\n");
  set_state(ORIENT_TRACK, false, 0.0f, CompassStatusDataInvalid, 90, 450);
  expect("track, no fix", compass_mode_label(), "N (NO GPS)");
  set_state(ORIENT_NORTH, false, 0.0f, CompassStatusDataInvalid, 90, 450);
  expect("north, no fix", compass_mode_label(), "N UP");

  printf("\nheading mode degrading, with a fix to fall back on:\n");
  set_state(ORIENT_HEADING, true, 110.0f, CompassStatusUnavailable, 90, 450);
  expect("no magnetometer", compass_mode_label(), "TRK (NO MAG)");
  expect_ref("falls back to track", compass_reference_deg(), 45.0f);

  set_state(ORIENT_HEADING, true, 110.0f, CompassStatusDataInvalid, 90, 450);
  expect("uncalibrated", compass_mode_label(), "TRK (MAG CAL)");
  expect_ref("falls back to track", compass_reference_deg(), 45.0f);

  set_state(ORIENT_HEADING, true, 110.0f, CompassStatusCalibrating, 90, 450);
  expect("calibrating is usable", compass_mode_label(), "HDG UP");

  printf("\nheading is carried from magnetic to true:\n");
  set_state(ORIENT_HEADING, true, 110.0f, CompassStatusCalibrated, 10, 450);
  g.decl_valid = true;
  g.decl_x10 = 125;  // 12.5 deg east, roughly Sydney
  expect_ref("10M with 12.5E", compass_reference_deg(), 22.5f);

  set_state(ORIENT_HEADING, true, 110.0f, CompassStatusCalibrated, 5, 450);
  g.decl_valid = true;
  g.decl_x10 = -100;  // 10 deg west
  expect_ref("wraps below zero", compass_reference_deg(), 355.0f);

  printf("\nthe magnetometer only runs when it is being used:\n");
  set_state(ORIENT_HEADING, true, 110.0f, CompassStatusCalibrated, 90, 450);
  if (!s_sub_called) { printf("  FAIL heading mode did not subscribe\n"); failures++; }
  else printf("  ok   heading mode subscribes\n");
  set_state(ORIENT_TRACK, true, 110.0f, CompassStatusCalibrated, 90, 450);
  if (s_sub_called) { printf("  FAIL track mode left the compass running\n"); failures++; }
  else printf("  ok   track mode unsubscribes\n");

  printf("\n%s\n", failures ? "COMPASS TESTS FAILED" : "COMPASS TESTS PASSED");
  return failures ? 1 : 0;
}
