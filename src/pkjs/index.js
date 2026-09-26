/*
 * CasioCGM – Phone-side JS
 * Fetches Nightscout, Weather, sends data + config to watch.
 *
 * CGM sync strategy (adapted from Nightscout-supercgm):
 *   When syncBgWithInterval=true: schedule next fetch at
 *     lastReading.timestamp + bgFetchIntervalMin + 30 s
 *   This keeps the fetch closely aligned with when Nightscout actually
 *   receives a new reading, rather than drifting on a fixed interval.
 *   On failure or when sync is off: fall back to bgManualIntervalMin.
 */

var config = {};
var fetchTimer   = null;  // CGM fetch timer (setTimeout)
var weatherTimer = null;  // Weather refresh timer (setInterval)
var FETCH_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes (legacy fallback)

// ── Config defaults ───────────────────────────────────────────────────────
var configDefaults = {
  syncBgWithInterval: true,  // sync fetch to CGM timestamp
  bgFetchIntervalMin: 5,     // expected CGM sensor interval (min)
  bgManualIntervalMin: 5     // fallback / max fetch interval (min)
};

// ── Key constants (must match main.c) ────────────────────────────────────
var K = {
  NS_URL:          0,  NS_TOKEN:       1,  NS_UNITS:        2,
  NS_HIGH:         3,  NS_LOW:         4,  NS_STALE_MIN:    5,
  COLOR_BG:        6,  COLOR_FG:       7,  COLOR_ACCENT:    8,
  COLOR_CGM_OK:    9,  COLOR_CGM_HIGH: 10, COLOR_CGM_LOW:   11,
  COMPLICATION:    12, LABEL_TOP_LEFT: 13, LABEL_TOP_RIGHT: 14,
  LABEL_BOTTOM:    15, FIRST_WEEKDAY:  16, DATE_FORMAT:     17,
  SHAKE_2ND:       18,
  WDAY_LANG:       19,
  SHOW_SECONDS:    20,
  COLOR_GHOST:     21,
  COLOR_LABEL_TOP: 22,
  GHOST_ENABLED:      23,
  COLOR_CGM_BANNER:   24,
  COLOR_TIME2_BG:     25,
  COLOR_CGM_INFO:     26,
  BACKLIGHT_ENABLED:  27,
  COLOR_BACKLIGHT:    28,
  CGM_BOX_ENABLED:    35,
  COLOR_CGM_BOX_BG:   36,
  GHOST_COMP_ENABLED: 37,
  VIBE_ON_LOW:        38,
  VIBE_ON_HIGH:       39,
  CGM_VALUE:          50, CGM_DELTA:      51, CGM_TREND:       52,
  CGM_AGE:         53, STEPS:          54, HR:              55,
  WEATHER_TEMP:    56, WEATHER_ICON:   57, BATT_PCT:        58,
  CGM_STATUS:      59, CGM_TS:         60, CGM_SGV:         61,
  REQUEST_BG:      62
};

// CGM status semantics (must match main.c / mirrors Nightscout-supercgm)
var BG_STATUS = { OK: 0, NO_DATA: 1, NO_CONN: 2, OLD: 3 };

// ── Trend direction → single ASCII char (drawn graphically in C) ──────────
function trendArrow(direction) {
  var map = {
    'DoubleUp':          'U',
    'SingleUp':          'u',
    'FortyFiveUp':       'r',
    'Flat':              '-',
    'FortyFiveDown':     'f',
    'SingleDown':        'd',
    'DoubleDown':        'D',
    'NOT COMPUTABLE':    '?',
    'RATE OUT OF RANGE': '!'
  };
  return map[direction] || '-';
}

// ── Color string (#RRGGBB) → int24 ────────────────────────────────────────
function colorToInt(hex) {
  hex = hex.replace('#', '');
  return parseInt(hex, 16);
}

// ── AppMessage queue: one message in flight, retry on NACK ────────────────
// Config and BG messages are often sent back to back; without a queue the
// second one fails with APP_MSG_BUSY.
var outbox = [];
var sending = false;

function pump() {
  if (sending || outbox.length === 0) return;
  sending = true;
  var item = outbox[0];
  Pebble.sendAppMessage(item.msg, function() {
    outbox.shift();
    sending = false;
    if (item.ok) item.ok();
    pump();
  }, function() {
    item.tries++;
    if (item.tries >= 3) {
      outbox.shift();
      console.log('[CasioCGM] ' + item.label + ' send failed');
    }
    sending = false;
    setTimeout(pump, 1000);
  });
}

