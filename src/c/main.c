// CasioTime2 — Casio-style watchface for Pebble Time 2 (emery)
// Layout modeled after a Casio HR-monitor quartz watch; the 5-digit
// complication shows Nightscout CGM data (logic mirrors supercgm).
#include <pebble.h>

// ── AppMessage keys: MESSAGE_KEY_* generated from package.json messageKeys
//   BG_SGV / BG_DELTA / BG_THRESH_*: mg/dL int, or mmol×10 when BG_UNIT=1
//   BG_TIMESTAMP: Unix seconds of the reading · BG_DELTA -9999 = unknown
//   BG_TREND: "^^" "^" "^>" "-" ">v" "v" "vv"
//   BG_FETCH_INTERVAL_MIN: sensor interval; reading is stale after 2× (min 5)
//   SHOW_SECONDS: 1=small seconds next to HH:MM (default), 0=off

// BG status semantics (mirrors Nightscout-supercgm)
enum { BG_OK = 0, BG_NO_DATA = 1, BG_NO_CONN = 2, BG_OLD = 3 };

// ── State ────────────────────────────────────────────────────────────────
static Window *s_window;
static Layer  *s_canvas;

static int    s_bg_status   = -1;     // -1 = nothing received yet
static int    s_bg_sgv      = -1;     // -1 = no valid reading
static time_t s_bg_ts       = 0;
static char   s_bg_trend[4] = "";
static int    s_bg_delta    = -9999;
static int    s_bg_unit     = 0;
static int    s_th_low      = 80;
static int    s_th_high     = 180;
static int    s_bg_interval = 5;      // sensor interval (min)
static bool   s_vibe_low    = false;
static bool   s_vibe_high   = false;
static time_t s_last_vibe_low_ts  = 0;
static time_t s_last_vibe_high_ts = 0;

static int    s_batt_pct    = 100;
static int    s_show_seconds = 1;

static GFont s_f_time48, s_f_date20, s_f_comp22;      // real segments (Bold)
static GFont s_f_gh48, s_f_gh20, s_f_gh22;            // ghost segments (Regular)

// ── Palette (Casio LCD look) ─────────────────────────────────────────────
#define COL_NAVY   GColorFromRGB(0x00, 0x00, 0x55)
#define COL_GHOST  GColorFromRGB(0xAA, 0xAA, 0xFF)
#define COL_RED    GColorFromRGB(0xFF, 0x00, 0x00)
#define COL_AMBER  GColorFromRGB(0xAA, 0x55, 0x00)
#define COL_BLUE   GColorFromRGB(0x00, 0x55, 0xFF)
#define COL_YELLOW GColorFromRGB(0xFF, 0xFF, 0x00)
#define COL_GREY   GColorDarkGray

// ── CGM helpers (logic mirrors Nightscout-supercgm) ──────────────────────
static int bg_age_min(void) {
  if (s_bg_ts <= 0) return 9999;
  time_t now = time(NULL);
  return (now > s_bg_ts) ? (int)((now - s_bg_ts) / 60) : 0;
}

// supercgm: a reading is stale after 2× the sensor interval (at least 5 min)
static bool bg_is_stale(void) {
  int stale_sec = s_bg_interval * 2 * 60;
  if (stale_sec < 300) stale_sec = 300;
  return s_bg_status == BG_OLD || (int)(time(NULL) - s_bg_ts) > stale_sec;
}

static bool bg_is_fresh(void) {
  return s_bg_status == BG_OK && s_bg_sgv >= 0 && !bg_is_stale();
}

// 5-glyph string for the comp box
static void bg_comp_str(char *buf, size_t len) {
  if (s_bg_status < 0)              { snprintf(buf, len, "-----"); return; }
  if (s_bg_status == BG_NO_CONN)    { snprintf(buf, len, "NOCON"); return; }
  if (s_bg_status == BG_NO_DATA ||
      s_bg_sgv < 0)                 { snprintf(buf, len, "NO-BG"); return; }
  if (bg_is_stale())                { snprintf(buf, len, "OLDBG"); return; }
  if (s_bg_unit == 1) snprintf(buf, len, "%d,%d", s_bg_sgv / 10, s_bg_sgv % 10);
  else                snprintf(buf, len, "%d", s_bg_sgv);
}

