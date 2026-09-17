# Nav Aid

A GPS navigation and flight aid for GA pilots, on a Pebble. Your phone's GPS
feeds the watch: distance and ETA to the next waypoint, leg times, fuel
remaining, nearby ADS-B traffic, the nearest METAR.

> **Not a certified navigation or traffic system.** Situational awareness only,
> and no substitute for charts, a flight plan, a working altimeter, or looking
> out of the window. Traffic shows only aircraft that transmit ADS-B and are in
> range of a volunteer receiver, so an empty traffic page means *no
> information*, never *no traffic*.

## Pages

UP and DOWN to move between them, and any of them can be turned off in
settings. The clock sits in the header on every page.

| Page | Shows | SELECT | Hold SELECT |
|---|---|---|---|
| NAV | Active waypoint, distance, bearing, groundspeed, ETE/ETA, course deviation bar, and distance and ETA to the end of the route | Next waypoint | Direct-to list |
| TIMER | Stopwatch, distance flown while it ran, average groundspeed | Start / stop | Reset |
| FUEL | Fuel remaining, endurance, used, fuel on arrival | Engine clock on / off | Refuel to full |
| TRAFFIC | Radar plan view of nearby ADS-B aircraft, or a text list | Cycle orientation | Radar ⇄ list |
| WX | Nearest METAR: wind with head/crosswind on track, QNH, temperature, density altitude, ceiling, flight category | Refresh | Raw METAR |
| INFO | Position, GPS altitude, track, top of descent, sunset, magnetic variation, link and battery | Refresh everything | Toggle magnetic / true |

Courses are magnetic, from the World Magnetic Model for wherever you actually
are. Waypoints sequence automatically inside 0.6 NM, or on station passage
(within 4 NM, range opening, more than 100° off track on two consecutive
fixes), and never off the last waypoint. SELECT sequences by hand. The engine
clock starts itself above 30 kt.

### Traffic

The number beside each target is its altitude relative to you in hundreds of
feet. `-29` is 2,900 ft below, `+10` is 1,000 ft above, and a trailing `^` or
`v` means climbing or descending. Anything inside 3 NM and 1200 ft is drawn in
the warning colour and buzzes the watch, at most once a minute.

An empty page says NONE SEEN when the phone looked and found nothing, and NO
DATA when it could not look at all. The footer carries the age of the picture
and turns amber past 90 seconds.

SELECT cycles orientation between track-up (default), heading-up off the watch
magnetometer, and north-up. Any mode falls back when its source is missing, and
the footer says which and why: `TRK (NO MAG)`, `N (SLOW)`, `N (NO GPS)`. Treat
heading-up with suspicion, because a magnetometer on a wrist inside a metal
airframe is a good deal less trustworthy than GPS track.

## Route entry

Settings → Waypoints, one per line, up to 24:

```
YSBK                      airport or ident, worldwide
YSBK 135/12               12 NM on the 135 radial (magnetic) from YSBK
PARRA -33.815 151.001     your own point, decimal degrees
PARRA S33 48.9 E151 0.1   your own point, degrees and decimal minutes
Katoomba Scenic World     place-name lookup
```

Idents resolve against airport databases only, never a fuzzy place search.
Unresolved idents and over-length routes are reported on the INFO page rather
than quietly flown wrong. Everything resolved over the network is cached, so a
saved route loads next flight with no signal.

## Settings

Fuel units, capacity, cruise burn, taxi allowance and reserve. Altimeter in hPa
or inHg. Traffic radius and vertical filter. Which pages show, which alerts
buzz, and circuit altitude for the top-of-descent figure.

## Data sources

All free, keyless and worldwide.

| | |
|---|---|
| Traffic | [adsb.fi](https://adsb.fi) community ADS-B |
| Weather | aviationweather.gov METAR |
| Idents | airport-data.com, then Wikidata by ICAO code, then aviationweather.gov |
| Places | [Photon](https://photon.komoot.io) (OpenStreetMap) |
| Variation | World Magnetic Model 2025, evaluated on the phone |
| Sun times | Computed locally |

Polling: GPS every 2 s; traffic every 15 s on the traffic page and 60 s
elsewhere; METAR every 10 min or 20 NM; variation every 15 NM.

## Building

```sh
pebble build
pebble install --emulator emery
pebble install --phone <ip>
./test/run.sh                    # host tests for the navigation maths
```

Targets emery (200×228), gabbro (260×260) and flint (144×168) only. The older
models are tight on memory and are not built.

`src/c/trig.c` replaces libm's sine, cosine, arctangent, square root and
modulo. Do not put libm back: its argument reduction faulted the app on this
target. `./test/run.sh` checks the replacements against a double-precision
reference.

The bundled WMM coefficients expire in November 2029. Regenerate
`src/pkjs/wmm2025.js` from the NOAA release then.
