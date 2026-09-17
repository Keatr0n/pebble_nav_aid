#include "nav.h"
#include "trig.h"

#define RADAR_PI 3.14159265358979f

static GRect row(GRect b, int16_t *y, int16_t h) {
  GRect r = GRect(b.origin.x, *y, b.size.w, h);
  *y += h;
  return r;
}

// Traffic is polled every 15 s with the radar up and every 60 s behind it, and
// a failing poll backs off to four minutes. Ninety seconds is therefore well
// past any healthy cadence but short of crying wolf over one missed poll.
#define TFC_STALE_S 90

static uint32_t traffic_age_s(void) {
  if (g.tfc_ts == 0) return UINT32_MAX;
  time_t now = time(NULL);
  return now > g.tfc_ts ? (uint32_t)(now - g.tfc_ts) : 0;
}

static bool traffic_is_stale(void) { return traffic_age_s() > TFC_STALE_S; }

// A target close enough and level enough to be worth looking for out the
// window. Deliberately conservative -- this is awareness, not separation.
static bool is_close(const Traffic *t) {
  if (t->dist_x10 > 30) return false;
  if (t->alt_ft == UNKNOWN_I32 || !g.fix.valid) return true;
  int32_t split = t->alt_ft - g.fix.alt_ft;
  if (split < 0) split = -split;
  return split <= 1200;
}

// Pilots are taught to call traffic by the clock face, so show it that way
// rather than making someone subtract headings in the circuit.
static void clock_position(char *buf, size_t n, const Traffic *t) {
  if (g.gs_smooth_kt > 20.0f) {
    float rel = geo_rel_bearing((float)t->brg, (float)g.fix.trk_x10 / 10.0f);
    int oc = (int)((rel < 0 ? rel + 360.0f : rel) / 30.0f + 0.5f);
    if (oc <= 0 || oc > 12) oc = 12;
    snprintf(buf, n, "%d o'c", oc);
  } else {
    // Sitting still there is no "ahead", so fall back to a compass bearing.
    snprintf(buf, n, "%03d", (int)geo_to_magnetic((float)t->brg));
  }
}

// ------------------------------------------------------------------ radar

// Outer ring, snapped up to a round number so the scale is readable at a
// glance instead of being whatever the farthest target happened to be.
static int radar_range_nm(void) {
  int far = 0;
  for (uint8_t i = 0; i < g.tfc_count; i++) {
    if (g.tfc[i].dist_x10 > far) far = g.tfc[i].dist_x10;
  }
  float far_nm = (float)far / 10.0f;
  const int steps[] = { 2, 5, 10, 20, 40, 80 };
  for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
    if (far_nm <= (float)steps[i]) return steps[i];
  }
  return g.cfg.traffic_radius_nm > 0 ? g.cfg.traffic_radius_nm : 20;
}