// Threshold colors on fresh readings, grey on stale/error (supercgm)
static GColor bg_range_color(void) {
  if (s_bg_status < 0)         return COL_NAVY;
  if (!bg_is_fresh())          return COL_GREY;
  if (s_bg_sgv < s_th_low)     return COL_RED;
  if (s_bg_sgv > s_th_high)    return COL_AMBER;
  return COL_NAVY;
}

// Vibrate on threshold breach, 10-minute cooldown per direction (supercgm)
static void check_bg_alerts(void) {
  if (s_bg_status != BG_OK || s_bg_sgv < 0) return;
  time_t now = time(NULL);
  const int cooldown = 600;
  if (s_vibe_low && s_bg_sgv < s_th_low) {
    if ((now - s_last_vibe_low_ts) >= cooldown) {
      s_last_vibe_low_ts = now;
      static const uint32_t segs[] = {200, 100, 200, 100, 200};
      VibePattern pat = { .durations = segs, .num_segments = ARRAY_LENGTH(segs) };
      vibes_enqueue_custom_pattern(pat);
    }
  } else if (s_vibe_high && s_bg_sgv > s_th_high) {
    if ((now - s_last_vibe_high_ts) >= cooldown) {
      s_last_vibe_high_ts = now;
      static const uint32_t segs[] = {200, 100, 200};
      VibePattern pat = { .durations = segs, .num_segments = ARRAY_LENGTH(segs) };
      vibes_enqueue_custom_pattern(pat);
    }
  }
}

// Trend string → single char code for the arrow renderer
static char trend_code(void) {
  if (strcmp(s_bg_trend, "^^") == 0) return 'U';
  if (strcmp(s_bg_trend, "^")  == 0) return 'u';
  if (strcmp(s_bg_trend, "^>") == 0) return 'r';
  if (strcmp(s_bg_trend, ">v") == 0) return 'f';
  if (strcmp(s_bg_trend, "v")  == 0) return 'd';
  if (strcmp(s_bg_trend, "vv") == 0) return 'D';
  return '-';
}

// ── Small drawing helpers ────────────────────────────────────────────────
static void draw_tri_arrow(GContext *ctx, GPoint tip, int half_h, bool right,
                           GColor col) {
  int base_x = tip.x + (right ? -(half_h * 2) : (half_h * 2));
  GPoint a = GPoint(base_x, tip.y - half_h);
  GPoint b = GPoint(base_x, tip.y + half_h);
  graphics_context_set_stroke_color(ctx, col);
  graphics_draw_line(ctx, tip, a);
  graphics_draw_line(ctx, tip, b);
  graphics_draw_line(ctx, a, b);
}

