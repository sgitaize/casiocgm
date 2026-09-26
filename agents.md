# CasioCGM – Agent Documentation

> Read this file first. It contains everything needed to understand, build, and extend this watchface without reading all source files.

## What is this?

A Pebble watchface styled after a Casio G-Shock / digital quartz watch. It integrates Nightscout CGM data (blood glucose) as the primary complication and supports configuring colors, labels, and additional complications via a hosted web config page.

---

## File Map

```
casiocgm/
├── package.json          – Pebble SDK app manifest (UUID, capabilities, platforms)
├── wscript               – Pebble build system script
├── src/
│   ├── c/
│   │   └── main.c        – Watch-side C code (drawing, tick, health, AppMessage)
│   └── pkjs/
│       └── app.js        – Phone-side JS (Nightscout fetch, weather, config relay)
├── docs/config/
│   └── index.html        – Web config (EN/DE, live preview), GitHub Pages
└── agents.md             – This file
```

---

## Architecture

```
[Nightscout API] ──fetch──► [app.js on phone] ──AppMessage──► [main.c on watch]
[Open-Meteo API] ──fetch──►        │
                                   │
[config/index.html]  ◄──openURL── [Pebble showConfiguration event]
        │
        └──webviewclosed──► [app.js] ──AppMessage──► [main.c]
```

### Data flow
1. `app.js` runs on the phone, fetches Nightscout every 5 minutes.
2. CGM data (value, delta, trend arrow, age) sent to watch via `Pebble.sendAppMessage()`.
3. Config changes (from web page) arrive via `webviewclosed`, are forwarded to watch.
4. Watch persists config in flash via `persist_write_*()` — survives reboots.
5. Health data (steps, HR) read directly from `HealthService` on watch each minute.

---

## AppMessage Keys (main.c ↔ app.js)

Must be identical in both files.

| Key constant        | Int | Direction     | Type   | Description                    |
|---------------------|-----|---------------|--------|--------------------------------|
| KEY_NS_URL          | 0   | config→watch  | string | Nightscout base URL            |
| KEY_NS_TOKEN        | 1   | config→watch  | string | API token (optional)           |
| KEY_NS_UNITS        | 2   | config→watch  | int    | 0=mg/dL, 1=mmol/L             |
| KEY_NS_HIGH         | 3   | config→watch  | int    | High glucose threshold         |
| KEY_NS_LOW          | 4   | config→watch  | int    | Low glucose threshold          |
| KEY_NS_STALE_MIN    | 5   | config→watch  | int    | Minutes until OLDBG = 2× sensor interval, min 5 (computed by pkjs) |
| KEY_COLOR_BG        | 6   | config→watch  | int    | Background color (RGB24 int)   |
| KEY_COLOR_FG        | 7   | config→watch  | int    | Foreground/text color          |
| KEY_COLOR_ACCENT    | 8   | config→watch  | int    | Accent color (labels, dots)    |
| KEY_COLOR_CGM_OK    | 9   | config→watch  | int    | CGM in-range color             |
| KEY_COLOR_CGM_HIGH  | 10  | config→watch  | int    | CGM high color                 |
| KEY_COLOR_CGM_LOW   | 11  | config→watch  | int    | CGM low color                  |
| KEY_COMPLICATION    | 12  | config→watch  | int    | 0=CGM,1=Steps,2=HR,3=Wx,4=Bat,5=Date2 |
| KEY_LABEL_TOP_LEFT  | 13  | config→watch  | string | Top-left text (default QUARTZ) |
| KEY_LABEL_TOP_RIGHT | 14  | config→watch  | string | Top-right text (default TIME 2)|
| KEY_LABEL_BOTTOM    | 15  | config→watch  | string | Bottom banner text             |
| KEY_FIRST_WEEKDAY   | 16  | config→watch  | int    | 0=Sun, 1=Mon                  |
| KEY_DATE_FORMAT     | 17  | config→watch  | int    | 0=DD-MM, 1=MM-DD              |
| KEY_SHOW_SECONDS    | 20  | config→watch  | int    | 1=small seconds next to HH:MM (default 0) |
| KEY_CGM_VALUE       | 50  | JS→watch      | string | Glucose display string (mg/dL int or mmol "5.6") |
| KEY_CGM_DELTA       | 51  | JS→watch      | string | Delta string e.g. "+3"        |
| KEY_CGM_TREND       | 52  | JS→watch      | string | 1-char trend code (U u r - f d D), drawn graphically in C |
| KEY_CGM_AGE         | 53  | JS→watch      | int    | Minutes since reading (legacy; watch prefers CGM_TS) |
| KEY_STEPS           | 54  | JS→watch      | int    | Step count today               |
| KEY_HR              | 55  | JS→watch      | int    | Heart rate BPM                 |
| KEY_WEATHER_TEMP    | 56  | JS→watch      | int    | Temperature (C or F)           |
| KEY_WEATHER_ICON    | 57  | JS→watch      | string | UTF-8 weather icon             |
| KEY_BATT_PCT        | 58  | JS→watch      | int    | Battery percent                |
| KEY_CGM_STATUS      | 59  | JS→watch      | int    | 0=OK, 1=NO_DATA, 2=NO_CONN, 3=OLD (supercgm semantics) |
| KEY_CGM_TS          | 60  | JS→watch      | int    | Unix ts (sec) of reading — watch ages it locally |
| KEY_CGM_SGV         | 61  | JS→watch      | int    | Raw sgv in mg/dL for range/threshold comparison |
| KEY_REQUEST_BG      | 62  | watch→JS      | int    | Request immediate BG fetch (sent on BT reconnect) |

