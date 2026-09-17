# Nav Aid

A GPS navigation and flight aid for GA pilots, on a Pebble.

It keeps the handful of numbers you otherwise keep recomputing on your knee —
distance and ETA to the next waypoint, leg time and distance, fuel remaining,
nearby ADS-B traffic, the nearest METAR — on your wrist, updated from your
phone's GPS.

> **Not a certified navigation or traffic system.** Situational awareness only.
> It is no substitute for charts, a flight plan, a working altimeter, or
> looking out of the window. Traffic comes from a volunteer ADS-B network and
> shows only aircraft that transmit ADS-B and are within range of a receiver —
> an empty traffic page means *no information*, never *no traffic*.

## Pages

Six pages, **UP** and **DOWN** to move between them. Any of them can be turned
off in settings if you only want three. The clock sits in the header on every
page, so you never have to leave the page you are on to read the time.

| Page | Shows | SELECT | Hold SELECT |
|---|---|---|---|
| **NAV** | Active waypoint, distance, bearing, groundspeed, ETE/ETA, course deviation bar, and distance and ETA to the far end of the route | Next waypoint | Direct-to list |
| **TIMER** | Stopwatch, distance flown while it ran, and average groundspeed over that period | Start / stop | Reset |
| **FUEL** | Fuel remaining, endurance, used, and fuel on arrival at the destination | Engine clock on / off | Refuel to full |
| **TRAFFIC** | Radar plan view of nearby ADS-B aircraft, or a text list | Cycle orientation | Radar ⇄ list |
| **WX** | Nearest METAR: wind with head/crosswind on track, QNH, temperature, density altitude, ceiling, flight category | Refresh | Raw METAR |
| **INFO** | Position, GPS altitude, track, top of descent, sunset and time to run, magnetic variation, link and battery | Refresh everything | Toggle magnetic / true |

### Navigation

Courses are **magnetic** by default, computed from the World Magnetic Model for
wherever you actually are — which matters, given variation runs from about
12°E in Sydney to 1.5°W at Perth.

Waypoints **sequence automatically** on either of two triggers:

- **Capture ring** — you come within **0.6 NM** of the waypoint.
- **Station passage** — you are within **4 NM**, the range is opening, and the
  waypoint has fallen more than **100°** off your track, on two consecutive
  fixes. This is what catches the usual case of cutting a corner or drifting
  wide enough never to clip the ring.

Requiring two fixes in a row, with the waypoint behind the wingline, is what
keeps GPS jitter on the inbound leg from sequencing you early. It never
sequences off the **last** waypoint, so the display stays on your destination
rather than running out of route.

Flown against a simulated track passing 1.5 NM abeam at 110 kt, it holds the
waypoint through the abeam point and sequences about 1.3 NM past it. Press
SELECT to sequence by hand at any time, hold SELECT for direct-to, or turn
auto-sequencing off in settings entirely.

The engine clock starts itself once you are moving above 30 kt, so the fuel
page stays honest even if you forget to press anything. Timers are stored with
an absolute start time, so a leg keeps counting if the app is closed and
reopened mid-flight — though distance-flown can only accumulate while the app
is actually open.

### Traffic radar

The plan view puts you at the centre with range rings that snap to a round
number. Radar is the default view; **hold SELECT to switch to the text list**
and back.

**The number beside each target is its altitude relative to you, in hundreds of
feet.** `-29` is 2,900 ft below you, `+10` is 1,000 ft above; a trailing `^` or
`v` means it is climbing or descending. This is the usual traffic-display
convention, and the radar footer spells it out so you never have to remember.
Anything inside 3 NM and 1200 ft is drawn in the warning colour and buzzes the
watch, at most once a minute.

Press SELECT to cycle what "up" means:

- **TRK UP** (default) — your GPS ground track.
- **HDG UP** — the watch's magnetometer. Offered because it was asked for, but
  treat it with suspicion: a magnetometer inside a metal airframe, on a wrist
  that moves independently of the aeroplane, is much less trustworthy than GPS
  track. It falls back to track-up while the compass is uncalibrated.
- **N UP** — true north, with your own ship symbol swinging to show track.

ADS-B bearings arrive as true; the watch compass reads magnetic. The app
converts between them using the same WMM variation it uses everywhere else.

## Route entry

Settings → Waypoints, one per line, up to 24:

```
YSBK                      airport or ident, worldwide
YSBK 135/12               12 NM on the 135 radial (magnetic) from YSBK
PARRA -33.815 151.001     your own point, decimal degrees
PARRA S33 48.9 E151 0.1   your own point, degrees and decimal minutes
Katoomba Scenic World     place-name lookup
```

