#include <pebble.h>

// ── Keys ─────────────────────────────────────────────────────────────────
#define KEY_NS_URL          0
#define KEY_NS_TOKEN        1
#define KEY_NS_UNITS        2
#define KEY_NS_HIGH         3
#define KEY_NS_LOW          4
#define KEY_NS_STALE_MIN    5
#define KEY_COLOR_BG        6
#define KEY_COLOR_FG        7
#define KEY_COLOR_ACCENT    8
#define KEY_COLOR_CGM_OK    9
#define KEY_COLOR_CGM_HIGH  10
#define KEY_COLOR_CGM_LOW   11
#define KEY_COMPLICATION    12
#define KEY_LABEL_TOP_LEFT  13
#define KEY_LABEL_TOP_RIGHT 14
#define KEY_LABEL_BOTTOM    15
#define KEY_FIRST_WEEKDAY   16
#define KEY_DATE_FORMAT     17
#define KEY_SHAKE_2ND       18   // 0=CGM delta,1=steps,2=HR,3=battery,4=weather,5=date
#define KEY_WDAY_LANG       19   // 0=EN, 1=DE
#define KEY_SHOW_SECONDS    20   // 1=small seconds next to HH:MM, 0=off (default)
#define KEY_COLOR_GHOST     21   // ghost segment color
#define KEY_COLOR_LABEL_TOP 22   // top banner label color
#define KEY_GHOST_ENABLED      23   // 1=show ghost segments (default), 0=hide
#define KEY_COLOR_CGM_BANNER   24   // color for CGM Active/Offline status text
#define KEY_COLOR_TIME2_BG     25   // background tint for date+comp row (0=same as LCD bg)
#define KEY_COLOR_CGM_INFO     26   // color for CGM trend arrow + delta/age info
#define KEY_BACKLIGHT_ENABLED  27   // 1=enable custom backlight color on shake (default), 0=use system default
#define KEY_COLOR_BACKLIGHT    28   // custom backlight tint color (rgb888)
#define KEY_CGM_BOX_ENABLED    35   // 1=show border box around CGM status (default), 0=hide
#define KEY_COLOR_CGM_BOX_BG   36   // fill color for CGM status box
#define KEY_GHOST_COMP_ENABLED 37   // 1=ghost 8s in comp box (default), 0=hide (issue #4)
#define KEY_VIBE_ON_LOW        38   // 1=vibrate on low BG (3 pulses), 10 min cooldown
#define KEY_VIBE_ON_HIGH       39   // 1=vibrate on high BG (2 pulses), 10 min cooldown
#define KEY_CGM_VALUE          50
#define KEY_CGM_DELTA       51
#define KEY_CGM_TREND       52
#define KEY_CGM_AGE         53
#define KEY_STEPS           54
#define KEY_HR              55
#define KEY_WEATHER_TEMP    56
#define KEY_WEATHER_ICON    57
#define KEY_BATT_PCT        58
#define KEY_CGM_STATUS      59   // 0=OK,1=NO_DATA,2=NO_CONN,3=OLD (supercgm semantics)
#define KEY_CGM_TS          60   // Unix ts (seconds) of last CGM reading
#define KEY_CGM_SGV         61   // raw sgv in mg/dL (int) for range comparison
#define KEY_REQUEST_BG      62   // watch → phone: request immediate BG fetch

// ── CGM status (mirrors Nightscout-supercgm) ─────────────────────────────
typedef enum {
  CGM_STATUS_OK      = 0,  // valid reading
  CGM_STATUS_NO_DATA = 1,  // server responded, nothing parseable
  CGM_STATUS_NO_CONN = 2,  // network / phone unreachable
  CGM_STATUS_OLD     = 3   // JS confirmed reading is stale
} CgmStatus;

// ── State ─────────────────────────────────────────────────────────────────
static Window   *s_window;
static Layer    *s_canvas;
static AppTimer *s_shake_timer;
static bool      s_shake_active = false;

static char s_ns_url[128]     = "";
static char s_ns_token[64]    = "";
static int  s_ns_units        = 0;
static int  s_ns_high         = 180;
static int  s_ns_low          = 70;
static int  s_ns_stale_min    = 10;

static int  s_color_bg        = 0xFFFFFF;
static int  s_color_fg        = 0x000044;
static int  s_color_accent    = 0xFF0000;
static int  s_color_cgm_ok    = 0x38571A;
static int  s_color_cgm_high  = 0xAA5500;
static int  s_color_cgm_low   = 0xAA0000;

static int  s_complication    = 0;
static char s_label_tl[32]    = "QUARTZ";
static char s_label_tr[32]    = "TIME 2";
static char s_label_bot[32]   = "E-PAPER DISPLAY";
static int  s_first_weekday   = 0;
static int  s_date_format     = 0;
static int  s_shake_2nd       = 0;  // secondary slot shown on shake
static int  s_wday_lang       = 0;  // 0=EN, 1=DE
static int  s_color_ghost      = 0xAAAAFF;  // ghost segment color
static int  s_color_label_top  = 0xFFFFFF;  // top banner label color
static int  s_ghost_enabled    = 1;         // 1=show ghost segments, 0=hide
static int  s_ghost_comp_enabled = 1;       // 1=ghost 8s in comp box, 0=hide (issue #4)
static int  s_color_cgm_banner = 0x55AAFF;  // CGM Active/Offline status text color
static int  s_color_time2_bg   = 0xFFFFFF; // date+comp row background tint (matches LCD bg default)
static int  s_color_cgm_info   = 0xFFFFFF; // CGM trend/delta/age info (sits on black bezel)
static int  s_backlight_enabled= 1;         // 1=use custom backlight color on shake, 0=system default
static int  s_color_backlight  = 0xFFFFFF; // backlight tint (rgb888, default = white)
static int  s_cgm_box_enabled  = 1;         // 1=show border box around CGM status, 0=hide
static int  s_color_cgm_box_bg = 0xEEEEEE; // CGM status box fill color (default = LCD bg)

static char s_cgm_value[16]   = "---";
static char s_cgm_delta[16]   = "";
static char s_cgm_trend[8]    = "-";
static int  s_cgm_status      = CGM_STATUS_NO_CONN; // until first message arrives
static time_t s_cgm_ts        = 0;   // reading timestamp; watch ages locally
static int  s_cgm_sgv         = 0;   // raw mg/dL for range comparison
static int  s_show_seconds    = 0;   // 1=small seconds next to HH:MM
static int  s_vibe_on_low     = 0;
static int  s_vibe_on_high    = 0;
static time_t s_last_vibe_low_ts  = 0;
static time_t s_last_vibe_high_ts = 0;

static int  s_steps           = 0;
static int  s_hr              = 0;
static int  s_weather_temp    = 0;
static char s_weather_icon[8] = "";
static int  s_batt_pct        = 100;