function sendMsg(msg, label, ok) {
  outbox.push({ msg: msg, label: label, ok: ok, tries: 0 });
  pump();
}

// supercgm: a reading is stale after 2x the sensor interval (min. 5 min)
function staleMin() {
  var sensorMin = Math.max(1, parseInt(config.bgFetchIntervalMin || configDefaults.bgFetchIntervalMin, 10));
  return Math.max(5, sensorMin * 2);
}

// ── Send config to watch ──────────────────────────────────────────────────
function sendConfig() {
  var msg = {};
  msg[K.NS_URL]          = config.nsUrl         || '';
  msg[K.NS_TOKEN]        = config.nsToken        || '';
  msg[K.NS_UNITS]        = parseInt(config.nsUnits)     || 0;
  // Thresholds are entered in the display unit but always sent as mg/dL,
  // because the watch compares against the raw sgv (CGM_SGV, mg/dL).
  var thHigh = parseFloat(config.nsHigh);
  var thLow  = parseFloat(config.nsLow);
  if (parseInt(config.nsUnits) === 1) {
    thHigh = isFinite(thHigh) ? Math.round(thHigh * 18) : 180;
    thLow  = isFinite(thLow)  ? Math.round(thLow  * 18) : 70;
  } else {
    thHigh = isFinite(thHigh) ? Math.round(thHigh) : 180;
    thLow  = isFinite(thLow)  ? Math.round(thLow)  : 70;
  }
  msg[K.NS_HIGH]         = thHigh;
  msg[K.NS_LOW]          = thLow;
  msg[K.NS_STALE_MIN]    = staleMin();
  msg[K.COLOR_BG]        = colorToInt(config.colorBg     || '#FFFFFF');
  msg[K.COLOR_FG]        = colorToInt(config.colorFg     || '#000055');
  msg[K.COLOR_ACCENT]    = colorToInt(config.colorAccent || '#FF0000');
  msg[K.COLOR_CGM_OK]    = colorToInt(config.colorCgmOk  || '#38571A');
  msg[K.COLOR_CGM_HIGH]  = colorToInt(config.colorCgmHigh|| '#FFAA00');
  msg[K.COLOR_CGM_LOW]   = colorToInt(config.colorCgmLow || '#FF0000');
  msg[K.COMPLICATION]    = parseInt(config.complication) || 0;
  msg[K.LABEL_TOP_LEFT]  = config.labelTopLeft   || 'QUARTZ';
  msg[K.LABEL_TOP_RIGHT] = config.labelTopRight  || 'TIME 2';
  msg[K.LABEL_BOTTOM]    = config.labelBottom    || 'CGM Enabled';
  msg[K.FIRST_WEEKDAY]   = parseInt(config.firstWeekday) || 0;
  msg[K.DATE_FORMAT]     = parseInt(config.dateFormat)   || 0;
  msg[K.SHAKE_2ND]       = parseInt(config.shake2nd)     || 0;
  msg[K.WDAY_LANG]       = parseInt(config.wdayLang)     || 0;
  msg[K.SHOW_SECONDS]    = parseInt(config.showSeconds)  || 0;
  msg[K.COLOR_GHOST]       = colorToInt(config.colorGhost     || '#ADADAD');
  msg[K.COLOR_LABEL_TOP]   = colorToInt(config.colorLabelTop  || '#FFFFFF');
  msg[K.GHOST_ENABLED]     = parseInt(config.ghostEnabled) !== 0 ? 1 : 0;
  msg[K.GHOST_COMP_ENABLED]= parseInt(config.ghostCompEnabled) !== 0 ? 1 : 0;
  msg[K.COLOR_CGM_BANNER]  = colorToInt(config.colorCgmBanner || '#38571A');
  msg[K.COLOR_TIME2_BG]    = colorToInt(config.colorTime2Bg   || '#EEEEEE');
  msg[K.COLOR_CGM_INFO]    = colorToInt(config.colorCgmInfo    || '#FFFFFF');
  msg[K.BACKLIGHT_ENABLED] = (parseInt(config.backlightEnabled) !== 0) ? 1 : 0;
  msg[K.COLOR_BACKLIGHT]   = colorToInt(config.colorBacklight  || '#FFFFFF');
  msg[K.CGM_BOX_ENABLED]  = (parseInt(config.cgmBoxEnabled) !== 0) ? 1 : 0;
  msg[K.COLOR_CGM_BOX_BG] = colorToInt(config.colorCgmBoxBg || '#EEEEEE');
  msg[K.VIBE_ON_LOW]      = config.vibeOnLow  ? 1 : 0;
  msg[K.VIBE_ON_HIGH]     = config.vibeOnHigh ? 1 : 0;

  sendMsg(msg, 'Config', function() { console.log('[CasioCGM] Config sent'); });
}

