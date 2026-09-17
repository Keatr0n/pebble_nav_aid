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

function clean(s) {
  return String(s || "").replace(/[^A-Za-z0-9\-]/g, "").substring(0, 9);
}

// `opts`: { radiusNm, altFilterFt, myAltFt, max }
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

      var alt = typeof a.alt_baro === "number" ? a.alt_baro
              : (typeof a.alt_geom === "number" ? a.alt_geom : null);

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

module.exports = { fetch: fetch };