// ── Custom fonts (loaded in window_load) ──────────────────────────────────
static GFont s_font_d14_time = NULL;  // DSEG14 52px : time (no seconds)
static GFont s_font_d14_time38 = NULL;// DSEG14 38px : time when seconds shown (fits next to the seconds)
static GFont s_font_d7_date  = NULL;  // DSEG14 20px : date
static GFont s_font_d7_comp  = NULL;  // DSEG14 Bold 22px : comp box real digits
static GFont s_font_comp_reg = NULL;  // DSEG14 Regular 22px : comp box ghost digits

// ── Helpers ───────────────────────────────────────────────────────────────
static GColor color_from_int(int v) {
  return GColorFromRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// ── CGM state evaluation (supercgm semantics, aged locally on the watch) ──
// Age is derived from the reading timestamp every redraw, so the display
// flips to OLD-BG even if the phone never sends another message.
static int cgm_age_min(void) {
  if (s_cgm_ts <= 0) return 9999;
  time_t now = time(NULL);
  if (now <= s_cgm_ts) return 0;
  return (int)((now - s_cgm_ts) / 60);
}

// Returns NULL when a fresh value should be shown, otherwise the
// DSEG-renderable status text for the comp box ("NOCON"/"NO-BG"/"OLDBG").
// Stale rule mirrors supercgm: older than 2x the sensor interval (pkjs sends
// that as NS_STALE_MIN, min. 5 min), compared in seconds.
static bool cgm_is_stale(void) {
  int stale_sec = s_ns_stale_min * 60;
  if (stale_sec < 300) stale_sec = 300;
  return s_cgm_status == CGM_STATUS_OLD ||
         (int)(time(NULL) - s_cgm_ts) > stale_sec;
}

static const char *cgm_status_text(void) {
  if (s_cgm_status == CGM_STATUS_NO_CONN)               return "NOCON";
  if (s_cgm_status == CGM_STATUS_NO_DATA || s_cgm_sgv <= 0) return "NO-BG";
  if (cgm_is_stale())                                   return "OLDBG";
  return NULL;
}

static bool cgm_is_fresh(void) {
  return (strlen(s_ns_url) > 0) && (cgm_status_text() == NULL);
}

static void complication_str(int slot, char *buf, size_t len) {
  switch (slot) {
    case 0: {
      if (strlen(s_ns_url) == 0) { snprintf(buf, len, "----"); break; }
      const char *st = cgm_status_text();
      if (st) snprintf(buf, len, "%s", st);
      else    snprintf(buf, len, "%s", s_cgm_value);
      break;
    }
    case 1: snprintf(buf, len, "%d", s_steps); break;
    case 2: snprintf(buf, len, s_hr > 0 ? "%d" : "----", s_hr); break;
    case 3: snprintf(buf, len, "%d", s_weather_temp); break;
    case 4: snprintf(buf, len, "%d", s_batt_pct); break;
    case 5: {
      time_t n = time(NULL); struct tm *t = localtime(&n);
      unsigned d = (unsigned)t->tm_mday, m = (unsigned)(t->tm_mon+1);
      if (s_date_format == 0) snprintf(buf, len, "%02u-%02u", d, m);
      else                    snprintf(buf, len, "%02u-%02u", m, d);
      break;
    }
    default: snprintf(buf, len, "-----");
  }
}

// Secondary-slot string shown while shaking.
// Slot numbering matches the config-page shake2nd select:
//   0 = CGM delta, 1 = steps, 2 = HR, 3 = battery %, 4 = weather temp
//   (5 = "None" – caller keeps primary, this is not invoked for slot 5)
static void shake_str(char *buf, size_t len) {
  switch (s_shake_2nd) {
    case 0:
      if (s_cgm_delta[0]) snprintf(buf, len, "%s", s_cgm_delta);
      else                 snprintf(buf, len, "----");
      break;
    case 1: snprintf(buf, len, "%d", s_steps); break;
    case 2: snprintf(buf, len, s_hr > 0 ? "%d" : "----", s_hr); break;
    case 3: snprintf(buf, len, "%d", s_batt_pct); break;
    case 4: snprintf(buf, len, "%d", s_weather_temp); break;
    default: snprintf(buf, len, "-----"); break;
  }
}

// Ghost segments behind real value – simulates unlit LCD segments.
// Respects the GHOST_ENABLED config toggle.
// DSEG text must never use TrailingEllipsis: the fonts have no '…' glyph and
// firmware 4.9 hangs (watchdog / frozen emulator) when it has to ellipsize.
static void lcd_text(GContext *ctx, const char *ghost, const char *real,
                     GFont font, GRect r, GColor gc, GColor rc,
                     GTextAlignment align) {
  if (s_ghost_enabled) {
    graphics_context_set_text_color(ctx, gc);
    graphics_draw_text(ctx, ghost, font, r,
                       GTextOverflowModeFill, align, NULL);
  }
  graphics_context_set_text_color(ctx, rc);
  graphics_draw_text(ctx, real, font, r,
                     GTextOverflowModeFill, align, NULL);
}

// Draw a small arrow (◄ or ►) using lines only
static void draw_arrow(GContext *ctx, GPoint tip, int half_h, bool right,
                       GColor col) {
  if (half_h < 1) return;
  int base_x = tip.x + (right ? -(half_h * 2) : (half_h * 2));
  GPoint top_pt = GPoint(base_x, tip.y - half_h);
  GPoint bot_pt = GPoint(base_x, tip.y + half_h);
  graphics_context_set_stroke_color(ctx, col);
  graphics_draw_line(ctx, tip, top_pt);
  graphics_draw_line(ctx, tip, bot_pt);
  graphics_draw_line(ctx, top_pt, bot_pt);
}

// Draw CGM trend direction as a graphic arrow in rect r with colour col.
// t: 'U'=strong up, 'u'=up, 'r'=rising 45°, '-'=flat,
//    'f'=falling 45°, 'd'=down, 'D'=strong down
static void draw_trend_arrow(GContext *ctx, char t, GRect r, GColor col) {
  int cx = r.origin.x + r.size.w / 2;
  int cy = r.origin.y + r.size.h / 2;
  int ah = r.size.h / 4;
  if (ah < 2) ah = 2;
  if (ah > 7) ah = 7;
  graphics_context_set_stroke_color(ctx, col);
  switch (t) {
    case 'U':  // DoubleUp — two upward chevrons ↑↑
      graphics_draw_line(ctx, GPoint(cx, cy-ah-1), GPoint(cx-ah, cy-1));
      graphics_draw_line(ctx, GPoint(cx, cy-ah-1), GPoint(cx+ah, cy-1));
      graphics_draw_line(ctx, GPoint(cx, cy+1),    GPoint(cx-ah, cy+ah+1));
      graphics_draw_line(ctx, GPoint(cx, cy+1),    GPoint(cx+ah, cy+ah+1));
      break;
    case 'u':  // SingleUp — arrow with stem ↑
      graphics_draw_line(ctx, GPoint(cx, cy+ah),   GPoint(cx,    cy-ah+1));
      graphics_draw_line(ctx, GPoint(cx, cy-ah),   GPoint(cx-ah, cy));
      graphics_draw_line(ctx, GPoint(cx, cy-ah),   GPoint(cx+ah, cy));
      break;
    case 'r':
      graphics_draw_line(ctx, GPoint(cx-ah, cy+ah),     GPoint(cx+ah, cy-ah));
      graphics_draw_line(ctx, GPoint(cx+ah, cy-ah),     GPoint(cx,    cy-ah));
      graphics_draw_line(ctx, GPoint(cx+ah, cy-ah),     GPoint(cx+ah, cy));
      break;
    case 'f':
      graphics_draw_line(ctx, GPoint(cx-ah, cy-ah),     GPoint(cx+ah, cy+ah));
      graphics_draw_line(ctx, GPoint(cx+ah, cy+ah),     GPoint(cx,    cy+ah));
      graphics_draw_line(ctx, GPoint(cx+ah, cy+ah),     GPoint(cx+ah, cy));
      break;
    case 'd':  // SingleDown — arrow with stem ↓
      graphics_draw_line(ctx, GPoint(cx, cy-ah),   GPoint(cx,    cy+ah-1));
      graphics_draw_line(ctx, GPoint(cx, cy+ah),   GPoint(cx-ah, cy));
      graphics_draw_line(ctx, GPoint(cx, cy+ah),   GPoint(cx+ah, cy));
      break;
    case 'D':  // DoubleDown — two downward chevrons ↓↓
      graphics_draw_line(ctx, GPoint(cx, cy-1),    GPoint(cx-ah, cy-ah-1));
      graphics_draw_line(ctx, GPoint(cx, cy-1),    GPoint(cx+ah, cy-ah-1));
      graphics_draw_line(ctx, GPoint(cx, cy+ah+1), GPoint(cx-ah, cy+1));
      graphics_draw_line(ctx, GPoint(cx, cy+ah+1), GPoint(cx+ah, cy+1));
      break;
    default:  // Flat
      graphics_draw_line(ctx, GPoint(cx-ah,    cy),       GPoint(cx+ah,    cy));
      graphics_draw_line(ctx, GPoint(cx+ah,    cy),       GPoint(cx+ah-ah/2, cy-ah/2));
      graphics_draw_line(ctx, GPoint(cx+ah,    cy),       GPoint(cx+ah-ah/2, cy+ah/2));
      break;
  }
}

// ── Battery callback (accurate, immediate updates) ───────────────────────
static void battery_state_handler(BatteryChargeState state) {
  s_batt_pct = (int)state.charge_percent;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── DRAW ──────────────────────────────────────────────────────────────────
//
// Layout follows the Casio "TIME 2 / HEART RATE MONITOR" original, in emery
// pixels (200×228; PX/PY scale for other sizes):
//    0..16   QUARTZ (left) / TIME 2 (right, suffix blue)   – editable labels
//   16..203  red ring (top band, thin side rails, bottom band)
//   20..43   ◄LIGHT · pebble · UP► / ENTER► (two rows)
//   43..164  LCD: white outer frame, white panel
//     49..75   date (DSEG 16) | comp box (DSEG 22, rounded border)
//     77..133  HH:MM (DSEG 48; 38 + small seconds when enabled), "P" (PM)
//    135..151  info strip: BAT + 10 bars | 7 day squares
//    151..164  weekday letters on the dark frame band
//  166..196  CGM status (2 lines, blue) · yellow heart with trend · DOWN►
//  203..228  yellow banner text (editable, default "E-PAPER DISPLAY")

// Yellow "heart rate" heart of the original, reused as CGM trend icon
static void draw_heart(GContext *ctx, GPoint c, int r, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(c.x - r, c.y - r / 2), r);
  graphics_fill_circle(ctx, GPoint(c.x + r, c.y - r / 2), r);
  GPathInfo tri = { 3, (GPoint[]){ {c.x - 2 * r, c.y - r / 4},
                                    {c.x + 2 * r, c.y - r / 4},
                                    {c.x, c.y + 2 * r} } };
  GPath *p = gpath_create(&tri);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w;
  int H = bounds.size.h;

#define PX(x) ((x)*W/200)
#define PY(y) ((y)*H/228)

  // ── Colors ────────────────────────────────────────────────────────────
  GColor col_bg    = color_from_int(s_color_bg);
  GColor col_fg    = color_from_int(s_color_fg);
  GColor col_ghost = color_from_int(s_color_ghost);
  GColor col_red   = color_from_int(s_color_accent);

  // Range comparison uses the raw mg/dL sgv; thresholds arrive in mg/dL too.
  bool cgm_fresh = cgm_is_fresh();
  // supercgm: strict thresholds on fresh values, grey when stale / error
  GColor col_cgm = cgm_fresh
      ? ( s_cgm_sgv > s_ns_high ? color_from_int(s_color_cgm_high)
        : s_cgm_sgv < s_ns_low  ? color_from_int(s_color_cgm_low)
                                : color_from_int(s_color_cgm_ok) )
      : PBL_IF_COLOR_ELSE(GColorDarkGray, color_from_int(s_color_fg));

  // ── Fonts ─────────────────────────────────────────────────────────────
  GFont f_tiny    = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_pebble  = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_lbl     = fonts_get_system_font(FONT_KEY_GOTHIC_09);
  GFont f_dseg_lg = s_font_d14_time ? s_font_d14_time
                  : fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont f_date    = s_font_d7_date ? s_font_d7_date
                  : fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  GFont f_comp    = s_font_d7_comp ? s_font_d7_comp
                  : fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  GFont f_comp_g  = s_font_comp_reg ? s_font_comp_reg : f_comp;

  // ── Y anchors ─────────────────────────────────────────────────────────
  int y_rs1     = PY(16);    // red ring top
  int y_btn     = PY(20);    // button label zone
  int y_lcd     = PY(43);    // LCD outer frame
  int y_in      = PY(46);    // white panel
  int y_dr      = PY(49);    // date / comp row
  int dr_h      = PY(26);
  int y_time    = PY(77);    // time row
  int y_info    = PY(135);   // info strip separator
  int y_in_end  = PY(151);   // panel end
  int y_lcd_end = PY(164);   // frame end (weekday band above)
  int y_cgs     = PY(166);   // CGM status zone
  int y_rs2     = PY(198);   // bottom red band
  int y_ban     = PY(203);   // yellow banner

  // ── X anchors ─────────────────────────────────────────────────────────
  int ring_s = PX(3);                 // thin red side rails
  int lx = PX(6),  lw = W - PX(12);   // LCD outer frame
  int ix = PX(10), iw = W - PX(20);   // white panel
  int x_l = ix + PX(4);               // content left
  int x_r = ix + iw - PX(4);          // content right
  int comp_w = PX(96);                // "88888" @22 ≈ 90 px + padding
  int comp_x = x_r - comp_w;
  int date_w = comp_x - x_l - PX(4);  // "88-88" @16 ≈ 65 px

  // ── 1. Black case + red ring ──────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, col_red);
  graphics_fill_rect(ctx, GRect(0, y_rs1, W, y_ban - y_rs1), PX(14), GCornersAll);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(ring_s, y_btn, W - 2 * ring_s, y_rs2 - y_btn),
                     PX(12), GCornersAll);

  // ── 2. Top labels: QUARTZ / TIME 2 (editable) ─────────────────────────
  {
    GColor col_lbl = color_from_int(s_color_label_top);
    graphics_context_set_text_color(ctx, col_lbl);
    graphics_draw_text(ctx, s_label_tl, f_tiny,
                       GRect(PX(6), PY(-1), PX(90), y_rs1),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    // Right label: a short last word (e.g. "2" of "TIME 2") is drawn in
    // Casio blue like on the original.
    GRect tr_r = GRect(W - PX(96), PY(-1), PX(90), y_rs1);
    const char *sp = strrchr(s_label_tr, ' ');
    if (sp && sp[1] != '\0' && strlen(sp + 1) <= 2) {
      char prefix[32];
      int plen = (int)(sp - s_label_tr);
      if (plen > (int)sizeof(prefix) - 1) plen = sizeof(prefix) - 1;
      memcpy(prefix, s_label_tr, plen);
      prefix[plen] = '\0';
      GSize suf_sz = graphics_text_layout_get_content_size(
          sp + 1, f_tiny, tr_r, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);
      graphics_draw_text(ctx, prefix, f_tiny,
                         GRect(tr_r.origin.x, tr_r.origin.y,
                               tr_r.size.w - suf_sz.w - PX(3), tr_r.size.h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
      graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite));
      graphics_draw_text(ctx, sp + 1, f_tiny,
                         GRect(tr_r.origin.x + tr_r.size.w - suf_sz.w, tr_r.origin.y,
                               suf_sz.w, tr_r.size.h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    } else {
      graphics_draw_text(ctx, s_label_tr, f_tiny, tr_r,
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
  }

  // ── 3. Button labels: ◄LIGHT · pebble · UP► / ENTER► ─────────────────
  {
    int bl_h  = y_lcd - y_btn;             // 23 px: two 11 px rows
    int row_h = bl_h / 2;
    int edge_l = PX(8), edge_r = W - PX(8);
    int cy = y_btn + bl_h / 2;
    graphics_context_set_text_color(ctx, GColorWhite);
    draw_arrow(ctx, GPoint(edge_l, cy), 2, false, GColorWhite);
    graphics_draw_text(ctx, "LIGHT", f_lbl, GRect(edge_l + PX(6), cy - 6, PX(34), 11),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, GColorYellow);
    graphics_draw_text(ctx, "pebble", f_pebble, GRect(PX(50), y_btn - PY(3), W - PX(100), 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "UP", f_lbl, GRect(edge_r - PX(40), y_btn - 1, PX(33), 11),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(edge_r, y_btn + row_h / 2 + 1), 2, true, GColorWhite);
    graphics_draw_text(ctx, "ENTER", f_lbl, GRect(edge_r - PX(40), y_btn + row_h - 1, PX(33), 11),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(edge_r, y_btn + row_h + row_h / 2 + 1), 2, true, GColorWhite);
  }

  // ── 4. LCD: white outer frame, white panel, weekday band ──────────────
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_round_rect(ctx, GRect(lx, y_lcd, lw, y_lcd_end - y_lcd), PX(8));
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_fill_color(ctx, col_bg);
  graphics_fill_rect(ctx, GRect(ix, y_in, iw, y_in_end - y_in), PX(5), GCornersTop);
  // optional tint behind the date + comp row
  graphics_context_set_fill_color(ctx, color_from_int(s_color_time2_bg));
  graphics_fill_rect(ctx, GRect(ix + PX(2), y_dr - PY(2), iw - PX(4), dr_h + PY(3)),
                     PX(4), GCornersAll);

  // ── 5. Date ───────────────────────────────────────────────────────────
  time_t now_t = time(NULL);
  struct tm *tnow = localtime(&now_t);
  char date_str[8];
  unsigned dd = (unsigned)tnow->tm_mday, dm = (unsigned)(tnow->tm_mon + 1);
  if (s_date_format == 0) snprintf(date_str, sizeof(date_str), "%02u-%02u", dd, dm);
  else                    snprintf(date_str, sizeof(date_str), "%02u-%02u", dm, dd);
  lcd_text(ctx, "88-88", date_str, f_date,
           GRect(x_l, y_dr + (dr_h - 16) / 2, date_w, 20),
           col_ghost, col_fg, GTextAlignmentLeft);

  // ── 6. Comp box (rounded, inside the panel) ───────────────────────────
  // shake_2nd uses its own slot numbering (0=CGM delta,3=battery,4=weather);
  // shake_2nd=5 means "None" – keep showing the primary slot unchanged.
  {
    char cstr[24];
    bool cgm_slot, cgm_valid;
    GColor creal;
    if (s_shake_active && s_shake_2nd != 5) {
      shake_str(cstr, sizeof(cstr));
      cgm_slot = false; cgm_valid = false; creal = col_fg;
    } else {
      complication_str(s_complication, cstr, sizeof(cstr));
      cgm_slot  = (s_complication == 0);
      cgm_valid = cgm_slot && cgm_fresh;
      creal     = (cgm_slot && strlen(s_ns_url) > 0) ? col_cgm : col_fg;
    }
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(col_ghost, col_fg));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, GRect(comp_x, y_dr, comp_w, dr_h), PX(5));
    graphics_context_set_stroke_width(ctx, 1);

    int ty = y_dr + (dr_h - 22) / 2 - PY(3);
    if (cgm_valid) {
      // value right-aligned in 4 cells + trend arrow in the 5th
      int arw_w = PX(14);
      GRect num_r = GRect(comp_x + PX(3), ty, comp_w - arw_w - PX(6), 26);
      if (s_ghost_enabled && s_ghost_comp_enabled) {
        graphics_context_set_text_color(ctx, col_ghost);
        graphics_draw_text(ctx, "8888", f_comp_g, num_r, GTextOverflowModeFill,
                           GTextAlignmentRight, NULL);
      }
      graphics_context_set_text_color(ctx, creal);
      graphics_draw_text(ctx, cstr, f_comp, num_r, GTextOverflowModeFill,
                         GTextAlignmentRight, NULL);
      draw_trend_arrow(ctx, s_cgm_trend[0],
                       GRect(comp_x + comp_w - arw_w - PX(3), y_dr + PY(3), arw_w, dr_h - PY(6)),
                       creal);
    } else {
      GRect all_r = GRect(comp_x + PX(3), ty, comp_w - PX(6), 26);
      if (s_ghost_enabled && s_ghost_comp_enabled) {
        graphics_context_set_text_color(ctx, col_ghost);
        graphics_draw_text(ctx, "88888", f_comp_g, all_r, GTextOverflowModeFill,
                           GTextAlignmentRight, NULL);
      }
      graphics_context_set_text_color(ctx, creal);
      graphics_draw_text(ctx, cstr, f_comp, all_r, GTextOverflowModeFill,
                         GTextAlignmentRight, NULL);
    }
  }

  // ── 7. Time HH:MM + optional seconds + "P" ────────────────────────────
  {
    char time_str[8];
    int hh = tnow->tm_hour;
    if (!clock_is_24h_style()) { hh %= 12; if (!hh) hh = 12; }
    snprintf(time_str, sizeof(time_str), "%02d:%02d", hh, tnow->tm_min);
    int t_h = y_info - y_time;
    if (s_show_seconds) {
      // HH:MM @38 (≈130 px) left, seconds @22 in the bottom-right corner
      GFont f_time_s = s_font_d14_time38 ? s_font_d14_time38 : f_dseg_lg;
      lcd_text(ctx, "88:88", time_str, f_time_s,
               GRect(x_l, y_time + (t_h - 38) / 2 - PY(4), PX(136), 44),
               col_ghost, col_fg, GTextAlignmentLeft);
      char sec_str[4];
      snprintf(sec_str, sizeof(sec_str), "%02d", tnow->tm_sec);
      lcd_text(ctx, "88", sec_str, f_comp,
               GRect(x_r - PX(38), y_info - PY(30), PX(38), 26),
               col_ghost, col_fg, GTextAlignmentRight);
    } else {
      // HH:MM @48 (≈164 px) centered
      lcd_text(ctx, "88:88", time_str, f_dseg_lg,
               GRect(ix, y_time + (t_h - 48) / 2 - PY(5), iw, 54),
               col_ghost, col_fg, GTextAlignmentCenter);
    }
    // "P" indicator: lit for PM in 12h mode, otherwise a ghost segment
    bool pm = !clock_is_24h_style() && tnow->tm_hour >= 12;
    if (pm || s_ghost_enabled) {
      graphics_context_set_text_color(ctx, pm ? col_fg : col_ghost);
      graphics_draw_text(ctx, "P", f_lbl, GRect(x_l, y_time - PY(3), PX(10), 11),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
  }

  // ── 8. Info strip: BAT + bars | day squares ───────────────────────────
  int strip_h = y_in_end - y_info;
  int mid_x   = ix + iw / 2 - PX(8);
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_line(ctx, GPoint(ix + PX(2), y_info), GPoint(ix + iw - PX(3), y_info));
  graphics_draw_line(ctx, GPoint(mid_x, y_info), GPoint(mid_x, y_in_end - 1));
  {
    int cy = y_info + strip_h / 2;
    graphics_context_set_text_color(ctx, col_fg);
    graphics_draw_text(ctx, "BAT", f_lbl, GRect(x_l, cy - 7, PX(20), 11),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    int n_seg = 10;
    int bx = x_l + PX(21);
    int bw = mid_x - PX(5) - bx;
    int seg_step = bw / n_seg;
    int seg_w = seg_step - 2; if (seg_w < 2) seg_w = 2;
    int seg_h = strip_h - PY(7); if (seg_h < 4) seg_h = 4;
    int lit = (s_batt_pct * n_seg + 50) / 100;
    if (lit > n_seg) lit = n_seg;
    for (int s = 0; s < n_seg; s++) {
      if (s >= lit && !s_ghost_enabled) continue;
      graphics_context_set_fill_color(ctx, s < lit ? col_fg : col_ghost);
      graphics_fill_rect(ctx, GRect(bx + s * seg_step, cy - seg_h / 2, seg_w, seg_h),
                         0, GCornerNone);
    }
  }

  // ── 9. Weekday squares (panel) + letters (dark frame band) ────────────
  static const char *D_SUN_EN[] = {"S","M","T","W","T","F","S"};
  static const char *D_MON_EN[] = {"M","T","W","T","F","S","S"};
  static const char *D_SUN_DE[] = {"S","M","D","M","D","F","S"};
  static const char *D_MON_DE[] = {"M","D","M","D","F","S","S"};
  const char **days_arr = (s_wday_lang == 1)
      ? ((s_first_weekday == 1) ? D_MON_DE : D_SUN_DE)
      : ((s_first_weekday == 1) ? D_MON_EN : D_SUN_EN);
  int today_idx = tnow->tm_wday;
  if (s_first_weekday == 1) today_idx = (today_idx + 6) % 7;
  {
    int wx = mid_x + PX(4);
    int step = (x_r - wx) / 7;
    int sq = PX(7);
    int sq_y = y_info + (strip_h - sq) / 2;
    for (int i = 0; i < 7; i++) {
      int dx = wx + i * step + (step - sq) / 2;
      if (i == today_idx) {
        graphics_context_set_fill_color(ctx, col_fg);
        graphics_fill_rect(ctx, GRect(dx, sq_y, sq, sq), 0, GCornerNone);
      } else if (s_ghost_enabled) {
        graphics_context_set_fill_color(ctx, col_ghost);
        graphics_fill_rect(ctx, GRect(dx, sq_y, sq, sq), 0, GCornerNone);
      } else {
        graphics_context_set_stroke_color(ctx, col_ghost);
        graphics_draw_rect(ctx, GRect(dx, sq_y, sq, sq));
      }
    }
    int lt_y = y_in_end + (y_lcd_end - y_in_end - 11) / 2 - 1;
    graphics_context_set_text_color(ctx, GColorWhite);
    for (int i = 0; i < 7; i++) {
      int dx = wx + i * step;
      graphics_draw_text(ctx, days_arr[i], f_lbl, GRect(dx, lt_y, step, 11),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }

  // ── 10. CGM status (2 lines) · heart with trend · DOWN► ───────────────
  {
    char l1[20], l2[32];
    int age = cgm_age_min();
    if (strlen(s_ns_url) == 0) {
      snprintf(l1, sizeof(l1), "NO URL");     snprintf(l2, sizeof(l2), "CGM MONITOR");
    } else if (s_cgm_status == CGM_STATUS_NO_CONN) {
      snprintf(l1, sizeof(l1), "NO CONN");    snprintf(l2, sizeof(l2), "CGM MONITOR");
    } else if (s_cgm_status == CGM_STATUS_NO_DATA || s_cgm_sgv <= 0) {
      snprintf(l1, sizeof(l1), "NO BG");      snprintf(l2, sizeof(l2), "CGM MONITOR");
    } else if (!cgm_fresh) {
      snprintf(l1, sizeof(l1), "OLD BG");
      if (age < 60)        snprintf(l2, sizeof(l2), "%d MIN", age);
      else if (age < 6000) snprintf(l2, sizeof(l2), "%d H", age / 60);
      else                 snprintf(l2, sizeof(l2), "--");
    } else {
      snprintf(l1, sizeof(l1), "CGM ACTIVE");
      snprintf(l2, sizeof(l2), "%s  %d MIN", s_cgm_delta, age);
    }
    graphics_context_set_text_color(ctx, color_from_int(s_color_cgm_banner));
    graphics_draw_text(ctx, l1, f_tiny, GRect(PX(10), y_cgs - PY(3), PX(96), 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, cgm_fresh ? color_from_int(s_color_cgm_info)
                                                   : color_from_int(s_color_cgm_banner));
    graphics_draw_text(ctx, l2, f_tiny, GRect(PX(10), y_cgs + PY(11), PX(96), 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    GPoint hc = GPoint(PX(122), y_cgs + PY(13));
    draw_heart(ctx, hc, PX(6), cgm_fresh ? GColorYellow : GColorDarkGray);
    draw_trend_arrow(ctx, cgm_fresh ? s_cgm_trend[0] : '-',
                     GRect(hc.x - PX(7), hc.y - PY(8), PX(14), PY(14)), GColorBlack);

    int cy = y_cgs + (y_rs2 - y_cgs) / 2;
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "DOWN", f_lbl, GRect(W - PX(48), cy - 6, PX(33), 11),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(W - PX(8), cy + 1), 2, true, GColorWhite);
  }

  // ── 11. Yellow banner (editable bottom label) ─────────────────────────
  graphics_context_set_text_color(ctx, GColorYellow);
  graphics_draw_text(ctx, s_label_bot, f_tiny, GRect(PX(4), y_ban + PY(2), W - PX(8), H - y_ban),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

#undef PX
#undef PY
}

// ── Shake ─────────────────────────────────────────────────────────────────
static void shake_timer_cb(void *ctx) {
  s_shake_active = false; s_shake_timer = NULL;
  if (s_canvas) layer_mark_dirty(s_canvas);
}
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  s_shake_active = true;
  if (s_shake_timer) app_timer_cancel(s_shake_timer);
  s_shake_timer = app_timer_register(5000, shake_timer_cb, NULL);
  // Backlight: set custom color (emery supports colored backlight)
  if (s_backlight_enabled) {
    light_set_color_rgb888((uint32_t)s_color_backlight);
  } else {
    light_set_system_color();
  }
  light_enable_interaction();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Bluetooth connection ─────────────────────────────────────────────────
// Phone/BT lost → show NOCON immediately instead of waiting for staleness.
// On reconnect, ask the phone for a fresh BG right away.
static void request_bg_fetch(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, KEY_REQUEST_BG, 1);
    app_message_outbox_send();
  }
}

// Vibrate on threshold breach, 10-minute cooldown per direction (supercgm)
static void check_bg_alerts(void) {
  if (s_cgm_status != CGM_STATUS_OK || s_cgm_sgv <= 0) return;
  time_t now = time(NULL);
  const int cooldown = 600;
  if (s_vibe_on_low && s_cgm_sgv < s_ns_low) {
    if ((now - s_last_vibe_low_ts) >= cooldown) {
      s_last_vibe_low_ts = now;
      static const uint32_t segs[] = {200, 100, 200, 100, 200};
      VibePattern pat = { .durations = segs, .num_segments = ARRAY_LENGTH(segs) };
      vibes_enqueue_custom_pattern(pat);
    }
  } else if (s_vibe_on_high && s_cgm_sgv > s_ns_high) {
    if ((now - s_last_vibe_high_ts) >= cooldown) {
      s_last_vibe_high_ts = now;
      static const uint32_t segs[] = {200, 100, 200};
      VibePattern pat = { .durations = segs, .num_segments = ARRAY_LENGTH(segs) };
      vibes_enqueue_custom_pattern(pat);
    }
  }
}

static void app_connection_handler(bool connected) {
  if (!connected) {
    s_cgm_status = CGM_STATUS_NO_CONN;
    s_cgm_sgv = 0;
    s_cgm_delta[0] = '\0';
    s_cgm_trend[0] = '\0';
    vibes_short_pulse();  // single buzz so user notices disconnect
  } else {
    request_bg_fetch();
  }
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Tick ──────────────────────────────────────────────────────────────────
static void tick_handler(struct tm *tick_time, TimeUnits units) {
  if (units & MINUTE_UNIT) {
#if defined(PBL_HEALTH)
    HealthServiceAccessibilityMask m;
    m = health_service_metric_accessible(HealthMetricStepCount,
                                         time_start_of_today(), time(NULL));
    if (m & HealthServiceAccessibilityMaskAvailable)
      s_steps = (int)health_service_sum_today(HealthMetricStepCount);
    m = health_service_metric_accessible(HealthMetricHeartRateBPM,
                                         time(NULL), time(NULL));
    if (m & HealthServiceAccessibilityMaskAvailable)
      s_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
#endif
    s_batt_pct = (int)battery_state_service_peek().charge_percent;
  }
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Tick subscription (seconds only when the seconds display is on) ──────
static void tick_handler(struct tm *tick_time, TimeUnits units);
static void update_tick_subscription(void) {
  tick_timer_service_subscribe(
      s_show_seconds ? (SECOND_UNIT | MINUTE_UNIT) : MINUTE_UNIT,
      tick_handler);
}

static void save_persist(void);

// ── AppMessage ────────────────────────────────────────────────────────────
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
#define GS(k,d) if((t=dict_find(iter,k))) snprintf(d,sizeof(d),"%s",t->value->cstring)
#define GI(k,d) if((t=dict_find(iter,k))) d=(int)t->value->int32
  GS(KEY_NS_URL,s_ns_url); GS(KEY_NS_TOKEN,s_ns_token);
  GI(KEY_NS_UNITS,s_ns_units); GI(KEY_NS_HIGH,s_ns_high);
  GI(KEY_NS_LOW,s_ns_low); GI(KEY_NS_STALE_MIN,s_ns_stale_min);
  GI(KEY_COLOR_BG,s_color_bg); GI(KEY_COLOR_FG,s_color_fg);
  GI(KEY_COLOR_ACCENT,s_color_accent); GI(KEY_COLOR_CGM_OK,s_color_cgm_ok);
  GI(KEY_COLOR_CGM_HIGH,s_color_cgm_high); GI(KEY_COLOR_CGM_LOW,s_color_cgm_low);
  GI(KEY_COMPLICATION,s_complication);
  GS(KEY_LABEL_TOP_LEFT,s_label_tl); GS(KEY_LABEL_TOP_RIGHT,s_label_tr);
  GS(KEY_LABEL_BOTTOM,s_label_bot);
  GI(KEY_FIRST_WEEKDAY,s_first_weekday); GI(KEY_DATE_FORMAT,s_date_format);
  GI(KEY_SHAKE_2ND,s_shake_2nd);
  GI(KEY_WDAY_LANG,s_wday_lang);
  GI(KEY_COLOR_GHOST,s_color_ghost); GI(KEY_COLOR_LABEL_TOP,s_color_label_top);
  GI(KEY_GHOST_ENABLED,s_ghost_enabled);
  GI(KEY_COLOR_CGM_BANNER,s_color_cgm_banner);
  GI(KEY_COLOR_TIME2_BG,s_color_time2_bg);
  GI(KEY_COLOR_CGM_INFO,s_color_cgm_info);
  GI(KEY_BACKLIGHT_ENABLED,s_backlight_enabled);
  GI(KEY_COLOR_BACKLIGHT,s_color_backlight);
  GI(KEY_CGM_BOX_ENABLED,s_cgm_box_enabled);
  GI(KEY_COLOR_CGM_BOX_BG,s_color_cgm_box_bg);
  GI(KEY_GHOST_COMP_ENABLED,s_ghost_comp_enabled);
  GI(KEY_VIBE_ON_LOW,s_vibe_on_low); GI(KEY_VIBE_ON_HIGH,s_vibe_on_high);
  {
    int prev_secs = s_show_seconds;
    GI(KEY_SHOW_SECONDS,s_show_seconds);
    if (s_show_seconds != prev_secs) update_tick_subscription();
  }
  GS(KEY_CGM_VALUE,s_cgm_value); GS(KEY_CGM_DELTA,s_cgm_delta);
  GS(KEY_CGM_TREND,s_cgm_trend);
  GI(KEY_CGM_SGV,s_cgm_sgv);
  if ((t = dict_find(iter, KEY_CGM_TS))) {
    s_cgm_ts = (time_t)t->value->int32;
  } else if ((t = dict_find(iter, KEY_CGM_AGE))) {
    // Fallback for messages without a timestamp: derive it from the age
    s_cgm_ts = time(NULL) - (time_t)t->value->int32 * 60;
  }
  if ((t = dict_find(iter, KEY_CGM_STATUS))) {
    s_cgm_status = (int)t->value->int32;
    // For OLD the value is still valid (just stale) → keep it.
    if (s_cgm_status == CGM_STATUS_NO_DATA || s_cgm_status == CGM_STATUS_NO_CONN) {
      s_cgm_sgv = 0;
      s_cgm_delta[0] = '\0';
      s_cgm_trend[0] = '\0';
    }
    check_bg_alerts();
  }
  GI(KEY_STEPS,s_steps); GI(KEY_HR,s_hr);
  GI(KEY_WEATHER_TEMP,s_weather_temp); GS(KEY_WEATHER_ICON,s_weather_icon);
  GI(KEY_BATT_PCT,s_batt_pct);
#undef GS
#undef GI
  // Persist only config messages (avoid flash wear on every 5-min CGM push);
  // config messages always carry KEY_COLOR_BG, data messages never do.
  if (dict_find(iter, KEY_COLOR_BG)) save_persist();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Persist ───────────────────────────────────────────────────────────────
// Config is stored in 4 keys (numeric settings as one struct, like supercgm)
// instead of ~35 single keys: writing that many keys on every config message
// left the firmware hanging on the next app exit/reinstall (seen on the
// emery emulator). PERSIST_DATA_MAX_LENGTH is 256 bytes, so strings get
// their own keys. Old per-key data (v1.x) is migrated once on load.
#define PKEY_CFG     100
#define PKEY_URL     101
#define PKEY_TOKEN   102
#define PKEY_LABELS  103
#define CFG_VERSION  1

typedef struct {
  int32_t version;
  int32_t ns_units, ns_high, ns_low, ns_stale_min;
  int32_t color_bg, color_fg, color_accent;
  int32_t color_cgm_ok, color_cgm_high, color_cgm_low;
  int32_t complication, first_weekday, date_format, shake_2nd, wday_lang;
  int32_t color_ghost, color_label_top, ghost_enabled, ghost_comp_enabled;
  int32_t color_cgm_banner, color_time2_bg, color_cgm_info;
  int32_t backlight_enabled, color_backlight;
  int32_t cgm_box_enabled, color_cgm_box_bg;
  int32_t show_seconds, vibe_on_low, vibe_on_high;
} PersistCfg;

typedef struct {
  char tl[32], tr[32], bot[32];
} PersistLabels;

static void cfg_to_struct(PersistCfg *c) {
  memset(c, 0, sizeof(*c));
  c->version = CFG_VERSION;
  c->ns_units = s_ns_units; c->ns_high = s_ns_high; c->ns_low = s_ns_low;
  c->ns_stale_min = s_ns_stale_min;
  c->color_bg = s_color_bg; c->color_fg = s_color_fg; c->color_accent = s_color_accent;
  c->color_cgm_ok = s_color_cgm_ok; c->color_cgm_high = s_color_cgm_high;
  c->color_cgm_low = s_color_cgm_low;
  c->complication = s_complication; c->first_weekday = s_first_weekday;
  c->date_format = s_date_format; c->shake_2nd = s_shake_2nd; c->wday_lang = s_wday_lang;
  c->color_ghost = s_color_ghost; c->color_label_top = s_color_label_top;
  c->ghost_enabled = s_ghost_enabled; c->ghost_comp_enabled = s_ghost_comp_enabled;
  c->color_cgm_banner = s_color_cgm_banner; c->color_time2_bg = s_color_time2_bg;
  c->color_cgm_info = s_color_cgm_info;
  c->backlight_enabled = s_backlight_enabled; c->color_backlight = s_color_backlight;
  c->cgm_box_enabled = s_cgm_box_enabled; c->color_cgm_box_bg = s_color_cgm_box_bg;
  c->show_seconds = s_show_seconds;
  c->vibe_on_low = s_vibe_on_low; c->vibe_on_high = s_vibe_on_high;
}

static void cfg_from_struct(const PersistCfg *c) {
  s_ns_units = c->ns_units; s_ns_high = c->ns_high; s_ns_low = c->ns_low;
  s_ns_stale_min = c->ns_stale_min;
  s_color_bg = c->color_bg; s_color_fg = c->color_fg; s_color_accent = c->color_accent;
  s_color_cgm_ok = c->color_cgm_ok; s_color_cgm_high = c->color_cgm_high;
  s_color_cgm_low = c->color_cgm_low;
  s_complication = c->complication; s_first_weekday = c->first_weekday;
  s_date_format = c->date_format; s_shake_2nd = c->shake_2nd; s_wday_lang = c->wday_lang;
  s_color_ghost = c->color_ghost; s_color_label_top = c->color_label_top;
  s_ghost_enabled = c->ghost_enabled; s_ghost_comp_enabled = c->ghost_comp_enabled;
  s_color_cgm_banner = c->color_cgm_banner; s_color_time2_bg = c->color_time2_bg;
  s_color_cgm_info = c->color_cgm_info;
  s_backlight_enabled = c->backlight_enabled; s_color_backlight = c->color_backlight;
  s_cgm_box_enabled = c->cgm_box_enabled; s_color_cgm_box_bg = c->color_cgm_box_bg;
  s_show_seconds = c->show_seconds;
  s_vibe_on_low = c->vibe_on_low; s_vibe_on_high = c->vibe_on_high;
}

// Write a key only when its content changed (saves flash writes)
static void persist_data_if_changed(uint32_t key, const void *data, size_t len) {
  static uint8_t buf[PERSIST_DATA_MAX_LENGTH];
  if (persist_exists(key) && persist_get_size(key) == (int)len &&
      persist_read_data(key, buf, len) == (int)len && memcmp(buf, data, len) == 0) {
    return;
  }
  persist_write_data(key, data, len);
}

static void save_persist(void) {
  PersistCfg c;
  cfg_to_struct(&c);
  persist_data_if_changed(PKEY_CFG, &c, sizeof(c));
  persist_data_if_changed(PKEY_URL, s_ns_url, sizeof(s_ns_url));
  persist_data_if_changed(PKEY_TOKEN, s_ns_token, sizeof(s_ns_token));
  PersistLabels l;
  memset(&l, 0, sizeof(l));
  strncpy(l.tl, s_label_tl, sizeof(l.tl) - 1);
  strncpy(l.tr, s_label_tr, sizeof(l.tr) - 1);
  strncpy(l.bot, s_label_bot, sizeof(l.bot) - 1);
  persist_data_if_changed(PKEY_LABELS, &l, sizeof(l));
}

// v1.x stored every setting under its AppMessage key
static void load_persist_legacy(void) {
#define LS(k,d) if(persist_exists(k)) persist_read_string(k,d,sizeof(d))
#define LI(k,d) if(persist_exists(k)) d=persist_read_int(k)
  LS(KEY_NS_URL,s_ns_url); LS(KEY_NS_TOKEN,s_ns_token);
  LI(KEY_NS_UNITS,s_ns_units); LI(KEY_NS_HIGH,s_ns_high);
  LI(KEY_NS_LOW,s_ns_low);
  LI(KEY_COLOR_BG,s_color_bg); LI(KEY_COLOR_FG,s_color_fg);
  LI(KEY_COLOR_ACCENT,s_color_accent); LI(KEY_COLOR_CGM_OK,s_color_cgm_ok);
  LI(KEY_COLOR_CGM_HIGH,s_color_cgm_high); LI(KEY_COLOR_CGM_LOW,s_color_cgm_low);
  LI(KEY_COMPLICATION,s_complication);
  LS(KEY_LABEL_TOP_LEFT,s_label_tl); LS(KEY_LABEL_TOP_RIGHT,s_label_tr);
  LS(KEY_LABEL_BOTTOM,s_label_bot);
  LI(KEY_FIRST_WEEKDAY,s_first_weekday); LI(KEY_DATE_FORMAT,s_date_format);
  LI(KEY_SHAKE_2ND,s_shake_2nd);
  LI(KEY_WDAY_LANG,s_wday_lang);
  LI(KEY_COLOR_GHOST,s_color_ghost); LI(KEY_COLOR_LABEL_TOP,s_color_label_top);
  LI(KEY_GHOST_ENABLED,s_ghost_enabled);
  LI(KEY_COLOR_CGM_BANNER,s_color_cgm_banner);
  LI(KEY_COLOR_TIME2_BG,s_color_time2_bg);
  LI(KEY_COLOR_CGM_INFO,s_color_cgm_info);
  LI(KEY_BACKLIGHT_ENABLED,s_backlight_enabled);
  LI(KEY_COLOR_BACKLIGHT,s_color_backlight);
  LI(KEY_CGM_BOX_ENABLED,s_cgm_box_enabled);
  LI(KEY_COLOR_CGM_BOX_BG,s_color_cgm_box_bg);
  LI(KEY_GHOST_COMP_ENABLED,s_ghost_comp_enabled);
  LI(KEY_SHOW_SECONDS,s_show_seconds);
#undef LS
#undef LI
}

static void load_persist(void) {
  PersistCfg c;
  if (persist_exists(PKEY_CFG) &&
      persist_read_data(PKEY_CFG, &c, sizeof(c)) == (int)sizeof(c) &&
      c.version == CFG_VERSION) {
    cfg_from_struct(&c);
    if (persist_exists(PKEY_URL))
      persist_read_data(PKEY_URL, s_ns_url, sizeof(s_ns_url));
    if (persist_exists(PKEY_TOKEN))
      persist_read_data(PKEY_TOKEN, s_ns_token, sizeof(s_ns_token));
    PersistLabels l;
    if (persist_exists(PKEY_LABELS) &&
        persist_read_data(PKEY_LABELS, &l, sizeof(l)) == (int)sizeof(l)) {
      memcpy(s_label_tl, l.tl, sizeof(s_label_tl));
      memcpy(s_label_tr, l.tr, sizeof(s_label_tr));
      memcpy(s_label_bot, l.bot, sizeof(s_label_bot));
    }
    s_ns_url[sizeof(s_ns_url) - 1] = '\0';
    s_ns_token[sizeof(s_ns_token) - 1] = '\0';
    s_label_tl[sizeof(s_label_tl) - 1] = '\0';
    s_label_tr[sizeof(s_label_tr) - 1] = '\0';
    s_label_bot[sizeof(s_label_bot) - 1] = '\0';
  } else {
    load_persist_legacy();
  }
}

// ── Window ────────────────────────────────────────────────────────────────
static void window_load(Window *w) {
  s_font_d14_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_TIME48));
  s_font_d14_time38 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_TIME38));
  s_font_d7_date  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_DATE16));
  s_font_d7_comp  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_COMP22));
  s_font_comp_reg = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_COMP22_REG));

  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
  if (s_font_d14_time) { fonts_unload_custom_font(s_font_d14_time); s_font_d14_time = NULL; }
  if (s_font_d14_time38) { fonts_unload_custom_font(s_font_d14_time38); s_font_d14_time38 = NULL; }
  if (s_font_d7_date)  { fonts_unload_custom_font(s_font_d7_date);  s_font_d7_date  = NULL; }
  if (s_font_d7_comp)  { fonts_unload_custom_font(s_font_d7_comp);  s_font_d7_comp  = NULL; }
  if (s_font_comp_reg) { fonts_unload_custom_font(s_font_comp_reg); s_font_comp_reg = NULL; }
}

// ── App lifecycle ─────────────────────────────────────────────────────────
static void init(void) {
  load_persist();
  battery_state_service_subscribe(battery_state_handler);
  s_batt_pct = (int)battery_state_service_peek().charge_percent;
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);
  update_tick_subscription();
  accel_tap_service_subscribe(accel_tap_handler);
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = app_connection_handler
  });
  {
    uint32_t in_max  = app_message_inbox_size_maximum();
    uint32_t out_max = app_message_outbox_size_maximum();
    app_message_open(in_max  < 512u ? in_max  : 512u,
                     out_max < 256u ? out_max : 256u);
  }
  app_message_register_inbox_received(inbox_received);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
