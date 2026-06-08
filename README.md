# CasioCGM

A Pebble watchface styled after the classic **Casio G-Shock digital watch**, with real-time **Nightscout CGM** blood glucose integration.

![CasioCGM Screenshot](screenshot.png)

---

## Features

- **Casio G-Shock retro design** — black background, red accent stripes, LCD-style 7-segment digits with ghost segments
- **Nightscout CGM** — live blood glucose value, trend arrow, delta, age-based stale detection
- **Configurable complication** — CGM, step count, heart rate, weather temperature, battery %, or alternate date
- **Shake to cycle** — shake the watch to preview all complications for 5 seconds
- **Double-border LCD rect** — retro double-frame display area with rounded corners
- **Weekday strip** — today highlighted, others greyed out
- **Full color customization** — background, foreground, accent, CGM range colors (Pebble Time / Time 2)
- **E-Paper bottom banner** — configurable label text in accent color
- **Bilingual config** — web config page in English and German
- **Multi-platform** — aplite, basalt, chalk, diorite, emery

---

## Installation

### From Pebble App Store
> Coming soon

### Manual (sideload)
1. Download the latest `casiocgm.pbw` from [Releases](../../releases)
2. Open the file on your phone to install via the Pebble app

---

## Configuration

Tap the settings gear in the Pebble app to open the web config.

Or open directly: **[casiocgm.aize-it.de/config/](http://casiocgm.aize-it.de/config/)**

### Nightscout / CGM
| Setting | Description |
|---|---|
| Nightscout URL | Full URL, e.g. `https://mysite.ns.10be.de` |
| API Token | Optional — only needed if your site requires authentication |
| Units | mg/dL or mmol/L |
| High threshold | Values above this are shown in CGM High color |
| Low threshold | Values below this are shown in CGM Low color |
| Stale after | Minutes before data is considered outdated |

### Complication (5-digit area)
The top-right LCD box shows one of:
- **CGM** — live blood glucose from Nightscout
- **Steps** — today's step count (requires health permission)
- **Heart Rate** — current BPM (Pebble Time 2 / HR models)
- **Weather Temp** — via Open-Meteo (no API key needed)
- **Battery %** — watch battery level
- **Alternate Date** — second date format

Shake the watch to cycle through all complications for 5 seconds.

### Labels
- **Top left / right** — the small labels in the QUARTZ / TIME 2 row
- **Bottom banner** — text shown in the E-Paper accent strip at the bottom

### Colors (Pebble Time / Time 2 only)
All colors are configurable: background, foreground/text, accent (labels, banner), and three CGM range colors (in range, high, low).

---

## Building from source

```bash
# Install Pebble SDK 4.9
pebble build
# Output: build/casiocgm.pbw
```

Requires [Pebble SDK 4.9.169](https://developer.rebble.io/developer.pebble.com/sdk/index.html).

---

## Deploy config page

The web config is a single `config/index.html` file. Deploy via FTP:

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
