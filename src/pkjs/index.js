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
var weatherTimer = null;  // Weather refresh timer (setInterval)
var FETCH_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes (legacy fallback)

// ── Config defaults ───────────────────────────────────────────────────────
var configDefaults = {
  bgSource: 'nightscout',    // 'nightscout' | 'openminipump' (local server of the iPhone app)
  ompUrl: 'http://127.0.0.1:17580',
  ompFallback: false,        // openMinipump: use nsUrl/nsToken as fallback when the app is unreachable
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
  CGM_STATUS:      59, CGM_TS:         60, CGM_SGV:         61, CGM_SOURCE: 63, OMP_NEXT: 64, OMP_INTERVAL: 65,
  REQUEST_BG:      62
};

// CGM status semantics (must match main.c / mirrors Nightscout-supercgm)
var BG_STATUS = { OK: 0, NO_DATA: 1, NO_CONN: 2, OLD: 3, NO_APP: 4 };

// ── Trend direction → single ASCII char (drawn graphically in C) ──────────
function trendArrow(direction) {
  // Same matching as supercgm (case-insensitive, also "up"/"down"); unknown
  // directions (NOT COMPUTABLE, RATE OUT OF RANGE, …) draw flat
  var dir = String(direction || '').toLowerCase();
  if (dir.indexOf('doubleup') >= 0)                   return 'U';
  if (dir.indexOf('singleup') >= 0 || dir === 'up')   return 'u';
  if (dir.indexOf('fortyfiveup') >= 0)                return 'r';
  if (dir.indexOf('fortyfivedown') >= 0)              return 'f';
  if (dir.indexOf('singledown') >= 0 || dir === 'down') return 'd';
  if (dir.indexOf('doubledown') >= 0)                 return 'D';
  return '-';
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

// supercgm: a reading is stale after 2x the sensor interval (the watch
// additionally enforces at least 5 min, like supercgm's C code)
function staleMin() {
  var sensorMin = Math.max(1, parseInt(config.bgFetchIntervalMin || configDefaults.bgFetchIntervalMin, 10));
  return sensorMin * 2;
}

// ── Send config to watch ──────────────────────────────────────────────────
function sendConfig() {
  var msg = {};
  // The watch only checks whether a source is configured; send the effective URL.
  msg[K.NS_URL]          = bgBaseUrl()           || '';
  msg[K.NS_TOKEN]        = isOpenMinipump() ? '' : (config.nsToken || '');
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
  msg[K.COLOR_CGM_HIGH]  = colorToInt(config.colorCgmHigh|| '#A96800');
  msg[K.COLOR_CGM_LOW]   = colorToInt(config.colorCgmLow || '#FF0000');
  msg[K.COMPLICATION]    = parseInt(config.complication) || 0;
  msg[K.LABEL_TOP_LEFT]  = config.labelTopLeft   || 'CGM';
  msg[K.LABEL_TOP_RIGHT] = config.labelTopRight  || 'TIME 2';
  msg[K.LABEL_BOTTOM]    = config.labelBottom    || 'E-PAPER DISPLAY';
  msg[K.FIRST_WEEKDAY]   = parseInt(config.firstWeekday) || 0;
  msg[K.DATE_FORMAT]     = parseInt(config.dateFormat)   || 0;
  msg[K.SHAKE_2ND]       = parseInt(config.shake2nd)     || 0;
  msg[K.WDAY_LANG]       = parseInt(config.wdayLang)     || 0;
  msg[K.SHOW_SECONDS]    = parseInt(config.showSeconds)  || 0;
  msg[K.COLOR_GHOST]       = colorToInt(config.colorGhost     || '#AAAAFF');
  msg[K.COLOR_LABEL_TOP]   = colorToInt(config.colorLabelTop  || '#FFFFFF');
  msg[K.GHOST_ENABLED]     = parseInt(config.ghostEnabled) !== 0 ? 1 : 0;
  msg[K.GHOST_COMP_ENABLED]= parseInt(config.ghostCompEnabled) !== 0 ? 1 : 0;
  msg[K.COLOR_CGM_BANNER]  = colorToInt(config.colorCgmBanner || '#55AAFF');
  msg[K.COLOR_TIME2_BG]    = colorToInt(config.colorTime2Bg   || '#FFFFFF');
  msg[K.COLOR_CGM_INFO]    = colorToInt(config.colorCgmInfo    || '#FFFFFF');
  msg[K.BACKLIGHT_ENABLED] = (parseInt(config.backlightEnabled) !== 0) ? 1 : 0;
  msg[K.COLOR_BACKLIGHT]   = colorToInt(config.colorBacklight  || '#FFFFFF');
  msg[K.CGM_BOX_ENABLED]  = (parseInt(config.cgmBoxEnabled) !== 0) ? 1 : 0;
  msg[K.COLOR_CGM_BOX_BG] = colorToInt(config.colorCgmBoxBg || '#EEEEEE');
  msg[K.VIBE_ON_LOW]      = config.vibeOnLow  ? 1 : 0;
  msg[K.VIBE_ON_HIGH]     = config.vibeOnHigh ? 1 : 0;

  sendMsg(msg, 'Config', function() { console.log('[CasioCGM] Config sent'); });
}

// last value on OLD, clears it on NO_DATA / NO_CONN.
function sendBgStatus(status, extra) {
  var msg = extra || {};
  msg[K.CGM_STATUS] = status;
  sendMsg(msg, 'BG status ' + status);
}

// supercgm parsers: auto-detect whether Nightscout sends mmol (float < 40)
// or mg/dL. The watch compares thresholds against mg/dL, so both return mg/dL.


// Delta display like supercgm: "+3" / "-0.2" / "+-0", "--" when unknown
function formatDelta(deltaMgdl, mmol) {
  if (!isFinite(deltaMgdl)) return '--';
  var v = mmol ? Math.round(deltaMgdl / 18 * 10) / 10 : Math.round(deltaMgdl);
  if (v === 0) return '+-0';
  var s = mmol ? Math.abs(v).toFixed(1) : String(Math.abs(v));
  return (v > 0 ? '+' : '-') + s;
}

// Data source: Nightscout (default) or the local web server of the openMinipump
// iPhone app (Nightscout-compatible /pebble on http://127.0.0.1:17580).
function isOpenMinipump() {
  return config.bgSource === 'openminipump' || config.bgSource === 'ipumpmini';
}

function bgBaseUrl() {
  if (isOpenMinipump()) return config.ompUrl || configDefaults.ompUrl;
  return config.nsUrl;
}

// ── CGM data source: shared library pebble-cgm (js/cgm.js) ─────────────────
// Nightscout (default) or iPump-mini with wake schedule, fallback and NO_APP.
var cgm = require('./cgm')({
  name: 'casiocgm',
  settings: function() {
    return {
      source: isOpenMinipump() ? 'ipumpmini' : 'nightscout',
      nsUrl: config.nsUrl, nsToken: config.nsToken,
      ompUrl: config.ompUrl || configDefaults.ompUrl, ompFallback: config.ompFallback,
      sensorMin: parseInt(config.bgFetchIntervalMin || configDefaults.bgFetchIntervalMin, 10),
      sync: !(config.syncBgWithInterval === false || config.syncBgWithInterval === '0' || config.syncBgWithInterval === 0),
      manualMin: parseInt(config.bgManualIntervalMin || configDefaults.bgManualIntervalMin, 10)
    };
  },
  onReading: function(r) {
    var mmol = parseInt(config.nsUnits) === 1;
    var msg = {};
    msg[K.CGM_VALUE]  = mmol ? (r.sgvMgdl / 18.0).toFixed(1) : String(r.sgvMgdl);
    msg[K.CGM_DELTA]  = formatDelta(r.deltaMgdl, mmol);
    msg[K.CGM_TREND]  = trendArrow(r.direction);
    msg[K.CGM_AGE]    = r.ageMin;
    msg[K.CGM_TS]     = r.tsSec;
    msg[K.CGM_SGV]    = r.sgvMgdl;
    msg[K.CGM_SOURCE] = r.fallback ? 1 : 0;   // 1 = Nightscout fallback (watch shows a cloud)
    sendBgStatus((r.nowSec - r.tsSec > staleMin() * 60) ? BG_STATUS.OLD : BG_STATUS.OK, msg);
    console.log('[CasioCGM] CGM: ' + msg[K.CGM_VALUE] + ' ' + msg[K.CGM_TREND] + ' age=' + r.ageMin + 'min');
  },
  onStatus: function(code) {
    sendBgStatus(code === 'NO_APP' ? BG_STATUS.NO_APP : code === 'NO_CONN' ? BG_STATUS.NO_CONN : BG_STATUS.NO_DATA);
  },
  onWakeSchedule: function(next, interval) {
    // The watch wakes this JS with REQUEST_BG at the app's next wake (reliable clock).
    var m = {};
    m[K.OMP_NEXT] = next;
    m[K.OMP_INTERVAL] = interval;
    sendMsg(m, 'wake schedule');
  }
});

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
  cgm.start();

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
  if (!cfg.bgSource)                        cfg.bgSource = configDefaults.bgSource;
  if (!cfg.ompUrl)                          cfg.ompUrl = configDefaults.ompUrl;
  cfg.ompFallback = cfg.ompFallback === true || cfg.ompFallback === '1' || cfg.ompFallback === 1;
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
  // v2.3.6: old default label QUARTZ → CGM (custom labels stay untouched)
  if (config.labelTopLeft === 'QUARTZ') {
    config.labelTopLeft = 'CGM';
    localStorage.setItem('casiocgm_config', JSON.stringify(config));
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
    // Fetch right now (the watch times this to the app's wake window).
    cgm.fetchNow();
  }
});
