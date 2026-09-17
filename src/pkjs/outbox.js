// The one path from phone to watch.
//
// AppMessage carries a single message at a time, so everything queues. What
// matters is that the queue knows not everything in it is equally disposable.
// A position is: another one is along in two seconds, and a stale one is
// worth less than the current one. A route message is not: the watch reads a
// route as a count followed by one message per waypoint, so a message lost
// in the middle leaves it navigating half of the old route with nothing on
// screen to say so.
//
// So droppable entries are the only ones dropped to make room, anything else
// is retried before being given up on, and giving one up calls that entry's
// own `onLost` -- which is how the route learns it has to go again. Traffic
// is droppable too, despite arriving in sets: a set that loses a message is
// discarded by the watch and replaced by the next poll a few seconds later,
// so it should give way to a route rather than compete with one.

function create(opts) {
  var send = opts.send;
  var limit = opts.limit || 40;
  var retries = opts.retries || 3;
  var onLoss = opts.onLoss || function () {};
  // Fires whenever anything gets through, so callers can tell a link that is
  // merely busy from one that is gone.
  var onSent = opts.onSent || function () {};
  // Injectable so tests need no timers, and so a retry never runs on the same
  // turn of the event loop as the failure that caused it.
  var defer = opts.defer || function (fn, ms) { setTimeout(fn, ms); };
  var retryMs = opts.retryMs || 400;

  var queue = [];
  var sending = false;

  function makeRoom() {
    for (var i = 0; i < queue.length; i++) {
      if (queue[i].droppable) { queue.splice(i, 1); return; }
    }
    // Nothing disposable left: something that mattered is about to be lost.
    var lost = queue.shift();
    lost.onLost();
  }

  function pump() {
    if (sending || queue.length === 0) return;
    sending = true;
    var item = queue.shift();
    send(item.dict, function () {
      sending = false;
      onSent();
      pump();
    }, function (e) {
      sending = false;
      item.tries++;
      if (!item.droppable && item.tries < retries) {
        queue.unshift(item);
        defer(pump, retryMs * item.tries);
        return;
      }
      if (!item.droppable) item.onLost();
      pump();
    });
  }

  // `opts`: { droppable, onLost }. Both optional; onLost falls back to the
  // outbox-wide handler.
  function enqueue(dict, opts) {
    if (queue.length >= limit) makeRoom();
    queue.push({
      dict: dict,
      droppable: !!(opts && opts.droppable),
      onLost: (opts && opts.onLost) || onLoss,
      tries: 0
    });
    pump();
  }

  return {
    enqueue: enqueue,
    depth: function () { return queue.length; },
    busy: function () { return sending; }
  };
}

module.exports = { create: create };