static void draw_radar(GContext *ctx, GRect b) {
  char buf[24];
  // Two lines: the mode and range in readable type, with a smaller reminder of
  // what the blip numbers mean underneath.
  const int16_t footer_h = 27;
  int16_t avail_h = b.size.h - footer_h;
  int16_t radius = (avail_h < b.size.w ? avail_h : b.size.w) / 2 - 2;
  if (radius < 20) radius = 20;
  GPoint c = GPoint(b.origin.x + b.size.w / 2, b.origin.y + avail_h / 2);

  int range = radar_range_nm();
  float ref = compass_reference_deg();

  graphics_context_set_stroke_color(ctx, ui_dim);
  graphics_draw_circle(ctx, c, radius);
  graphics_draw_circle(ctx, c, radius / 2);

  // Four ticks fixed to the screen. These are NOT cardinal points -- under
  // track-up and heading-up they are the 12, 3, 6 and 9 o'clock references
  // a pilot actually calls traffic by, and the list view names targets the
  // same way. They only happen to be cardinal when north is up.
  for (int i = 0; i < 4; i++) {
    float a = (float)i * 90.0f * (RADAR_PI / 180.0f);
    int16_t x1 = c.x + (int16_t)(nav_sinf(a) * (float)(radius - 4));
    int16_t y1 = c.y - (int16_t)(nav_cosf(a) * (float)(radius - 4));
    int16_t x2 = c.x + (int16_t)(nav_sinf(a) * (float)radius);
    int16_t y2 = c.y - (int16_t)(nav_cosf(a) * (float)radius);
    graphics_draw_line(ctx, GPoint(x1, y1), GPoint(x2, y2));
  }

  // True north, which is the one direction the fixed ticks cannot show once
  // the picture is rotated. Longer and in the accent colour so it reads as a
  // different kind of mark; under north-up it simply sits on the 12 o'clock
  // tick and makes it bolder.
  {
    float na = -ref * (RADAR_PI / 180.0f);
    graphics_context_set_stroke_color(ctx, ui_accent);
    graphics_draw_line(ctx,
        GPoint(c.x + (int16_t)(nav_sinf(na) * (float)(radius - 9)),
               c.y - (int16_t)(nav_cosf(na) * (float)(radius - 9))),
        GPoint(c.x + (int16_t)(nav_sinf(na) * (float)radius),
               c.y - (int16_t)(nav_cosf(na) * (float)radius)));
    graphics_context_set_stroke_color(ctx, ui_dim);
  }

  // Own ship. Under track-up it points straight up by definition; under
  // north-up it swings round to show which way we are actually going.
  float own = 0.0f;
  if (g.fix.valid && g.gs_smooth_kt > 20.0f) {
    own = geo_rel_bearing((float)g.fix.trk_x10 / 10.0f, ref);
  }
  ui_arrow(ctx, c, 7, own, ui_accent);

  for (uint8_t i = 0; i < g.tfc_count && i < MAX_TRAFFIC; i++) {
    Traffic *t = &g.tfc[i];
    float d_nm = (float)t->dist_x10 / 10.0f;
    if (d_nm > (float)range) continue;

    float rel = geo_rel_bearing((float)t->brg, ref) * (RADAR_PI / 180.0f);
    float scale = d_nm / (float)range;
    int16_t px = c.x + (int16_t)(nav_sinf(rel) * scale * (float)radius);
    int16_t py = c.y - (int16_t)(nav_cosf(rel) * scale * (float)radius);

    GColor col = is_close(t) ? ui_warn : ui_fg;
    graphics_context_set_fill_color(ctx, col);
    graphics_fill_circle(ctx, GPoint(px, py), 3);

    // Relative altitude in hundreds of feet, the way a traffic display reads:
    // -29 is 2900 ft below you. The footer spells this out, because it is not
    // something you should have to infer mid-flight.
    if (t->alt_ft != UNKNOWN_I32 && g.fix.valid) {
      int hundreds = (int)((t->alt_ft - g.fix.alt_ft) / 100);
      const char *trend = t->vs_fpm > 300 ? "^" : (t->vs_fpm < -300 ? "v" : "");
      snprintf(buf, sizeof(buf), "%+d%s", hundreds, trend);
      GRect lr = GRect(px - 18, py + 3, 36, 14);
      ui_text(ctx, lr, FONT_KEY_GOTHIC_09, buf, GTextAlignmentCenter, col);
    }
  }

  int16_t fy = b.origin.y + b.size.h - footer_h;
  snprintf(buf, sizeof(buf), "%s · %d NM", compass_mode_label(), range);
  ui_text(ctx, GRect(b.origin.x, fy, b.size.w, 16),
          FONT_KEY_GOTHIC_14, buf, GTextAlignmentCenter, ui_dim);

  // Blips are drawn crisply whatever their age, and a radar that has stopped
  // updating looks exactly like one that is working. The list view has always
  // carried the age of the picture; the radar has to as well.
  uint32_t age = traffic_age_s();
  if (age == UINT32_MAX) {
    snprintf(buf, sizeof(buf), "alt x100 ft");
  } else if (age < 600) {
    snprintf(buf, sizeof(buf), "alt x100 ft · %ds ago", (int)age);
  } else {
    snprintf(buf, sizeof(buf), "alt x100 ft · stale");
  }
  ui_text(ctx, GRect(b.origin.x, fy + 14, b.size.w, 13),
          FONT_KEY_GOTHIC_09, buf, GTextAlignmentCenter,
          traffic_is_stale() ? ui_warn : ui_dim);
}

// ------------------------------------------------------------------- list

