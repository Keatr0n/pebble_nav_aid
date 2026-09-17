#include "nav.h"
#include "trig.h"

// Under 100 NM a tenth is meaningful and fits; past that it is noise.
void fmt_dist(char *buf, size_t n, float nm) {
  if (!nav_isfinite(nm) || nm < 0.0f) {
    snprintf(buf, n, "--.-");
  } else if (nm < 100.0f) {
    int whole = (int)nm;
    int tenth = (int)((nm - (float)whole) * 10.0f + 0.5f);
    if (tenth >= 10) { whole += 1; tenth = 0; }
    snprintf(buf, n, "%d.%d", whole, tenth);
  } else {
    snprintf(buf, n, "%d", (int)(nm + 0.5f));
  }
}

void fmt_hms(char *buf, size_t n, uint32_t secs) {
  uint32_t h = secs / 3600;
  uint32_t m = (secs % 3600) / 60;
  uint32_t s = secs % 60;
  if (h > 0) {
    snprintf(buf, n, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  } else {
    snprintf(buf, n, "%02u:%02u", (unsigned)m, (unsigned)s);
  }
}

void fmt_hm(char *buf, size_t n, uint32_t secs) {
  uint32_t h = secs / 3600;
  uint32_t m = (secs % 3600) / 60;
  snprintf(buf, n, "%u:%02u", (unsigned)h, (unsigned)m);
}

// On a 12-hour watch the meridiem is not decoration: this formats ETAs and
// sunset as well as the header clock, and "07:30" for an arrival is twelve
// hours ambiguous on exactly the page a VFR pilot uses to think about last
// light. A single trailing letter costs less width than " AM" and is still
// unmistakable.
void fmt_clock(char *buf, size_t n, time_t when) {
  struct tm *t = localtime(&when);
  if (!t) { snprintf(buf, n, "--:--"); return; }
  if (clock_is_24h_style()) {
    strftime(buf, n, "%H:%M", t);
    return;
  }
  strftime(buf, n, "%I:%M", t);
  size_t len = strlen(buf);
  if (len + 2 <= n) {
    buf[len] = t->tm_hour < 12 ? 'a' : 'p';
    buf[len + 1] = '\0';
  }
}

void fmt_course(char *buf, size_t n, float true_deg) {
  int32_t d = geo_to_magnetic(true_deg);
  snprintf(buf, n, "%03d%s", (int)d, (g.cfg.magnetic && g.decl_valid) ? "M" : "T");
}

void fmt_fuel(char *buf, size_t n, float qty) {
  if (qty < 0.0f) qty = 0.0f;
  // Litres and pounds come in large enough numbers that a decimal is clutter.
  if (g.cfg.fuel_unit == FUEL_LITRE || g.cfg.fuel_unit == FUEL_LB || qty >= 100.0f) {
    snprintf(buf, n, "%d", (int)(qty + 0.5f));
  } else {
    int whole = (int)qty;
    int tenth = (int)((qty - (float)whole) * 10.0f + 0.5f);
    if (tenth >= 10) { whole += 1; tenth = 0; }
    snprintf(buf, n, "%d.%d", whole, tenth);
  }
}

const char *fmt_fuel_unit(void) {
  switch (g.cfg.fuel_unit) {
    case FUEL_LITRE:  return "L";
    case FUEL_IMPGAL: return "IG";
    case FUEL_KG:     return "KG";
    case FUEL_LB:     return "LB";
    default:          return "GAL";
  }
}
