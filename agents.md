# CasioTime2 – Agent Documentation

> Read this file first. Casio-style watchface for Pebble Time 2 (emery),
> rebuilt from scratch (July 2026) after the old `casiocgm` codebase hit an
> undiagnosable firmware crash. Template: Casio HR-monitor quartz watch photo.

## Layout (ref 144×168, scaled to emery 200×228)

- Top strip: QUARTZ | TIME 2 ("2" blue)
- Red rounded ring around the middle section
- ◄LIGHT · yellow "pebble" · UP► / ENTER►
- White LCD panel: date (DSEG 16) | 5-digit CGM comp box (DSEG 18, rounded
  border), HH:MM (DSEG 38) + seconds (DSEG 16), BAT 12-segment bar,
  7 weekday squares (Sunday first, today filled)
- S M T W T F S letters on the black case below the LCD
- Blue "CGM MONITOR" + status line, trend icon (yellow square, arrow),
  DOWN►
- Yellow "E-PAPER DISPLAY" banner

## CGM (ported from github.com/sgitaize/Nightscout-supercgm, v1.1)

- JS fetches `<bgUrl>/pebble` (token appended URL-encoded; `/pebble`
  already in URL is respected). Parser handles `bgs[0]`, arrays and flat
  objects (same as supercgm).
- Unit encoding: mg/dL as int, **mmol as value×10** — thresholds use the
  same encoding (supercgm: `low*10` for mmol), C compares without
  conversion. mmol renders as "5,6". Old mmol configs with mg/dL-looking
  thresholds (>30) are migrated.
- Status enum OK/NO_DATA/NO_CONN/OLD; comp box shows NOCON / NO-BG / OLDBG.
  NO_DATA/NO_CONN clear sgv/delta/trend on the watch, OLD keeps the value.
- **Staleness = 2x sensor interval (min 5 min)** in JS *and* C
  (`BG_FETCH_INTERVAL_MIN`); the watch ages the reading locally via
  BG_TIMESTAMP on every redraw. `BG_TIMEOUT_MIN` is no longer used.
- Colors: fresh → red if sgv < low, amber if sgv > high, else navy;
  stale/error → grey. Trend icon only on fresh readings.
- Delta: "+2" / "-0,3" / "+-0" (zero), "--" unknown.
- Vibration alerts (optional, `VIBE_ON_LOW/HIGH`): low 3 pulses, high 2,
  10-min cooldown per direction. Phone disconnect → NOCON + short pulse.
- Scheduling: next fetch = reading ts + sensor interval + 30 s, using
  Nightscout **server time** (`status[0].now`); min 15 s, clamp to manual
  interval. Unlike supercgm, HTTP errors keep polling.
- Deliberately NOT ported: supercgm's trend matching via `strstr` (maps
  "^>" to up and ">v" to down) — exact `strcmp` mapping kept here.
- AppMessages are queued in JS (one in flight, 3 tries) to avoid
  APP_MSG_BUSY when config and BG are sent back to back.

## Config page (GitHub Pages)

- Source: `docs/config/index.html` (Pages: branch `main`, folder `/docs`)
  → https://sgitaize.github.io/casiocgm/config/
- Current config is passed as `#cfg=<json>` in the URL **fragment** (token
  never reaches the web server); `return_to` (emulator) is parsed from the
  whole href. Payload uses supercgm field names (bgUrl, authToken, bgUnit,
  low, high, bgFetchIntervalMin, syncBgWithInterval, bgManualIntervalMin,
  vibeOnLow, vibeOnHigh, showSeconds); JS still accepts legacy nsUrl/...
- Language: German if the browser locale starts with `de`, else English.

## AppMessage keys (package.json `messageKeys`; C uses MESSAGE_KEY_*, JS names)

0 BG_STATUS · 1 BG_SGV · 2 BG_TIMESTAMP · 3 BG_TREND ("^^","^","^>","-",
">v","v","vv", "" unknown) · 4 BG_DELTA (-9999 = none) · 5 BG_UNIT ·
6/7 BG_THRESH_LOW/HIGH · 8 BG_TIMEOUT_MIN (unused) · 9 REQUEST_BG
(reserved) · 10 SHOW_SECONDS (1=default; 0 → time centered full-width,
tick drops to MINUTE_UNIT) · 11 BG_FETCH_INTERVAL_MIN · 12 VIBE_ON_LOW ·
13 VIBE_ON_HIGH. After adding keys: `pebble clean && pebble build`.

## Font gotchas (learned the hard way)

- **The trailing number in a font resource NAME wins over the `size`
  attribute** (FONT_DSEG_TIME38 → 38 px). Keep both in sync.
- DSEG14 is monospaced at **0.816 em advance — comma and dash too**.
  Width of a string = 0.816 × size × glyphs (colon: 0.2 em).
  "88:88"@38 ≈ 132 px, "88888"@18 ≈ 73 px.
- Ghost strings must match the shown string glyph-for-glyph (same count),
  otherwise ghost and real segments misalign.
- `pebble build` caches font resources; after changing package.json font
  entries do `rm -rf build`.

## Build & test

```bash
pebble build            # SDK 4.33.1 (pebble-tool 5), target emery
pebble install --emulator emery
pebble screenshot --emulator emery out.png
```

Emulator quirks in Claude Code sessions: QEMU/pypkjs die with the shell
call that spawned them — start them as persistent background tasks and
patch `$TMPDIR/pb-emulator.json` PIDs/ports. Corrupt
`qemu_spi_flash.bin` (boot logo hang) → restore from
`SDKs/4.9.169/sdk-core/pebble/emery/qemu/qemu_spi_flash.bin.bz2`.
First boot of a fresh flash takes minutes (progress bar — not a hang).
Emulator clock shows +2 h (RTC localtime interpreted as UTC) — cosmetic.

End-to-end CGM test without a real Nightscout: run a local mock server
serving the `/pebble` JSON and inject the config into pypkjs localStorage
(`dbm.dumb` file `<persist>/localstorage/<uuid>`), then reinstall.
