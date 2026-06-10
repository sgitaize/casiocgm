# CasioCGM

A Pebble watchface styled after the classic **Casio G-Shock digital watch**, with real-time **Nightscout CGM** blood glucose integration.

![CasioCGM Screenshot](screenshot.png)

---

## Features

### Display
- **Casio G-Shock retro design** — black background, thin red tapered separator bands, thick double-bordered LCD frame with rounded corners
- **DSEG14 Classic Bold LCD digits** — authentic 7/14-segment font on emery (Pebble Time 2); LECO fallback on other color platforms. Ghost-segment effect on all digits.
- **Ghost segments** — unlit LCD segments rendered in a configurable gray before the real value, simulating a real LCD

### Time & Date
- **Large time display** — DSEG14 52 px on emery (full-width); LECO_38 + seconds column on basalt/chalk
- **Seconds column** — HH:MM + seconds (top) + AM/PM + CGM trend arrow (bottom); can be hidden via config (non-emery only)
- **Date** — DD-MM or MM-DD, centered left of the complication box, vertically aligned with it

### Complication box (top-right)
Six selectable slots — shown permanently; shake cycles to the secondary slot:

| Slot | What is shown |
|---|---|
| 0 — CGM / Blood Glucose | 4-digit value right-aligned + graphic trend arrow |
| 1 — Step Count | today's steps (5 digits) |
| 2 — Heart Rate | BPM (5 digits) |
| 3 — Weather Temp | degrees, no unit (5 digits) |
| 4 — Battery % | percentage, no unit (5 digits) |
| 5 — Alternate Date | DD-MM or MM-DD (5 digits) |

### Shake gesture
- **Shake to see 2nd data** — shake the watch to temporarily swap the complication box to a second configurable slot for 5 seconds
- Useful for a quick glance at CGM delta, steps, etc. while keeping the primary slot as your default

### Info strip
- **Battery bar** — 6-segment fill bar, live via `battery_state_service_subscribe`
- **Weekday row** — S M T W T F S (or German S M D M D F S); today highlighted with filled pill

### CGM integration
- Fetches **Nightscout `/pebble`** endpoint every 5 minutes via the Pebble phone app
- Displays: value, trend direction arrow (graphic, 9 variants), delta, age
- Stale detection — marks data as stale if older than the configured threshold
- CGM value color follows range: in-range / high / low with separate configurable colors
- Shows CGM status below LCD: **CGM Active** / **CGM Offline** / **No URL**

### Labels & banners
- **Top banner** (above red band) — two free-text labels (default: `QUARTZ` left, `TIME 2` right) with configurable color
- **"pebble" center label** — Gothic Bold, yellow by default, matching the E-Paper banner style
- **Button labels** — ◄LIGHT, UP►, SELECT►, DOWN► around the LCD frame
- **E-Paper bottom banner** — `E-PAPER DISPLAY` in yellow on color platforms; configurable label text above it

### Full color customization (Pebble Time / Time 2)
All colors configurable via the web config page:

| Color | Default | Purpose |
|---|---|---|
| Background | `#EEEEEE` | LCD inner fill |
| Foreground | `#000044` | digits, borders, labels |
| Accent | `#FFAA00` | (reserved / future) |
| CGM in range | `#38571A` | CGM value when in range |
| CGM high | `#FFAA00` | CGM value above high threshold |
| CGM low | `#AA0000` | CGM value below low threshold + offline |
| Ghost segments | `#ADADAD` | unlit LCD segment color |
| Top banner labels | `#FFFFFF` | QUARTZ / TIME 2 text color |

### Platform
Built exclusively for the **Pebble Time 2** (emery, 200×228 px) — DSEG14 Bold fonts, full-width time display, 22 px comp box.

---

## Layout (emery / Pebble Time 2)

```
┌──────────────────────────────────┐  ← black area: QUARTZ (left)  TIME 2 (right)
◆──────────────────────────────────◆  ← red tapered band
║  ◄LIGHT      pebble      UP►     ║
║                           SELECT►║
◆──────────────────────────────────◆
┌──────────────────────────────────┐  ← outer LCD frame (thick double border)
│ ┌────────────────────────────────┤
│ │  09-06        ┌────────────┐  │  ← date (DSEG14 20px) | comp box (DSEG14 22px)
│ │               │  126  ↑   │  │
│ │               └────────────┘  │
│ │                               │
│ │       06:52                   │  ← time DSEG14 52px (emery full width)
│ │                               │
│ ├───────────────────────────────┤
│ │  ████░░  S M D M D F [S]    │  ← battery bar + weekday strip (DE)
│ └────────────────────────────────┘
│  CGM Active          ↑     DOWN►│  ← CGM status + DOWN button label
└──────────────────────────────────┘
◆──────────────────────────────────◆  ← red tapered band
[ E-PAPER DISPLAY                  ]  ← yellow banner
```

---

## Configuration

Tap the settings gear in the Pebble app, or open directly:  
**[casiocgm.aize-it.de/config/](http://casiocgm.aize-it.de/config/)**

The config page is available in **English and German** (toggle top-right).

### Nightscout / CGM
| Setting | Default | Description |
|---|---|---|
| Nightscout URL | — | Full URL, e.g. `https://mysite.ns.10be.de` — `/pebble` appended automatically |
| API Token | — | Optional — only needed if your site requires authentication |
| Units | mg/dL | mg/dL or mmol/L |
| High threshold | 180 | Values above this shown in CGM High color |
| Low threshold | 70 | Values below this shown in CGM Low color |
| Stale after | 10 min | Minutes before data is considered outdated |

### Complication
| Setting | Default | Description |
|---|---|---|
| Primary complication | CGM | Slot shown permanently in the comp box |
| Shake – 2nd slot | CGM Delta | Slot shown while shaking (replaces primary for 5 s) |

### Date & Time
| Setting | Default | Description |
|---|---|---|
| Date format | DD-MM | DD-MM or MM-DD |
| First weekday | Sunday | Sunday or Monday |
| Weekday language | English | English (S M T W T F S) or German (S M D M D F S) |
| Seconds display | — | Not applicable (no seconds column on Pebble Time 2) |

### Labels
| Setting | Default | Description |
|---|---|---|
| Top left | `QUARTZ` | Left label in the top black strip |
| Top right | `TIME 2` | Right label in the top black strip |
| Bottom banner | `CGM Enabled` | Text in the E-Paper accent strip |

### Colors
| Setting | Default | Description |
|---|---|---|
| Background | `#EEEEEE` | LCD inner fill |
| Foreground | `#000044` | Digits, borders, frame |
| Accent | `#FFAA00` | (Reserved for future use) |
| CGM in range | `#38571A` | Value color when BG is in range |
| CGM high | `#FFAA00` | Value color when BG ≥ high threshold |
| CGM low | `#AA0000` | Value color when BG ≤ low threshold or stale |
| Ghost segments | `#ADADAD` | Unlit LCD segment color — lighter = more subtle |
| Top banner labels | `#FFFFFF` | Color of QUARTZ / TIME 2 text |

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
# Edit .ftpconfig with your FTP credentials
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
