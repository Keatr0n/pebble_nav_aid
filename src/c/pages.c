#include "nav.h"

static const Page s_all_pages[] = {
  { "NAV",     page_nav_render,     page_nav_select,     page_nav_select_long },
  { "TIMER",   page_timer_render,   page_timer_select,   page_timer_select_long },
  { "FUEL",    page_fuel_render,    page_fuel_select,    page_fuel_select_long },
  { "TRAFFIC", page_traffic_render, page_traffic_select, page_traffic_select_long },
  { "WX",      page_wx_render,      page_wx_select,      page_wx_select_long },
  { "INFO",    page_info_render,    page_info_select,    page_info_select_long },
};
#define ALL_PAGE_COUNT (int)(sizeof(s_all_pages) / sizeof(s_all_pages[0]))

static Layer *s_layer;
static int s_current;  // index into the enabled list, not into s_all_pages

// The user can switch pages off in settings; everything here works in terms of
// the enabled subset so a disabled page never gets a dot or a turn.
static int enabled_list(int *out) {
  int n = 0;
  for (int i = 0; i < ALL_PAGE_COUNT; i++) {
    if (g.cfg.pages_mask & (1 << i)) out[n++] = i;
  }
  if (n == 0) out[n++] = 0;  // never strand the user on a blank app
  return n;
}

// The phone is told the absolute page id, not the position in the enabled
// list, so turning pages off in settings cannot change what "traffic" means.
static void announce_page(void) {
  int list[ALL_PAGE_COUNT];
  int n = enabled_list(list);
  if (s_current >= n) s_current = 0;
  comm_send_page((uint8_t)list[s_current]);
}

void pages_announce(void) { announce_page(); }

void pages_set_layer(Layer *layer) { s_layer = layer; }
void pages_mark_dirty(void) { if (s_layer) layer_mark_dirty(s_layer); }
int pages_current(void) { return s_current; }

void pages_render(GContext *ctx, GRect bounds) {
  int list[ALL_PAGE_COUNT];
  int n = enabled_list(list);
  if (s_current >= n) s_current = 0;
  const Page *p = &s_all_pages[list[s_current]];

  ui_fill_bg(ctx, bounds);

  // A round display is narrowest at the top, exactly where the header sits.
  // The body gets a modest inset, and the header band is pulled in further
  // still so the title and clock are not sliced off by the curve.
  GRect area = bounds;
  GRect header_area;
#if PBL_ROUND
  // Insets are a fraction of the diameter rather than fixed pixels: the round
  // hardware is 260 px across, and values tuned for a 180 px watch would throw
  // away a third of the glass.
  const int16_t d = PBL_DISPLAY_WIDTH;
  area = grect_inset(bounds, GEdgeInsets(d * 10 / 100, d * 15 / 100,
                                         d * 14 / 100, d * 15 / 100));
  header_area = grect_inset(area, GEdgeInsets(0, d * 7 / 100, 0, d * 7 / 100));
#else
  area = grect_inset(bounds, GEdgeInsets(2, 4, 2, 4));
  header_area = area;
#endif

  int16_t y = ui_header(ctx, header_area, p->title, s_current, n);
  GRect body = GRect(area.origin.x, y, area.size.w, area.origin.y + area.size.h - y);
  p->render(ctx, body);
}

void pages_next(void) {
  int list[ALL_PAGE_COUNT];
  int n = enabled_list(list);
  s_current = (s_current + 1) % n;
  announce_page();
  pages_mark_dirty();
}

void pages_prev(void) {
  int list[ALL_PAGE_COUNT];
  int n = enabled_list(list);
  s_current = (s_current + n - 1) % n;
  announce_page();
  pages_mark_dirty();
}

void pages_select(void) {
  int list[ALL_PAGE_COUNT];
  int n = enabled_list(list);
  if (s_current >= n) return;
  const Page *p = &s_all_pages[list[s_current]];
  if (p->select) p->select();
  pages_mark_dirty();
}

void pages_select_long(void) {
  int list[ALL_PAGE_COUNT];
  int n = enabled_list(list);
  if (s_current >= n) return;
  const Page *p = &s_all_pages[list[s_current]];
  if (p->select_long) p->select_long();
  pages_mark_dirty();
}