// ── Smart CGM scheduling ──────────────────────────────────────────────────
// Schedule the next fetchNightscout() call.
// lastBgTsSec: Unix timestamp (seconds) of the last CGM reading, or 0/null.
//
// When syncBgWithInterval=true (default):
//   next fetch = lastReading + bgFetchIntervalMin*60 + 30 s
//   The +30 s buffer ensures Nightscout has received and stored the reading.
//   Clamped to [15 s, 3× manual interval] to handle bad timestamps.
// When syncBgWithInterval=false:
//   next fetch = now + bgManualIntervalMin
// serverNowSec: Nightscout server time (status[0].now) — using it instead of
// the phone clock avoids drift/skew between phone and NS server.
function planNextBGFetch(lastBgTsSec, serverNowSec) {
  if (fetchTimer) { clearTimeout(fetchTimer); fetchTimer = null; }

  var manualMin = Math.max(1, parseInt(config.bgManualIntervalMin || configDefaults.bgManualIntervalMin, 10));
  var manualMs  = manualMin * 60 * 1000;
  var useSync   = config.syncBgWithInterval !== false &&
                  config.syncBgWithInterval !== '0'  &&
                  config.syncBgWithInterval !== 0;

  if (!useSync || !lastBgTsSec || lastBgTsSec <= 0) {
    // No timestamp (error / no data): retry soon so recovery is quick,
    // but never faster than 1 minute.
    var ms = useSync ? Math.min(manualMs, 60000) : manualMs;
    console.log('[CasioCGM] planNextBGFetch: fallback, next in ' + Math.round(ms / 1000) + ' s');
    fetchTimer = setTimeout(fetchNightscout, ms);
    return;
  }

  var sensorMin = Math.max(1, parseInt(config.bgFetchIntervalMin || configDefaults.bgFetchIntervalMin, 10));
  var sensorMs  = sensorMin * 60 * 1000;
  // Target: lastReading + sensor interval + 30 s overhead (supercgm issue #14)
  var refNowMs  = (serverNowSec && isFinite(serverNowSec) && serverNowSec > 0)
                  ? serverNowSec * 1000 : Date.now();
  var targetMs  = lastBgTsSec * 1000 + sensorMs + 30000;
  var delay     = targetMs - refNowMs;

  if (delay < 15000) delay = 15000;  // overdue: poll every 15 s (supercgm)
  if (delay > manualMs * 3) delay = manualMs;  // clamp if ts looks wrong (future)

  console.log('[CasioCGM] planNextBGFetch: synced, next in ' + Math.round(delay / 1000) + ' s');
  fetchTimer = setTimeout(fetchNightscout, delay);
}

// ── Fetch Nightscout ──────────────────────────────────────────────────────
// Uses the /pebble endpoint:
//   GET <nsUrl>/pebble
//   Response: {"bgs":[{"sgv":"108","trend":1,"direction":"DoubleUp",
//               "datetime":1780980660000,"bgdelta":18,...}],...}
// Send a BG status (optionally with data) to the watch. The watch keeps the
// last value on OLD, clears it on NO_DATA / NO_CONN.
function sendBgStatus(status, extra) {
  var msg = extra || {};
  msg[K.CGM_STATUS] = status;
  sendMsg(msg, 'BG status ' + status);
}

// supercgm parsers: auto-detect whether Nightscout sends mmol (float < 40)
// or mg/dL. The watch compares thresholds against mg/dL, so both return mg/dL.
function parseSgvMgdl(raw) {
  var f = parseFloat(raw);
  if (!isFinite(f) || f <= 0) return NaN;
  return f < 40 ? Math.round(f * 18) : Math.round(f);
}

function parseDeltaMgdl(raw) {
  var f = parseFloat(raw);
  if (!isFinite(f)) return NaN;
  return (Math.abs(f) < 30 && String(raw).indexOf('.') >= 0) ? f * 18 : f;
}

