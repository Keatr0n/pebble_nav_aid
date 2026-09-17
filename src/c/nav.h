#pragma once
#include <pebble.h>

#define MAX_WAYPOINTS 24
#define MAX_TRAFFIC 6

#define WPT_NAME_LEN 10
#define TFC_CALL_LEN 10
#define TFC_TYPE_LEN 6
#define WX_STATION_LEN 8
#define WX_RAW_LEN 140

#define NM_PER_DEG_LAT 60.0f

// A value we can safely treat as "the phone never told us".
#define UNKNOWN_I32 INT32_MIN
#define UNKNOWN_I16 INT16_MIN

typedef struct {
  char name[WPT_NAME_LEN];
  int32_t lat_e6;
  int32_t lon_e6;
} Waypoint;

typedef struct {
  char call[TFC_CALL_LEN];
  char type[TFC_TYPE_LEN];
  int16_t dist_x10;  // nautical miles * 10, from us
  int16_t brg;       // degrees true, from us toward the target
  int32_t alt_ft;    // barometric altitude, UNKNOWN_I32 if not reported
  int16_t vs_fpm;
  int16_t gs_kt;
} Traffic;

// How the traffic radar is rotated. Track-up is the default because a watch
// magnetometer inside a metal airframe, strapped to a wrist that moves
// independently of the aeroplane, is far less trustworthy than GPS track.
typedef enum {
  ORIENT_TRACK = 0,
  ORIENT_HEADING,
  ORIENT_NORTH
} OrientMode;

typedef enum {
  GPS_UNKNOWN = 0,
  GPS_SEARCHING,
  GPS_OK,
  GPS_DENIED,
  GPS_ERROR
} GpsState;

typedef struct {
  bool valid;
  int32_t lat_e6;
  int32_t lon_e6;
  int32_t alt_ft;
  int16_t gs_x10;   // knots * 10
  int16_t trk_x10;  // degrees true * 10
  int16_t acc_m;
  time_t ts;
} Fix;

// A run/stop timer that also banks distance flown while it was running.
// `started` is an absolute timestamp so elapsed survives the app being closed.
typedef struct {
  bool running;
  time_t started;
  uint32_t accum_s;
  uint32_t accum_nm_x100;
} Chrono;

typedef enum {
  FUEL_USGAL = 0,
  FUEL_LITRE,
  FUEL_IMPGAL,
  FUEL_KG,
  FUEL_LB
} FuelUnit;

typedef struct {
  uint16_t fuel_cap_x10;      // usable fuel on board at full, unit below
  uint16_t fuel_burn_x10;     // consumption per hour
  uint16_t fuel_taxi_x10;     // deducted up front for start/taxi/runup
  uint16_t fuel_reserve_min;  // minutes of fuel that must remain
  uint8_t fuel_unit;

  bool traffic_enable;
  uint8_t traffic_radius_nm;
  uint16_t traffic_alt_filter;  // ignore targets beyond this vertical split

  bool alert_traffic;
  bool alert_fuel;
  bool alert_wpt;

  bool auto_seq;
  bool traffic_radar;      // radar plan view rather than the text list
  uint8_t traffic_orient;  // OrientMode
  bool magnetic;      // present courses in magnetic rather than true
  uint8_t altim_unit; // 0 = hPa, 1 = inHg
  uint16_t pattern_alt;
  uint8_t pages_mask;
} Config;

typedef struct {
  bool valid;
  char station[WX_STATION_LEN];
  char raw[WX_RAW_LEN];
  int16_t wdir;      // degrees true, UNKNOWN_I16 if variable/calm
  int16_t wspd;      // knots
  int16_t gust;      // knots, 0 if none
  int16_t vis_x10;   // statute miles * 10
  int16_t temp_c;
  int16_t dewp_c;
  int16_t altim_hpa_x10;
  int16_t ceil_ft;   // lowest broken/overcast base, -1 if none
  int32_t elev_ft;   // station elevation, for density altitude
  uint8_t cat;       // 0 VFR, 1 MVFR, 2 IFR, 3 LIFR, 255 unknown
  time_t obs_ts;
} Metar;

typedef struct {
  Fix fix;
  // When the phone last told us anything, by the watch's own clock. Measuring
  // staleness against the fix's own timestamp would fold in any clock skew
  // between the two devices; what matters is how long we have been in the dark.
  time_t fix_rx;
  GpsState gps;

  int16_t decl_x10;  // magnetic variation, degrees east * 10
  bool decl_valid;

  int16_t compass_deg;     // clockwise from magnetic north
  int8_t compass_status;   // CompassStatus, or -1 when never reported

  Waypoint wpts[MAX_WAYPOINTS];
  uint8_t wpt_count;
  uint8_t wpt_active;

  Traffic tfc[MAX_TRAFFIC];
  uint8_t tfc_count;
  time_t tfc_ts;

  Chrono stopwatch;
  Chrono hobbs;

  Metar wx;
  time_t sunrise;
  time_t sunset;

  Config cfg;

  // Groundspeed jitters fix to fix; ETE computed straight off it is unreadable.
  float gs_smooth_kt;
  // Previous range to the active waypoint, for detecting station passage.
  float prev_dist_nm;
  uint8_t receding_count;

  char status[40];
} AppState;

