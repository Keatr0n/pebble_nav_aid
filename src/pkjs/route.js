// Turning the route the pilot typed on their phone into waypoints the watch
// can navigate to.
//
// Five accepted line shapes, in the order they are tried:
//   YSBK                      ident lookup (ICAO, worldwide)
//   YSBK 135/12               12 NM on the 135 radial (magnetic) from YSBK
//   PARRA -33.815 151.001     your own point, decimal degrees
//   PARRA S33 48.9 E151 0.1   your own point, degrees and decimal minutes
//   Katoomba Scenic World     free-text place lookup
//
// Anything resolved over the network is cached, so a route re-sends instantly
// and still works next flight with no signal.

var http = require("./http.js");
var magvar = require("./magvar.js");

var CACHE_PREFIX = "wpt:";
var MAX_NAME = 9;  // the watch stores 10 bytes including the terminator

// ------------------------------------------------------------- coordinates

// "S33" / "-33.8" / "33.8S" -> { hemi: "S", val: 33.8 }
function splitHemi(tok) {
  var m = /^([NSEW])?(-?\d+(?:\.\d+)?)([NSEW])?$/i.exec(tok);
  if (!m) return null;
  return {
    hemi: (m[1] || m[3] || "").toUpperCase(),
    val: parseFloat(m[2])
  };
}

function applyHemi(part, isLat) {
  if (!part || isNaN(part.val)) return null;
  var v = part.val;
  if (part.hemi) {
    if (isLat && part.hemi !== "N" && part.hemi !== "S") return null;
    if (!isLat && part.hemi !== "E" && part.hemi !== "W") return null;
    v = Math.abs(v);
    if (part.hemi === "S" || part.hemi === "W") v = -v;
  }
  return v;
}