Idents are resolved against airport databases **only**, never against a fuzzy
place search. During development `YWOL` — a retired code for what is now
Shellharbour, `YSHL` — fuzzy-matched to Newcastle Airport, 150 NM from the
field meant. A nav aid that quietly points somewhere plausible but wrong is
worse than one that admits it does not know, so an unresolved ident is reported
on the INFO page and you can enter coordinates instead.

Everything resolved over the network is cached, so a saved route re-sends
instantly and still loads next flight with no signal.

## Settings

Fuel in litres, US gallons, imperial gallons, kilograms or pounds; capacity,
cruise burn, taxi allowance and fixed reserve. Altimeter setting in hPa or
inHg. Traffic radius and vertical filter. Which pages to show. Which alerts
buzz. Circuit altitude, used for the top-of-descent figure.

## Data sources

All free, keyless and worldwide:

| | |
|---|---|
| Traffic | [adsb.fi](https://adsb.fi) community ADS-B |
| Weather | aviationweather.gov METAR (carries international stations, QNH in hPa) |
| Idents | airport-data.com, then Wikidata by ICAO code, then aviationweather.gov |
| Places | [Photon](https://photon.komoot.io) (OpenStreetMap) |
| Variation | World Magnetic Model 2025, evaluated on the phone |
| Sun times | Computed locally |

### Update rates

| | Interval |
|---|---|
| GPS position | ~2 s |
| Traffic, while the traffic page is open | 15 s |
| Traffic, on any other page | 60 s |
| METAR | 10 min, or after 20 NM of movement |
| Magnetic variation | after 15 NM of movement |
| Sunset | once a day |

The watch tells the phone which page is open, so the radar refreshes four times
a minute while you are looking at it and eases off when you are not — the ADS-B
feed is free and community-run, and worth being polite to. Arriving on the
traffic page forces an immediate refresh rather than showing you the tail end of
the slower cadence. Failed polls back off to a four-minute ceiling.

Polling runs on its own timer rather than off the back of a GPS callback. It
used to be callback-driven, which meant that if the phone's geolocation went
quiet — no fix, or the OS backgrounding the app — traffic and weather silently
stopped updating. The watch also now shows the age of the last fix on the INFO
page, and turns the distance on the NAV page amber once it is more than ten
seconds old, so a frozen number cannot be mistaken for a live one.

## Building

```sh
pebble build
pebble install --emulator emery
pebble install --phone <ip>
./test/run.sh                    # host tests for the navigation maths
```

Targets current hardware only — **emery** (Pebble Time 2, 200×228),
**gabbro** (Pebble Round 2, 260×260) and **flint** (Pebble 2 Duo, 144×168).
The older models are not built: they are tight on memory, and dropping them
means the layout can use the space the current screens actually have.

## Layout

```
src/c/
  main.c          window, buttons, one-second redraw
  pages.c         page table and dispatch
  page_*.c        one file per page
  wpt_menu.c      direct-to waypoint list
  state.c         flight state, timers, fuel, auto-sequencing, persistence
  comm.c          AppMessage to and from the phone
  compass.c       magnetometer handling and radar orientation
  geodesy.c       great-circle distance and bearing
  trig.c          self-contained float maths (see below)
  geo.c           fixed-point wrappers, magnetic variation, density altitude
  ui.c            drawing kit: header, rows, big values, CDI, arrows
src/pkjs/
  index.js        GPS, orchestration, message queue
  route.js        waypoint parsing and resolution
  traffic.js      ADS-B
  weather.js      METAR
  magvar.js       World Magnetic Model
  sun.js          sunrise and sunset
  config.json     settings UI
test/             host tests for trig and geodesy
```

### Why there is a hand-written trig library

`src/c/trig.c` implements sine, cosine, arctangent, square root and modulo
rather than calling libm. This is not premature optimisation — libm's trig
faulted the app on this target, with the program counter landing inside libm's
own `__ieee754_rem_pio2f` argument reduction. The replacements are plain
polynomial evaluations: no tables, no large stack frames, and about 1.5 KB
smaller.

They are checked against a double-precision reference by `./test/run.sh`.
Across 200,000 random global point pairs the worst error is **0.04 % in
distance and 0.018° in bearing**, which is far inside what the display can even
show. The tests also pin the edge cases that used to hang the watch: angle
normalisation by repeated subtraction never terminates when fed an infinity,
and a watchdog reset in flight looks exactly like the app vanishing.

### Model expiry

The bundled WMM coefficients are valid until **November 2029**. After that,
regenerate `src/pkjs/wmm2025.js` from the NOAA release; until then variation
degrades gracefully by secular extrapolation.
