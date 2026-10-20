# CasioCGM

**English** · [Deutsch](README.de.md)

A retro Casio-style LCD watchface for the **Pebble Time 2**, with live **CGM** blood glucose from Nightscout or iPump-mini.

> As a diabetic you want a cool watchface too — not just a plain number on a screen. That is why CasioCGM exists.

**Status:** in the Pebble Appstore · data from Nightscout or [iPump-mini](https://github.com/sgitaize/iPump-mini) · actively maintained

## Get it

| | |
|---|---|
| **Pebble Appstore** | [CasioCGM](https://apps.rePebble.com/58fcfcc22c37487f815520db) – install from the Pebble app on your phone |
| **Releases** | [`.pbw` on GitHub](https://github.com/sgitaize/casiocgm/releases/latest) for side-loading |
| **Settings** | gear icon in the Pebble app → [settings page](https://sgitaize.github.io/casiocgm/config/) (English / German) |

![CasioCGM Screenshot](resources/screenshots/casiocgm-v2.2.1.png)

---

## Features

### Display
- **Casio TIME 2 retro design** — black case, red ring, white LCD frame, yellow "pebble" and banner, button labels like the original
- **DSEG14 Classic LCD digits** — authentic segment font, ghost-segment effect on all digits
- **Ghost segments** — unlit LCD segments in a configurable colour (default lavender), simulating a real LCD

### Time & Date
- **Large time display** — DSEG14 48 px, centred
- **Seconds (optional)** — HH:MM 38 px plus small seconds in the bottom-right corner, like the original
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
- **Battery bar** — BAT label with 10 segments
- **Weekday row** — 7 squares (today filled) with S M T W T F S (or German S M D M D F S) below

### CGM integration
- **Two data sources:** Nightscout (`/pebble`, default) or [iPump-mini](https://github.com/sgitaize/iPump-mini) – the iPhone app that reads a MiniMed 780G directly, no internet needed. With iPump-mini, Nightscout can act as a fallback (marked with a small cloud).
- **Battery-friendly sync:** fetches right after each new sensor reading instead of polling; with iPump-mini it follows the app's wake schedule, and the watch nudges the phone if it stays quiet.
- Displays value, graphic trend arrow (9 variants), delta and age; the last value survives a restart of the watchface.
- Stale detection, colours by range (in range / high / low), optional vibration on low or high.
- Status below the LCD: **CGM Active** / **Old BG** / **No Conn** / **OPEN APP** (iPump-mini not running) / **No URL**.
- Shared CGM logic: [pebble-cgm](https://github.com/sgitaize/pebble-cgm), also used by [Nightscout-supercgm](https://github.com/sgitaize/Nightscout-supercgm) and [91 Dub CGM](https://github.com/sgitaize/dubv4cgm).

### Labels & banners
- **Top banner** (above red band) — two free-text labels (default: `CGM` left, `TIME 2` right) with configurable color
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
| Top banner labels | `#FFFFFF` | CGM / TIME 2 text color |

### Platform
Built exclusively for the **Pebble Time 2** (emery, 200×228 px) — DSEG14 Bold fonts, full-width time display, 22 px comp box.

---

## Architecture

<p align="center"><img src="docs/architecture.png" alt="Architecture: the watchface and the C part of pebble-cgm on the watch talk via AppMessage to PebbleKit JS on the phone, which fetches from Nightscout or iPump-mini and Open-Meteo; settings come from a page on GitHub Pages" width="100%"></p>

<sub>Source: <a href="docs/architecture.mmd">docs/architecture.mmd</a> (Mermaid)</sub>

**How it works**
- **The watch draws, the phone fetches.** The C code on the watch renders the face and keeps the last value; the JavaScript in the Pebble app (PebbleKit JS) talks to the internet and sends compact AppMessages.
- **pebble-cgm** handles everything CGM on both sides: choosing the source, timing fetches after each new reading, the iPump-mini wake schedule, fallback to Nightscout, a send queue with acknowledgements, and a watchdog on the watch.
- **Settings** are a static page on GitHub Pages; the configuration travels in the URL fragment, so your Nightscout token never reaches a server.
- Steps and heart rate come straight from the watch (HealthService); weather from Open-Meteo.

| Part | Where | What it does |
|---|---|---|
| Watchface | `src/c/main.c` | drawing, layout, complications, shake, persistence |
| CGM (watch) | `src/c/cgm.c` | last value, status, watchdog, wake timer (from pebble-cgm) |
| Phone script | `src/pkjs/index.js` | settings, weather, battery, messages to the watch |
| CGM (phone) | `src/pkjs/cgm.js` | Nightscout / iPump-mini, scheduling, fallback (from pebble-cgm) |
| Settings page | `docs/config/` | English / German, live preview |
| Store listing | `store/` | description, screenshots, `publish.sh` |
| Developer notes | [`agents.md`](agents.md) | message keys, layout, status logic |

## Configuration

Tap the settings gear in the Pebble app to open the config page.  
Available in **English and German** (toggle top-right).

### Data source / CGM
| Setting | Default | Description |
|---|---|---|
| Data source | Nightscout | Nightscout or iPump-mini (local app on the iPhone) |
| iPump-mini fallback | off | Use Nightscout when iPump-mini is not reachable (cloud symbol) |
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
| Seconds | off | Small seconds next to HH:MM |

### Labels
| Setting | Default | Description |
|---|---|---|
| Top left | `CGM` | Left label in the top black strip |
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
| Top banner labels | `#FFFFFF` | Color of CGM / TIME 2 text |

---

## Building from source

```bash
# Pebble SDK 4.33.1 (pebble-tool 5), target emery
pebble build
# Output: build/*.pbw
```

Clean build:
```bash
pebble clean && pebble build
```

---

## Status & roadmap

- **Live:** 2.5.1 in the Pebble Appstore (Nightscout and iPump-mini, reliability fixes from pebble-cgm 1.1.0).
- Ideas and bugs are welcome as [issues](https://github.com/sgitaize/casiocgm/issues).

---

## Support / Donate

If you find CasioCGM useful, a small donation is always appreciated!

[![Donate via PayPal](https://img.shields.io/badge/Donate-PayPal-blue?logo=paypal)](https://paypal.me/simongutjahr)

---

## Motivation & Credits

- **Why:** as a diabetic you want a cool watchface too. CasioCGM combines a retro Casio look with live Nightscout CGM data.
- **Design inspired by** [*Quartz by Dalpek*](https://apps.repebble.com/quartz-by-dalpek_31cfe29ecd814df4b5ca8bb4) — thanks for the great retro LCD design! (No code was taken; the watchface is written from scratch. The design is closely based on Quartz.)
- **CGM logic** ported from [Nightscout-supercgm](https://github.com/sgitaize/Nightscout-supercgm).
- **~95 % vibe coded** with [Claude Code](https://claude.com/claude-code).

---

## License

MIT — do whatever you want, attribution appreciated.
