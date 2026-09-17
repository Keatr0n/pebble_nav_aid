// Magnetic variation from the World Magnetic Model.
//
// GPS reports true track, but pilots fly and are cleared in magnetic. The
// error is not cosmetic: it is ~12E around Sydney and Melbourne, ~1.5W at
// Perth, so a true-only display would be off by a full runway designator.
// The model is evaluated on the phone and only the resulting declination is
// sent to the watch, which keeps float-heavy spherical harmonics off a device
// that has no FPU.
//
// This is a direct implementation of the WMM synthesis described in the NOAA
// technical report. Only PcupLow is needed because n_max is 12; the high-order
// path in the reference library exists for models above degree 16.

var MODEL = require("./wmm2025.js");

// WGS84, plus the geomagnetic reference radius the model is defined against.
var WGS84_A = 6378.137;
var WGS84_B = 6356.7523142;
var GEOMAG_RE = 6371.2;
var EPS_SQ = 1 - (WGS84_B * WGS84_B) / (WGS84_A * WGS84_A);

var DEG = Math.PI / 180;

// Schmidt semi-normalised associated Legendre functions of sin(latitude), and
// their derivatives with respect to latitude, indexed by i = n(n+1)/2 + m.
function legendre(sinPhi, nMax) {
  var norm = [1.0];
  var p = [1.0];
  var dp = [0.0];
  var z = Math.sqrt((1 - sinPhi) * (1 + sinPhi));
  var n, m, i, i1, i2, k;

  for (n = 1; n <= nMax; n++) {
    for (m = 0; m <= n; m++) {
      i = (n * (n + 1)) / 2 + m;
      if (n === m) {
        i1 = ((n - 1) * n) / 2 + m - 1;
        p[i] = z * p[i1];
        dp[i] = z * dp[i1] + sinPhi * p[i1];
      } else if (n === 1 && m === 0) {
        i1 = ((n - 1) * n) / 2 + m;
        p[i] = sinPhi * p[i1];
        dp[i] = sinPhi * dp[i1] - z * p[i1];
      } else {
        i1 = ((n - 2) * (n - 1)) / 2 + m;
        i2 = ((n - 1) * n) / 2 + m;
        if (m > n - 2) {
          p[i] = sinPhi * p[i2];
          dp[i] = sinPhi * dp[i2] - z * p[i2];
        } else {
          k = ((n - 1) * (n - 1) - m * m) / ((2 * n - 1) * (2 * n - 3));
          p[i] = sinPhi * p[i2] - k * p[i1];
          dp[i] = sinPhi * dp[i2] - z * p[i2] - k * dp[i1];
        }
      }
    }
  }

  for (n = 1; n <= nMax; n++) {
    i = (n * (n + 1)) / 2;
    i1 = ((n - 1) * n) / 2;
    norm[i] = (norm[i1] * (2 * n - 1)) / n;
    for (m = 1; m <= n; m++) {
      i = (n * (n + 1)) / 2 + m;
      i1 = (n * (n + 1)) / 2 + m - 1;
      norm[i] = norm[i1] * Math.sqrt(((n - m + 1) * (m === 1 ? 2 : 1)) / (n + m));
    }
  }

  for (n = 1; n <= nMax; n++) {
    for (m = 0; m <= n; m++) {
      i = (n * (n + 1)) / 2 + m;
      p[i] *= norm[i];
      dp[i] *= -norm[i];
    }
  }

  return { p: p, dp: dp };
}

// Decimal year, matching the convention the model's secular terms assume.
function decimalYear(date) {
  var y = date.getUTCFullYear();
  return y + (date.valueOf() - Date.UTC(y)) / (1000 * 3600 * 24 * 365);
}

