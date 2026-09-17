#include "nav.h"
#include "trig.h"

#define PI_F 3.14159265358979f

GColor ui_bg, ui_fg, ui_dim, ui_accent, ui_warn, ui_good;

// Dark ground throughout: this gets used at night in an unlit cockpit, and a
// white screen at 2200 local ruins night vision for several minutes.
void ui_init_colors(void) {
  ui_bg = GColorBlack;
  ui_fg = GColorWhite;
  ui_dim = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);
  ui_accent = PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite);
  ui_warn = PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
  ui_good = PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite);
}

void ui_fill_bg(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, ui_bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
}

void ui_text(GContext *ctx, GRect r, const char *font, const char *s,
             GTextAlignment al, GColor c) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, fonts_get_system_font(font), r,
                     GTextOverflowModeTrailingEllipsis, al, NULL);
}

int16_t ui_text_h(GContext *ctx, GRect r, const char *font, const char *s,
                  GTextAlignment al, GColor c) {
  GFont f = fonts_get_system_font(font);
  GSize sz = graphics_text_layout_get_content_size(
      s, f, r, GTextOverflowModeWordWrap, al);
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeWordWrap, al, NULL);
  return sz.h;
}

void ui_hline(GContext *ctx, GRect b, int16_t y, GColor c) {
  graphics_context_set_stroke_color(ctx, c);
  graphics_draw_line(ctx, GPoint(b.origin.x, y), GPoint(b.origin.x + b.size.w, y));
}

