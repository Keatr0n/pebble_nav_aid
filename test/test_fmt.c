// Display formatting, on the host.
//
// These strings are the whole of what a pilot actually reads, and two of them
// have to survive being read in a hurry: a distance that must not round the
// wrong way at a tenth, and a clock that must not leave an arrival time
// twelve hours ambiguous on a watch set to twelve-hour time.

#include "nav.h"
#include <stdlib.h>

AppState g;
GColor ui_bg, ui_fg, ui_dim, ui_accent, ui_warn, ui_good;

static bool s_24h;
bool clock_is_24h_style(void) { return s_24h; }

static int failures;

static void expect(const char *what, const char *got, const char *want) {
  bool ok = strcmp(got, want) == 0;
  if (!ok) failures++;
  printf("  %s %-30s got %-10s want %s\n", ok ? "ok  " : "FAIL", what, got, want);
}

// A fixed local time, built through mktime so it is whatever the host's
// timezone makes of it and localtime() hands the same wall clock back.
static time_t at(int hour, int min) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = 126;  // 2026
  t.tm_mon = 8;
  t.tm_mday = 17;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_isdst = -1;
  return mktime(&t);
}

int main(void) {
  char buf[32];

  printf("24-hour watch:\n");
  s_24h = true;
  fmt_clock(buf, sizeof(buf), at(7, 30));  expect("morning", buf, "07:30");
  fmt_clock(buf, sizeof(buf), at(19, 30)); expect("evening", buf, "19:30");
  fmt_clock(buf, sizeof(buf), at(0, 5));   expect("after midnight", buf, "00:05");

  printf("\n12-hour watch: an ETA must not be twelve hours ambiguous\n");
  s_24h = false;
  fmt_clock(buf, sizeof(buf), at(7, 30));  expect("morning", buf, "07:30a");
  fmt_clock(buf, sizeof(buf), at(19, 30)); expect("evening", buf, "07:30p");
  fmt_clock(buf, sizeof(buf), at(0, 5));   expect("midnight is am", buf, "12:05a");
  fmt_clock(buf, sizeof(buf), at(12, 5));  expect("noon is pm", buf, "12:05p");

  // Morning and evening differed by nothing at all before.
  char am[32], pm[32];
  fmt_clock(am, sizeof(am), at(7, 30));
  fmt_clock(pm, sizeof(pm), at(19, 30));
  if (strcmp(am, pm) == 0) {
    printf("  FAIL 07:30 and 19:30 render identically\n");
    failures++;
  } else {
    printf("  ok   07:30 and 19:30 are distinguishable\n");
  }

  printf("\na short buffer must still be terminated, not half a time:\n");
  char tiny[7];
  memset(tiny, 'x', sizeof(tiny));
  fmt_clock(tiny, sizeof(tiny), at(19, 30));
  if (strlen(tiny) >= sizeof(tiny)) {
    printf("  FAIL fmt_clock overran a %d byte buffer\n", (int)sizeof(tiny));
    failures++;
  } else {
    printf("  ok   fits a %d byte buffer as \"%s\"\n", (int)sizeof(tiny), tiny);
  }

  printf("\ndistances:\n");
  fmt_dist(buf, sizeof(buf), 12.34f);   expect("under 100, a tenth", buf, "12.3");
  fmt_dist(buf, sizeof(buf), 9.96f);    expect("rounds up cleanly", buf, "10.0");
  fmt_dist(buf, sizeof(buf), 0.04f);    expect("almost nothing", buf, "0.0");
  fmt_dist(buf, sizeof(buf), 123.4f);   expect("past 100, whole", buf, "123");
  fmt_dist(buf, sizeof(buf), -1.0f);    expect("no answer", buf, "--.-");

  printf("\nelapsed times:\n");
  fmt_hms(buf, sizeof(buf), 59);        expect("under a minute", buf, "00:59");
  fmt_hms(buf, sizeof(buf), 3661);      expect("past an hour", buf, "1:01:01");
  fmt_hm(buf, sizeof(buf), 3661);       expect("hours and minutes", buf, "1:01");

  printf("\n%s\n", failures ? "FORMAT TESTS FAILED" : "FORMAT TESTS PASSED");
  return failures ? 1 : 0;
}
