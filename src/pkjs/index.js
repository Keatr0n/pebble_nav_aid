// Phone side of Nav Aid.
//
// The watch owns all the navigation arithmetic so that it keeps working
// between position reports. The phone's job is to feed it: GPS fixes,
// magnetic variation, a resolved route, nearby ADS-B traffic, the nearest
// METAR, and today's sunset.

var Clay = require("@rebble/clay");
var clayConfig = require("./config.json");
var customClay = require("./custom-clay.js");
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

var route = require("./route.js");
var traffic = require("./traffic.js");
var weather = require("./weather.js");
var magvar = require("./magvar.js");
var sun = require("./sun.js");

var MAX_WAYPOINTS = 24;
// Must match MAX_TRAFFIC in src/c/nav.h -- the watch has room for this many.
var MAX_TRAFFIC = 6;

var POSITION_MIN_INTERVAL_MS = 2000;

// Traffic is polled harder while the radar is actually on screen and eased off
// when it is not, which keeps the phone's radio and the free ADS-B feed out of
// trouble without ever letting the picture go properly stale. adsb.fi asks for
// no more than about one request a second; 15 s is four a minute.
var TRAFFIC_INTERVAL_VIEWING_MS = 15000;
var TRAFFIC_INTERVAL_BACKGROUND_MS = 60000;
var TRAFFIC_MAX_INTERVAL_MS = 240000;

var WX_INTERVAL_MS = 10 * 60 * 1000;
// First retry after a failed fetch; doubles up to WX_INTERVAL_MS.
var WX_RETRY_MS = 60 * 1000;
// METARs are reissued twice an hour at best, so time alone is a poor trigger.
// Moving this far means the nearest reporting station has probably changed.
var WX_REFRESH_NM = 20;

// How often the scheduler wakes up to decide whether anything is due.
var POLL_TICK_MS = 5000;

// Absolute page ids, matching the page table in src/c/pages.c.
var PAGE_TRAFFIC = 3;
// Variation changes slowly over the ground; recomputing it constantly is waste.
var DECL_REFRESH_NM = 15;

// Defaults must mirror config.json: before the settings page has ever been
// opened there is nothing in localStorage to read.
var settings = {
  routeText: "",
  fuelUnit: 1,
  fuelCap: 150,
  fuelBurn: 32,
  fuelTaxi: 5,
  fuelReserve: 45,
  tfcEnable: true,
  tfcRadius: 20,
  tfcAltFilter: 5000,
  magnetic: true,
  altimUnit: 0,
  autoSeq: true,
  patternAlt: 1000,
  pages: [true, true, true, true, true, true],
  alertTfc: true,
  alertFuel: true,
  alertWpt: true
};

var lastFix = null;
var lastPositionSentAt = 0;
var declCache = null;        // { lat, lon, decl }
var lastTrafficAt = 0;
// A multiplier rather than an absolute delay, so that changing page takes
// effect on the very next tick instead of waiting out a stale interval.
var trafficBackoff = 1;
var trafficInFlight = false;
var lastWxAt = 0;
var lastWxPos = null;
var wxInFlight = false;
// A failed METAR used to cost the full ten minutes before anything tried
// again, because the attempt stamped the clock on its way out and nothing
// rolled it back. Failures now come back quickly and only then ease off.
var wxFailures = 0;
var wxRetryAt = 0;
// QNH from the last METAR, used to carry ADS-B pressure altitudes to a height
// that can be compared with our own GPS altitude.
var lastQnhHpa = null;
var lastRouteSendAt = 0;
var activePage = -1;
var lastSunDay = null;
var watchId = null;
var routeSending = false;

// --------------------------------------------------------------- outbox

// Everything phone-to-watch goes through the outbox, which knows which
// messages may be dropped to make room (positions) and which have to be
// retried and confessed to if lost (route, config). See outbox.js.
var routeDirty = false;
var routeResends = 0;

var outbox = require("./outbox.js").create({
  send: function (dict, ok, fail) { Pebble.sendAppMessage(dict, ok, fail); },
  onLoss: function () { routeDirty = true; },
  onSent: function () { routeResends = 0; }
});

