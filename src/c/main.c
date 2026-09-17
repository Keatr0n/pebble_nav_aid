#include "nav.h"

static Window *s_window;
static Layer *s_canvas;

static void canvas_update(Layer *layer, GContext *ctx) {
  pages_render(ctx, layer_get_bounds(layer));
}

static void up_click(ClickRecognizerRef r, void *ctx) { pages_prev(); }
static void down_click(ClickRecognizerRef r, void *ctx) { pages_next(); }
// Some firmware delivers the single-click on release even after the long-click
// handler has already run, which makes one hold perform both actions. The
// window between them is tiny, so a short press arriving right on the heels of
// a hold is discarded. Time-based rather than a flag, so it cannot get stuck
// and swallow a later genuine press.
static time_t s_long_s;
static uint16_t s_long_ms;

static uint32_t ms_since_long(void) {
  time_t now_s;
  uint16_t now_ms;
  time_ms(&now_s, &now_ms);
  if (s_long_s == 0) return UINT32_MAX;
  int32_t delta = (int32_t)(now_s - s_long_s) * 1000 + (int32_t)now_ms - (int32_t)s_long_ms;
  return delta < 0 ? 0 : (uint32_t)delta;
}

static void select_click(ClickRecognizerRef r, void *ctx) {
  if (ms_since_long() < 600) return;
  pages_select();
}

static void select_long(ClickRecognizerRef r, void *ctx) {
  time_ms(&s_long_s, &s_long_ms);
  pages_select_long();
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  // 700 ms rather than the 500 ms default: at 500 a deliberate hold is easy to
  // undershoot, and the press then reads as a short click and fires the wrong
  // action. Gloves and turbulence both argue for the longer window.
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, select_long, NULL);
}

// Everything on screen is time-derived -- elapsed, ETE, ETA, observation age --
// so the whole canvas is simply redrawn once a second.
static void tick(struct tm *tick_time, TimeUnits units) {
  layer_mark_dirty(s_canvas);
}

// The first announcement waits for the phone-side JS to finish starting;
// sending into a link that is not up yet just gets dropped.
static void announce_startup_page(void *data) {
  pages_announce();
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
  pages_set_layer(s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  ui_init_colors();
  state_init();
  comm_init();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(SECOND_UNIT, tick);
  compass_apply_mode();
  app_timer_register(2500, announce_startup_page, NULL);
  // No battery subscription: the info page peeks at the charge state when it
  // draws, and subscribing with a null handler makes the firmware call it.
}

static void deinit(void) {
  state_save();
  tick_timer_service_unsubscribe();
  compass_stop();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