static void draw_list(GContext *ctx, GRect b) {
  int16_t y = b.origin.y;
  char buf[28], buf2[48];
  int16_t bottom = b.origin.y + b.size.h;

  for (uint8_t i = 0; i < g.tfc_count && i < MAX_TRAFFIC; i++) {
    if (y + 30 > bottom) break;
    Traffic *t = &g.tfc[i];
    GColor c = is_close(t) ? ui_warn : ui_fg;

    // The range is the number that matters most, so it gets the width it
    // needs; two-digit distances used to be clipped to "12.9...".
    const int16_t dist_w = 62;
    GRect r1 = row(b, &y, 17);
    ui_text(ctx, GRect(r1.origin.x, r1.origin.y - 3, r1.size.w - dist_w - 2, 20),
            FONT_KEY_GOTHIC_18_BOLD, t->call[0] ? t->call : "(no id)",
            GTextAlignmentLeft, c);
    fmt_dist(buf, sizeof(buf), (float)t->dist_x10 / 10.0f);
    strncat(buf, "NM", sizeof(buf) - strlen(buf) - 1);
    ui_text(ctx, GRect(r1.origin.x + r1.size.w - dist_w, r1.origin.y - 2, dist_w, 20),
            FONT_KEY_GOTHIC_18_BOLD, buf, GTextAlignmentRight, c);

    GRect r2 = row(b, &y, 15);
    clock_position(buf, sizeof(buf), t);

    // Relative altitude beats absolute: the only question is above or below.
    if (t->alt_ft == UNKNOWN_I32) {
      snprintf(buf2, sizeof(buf2), "%s  alt ?", buf);
    } else if (g.fix.valid) {
      int32_t split = t->alt_ft - g.fix.alt_ft;
      const char *vs = t->vs_fpm > 300 ? "^" : (t->vs_fpm < -300 ? "v" : " ");
      snprintf(buf2, sizeof(buf2), "%s  %+d ft %s", buf, (int)split, vs);
    } else {
      snprintf(buf2, sizeof(buf2), "%s  %d ft", buf, (int)t->alt_ft);
    }
    ui_text(ctx, GRect(r2.origin.x, r2.origin.y - 4, r2.size.w, 18),
            FONT_KEY_GOTHIC_14, buf2, GTextAlignmentLeft, ui_dim);

    if (i + 1 < g.tfc_count) ui_hline(ctx, b, y, ui_dim);
    y += 2;
  }

  if (g.tfc_ts > 0 && y + 16 <= bottom) {
    int age = (int)(time(NULL) - g.tfc_ts);
    snprintf(buf, sizeof(buf), "%ds ago · %d NM", age, g.cfg.traffic_radius_nm);
    ui_text(ctx, GRect(b.origin.x, bottom - 16, b.size.w, 16),
            FONT_KEY_GOTHIC_14, buf, GTextAlignmentCenter, ui_dim);
  }
}

void page_traffic_render(GContext *ctx, GRect b) {
  char buf[28];

  if (!g.cfg.traffic_enable) {
    ui_text(ctx, GRect(b.origin.x, b.origin.y + 30, b.size.w, 40),
            FONT_KEY_GOTHIC_18_BOLD, "TRAFFIC OFF", GTextAlignmentCenter, ui_dim);
    return;
  }
  if (g.tfc_count == 0) {
    // Two different things used to look identical here. Never having heard
    // from the phone is not the same as having looked and found nothing, and
    // neither of them is good news: an empty page means no information, not
    // no traffic, so it is not drawn in the colour that means "all clear".
    bool ever = g.tfc_ts != 0;
    ui_text(ctx, GRect(b.origin.x, b.origin.y + 24, b.size.w, 30),
            FONT_KEY_GOTHIC_24_BOLD, ever ? "NONE SEEN" : "NO DATA",
            GTextAlignmentCenter, ever ? ui_fg : ui_warn);
    if (!ever) {
      snprintf(buf, sizeof(buf), "no traffic report yet");
    } else if (traffic_is_stale()) {
      snprintf(buf, sizeof(buf), "last checked %ds ago", (int)traffic_age_s());
    } else {
      snprintf(buf, sizeof(buf), "within %d NM", g.cfg.traffic_radius_nm);
    }
    ui_text(ctx, GRect(b.origin.x, b.origin.y + 54, b.size.w, 20),
            FONT_KEY_GOTHIC_14, buf, GTextAlignmentCenter,
            traffic_is_stale() ? ui_warn : ui_dim);
    ui_text(ctx, GRect(b.origin.x, b.origin.y + 76, b.size.w, 40),
            FONT_KEY_GOTHIC_14,
            "ADS-B only. Not all aircraft transmit.", GTextAlignmentCenter, ui_dim);
    return;
  }

  if (g.cfg.traffic_radar) {
    draw_radar(ctx, b);
  } else {
    draw_list(ctx, b);
  }
}

// Cycle what the top of the radar means: track, watch compass, then north.
// The footer names the current mode, so the change is visible and needs no
// buzz to announce itself.
void page_traffic_select(void) {
  g.cfg.traffic_orient = (uint8_t)((g.cfg.traffic_orient + 1) % 3);
  compass_apply_mode();
  state_save_soon();
}

// Switching between the plan view and the list is a display choice, so it does
// not go back to the network; the feed refreshes on its own timer.
void page_traffic_select_long(void) {
  g.cfg.traffic_radar = !g.cfg.traffic_radar;
  state_save_soon();
}
