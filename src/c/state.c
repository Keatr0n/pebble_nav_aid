#include "nav.h"

AppState g;

#define PK_VERSION   1
#define PK_CONFIG    2
#define PK_WPTS_0    3
#define PK_WPTS_1    4
#define PK_WPT_META  5
#define PK_STOPWATCH 6
#define PK_HOBBS     7

#define STATE_VERSION 2
// persist_write_data caps at 256 bytes per key, so the route straddles two.
#define WPTS_PER_CHUNK 12

typedef struct {
  uint8_t count;
  uint8_t active;
} WptMeta;

static Fix s_prev_fix;
static bool s_fuel_warned;

static void defaults(void) {
  Config *c = &g.cfg;
  c->fuel_cap_x10 = 1500;   // 150 L, a typical light single
  c->fuel_burn_x10 = 320;   // 32 L/hr
  c->fuel_taxi_x10 = 50;
  c->fuel_reserve_min = 45;
  c->fuel_unit = FUEL_LITRE;
  c->traffic_enable = true;
  c->traffic_radius_nm = 20;
  c->traffic_alt_filter = 5000;
  c->alert_traffic = true;
  c->alert_fuel = true;
  c->alert_wpt = true;
  c->auto_seq = true;
  c->traffic_radar = true;
  c->traffic_orient = ORIENT_TRACK;
  c->magnetic = true;
  c->altim_unit = 0;
  c->pattern_alt = 1000;
  c->pages_mask = 0x3F;
}

void state_init(void) {
  memset(&g, 0, sizeof(g));
  defaults();
  g.wx.cat = 255;
  g.wx.ceil_ft = -1;
  g.prev_dist_nm = -1.0f;

  if (persist_exists(PK_VERSION) && persist_read_int(PK_VERSION) == STATE_VERSION) {
    if (persist_exists(PK_CONFIG)) {
      persist_read_data(PK_CONFIG, &g.cfg, sizeof(g.cfg));
    }
    WptMeta meta = { 0, 0 };
    if (persist_exists(PK_WPT_META)) {
      persist_read_data(PK_WPT_META, &meta, sizeof(meta));
      if (meta.count > MAX_WAYPOINTS) meta.count = MAX_WAYPOINTS;
      g.wpt_count = meta.count;
      g.wpt_active = meta.active < meta.count ? meta.active : 0;
      if (persist_exists(PK_WPTS_0)) {
        persist_read_data(PK_WPTS_0, &g.wpts[0], sizeof(Waypoint) * WPTS_PER_CHUNK);
      }
      if (persist_exists(PK_WPTS_1)) {
        persist_read_data(PK_WPTS_1, &g.wpts[WPTS_PER_CHUNK],
                          sizeof(Waypoint) * (MAX_WAYPOINTS - WPTS_PER_CHUNK));
      }
    }
    // Timers restore with their absolute start time, so a leg keeps counting
    // across an app restart -- which happens every time a notification steals
    // the screen mid-flight.
    if (persist_exists(PK_STOPWATCH)) {
      persist_read_data(PK_STOPWATCH, &g.stopwatch, sizeof(Chrono));
    }
    if (persist_exists(PK_HOBBS)) {
      persist_read_data(PK_HOBBS, &g.hobbs, sizeof(Chrono));
    }
  }
}

// Flash is not free and persist_write_data is not cheap. Anything the pilot
// can sit and press repeatedly -- cycling the radar orientation, say -- marks
// the state dirty instead and the write happens once the presses stop.
static AppTimer *s_save_timer;

static void flush_save(void *data) {
  s_save_timer = NULL;
  state_save();
}

void state_save_soon(void) {
  if (s_save_timer) {
    app_timer_reschedule(s_save_timer, 4000);
  } else {
    s_save_timer = app_timer_register(4000, flush_save, NULL);
  }
}

void state_save(void) {
  if (s_save_timer) {
    app_timer_cancel(s_save_timer);
    s_save_timer = NULL;
  }
  persist_write_int(PK_VERSION, STATE_VERSION);
  persist_write_data(PK_CONFIG, &g.cfg, sizeof(g.cfg));
  WptMeta meta = { g.wpt_count, g.wpt_active };
  persist_write_data(PK_WPT_META, &meta, sizeof(meta));
  persist_write_data(PK_WPTS_0, &g.wpts[0], sizeof(Waypoint) * WPTS_PER_CHUNK);
  persist_write_data(PK_WPTS_1, &g.wpts[WPTS_PER_CHUNK],
                     sizeof(Waypoint) * (MAX_WAYPOINTS - WPTS_PER_CHUNK));
  persist_write_data(PK_STOPWATCH, &g.stopwatch, sizeof(Chrono));
  persist_write_data(PK_HOBBS, &g.hobbs, sizeof(Chrono));
}

