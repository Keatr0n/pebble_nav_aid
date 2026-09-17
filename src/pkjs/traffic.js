// Nearby aircraft from the adsb.fi community feed: free, keyless, worldwide.
//
// The API takes our position and hands back targets already tagged with `dst`
// (nautical miles) and `dir` (true bearing) relative to that point, so no
// geometry is needed here.
//
// This is awareness, never separation. Only ADS-B-equipped aircraft appear,
// coverage depends on volunteer receivers, and low-level targets are routinely
// missed entirely.

var http = require("./http.js");

var ENDPOINT = "https://opendata.adsb.fi/api/v2";

// Standard atmosphere, and the usual 27 ft per hectopascal near the surface.
var ISA_HPA = 1013.25;
var FT_PER_HPA = 27;

function clean(s) {
  return String(s || "").replace(/[^A-Za-z0-9\-]/g, "").substring(0, 9);
}

// The watch subtracts our own altitude from this to get the vertical split it
// shows and alerts on, and our own altitude comes from the phone's GPS -- a
// height above the WGS84 ellipsoid. So whatever we hand over has to be the
// same kind of number.
//
// `alt_geom` is exactly that, height above the same ellipsoid, and is used
// whenever the target reports it. `alt_baro` is not: it is pressure altitude
// against 1013.25, which at a QNH of 1000 sits some 360 ft above where the
// aircraft actually is. Subtracting that from a GPS altitude does not cancel
// the way TCAS's baro-minus-baro does -- it just adds the whole error to the
// split, on a display whose entire job is that number. So baro is carried to
// mean sea level with the QNH we already hold from the nearest METAR.
//
// What is left after that is the geoid separation between mean sea level and
// the ellipsoid, tens to a couple of hundred feet depending where you are,
// which needs a geoid model we do not carry. Hence the preference for
// `alt_geom`: when it is there, none of this applies.
function comparableAltFt(a, qnhHpa) {
  if (typeof a.alt_geom === "number") return a.alt_geom;
  if (typeof a.alt_baro !== "number") return null;
  var qnh = typeof qnhHpa === "number" && qnhHpa > 800 && qnhHpa < 1100
      ? qnhHpa : ISA_HPA;
  return Math.round(a.alt_baro + (qnh - ISA_HPA) * FT_PER_HPA);
}

// `opts`: { radiusNm, altFilterFt, myAltFt, max, qnhHpa }
function fetch(lat, lon, opts, cb) {
  var radius = Math.max(1, Math.min(250, opts.radiusNm || 20));
  var url = ENDPOINT + "/lat/" + lat.toFixed(4) + "/lon/" + lon.toFixed(4) +
            "/dist/" + radius;

  http.getJSON(url, 15000, function (err, data) {
    if (err) { cb(err, null); return; }
    if (!data || !data.aircraft) { cb(null, []); return; }

    var out = [];
    for (var i = 0; i < data.aircraft.length; i++) {
      var a = data.aircraft[i];

      // Aircraft on the ground clutter the list at exactly the moment the
      // list matters least.
      if (a.alt_baro === "ground") continue;
      if (typeof a.dst !== "number") continue;

      // A position older than a minute is not where the aeroplane is now.
      if (typeof a.seen_pos === "number" && a.seen_pos > 60) continue;

      var alt = comparableAltFt(a, opts.qnhHpa);

      if (alt !== null && typeof opts.myAltFt === "number" && opts.altFilterFt > 0) {
        if (Math.abs(alt - opts.myAltFt) > opts.altFilterFt) continue;
      }

      var call = clean(a.flight) || clean(a.r) || clean(a.hex);
      var vs = typeof a.geom_rate === "number" ? a.geom_rate
             : (typeof a.baro_rate === "number" ? a.baro_rate : 0);

      out.push({
        call: call,
        type: clean(a.t).substring(0, 5),
        distNm: a.dst,
        brg: typeof a.dir === "number" ? Math.round(a.dir) : 0,
        altFt: alt,
        vsFpm: Math.round(vs),
        gsKt: typeof a.gs === "number" ? Math.round(a.gs) : 0
      });
    }

    out.sort(function (x, y) { return x.distNm - y.distNm; });
    cb(null, out.slice(0, opts.max || 5));
  });
}

module.exports = { fetch: fetch, comparableAltFt: comparableAltFt };
