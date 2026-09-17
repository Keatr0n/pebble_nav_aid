#include "nav.h"
#include "trig.h"

static bool s_show_raw;

static GRect row(GRect b, int16_t *y, int16_t h) {
  GRect r = GRect(b.origin.x, *y, b.size.w, h);
  *y += h;
  return r;
}

static const char *cat_name(uint8_t c) {
  switch (c) {
    case 0: return "VFR";
    case 1: return "MVFR";
    case 2: return "IFR";
    case 3: return "LIFR";
    default: return "";
  }
}

static GColor cat_color(uint8_t c) {
  switch (c) {
    case 0: return ui_good;
    case 1: return ui_accent;
    default: return ui_warn;
  }
}

void page_wx_render(GContext *ctx, GRect b) {
  int16_t y = b.origin.y;
  char buf[40], buf2[40];

  if (!g.wx.valid) {
    ui_text(ctx, GRect(b.origin.x, b.origin.y + 30, b.size.w, 40),
            FONT_KEY_GOTHIC_18_BOLD, "NO METAR", GTextAlignmentCenter, ui_dim);
    ui_text(ctx, GRect(b.origin.x, b.origin.y + 58, b.size.w, 20),
            FONT_KEY_GOTHIC_14, "SEL to fetch", GTextAlignmentCenter, ui_dim);
    return;
  }

  if (s_show_raw) {
    GRect r = GRect(b.origin.x, b.origin.y, b.size.w, b.size.h);
    ui_text_h(ctx, r, FONT_KEY_GOTHIC_14, g.wx.raw, GTextAlignmentLeft, ui_fg);
    return;
  }

  // Station, observation age, and the flight category as a colour.
  GRect hr = row(b, &y, 22);
  ui_text(ctx, GRect(hr.origin.x, hr.origin.y - 4, hr.size.w - 46, 22),
          FONT_KEY_GOTHIC_18_BOLD, g.wx.station, GTextAlignmentLeft, ui_fg);
  ui_text(ctx, GRect(hr.origin.x + hr.size.w - 46, hr.origin.y - 3, 46, 22),
          FONT_KEY_GOTHIC_18_BOLD, cat_name(g.wx.cat), GTextAlignmentRight,
          cat_color(g.wx.cat));
  ui_hline(ctx, b, y, ui_dim);
  y += 3;

  if (g.wx.wdir == UNKNOWN_I16) {
    snprintf(buf, sizeof(buf), "VRB %d", (int)g.wx.wspd);
  } else if (g.wx.gust > 0) {
    snprintf(buf, sizeof(buf), "%03d/%dG%d", (int)g.wx.wdir, (int)g.wx.wspd, (int)g.wx.gust);
  } else {
    snprintf(buf, sizeof(buf), "%03d/%d", (int)g.wx.wdir, (int)g.wx.wspd);
  }
  ui_label_value(ctx, row(b, &y, 20), "WIND", buf, ui_fg);

  // Head/tail and crosswind resolved against the current track. Not a runway
  // wind component, but it tells you what the air is doing to you right now.
  if (g.wx.wdir != UNKNOWN_I16 && g.gs_smooth_kt > 20.0f) {
    float ang = geo_rel_bearing((float)g.wx.wdir, (float)g.fix.trk_x10 / 10.0f);
    float rad = ang * 3.14159265f / 180.0f;
    int head = (int)((float)g.wx.wspd * nav_cosf(rad) + 0.5f);
    float x = (float)g.wx.wspd * nav_sinf(rad);
    int cross = (int)((x < 0.0f ? -x : x) + 0.5f);
    snprintf(buf, sizeof(buf), "%s%d  X%d",
             head >= 0 ? "H" : "T", head >= 0 ? head : -head, cross);
    ui_label_value(ctx, row(b, &y, 20), "ON TRACK", buf, ui_fg);
  }

  if (g.cfg.altim_unit == 1) {
    int inhg = (int)(((float)g.wx.altim_hpa_x10 / 10.0f) * 0.2953f * 100.0f + 0.5f);
    snprintf(buf, sizeof(buf), "%d.%02d", inhg / 100, inhg % 100);
  } else {
    snprintf(buf, sizeof(buf), "Q%d", (int)((g.wx.altim_hpa_x10 + 5) / 10));
  }
  ui_label_value(ctx, row(b, &y, 20), "QNH", buf, ui_fg);

  snprintf(buf, sizeof(buf), "%d/%d C", (int)g.wx.temp_c, (int)g.wx.dewp_c);
  ui_label_value(ctx, row(b, &y, 20), "TEMP/DP", buf, ui_fg);

  // Density altitude is the one that bites on a hot day at a short strip.
  float da = geo_density_alt_ft(g.wx.elev_ft, g.wx.temp_c, g.wx.altim_hpa_x10);
  snprintf(buf, sizeof(buf), "%d ft", (int)(da + 0.5f));
  ui_label_value(ctx, row(b, &y, 20), "DENS ALT", buf,
                 da > (float)g.wx.elev_ft + 2000.0f ? ui_accent : ui_fg);

  GRect vr = row(b, &y, 20);
  if (vr.origin.y + vr.size.h <= b.origin.y + b.size.h) {
    if (g.wx.ceil_ft >= 0) {
      snprintf(buf2, sizeof(buf2), "%d ft", (int)g.wx.ceil_ft);
    } else {
      snprintf(buf2, sizeof(buf2), "none");
    }
    ui_label_value(ctx, vr, "CEILING", buf2, ui_fg);
  }

  GRect ar = row(b, &y, 16);
  if (ar.origin.y + ar.size.h <= b.origin.y + b.size.h && g.wx.obs_ts > 0) {
    int mins = (int)((time(NULL) - g.wx.obs_ts) / 60);
    snprintf(buf, sizeof(buf), "obs %d min ago · hold for raw", mins);
    ui_text(ctx, ar, FONT_KEY_GOTHIC_14, buf, GTextAlignmentCenter,
            mins > 90 ? ui_warn : ui_dim);
  }
}

void page_wx_select(void) { comm_request_wx(); }
void page_wx_select_long(void) { s_show_raw = !s_show_raw; }
