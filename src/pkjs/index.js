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
var MAX_TRAFFIC = 5;

var POSITION_MIN_INTERVAL_MS = 2000;

// Traffic is polled harder while the radar is actually on screen and eased off
// when it is not, which keeps the phone's radio and the free ADS-B feed out of
// trouble without ever letting the picture go properly stale. adsb.fi asks for
// no more than about one request a second; 15 s is four a minute.
var TRAFFIC_INTERVAL_VIEWING_MS = 15000;
var TRAFFIC_INTERVAL_BACKGROUND_MS = 60000;
var TRAFFIC_MAX_INTERVAL_MS = 240000;

var WX_INTERVAL_MS = 10 * 60 * 1000;
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
var activePage = -1;
var lastSunDay = null;
var watchId = null;
var routeSending = false;

// --------------------------------------------------------------- outbox

// Only one AppMessage may be in flight at a time, so everything funnels
// through a queue. Old entries are dropped rather than the new ones: a stale
// position is worth less than the current one.
var queue = [];
var sending = false;
var QUEUE_LIMIT = 40;

function enqueue(dict) {
  if (queue.length >= QUEUE_LIMIT) queue.shift();
  queue.push(dict);
  pump();
}

function pump() {
  if (sending || queue.length === 0) return;
  sending = true;
  var dict = queue.shift();
  Pebble.sendAppMessage(dict,
    function () { sending = false; pump(); },
    function (e) {
      console.log("send failed: " + JSON.stringify(e));
      sending = false;
      pump();
    });
}

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
  if (!settings.routeText || settings.routeText.trim().length === 0) {
    enqueue({ WptTotal: 0, WptReset: 1 });
    rememberRouteSent();
    return;
  }
  routeSending = true;
  var changed = routeChangedSinceLastSend();

  route.resolve(settings.routeText, MAX_WAYPOINTS, null, function (wpts, errors) {
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
    // their route is missing.
    if (errors.length > 0) {
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
    enqueue(dict);
  }

  maybeSendSun();
}

// Polling runs on its own clock rather than off the back of a position
// callback. Geolocation can go quiet -- no fix indoors, the phone backgrounding
// the app -- and when it did, traffic and weather used to stop updating with
// nothing on the watch to say so.
function pollTick() {
  maybeSendSun();
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
    max: MAX_TRAFFIC
  }, function (err, list) {
    trafficInFlight = false;
    if (err) {
      // Most likely a rate limit or no signal; ease off and try again later.
      trafficBackoff = Math.min(trafficBackoff * 2, 16);
      return;
    }
    trafficBackoff = 1;

    enqueue({ TfcTotal: list.length });
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
      enqueue(d);
    }
  });
}

// ------------------------------------------------------------- weather

function maybePollWx(force) {
  if (!lastFix || wxInFlight) return;
  var now = Date.now();
  var movedFar = lastWxPos && nmBetween(lastWxPos, lastFix) >= WX_REFRESH_NM;
  if (!force && !movedFar && now - lastWxAt < WX_INTERVAL_MS) return;
  lastWxAt = now;
  lastWxPos = { lat: lastFix.lat, lon: lastFix.lon };
  wxInFlight = true;

  weather.fetchNearest(lastFix.lat, lastFix.lon, function (err, m) {
    wxInFlight = false;
    if (err || !m) return;
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
    enqueue(d);
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