extern AppState g;

// geo.c
float geo_dist_nm(int32_t lat1_e6, int32_t lon1_e6, int32_t lat2_e6, int32_t lon2_e6);
float geo_brg_deg(int32_t lat1_e6, int32_t lon1_e6, int32_t lat2_e6, int32_t lon2_e6);
float geo_norm360(float deg);
float geo_rel_bearing(float brg, float trk);  // -180..180, right positive
int32_t geo_to_magnetic(float true_deg);
float geo_density_alt_ft(int32_t elev_ft, int16_t temp_c, int16_t altim_hpa_x10);

// fmt.c
void fmt_dist(char *buf, size_t n, float nm);
void fmt_hms(char *buf, size_t n, uint32_t secs);
void fmt_hm(char *buf, size_t n, uint32_t secs);
void fmt_clock(char *buf, size_t n, time_t when);
void fmt_course(char *buf, size_t n, float true_deg);
void fmt_fuel(char *buf, size_t n, float qty);
const char *fmt_fuel_unit(void);

// state.c
void state_init(void);
void state_save(void);
void state_on_fix(void);
void state_next_wpt(void);
void state_prev_wpt(void);
void state_goto_wpt(uint8_t idx);
void chrono_toggle(Chrono *c);
void chrono_reset(Chrono *c);
uint32_t chrono_elapsed_s(const Chrono *c);
float chrono_dist_nm(const Chrono *c);
float fuel_used(void);
float fuel_remaining(void);
uint32_t fuel_endurance_s(void);
// Seconds since the phone last sent a position, by the watch's clock.
uint32_t fix_age_s(void);
// True when the numbers on screen are being computed from an old fix.
bool fix_is_stale(void);
bool nav_active_wpt(Waypoint *out);
float nav_dist_nm(void);
float nav_brg_deg(void);
uint32_t nav_ete_s(float dist_nm);
float nav_dist_to_dest_nm(void);

// ui.c
extern GColor ui_bg, ui_fg, ui_dim, ui_accent, ui_warn, ui_good;
void ui_init_colors(void);
void ui_fill_bg(GContext *ctx, GRect b);
int16_t ui_header(GContext *ctx, GRect b, const char *title, int page, int page_count);
void ui_text(GContext *ctx, GRect r, const char *font, const char *s, GTextAlignment al, GColor c);
int16_t ui_text_h(GContext *ctx, GRect r, const char *font, const char *s, GTextAlignment al, GColor c);
void ui_label_value(GContext *ctx, GRect r, const char *label, const char *value, GColor vc);
void ui_big_value(GContext *ctx, GRect r, const char *value, const char *suffix, GColor c);
void ui_hline(GContext *ctx, GRect b, int16_t y, GColor c);
void ui_cdi(GContext *ctx, GRect r, float rel_brg, GColor c);
void ui_arrow(GContext *ctx, GPoint centre, int16_t radius, float deg, GColor c);

// compass.c
void compass_start(void);
void compass_stop(void);
void compass_apply_mode(void);
bool compass_usable(void);
// Reference direction the radar is rotated to, in degrees TRUE.
float compass_reference_deg(void);
const char *compass_mode_label(void);

// comm.c
void comm_init(void);
void comm_request_traffic(void);
void comm_request_route(void);
void comm_request_wx(void);
// Tells the phone which page is up, so it can poll traffic harder when the
// radar is actually being looked at.
void comm_send_page(uint8_t page);

// pages.c
typedef struct {
  const char *title;
  void (*render)(GContext *ctx, GRect body);
  void (*select)(void);
  void (*select_long)(void);
} Page;

void pages_render(GContext *ctx, GRect bounds);
void pages_next(void);
void pages_prev(void);
void pages_select(void);
void pages_select_long(void);
void pages_mark_dirty(void);
void pages_set_layer(Layer *layer);
void pages_announce(void);
int pages_current(void);

void page_nav_render(GContext *ctx, GRect b);
void page_nav_select(void);
void page_nav_select_long(void);
void page_timer_render(GContext *ctx, GRect b);
void page_timer_select(void);
void page_timer_select_long(void);
void page_fuel_render(GContext *ctx, GRect b);
void page_fuel_select(void);
void page_fuel_select_long(void);
void page_traffic_render(GContext *ctx, GRect b);
void page_traffic_select(void);
void page_traffic_select_long(void);
void page_wx_render(GContext *ctx, GRect b);
void page_wx_select(void);
void page_wx_select_long(void);
void page_info_render(GContext *ctx, GRect b);
void page_info_select(void);
void page_info_select_long(void);

// wpt_menu.c
void wpt_menu_show(void);
