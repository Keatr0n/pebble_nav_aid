// Sunrise and sunset. A VFR pilot's hard limit is last light, so this is one
// of the few numbers on the watch that can end a flight rather than inform it.
// Standard NOAA sunrise equation; accurate to well under a minute, which is
// far finer than the legal margins it feeds into.

var DEG = Math.PI / 180;
var J1970 = 2440588;
var J2000 = 2451545;

function toJulian(date) {
  return date.valueOf() / 86400000 - 0.5 + J1970;
}

function fromJulian(j) {
  return (j + 0.5 - J1970) * 86400000;
}

// Returns { sunrise: Date|null, sunset: Date|null }. Null means the sun does
// not cross the horizon that day -- polar summer or winter.
function times(date, lat, lon) {
  // The equation is written in degrees WEST of Greenwich, so eastern
  // longitudes (all of Australia) go in negative.
  var lw = -lon;
  var d = toJulian(date) - J2000;

  // Which solar day at this longitude the given instant falls in.
  var n = Math.round(d - 0.0009 - lw / 360);

  var ds = 0.0009 + lw / 360 + n;
  var M = (357.5291 + 0.98560028 * ds) % 360;
  var Mrad = M * DEG;
  var C = 1.9148 * Math.sin(Mrad) + 0.02 * Math.sin(2 * Mrad) + 0.0003 * Math.sin(3 * Mrad);
  var L = (M + C + 180 + 102.9372) % 360;
  var Lrad = L * DEG;

  var jTransit = J2000 + ds + 0.0053 * Math.sin(Mrad) - 0.0069 * Math.sin(2 * Lrad);

  var delta = Math.asin(Math.sin(Lrad) * Math.sin(23.4397 * DEG));

  // -0.833 degrees accounts for refraction and the solar disc's radius, the
  // same convention official sunset tables use.
  var cosOmega =
    (Math.sin(-0.833 * DEG) - Math.sin(lat * DEG) * Math.sin(delta)) /
    (Math.cos(lat * DEG) * Math.cos(delta));

  if (cosOmega > 1 || cosOmega < -1) return { sunrise: null, sunset: null };

  var omega = Math.acos(cosOmega) / DEG;
  var jSet = J2000 + (0.0009 + (omega + lw) / 360 + n) +
             0.0053 * Math.sin(Mrad) - 0.0069 * Math.sin(2 * Lrad);
  var jRise = jTransit - (jSet - jTransit);

  return {
    sunrise: new Date(fromJulian(jRise)),
    sunset: new Date(fromJulian(jSet))
  };
}

module.exports = { times: times };