// ---------------------------------------------------------------- chronos

uint32_t chrono_elapsed_s(const Chrono *c) {
  uint32_t e = c->accum_s;
  if (c->running && c->started > 0) {
    time_t now = time(NULL);
    if (now > c->started) e += (uint32_t)(now - c->started);
  }
  return e;
}

float chrono_dist_nm(const Chrono *c) { return (float)c->accum_nm_x100 / 100.0f; }

void chrono_toggle(Chrono *c) {
  if (c->running) {
    c->accum_s = chrono_elapsed_s(c);
    c->running = false;
    c->started = 0;
  } else {
    c->started = time(NULL);
    c->running = true;
  }
  state_save();
}

void chrono_reset(Chrono *c) {
  c->running = false;
  c->started = 0;
  c->accum_s = 0;
  c->accum_nm_x100 = 0;
  state_save();
}

// ------------------------------------------------------------------ fuel

static float fuel_burn_per_hour(void) { return (float)g.cfg.fuel_burn_x10 / 10.0f; }

float fuel_used(void) {
  float hours = (float)chrono_elapsed_s(&g.hobbs) / 3600.0f;
  return (float)g.cfg.fuel_taxi_x10 / 10.0f + hours * fuel_burn_per_hour();
}

float fuel_remaining(void) {
  float r = (float)g.cfg.fuel_cap_x10 / 10.0f - fuel_used();
  return r < 0.0f ? 0.0f : r;
}

uint32_t fuel_endurance_s(void) {
  float burn = fuel_burn_per_hour();
  if (burn <= 0.0f) return 0;
  return (uint32_t)((fuel_remaining() / burn) * 3600.0f);
}

// -------------------------------------------------------------- waypoints

// Position arrives about every two seconds. Ten is long enough not to trip on
// a missed message or a moment of Bluetooth congestion, short enough that a
// pilot is told before they have acted on a frozen distance.
#define FIX_STALE_S 10

uint32_t fix_age_s(void) {
  if (!g.fix.valid || g.fix_rx == 0) return UINT32_MAX;
  time_t now = time(NULL);
  return now > g.fix_rx ? (uint32_t)(now - g.fix_rx) : 0;
}

bool fix_is_stale(void) {
  return g.fix.valid && fix_age_s() > FIX_STALE_S;
}

bool nav_active_wpt(Waypoint *out) {
  if (g.wpt_count == 0 || g.wpt_active >= g.wpt_count) return false;
  if (out) *out = g.wpts[g.wpt_active];
  return true;
}

float nav_dist_nm(void) {
  Waypoint w;
  if (!g.fix.valid || !nav_active_wpt(&w)) return -1.0f;
  return geo_dist_nm(g.fix.lat_e6, g.fix.lon_e6, w.lat_e6, w.lon_e6);
}

float nav_brg_deg(void) {
  Waypoint w;
  if (!g.fix.valid || !nav_active_wpt(&w)) return -1.0f;
  return geo_brg_deg(g.fix.lat_e6, g.fix.lon_e6, w.lat_e6, w.lon_e6);
}

// Remaining route distance: direct to the active waypoint, then leg by leg.
float nav_dist_to_dest_nm(void) {
  if (g.wpt_count == 0 || !g.fix.valid) return -1.0f;
  float total = nav_dist_nm();
  if (total < 0.0f) return -1.0f;
  for (uint8_t i = g.wpt_active; i + 1 < g.wpt_count; i++) {
    total += geo_dist_nm(g.wpts[i].lat_e6, g.wpts[i].lon_e6,
                         g.wpts[i + 1].lat_e6, g.wpts[i + 1].lon_e6);
  }
  return total;
}

// Below a walking pace the groundspeed is noise and any ETE from it is a lie.
uint32_t nav_ete_s(float dist_nm) {
  if (dist_nm < 0.0f || g.gs_smooth_kt < 5.0f) return 0;
  return (uint32_t)((dist_nm / g.gs_smooth_kt) * 3600.0f);
}

