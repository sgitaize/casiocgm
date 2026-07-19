// CasioTime2 pkjs — CGM fetch logic ported from Nightscout-supercgm.
// Status semantics, unit encoding (mmol -> value*10) and server-time based
// scheduling mirror github.com/sgitaize/Nightscout-supercgm.
/* global Pebble */
(function() {
  'use strict';

  // Keys must match src/c/main.c
  var K = {
    BG_STATUS: 0, BG_SGV: 1, BG_TIMESTAMP: 2, BG_TREND: 3, BG_DELTA: 4,
    BG_UNIT: 5, BG_THRESH_LOW: 6, BG_THRESH_HIGH: 7, BG_TIMEOUT_MIN: 8,
    REQUEST_BG: 9, SHOW_SECONDS: 10
  };
  var BG_STATUS = { OK: 0, NO_DATA: 1, NO_CONN: 2, OLD: 3 };

  var config = {
    bgUrl: null,
    authToken: null,
    bgUnit: 'mgdl',            // 'mgdl' | 'mmol'
    low: 80,                   // in configured unit
    high: 180,
    bgTimeoutMin: 20,
    bgFetchIntervalMin: 5,     // sensor interval
    bgManualIntervalMin: 5,
    syncBgWithInterval: true,
    showSeconds: true
  };

  var fetchTimer = null;

  // sgv/delta encoding: mg/dL as int, mmol as value*10 (supercgm convention)
  function encodeSgv(raw) {
    var f = parseFloat(raw);
    if (!isFinite(f) || f <= 0) return NaN;
    if (config.bgUnit === 'mmol') {
      return f < 40 ? Math.round(f * 10) : Math.round(f * 10 / 18);
    }
    return f < 40 ? Math.round(f * 18) : Math.round(f);
  }
  function encodeDelta(raw) {
    var f = parseFloat(raw);
    if (!isFinite(f)) return NaN;
    if (config.bgUnit === 'mmol') {
      return Math.abs(f) < 30 ? Math.round(f * 10) : Math.round(f * 10 / 18);
    }
    return Math.abs(f) < 30 && String(raw).indexOf('.') >= 0
      ? Math.round(f * 18) : Math.round(f);
  }
  function encodeThreshold(raw, fallback) {
    var f = parseFloat(raw);
    if (!isFinite(f) || f <= 0) return fallback;
    if (config.bgUnit === 'mmol') {
      return f < 40 ? Math.round(f * 10) : Math.round(f * 10 / 18);
    }
    return f < 40 ? Math.round(f * 18) : Math.round(f);
  }

  function trendArrow(direction) {
    var dir = (direction || '').toLowerCase();
    if (dir.indexOf('doubleup') >= 0) return '^^';
    if (dir.indexOf('singleup') >= 0 || dir === 'up') return '^';
    if (dir.indexOf('fortyfiveup') >= 0) return '^>';
    if (dir.indexOf('fortyfivedown') >= 0) return '>v';
    if (dir.indexOf('singledown') >= 0 || dir === 'down') return 'v';
    if (dir.indexOf('doubledown') >= 0) return 'vv';
    return '-';
  }

  function sendMsg(dict) {
    Pebble.sendAppMessage(dict, function() {}, function() {
      // one retry (supercgm-style reliability)
      Pebble.sendAppMessage(dict, function() {}, function() {});
    });
  }

  function sendStatus(status, extras) {
    var msg = extras || {};
    msg[K.BG_STATUS] = status;
    sendMsg(msg);
  }

  function sendConfigToWatch() {
    var msg = {};
    msg[K.BG_UNIT] = config.bgUnit === 'mmol' ? 1 : 0;
    msg[K.BG_THRESH_LOW]  = encodeThreshold(config.low, 80);
    msg[K.BG_THRESH_HIGH] = encodeThreshold(config.high, 180);
    msg[K.BG_TIMEOUT_MIN] = Math.max(5, parseInt(config.bgTimeoutMin, 10) || 20);
    msg[K.SHOW_SECONDS] = (config.showSeconds === false ||
                           config.showSeconds === 0) ? 0 : 1;
    sendMsg(msg);
  }

  // ── Scheduling (supercgm: align to sensor timestamp + 30s, server time) ─
  function scheduleNextBG(delayMs) {
    if (fetchTimer) { clearTimeout(fetchTimer); fetchTimer = null; }
    var ms = Math.max(15000, parseInt(delayMs, 10) || 0);
    console.log('[CasioTime2] next BG fetch in ' + Math.round(ms / 1000) + 's');
    fetchTimer = setTimeout(fetchBG, ms);
  }

  function planNextBGFetch(lastBgTsSec, serverNowSec) {
    if (!config.bgUrl) return;
    var manualMin = Math.max(1, parseInt(config.bgManualIntervalMin || config.bgFetchIntervalMin || 5, 10));
    var manualMs = manualMin * 60 * 1000;
    if (config.syncBgWithInterval === false) {
      scheduleNextBG(manualMs);
      return;
    }
    var sensorSec = Math.max(1, parseInt(config.bgFetchIntervalMin || 5, 10)) * 60;
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
  function fetchBG() {
    if (!config.bgUrl) {
      sendStatus(BG_STATUS.NO_DATA);
      return;
    }
    var baseUrl = String(config.bgUrl).replace(/\/$/, '');
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
          planNextBGFetch(null);
          return;
        }
        var json = JSON.parse(this.responseText);
        var sgv = null, ts = null, trend = null, delta = NaN, serverNow = null;
        if (json && json.bgs && json.bgs.length > 0) {
          var b = json.bgs[0];
          sgv = encodeSgv(b.sgv || b.glucose || b.value);
          ts = parseInt(b.datetime || b.date || b.mills || b.timestamp || 0, 10);
          trend = b.direction || b.trend || null;
          delta = encodeDelta(b.bgdelta);
          if (json.status && json.status[0]) {
            serverNow = parseInt(json.status[0].now || 0, 10);
          }
        } else if (Object.prototype.toString.call(json) === '[object Array]' && json.length > 0) {
          sgv = encodeSgv(json[0].sgv || json[0].glucose || json[0].value);
          ts = parseInt(json[0].datetime || json[0].date || json[0].mills || json[0].timestamp || 0, 10);
          trend = json[0].direction || json[0].trend || null;
          delta = encodeDelta(json[0].bgdelta);
        }
        if (ts && ts > 1000000000000) ts = Math.floor(ts / 1000);
        if (serverNow && serverNow > 1000000000000) serverNow = Math.floor(serverNow / 1000);

        if (isFinite(sgv) && sgv > 0) {
          var nowSec = (serverNow && serverNow > 0) ? serverNow : Math.floor(Date.now() / 1000);
          var bgTs = ts || nowSec;
          var ageSec = nowSec - bgTs;
          var timeoutSec = Math.max(5, parseInt(config.bgTimeoutMin, 10) || 20) * 60;
          var status = (ageSec > timeoutSec) ? BG_STATUS.OLD : BG_STATUS.OK;
          var extras = {};
          extras[K.BG_SGV] = sgv;
          extras[K.BG_TIMESTAMP] = bgTs;
          extras[K.BG_TREND] = trendArrow(trend);
          extras[K.BG_DELTA] = isFinite(delta) ? delta : -9999;
          extras[K.BG_UNIT] = config.bgUnit === 'mmol' ? 1 : 0;
          sendStatus(status, extras);
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
  function loadConfig() {
    try {
      var saved = localStorage.getItem('casiotime2_config');
      if (saved) {
        var cfg = JSON.parse(saved);
        if (cfg && typeof cfg === 'object') {
          for (var k in config) {
            if (cfg[k] !== undefined) config[k] = cfg[k];
          }
        }
      }
    } catch (e) {}
  }

  // Accept both supercgm-style (bgUrl/authToken/bgUnit) and casiocgm-style
  // (nsUrl/nsToken/nsUnits) config page payloads.
  function applyWebConfig(cfg) {
    if (cfg.bgUrl !== undefined) config.bgUrl = cfg.bgUrl || null;
    else if (cfg.nsUrl !== undefined) config.bgUrl = cfg.nsUrl || null;
    if (cfg.authToken !== undefined) config.authToken = cfg.authToken || null;
    else if (cfg.nsToken !== undefined) config.authToken = cfg.nsToken || null;
    if (cfg.bgUnit !== undefined) config.bgUnit = cfg.bgUnit === 'mmol' ? 'mmol' : 'mgdl';
    else if (cfg.nsUnits !== undefined) config.bgUnit = parseInt(cfg.nsUnits, 10) === 1 ? 'mmol' : 'mgdl';
    if (cfg.low !== undefined) config.low = cfg.low;
    else if (cfg.nsLow !== undefined) config.low = cfg.nsLow;
    if (cfg.high !== undefined) config.high = cfg.high;
    else if (cfg.nsHigh !== undefined) config.high = cfg.nsHigh;
    if (cfg.bgTimeoutMin !== undefined) config.bgTimeoutMin = cfg.bgTimeoutMin;
    else if (cfg.nsStaleMin !== undefined) config.bgTimeoutMin = cfg.nsStaleMin;
    if (cfg.bgFetchIntervalMin !== undefined) config.bgFetchIntervalMin = cfg.bgFetchIntervalMin;
    if (cfg.bgManualIntervalMin !== undefined) config.bgManualIntervalMin = cfg.bgManualIntervalMin;
    if (cfg.syncBgWithInterval !== undefined) {
      config.syncBgWithInterval = cfg.syncBgWithInterval !== false &&
                                  cfg.syncBgWithInterval !== 0;
    }
    if (cfg.showSeconds !== undefined) {
      config.showSeconds = cfg.showSeconds !== false &&
                           cfg.showSeconds !== 0 && cfg.showSeconds !== '0';
    }
  }

  // ── Pebble events ───────────────────────────────────────────────────────
  Pebble.addEventListener('ready', function() {
    console.log('[CasioTime2] JS ready');
    loadConfig();
    sendConfigToWatch();
    fetchBG();
  });

  Pebble.addEventListener('appmessage', function(e) {
    if (e && e.payload &&
        (e.payload[K.REQUEST_BG] !== undefined ||
         e.payload.REQUEST_BG !== undefined)) {
      fetchBG();
    }
  });

  Pebble.addEventListener('showConfiguration', function() {
    var stored = localStorage.getItem('casiotime2_config') || '{}';
    var url = 'http://casiocgm.aize-it.de/config/?config=' +
              encodeURIComponent(stored);
    Pebble.openURL(url);
  });

  Pebble.addEventListener('webviewclosed', function(e) {
    if (!e || !e.response || e.response === 'CANCELLED') return;
    try {
      var cfg = JSON.parse(decodeURIComponent(e.response));
      applyWebConfig(cfg);
      try { localStorage.setItem('casiotime2_config', JSON.stringify(config)); } catch (err) {}
      sendConfigToWatch();
      fetchBG();
    } catch (ex) {
      console.log('[CasioTime2] config parse error: ' + ex);
    }
  });
})();
