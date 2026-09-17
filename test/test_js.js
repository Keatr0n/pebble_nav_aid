// Host tests for the phone side.
//
// These cover the parts that decide what the watch is told rather than how it
// is drawn: which altitude a target is compared against, what happens to a
// route too long to fit, and which messages the outbox is allowed to throw
// away when the link backs up. All three are places where being quietly wrong
// looks exactly like being right.

var path = require("path");
var outbox = require(path.join(__dirname, "../src/pkjs/outbox.js"));
var traffic = require(path.join(__dirname, "../src/pkjs/traffic.js"));
var route = require(path.join(__dirname, "../src/pkjs/route.js"));

var failures = 0;

function check(what, got, want) {
  var ok = JSON.stringify(got) === JSON.stringify(want);
  if (!ok) failures++;
  console.log("  " + (ok ? "ok  " : "FAIL") + " " + pad(what, 34) +
              " got " + pad(JSON.stringify(got), 12) + " want " + JSON.stringify(want));
}

function pad(s, n) {
  s = String(s);
  while (s.length < n) s += " ";
  return s;
}

// ------------------------------------------------- comparable altitude

console.log("target altitude must be comparable with our own GPS altitude:");

check("geometric is used as-is",
      traffic.comparableAltFt({ alt_geom: 4500, alt_baro: 4900 }, 1000), 4500);
check("geometric wins even with no QNH",
      traffic.comparableAltFt({ alt_geom: 4500, alt_baro: 4900 }, null), 4500);
// 1013.25 - 1000 = 13.25 hPa low, so pressure altitude reads ~358 ft high.
check("baro carried to MSL at QNH 1000",
      traffic.comparableAltFt({ alt_baro: 4858 }, 1000), 4500);
check("baro at ISA is unchanged",
      traffic.comparableAltFt({ alt_baro: 4500 }, 1013.25), 4500);
// A high QNH means pressure altitude reads low, so the correction is upward.
check("baro carried up at QNH 1030",
      traffic.comparableAltFt({ alt_baro: 4500 }, 1030), 4952);
check("nonsense QNH falls back to ISA",
      traffic.comparableAltFt({ alt_baro: 4500 }, 7), 4500);
check("no QNH at all falls back to ISA",
      traffic.comparableAltFt({ alt_baro: 4500 }, null), 4500);
check("no altitude reported", traffic.comparableAltFt({}, 1013.25), null);
check("ground string is not a number",
      traffic.comparableAltFt({ alt_baro: "ground" }, 1013.25), null);

// The size of the error this exists to remove.
var raw = 4858;
var corrected = traffic.comparableAltFt({ alt_baro: raw }, 1000);
console.log("  ..  uncorrected baro would have read " + (raw - corrected) +
            " ft high against our own GPS altitude");

// ------------------------------------------------------ route overflow

console.log("\na route too long to fit must say so, not truncate in silence:");

// Coordinate lines resolve locally, so none of this touches the network.
function coordLines(n) {
  var out = [];
  for (var i = 0; i < n; i++) {
    out.push("P" + i + " -33." + (100 + i) + " 151.00" + (i % 10));
  }
  return out.join("\n");
}

var done = 0;

route.resolve(coordLines(30), 24, null, function (wpts, errors, dropped) {
  check("30 lines, 24 max: kept", wpts.length, 24);
  check("30 lines, 24 max: dropped", dropped, 6);
  check("30 lines, 24 max: errors", errors.length, 0);
  done++;
});

route.resolve(coordLines(5), 24, null, function (wpts, errors, dropped) {
  check("5 lines: kept", wpts.length, 5);
  check("5 lines: nothing dropped", dropped, 0);
  done++;
});

// Blank lines past the limit are not waypoints and must not be counted.
route.resolve(coordLines(24) + "\n\n\n", 24, null, function (wpts, errors, dropped) {
  check("blank tail is not a drop", dropped, 0);
  done++;
});

route.resolve("", 24, null, function (wpts, errors, dropped) {
  check("empty route", wpts.length, 0);
  check("empty route drops nothing", dropped, 0);
  done++;
});

// ------------------------------------------------------------- outbox

console.log("\nthe outbox must drop positions before it drops a route:");

function harness(opts) {
  var sent = [];
  var pending = [];
  var deferred = [];
  var losses = 0;
  var sends = 0;
  var o = outbox.create({
    limit: (opts && opts.limit) || 40,
    retries: (opts && opts.retries) || 3,
    send: function (dict, ok, fail) { pending.push({ dict: dict, ok: ok, fail: fail }); },
    onLoss: function () { losses++; },
    onSent: function () { sends++; },
    defer: function (fn) { deferred.push(fn); }
  });
  return {
    box: o,
    sent: sent,
    losses: function () { return losses; },
    sends: function () { return sends; },
    // Let the in-flight message succeed or fail, then run any retry timer.
    settle: function (succeed) {
      while (pending.length) {
        var p = pending.shift();
        if (succeed) { sent.push(p.dict); p.ok(); }
        else { p.fail(new Error("no link")); }
        while (deferred.length) deferred.shift()();
      }
    }
  };
}

// A stalled link: one message goes in flight and never completes, so the
// queue fills behind it exactly as it would over a bad Bluetooth connection.
var h = harness({ limit: 6 });
h.box.enqueue({ WptTotal: 3 });                    // in flight, stuck
for (var i = 0; i < 3; i++) h.box.enqueue({ WptIdx: i });
for (var j = 0; j < 8; j++) h.box.enqueue({ PosLat: j }, { droppable: true });

check("queue held at its limit", h.box.depth(), 6);
check("no route message was lost", h.losses(), 0);

h.settle(true);
var kinds = h.sent.map(function (d) { return Object.keys(d)[0]; });
check("every route message survived",
      kinds.filter(function (k) { return k === "WptTotal" || k === "WptIdx"; }).length, 4);
check("positions were the ones dropped",
      kinds.filter(function (k) { return k === "PosLat"; }).length < 8, true);

console.log("\na failed send is retried, and confessed to if it never lands:");

var r = harness({ retries: 3 });
r.box.enqueue({ WptIdx: 0 });
r.settle(false);
check("given up on after its retries", r.losses(), 1);

// A per-entry handler, so losing a traffic message cannot be mistaken for
// losing a route message.
var m = harness({ retries: 3 });
var mine = 0;
m.box.enqueue({ WptIdx: 0 }, { onLost: function () { mine++; } });
m.settle(false);
check("the entry's own handler ran", mine, 1);
check("the outbox-wide one did not", m.losses(), 0);

var q = harness({ retries: 3 });
q.box.enqueue({ PosLat: 1 }, { droppable: true });
q.settle(false);
check("a lost position is not confessed to", q.losses(), 0);

var t = harness({});
t.box.enqueue({ WptIdx: 0 });
t.settle(true);
check("a send that lands is reported", t.sends(), 1);

// ------------------------------------------------------------- finish

process.on("exit", function () {
  if (done !== 4) {
    console.log("  FAIL only " + done + " of 4 route callbacks ran");
    failures++;
  }
  console.log("\n" + (failures ? "JS TESTS FAILED" : "JS TESTS PASSED"));
  process.exitCode = failures ? 1 : 0;
});