static void draw_trend_arrow(GContext *ctx, char t, GRect r, GColor col) {
  int cx = r.origin.x + r.size.w / 2;
  int cy = r.origin.y + r.size.h / 2;
  int ah = r.size.h / 4;
  if (ah < 2) ah = 2;
  if (ah > 7) ah = 7;
  graphics_context_set_stroke_color(ctx, col);
  switch (t) {
    case 'U':
      graphics_draw_line(ctx, GPoint(cx, cy-ah-1), GPoint(cx-ah, cy-1));
      graphics_draw_line(ctx, GPoint(cx, cy-ah-1), GPoint(cx+ah, cy-1));
      graphics_draw_line(ctx, GPoint(cx, cy+1),    GPoint(cx-ah, cy+ah+1));
      graphics_draw_line(ctx, GPoint(cx, cy+1),    GPoint(cx+ah, cy+ah+1));
      break;
    case 'u':
      graphics_draw_line(ctx, GPoint(cx, cy+ah),   GPoint(cx,    cy-ah+1));
      graphics_draw_line(ctx, GPoint(cx, cy-ah),   GPoint(cx-ah, cy));
      graphics_draw_line(ctx, GPoint(cx, cy-ah),   GPoint(cx+ah, cy));
      break;
    case 'r':
      graphics_draw_line(ctx, GPoint(cx-ah, cy+ah), GPoint(cx+ah, cy-ah));
      graphics_draw_line(ctx, GPoint(cx+ah, cy-ah), GPoint(cx,    cy-ah));
      graphics_draw_line(ctx, GPoint(cx+ah, cy-ah), GPoint(cx+ah, cy));
      break;
    case 'f':
      graphics_draw_line(ctx, GPoint(cx-ah, cy-ah), GPoint(cx+ah, cy+ah));
      graphics_draw_line(ctx, GPoint(cx+ah, cy+ah), GPoint(cx,    cy+ah));
      graphics_draw_line(ctx, GPoint(cx+ah, cy+ah), GPoint(cx+ah, cy));
      break;
    case 'd':
      graphics_draw_line(ctx, GPoint(cx, cy-ah),   GPoint(cx,    cy+ah-1));
      graphics_draw_line(ctx, GPoint(cx, cy+ah),   GPoint(cx-ah, cy));
      graphics_draw_line(ctx, GPoint(cx, cy+ah),   GPoint(cx+ah, cy));
      break;
    case 'D':
      graphics_draw_line(ctx, GPoint(cx, cy-1),    GPoint(cx-ah, cy-ah-1));
      graphics_draw_line(ctx, GPoint(cx, cy-1),    GPoint(cx+ah, cy-ah-1));
      graphics_draw_line(ctx, GPoint(cx, cy+ah+1), GPoint(cx-ah, cy+1));
      graphics_draw_line(ctx, GPoint(cx, cy+ah+1), GPoint(cx+ah, cy+1));
      break;
    default:
      graphics_draw_line(ctx, GPoint(cx-ah, cy), GPoint(cx+ah, cy));
      graphics_draw_line(ctx, GPoint(cx+ah, cy), GPoint(cx+ah-ah/2, cy-ah/2));
      graphics_draw_line(ctx, GPoint(cx+ah, cy), GPoint(cx+ah-ah/2, cy+ah/2));
      break;
  }
}

