#include "nav.h"

static bool s_pos_updated;
static bool s_tfc_updated;
static time_t s_last_tfc_alert;

// Traffic and the route both arrive as a count followed by one message per
// entry, and AppMessage delivers those one at a time. Committing each as it
// lands would leave the display showing a new count against the previous
// set's contents, and it ran the proximity alert over the previous poll's
// targets -- always a cycle behind, which on the 60 s background cadence is
// a long time to be told late about something at three miles. It also meant
// a message lost in a stall left half of one route spliced onto half of
// another with nothing to say so. Both sets are staged and swapped in whole,
// or not at all.
static Traffic s_tfc_stage[MAX_TRAFFIC];
static uint8_t s_tfc_expect, s_tfc_have;
static Waypoint s_wpt_stage[MAX_WAYPOINTS];
static uint8_t s_wpt_expect, s_wpt_have;
static bool s_wpt_reset;

// Staging turns a lost route message from silent corruption into silent
// staleness, which is better but still silent: the pilot would be looking at
// the previous route while the phone believed it had sent the new one. So an
// incomplete set is given a few seconds to finish and then asked for again.
// Capped, because a link that is properly down should not be nagged forever.
#define WPT_STAGE_TIMEOUT_MS 8000
#define WPT_STAGE_MAX_RETRY  2
static AppTimer *s_wpt_timer;
static uint8_t s_wpt_retry;

static void wpt_stage_timeout(void *data) {
  s_wpt_timer = NULL;
  if (s_wpt_expect == 0) return;  // completed after all
  s_wpt_expect = 0;
  s_wpt_have = 0;
  if (s_wpt_retry < WPT_STAGE_MAX_RETRY) {
    s_wpt_retry++;
    APP_LOG(APP_LOG_LEVEL_WARNING, "route incomplete, asking again");
    comm_request_route();
  } else {
    snprintf(g.status, sizeof(g.status), "Route incomplete - check link");
    pages_mark_dirty();
  }
}

static void wpt_stage_arm(void) {
  if (s_wpt_timer) {
    app_timer_reschedule(s_wpt_timer, WPT_STAGE_TIMEOUT_MS);
  } else {
    s_wpt_timer = app_timer_register(WPT_STAGE_TIMEOUT_MS, wpt_stage_timeout, NULL);
  }
}

static void wpt_stage_done(void) {
  if (s_wpt_timer) {
    app_timer_cancel(s_wpt_timer);
    s_wpt_timer = NULL;
  }
  s_wpt_expect = 0;
  s_wpt_retry = 0;
}

static void copy_str(char *dst, size_t n, const char *src) {
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

static void send_request(uint32_t key) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, key, 1);
  app_message_outbox_send();
}

void comm_request_traffic(void) { send_request(MESSAGE_KEY_ReqTraffic); }

void comm_send_page(uint8_t page) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_PageActive, page);
  app_message_outbox_send();
}
void comm_request_route(void)   { send_request(MESSAGE_KEY_ReqRoute); }
void comm_request_wx(void)      { send_request(MESSAGE_KEY_ReqWx); }

// A target close enough and level enough to matter. Deliberately conservative
// -- this is situational awareness, not a certified TCAS, and a buzzer that
// cries wolf gets ignored exactly when it shouldn't be.
static void maybe_alert_traffic(void) {
  if (!g.cfg.alert_traffic || g.tfc_count == 0) return;
  time_t now = time(NULL);
  if (now - s_last_tfc_alert < 60) return;

  for (uint8_t i = 0; i < g.tfc_count; i++) {
    Traffic *t = &g.tfc[i];
    if (t->dist_x10 > 30) continue;  // beyond 3.0 NM
    if (t->alt_ft != UNKNOWN_I32 && g.fix.valid) {
      int32_t split = t->alt_ft - g.fix.alt_ft;
      if (split < 0) split = -split;
      if (split > 1200) continue;
    }
    s_last_tfc_alert = now;
    vibes_double_pulse();
    return;
  }
}

// Message keys are resolved at runtime by the SDK, so they cannot be switch
// labels. Pulling each field by name also means a partial message updates only
// what it carries and leaves the rest of the state alone.
#define GET_I32(key, dst)  do { Tuple *_t = dict_find(iter, key); if (_t) (dst) = _t->value->int32; } while (0)
#define GET_I16(key, dst)  do { Tuple *_t = dict_find(iter, key); if (_t) (dst) = (int16_t)_t->value->int32; } while (0)
#define GET_U16(key, dst)  do { Tuple *_t = dict_find(iter, key); if (_t) (dst) = (uint16_t)_t->value->int32; } while (0)
#define GET_U8(key, dst)   do { Tuple *_t = dict_find(iter, key); if (_t) (dst) = _t->value->uint8; } while (0)
#define GET_BOOL(key, dst) do { Tuple *_t = dict_find(iter, key); if (_t) (dst) = _t->value->uint8 != 0; } while (0)
#define GET_STR(key, dst, n) do { Tuple *_t = dict_find(iter, key); if (_t) copy_str(dst, n, _t->value->cstring); } while (0)

