#include "nav.h"

static GRect row(GRect b, int16_t *y, int16_t h) {
  GRect r = GRect(b.origin.x, *y, b.size.w, h);
  *y += h;
  return r;
}

void page_fuel_render(GContext *ctx, GRect b) {
  int16_t y = b.origin.y;
  char buf[28], buf2[48];

  float rem = fuel_remaining();
  uint32_t end_s = fuel_endurance_s();
  uint32_t reserve_s = (uint32_t)g.cfg.fuel_reserve_min * 60;
  bool into_reserve = end_s <= reserve_s;

  fmt_fuel(buf, sizeof(buf), rem);
  ui_big_value(ctx, row(b, &y, 44), buf, fmt_fuel_unit(),
               into_reserve ? ui_warn : ui_fg);

  GRect lr = row(b, &y, 15);
  ui_text(ctx, GRect(lr.origin.x, lr.origin.y - 4, lr.size.w, 20),
          FONT_KEY_GOTHIC_14_BOLD, "REMAINING", GTextAlignmentCenter, ui_dim);

  ui_hline(ctx, b, y, ui_dim);
  y += 3;

  // Endurance is the number that decides diversions, so it sits directly under
  // the quantity and turns red the moment it eats into the planned reserve.
  fmt_hm(buf, sizeof(buf), end_s);
  ui_label_value(ctx, row(b, &y, 20), "ENDURANCE", buf,
                 into_reserve ? ui_warn : ui_good);

  fmt_fuel(buf, sizeof(buf), fuel_used());
  ui_label_value(ctx, row(b, &y, 20), "USED", buf, ui_fg);

  // Fuel over destination: what is left after flying the rest of the route at
  // the current groundspeed. Negative is a decision, not a display quirk.
  float dest = nav_dist_to_dest_nm();
  uint32_t dete = nav_ete_s(dest);
  if (dest >= 0.0f && dete > 0 && dete < 24 * 3600) {
    float burn = (float)g.cfg.fuel_burn_x10 / 10.0f;
    float fod = rem - burn * ((float)dete / 3600.0f);
    if (fod < 0.0f) {
      snprintf(buf, sizeof(buf), "NONE");
    } else {
      fmt_fuel(buf, sizeof(buf), fod);
    }
    snprintf(buf2, sizeof(buf2), "%s @ %s", buf,
             g.wpts[g.wpt_count ? g.wpt_count - 1 : 0].name);
    ui_label_value(ctx, row(b, &y, 20), "ON ARRIVAL", buf2,
                   fod < (burn * ((float)reserve_s / 3600.0f)) ? ui_warn : ui_fg);
  } else {
    ui_label_value(ctx, row(b, &y, 20), "ON ARRIVAL", "--", ui_dim);
  }

  fmt_hm(buf, sizeof(buf), chrono_elapsed_s(&g.hobbs));
  snprintf(buf2, sizeof(buf2), "%s%s", buf, g.hobbs.running ? "" : " (off)");
  ui_label_value(ctx, row(b, &y, 20), "ENGINE", buf2,
                 g.hobbs.running ? ui_good : ui_dim);

  GRect hr = row(b, &y, 18);
  if (hr.origin.y + hr.size.h <= b.origin.y + b.size.h) {
    ui_text(ctx, hr, FONT_KEY_GOTHIC_14, "SEL clock · hold refuel",
            GTextAlignmentCenter, ui_dim);
  }
}

void page_fuel_select(void) { chrono_toggle(&g.hobbs); }

// "Refuel": the tanks are full again and the clock starts from zero.
void page_fuel_select_long(void) {
  chrono_reset(&g.hobbs);
  vibes_short_pulse();
}
