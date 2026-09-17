#include "nav.h"

// Direct-to: jump the active waypoint anywhere in the route without flying
// past everything in between. Reached by holding SELECT on the nav page.

static Window *s_window;
static MenuLayer *s_menu;

static uint16_t get_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  return g.wpt_count;
}

static int16_t get_cell_height(MenuLayer *m, MenuIndex *idx, void *ctx) {
  // menu_cell_basic_draw sets its own type sizes per platform, and the larger
  // screens need the extra room or the title and subtitle collide.
  return PBL_DISPLAY_WIDTH > 160 ? 46 : 36;
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *cb) {
  if (idx->row >= g.wpt_count) return;
  Waypoint *w = &g.wpts[idx->row];
  char sub[32];

  if (g.fix.valid) {
    char d[12];
    fmt_dist(d, sizeof(d), geo_dist_nm(g.fix.lat_e6, g.fix.lon_e6, w->lat_e6, w->lon_e6));
    char c[12];
    fmt_course(c, sizeof(c), geo_brg_deg(g.fix.lat_e6, g.fix.lon_e6, w->lat_e6, w->lon_e6));
    snprintf(sub, sizeof(sub), "%s NM  %s%s", d, c,
             idx->row == g.wpt_active ? "  *" : "");
  } else {
    snprintf(sub, sizeof(sub), "%s", idx->row == g.wpt_active ? "active" : "");
  }
  menu_cell_basic_draw(ctx, (Layer *)cell, w->name, sub, NULL);
}

static void select_row(MenuLayer *m, MenuIndex *idx, void *ctx) {
  state_goto_wpt(idx->row);
  vibes_short_pulse();
  window_stack_pop(true);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_row,
  });
  menu_layer_set_click_config_onto_window(s_menu, window);
#if PBL_COLOR
  menu_layer_set_normal_colors(s_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu, GColorYellow, GColorBlack);
#endif
  layer_add_child(root, menu_layer_get_layer(s_menu));

  if (g.wpt_active < g.wpt_count) {
    menu_layer_set_selected_index(s_menu, MenuIndex(0, g.wpt_active),
                                  MenuRowAlignCenter, false);
  }
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void wpt_menu_show(void) {
  if (g.wpt_count == 0) return;
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
