// Minimal GET helper. Everything this app talks to is a plain JSON GET, and
// PebbleKit JS has no fetch.
function getJSON(url, timeoutMs, onDone) {
  var xhr = new XMLHttpRequest();
  var finished = false;

  function done(err, data) {
    if (finished) return;
    finished = true;
    onDone(err, data);
  }

  try {
    xhr.open("GET", url, true);
  } catch (e) {
    done(e, null);
    return;
  }
  xhr.timeout = timeoutMs || 15000;
  xhr.onload = function () {
    if (xhr.status < 200 || xhr.status >= 300) {
      done(new Error("HTTP " + xhr.status), null);
      return;
    }
    try {
      done(null, JSON.parse(xhr.responseText));
    } catch (e) {
      done(new Error("bad JSON"), null);
    }
  };
  xhr.onerror = function () { done(new Error("network"), null); };
  xhr.ontimeout = function () { done(new Error("timeout"), null); };
  try {
    xhr.send();
  } catch (e) {
    done(e, null);
  }
}

module.exports = { getJSON: getJSON };
