// CasioTime2 pkjs — CGM fetch logic ported from Nightscout-supercgm.
// Status semantics, unit encoding (mmol -> value*10), staleness rule
// (2× sensor interval) and server-time based scheduling mirror
// github.com/sgitaize/Nightscout-supercgm (src/js/pebble-js-app.js).
/* global Pebble */
(function() {
  'use strict';

  var CONFIG_URL = 'https://sgitaize.github.io/casiocgm/config/';
  var STORAGE_KEY = 'casiotime2_config';
  var BG_STATUS = { OK: 0, NO_DATA: 1, NO_CONN: 2, OLD: 3 };

  var config = {
    bgUrl: null,
    authToken: null,
    bgUnit: 'mgdl',            // 'mgdl' | 'mmol'
    low: 80,                   // in configured unit (mmol as 4.4)
    high: 180,
    bgFetchIntervalMin: 5,     // sensor interval
    syncBgWithInterval: true,
    bgManualIntervalMin: 5,
    vibeOnLow: false,
    vibeOnHigh: false,
    showSeconds: true
  };

  var fetchTimer = null;

  // ── AppMessage queue: one message in flight, retry on NACK ──────────────
  var outbox = [];
  var sending = false;

  function pump() {
    if (sending || outbox.length === 0) return;
    sending = true;
    var item = outbox[0];
    Pebble.sendAppMessage(item.dict, function() {
      outbox.shift();
      sending = false;
      pump();
    }, function() {
      item.tries++;
      if (item.tries >= 3) outbox.shift();
      sending = false;
      setTimeout(pump, 1000);
    });
  }

  function sendMsg(dict) {
    outbox.push({ dict: dict, tries: 0 });
    pump();
  }

  function sendStatus(status, extras) {
    var msg = extras || {};
    msg.BG_STATUS = status;
    sendMsg(msg);
  }

  function sensorIntervalMin() {
    return Math.max(1, parseInt(config.bgFetchIntervalMin || 5, 10));
  }

  function sendConfigToWatch() {
    var mmol = config.bgUnit === 'mmol';
    // Thresholds are stored in the configured unit; send ×10 for mmol so C
    // compares in the same unit as sgv (supercgm)
    sendMsg({
      BG_UNIT: mmol ? 1 : 0,
      BG_THRESH_LOW:  mmol ? Math.round((parseFloat(config.low)  || 0) * 10) : Math.round(parseFloat(config.low)  || 80),
      BG_THRESH_HIGH: mmol ? Math.round((parseFloat(config.high) || 0) * 10) : Math.round(parseFloat(config.high) || 180),
      BG_FETCH_INTERVAL_MIN: sensorIntervalMin(),
      VIBE_ON_LOW: config.vibeOnLow ? 1 : 0,
      VIBE_ON_HIGH: config.vibeOnHigh ? 1 : 0,
      SHOW_SECONDS: config.showSeconds === false ? 0 : 1
    });
  }

  // ── Scheduling (supercgm: sensor timestamp + interval + 30s, server time) ─
  function scheduleNextBG(delayMs) {
    if (fetchTimer) { clearTimeout(fetchTimer); fetchTimer = null; }
    var ms = Math.max(15000, parseInt(delayMs, 10) || 0);
    console.log('[CasioTime2] next BG fetch in ' + Math.round(ms / 1000) + 's');
    fetchTimer = setTimeout(fetchBG, ms);
  }

  function planNextBGFetch(lastBgTsSec, serverNowSec) {
    if (!config.bgUrl) {
      if (fetchTimer) { clearTimeout(fetchTimer); fetchTimer = null; }
      return;
    }
    var manualMin = Math.max(1, parseInt(config.bgManualIntervalMin || config.bgFetchIntervalMin || 5, 10));
    var manualMs = manualMin * 60 * 1000;
    if (config.syncBgWithInterval === false) {
      scheduleNextBG(manualMs);
      return;
    }
    var sensorSec = sensorIntervalMin() * 60;
    if (lastBgTsSec && isFinite(lastBgTsSec) && lastBgTsSec > 0) {
      var targetMs = ((lastBgTsSec + sensorSec) * 1000) + 30000;
      var refNowMs = (serverNowSec && isFinite(serverNowSec) && serverNowSec > 0)
                     ? serverNowSec * 1000 : Date.now();
      var delay = targetMs - refNowMs;
      if (delay < 15000) delay = 15000;
      if (delay > manualMs * 3) delay = manualMs;
      scheduleNextBG(delay);
      return;
    }
    scheduleNextBG(Math.min(manualMs, 60000));
  }

  // ── Nightscout /pebble fetch (supercgm parser) ──────────────────────────
  // Auto-detect whether NS sends mmol (float < 40) or mg/dL; for bgUnit
  // mmol always send mmol×10 so C can display "X,Y" without conversion.
  function parseSgv(raw) {
    var f = parseFloat(raw);
    if (!isFinite(f) || f <= 0) return NaN;
    if (config.bgUnit === 'mmol') {
      return f < 40 ? Math.round(f * 10) : Math.round(f * 10 / 18);
    }
    return f < 40 ? Math.round(f * 18) : Math.round(f);
  }

  function parseDelta(raw) {
    var f = parseFloat(raw);
    if (!isFinite(f)) return NaN;
    if (config.bgUnit === 'mmol') {
      return Math.abs(f) < 30 ? Math.round(f * 10) : Math.round(f * 10 / 18);
    }
    return Math.abs(f) < 30 && String(raw).indexOf('.') >= 0
      ? Math.round(f * 18) : Math.round(f);
  }

  function trendArrow(direction) {
    var dir = (direction || '').toLowerCase();
    if (dir.indexOf('doubleup') >= 0) return '^^';
    if (dir.indexOf('singleup') >= 0 || dir === 'up') return '^';
    if (dir.indexOf('fortyfiveup') >= 0) return '^>';
    if (dir.indexOf('flat') >= 0) return '-';
    if (dir.indexOf('fortyfivedown') >= 0) return '>v';
    if (dir.indexOf('singledown') >= 0 || dir === 'down') return 'v';
    if (dir.indexOf('doubledown') >= 0) return 'vv';
    return '';
  }

  function fetchBG() {
    if (!config.bgUrl) {
      sendStatus(BG_STATUS.NO_DATA);
      planNextBGFetch(null);
      return;
    }
    var baseUrl = String(config.bgUrl).replace(/\/$/, '');
    // If bgUrl already contains /pebble (e.g. with ?token= workaround), use as-is
    var url = (baseUrl.indexOf('/pebble') >= 0) ? baseUrl : baseUrl + '/pebble';
    if (config.authToken && url.indexOf('token=') < 0) {
      url += (url.indexOf('?') >= 0 ? '&' : '?') +
             'token=' + encodeURIComponent(config.authToken);
    }
    var req = new XMLHttpRequest();
    req.onload = function() {
      try {
        if (this.status && (this.status < 200 || this.status >= 300)) {
          sendStatus(BG_STATUS.NO_CONN);
          // supercgm stops polling here; keep retrying instead
          planNextBGFetch(null);
          return;
        }
        var json = JSON.parse(this.responseText);
        var sgv = null, ts = null, trend = null, bgDelta = null, serverNow = null;
        var src = null;
        if (json && Array.isArray(json.bgs) && json.bgs.length > 0) {
          src = json.bgs[0];
          if (Array.isArray(json.status) && json.status[0]) {
            serverNow = parseInt(json.status[0].now || 0, 10);
          }
        } else if (Array.isArray(json) && json.length > 0) {
          src = json[0];
        } else if (json && (json.sgv || json.value || json.glucose)) {
          src = json;
        }
        if (src) {
          sgv = parseSgv(src.sgv || src.glucose || src.value);
          ts = parseInt(src.datetime || src.date || src.mills || src.timestamp || 0, 10);
          trend = src.direction || src.trend || null;
          bgDelta = parseDelta(src.bgdelta);
        }
        if (ts && ts > 1000000000000) ts = Math.floor(ts / 1000);          // ms -> s
        if (serverNow && serverNow > 1000000000000) serverNow = Math.floor(serverNow / 1000);

        if (isFinite(sgv) && sgv > 0) {
          var nowSec = (serverNow && isFinite(serverNow) && serverNow > 0)
                       ? serverNow : Math.floor(Date.now() / 1000);
          var bgTs = ts || nowSec;
          var ageSec = nowSec - bgTs;
          var status = (ageSec > sensorIntervalMin() * 60 * 2) ? BG_STATUS.OLD : BG_STATUS.OK;
          sendStatus(status, {
            BG_SGV: sgv,
            BG_TIMESTAMP: bgTs,
            BG_TREND: trendArrow(trend),
            BG_UNIT: config.bgUnit === 'mmol' ? 1 : 0,
            BG_DELTA: isFinite(bgDelta) ? bgDelta : -9999
          });
          console.log('[CasioTime2] BG ' + sgv + ' age=' + Math.round(ageSec / 60) + 'min status=' + status);
          planNextBGFetch(bgTs, nowSec);
        } else {
          sendStatus(BG_STATUS.NO_DATA);
          planNextBGFetch(null);
        }
      } catch (e) {
        console.log('[CasioTime2] parse error: ' + e);
        sendStatus(BG_STATUS.NO_DATA);
        planNextBGFetch(null);
      }
    };
    req.onerror = function() { sendStatus(BG_STATUS.NO_CONN); planNextBGFetch(null); };
    req.ontimeout = function() { sendStatus(BG_STATUS.NO_CONN); planNextBGFetch(null); };
    req.open('GET', url);
    req.timeout = 10000;
    req.send();
  }

  // ── Config load/save ────────────────────────────────────────────────────
  function toBool(v) {
    return !(v === false || v === 0 || v === '0' || v === 'false');
  }

  // Accept supercgm-style (bgUrl/authToken/bgUnit) and legacy casiocgm-style
  // (nsUrl/nsToken/nsUnits) payloads.
  function applyConfig(cfg) {
    if (!cfg || typeof cfg !== 'object') return;
    function pick(a, b) { return cfg[a] !== undefined ? cfg[a] : cfg[b]; }
    var v;
    if ((v = pick('bgUrl', 'nsUrl')) !== undefined) config.bgUrl = v ? String(v).trim() : null;
    if ((v = pick('authToken', 'nsToken')) !== undefined) config.authToken = v ? String(v).trim() : null;
    if (cfg.bgUnit !== undefined) config.bgUnit = cfg.bgUnit === 'mmol' ? 'mmol' : 'mgdl';
    else if (cfg.nsUnits !== undefined) config.bgUnit = parseInt(cfg.nsUnits, 10) === 1 ? 'mmol' : 'mgdl';
    if ((v = pick('low', 'nsLow')) !== undefined) config.low = parseFloat(v) || config.low;
    if ((v = pick('high', 'nsHigh')) !== undefined) config.high = parseFloat(v) || config.high;
    if (cfg.bgFetchIntervalMin !== undefined) config.bgFetchIntervalMin = parseInt(cfg.bgFetchIntervalMin, 10) || 5;
    if (cfg.bgManualIntervalMin !== undefined) config.bgManualIntervalMin = parseInt(cfg.bgManualIntervalMin, 10) || 5;
    if (cfg.syncBgWithInterval !== undefined) config.syncBgWithInterval = toBool(cfg.syncBgWithInterval);
    if (cfg.vibeOnLow !== undefined) config.vibeOnLow = toBool(cfg.vibeOnLow);
    if (cfg.vibeOnHigh !== undefined) config.vibeOnHigh = toBool(cfg.vibeOnHigh);
    if (cfg.showSeconds !== undefined) config.showSeconds = toBool(cfg.showSeconds);
    // supercgm migration: mmol unit but thresholds still in mg/dL (>30)
    if (config.bgUnit === 'mmol' && config.low > 30) {
      config.low  = Math.round(config.low  * 10 / 18) / 10;
      config.high = Math.round(config.high * 10 / 18) / 10;
    }
  }

  function loadConfig() {
    try {
      var saved = localStorage.getItem(STORAGE_KEY);
      if (saved) applyConfig(JSON.parse(saved));
    } catch (e) {}
  }

  function saveConfig() {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(config)); } catch (e) {}
  }

  // ── Pebble events ───────────────────────────────────────────────────────
  Pebble.addEventListener('ready', function() {
    console.log('[CasioTime2] JS ready');
    loadConfig();
    sendConfigToWatch();
    fetchBG();
  });

  Pebble.addEventListener('appmessage', function(e) {
    if (e && e.payload && e.payload.REQUEST_BG !== undefined) fetchBG();
  });

  Pebble.addEventListener('showConfiguration', function() {
    // Current config travels in the URL fragment, so the token is never
    // sent to the web server hosting the page.
    Pebble.openURL(CONFIG_URL + '#cfg=' + encodeURIComponent(JSON.stringify(config)));
  });

  Pebble.addEventListener('webviewclosed', function(e) {
    if (!e || !e.response || e.response === 'CANCELLED') return;
    try {
      var raw = e.response;
      applyConfig(JSON.parse(raw.charAt(0) === '{' ? raw : decodeURIComponent(raw)));
      saveConfig();
      sendConfigToWatch();
      fetchBG();
    } catch (ex) {
      console.log('[CasioTime2] config parse error: ' + ex);
    }
  });
})();