// Title bar: page name, position dots, and the wall clock.
//
// The clock sits on every page on purpose. Time of day drives so much of a
// flight -- circuit deadlines, last light, position report times, the next ETA
// -- that having to leave whatever page you are on to read it would be a
// design failure.
int16_t ui_header(GContext *ctx, GRect b, const char *title, int page, int page_count) {
  const int16_t h = PBL_IF_RECT_ELSE(18, 22);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  char clock[12];
  fmt_clock(clock, sizeof(clock), time(NULL));
  GSize cs = graphics_text_layout_get_content_size(
      clock, f, GRect(0, 0, 80, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);

  GRect cr = GRect(b.origin.x + b.size.w - cs.w, b.origin.y - 3, cs.w, h + 4);
  ui_text(ctx, cr, FONT_KEY_GOTHIC_14_BOLD, clock, GTextAlignmentRight, ui_fg);

  GRect tr = GRect(b.origin.x + 1, b.origin.y - 3, b.size.w - cs.w - 6, h + 4);
  ui_text(ctx, tr, FONT_KEY_GOTHIC_14_BOLD, title, GTextAlignmentLeft, ui_accent);

  // Dots tuck in between the title and the clock. A round display has no
  // width to spare up here, so the page name alone carries the position.
#if PBL_ROUND
  (void)page;
  (void)page_count;
#else
  const int16_t dot_r = 2;
  const int16_t gap = 6;
  int16_t total = (page_count - 1) * gap;
  int16_t cx = b.origin.x + b.size.w - cs.w - 8 - total;
  int16_t cy = b.origin.y + h / 2;
  for (int i = 0; i < page_count; i++) {
    if (i == page) {
      graphics_context_set_fill_color(ctx, ui_accent);
      graphics_fill_circle(ctx, GPoint(cx + i * gap, cy), dot_r);
    } else {
      graphics_context_set_stroke_color(ctx, ui_dim);
      graphics_draw_circle(ctx, GPoint(cx + i * gap, cy), dot_r - 1);
    }
  }
#endif
  ui_hline(ctx, b, b.origin.y + h, ui_dim);
  return b.origin.y + h + 2;
}

// Small dim label on the left, value right-aligned. The workhorse row.
void ui_label_value(GContext *ctx, GRect r, const char *label, const char *value, GColor vc) {
  GRect lr = r;
  lr.origin.y -= 3;
  ui_text(ctx, lr, FONT_KEY_GOTHIC_14, label, GTextAlignmentLeft, ui_dim);
  GRect vr = r;
  vr.origin.y -= 4;
  ui_text(ctx, vr, FONT_KEY_GOTHIC_18_BOLD, value, GTextAlignmentRight, vc);
}

// One dominant number with a small unit tucked to its right. The number is
// what a pilot reads in a half-second glance, so it gets the biggest font
// that still fits the platform.
void ui_big_value(GContext *ctx, GRect r, const char *value, const char *suffix, GColor c) {
  // Chosen on width, not on whether the screen is round: the round watch is
  // the widest of the three and has the most room for big digits.
  const char *num_font = (PBL_DISPLAY_WIDTH > 160)
                             ? FONT_KEY_LECO_42_NUMBERS
                             : FONT_KEY_LECO_36_BOLD_NUMBERS;

  GFont f = fonts_get_system_font(num_font);
  GSize sz = graphics_text_layout_get_content_size(
      value, f, r, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

  int16_t suffix_w = suffix ? 30 : 0;
  int16_t x = r.origin.x + (r.size.w - sz.w - suffix_w) / 2;
  if (x < r.origin.x) x = r.origin.x;

  GRect nr = GRect(x, r.origin.y, sz.w + 4, r.size.h);
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, value, f, nr, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);

  if (suffix) {
    GRect sr = GRect(x + sz.w + 3, r.origin.y + sz.h - 22, suffix_w, 20);
    ui_text(ctx, sr, FONT_KEY_GOTHIC_14_BOLD, suffix, GTextAlignmentLeft, ui_dim);
  }
}

// Filled triangle pointing `deg` away from straight up, inside a ring.
// Straight up means "the waypoint is dead ahead along your current track".
void ui_arrow(GContext *ctx, GPoint centre, int16_t radius, float deg, GColor c) {
  graphics_context_set_stroke_color(ctx, ui_dim);
  graphics_draw_circle(ctx, centre, radius);

  float a = deg * (PI_F / 180.0f);
  float tipr = (float)radius - 1.0f;
  float tailr = (float)radius * 0.85f;

  static GPoint pts[3];
  static GPathInfo info = { .num_points = 3, .points = pts };

  pts[0] = GPoint((int16_t)(centre.x + tipr * nav_sinf(a)),
                  (int16_t)(centre.y - tipr * nav_cosf(a)));
  pts[1] = GPoint((int16_t)(centre.x + tailr * nav_sinf(a + 2.5f)),
                  (int16_t)(centre.y - tailr * nav_cosf(a + 2.5f)));
  pts[2] = GPoint((int16_t)(centre.x + tailr * nav_sinf(a - 2.5f)),
                  (int16_t)(centre.y - tailr * nav_cosf(a - 2.5f)));

  GPath *p = gpath_create(&info);
  if (!p) return;
  graphics_context_set_fill_color(ctx, c);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// Course deviation bar: centre tick is on-track, the block slides toward the
// side you must turn. Full scale is +/-30 degrees of track angle error.
void ui_cdi(GContext *ctx, GRect r, float rel_brg, GColor c) {
  int16_t cy = r.origin.y + r.size.h / 2;
  int16_t half = r.size.w / 2 - 4;
  int16_t cx = r.origin.x + r.size.w / 2;

  graphics_context_set_stroke_color(ctx, ui_dim);
  graphics_draw_line(ctx, GPoint(cx - half, cy), GPoint(cx + half, cy));
  for (int i = -2; i <= 2; i++) {
    int16_t x = cx + (int16_t)((half * i) / 2);
    graphics_draw_line(ctx, GPoint(x, cy - 2), GPoint(x, cy + 2));
  }

  float clamped = rel_brg;
  if (clamped > 30.0f) clamped = 30.0f;
  if (clamped < -30.0f) clamped = -30.0f;
  int16_t px = cx + (int16_t)((clamped / 30.0f) * (float)half);

  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, GRect(px - 3, cy - 5, 6, 11), 1, GCornersAll);
}
