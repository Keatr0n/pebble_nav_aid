#include "nav.h"

// Carve a row off the top of the remaining body and advance the cursor.
static GRect row(GRect b, int16_t *y, int16_t h) {
  GRect r = GRect(b.origin.x, *y, b.size.w, h);
  *y += h;
  return r;
}

static void half_rows(GContext *ctx, GRect r, const char *l1, const char *v1,
                      const char *l2, const char *v2) {
  int16_t hw = r.size.w / 2;
  GRect a = GRect(r.origin.x, r.origin.y, hw - 2, r.size.h);
  GRect b = GRect(r.origin.x + hw + 2, r.origin.y, hw - 2, r.size.h);
  ui_label_value(ctx, a, l1, v1, ui_fg);
  ui_label_value(ctx, b, l2, v2, ui_fg);
}

// True if a row of this height still lands inside the body. Round displays
// give us noticeably less vertical room, so the lower rows are dropped rather
// than drawn off the edge of the glass.
static bool fits(GRect b, int16_t y, int16_t h) {
  return y + h <= b.origin.y + b.size.h;
}

void page_nav_render(GContext *ctx, GRect b) {
  int16_t y = b.origin.y;
  char buf[24], buf2[24];

  if (g.wpt_count == 0) {
    GRect r = GRect(b.origin.x, b.origin.y + 20, b.size.w, 60);
    ui_text(ctx, r, FONT_KEY_GOTHIC_24_BOLD, "NO ROUTE", GTextAlignmentCenter, ui_accent);
    r.origin.y += 30;
    ui_text_h(ctx, r, FONT_KEY_GOTHIC_14, "Add waypoints in the phone settings.",
              GTextAlignmentCenter, ui_dim);
    return;
  }

  Waypoint w;
  if (!nav_active_wpt(&w)) return;

  // Identifier row: which waypoint, and where it sits in the route.
  GRect idr = row(b, &y, 24);
  GRect nr = GRect(idr.origin.x, idr.origin.y - 4, idr.size.w - 34, 26);
  ui_text(ctx, nr, FONT_KEY_GOTHIC_24_BOLD, w.name, GTextAlignmentLeft, ui_fg);
  snprintf(buf, sizeof(buf), "%d/%d", g.wpt_active + 1, g.wpt_count);
  GRect cr = GRect(idr.origin.x + idr.size.w - 34, idr.origin.y + 2, 34, 20);
  ui_text(ctx, cr, FONT_KEY_GOTHIC_14, buf, GTextAlignmentRight, ui_dim);

  float dist = nav_dist_nm();
  float brg = nav_brg_deg();
  float trk = (float)g.fix.trk_x10 / 10.0f;

  if (!g.fix.valid) {
    GRect r = row(b, &y, 50);
    ui_text(ctx, r, FONT_KEY_GOTHIC_24_BOLD,
            g.gps == GPS_DENIED ? "NO GPS PERM" : "ACQUIRING", GTextAlignmentCenter, ui_accent);
  } else {
    fmt_dist(buf, sizeof(buf), dist);
    // A distance computed from an old fix is not wrong so much as no longer
    // true, and it looks identical to a live one. Colour is the cheapest way
    // to stop it being read as current.
    ui_big_value(ctx, row(b, &y, 44), buf, "NM",
                 fix_is_stale() ? ui_warn : ui_fg);

    // The deviation bar only means something once there is a track to deviate
    // from, and it is the first thing to sacrifice when the glass is short:
    // the numeric rows below carry more information per pixel. Round watches
    // therefore keep ETE and ETA and lose the bar.
    int16_t rows_wanted = (g.wpt_active + 1 < g.wpt_count) ? 3 : 2;
    int16_t room_left = (b.origin.y + b.size.h) - y;
    if (g.gs_smooth_kt > 15.0f && room_left >= rows_wanted * 20 + 14) {
      ui_cdi(ctx, row(b, &y, 14), geo_rel_bearing(brg, trk), ui_accent);
    }
  }

  if (fits(b, y, 20)) {
    fmt_course(buf, sizeof(buf), brg < 0.0f ? 0.0f : brg);
    if (g.fix.valid && g.gs_smooth_kt > 5.0f) {
      snprintf(buf2, sizeof(buf2), "%d", (int)(g.gs_smooth_kt + 0.5f));
    } else {
      snprintf(buf2, sizeof(buf2), "--");
    }
    half_rows(ctx, row(b, &y, 20), "BRG", g.fix.valid ? buf : "---", "GS", buf2);
  }

  uint32_t ete = nav_ete_s(dist);
  if (fits(b, y, 20)) {
    if (ete > 0 && ete < 24 * 3600) {
      fmt_hm(buf, sizeof(buf), ete);
      fmt_clock(buf2, sizeof(buf2), time(NULL) + (time_t)ete);
    } else {
      snprintf(buf, sizeof(buf), "--:--");
      snprintf(buf2, sizeof(buf2), "--:--");
    }
    half_rows(ctx, row(b, &y, 20), "ETE", buf, "ETA", buf2);
  }

  // Once there are legs beyond the active one, the number that actually
  // drives decisions is the one to the far end of the route.
  if (g.wpt_active + 1 < g.wpt_count && fits(b, y, 20)) {
    float dest = nav_dist_to_dest_nm();
    uint32_t dete = nav_ete_s(dest);
    fmt_dist(buf, sizeof(buf), dest);
    strncat(buf, " NM", sizeof(buf) - strlen(buf) - 1);
    if (dete > 0 && dete < 24 * 3600) {
      fmt_clock(buf2, sizeof(buf2), time(NULL) + (time_t)dete);
    } else {
      snprintf(buf2, sizeof(buf2), "--:--");
    }
    char lbl[16];
    snprintf(lbl, sizeof(lbl), "%s", g.wpts[g.wpt_count - 1].name);
    half_rows(ctx, row(b, &y, 20), lbl, buf, "ETA", buf2);
  }
}

void page_nav_select(void) {
  state_next_wpt();
}

void page_nav_select_long(void) {
  wpt_menu_show();
}