function enqueue(dict, opts) {
  outbox.enqueue(dict, opts);
}

// Positions and traffic can both be dropped: another position is along in two
// seconds, and a traffic set that loses a message is discarded by the watch
// and replaced by the next poll. Route, config and status cannot.
var DROPPABLE = { droppable: true };

function status(msg) {
  enqueue({ StatusMsg: String(msg).substring(0, 39) });
}

// ------------------------------------------------------------- settings

function num(v, fallback) {
  var n = parseFloat(v);
  return isNaN(n) ? fallback : n;
}

function loadSettings() {
  var data;
  try {
    data = localStorage.getItem("clay-settings");
  } catch (e) {
    return;
  }
  if (!data) return;

  try {
    var s = JSON.parse(data);
    if (typeof s.RouteText === "string") settings.routeText = s.RouteText;
    settings.fuelUnit = num(s.CfgFuelUnit, settings.fuelUnit);
    settings.fuelCap = num(s.CfgFuelCap, settings.fuelCap);
    settings.fuelBurn = num(s.CfgFuelBurn, settings.fuelBurn);
    settings.fuelTaxi = num(s.CfgFuelTaxi, settings.fuelTaxi);
    settings.fuelReserve = num(s.CfgFuelReserve, settings.fuelReserve);
    if (typeof s.CfgTfcEnable !== "undefined") settings.tfcEnable = !!s.CfgTfcEnable;
    settings.tfcRadius = num(s.CfgTfcRadius, settings.tfcRadius);
    settings.tfcAltFilter = num(s.CfgTfcAltFilter, settings.tfcAltFilter);
    if (typeof s.CfgMagnetic !== "undefined") settings.magnetic = !!s.CfgMagnetic;
    settings.altimUnit = num(s.CfgAltimUnit, settings.altimUnit);
    if (typeof s.CfgAutoSeq !== "undefined") settings.autoSeq = !!s.CfgAutoSeq;
    settings.patternAlt = num(s.CfgPatternAlt, settings.patternAlt);
    if (Array.isArray(s.CfgPagesList) && s.CfgPagesList.length === 6) {
      settings.pages = s.CfgPagesList;
    }
    if (typeof s.CfgAlertTfc !== "undefined") settings.alertTfc = !!s.CfgAlertTfc;
    if (typeof s.CfgAlertFuel !== "undefined") settings.alertFuel = !!s.CfgAlertFuel;
    if (typeof s.CfgAlertWpt !== "undefined") settings.alertWpt = !!s.CfgAlertWpt;
  } catch (e) {
    console.log("settings parse failed: " + e);
  }
}

function pagesMask() {
  var mask = 0;
  for (var i = 0; i < 6; i++) {
    if (settings.pages[i]) mask |= (1 << i);
  }
  // An empty selection would leave the watch with nothing to show.
  return mask === 0 ? 0x3f : mask;
}

// Quantities go across as tenths so the watch can stay in integers.
function sendConfig() {
  enqueue({
    CfgFuelCap: Math.round(settings.fuelCap * 10),
    CfgFuelBurn: Math.round(settings.fuelBurn * 10),
    CfgFuelTaxi: Math.round(settings.fuelTaxi * 10),
    CfgFuelReserve: Math.round(settings.fuelReserve),
    CfgFuelUnit: Math.round(settings.fuelUnit),
    CfgTfcEnable: settings.tfcEnable ? 1 : 0,
    CfgTfcRadius: Math.round(settings.tfcRadius),
    CfgTfcAltFilter: Math.round(settings.tfcAltFilter),
    CfgAlertTfc: settings.alertTfc ? 1 : 0,
    CfgAlertFuel: settings.alertFuel ? 1 : 0,
    CfgAlertWpt: settings.alertWpt ? 1 : 0,
    CfgAutoSeq: settings.autoSeq ? 1 : 0,
    CfgMagnetic: settings.magnetic ? 1 : 0,
    CfgAltimUnit: Math.round(settings.altimUnit),
    CfgPatternAlt: Math.round(settings.patternAlt),
    CfgPages: pagesMask()
  });
}

