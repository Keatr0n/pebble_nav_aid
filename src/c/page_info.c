#include "nav.h"

static bool fits_info(GRect b, int16_t y, int16_t h) {
  return y + h <= b.origin.y + b.size.h;
}

static GRect row(GRect b, int16_t *y, int16_t h) {
  GRect r = GRect(b.origin.x, *y, b.size.w, h);
  *y += h;
  return r;
}

// Degrees and decimal minutes: the format position reports are given in.
static void fmt_latlon(char *buf, size_t n, int32_t lat_e6, int32_t lon_e6) {
  int32_t alat = lat_e6 < 0 ? -lat_e6 : lat_e6;
  int32_t alon = lon_e6 < 0 ? -lon_e6 : lon_e6;
  int latd = (int)(alat / 1000000);
  int latm = (int)(((alat % 1000000) * 60) / 1000000);
  int latf = (int)(((((alat % 1000000) * 60) % 1000000) * 10) / 1000000);
  int lond = (int)(alon / 1000000);
  int lonm = (int)(((alon % 1000000) * 60) / 1000000);
  int lonf = (int)(((((alon % 1000000) * 60) % 1000000) * 10) / 1000000);
  snprintf(buf, n, "%c%02d %02d.%d %c%03d %02d.%d",
           lat_e6 < 0 ? 'S' : 'N', latd, latm, latf,
           lon_e6 < 0 ? 'W' : 'E', lond, lonm, lonf);
}

void page_info_render(GContext *ctx, GRect b) {
  int16_t y = b.origin.y;
  char buf[64], buf2[32];

  if (g.fix.valid) {
    fmt_latlon(buf, sizeof(buf), g.fix.lat_e6, g.fix.lon_e6);
    GRect pr = row(b, &y, 18);
    ui_text(ctx, GRect(pr.origin.x, pr.origin.y - 4, pr.size.w, 20),
            FONT_KEY_GOTHIC_14_BOLD, buf, GTextAlignmentCenter, ui_accent);
  } else {
    ui_text(ctx, row(b, &y, 18), FONT_KEY_GOTHIC_14_BOLD,
            g.gps == GPS_DENIED ? "LOCATION DENIED" : "NO FIX",
            GTextAlignmentCenter, ui_warn);
  }
  ui_hline(ctx, b, y, ui_dim);
  y += 3;

  snprintf(buf, sizeof(buf), "%d ft", (int)g.fix.alt_ft);
  ui_label_value(ctx, row(b, &y, 20), "GPS ALT", g.fix.valid ? buf : "--", ui_fg);

  if (g.fix.valid && g.gs_smooth_kt > 5.0f) {
    fmt_course(buf, sizeof(buf), (float)g.fix.trk_x10 / 10.0f);
  } else {
    snprintf(buf, sizeof(buf), "---");
  }
  ui_label_value(ctx, row(b, &y, 20), "TRACK", buf, ui_fg);

  // Top of descent against the far end of the route, on a 3-degree slope
  // (318 ft per NM) down to the configured circuit altitude.
  float dest = nav_dist_to_dest_nm();
  if (dest >= 0.0f && g.fix.alt_ft > (int32_t)g.cfg.pattern_alt + 200) {
    float descend_ft = (float)(g.fix.alt_ft - (int32_t)g.cfg.pattern_alt);
    float tod_nm = descend_ft / 318.0f;
    float togo = dest - tod_nm;
    if (togo > 0.0f) {
      fmt_dist(buf2, sizeof(buf2), togo);
      snprintf(buf, sizeof(buf), "in %s NM", buf2);
      ui_label_value(ctx, row(b, &y, 20), "T/D", buf, ui_fg);
    } else {
      uint32_t ete = nav_ete_s(dest);
      if (ete > 60) {
        int vs = (int)(descend_ft / ((float)ete / 60.0f));
        snprintf(buf, sizeof(buf), "%d fpm", vs);
      } else {
        snprintf(buf, sizeof(buf), "now");
      }
      ui_label_value(ctx, row(b, &y, 20), "DESCEND", buf, ui_accent);
    }
  }

  // Last light drives the go/no-go for a VFR pilot more often than anything
  // else on this screen.
  if (g.sunset > 0) {
    fmt_clock(buf2, sizeof(buf2), g.sunset);
    time_t left = g.sunset - time(NULL);
    if (left > 0 && left < 24 * 3600) {
      char hm[12];
      fmt_hm(hm, sizeof(hm), (uint32_t)left);
      snprintf(buf, sizeof(buf), "%s (%s)", buf2, hm);
    } else {
      snprintf(buf, sizeof(buf), "%s", buf2);
    }
    ui_label_value(ctx, row(b, &y, 20), "SUNSET", buf,
                   (left > 0 && left < 1800) ? ui_warn : ui_fg);
  }

  if (fits_info(b, y, 20)) {
    uint32_t age = fix_age_s();
    if (!g.fix.valid) {
      snprintf(buf, sizeof(buf), "--");
    } else if (age < 3) {
      snprintf(buf, sizeof(buf), "live");
    } else if (age < 600) {
      snprintf(buf, sizeof(buf), "%ds ago", (int)age);
    } else {
      snprintf(buf, sizeof(buf), "stale");
    }
    ui_label_value(ctx, row(b, &y, 20), "GPS FIX", buf,
                   fix_is_stale() ? ui_warn : ui_dim);
  }

  if (g.decl_valid) {
    int v = g.decl_x10;
    snprintf(buf, sizeof(buf), "%d.%d%s", (v < 0 ? -v : v) / 10, (v < 0 ? -v : v) % 10,
             v < 0 ? "W" : "E");
    ui_label_value(ctx, row(b, &y, 20), "VAR", buf, ui_dim);
  }

  // Anything the phone could not resolve is shown here rather than silently
  // dropped, so a mistyped or retired ident is visible before departure.
  if (g.status[0] != '\0') {
    GRect er = row(b, &y, 18);
    if (er.origin.y + er.size.h <= b.origin.y + b.size.h) {
      ui_text(ctx, er, FONT_KEY_GOTHIC_14, g.status, GTextAlignmentCenter, ui_warn);
    }
  }

  GRect sr = row(b, &y, 18);
  if (sr.origin.y + sr.size.h <= b.origin.y + b.size.h) {
    BatteryChargeState bat = battery_state_service_peek();
    snprintf(buf, sizeof(buf), "%s · batt %d%%",
             connection_service_peek_pebble_app_connection() ? "linked" : "NO PHONE",
             (int)bat.charge_percent);
    ui_text(ctx, sr, FONT_KEY_GOTHIC_14, buf, GTextAlignmentCenter,
            connection_service_peek_pebble_app_connection() ? ui_dim : ui_warn);
  }
}

void page_info_select(void) {
  comm_request_route();
  comm_request_wx();
  comm_request_traffic();
}

void page_info_select_long(void) {
  g.cfg.magnetic = !g.cfg.magnetic;
  state_save();
  vibes_short_pulse();
}
