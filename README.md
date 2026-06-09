# CasioCGM

A Pebble watchface styled after the classic **Casio G-Shock digital watch**, with real-time **Nightscout CGM** blood glucose integration.

![CasioCGM Screenshot](screenshot.png)

---

## Features

- **Casio G-Shock retro design** — black background, thin red separator bands with semicircular inner corners, thick LCD frame
- **7/14-segment LCD digits** — LECO fonts throughout (date, complication, time) with authentic ghost-segment effect
- **Nightscout CGM** — live blood glucose via `/pebble` endpoint, trend arrow, delta, stale detection
- **CGM complication** — 4-digit value right-aligned + separate trend arrow at far right
- **Configurable complication** — CGM, step count, heart rate, weather temperature, battery %, alternate date
- **All complications right-aligned** — authentic LCD readout look
- **Shake to cycle** — shake to preview all complications for 5 seconds; configurable 2nd-row data on shake
- **Weekday strip** — today highlighted; language selectable (English S M T W T F S or German S M D M D F S)
- **Seconds toggle** — seconds display can be hidden via config (non-emery platforms)
- **Accurate battery** — live battery via `battery_state_service_subscribe`, no polling delay
- **Full color customization** — background, foreground, accent, three CGM range colors (Pebble Time / Time 2)
- **E-Paper bottom banner** — configurable label text in accent color
- **Bilingual config** — web config page in English and German
- **Multi-platform** — aplite, basalt, chalk, diorite, emery

---

## Layout (emery / Pebble Time 2)

```
┌──────────────────────────────┐  ← black area: QUARTZ (left)  TIME 2 (right)
╔══════════════════════════════╗  ← red band (thin, bottom corners rounded)
║  ◄LIGHT    pebble    UP►     ║
╚══════════════════════════════╝
┌──────────────────────────────┐  ← outer LCD frame (thick double border)
│ ┌────────────────────────────┤
│ │  09-06      ┌──────────┐  │  ← date (LECO, centered) | CGM comp box
│ │             │  126  ↑  │  │    4-digit value + trend arrow
│ │             └──────────┘  │
│ │                           │
│ │       06:52               │  ← time LECO_60 (emery full width)
│ │                           │
│ ├───────────────────────────┤  ← separator line
│ │  ████░░  S M D M D F [S] │  ← battery + weekday (DE shown)
│ └────────────────────────────┘
│  CGM  Active          ↑  DOWN│  ← CGM status area
└──────────────────────────────┘
╔══════════════════════════════╗  ← red band (thin, top corners rounded)
╚══════════════════════════════╝
[ E-PAPER DISPLAY              ]  ← accent banner
```

---

## Configuration

Tap the settings gear in the Pebble app, or open directly:  
**[casiocgm.aize-it.de/config/](http://casiocgm.aize-it.de/config/)**

### Nightscout / CGM
| Setting | Description |
|---|---|
| Nightscout URL | Full URL, e.g. `https://mysite.ns.10be.de` — `/pebble` is appended automatically |
| API Token | Optional — only needed if your site requires authentication |
| Units | mg/dL or mmol/L |
| High threshold | Values above this shown in CGM High color |
| Low threshold | Values below this shown in CGM Low color |
| Stale after | Minutes before data is considered outdated |

The watchface fetches `<nsUrl>/pebble` and reads:
```json
{"bgs":[{"sgv":"108","direction":"SingleUp","bgdelta":6,"datetime":1780980660000}]}
```

### Complication (top-right box)
| Slot | Display |
|---|---|
| CGM / Blood Glucose | 4-digit value + trend arrow |
| Step Count | today's steps |
| Heart Rate | BPM |
| Weather Temp | degrees (no unit — LECO font) |
| Battery % | percentage (no unit — LECO font) |
| Alternate Date | DD-MM or MM-DD |

Shake the watch to cycle through all complications for 5 seconds.

### Date & Time
| Setting | Description |
|---|---|
| Date format | DD-MM or MM-DD |
| First weekday | Sunday or Monday |
| Weekday language | English (S M T W T F S) or German (S M D M D F S) |
| Seconds display | Show or hide seconds (non-emery platforms only) |

### Labels
- **Top left / right** — QUARTZ and TIME 2 label row
- **Bottom banner** — text shown in the E-Paper accent strip

### Colors (Pebble Time / Time 2 only)
Background, foreground, accent, and three CGM range colors (in range / high / low).

---

## Building from source

```bash
# Requires Pebble SDK 4.9.169
pebble build
# Output: build/casiocgm.pbw
```

Clean build:
```bash
pebble clean && pebble build
```

---

## Deploy config page

The web config is a single `config/index.html`. Deploy via FTP:

```bash
cp .ftpconfig.example .ftpconfig
# Edit .ftpconfig with your credentials
./deploy_ftp.sh
```

> `.ftpconfig` is in `.gitignore` — never commit credentials.

---

## Support / Donate

If you find CasioCGM useful, a small donation is always appreciated!

[![Donate via PayPal](https://img.shields.io/badge/Donate-PayPal-blue?logo=paypal)](https://paypal.me/simongutjahr)

---

## License

MIT — do whatever you want, attribution appreciated.