// ---------------------------------------------------------------- route

// True when the route text differs from the one we last delivered, which is
// how the watch knows to drop back to the first waypoint.
function routeChangedSinceLastSend() {
  try {
    var previous = localStorage.getItem("last-route-sent");
    return previous !== settings.routeText;
  } catch (e) {
    // Without storage we cannot tell, and resetting is the safe answer.
    return true;
  }
}

function rememberRouteSent() {
  try {
    localStorage.setItem("last-route-sent", settings.routeText);
  } catch (e) {
    // Only costs us the ability to preserve the leg across a relaunch.
  }
}

function sendRoute() {
  if (routeSending) return;
  routeDirty = false;
  lastRouteSendAt = Date.now();
  if (!settings.routeText || settings.routeText.trim().length === 0) {
    enqueue({ WptTotal: 0, WptReset: 1 });
    rememberRouteSent();
    return;
  }
  routeSending = true;
  var changed = routeChangedSinceLastSend();

  route.resolve(settings.routeText, MAX_WAYPOINTS, null,
                function (wpts, errors, dropped) {
    routeSending = false;
    rememberRouteSent();
    enqueue({ WptTotal: wpts.length, WptReset: changed ? 1 : 0 });
    for (var i = 0; i < wpts.length; i++) {
      enqueue({
        WptIdx: i,
        WptName: wpts[i].name,
        WptLat: Math.round(wpts[i].lat * 1e6),
        WptLon: Math.round(wpts[i].lon * 1e6)
      });
    }
    // Name the offender: "2 not found" leaves the pilot guessing which leg of
    // their route is missing. A route too long to fit gets said out loud for
    // the same reason -- the legs past the limit are simply not being flown.
    if (dropped > 0) {
      status(dropped + " past " + MAX_WAYPOINTS + " dropped");
    } else if (errors.length > 0) {
      var msg = "Not found: " + errors.slice(0, 2).join(", ");
      if (errors.length > 2) msg += " +" + (errors.length - 2);
      status(msg);
    } else {
      status("");
    }
  });
}

// ------------------------------------------------------------- position

function nmBetween(a, b) {
  var toRad = Math.PI / 180;
  var dLat = (b.lat - a.lat) * toRad;
  var dLon = (b.lon - a.lon) * toRad;
  var h = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
          Math.cos(a.lat * toRad) * Math.cos(b.lat * toRad) *
          Math.sin(dLon / 2) * Math.sin(dLon / 2);
  return 2 * Math.atan2(Math.sqrt(h), Math.sqrt(1 - h)) * 3440.065;
}

function declinationFor(lat, lon, altFt) {
  if (declCache && nmBetween(declCache, { lat: lat, lon: lon }) < DECL_REFRESH_NM) {
    return declCache.decl;
  }
  var d = magvar.declination(lat, lon, altFt, new Date());
  if (d === null) return declCache ? declCache.decl : null;
  declCache = { lat: lat, lon: lon, decl: d };
  return d;
}

function onPosition(pos) {
  var c = pos.coords;
  if (typeof c.latitude !== "number" || typeof c.longitude !== "number") return;

  lastFix = {
    lat: c.latitude,
    lon: c.longitude,
    altFt: typeof c.altitude === "number" ? Math.round(c.altitude * 3.28084) : 0,
    // Geolocation reports metres per second; aviation wants knots.
    gsKt: typeof c.speed === "number" && c.speed >= 0 ? c.speed * 1.943844 : 0,
    // Heading is null when stationary, because there is no course to report.
    trkDeg: typeof c.heading === "number" && !isNaN(c.heading) ? c.heading : 0,
    accM: typeof c.accuracy === "number" ? Math.round(c.accuracy) : 0,
    ts: Math.round((pos.timestamp || Date.now()) / 1000)
  };

  var now = Date.now();
  if (now - lastPositionSentAt >= POSITION_MIN_INTERVAL_MS) {
    lastPositionSentAt = now;
    var dict = {
      GpsState: 2,
      PosLat: Math.round(lastFix.lat * 1e6),
      PosLon: Math.round(lastFix.lon * 1e6),
      PosAlt: lastFix.altFt,
      PosSpd: Math.round(lastFix.gsKt * 10),
      PosTrk: Math.round(lastFix.trkDeg * 10),
      PosAcc: lastFix.accM,
      PosTs: lastFix.ts
    };
    var decl = declinationFor(lastFix.lat, lastFix.lon, lastFix.altFt);
    if (decl !== null) dict.PosDecl = Math.round(decl * 10);
    enqueue(dict, DROPPABLE);
  }

  maybeSendSun();
}