// Delta display like supercgm: "+3" / "-0.2" / "+-0", "--" when unknown
function formatDelta(deltaMgdl, mmol) {
  if (!isFinite(deltaMgdl)) return '--';
  var v = mmol ? Math.round(deltaMgdl / 18 * 10) / 10 : Math.round(deltaMgdl);
  if (v === 0) return '+-0';
  var s = mmol ? Math.abs(v).toFixed(1) : String(Math.abs(v));
  return (v > 0 ? '+' : '-') + s;
}

function fetchNightscout() {
  var url = config.nsUrl;
  if (!url || url.length < 4) {
    planNextBGFetch(null);
    return;
  }

  // Remove trailing slash, then append /pebble (unless already present,
  // e.g. token-in-URL workaround)
  url = url.replace(/\/$/, '');
  var apiUrl = (url.indexOf('/pebble') >= 0) ? url : url + '/pebble';
  if (config.nsToken && apiUrl.indexOf('token=') < 0) {
    apiUrl += (apiUrl.indexOf('?') >= 0 ? '&' : '?') +
              'token=' + encodeURIComponent(config.nsToken);
  }

  var req = new XMLHttpRequest();
  req.open('GET', apiUrl, true);
  req.timeout = 15000;
  req.onload = function() {
    if (req.status === 200) {
      try {
        var data = JSON.parse(req.responseText);
        // Handle Nightscout /pebble ({bgs:[...]}), plain arrays and flat
        // objects (supercgm parser)
        var bg = null;
        var serverNow = 0;
        if (data && Array.isArray(data.bgs) && data.bgs.length > 0) {
          bg = data.bgs[0];
          // Prefer Nightscout server time over the phone clock (avoids skew)
          if (Array.isArray(data.status) && data.status[0]) {
            serverNow = parseInt(data.status[0].now || 0, 10) || 0;
          }
        } else if (Array.isArray(data) && data.length > 0) {
          bg = data[0];
        } else if (data && (data.sgv || data.value || data.glucose)) {
          bg = data;
        }
        if (!bg) {
          sendBgStatus(BG_STATUS.NO_DATA);
          planNextBGFetch(null);
          return;
        }

        var sgv    = parseSgvMgdl(bg.sgv || bg.glucose || bg.value) || 0;
        var delta  = parseDeltaMgdl(bg.bgdelta);
        var trend  = trendArrow(bg.direction || bg.trend || '');
        var bgTs   = parseInt(bg.datetime || bg.date || bg.mills || bg.timestamp || 0, 10) || 0;
        if (bgTs > 1000000000000) bgTs = Math.floor(bgTs / 1000);        // ms -> s
        if (serverNow > 1000000000000) serverNow = Math.floor(serverNow / 1000);
        var nowSec = serverNow > 0 ? serverNow : Math.floor(Date.now() / 1000);
        if (!bgTs) bgTs = nowSec;
        var ageMin = Math.max(0, Math.round((nowSec - bgTs) / 60));

        if (sgv <= 0) {
          sendBgStatus(BG_STATUS.NO_DATA);
          planNextBGFetch(null);
          return;
        }

        // OLD after 2x the sensor interval (supercgm); the watch also
        // re-checks this locally on every redraw.
        var status = (nowSec - bgTs > staleMin() * 60) ? BG_STATUS.OLD : BG_STATUS.OK;

        // Display strings (mmol with one decimal)
        var mmol = parseInt(config.nsUnits) === 1;
        var valStr   = mmol ? (sgv / 18.0).toFixed(1) : String(sgv);
        var deltaStr = formatDelta(delta, mmol);

        var msg = {};
        msg[K.CGM_VALUE] = valStr;
        msg[K.CGM_DELTA] = deltaStr;
        msg[K.CGM_TREND] = trend;
        msg[K.CGM_AGE]   = ageMin;
        msg[K.CGM_TS]    = bgTs;
        msg[K.CGM_SGV]   = sgv;
        sendBgStatus(status, msg);
        console.log('[CasioCGM] CGM: ' + valStr + ' ' + trend +
                    ' age=' + ageMin + 'min ts=' + bgTs + ' status=' + status);

        // Schedule next fetch aligned to this reading's timestamp
        planNextBGFetch(bgTs, nowSec);
      } catch (ex) {
        console.log('[CasioCGM] Parse error: ' + ex);
        sendBgStatus(BG_STATUS.NO_DATA);
        planNextBGFetch(null);
      }
    } else {
      // Unlike supercgm, keep polling after HTTP errors
      console.log('[CasioCGM] HTTP error: ' + req.status);
      sendBgStatus(BG_STATUS.NO_CONN);
      planNextBGFetch(null);
    }
  };
  req.onerror = function() {
    console.log('[CasioCGM] Fetch error');
    sendBgStatus(BG_STATUS.NO_CONN);
    planNextBGFetch(null);
  };
  req.ontimeout = function() {
    console.log('[CasioCGM] Fetch timeout');
    sendBgStatus(BG_STATUS.NO_CONN);
    planNextBGFetch(null);
  };
  req.send();
}