// Ghost + real segment text in one call
static void lcd_text(GContext *ctx, const char *ghost, const char *real,
                     GFont f_ghost, GFont f_real, GRect r,
                     GColor gc, GColor rc, GTextAlignment align) {
  graphics_context_set_text_color(ctx, gc);
  graphics_draw_text(ctx, ghost, f_ghost, r,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
  graphics_context_set_text_color(ctx, rc);
  graphics_draw_text(ctx, real, f_real, r,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

// ── DRAW ─────────────────────────────────────────────────────────────────
// Reference layout 144×168 (scaled to emery 200×228):
//   0..14   black strip: QUARTZ | TIME 2 ("2" blue)
//  14..152  red ring (band top 14..19, thin side rails, band bottom 147..152)
//  19..31   ◄LIGHT · pebble · UP► / ENTER►
//  31..130  LCD: white frame, white panel
//    37..61   date (DSEG 20) | comp box "88,888" (DSEG 22)
//    62..106  HH:MM (DSEG 48) + small seconds (DSEG 22) right
//   108..127  info strip: BAT bar | 7 day squares
//  130..137  weekday letters S M T W T F S (white on black)
//  137..147  CGM status (blue, 2 lines) + trend icon + DOWN►
//  152..168  yellow "E-PAPER DISPLAY"
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w;
  int H = bounds.size.h;
#define SX(x) ((x) * W / 144)
#define SY(y) ((y) * H / 168)

  GFont f_tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_lbl  = fonts_get_system_font(FONT_KEY_GOTHIC_09);

  time_t now_t = time(NULL);
  struct tm *tnow = localtime(&now_t);

  // Y anchors
  int y_strip   = SY(14);   // top black strip ends
  int y_band1   = SY(19);   // top red band ends
  int y_lcd     = SY(31);   // LCD frame top
  int y_dr      = SY(37);   // date/comp row top
  int y_time    = SY(62);   // time row top
  int y_info    = SY(108);  // info strip separator
  int y_in_end  = SY(127);  // LCD panel bottom (inside frame)
  int y_lcd_end = SY(130);  // LCD frame bottom
  int y_wd_end  = SY(138);  // weekday letters end
  int y_band2   = SY(147);  // bottom red band top
  int y_ban     = SY(152);  // yellow banner top

  // X anchors
  int lx = SX(8),  lw = W - SX(16);       // LCD frame
  int ix = lx + SX(3), iw = lw - SX(6);   // LCD panel (inside white frame)
  int x_l = ix + SX(4);                   // content left
  int x_r = ix + iw - SX(4);              // content right

  // ── 1. Black case ─────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── 2. Top strip: QUARTZ | TIME 2 ────────────────────────────────────
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "QUARTZ", f_tiny,
                     GRect(SX(6), SY(0), SX(70), y_strip),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, "TIME", f_tiny,
                     GRect(W - SX(58), SY(0), SX(36), y_strip),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  graphics_context_set_text_color(ctx, COL_BLUE);
  graphics_draw_text(ctx, "2", f_tiny,
                     GRect(W - SX(20), SY(0), SX(14), y_strip),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // ── 3. Red ring ───────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, COL_RED);
  graphics_fill_rect(ctx, GRect(0, y_strip, W, y_ban - y_strip),
                     SX(10), GCornersAll);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(SX(2), y_band1, W - SX(4), y_band2 - y_band1),
                     SX(8), GCornersAll);

  // ── 4. Button labels ─────────────────────────────────────────────────
  {
    int bl_y = y_band1, bl_h = y_lcd - y_band1;
    int row_h = bl_h / 2;
    graphics_context_set_text_color(ctx, GColorWhite);
    draw_tri_arrow(ctx, GPoint(SX(7), bl_y + bl_h/2), 2, false, GColorWhite);
    graphics_draw_text(ctx, "LIGHT", f_lbl,
                       GRect(SX(13), bl_y + (bl_h-9)/2, SX(30), 9),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, COL_YELLOW);
    graphics_draw_text(ctx, "pebble", f_tiny,
                       GRect(SX(42), bl_y + (bl_h-16)/2, W - SX(84), 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "UP", f_lbl,
                       GRect(W - SX(40), bl_y, SX(26), row_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_tri_arrow(ctx, GPoint(W - SX(7), bl_y + row_h/2), 2, true, GColorWhite);
    graphics_draw_text(ctx, "ENTER", f_lbl,
                       GRect(W - SX(48), bl_y + row_h, SX(34), row_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_tri_arrow(ctx, GPoint(W - SX(7), bl_y + row_h + row_h/2), 2, true, GColorWhite);
  }

  // ── 5. LCD: white frame + panel ──────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(lx, y_lcd, lw, y_lcd_end - y_lcd),
                     SX(6), GCornersAll);
  // thin navy outline around the panel interior
  graphics_context_set_stroke_color(ctx, COL_NAVY);
  graphics_draw_round_rect(ctx, GRect(ix, y_lcd + SX(3), iw, y_in_end - y_lcd - SX(3)), SX(4));

  // ── 6. Date + comp box ───────────────────────────────────────────────
  int dr_h = y_time - y_dr;
  char date_str[16];
  snprintf(date_str, sizeof(date_str), "%02d-%02d",
           tnow->tm_mday, tnow->tm_mon + 1);
  lcd_text(ctx, "88-88", date_str, s_f_gh20, s_f_date20,
           GRect(x_l, y_dr + (dr_h - 16) / 2 - 2, SX(50), 18),
           COL_GHOST, COL_NAVY, GTextAlignmentLeft);

  {
    int box_x = x_l + SX(52);
    int box_w = x_r - box_x;
    graphics_context_set_stroke_color(ctx, COL_NAVY);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, GRect(box_x, y_dr, box_w, dr_h), SX(3));
    graphics_context_set_stroke_width(ctx, 1);
    char comp[16];
    bg_comp_str(comp, sizeof(comp));
    // All 5 ghost digits always visible; DSEG14 is monospaced, so the
    // right-aligned real string covers the trailing ghost cells exactly.
    GRect cr = GRect(box_x + SX(2), y_dr + (dr_h - 18) / 2 - 2,
                     box_w - SX(5), 20);
    lcd_text(ctx, "88888", comp, s_f_gh22, s_f_comp22,
             cr, COL_GHOST, bg_range_color(), GTextAlignmentRight);
  }

  // ── 7. Time + seconds ────────────────────────────────────────────────
  char time_str[8];
  if (clock_is_24h_style()) {
    snprintf(time_str, sizeof(time_str), "%02d:%02d",
             tnow->tm_hour, tnow->tm_min);
  } else {
    int hh = tnow->tm_hour % 12;
    if (hh == 0) hh = 12;
    snprintf(time_str, sizeof(time_str), "%02d:%02d", hh, tnow->tm_min);
    if (tnow->tm_hour >= 12) {
      graphics_context_set_text_color(ctx, COL_NAVY);
      graphics_draw_text(ctx, "P", f_lbl,
                         GRect(x_l, y_time - SY(2), SX(10), 10),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
  }
  {
    int t_h = y_info - y_time;
    int tdy = y_time + (t_h - 38 - 10) / 2;
    if (tdy < y_time) tdy = y_time;
    // Measured: DSEG14 mono advance 0.816em → "88:88"@38 = 132px, "88"@16 = 27px.
    if (s_show_seconds) {
      // Time gets the left region, seconds the reserved right column.
      lcd_text(ctx, "88:88", time_str, s_f_gh48, s_f_time48,
               GRect(ix + SX(1), tdy, (x_r - SX(20)) - (ix + SX(1)), y_info - tdy),
               COL_GHOST, COL_NAVY, GTextAlignmentLeft);
      char sec_str[4];
      snprintf(sec_str, sizeof(sec_str), "%02d", tnow->tm_sec);
      lcd_text(ctx, "88", sec_str, s_f_gh20, s_f_date20,
               GRect(x_r - SX(20), y_info - 22, SX(20), 20),
               COL_GHOST, COL_NAVY, GTextAlignmentRight);
    } else {
      // No seconds: center the time across the full panel width
      lcd_text(ctx, "88:88", time_str, s_f_gh48, s_f_time48,
               GRect(ix + SX(1), tdy, iw - SX(2), y_info - tdy),
               COL_GHOST, COL_NAVY, GTextAlignmentCenter);
    }
  }

  // ── 8. Info strip: BAT bar | day squares ─────────────────────────────
  graphics_context_set_stroke_color(ctx, COL_NAVY);
  graphics_draw_line(ctx, GPoint(ix + 2, y_info), GPoint(ix + iw - 2, y_info));
  {
    int info_top = y_info + SY(2);
    int info_h   = y_in_end - info_top - SY(2);
    if (info_h < 5) info_h = 5;
    graphics_context_set_text_color(ctx, COL_NAVY);
    graphics_draw_text(ctx, "BAT", f_lbl,
                       GRect(x_l, info_top + (info_h-9)/2 - 1, SX(16), 9),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    // 12-segment battery bar
    int bar_x = x_l + SX(18);
    int bar_w = SX(44);
    int bar_h = info_h - 2;
    int bar_y = info_top + (info_h - bar_h) / 2;
    graphics_context_set_stroke_color(ctx, COL_NAVY);
    graphics_draw_rect(ctx, GRect(bar_x, bar_y, bar_w, bar_h));
    int n_seg = 12;
    int lit = (s_batt_pct * n_seg + 50) / 100;
    if (lit > n_seg) lit = n_seg;
    int seg_w = (bar_w - 2 - (n_seg - 1)) / n_seg;
    if (seg_w < 1) seg_w = 1;
    for (int i = 0; i < n_seg; i++) {
      int sx0 = bar_x + 2 + i * (seg_w + 1);
      graphics_context_set_fill_color(ctx, i < lit ? COL_NAVY : COL_GHOST);
      graphics_fill_rect(ctx, GRect(sx0, bar_y + 2, seg_w, bar_h - 4),
                         0, GCornerNone);
    }
    // 7 weekday squares, Sunday first (photo layout), today filled
    int sq = 5;
    int sq_x0 = bar_x + bar_w + SX(6);
    int sq_step = (x_r - sq_x0) / 7;
    int sq_y = info_top + (info_h - sq) / 2;
    for (int i = 0; i < 7; i++) {
      int dx = sq_x0 + i * sq_step + (sq_step - sq) / 2;
      graphics_context_set_fill_color(ctx,
          (i == tnow->tm_wday) ? COL_NAVY : COL_GHOST);
      graphics_fill_rect(ctx, GRect(dx, sq_y, sq, sq), 0, GCornerNone);
    }
    // weekday letters below the LCD on the black case, same columns
    static const char *DAYS[7] = {"S","M","T","W","T","F","S"};
    graphics_context_set_text_color(ctx, GColorWhite);
    for (int i = 0; i < 7; i++) {
      int dx = sq_x0 + i * sq_step + (sq_step - SX(10)) / 2;
      graphics_draw_text(ctx, DAYS[i], f_lbl,
                         GRect(dx, y_lcd_end + (y_wd_end - y_lcd_end - 9) / 2, SX(10), 9),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }

  // ── 9. CGM status (blue) + trend icon + DOWN ─────────────────────────
  {
    int st_y = y_wd_end;
    int st_h = y_band2 - st_y;
    char l2[32];
    if (s_bg_status < 0)                 snprintf(l2, sizeof(l2), "WAITING");
    else if (s_bg_status == BG_NO_CONN)  snprintf(l2, sizeof(l2), "NO CONN");
    else if (s_bg_status == BG_NO_DATA ||
             s_bg_sgv < 0)               snprintf(l2, sizeof(l2), "NO DATA");
    else if (!bg_is_fresh()) {
      int a = bg_age_min();
      if (a < 60)        snprintf(l2, sizeof(l2), "OLD %dM", a);
      else if (a < 6000) snprintf(l2, sizeof(l2), "OLD %dH", a / 60);
      else               snprintf(l2, sizeof(l2), "OLD");
    } else {
      // fresh: delta + age, e.g. "+2 5M" / "+0,2 5M" / "+-0 5M" (supercgm)
      char dstr[16];
      int d = s_bg_delta;
      int dabs = d < 0 ? -d : d;
      if (d == -9999)          snprintf(dstr, sizeof(dstr), "--");
      else if (d == 0)         snprintf(dstr, sizeof(dstr), "+-0");
      else if (s_bg_unit == 1) snprintf(dstr, sizeof(dstr), "%c%d,%d",
                                        d < 0 ? '-' : '+', dabs / 10, dabs % 10);
      else                     snprintf(dstr, sizeof(dstr), "%+d", d);
      snprintf(l2, sizeof(l2), "%s %dM", dstr, bg_age_min());
    }
    graphics_context_set_text_color(ctx, COL_BLUE);
    graphics_draw_text(ctx, "CGM MONITOR", f_tiny,
                       GRect(SX(8), st_y - 3, SX(86), 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, l2, f_tiny,
                       GRect(SX(8), st_y + SY(7) - 2, SX(86), 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    // trend icon: yellow rounded square, arrow inside (heart position)
    {
      int hx = SX(96), hs = SY(12);
      int hy = st_y + (st_h - hs) / 2;
      graphics_context_set_fill_color(ctx,
          bg_is_fresh() ? COL_YELLOW : GColorLightGray);
      graphics_fill_rect(ctx, GRect(hx, hy, hs, hs), 2, GCornersAll);
      draw_trend_arrow(ctx, bg_is_fresh() ? trend_code() : '-',
                       GRect(hx, hy, hs, hs), GColorBlack);
    }
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "DOWN", f_lbl,
                       GRect(W - SX(44), st_y + (st_h-9)/2, SX(30), 9),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_tri_arrow(ctx, GPoint(W - SX(7), st_y + st_h/2), 2, true, GColorWhite);
  }

  // ── 10. Yellow banner ────────────────────────────────────────────────
  graphics_context_set_text_color(ctx, COL_YELLOW);
  graphics_draw_text(ctx, "E-PAPER DISPLAY", f_tiny,
                     GRect(0, y_ban + (H - y_ban - 16) / 2 - 2, W, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
#undef SX
#undef SY
}

// ── Services ─────────────────────────────────────────────────────────────
static void tick_handler(struct tm *tick_time, TimeUnits units) {
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// Seconds tick only while the seconds display is on (saves battery)
static void update_tick_subscription(void) {
  tick_timer_service_subscribe(
      s_show_seconds ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

// Phone connection lost → NOCON immediately, single buzz (supercgm)
static void connection_handler(bool connected) {
  if (connected) return;
  s_bg_status = BG_NO_CONN;
  s_bg_sgv = -1;
  s_bg_delta = -9999;
  s_bg_trend[0] = 0;
  vibes_short_pulse();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void battery_handler(BatteryChargeState state) {
  s_batt_pct = (int)state.charge_percent;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  // Config first, so alerts below use the new thresholds
  if ((t = dict_find(iter, MESSAGE_KEY_BG_UNIT)))        s_bg_unit = t->value->int32 ? 1 : 0;
  if ((t = dict_find(iter, MESSAGE_KEY_BG_THRESH_LOW)))  s_th_low  = (int)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_BG_THRESH_HIGH))) s_th_high = (int)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_BG_FETCH_INTERVAL_MIN))) {
    s_bg_interval = (int)t->value->int32;
    if (s_bg_interval < 1) s_bg_interval = 1;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_VIBE_ON_LOW)))    s_vibe_low  = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_VIBE_ON_HIGH)))   s_vibe_high = t->value->int32 != 0;

  // Reading
  if ((t = dict_find(iter, MESSAGE_KEY_BG_SGV)))       s_bg_sgv = (int)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_BG_TIMESTAMP))) s_bg_ts  = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_BG_TREND)))
    snprintf(s_bg_trend, sizeof(s_bg_trend), "%s", t->value->cstring);
  if ((t = dict_find(iter, MESSAGE_KEY_BG_DELTA)))     s_bg_delta = (int)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_BG_STATUS))) {
    s_bg_status = (int)t->value->int32;
    // NO_DATA / NO_CONN carry no valid reading; OLD keeps the stale value
    if (s_bg_status == BG_NO_DATA || s_bg_status == BG_NO_CONN) {
      s_bg_sgv = -1;
      s_bg_delta = -9999;
      s_bg_trend[0] = 0;
    }
    check_bg_alerts();
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_SECONDS))) {
    int prev = s_show_seconds;
    s_show_seconds = (int)t->value->int32 ? 1 : 0;
    if (s_show_seconds != prev) update_tick_subscription();
  }
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Window / lifecycle ───────────────────────────────────────────────────
static void window_load(Window *w) {
  s_f_time48 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_TIME38));
  s_f_date20 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_DATE16));
  s_f_comp22 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_COMP18));
  s_f_gh48   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_GHOSTT38));
  s_f_gh20   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_GHOSTD16));
  s_f_gh22   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_GHOSTC18));

  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
  fonts_unload_custom_font(s_f_time48);
  fonts_unload_custom_font(s_f_date20);
  fonts_unload_custom_font(s_f_comp22);
  fonts_unload_custom_font(s_f_gh48);
  fonts_unload_custom_font(s_f_gh20);
  fonts_unload_custom_font(s_f_gh22);
}

static void init(void) {
  s_batt_pct = (int)battery_state_service_peek().charge_percent;
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  update_tick_subscription();
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = connection_handler
  });
  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