// Polling runs on its own clock rather than off the back of a position
// callback. Geolocation can go quiet -- no fix indoors, the phone backgrounding
// the app -- and when it did, traffic and weather used to stop updating with
// nothing on the watch to say so.
// A route that lost a message in a stalled link is incomplete on the watch
// and cannot repair itself; resending is cheap because every point it needs
// is already in the resolver's cache.
var ROUTE_RESEND_MIN_MS = 30000;
var ROUTE_RESEND_MAX_MS = 300000;

function maybeResendRoute() {
  if (!routeDirty || routeSending) return;
  // A link that is properly down would otherwise have the whole route thrown
  // at it every thirty seconds forever, so the gap widens while it keeps
  // failing and resets the moment one gets through.
  var gap = Math.min(ROUTE_RESEND_MIN_MS * Math.pow(2, routeResends),
                     ROUTE_RESEND_MAX_MS);
  if (Date.now() - lastRouteSendAt < gap) return;
  routeResends++;
  sendConfig();
  sendRoute();
}

function pollTick() {
  maybeSendSun();
  maybeResendRoute();
  maybePollTraffic();
  maybePollWx();
}

function onPositionError(err) {
  // 1 is PERMISSION_DENIED; anything else is a transient failure to acquire.
  enqueue({ GpsState: err && err.code === 1 ? 3 : 1 });
  console.log("geolocation error: " + (err && err.message));
}

function startGps() {
  if (watchId !== null) return;
  enqueue({ GpsState: 1 });
  try {
    watchId = navigator.geolocation.watchPosition(onPosition, onPositionError, {
      enableHighAccuracy: true,
      maximumAge: 1000,
      timeout: 30000
    });
  } catch (e) {
    enqueue({ GpsState: 4 });
  }
}

// ----------------------------------------------------------------- sun

function maybeSendSun() {
  if (!lastFix) return;
  var today = new Date().toDateString();
  if (lastSunDay === today) return;
  lastSunDay = today;

  var t = sun.times(new Date(), lastFix.lat, lastFix.lon);
  enqueue({
    SunRise: t.sunrise ? Math.round(t.sunrise.valueOf() / 1000) : 0,
    SunSet: t.sunset ? Math.round(t.sunset.valueOf() / 1000) : 0
  });
}

// ------------------------------------------------------------- traffic

function trafficIntervalMs() {
  var base = activePage === PAGE_TRAFFIC
      ? TRAFFIC_INTERVAL_VIEWING_MS
      : TRAFFIC_INTERVAL_BACKGROUND_MS;
  return Math.min(base * trafficBackoff, TRAFFIC_MAX_INTERVAL_MS);
}

