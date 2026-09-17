#include "nav.h"

static GRect row(GRect b, int16_t *y, int16_t h) {
  GRect r = GRect(b.origin.x, *y, b.size.w, h);
  *y += h;
  return r;
}

void page_timer_render(GContext *ctx, GRect b) {
  int16_t y = b.origin.y;
  char buf[24];

  uint32_t el = chrono_elapsed_s(&g.stopwatch);

  // Elapsed time is the headline; the LECO digits stay readable in turbulence.
  fmt_hms(buf, sizeof(buf), el);
  const char *font = (el >= 3600) ? FONT_KEY_LECO_32_BOLD_NUMBERS : FONT_KEY_LECO_42_NUMBERS;
  GRect br = row(b, &y, 48);
  graphics_context_set_text_color(ctx, g.stopwatch.running ? ui_good : ui_fg);
  graphics_draw_text(ctx, buf, fonts_get_system_font(font),
                     GRect(br.origin.x, br.origin.y - 6, br.size.w, br.size.h + 8),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  GRect sr = row(b, &y, 16);
  ui_text(ctx, GRect(sr.origin.x, sr.origin.y - 4, sr.size.w, 20),
          FONT_KEY_GOTHIC_14_BOLD,
          g.stopwatch.running ? "RUNNING" : (el > 0 ? "STOPPED" : "READY"),
          GTextAlignmentCenter, g.stopwatch.running ? ui_good : ui_dim);

  ui_hline(ctx, b, y, ui_dim);
  y += 4;

  // Distance banked only while the timer ran -- the leg-distance question.
  float d = chrono_dist_nm(&g.stopwatch);
  fmt_dist(buf, sizeof(buf), d);
  strncat(buf, " NM", sizeof(buf) - strlen(buf) - 1);
  ui_label_value(ctx, row(b, &y, 21), "DISTANCE", buf, ui_fg);

  // Average over the timed period, which is what you use to revise an ETA --
  // not the instantaneous groundspeed.
  if (el > 30 && d > 0.05f) {
    float avg = d / ((float)el / 3600.0f);
    snprintf(buf, sizeof(buf), "%d KT", (int)(avg + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "-- KT");
  }
  ui_label_value(ctx, row(b, &y, 21), "AVG GS", buf, ui_fg);

  if (g.fix.valid && g.gs_smooth_kt > 5.0f) {
    snprintf(buf, sizeof(buf), "%d KT", (int)(g.gs_smooth_kt + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "-- KT");
  }
  ui_label_value(ctx, row(b, &y, 21), "NOW", buf, ui_dim);

  GRect hr = row(b, &y, 18);
  if (hr.origin.y + hr.size.h <= b.origin.y + b.size.h) {
    ui_text(ctx, hr, FONT_KEY_GOTHIC_14, "SEL start/stop · hold reset",
            GTextAlignmentCenter, ui_dim);
  }
}

void page_timer_select(void) { chrono_toggle(&g.stopwatch); }
void page_timer_select_long(void) { chrono_reset(&g.stopwatch); vibes_short_pulse(); }