function inRange(lat, lon) {
  return lat !== null && lon !== null &&
         !isNaN(lat) && !isNaN(lon) &&
         lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

// Try to read a coordinate pair off the end of the token list. Returns
// { lat, lon, consumed } or null.
function parseTrailingCoords(tokens) {
  var n = tokens.length;

  // Degrees and decimal minutes: "S33 48.9 E151 0.1"
  if (n >= 4) {
    var latD = splitHemi(tokens[n - 4]);
    var latM = parseFloat(tokens[n - 3]);
    var lonD = splitHemi(tokens[n - 2]);
    var lonM = parseFloat(tokens[n - 1]);
    if (latD && lonD && !isNaN(latM) && !isNaN(lonM) &&
        latM >= 0 && latM < 60 && lonM >= 0 && lonM < 60 &&
        latD.hemi && lonD.hemi) {
      var lat = (Math.abs(latD.val) + latM / 60) * (latD.hemi === "S" ? -1 : 1);
      var lon = (Math.abs(lonD.val) + lonM / 60) * (lonD.hemi === "W" ? -1 : 1);
      if (inRange(lat, lon)) return { lat: lat, lon: lon, consumed: 4 };
    }
  }

  // Decimal degrees: "-33.815 151.001"
  if (n >= 2) {
    var dlat = applyHemi(splitHemi(tokens[n - 2]), true);
    var dlon = applyHemi(splitHemi(tokens[n - 1]), false);
    // Require a decimal point on at least one, so "YSBK 12" is not read as
    // a coordinate pair.
    var looksNumeric = /\./.test(tokens[n - 2]) || /\./.test(tokens[n - 1]);
    if (inRange(dlat, dlon) && looksNumeric) {
      return { lat: dlat, lon: dlon, consumed: 2 };
    }
  }

  return null;
}

// ------------------------------------------------------------- projection

var R_NM = 3440.065;

// Point at a bearing and distance from another point, along a great circle.
function project(lat, lon, bearingTrue, distNm) {
  var d = distNm / R_NM;
  var br = bearingTrue * Math.PI / 180;
  var p1 = lat * Math.PI / 180;
  var l1 = lon * Math.PI / 180;

  var p2 = Math.asin(Math.sin(p1) * Math.cos(d) + Math.cos(p1) * Math.sin(d) * Math.cos(br));
  var l2 = l1 + Math.atan2(Math.sin(br) * Math.sin(d) * Math.cos(p1),
                           Math.cos(d) - Math.sin(p1) * Math.sin(p2));
  var lonOut = ((l2 * 180 / Math.PI + 540) % 360) - 180;
  return { lat: p2 * 180 / Math.PI, lon: lonOut };
}

// ------------------------------------------------------------------ names

function makeName(raw) {
  var s = String(raw).toUpperCase().replace(/[^A-Z0-9]/g, "");
  if (s.length === 0) s = "WPT";
  return s.substring(0, MAX_NAME);
}

// ----------------------------------------------------------- cache + fetch

function cacheGet(key) {
  try {
    var v = localStorage.getItem(CACHE_PREFIX + key);
    return v ? JSON.parse(v) : null;
  } catch (e) {
    return null;
  }
}

function cacheSet(key, val) {
  try {
    localStorage.setItem(CACHE_PREFIX + key, JSON.stringify(val));
  } catch (e) {
    // A full or disabled store only costs us the offline shortcut.
  }
}

// Ident lookup runs through several free, keyless, worldwide sources. None of
// them alone is complete: airport-data.com misses some GA fields, Wikidata
// carries them but is slower, and aviationweather.gov is strong on
// international aerodromes. The first that answers wins.

function lookupAirportData(ident, cb) {
  var url = "https://airport-data.com/api/ap_info.json?icao=" + encodeURIComponent(ident);
  http.getJSON(url, 12000, function (err, data) {
    if (!err && data && data.status === 200) {
      var lat = parseFloat(data.latitude);
      var lon = parseFloat(data.longitude);
      if (inRange(lat, lon)) { cb(null, { lat: lat, lon: lon }); return; }
    }
    cb(err || new Error("not found"), null);
  });
}

// P239 is the ICAO code. Matching on the property rather than on text means a
// hit is the airport with that code, not something that merely reads like it.
function lookupWikidata(ident, cb) {
  var sparql =
    'SELECT ?lat ?lon WHERE { ?a wdt:P239 "' + ident +
    '"; p:P625/psv:P625 ?v . ?v wikibase:geoLatitude ?lat ; wikibase:geoLongitude ?lon } LIMIT 1';
  var url = "https://query.wikidata.org/sparql?format=json&query=" + encodeURIComponent(sparql);
  http.getJSON(url, 15000, function (err, data) {
    if (err) { cb(err, null); return; }
    var b = data && data.results && data.results.bindings;
    if (!b || !b.length) { cb(new Error("not found"), null); return; }
    var lat = parseFloat(b[0].lat.value);
    var lon = parseFloat(b[0].lon.value);
    if (!inRange(lat, lon)) { cb(new Error("bad coords"), null); return; }
    cb(null, { lat: lat, lon: lon });
  });
}

function lookupAviationWeather(ident, cb) {
  var url = "https://aviationweather.gov/api/data/airport?format=json&ids=" +
            encodeURIComponent(ident);
  http.getJSON(url, 12000, function (err, data) {
    if (err) { cb(err, null); return; }
    if (!data || !data.length) { cb(new Error("not found"), null); return; }
    var lat = parseFloat(data[0].lat);
    var lon = parseFloat(data[0].lon);
    if (!inRange(lat, lon)) { cb(new Error("bad coords"), null); return; }
    cb(null, { lat: lat, lon: lon });
  });
}

// Free-text geocoding, for waypoints the pilot describes rather than codes.
// Photon is an OSM geocoder with no key and no User-Agent requirement, which
// matters because PebbleKit JS cannot set one.
function lookupPlace(query, cb) {
  var url = "https://photon.komoot.io/api/?limit=1&q=" + encodeURIComponent(query);
  http.getJSON(url, 12000, function (err, data) {
    if (err) { cb(err, null); return; }
    if (!data || !data.features || data.features.length === 0) {
      cb(new Error("not found"), null);
      return;
    }
    var f = data.features[0];
    var c = f.geometry && f.geometry.coordinates;
    if (!c || c.length < 2) { cb(new Error("not found"), null); return; }
    var lon = parseFloat(c[0]);
    var lat = parseFloat(c[1]);
    if (!inRange(lat, lon)) { cb(new Error("bad coords"), null); return; }
    var label = (f.properties && (f.properties.name || f.properties.city)) || query;
    cb(null, { name: makeName(label), lat: lat, lon: lon });
  });
}

// Resolve a bare token.
//
// An ident is looked up ONLY against airport databases. It deliberately does
// not fall through to a place search: "YWOL" fuzzy-matched to Newcastle
// Airport, 150 NM from the field the pilot meant, and a nav aid that quietly
// points somewhere plausible but wrong is worse than one that says it does not
// know. An unresolved ident is reported instead, and the pilot can enter
// coordinates directly.
function resolveToken(token, cb) {
  var key = token.toUpperCase();
  var hit = cacheGet(key);
  if (hit) { cb(null, hit); return; }

  function finish(err, wpt) {
    if (wpt) cacheSet(key, wpt);
    cb(err, wpt);
  }

  if (/^[A-Z0-9]{3,5}$/i.test(token)) {
    var ident = key;
    var sources = [lookupAirportData, lookupWikidata, lookupAviationWeather];
    var i = 0;
    (function next() {
      if (i >= sources.length) { finish(new Error("ident not found"), null); return; }
      sources[i++](ident, function (err, hit2) {
        if (hit2) {
          finish(null, { name: makeName(ident), lat: hit2.lat, lon: hit2.lon });
        } else {
          next();
        }
      });
    })();
    return;
  }

  lookupPlace(token, finish);
}

// -------------------------------------------------------------- one line

function resolveLine(line, cb) {
  var raw = line.trim();
  if (raw.length === 0) { cb(null, null); return; }

  var tokens = raw.split(/[\s,]+/).filter(function (t) { return t.length > 0; });
  if (tokens.length === 0) { cb(null, null); return; }

  // Explicit coordinates need no network at all.
  var coords = parseTrailingCoords(tokens);
  if (coords) {
    var leading = tokens.slice(0, tokens.length - coords.consumed).join(" ");
    cb(null, {
      name: makeName(leading || "WPT"),
      lat: coords.lat,
      lon: coords.lon
    });
    return;
  }

  // Radial/distance offset: "YSBK 135/12".
  var rd = /^(\d{1,3})\/(\d+(?:\.\d+)?)$/.exec(tokens[tokens.length - 1]);
  if (rd && tokens.length >= 2) {
    var base = tokens.slice(0, tokens.length - 1).join(" ");
    var radialMag = parseFloat(rd[1]);
    var dist = parseFloat(rd[2]);
    if (radialMag > 360 || dist <= 0) { cb(new Error("bad radial"), null); return; }
    resolveToken(base, function (err, origin) {
      if (err || !origin) { cb(err || new Error("no origin"), null); return; }
      // Radials are quoted magnetic, so convert to true before projecting.
      var decl = magvar.declination(origin.lat, origin.lon, 0, new Date());
      var radialTrue = radialMag + (decl === null ? 0 : decl);
      var p = project(origin.lat, origin.lon, radialTrue, dist);
      cb(null, {
        name: makeName(origin.name.substring(0, 4) + rd[1]),
        lat: p.lat,
        lon: p.lon
      });
    });
    return;
  }

  resolveToken(raw, cb);
}

// ------------------------------------------------------------- whole route

// Resolves lines one at a time rather than in parallel: it keeps the order
// stable, and it is gentle on free geocoders that rate-limit by IP.
//
// Calls back with (waypoints, errors, dropped). `dropped` is how many lines
// were left unresolved because the route was already full: they used to be
// discarded in silence, which is the same sin as guessing at an ident -- the
// pilot ends up flying a route that is not the one they typed and has no way
// of knowing.
function resolve(text, maxWaypoints, onProgress, onDone) {
  var lines = String(text || "").split(/[\r\n;]+/);
  var out = [];
  var errors = [];
  var i = 0;

  function remainingLines() {
    var n = 0;
    for (var j = i; j < lines.length; j++) {
      if (lines[j].trim().length > 0) n++;
    }
    return n;
  }

  function step() {
    if (out.length >= maxWaypoints) { onDone(out, errors, remainingLines()); return; }
    if (i >= lines.length) { onDone(out, errors, 0); return; }
    var line = lines[i++];
    if (line.trim().length === 0) { step(); return; }
    resolveLine(line, function (err, wpt) {
      if (wpt) {
        out.push(wpt);
        if (onProgress) onProgress(out.length, wpt);
      } else if (err) {
        errors.push(line.trim());
      }
      step();
    });
  }

  step();
}

module.exports = {
  resolve: resolve,
  resolveLine: resolveLine,
  project: project,
  makeName: makeName
};