function maybePollTraffic(force) {
  if (!settings.tfcEnable || !lastFix || trafficInFlight) return;
  var now = Date.now();
  if (!force && now - lastTrafficAt < trafficIntervalMs()) return;
  lastTrafficAt = now;
  trafficInFlight = true;

  traffic.fetch(lastFix.lat, lastFix.lon, {
    radiusNm: settings.tfcRadius,
    altFilterFt: settings.tfcAltFilter,
    myAltFt: lastFix.altFt,
    max: MAX_TRAFFIC,
    qnhHpa: lastQnhHpa
  }, function (err, list) {
    trafficInFlight = false;
    if (err) {
      // Most likely a rate limit or no signal; ease off and try again later.
      trafficBackoff = Math.min(trafficBackoff * 2, 16);
      return;
    }
    trafficBackoff = 1;

    enqueue({ TfcTotal: list.length }, DROPPABLE);
    for (var i = 0; i < list.length; i++) {
      var t = list[i];
      var d = {
        TfcIdx: i,
        TfcCall: t.call,
        TfcType: t.type,
        TfcDist: Math.round(t.distNm * 10),
        TfcBrg: t.brg,
        TfcVs: t.vsFpm,
        TfcGs: t.gsKt
      };
      // Leaving the key out entirely is how the watch learns the altitude is
      // unknown, rather than being told a wrong number.
      if (t.altFt !== null) d.TfcAlt = t.altFt;
      enqueue(d, DROPPABLE);
    }
  });
}

// ------------------------------------------------------------- weather

function maybePollWx(force) {
  if (!lastFix || wxInFlight) return;
  var now = Date.now();
  var movedFar = lastWxPos && nmBetween(lastWxPos, lastFix) >= WX_REFRESH_NM;
  var retryDue = wxRetryAt > 0 && now >= wxRetryAt;
  if (!force && !movedFar && !retryDue && now - lastWxAt < WX_INTERVAL_MS) return;
  lastWxAt = now;
  lastWxPos = { lat: lastFix.lat, lon: lastFix.lon };
  wxInFlight = true;

  weather.fetchNearest(lastFix.lat, lastFix.lon, function (err, m) {
    wxInFlight = false;
    if (err || !m) {
      // A minute, then two, then four, up to the ordinary interval. One
      // dropout should not cost the same as no weather at all.
      wxFailures++;
      wxRetryAt = Date.now() +
          Math.min(WX_RETRY_MS * Math.pow(2, wxFailures - 1), WX_INTERVAL_MS);
      return;
    }
    wxFailures = 0;
    wxRetryAt = 0;
    if (typeof m.altimHpaX10 === "number") lastQnhHpa = m.altimHpaX10 / 10;
    var d = {
      WxStation: m.station,
      WxRaw: m.raw,
      WxWspd: m.wspd,
      WxGust: m.gust,
      WxVis: m.visTenths,
      WxTemp: m.tempC,
      WxDewp: m.dewpC,
      WxAltim: m.altimHpaX10,
      WxCat: m.cat,
      WxCeil: m.ceilFt,
      WxElev: m.elevFt,
      WxAge: m.obsAgeMin
    };
    // Omitted when the wind is variable, so the watch can show VRB.
    if (m.wdir !== null) d.WxWdir = m.wdir;
    enqueue(d, DROPPABLE);
  });
}

// -------------------------------------------------------------- events

Pebble.addEventListener("ready", function () {
  loadSettings();
  sendConfig();
  sendRoute();
  startGps();
  setInterval(pollTick, POLL_TICK_MS);
});

Pebble.addEventListener("appmessage", function (e) {
  var p = e.payload || {};
  if (p.ReqRoute) {
    sendConfig();
    sendRoute();
  }
  if (p.ReqTraffic) maybePollTraffic(true);
  if (p.ReqWx) maybePollWx(true);

  if (typeof p.PageActive === "number") {
    var wasViewing = activePage === PAGE_TRAFFIC;
    activePage = p.PageActive;
    // Arriving on the radar should show a current picture, not whatever was
    // left over from the slower background cadence.
    if (!wasViewing && activePage === PAGE_TRAFFIC) {
      if (Date.now() - lastTrafficAt > 5000) maybePollTraffic(true);
    }
  }
});

Pebble.addEventListener("showConfiguration", function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener("webviewclosed", function (e) {
  if (!e || !e.response) return;
  try {
    clay.getSettings(e.response);
  } catch (ex) {
    console.log("failed to save settings: " + ex);
    return;
  }
  loadSettings();
  sendConfig();
  sendRoute();
  // New radius or altitude filter should show up immediately, not in 20s.
  maybePollTraffic(true);
});