static void sequence_to(uint8_t idx) {
  g.wpt_active = idx;
  g.prev_dist_nm = -1.0f;
  g.receding_count = 0;
  state_save();
}

void state_next_wpt(void) {
  if (g.wpt_count == 0) return;
  if (g.wpt_active + 1 < g.wpt_count) sequence_to(g.wpt_active + 1);
}

void state_prev_wpt(void) {
  if (g.wpt_count == 0) return;
  if (g.wpt_active > 0) sequence_to(g.wpt_active - 1);
}

void state_goto_wpt(uint8_t idx) {
  if (idx < g.wpt_count) sequence_to(idx);
}

// ------------------------------------------------------------------- fix

// Called whenever the phone delivers a new position.
void state_on_fix(void) {
  if (!g.fix.valid) return;

  float gs = (float)g.fix.gs_x10 / 10.0f;
  if (gs < 0.0f) gs = 0.0f;
  // Exponential smoothing: enough to stop ETE flickering by whole minutes,
  // fast enough to follow a real acceleration through the circuit.
  if (g.gs_smooth_kt <= 0.0f) {
    g.gs_smooth_kt = gs;
  } else {
    g.gs_smooth_kt = g.gs_smooth_kt * 0.7f + gs * 0.3f;
  }

  if (s_prev_fix.valid) {
    float step = geo_dist_nm(s_prev_fix.lat_e6, s_prev_fix.lon_e6,
                             g.fix.lat_e6, g.fix.lon_e6);
    time_t dt = g.fix.ts - s_prev_fix.ts;
    // Reject GPS jumps: anything implying more than 600 kt is the phone
    // relocating itself, not the aeroplane moving.
    bool plausible = (dt > 0) && (dt < 120) &&
                     (step / ((float)dt / 3600.0f) < 600.0f) &&
                     (g.fix.acc_m <= 0 || g.fix.acc_m < 150);
    if (plausible) {
      uint32_t add = (uint32_t)(step * 100.0f + 0.5f);
      if (g.stopwatch.running) g.stopwatch.accum_nm_x100 += add;
      if (g.hobbs.running) g.hobbs.accum_nm_x100 += add;
    }
  }
  s_prev_fix = g.fix;

  // Engine/fuel clock starts itself once we are clearly rolling, so the fuel
  // page is honest even if the pilot forgets to press anything.
  if (!g.hobbs.running && g.hobbs.accum_s == 0 && gs > 30.0f) {
    g.hobbs.started = time(NULL);
    g.hobbs.running = true;
  }

  // Auto-sequencing. Two triggers: inside the capture ring, or station
  // passage -- range opening up while the waypoint sits behind the wingline.
  Waypoint w;
  if (g.cfg.auto_seq && nav_active_wpt(&w) && g.wpt_active + 1 < g.wpt_count) {
    float d = nav_dist_nm();
    bool advance = false;
    if (d >= 0.0f && d < 0.6f) {
      advance = true;
    } else if (d >= 0.0f && d < 4.0f && g.prev_dist_nm >= 0.0f && d > g.prev_dist_nm) {
      float rel = geo_rel_bearing(nav_brg_deg(), (float)g.fix.trk_x10 / 10.0f);
      if (rel > 100.0f || rel < -100.0f) {
        if (++g.receding_count >= 2) advance = true;
      } else {
        // Opening the range while the waypoint is still ahead is manoeuvring,
        // not passage. Without clearing the tally here two such moments minutes
        // apart would add up and sequence the leg early.
        g.receding_count = 0;
      }
    } else {
      g.receding_count = 0;
    }
    g.prev_dist_nm = d;
    if (advance) {
      state_next_wpt();
      if (g.cfg.alert_wpt) vibes_double_pulse();
    }
  }

  if (g.cfg.alert_fuel) {
    uint32_t reserve_s = (uint32_t)g.cfg.fuel_reserve_min * 60;
    if (!s_fuel_warned && g.hobbs.running && fuel_endurance_s() <= reserve_s) {
      s_fuel_warned = true;
      vibes_long_pulse();
    } else if (fuel_endurance_s() > reserve_s + 300) {
      s_fuel_warned = false;
    }
  }
}