static void inbox(DictionaryIterator *iter, void *context) {
  GET_U8(MESSAGE_KEY_GpsState, g.gps);
  GET_STR(MESSAGE_KEY_StatusMsg, g.status, sizeof(g.status));

  Tuple *lat = dict_find(iter, MESSAGE_KEY_PosLat);
  if (lat) {
    g.fix.lat_e6 = lat->value->int32;
    g.fix.valid = true;
    s_pos_updated = true;
    GET_I32(MESSAGE_KEY_PosLon, g.fix.lon_e6);
    GET_I32(MESSAGE_KEY_PosAlt, g.fix.alt_ft);
    GET_I16(MESSAGE_KEY_PosSpd, g.fix.gs_x10);
    GET_I16(MESSAGE_KEY_PosTrk, g.fix.trk_x10);
    GET_I16(MESSAGE_KEY_PosAcc, g.fix.acc_m);
    Tuple *ts = dict_find(iter, MESSAGE_KEY_PosTs);
    g.fix.ts = ts ? (time_t)ts->value->int32 : time(NULL);
    g.fix_rx = time(NULL);
  }
  Tuple *decl = dict_find(iter, MESSAGE_KEY_PosDecl);
  if (decl) {
    g.decl_x10 = (int16_t)decl->value->int32;
    g.decl_valid = true;
  }

  Tuple *wtot = dict_find(iter, MESSAGE_KEY_WptTotal);
  if (wtot) {
    uint8_t n = wtot->value->uint8;
    if (n > MAX_WAYPOINTS) n = MAX_WAYPOINTS;
    // Relaunching mid-flight must not throw away which leg you are on, so the
    // active waypoint survives an identical route being re-sent. A route the
    // pilot actually edited is a different matter: leg 3 of the old route is
    // meaningless in the new one, so the phone flags that and we start over.
    Tuple *reset = dict_find(iter, MESSAGE_KEY_WptReset);
    s_wpt_reset = reset && reset->value->uint8;
    s_wpt_expect = n;
    s_wpt_have = 0;
    if (n == 0) {
      g.wpt_count = 0;
      g.wpt_active = 0;
      g.prev_dist_nm = -1.0f;
      g.receding_count = 0;
      wpt_stage_done();
      state_save();
    } else {
      wpt_stage_arm();
    }
  }
  Tuple *widx = dict_find(iter, MESSAGE_KEY_WptIdx);
  if (widx && s_wpt_expect > 0 && widx->value->uint8 < MAX_WAYPOINTS) {
    uint8_t i = widx->value->uint8;
    Waypoint w;
    memset(&w, 0, sizeof(w));
    GET_STR(MESSAGE_KEY_WptName, w.name, WPT_NAME_LEN);
    GET_I32(MESSAGE_KEY_WptLat, w.lat_e6);
    GET_I32(MESSAGE_KEY_WptLon, w.lon_e6);
    s_wpt_stage[i] = w;
    if (i + 1 > s_wpt_have) s_wpt_have = i + 1;
    wpt_stage_arm();

    if (s_wpt_have >= s_wpt_expect) {
      memcpy(g.wpts, s_wpt_stage, sizeof(Waypoint) * s_wpt_expect);
      g.wpt_count = s_wpt_expect;
      if (s_wpt_reset || g.wpt_active >= g.wpt_count) g.wpt_active = 0;
      g.prev_dist_nm = -1.0f;
      g.receding_count = 0;
      wpt_stage_done();
      // One write per route rather than one per waypoint: the old code put the
      // whole of persistent storage down twenty-four times to load one route.
      state_save();
    }
  }

  Tuple *ttot = dict_find(iter, MESSAGE_KEY_TfcTotal);
  if (ttot) {
    uint8_t n = ttot->value->uint8;
    if (n > MAX_TRAFFIC) n = MAX_TRAFFIC;
    s_tfc_expect = n;
    s_tfc_have = 0;
    // An empty poll is complete the moment it is announced.
    if (n == 0) {
      g.tfc_count = 0;
      g.tfc_ts = time(NULL);
      s_tfc_expect = 0;
    }
  }
  Tuple *tidx = dict_find(iter, MESSAGE_KEY_TfcIdx);
  // A target with no count ahead of it means the count was lost in transit.
  // Dropping it keeps the last complete picture rather than building a new
  // one out of two polls.
  if (tidx && s_tfc_expect > 0 && tidx->value->uint8 < MAX_TRAFFIC) {
    uint8_t i = tidx->value->uint8;
    Traffic t;
    memset(&t, 0, sizeof(t));
    t.alt_ft = UNKNOWN_I32;
    GET_STR(MESSAGE_KEY_TfcCall, t.call, TFC_CALL_LEN);
    GET_STR(MESSAGE_KEY_TfcType, t.type, TFC_TYPE_LEN);
    GET_I16(MESSAGE_KEY_TfcDist, t.dist_x10);
    GET_I16(MESSAGE_KEY_TfcBrg, t.brg);
    GET_I32(MESSAGE_KEY_TfcAlt, t.alt_ft);
    GET_I16(MESSAGE_KEY_TfcVs, t.vs_fpm);
    GET_I16(MESSAGE_KEY_TfcGs, t.gs_kt);
    s_tfc_stage[i] = t;
    if (i + 1 > s_tfc_have) s_tfc_have = i + 1;

    if (s_tfc_have >= s_tfc_expect) {
      memcpy(g.tfc, s_tfc_stage, sizeof(Traffic) * s_tfc_expect);
      g.tfc_count = s_tfc_expect;
      g.tfc_ts = time(NULL);
      s_tfc_expect = 0;
      // Only now, with a complete and current set, is it worth asking whether
      // anything out there is close.
      s_tfc_updated = true;
    }
  }

  Tuple *station = dict_find(iter, MESSAGE_KEY_WxStation);
  if (station) {
    copy_str(g.wx.station, WX_STATION_LEN, station->value->cstring);
    g.wx.valid = true;
    g.wx.wdir = UNKNOWN_I16;
    g.wx.gust = 0;
    g.wx.ceil_ft = -1;
    g.wx.cat = 255;
    GET_STR(MESSAGE_KEY_WxRaw, g.wx.raw, WX_RAW_LEN);
    GET_I16(MESSAGE_KEY_WxWdir, g.wx.wdir);
    GET_I16(MESSAGE_KEY_WxWspd, g.wx.wspd);
    GET_I16(MESSAGE_KEY_WxGust, g.wx.gust);
    GET_I16(MESSAGE_KEY_WxVis, g.wx.vis_x10);
    GET_I16(MESSAGE_KEY_WxTemp, g.wx.temp_c);
    GET_I16(MESSAGE_KEY_WxDewp, g.wx.dewp_c);
    GET_I16(MESSAGE_KEY_WxAltim, g.wx.altim_hpa_x10);
    GET_U8(MESSAGE_KEY_WxCat, g.wx.cat);
    GET_I16(MESSAGE_KEY_WxCeil, g.wx.ceil_ft);
    GET_I32(MESSAGE_KEY_WxElev, g.wx.elev_ft);
    Tuple *age = dict_find(iter, MESSAGE_KEY_WxAge);
    g.wx.obs_ts = time(NULL) - (time_t)(age ? age->value->int32 : 0) * 60;
  }

  GET_I32(MESSAGE_KEY_SunRise, g.sunrise);
  GET_I32(MESSAGE_KEY_SunSet, g.sunset);

  bool cfg_changed = dict_find(iter, MESSAGE_KEY_CfgFuelCap) != NULL;
  GET_U16(MESSAGE_KEY_CfgFuelCap, g.cfg.fuel_cap_x10);
  GET_U16(MESSAGE_KEY_CfgFuelBurn, g.cfg.fuel_burn_x10);
  GET_U16(MESSAGE_KEY_CfgFuelReserve, g.cfg.fuel_reserve_min);
  GET_U16(MESSAGE_KEY_CfgFuelTaxi, g.cfg.fuel_taxi_x10);
  GET_U8(MESSAGE_KEY_CfgFuelUnit, g.cfg.fuel_unit);
  GET_BOOL(MESSAGE_KEY_CfgTfcEnable, g.cfg.traffic_enable);
  GET_U8(MESSAGE_KEY_CfgTfcRadius, g.cfg.traffic_radius_nm);
  GET_U16(MESSAGE_KEY_CfgTfcAltFilter, g.cfg.traffic_alt_filter);
  GET_BOOL(MESSAGE_KEY_CfgAlertTfc, g.cfg.alert_traffic);
  GET_BOOL(MESSAGE_KEY_CfgAlertFuel, g.cfg.alert_fuel);
  GET_BOOL(MESSAGE_KEY_CfgAlertWpt, g.cfg.alert_wpt);
  GET_BOOL(MESSAGE_KEY_CfgAutoSeq, g.cfg.auto_seq);
  GET_BOOL(MESSAGE_KEY_CfgMagnetic, g.cfg.magnetic);
  GET_U8(MESSAGE_KEY_CfgAltimUnit, g.cfg.altim_unit);
  GET_U16(MESSAGE_KEY_CfgPatternAlt, g.cfg.pattern_alt);
  GET_U8(MESSAGE_KEY_CfgPages, g.cfg.pages_mask);
  if (cfg_changed) state_save();

  if (s_pos_updated) {
    s_pos_updated = false;
    state_on_fix();
  }
  if (s_tfc_updated) {
    s_tfc_updated = false;
    maybe_alert_traffic();
  }
  pages_mark_dirty();
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", (int)reason);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", (int)reason);
}

void comm_init(void) {
  app_message_register_inbox_received(inbox);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  // The METAR raw string is the single biggest thing we ever receive.
  app_message_open(512, 128);
}