// Coefficients advanced from the model epoch to `date` by their secular rates.
function timeAdjusted(date) {
  var dt = decimalYear(date) - MODEL.epoch;
  var g = [0];
  var h = [0];
  var n, m, i;
  for (n = 1; n <= MODEL.nMax; n++) {
    for (m = 0; m <= n; m++) {
      i = (n * (n + 1)) / 2 + m;
      // Above n_max_sec_var there is no drift term, so the epoch value stands.
      var driftable = n <= MODEL.nMaxSecVar;
      g[i] = MODEL.g[i] + (driftable ? dt * MODEL.dg[i] : 0);
      h[i] = MODEL.h[i] + (driftable ? dt * MODEL.dh[i] : 0);
    }
  }
  return { g: g, h: h };
}

// Magnetic variation in degrees, east positive. `altFt` is optional.
// Returns null rather than a wrong number if the inputs are unusable.
function declination(lat, lon, altFt, date) {
  if (typeof lat !== "number" || typeof lon !== "number") return null;
  if (!isFinite(lat) || !isFinite(lon)) return null;
  if (lat > 90 || lat < -90) return null;

  var heightKm = ((altFt || 0) * 0.3048) / 1000;
  var c = timeAdjusted(date || new Date());
  var nMax = MODEL.nMax;

  // Geodetic -> geocentric spherical.
  var cosLat = Math.cos(lat * DEG);
  var sinLat = Math.sin(lat * DEG);
  var rc = WGS84_A / Math.sqrt(1 - EPS_SQ * sinLat * sinLat);
  var xp = (rc + heightKm) * cosLat;
  var zp = (rc * (1 - EPS_SQ) + heightKm) * sinLat;
  var r = Math.sqrt(xp * xp + zp * zp);
  var phig = Math.asin(zp / r);

  var lf = legendre(Math.sin(phig), nMax);

  var cosLambda = Math.cos(lon * DEG);
  var sinLambda = Math.sin(lon * DEG);
  var cosML = [1.0, cosLambda];
  var sinML = [0.0, sinLambda];
  var m, n, i;
  for (m = 2; m <= nMax; m++) {
    cosML[m] = cosML[m - 1] * cosLambda - sinML[m - 1] * sinLambda;
    sinML[m] = cosML[m - 1] * sinLambda + sinML[m - 1] * cosLambda;
  }

  var rr = [(GEOMAG_RE / r) * (GEOMAG_RE / r)];
  for (n = 1; n <= nMax; n++) rr[n] = rr[n - 1] * (GEOMAG_RE / r);

  var bx = 0;
  var by = 0;
  var bz = 0;
  for (n = 1; n <= nMax; n++) {
    for (m = 0; m <= n; m++) {
      i = (n * (n + 1)) / 2 + m;
      var cs = c.g[i] * cosML[m] + c.h[i] * sinML[m];
      bz -= rr[n] * cs * (n + 1) * lf.p[i];
      by += rr[n] * (c.g[i] * sinML[m] - c.h[i] * cosML[m]) * m * lf.p[i];
      bx -= rr[n] * cs * lf.dp[i];
    }
  }

  var cosPhig = Math.cos(phig);
  if (Math.abs(cosPhig) > 1e-10) {
    by = by / cosPhig;
  } else {
    // Within metres of a geographic pole the 1/cos term blows up. Declination
    // is meaningless there for our purposes, and no GA aircraft is asking.
    return null;
  }

  // Rotate the geocentric field back onto the geodetic horizon.
  var psi = phig - lat * DEG;
  var bxGeo = bx * Math.cos(psi) - bz * Math.sin(psi);
  var byGeo = by;

  var decl = Math.atan2(byGeo, bxGeo) / DEG;
  if (!isFinite(decl)) return null;
  return decl;
}

// True -> magnetic. Variation east means magnetic reads less than true
// ("variation east, magnetic least").
function trueToMagnetic(trueDeg, decl) {
  var v = trueDeg - decl;
  v = v % 360;
  if (v < 0) v += 360;
  return v;
}

module.exports = {
  declination: declination,
  trueToMagnetic: trueToMagnetic,
  modelEnd: MODEL.endDate
};
