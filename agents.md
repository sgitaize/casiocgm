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
├── config/
│   └── index.html        – Hosted web config (EN/DE, live preview, all settings)
├── deploy_ftp.sh         – FTP upload script for config/ → casiocgm.aize-it.de/config/
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
| KEY_NS_STALE_MIN    | 5   | config→watch  | int    | Minutes until "No Conn"        |
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
| KEY_CGM_VALUE       | 50  | JS→watch      | string | Glucose value as string        |
| KEY_CGM_DELTA       | 51  | JS→watch      | string | Delta string e.g. "+3"        |
| KEY_CGM_TREND       | 52  | JS→watch      | string | UTF-8 arrow (↑ ↗ → ↘ ↓)      |
| KEY_CGM_AGE         | 53  | JS→watch      | int    | Minutes since last reading     |
| KEY_STEPS           | 54  | JS→watch      | int    | Step count today               |
| KEY_HR              | 55  | JS→watch      | int    | Heart rate BPM                 |
| KEY_WEATHER_TEMP    | 56  | JS→watch      | int    | Temperature (C or F)           |
| KEY_WEATHER_ICON    | 57  | JS→watch      | string | UTF-8 weather icon             |
| KEY_BATT_PCT        | 58  | JS→watch      | int    | Battery percent                |

---

## Watch Layout (144×168px, Basalt/Aplite)

```
┌──────────────────────────────────────┐
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ (red stripe)   │
│ QUARTZ              TIME 2           │  ← s_label_tl / s_label_tr
│ ◄LIGHT   pebble    UP►              │
│                     ENTER►           │
│ ┌────────┐ ┌──────────────┐         │
│ │ 22-06  │ │   CGM/COMP   │  ←trend │  ← date + complication + trend arrow
│ └────────┘ └──────────────┘    ↑    │
│                                      │
│      08:08          42sec  ↗         │  ← large time + seconds + trend arrow
│                           +2         │  ← delta
│                                      │
│ BAT ████████░░  ● ● ● ● ● ● ●       │  ← battery bar + dots
│      S  M  T  W  T  F  S            │  ← weekday strip
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ (red stripe)   │
│ CGM Active              DOWN►        │  ← CGM status / label
│ ▓▓▓▓  CGM Enabled  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓ │  ← bottom banner bar
└──────────────────────────────────────┘
```

---

## Complications

The 5-digit area (top-right) shows one of:

| Index | Name         | Source              | Notes                         |
|-------|--------------|---------------------|-------------------------------|
| 0     | CGM          | Nightscout API      | Shows "----" when stale       |
| 1     | Steps        | HealthService       | Day total                     |
| 2     | Heart Rate   | HealthService peek  | BPM                           |
| 3     | Weather      | Open-Meteo API      | Temp + icon                   |
| 4     | Battery      | battery_state_service_peek | Percent              |
| 5     | Alternate Date | localtime           | Alternate date format        |

**Shake-to-cycle**: `accel_tap_handler` cycles through all 6 slots, shows for 5s, then reverts to configured primary. Timer cancels on each shake to reset the 5s window.

---

## CGM Status Logic

In `cgm_status_label()`:
- `s_ns_url` empty → "NO URL"
- `s_cgm_age > s_ns_stale_min` → "CGM No Conn"
- Otherwise → "CGM Active"

`s_cgm_age` is set from `KEY_CGM_AGE` (minutes since reading timestamp), calculated in `app.js` as `(Date.now() - entry.date) / 60000`.

---

## Trend Arrow

Displayed where the heart icon was in the original Casio design (bottom-right area of time row). Color follows CGM range (OK/High/Low colors). Unicode arrows: ⇑ ↑ ↗ → ↘ ↓ ⇓

Mapping in `app.js` `trendArrow()` function — maps Nightscout `direction` strings.

---

## Nightscout API

- Endpoint: `GET {nsUrl}/api/v1/entries/sgv.json?count=2[&token={nsToken}]`
- Fetch interval: 5 minutes (phone side)
- Fields used: `sgv`, `glucose`, `bgdelta` (fallback: delta between entries), `direction`, `date`
- mmol/L conversion: `sgv / 18.0`

---

## Web Config

- URL: `http://casiocgm.aize-it.de/config/`
- Loaded with: `?config=<URLencoded JSON of current config>`
- Returns via: `pebblejs://close#{URLencoded JSON}`
- Languages: English (default), German (toggle button top-right)
- Sections: Nightscout, Complication, Labels, Date&Time, Colors
- Live preview: Casio-style watch mockup updates in real-time

---

## FTP Deploy

Script: `deploy_ftp.sh`
- Uploads `config/` directory to `casiocgm.aize-it.de/config/` via FTP
- Host: `hosting139074.a2e5b.netcup.net`
- User: `casiopebble`
- Run: `bash deploy_ftp.sh`

---

## Build

```bash
pebble build          # builds all platforms
pebble install        # install to paired watch
pebble logs           # tail watch logs
```

Requires Pebble SDK 3 (`pebble` CLI).

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
