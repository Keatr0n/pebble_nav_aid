#include "nav.h"

static bool s_pos_updated;
static bool s_tfc_updated;
static time_t s_last_tfc_alert;

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
    g.wpt_count = n;
    // Relaunching mid-flight must not throw away which leg you are on, so the
    // active waypoint survives an identical route being re-sent. A route the
    // pilot actually edited is a different matter: leg 3 of the old route is
    // meaningless in the new one, so the phone flags that and we start over.
    Tuple *reset = dict_find(iter, MESSAGE_KEY_WptReset);
    if ((reset && reset->value->uint8) || g.wpt_active >= n) g.wpt_active = 0;
    g.prev_dist_nm = -1.0f;
    g.receding_count = 0;
  }
  Tuple *widx = dict_find(iter, MESSAGE_KEY_WptIdx);
  if (widx && widx->value->uint8 < MAX_WAYPOINTS) {
    uint8_t i = widx->value->uint8;
    Waypoint w;
    memset(&w, 0, sizeof(w));
    GET_STR(MESSAGE_KEY_WptName, w.name, WPT_NAME_LEN);
    GET_I32(MESSAGE_KEY_WptLat, w.lat_e6);
    GET_I32(MESSAGE_KEY_WptLon, w.lon_e6);
    g.wpts[i] = w;
    if (i >= g.wpt_count) g.wpt_count = i + 1;
    state_save();
  }

  Tuple *ttot = dict_find(iter, MESSAGE_KEY_TfcTotal);
  if (ttot) {
    uint8_t n = ttot->value->uint8;
    if (n > MAX_TRAFFIC) n = MAX_TRAFFIC;
    g.tfc_count = n;
    g.tfc_ts = time(NULL);
    s_tfc_updated = true;
  }
  Tuple *tidx = dict_find(iter, MESSAGE_KEY_TfcIdx);
  if (tidx && tidx->value->uint8 < MAX_TRAFFIC) {
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
    g.tfc[i] = t;
    if (i >= g.tfc_count) g.tfc_count = i + 1;
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
