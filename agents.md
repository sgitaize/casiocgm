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

## CGM (ported from github.com/sgitaize/Nightscout-supercgm)

- JS fetches `<bgUrl>/pebble` (token appended URL-encoded; `/pebble`
  already in URL is respected). Parser handles `bgs[0]`, arrays and flat
  objects.
- Unit encoding: mg/dL as int, **mmol as value×10** — thresholds use the
  same encoding, so C compares without conversion. mmol renders as "5,6".
- Status enum OK/NO_DATA/NO_CONN/OLD; comp box shows NOCON / NO-BG / OLDBG.
- Watch ages the reading locally via BG_TIMESTAMP on every redraw (OLD
  appears even without new messages).
- Scheduling: next fetch = reading ts + sensor interval + 30 s, using
  Nightscout **server time** (`status[0].now`) to avoid clock skew;
  15 s fast-poll when overdue, clamp to manual interval.
- Config page: reuses `http://casiocgm.aize-it.de/config/`; JS accepts both
  supercgm-style (`bgUrl`) and casiocgm-style (`nsUrl`) payload fields.

## AppMessage keys (hardcoded in BOTH main.c and index.js — keep in sync)

0 BG_STATUS · 1 BG_SGV · 2 BG_TIMESTAMP · 3 BG_TREND ("^^","^","^>","-",
">v","v","vv") · 4 BG_DELTA (-9999 = none) · 5 BG_UNIT · 6/7 BG_THRESH_LOW/
HIGH · 8 BG_TIMEOUT_MIN · 9 REQUEST_BG (reserved) · 10 SHOW_SECONDS
(1=default; 0 → time centered full-width, tick drops to MINUTE_UNIT)

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
pebble build            # SDK 4.9, target emery
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
