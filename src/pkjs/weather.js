// Nearest METAR from aviationweather.gov. Despite the .gov, the feed carries
// observations worldwide -- Australian stations included, reporting QNH in
// hectopascals exactly as they do on the ground.

var http = require("./http.js");

var ENDPOINT = "https://aviationweather.gov/api/data/metar";
// Widen the search until something reports. Roughly 30 NM, 60 NM, 150 NM.
var BOX_STEPS = [0.5, 1.0, 2.5];

function distNm(lat1, lon1, lat2, lon2) {
  var toRad = Math.PI / 180;
  var dLat = (lat2 - lat1) * toRad;
  var dLon = (lon2 - lon1) * toRad;
  var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
          Math.cos(lat1 * toRad) * Math.cos(lat2 * toRad) *
          Math.sin(dLon / 2) * Math.sin(dLon / 2);
  return 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a)) * 3440.065;
}

var CATEGORIES = { VFR: 0, MVFR: 1, IFR: 2, LIFR: 3 };

// Lowest broken or overcast layer -- the base that defines a ceiling.
function ceilingOf(m) {
  if (!m.clouds || !m.clouds.length) return -1;
  var best = -1;
  for (var i = 0; i < m.clouds.length; i++) {
    var c = m.clouds[i];
    if ((c.cover === "BKN" || c.cover === "OVC") && typeof c.base === "number") {
      if (best < 0 || c.base < best) best = c.base;
    }
  }
  return best;
}

// "10+" and "6+" appear alongside plain numbers; both mean "at least".
function visTenths(v) {
  if (typeof v === "number") return Math.round(v * 10);
  var n = parseFloat(String(v || "").replace("+", ""));
  return isNaN(n) ? 0 : Math.round(n * 10);
}

function normalise(m, lat, lon) {
  var wdir = typeof m.wdir === "number" ? m.wdir : null;  // null covers VRB
  return {
    station: String(m.icaoId || "").substring(0, 7),
    raw: String(m.rawOb || "").substring(0, 139),
    wdir: wdir,
    wspd: typeof m.wspd === "number" ? m.wspd : 0,
    gust: typeof m.wgst === "number" ? m.wgst : 0,
    visTenths: visTenths(m.visib),
    tempC: typeof m.temp === "number" ? Math.round(m.temp) : 0,
    dewpC: typeof m.dewp === "number" ? Math.round(m.dewp) : 0,
    altimHpaX10: typeof m.altim === "number" ? Math.round(m.altim * 10) : 10133,
    cat: CATEGORIES[m.fltCat] === undefined ? 255 : CATEGORIES[m.fltCat],
    ceilFt: ceilingOf(m),
    elevFt: typeof m.elev === "number" ? Math.round(m.elev * 3.28084) : 0,
    obsAgeMin: m.obsTime ? Math.max(0, Math.round((Date.now() / 1000 - m.obsTime) / 60)) : 0,
    distNm: (typeof m.lat === "number" && typeof m.lon === "number")
            ? distNm(lat, lon, m.lat, m.lon) : 9999
  };
}

function fetchNearest(lat, lon, cb) {
  var step = 0;

  function attempt() {
    if (step >= BOX_STEPS.length) { cb(new Error("no station"), null); return; }
    var d = BOX_STEPS[step++];
    // The API wants minLat,minLon,maxLat,maxLon. Longitude degrees shrink
    // toward the poles, so widen the box to keep it roughly square.
    var lonPad = d / Math.max(0.2, Math.cos(lat * Math.PI / 180));
    var bbox = [(lat - d).toFixed(3), (lon - lonPad).toFixed(3),
                (lat + d).toFixed(3), (lon + lonPad).toFixed(3)].join(",");
    var url = ENDPOINT + "?format=json&bbox=" + bbox;

    http.getJSON(url, 15000, function (err, data) {
      if (err || !data || !data.length) { attempt(); return; }

      var best = null;
      for (var i = 0; i < data.length; i++) {
        // Stations that report no pressure are usually partial observations.
        if (typeof data[i].altim !== "number") continue;
        var n = normalise(data[i], lat, lon);
        if (!best || n.distNm < best.distNm) best = n;
      }
      if (!best) { attempt(); return; }
      cb(null, best);
    });
  }

  attempt();
}

module.exports = { fetchNearest: fetchNearest };