// ── Fetch Weather (Open-Meteo, no API key) ───────────────────────────────
function fetchWeather() {
  navigator.geolocation.getCurrentPosition(function(pos) {
    var lat = pos.coords.latitude;
    var lon = pos.coords.longitude;
    var unit = (parseInt(config.nsUnits) === 1) ? 'celsius' : 'fahrenheit';
    var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
              '&longitude=' + lon +
              '&current_weather=true&temperature_unit=' + unit;

    var req = new XMLHttpRequest();
    req.open('GET', url, true);
    req.timeout = 10000;
    req.onload = function() {
      if (req.status === 200) {
        try {
          var data = JSON.parse(req.responseText);
          var temp = Math.round(data.current_weather.temperature);
          var wcode = data.current_weather.weathercode || 0;
          // Simple icon mapping
          var icon = '';
          if (wcode === 0)             icon = '☀'; // sun
          else if (wcode <= 3)         icon = '⛅'; // partly cloudy
          else if (wcode <= 67)        icon = '☔'; // rain
          else if (wcode <= 77)        icon = '❄'; // snow
          else                         icon = '⚡'; // storm

          var msg = {};
          msg[K.WEATHER_TEMP] = temp;
          msg[K.WEATHER_ICON] = icon;
          sendMsg(msg, 'Weather');
        } catch(ex) {}
      }
    };
    req.send();
  }, function(err) {
    console.log('[CasioCGM] Geo error: ' + err.message);
  });
}

// ── Scheduled fetch ──────────────────────────────────────────────────────
function scheduleFetch() {
  // CGM: kick off immediately; planNextBGFetch handles subsequent timing
  if (fetchTimer) { clearTimeout(fetchTimer); fetchTimer = null; }
  fetchNightscout();

  // Weather: refresh every 30 min (independent of CGM sync)
  fetchWeather();
  if (weatherTimer) clearInterval(weatherTimer);
  weatherTimer = setInterval(fetchWeather, 30 * 60 * 1000);
}

// ── Apply config defaults for missing keys ────────────────────────────────
function applyDefaults(cfg) {
  if (cfg.syncBgWithInterval === undefined) cfg.syncBgWithInterval = configDefaults.syncBgWithInterval;
  if (!cfg.bgFetchIntervalMin)              cfg.bgFetchIntervalMin  = configDefaults.bgFetchIntervalMin;
  if (!cfg.bgManualIntervalMin)             cfg.bgManualIntervalMin = configDefaults.bgManualIntervalMin;
  return cfg;
}

// ── Pebble events ────────────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  console.log('[CasioCGM] JS ready');
  // Load stored config
  var stored = localStorage.getItem('casiocgm_config');
  if (stored) {
    try { config = applyDefaults(JSON.parse(stored)); } catch(e) {}
  } else {
    config = applyDefaults({});
  }
  sendConfig();
  scheduleFetch();
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') return;
  try {
    var raw = e.response;
    config = applyDefaults(JSON.parse(raw.charAt(0) === '{' ? raw : decodeURIComponent(raw)));
    localStorage.setItem('casiocgm_config', JSON.stringify(config));
    sendConfig();
    scheduleFetch();
  } catch(ex) {
    console.log('[CasioCGM] Config parse error: ' + ex);
  }
});

Pebble.addEventListener('showConfiguration', function() {
  // Config travels in the URL fragment: the token never reaches the server
  var baseUrl = 'https://sgitaize.github.io/casiocgm/config/';
  var stored  = localStorage.getItem('casiocgm_config') || '{}';
  var url     = baseUrl + '#config=' + encodeURIComponent(stored);
  Pebble.openURL(url);
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('[CasioCGM] Message from watch: ' + JSON.stringify(e.payload));
  // Watch requests an immediate BG fetch (e.g. after BT reconnect)
  if (e.payload && (e.payload[K.REQUEST_BG] !== undefined ||
                    e.payload['' + K.REQUEST_BG] !== undefined ||
                    e.payload.REQUEST_BG !== undefined)) {
    fetchNightscout();
  }
});
