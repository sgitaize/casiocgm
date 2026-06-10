/*
 * CasioCGM – Phone-side JS
 * Fetches Nightscout, Weather, sends data + config to watch
 */

var config = {};
var fetchTimer = null;
var FETCH_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes

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
  CGM_VALUE:       50, CGM_DELTA:      51, CGM_TREND:       52,
  CGM_AGE:         53, STEPS:          54, HR:              55,
  WEATHER_TEMP:    56, WEATHER_ICON:   57, BATT_PCT:        58
};

// ── Trend direction → single ASCII char (drawn graphically in C) ──────────
// U=strong up, u=up, r=rising(45°), -=flat, f=falling(45°), d=down, D=strong down
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

// ── Send config to watch ──────────────────────────────────────────────────
function sendConfig() {
  var msg = {};
  msg[K.NS_URL]          = config.nsUrl         || '';
  msg[K.NS_TOKEN]        = config.nsToken        || '';
  msg[K.NS_UNITS]        = parseInt(config.nsUnits)     || 0;
  msg[K.NS_HIGH]         = parseInt(config.nsHigh)      || 180;
  msg[K.NS_LOW]          = parseInt(config.nsLow)       || 70;
  msg[K.NS_STALE_MIN]    = parseInt(config.nsStaleMin)  || 10;
  msg[K.COLOR_BG]        = colorToInt(config.colorBg     || '#FFFFFF');
  msg[K.COLOR_FG]        = colorToInt(config.colorFg     || '#000055');
  msg[K.COLOR_ACCENT]    = colorToInt(config.colorAccent || '#0000FF');
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
  msg[K.COLOR_GHOST]     = colorToInt(config.colorGhost    || '#ADADAD');
  msg[K.COLOR_LABEL_TOP] = colorToInt(config.colorLabelTop || '#FFFFFF');

  Pebble.sendAppMessage(msg, function() {
    console.log('[CasioCGM] Config sent');
  }, function(e) {
    console.log('[CasioCGM] Config send failed: ' + JSON.stringify(e));
  });
}

// ── Fetch Nightscout ──────────────────────────────────────────────────────
// Uses the /pebble endpoint:
//   GET <nsUrl>/pebble
//   Response: {"bgs":[{"sgv":"108","trend":1,"direction":"DoubleUp",
//               "datetime":1780980660000,"bgdelta":18,...}],...}
function fetchNightscout() {
  var url = config.nsUrl;
  if (!url || url.length < 4) return;

  // Remove trailing slash, then append /pebble
  url = url.replace(/\/$/, '');
  var apiUrl = url + '/pebble';
  if (config.nsToken) {
    apiUrl += '?token=' + config.nsToken;
  }

  var req = new XMLHttpRequest();
  req.open('GET', apiUrl, true);
  req.timeout = 15000;
  req.onload = function() {
    if (req.status === 200) {
      try {
        var data = JSON.parse(req.responseText);
        if (!data || !data.bgs || data.bgs.length === 0) return;

        var bg     = data.bgs[0];
        var sgv    = parseInt(bg.sgv)      || 0;
        var delta  = parseInt(bg.bgdelta)  || 0;
        var trend  = trendArrow(bg.direction || '');
        var ageMs  = Date.now() - (bg.datetime || 0);
        var ageMin = Math.round(ageMs / 60000);

        // Convert to mmol if needed
        var valStr, deltaStr;
        if (parseInt(config.nsUnits) === 1) {
          valStr   = (sgv / 18.0).toFixed(1);
          deltaStr = (delta >= 0 ? '+' : '') + (delta / 18.0).toFixed(1);
        } else {
          valStr   = String(sgv);
          deltaStr = (delta >= 0 ? '+' : '') + String(delta);
        }

        var msg = {};
        msg[K.CGM_VALUE] = valStr;
        msg[K.CGM_DELTA] = deltaStr;
        msg[K.CGM_TREND] = trend;
        msg[K.CGM_AGE]   = ageMin;

        Pebble.sendAppMessage(msg, function() {
          console.log('[CasioCGM] CGM sent: ' + valStr + ' ' + trend + ' age=' + ageMin + 'min');
        }, function(e) {
          console.log('[CasioCGM] CGM send failed');
        });
      } catch (ex) {
        console.log('[CasioCGM] Parse error: ' + ex);
      }
    }
  };
  req.onerror = function() {
    console.log('[CasioCGM] Fetch error');
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
          Pebble.sendAppMessage(msg);
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
  fetchNightscout();
  fetchWeather();
  if (fetchTimer) clearInterval(fetchTimer);
  fetchTimer = setInterval(function() {
    fetchNightscout();
    fetchWeather();
  }, FETCH_INTERVAL_MS);
}

// ── Pebble events ────────────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  console.log('[CasioCGM] JS ready');
  // Load stored config
  var stored = localStorage.getItem('casiocgm_config');
  if (stored) {
    try { config = JSON.parse(stored); } catch(e) {}
  }
  sendConfig();
  scheduleFetch();
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') return;
  try {
    config = JSON.parse(decodeURIComponent(e.response));
    localStorage.setItem('casiocgm_config', JSON.stringify(config));
    sendConfig();
    scheduleFetch();
  } catch(ex) {
    console.log('[CasioCGM] Config parse error: ' + ex);
  }
});

Pebble.addEventListener('showConfiguration', function() {
  var baseUrl = 'http://casiocgm.aize-it.de/config/';
  var stored  = localStorage.getItem('casiocgm_config') || '{}';
  var url     = baseUrl + '?config=' + encodeURIComponent(stored);
  Pebble.openURL(url);
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('[CasioCGM] Message from watch: ' + JSON.stringify(e.payload));
});