Note: `NS_HIGH` / `NS_LOW` are entered in the display unit on the config page
but **always sent to the watch in mg/dL** — the watch compares them against
`CGM_SGV` (raw mg/dL), never against the display string.

(Config keys 38/39: VIBE_ON_LOW / VIBE_ON_HIGH. Keys 18–37: shake slot, weekday language, ghost/backlight/banner
colors, ghost-8s-in-comp-box toggle (37, issue #4) etc. — see the `#define`
block at the top of `main.c`.)

---

## Watch Layout (emery 200×228, modelled on the Casio "TIME 2 / HEART RATE MONITOR" photo)

```
  0..16   QUARTZ (s_label_tl)                 TIME 2 (s_label_tr, last word blue)
 16..203  red ring (accent colour): top band, thin side rails, bottom band
 20..43   ◄LIGHT        pebble (yellow)        UP► / ENTER► (two rows)
 43..164  LCD: white outer frame (3 px), white panel
   49..75   date DSEG16 "88-88"   | comp box DSEG22 (rounded, lavender border)
   77..133  HH:MM DSEG48 centred  (seconds on: DSEG38 left + DSEG22 seconds)
            small "P" (ghost, lit for PM in 12h mode)
  135..151  BAT + 10 bars | divider | 7 day squares (today filled)
  151..164  S M T W T F S (white, dark frame band)
166..196  CGM status 2 lines (blue: status / white: delta + age)
          yellow heart with trend arrow (grey when stale/error)    DOWN►
203..228  s_label_bot in yellow (default "E-PAPER DISPLAY")
```

All geometry is in `canvas_update_proc()` (PX/PY scale from 200×228); the
config page preview (`drawPreview()` in docs/config/index.html) mirrors it
with the same pixel values — keep both in sync.

Defaults: LCD bg #FFFFFF, fg #000055, ghost #AAAAFF, status blue #55AAFF.
The old status box options (CGM_BOX_ENABLED, COLOR_CGM_BOX_BG) have no effect
since v2.2 and are hidden on the config page.

---

## Complications

The 5-digit area (top-right) shows one of:

| Index | Name         | Source              | Notes                         |
|-------|--------------|---------------------|-------------------------------|
| 0     | CGM          | Nightscout API      | Shows NOCON/NO-BG/OLDBG status text; "----" without URL |
| 1     | Steps        | HealthService       | Day total                     |
| 2     | Heart Rate   | HealthService peek  | BPM                           |
| 3     | Weather      | Open-Meteo API      | Temp + icon                   |
| 4     | Battery      | battery_state_service_peek | Percent              |
| 5     | Alternate Date | localtime           | Alternate date format        |

**Shake-to-cycle**: `accel_tap_handler` cycles through all 6 slots, shows for 5s, then reverts to configured primary. Timer cancels on each shake to reset the 5s window.

---

## CGM Status Logic (supercgm semantics)

Status enum (`CgmStatus`, mirrors Nightscout-supercgm): `OK=0, NO_DATA=1,
NO_CONN=2, OLD=3`. Set by pkjs (`CGM_STATUS`) and re-evaluated **locally on
the watch every redraw** via `cgm_status_text()`:

- `NO_CONN` → comp box "NOCON", banner "No Conn" (also set immediately by
  `connection_service` when BT drops, with a short vibe; on reconnect the
  watch sends `REQUEST_BG` to trigger an instant fetch)
- `NO_DATA` or sgv≤0 → "NO-BG" / "No BG"
- status `OLD` **or** `now - s_cgm_ts > s_ns_stale_min*60 s` → "OLDBG" / "Old BG"
  (stale = 2× sensor interval, min. 5 min — same rule as supercgm)
  (value stays valid but is marked stale; the banner shows its age)
- otherwise fresh → value + trend arrow, banner "CGM Active" + delta + age
- Colours (supercgm): fresh → low colour if sgv < low, high colour if
  sgv > high (strict), else OK colour; stale/error → dark grey.
- NO_DATA / NO_CONN (also BT disconnect) clear value, delta and trend.
- Optional vibration (VIBE_ON_LOW/HIGH): low 3 pulses, high 2 pulses,
  10-min cooldown per direction, checked when a CGM_STATUS arrives.
- Delta string: "+3" / "-0.2" / "+-0" (zero) / "--" (unknown).
- Trend: case-insensitive matching like supercgm; unknown directions flat.
- Deliberate deviations from supercgm: polling continues after HTTP errors
  (supercgm stops for good), no URL shows "----" / "NO URL".

Because the age is derived from `s_cgm_ts` (not a static age int), the face
flips to OLDBG even if the phone never sends another message.

### Fetch scheduling (pkjs)
Synced mode (default): next fetch = `reading_ts + sensor_interval + 30 s`,
using Nightscout server time (`status[0].now`) as reference to avoid phone
clock skew. If the reading is already due, poll every 15 s (supercgm).

**Learned upload lag (v2.3, battery):** many uploaders/bridges put readings
into Nightscout minutes late (measured live: 10–30 s, then 190–240 s). pkjs
learns that lag (`localStorage casiocgm_upload_lag`) and plans the fetch at
reading + interval + max(30 s, lag + 10 s). A new reading found between two
fetches gives the lag window: previous fetch ≤ 20 s ago → exact sample
(raise at once, lower via EMA); later than learned → raise to the window
middle; otherwise upper bound → min(). If the learned time is ≥ 1 min after
+30 s, +30 s is probed once per reading so a faster uploader is noticed at
once. Overdue > 2 min (sensor gap) → poll every 60 s instead of 15 s.
Simulation (`test/sync_sim2.js`): slow uploader 153 → 28 fetches/h, fast
uploader unchanged (12/h). Live check: `test/live_monitor.js <ns-url> <min>`.
Errors send `NO_CONN`/`NO_DATA` to the watch and retry after
min(fallback, 1 min) — also after HTTP errors (supercgm stops there).
Parser (supercgm): `/pebble` `bgs[0]`, plain arrays and flat objects;
sgv/delta auto-detect mmol (< 40) vs mg/dL. AppMessages go through a queue
(one in flight, 3 tries) so config + BG never collide (APP_MSG_BUSY).

---

## Trend Arrow

Displayed where the heart icon was in the original Casio design (bottom-right area of time row). Color follows CGM range (OK/High/Low colors). Unicode arrows: ⇑ ↑ ↗ → ↘ ↓ ⇓

Mapping in `app.js` `trendArrow()` function — maps Nightscout `direction` strings.

---

## Nightscout API

- Endpoint: `GET {nsUrl}/pebble[?token={nsToken}]` (`/pebble` already in the URL is respected)
- Fields used: `sgv`/`glucose`/`value`, `bgdelta`, `direction`/`trend`, `datetime`/`date`/`mills`, `status[0].now`
- mmol/L conversion: `sgv / 18.0`

---

## Web Config

- URL: `https://sgitaize.github.io/casiocgm/config/` (GitHub Pages, source
  branch `legacy-casiocgm`, folder `/docs`)
- Loaded with: `#config=<URLencoded JSON>` in the URL **fragment** (token never
  reaches the server; `?config=` still works)
- Returns via: `return_to` param if present (emulator), else
  `pebblejs://close#{URLencoded JSON}`
- Languages: English (default), German (toggle button top-right)
- Sections: Nightscout, Complication, Labels, Date&Time, Colors
- Live preview: Casio-style watch mockup updates in real-time

---

## Build

```bash
pebble build                      # SDK 4.33.1 (pebble-tool 5), target emery
pebble install --emulator emery   # 60 s rule: longer = bug in the face
pebble logs
```

### Firmware hang pitfalls (learned the hard way, v2.1)

- **Never draw DSEG text that does not fit its box with
  `GTextOverflowModeTrailingEllipsis`.** The DSEG fonts have no '…' glyph;
  firmware 4.9 froze completely (emulator unresponsive, app log stopped in
  the date draw). DSEG draws use `GTextOverflowModeFill` now, and font sizes
  are chosen so everything fits: DSEG14 advance = 0.816 em per glyph
  ("88:88"@48 ≈ 164 px, "88-88"@16 ≈ 65 px, "88:88"@38 ≈ 130 px).
- **The trailing number in a font resource NAME wins over `size`** — keep
  both in sync (FONT_DSEG_TIME48 / DATE16 / TIME38).
- Persist: config is stored in 4 keys (numeric struct, URL, token, labels),
  written only when changed — not ~35 single keys per config message.
  v1.x per-key data is migrated on first start.
- **Always cross-check with a known-working face first** (e.g. official
  pebble-examples) before blaming the infrastructure.
- SDK 4.9 emulator: sporadic "App install failed" after ~19 s for *any* app
  with resources (official watchface-tutorial: 11/15, simple-analog without
  resources: 15/15). On SDK 4.33.1 CasioCGM and the tutorial pass 10/10.
- pebble-tool output is block-buffered when redirected — set
  `PYTHONUNBUFFERED=1` in test scripts, otherwise results look like hangs.

---

## Appstore (Core Devices / repebble)

- UUID `6a8bead9-d7d1-4183-bbb6-e351208ac33f` since v2.2.2 (the old
  placeholder UUID was replaced for the store; installs of ≤2.2.1 appear as
  a different app, phone-side settings start fresh).
- `store/` holds the listing: `description.txt`, `release-notes.txt`,
  screenshots `emery_*.png` (200×228, filename must start with the platform).
- Upload: `pebble login` once (browser; on a headless server use
  `--no-open-browser` and forward the final `http://localhost:60000/?…`
  redirect URL with curl), then `store/publish.sh`, which publishes the release publicly
  (`--is-published`, Simon's decision 2026-09-26).

---

## Extension Points

To add a new complication:
1. Add a new `case` in `complication_value()` in `main.c`
2. Increment `SHAKE_CYCLE_COUNT`
3. Add fetch logic in `app.js`
4. Add option to `<select id="complication">` in `config/index.html`
5. Add to the key table in `agents.md`

To change colors for specific platforms:
- Wrap color code in `#ifdef PBL_COLOR` / `#else` blocks
- `color_from_int()` already handles B&W fallback
