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

static int  s_color_bg        = 0xEEEEEE;
static int  s_color_fg        = 0x000044;
static int  s_color_accent    = 0xFF0000;
static int  s_color_cgm_ok    = 0x38571A;
static int  s_color_cgm_high  = 0xAA5500;
static int  s_color_cgm_low   = 0xAA0000;

static int  s_complication    = 0;
static char s_label_tl[32]    = "QUARTZ";
static char s_label_tr[32]    = "TIME 2";
static char s_label_bot[32]   = "Enabled";
static int  s_first_weekday   = 0;
static int  s_date_format     = 0;
static int  s_shake_2nd       = 0;  // secondary slot shown on shake
static int  s_wday_lang       = 0;  // 0=EN, 1=DE
static int  s_color_ghost      = 0xADADAD;  // ghost segment color
static int  s_color_label_top  = 0xFFFFFF;  // top banner label color
static int  s_ghost_enabled    = 1;         // 1=show ghost segments, 0=hide
static int  s_ghost_comp_enabled = 1;       // 1=ghost 8s in comp box, 0=hide (issue #4)
static int  s_color_cgm_banner = 0x38571A;  // CGM Active/Offline status text color
static int  s_color_time2_bg   = 0xEEEEEE; // date+comp row background tint (matches LCD bg default)
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

static int  s_steps           = 0;
static int  s_hr              = 0;
static int  s_weather_temp    = 0;
static char s_weather_icon[8] = "";
static int  s_batt_pct        = 100;

// ── Custom fonts (loaded in window_load) ──────────────────────────────────
static GFont s_font_d14_time = NULL;  // DSEG14 52px : time (no seconds)
static GFont s_font_d14_time44 = NULL;// DSEG14 44px : time when seconds shown
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
static const char *cgm_status_text(void) {
  if (s_cgm_status == CGM_STATUS_NO_CONN)               return "NOCON";
  if (s_cgm_status == CGM_STATUS_NO_DATA || s_cgm_sgv <= 0) return "NO-BG";
  if (s_cgm_status == CGM_STATUS_OLD ||
      cgm_age_min() > s_ns_stale_min)                    return "OLDBG";
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
static void lcd_text(GContext *ctx, const char *ghost, const char *real,
                     GFont font, GRect r, GColor gc, GColor rc,
                     GTextAlignment align) {
  if (s_ghost_enabled) {
    graphics_context_set_text_color(ctx, gc);
    graphics_draw_text(ctx, ghost, font, r,
                       GTextOverflowModeTrailingEllipsis, align, NULL);
  }
  graphics_context_set_text_color(ctx, rc);
  graphics_draw_text(ctx, real, font, r,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
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
// Y layout (ref H=168, W=144 → scaled to emery 228×200):
//   0..13    black label strip: QUARTZ (left) / TIME 2 (right, "2" blue)
//  13..152   RED RING: continuous rounded frame (top band, sides, bottom band)
//  17..30    button label zone: ◄LIGHT  pebble  UP► / ENTER►
//  30..131   outer LCD frame (dark, light rim, DSEG14 fonts)
//    34..118   inner white panel (2px double border + info strip)
//      36..61    date (DSEG14 20px, left) + comp box (DSEG14 22px, right)
//      61..106   HH:MM (DSEG14 52px; 44px + small seconds when enabled)
//     106..118   info strip: BAT label+bar (left) | day marker squares (right)
//   118..131   dark frame band: weekday letters S M T W T F S (right)
//  133..148  CGM status: [No URL/No Conn/No BG/Old BG/Active] [trend] [DOWN►]
//  152..168  E-Paper banner: "E-PAPER DISPLAY" in yellow
//
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w;
  int H = bounds.size.h;

#define SY(y) ((y)*H/168)
#define SX(x) ((x)*W/144)

  // ── Colors ────────────────────────────────────────────────────────────
  GColor col_bg    = color_from_int(s_color_bg);
  GColor col_fg    = color_from_int(s_color_fg);
  GColor col_ghost = color_from_int(s_color_ghost);
  GColor col_red   = color_from_int(s_color_accent);

  // Range comparison uses the raw mg/dL sgv; thresholds arrive in mg/dL too.
  bool cgm_fresh = cgm_is_fresh();
  GColor col_cgm = cgm_fresh
      ? ( s_cgm_sgv >= s_ns_high ? color_from_int(s_color_cgm_high)
        : s_cgm_sgv <= s_ns_low  ? color_from_int(s_color_cgm_low)
                                 : color_from_int(s_color_cgm_ok) )
      : color_from_int(s_color_cgm_low);

  // ── Fonts ─────────────────────────────────────────────────────────────
  GFont f_tiny    = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_lbl     = fonts_get_system_font(FONT_KEY_GOTHIC_09);
  GFont f_dseg_lg = s_font_d14_time ? s_font_d14_time
                  : fonts_get_system_font(FONT_KEY_LECO_60_BOLD_NUMBERS_AM_PM);
  GFont f_date    = s_font_d7_date ? s_font_d7_date
                  : fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  GFont f_comp    = s_font_d7_comp ? s_font_d7_comp
                  : s_font_d7_date ? s_font_d7_date
                  : fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  GFont f_comp_g  = s_font_comp_reg ? s_font_comp_reg : f_comp;

  // ── Y anchors (ref H=168) ─────────────────────────────────────────────
  int y_rs1     = SY(13);
  int y_lcd     = SY(30);
  int y_in      = SY(34);
  int y_dr      = SY(36);
  int y_time    = SY(61);
  int y_info    = SY(106);
  int y_in_end  = SY(118);   // white panel ends here …
  int y_lcd_end = SY(131);   // … dark outer frame continues (weekday band)
  int y_cgs     = SY(133);
  int y_rs2     = SY(148);
  int y_ban     = SY(152);

  // ── Radii ─────────────────────────────────────────────────────────────
  int lrad = SX(8);
  int irad = SX(6);

  // ── X anchors (thin red rails sit flush at the screen edge) ──────────
  int ring_s = SX(2);              // ring side thickness (thin, like the case)
  if (ring_s < 2) ring_s = 2;
  int lx = SX(6);
  int lw = W - SX(12);
  int ix = lx + SX(4);
  int iw = lw - SX(8);
  // content insets clear of the 2px double border (gap=4 + stroke)
  int x_l = ix + SX(5);
  int x_r = ix + iw - SX(5);

  // Comp box width; 4 px right-margin keeps box clear of the double border
  int comp_w = SX(68);
  int comp_x = x_r - comp_w - SX(4);
  int date_w = comp_x - x_l - SX(2);

  // Date+comp centering:
  //   render_h      = full metric height for GRect (no clipping)
  //   render_h_comp = metric height for comp font (22 px)
  //   glyph_h       = estimated ink height for visual centering
  int dr_h        = y_time - y_dr;
  int render_h      = 20;
  int render_h_comp = 22;
  int glyph_h       = 15;
  // Center glyphs vertically in the date/comp row, then shift up 3 px so the
  // visual ink sits between the inner-LCD border (y_in) and the row top (y_dr).
  // The rect borders (date box, comp box) remain anchored at y_dr / dr_h.
  int row_ty = y_dr + (dr_h - glyph_h) / 2 - 3;
  if (row_ty < y_dr) row_ty = y_dr;
  if (row_ty + render_h > y_dr + dr_h) row_ty = y_dr + dr_h - render_h;

  int tri_s = SY(3); if (tri_s < 2) tri_s = 2; (void)tri_s;

  // ── 1. Black background ───────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── 1b. Red ring: flush with the screen edge ──────────────────────────
  // Top/bottom bands plus thin rails hugging the very edge of the display;
  // the inside stays black (like the Casio case).
  {
    graphics_context_set_fill_color(ctx, col_red);
    graphics_fill_rect(ctx, GRect(0, y_rs1, W, y_ban - y_rs1),
                       SX(12), GCornersAll);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx,
      GRect(ring_s, y_rs1 + SY(4), W - 2*ring_s, y_rs2 - (y_rs1 + SY(4))),
      SX(10), GCornersAll);
  }

  // ── 2. Top banner labels ──────────────────────────────────────────────
  {
    GColor col_lbl = color_from_int(s_color_label_top);
    graphics_context_set_text_color(ctx, col_lbl);
    graphics_draw_text(ctx, s_label_tl, f_tiny,
                       GRect(SX(4), SY(1), SX(72), y_rs1-SY(1)),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    // Right label: if it ends in " <short suffix>" (e.g. "TIME 2"), the
    // suffix is drawn in Casio blue like on the original.
    GRect tr_r = GRect(W-SX(76), SY(1), SX(72), y_rs1-SY(1));
    const char *sp = strrchr(s_label_tr, ' ');
    if (sp && sp[1] != '\0' && strlen(sp + 1) <= 2) {
      char prefix[32];
      int plen = (int)(sp - s_label_tr);
      if (plen > (int)sizeof(prefix) - 1) plen = sizeof(prefix) - 1;
      memcpy(prefix, s_label_tr, plen);
      prefix[plen] = '\0';
      GSize suf_sz = graphics_text_layout_get_content_size(
          sp + 1, f_tiny, tr_r, GTextOverflowModeTrailingEllipsis,
          GTextAlignmentRight);
      GRect pre_r = GRect(tr_r.origin.x, tr_r.origin.y,
                          tr_r.size.w - suf_sz.w - SX(2), tr_r.size.h);
      graphics_draw_text(ctx, prefix, f_tiny, pre_r,
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
      graphics_context_set_text_color(ctx, GColorVividCerulean);
      graphics_draw_text(ctx, sp + 1, f_tiny,
                         GRect(tr_r.origin.x + tr_r.size.w - suf_sz.w, tr_r.origin.y,
                               suf_sz.w, tr_r.size.h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    } else {
      graphics_draw_text(ctx, s_label_tr, f_tiny, tr_r,
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
  }

  // ── 4. Button labels: ◄LIGHT · pebble · UP► / ENTER► ─────────────────
  {
    int bl_y  = y_rs1 + SY(4);
    int bl_h  = y_lcd - bl_y;
    int row_h = bl_h / 2;
    int tri_b = 2;
    int edge_l = SX(6);          // stay clear of the ring side rails
    int edge_r = W - SX(6);

    int bl_cy = bl_y + bl_h / 2;
    draw_arrow(ctx, GPoint(edge_l + tri_b, bl_cy), tri_b, false, GColorWhite);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "LIGHT", f_lbl,
                       GRect(edge_l+tri_b*2+SX(3), bl_y+(bl_h-9)/2, SX(30), 9),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    graphics_context_set_text_color(ctx, GColorYellow);
    graphics_draw_text(ctx, "pebble", f_tiny,
                       GRect(SX(42), bl_y+(bl_h-16)/2, W-SX(84), 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cy_up  = bl_y + row_h / 2;
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "UP", f_lbl,
                       GRect(edge_r-tri_b*2-SX(16), bl_y, SX(14), row_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(edge_r, cy_up), tri_b, true, GColorWhite);

    int cy_sel = bl_y + row_h + row_h / 2;
    graphics_draw_text(ctx, "ENTER", f_lbl,
                       GRect(edge_r-tri_b*2-SX(38), bl_y+row_h, SX(36), row_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(edge_r, cy_sel), tri_b, true, GColorWhite);
  }

  // ── 5. Outer LCD frame ────────────────────────────────────────────────
  // Chunky dark frame with a subtle light rim against the black case; the
  // frame extends below the white panel (weekday letters live in that band).
  int lh = y_lcd_end - y_lcd;
  int bd = SX(4);
  graphics_context_set_fill_color(ctx, col_fg);
  graphics_fill_rect(ctx, GRect(lx, y_lcd, lw, lh), lrad, GCornersAll);
  graphics_context_set_fill_color(ctx, col_bg);
  graphics_fill_rect(ctx, GRect(lx+bd, y_in, lw-2*bd, y_in_end-y_in),
                     lrad > bd ? lrad-bd : 0, GCornersAll);
  graphics_context_set_stroke_color(ctx, col_bg);
  graphics_draw_round_rect(ctx, GRect(lx-1, y_lcd-1, lw+2, lh+2), lrad+1);

  // ── 5b. Time 2 row tinted background ─────────────────────────────────
  // Fills the date+comp row with a separate color (default matches LCD bg).
  // Drawn before the double border so border strokes appear on top.
  {
    int t2_gap = 4;  // stay inside the double-border stroke (gap=3 + 1px)
    graphics_context_set_fill_color(ctx, color_from_int(s_color_time2_bg));
    graphics_fill_rect(ctx, GRect(ix+t2_gap, y_dr, iw-2*t2_gap, y_time-y_dr),
                       0, GCornerNone);
  }

  // ── 6. Double border around white LCD area + separator ───────────────
  // Two concentric 2px strokes, 3 px gap between them (filled col_bg).
  // Separator kept within the inner border.
  {
    int gap = 4;
    graphics_context_set_stroke_width(ctx, 2);
    graphics_context_set_stroke_color(ctx, col_fg);
    graphics_draw_round_rect(ctx, GRect(ix, y_in, iw, y_in_end-y_in), irad);
    graphics_draw_round_rect(ctx,
      GRect(ix+gap, y_in+gap, iw-2*gap, (y_in_end-y_in)-2*gap),
      irad > gap ? irad-gap : 0);
    graphics_draw_line(ctx,
      GPoint(ix+gap+2, y_info), GPoint(ix+iw-gap-2, y_info));
    graphics_context_set_stroke_width(ctx, 1);
  }

  // ── 7. Date ───────────────────────────────────────────────────────────
  time_t now_t = time(NULL);
  struct tm *tnow = localtime(&now_t);
  char date_str[8];
  unsigned dd = (unsigned)tnow->tm_mday, dm = (unsigned)(tnow->tm_mon+1);
  if (s_date_format == 0) snprintf(date_str, sizeof(date_str), "%02u-%02u", dd, dm);
  else                    snprintf(date_str, sizeof(date_str), "%02u-%02u", dm, dd);

  lcd_text(ctx, "88-88", date_str, f_date,
           GRect(x_l, row_ty, date_w, render_h),
           col_ghost, col_fg, GTextAlignmentCenter);

  // ── 8. Comp box ───────────────────────────────────────────────────────
  // shake_2nd uses its own slot numbering (0=CGM delta,3=battery,4=weather)
  // which differs from complication_str's primary-slot numbering.
  // shake_2nd=5 means "None" – keep showing the primary slot unchanged.
  char cstr[24];
  bool cgm_slot, cgm_valid;
  GColor creal;
  if (s_shake_active && s_shake_2nd != 5) {
    shake_str(cstr, sizeof(cstr));
    cgm_slot  = false;   // shake always renders as plain 5-digit slot
    cgm_valid = false;
    creal     = col_fg;
  } else {
    complication_str(s_complication, cstr, sizeof(cstr));
    cgm_slot  = (s_complication == 0);
    cgm_valid = cgm_slot && cgm_fresh;
    // Fresh CGM → range color; stale/no-data status text → low color;
    // no URL ("----") or non-CGM slots → plain fg.
    creal     = (cgm_slot && strlen(s_ns_url) > 0) ? col_cgm : col_fg;
  }

  // Comp box border: rounded 2px Casio-style frame
  graphics_context_set_stroke_width(ctx, 2);
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_round_rect(ctx, GRect(comp_x, y_dr, comp_w, dr_h), SX(4));
  graphics_context_set_stroke_width(ctx, 1);

  if (cgm_slot && cgm_valid) {
    // Fresh CGM: 4-digit value + trend arrow; narrower arrow to fit 22px font
    int arw_w = SX(12);
    int num_w = comp_w - arw_w - SX(2);
    GRect num_r = GRect(comp_x+SX(1), row_ty, num_w, render_h_comp);
    if (s_ghost_enabled && s_ghost_comp_enabled) {
      graphics_context_set_text_color(ctx, col_ghost);
      graphics_draw_text(ctx, "8888", f_comp_g, num_r,
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
    graphics_context_set_text_color(ctx, creal);
    graphics_draw_text(ctx, cstr, f_comp, num_r,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_trend_arrow(ctx, s_cgm_trend[0],
                     GRect(comp_x+SX(1)+num_w, row_ty, arw_w, render_h_comp),
                     creal);
  } else if (cgm_slot) {
    // CGM status text (NOCON / NO-BG / OLDBG) across the full box width
    GRect all_r = GRect(comp_x+SX(1), row_ty, comp_w-SX(2), render_h_comp);
    if (s_ghost_enabled && s_ghost_comp_enabled) {
      graphics_context_set_text_color(ctx, col_ghost);
      graphics_draw_text(ctx, "88888", f_comp_g, all_r,
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
    graphics_context_set_text_color(ctx, creal);
    graphics_draw_text(ctx, cstr, f_comp, all_r,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  } else {
    GRect all_r = GRect(comp_x+SX(1), row_ty, comp_w-SX(2), render_h_comp);
    if (s_ghost_enabled && s_ghost_comp_enabled) {
      graphics_context_set_text_color(ctx, col_ghost);
      graphics_draw_text(ctx, "88888", f_comp_g, all_r,
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
    graphics_context_set_text_color(ctx, creal);
    graphics_draw_text(ctx, cstr, f_comp, all_r,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }

  // ── 9. Time HH:MM (DSEG14 52px) + optional small seconds + P (PM) ─────
  char time_str[8];
  if (clock_is_24h_style()) {
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tnow->tm_hour, tnow->tm_min);
  } else {
    int hh = tnow->tm_hour % 12; if (!hh) hh = 12;
    snprintf(time_str, sizeof(time_str), "%02d:%02d", hh, tnow->tm_min);
  }
  {
    int tdy = y_time + (y_info - y_time - SY(40)) / 2;
    if (tdy < y_time) tdy = y_time;
    int th = y_info - tdy;

    if (s_show_seconds) {
      // HH:MM in the smaller 44px font, left-aligned; small seconds sit in
      // the freed bottom-right corner, like the Casio original.
      GFont f_time_s = s_font_d14_time44 ? s_font_d14_time44 : f_dseg_lg;
      int tdy2 = y_time + (y_info - y_time - SY(34)) / 2;
      if (tdy2 < y_time) tdy2 = y_time;
      lcd_text(ctx, "88:88", time_str, f_time_s,
               GRect(ix + SX(1), tdy2, iw - SX(2), y_info - tdy2),
               col_ghost, col_fg, GTextAlignmentLeft);
      char sec_str[4];
      snprintf(sec_str, sizeof(sec_str), "%02d", tnow->tm_sec);
      int sec_w = SX(26);
      int sec_h = 24;                        // 22px comp font + leading
      int sec_y = y_info - SY(2) - sec_h;
      lcd_text(ctx, "88", sec_str, f_comp,
               GRect(x_r - sec_w, sec_y, sec_w, sec_h),
               col_ghost, col_fg, GTextAlignmentRight);
    } else {
      // No seconds: use the full width, centered
      lcd_text(ctx, "88:88", time_str, f_dseg_lg,
               GRect(ix + SX(1), tdy, iw - SX(2), th),
               col_ghost, col_fg, GTextAlignmentCenter);
    }

    // "P" indicator (12h mode, PM) at the left edge of the time row
    if (!clock_is_24h_style() && tnow->tm_hour >= 12) {
      graphics_context_set_text_color(ctx, col_fg);
      graphics_draw_text(ctx, "P", f_lbl,
                         GRect(x_l, y_time - SY(1), SX(10), 10),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
  }

  // ── 10. Info strip: "BAT" label + bar (left) | day squares (right) ────
  int info_top = y_info + SY(1);
  int info_h   = y_in_end - y_info - SY(2);
  if (info_h < 4) info_h = 4;
  int mid_x = ix + iw / 2;

  // "BAT" label
  graphics_context_set_text_color(ctx, col_fg);
  graphics_draw_text(ctx, "BAT", f_lbl,
                     GRect(x_l, info_top + (info_h-9)/2 - 1, SX(16), 9),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Battery bar
  int bat_bx  = x_l + SX(17);
  int bat_bw  = mid_x - bat_bx - SX(3);
  int bat_h   = (info_h * 3) / 5;
  if (bat_h < 3) bat_h = 3;
  int bat_top = info_top + (info_h - bat_h) / 2;
  int n_seg   = 8;
  int seg_gap = 2;
  int seg_w   = bat_bw > (n_seg+1)*seg_gap
                ? (bat_bw - (n_seg+1)*seg_gap) / n_seg : 2;
  if (seg_w < 2) seg_w = 2;
  if (seg_w > SX(6)) seg_w = SX(6);
  int lit = (s_batt_pct * n_seg + 50) / 100;
  if (lit > n_seg) lit = n_seg;
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_rect(ctx, GRect(bat_bx, bat_top, bat_bw, bat_h));
  for (int s = 0; s < n_seg; s++) {
    int sx = bat_bx + seg_gap + s*(seg_w+seg_gap);
    graphics_context_set_fill_color(ctx, s < lit ? col_fg : col_ghost);
    graphics_fill_rect(ctx, GRect(sx, bat_top+1, seg_w, bat_h-2), 0, GCornerNone);
  }

  // ── 11. Weekday: marker squares inside LCD, letters on the bezel ─────
  // (matches the Casio original: squares in the LCD info strip, white
  //  S M T W T F S letters printed on the black case below the LCD)
  static const char *D_SUN_EN[] = {"S","M","T","W","T","F","S"};
  static const char *D_MON_EN[] = {"M","T","W","T","F","S","S"};
  static const char *D_SUN_DE[] = {"S","M","D","M","D","F","S"};
  static const char *D_MON_DE[] = {"M","D","M","D","F","S","S"};
  const char **days_arr;
  if (s_wday_lang == 1) {
    days_arr = (s_first_weekday == 1) ? D_MON_DE : D_SUN_DE;
  } else {
    days_arr = (s_first_weekday == 1) ? D_MON_EN : D_SUN_EN;
  }
  int today_idx = tnow->tm_wday;
  if (s_first_weekday == 1) today_idx = (today_idx + 6) % 7;
  int wday_x   = mid_x + SX(2);
  int wday_w   = x_r - wday_x;
  int day_step = wday_w / 7;
  int sq = SY(5);                       // marker square size
  if (sq > info_h - 2) sq = info_h - 2;
  if (sq < 3) sq = 3;
  int sq_y = info_top + (info_h - sq) / 2;
  for (int i = 0; i < 7; i++) {
    int dx = wday_x + i*day_step + (day_step - sq)/2;
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
  // Letters in the dark bottom band of the LCD frame (outer border of the
  // box, like the Casio original), one per square, same columns
  {
    int lt_h = y_lcd_end - y_in_end;
    int lt_y = y_in_end + (lt_h - 9) / 2 - 1;
    graphics_context_set_text_color(ctx, col_bg);
    for (int i = 0; i < 7; i++) {
      int dw = SX(10);
      int dx = wday_x + i*day_step + (day_step - dw)/2;
      graphics_draw_text(ctx, days_arr[i], f_lbl,
                         GRect(dx, lt_y, dw, 9),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }

  // ── 12. CGM status + DOWN button label ───────────────────────────────
  {
    int cgs_h  = y_rs2 - y_cgs;
    int cgs_cy = y_cgs + cgs_h / 2;

    char cgs_txt[16];
    GColor cgs_col = color_from_int(s_color_cgm_low);
    if (strlen(s_ns_url) == 0) {
      snprintf(cgs_txt, sizeof(cgs_txt), "No URL");
    } else if (s_cgm_status == CGM_STATUS_NO_CONN) {
      snprintf(cgs_txt, sizeof(cgs_txt), "No Conn");
    } else if (s_cgm_status == CGM_STATUS_NO_DATA || s_cgm_sgv <= 0) {
      snprintf(cgs_txt, sizeof(cgs_txt), "No BG");
    } else if (!cgm_fresh) {
      snprintf(cgs_txt, sizeof(cgs_txt), "Old BG");
    } else {
      snprintf(cgs_txt, sizeof(cgs_txt), "CGM Active");
      cgs_col = color_from_int(s_color_cgm_banner);
    }
    // Measure text to dynamically size the box
    int box_pad = SX(5);
    GSize txt_sz = graphics_text_layout_get_content_size(
        cgs_txt, f_tiny, GRect(0, 0, W / 2, cgs_h),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    int box_w = txt_sz.w + 2 * box_pad;
    GRect box_r = GRect(SX(6), y_cgs+1, box_w, cgs_h-2);

    // #7: optional border box — same corner radius as inner LCD frame
    if (s_cgm_box_enabled) {
      graphics_context_set_fill_color(ctx, color_from_int(s_color_cgm_box_bg));
      graphics_fill_rect(ctx, box_r, irad, GCornersAll);
      graphics_context_set_stroke_color(ctx, cgs_col);
      graphics_draw_round_rect(ctx, box_r, irad);
    }
    // Text centered inside box
    graphics_context_set_text_color(ctx, cgs_col);
    graphics_draw_text(ctx, cgs_txt, f_tiny,
                       GRect(SX(6)+box_pad, y_cgs+1, box_w-2*box_pad, cgs_h-2),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cgm_age = cgm_age_min();
    if (cgm_fresh) {
      // Trend arrow directly after the box
      GColor col_info = color_from_int(s_color_cgm_info);
      int arrow_x = SX(6) + box_w + SX(3);
      draw_trend_arrow(ctx, s_cgm_trend[0],
                       GRect(arrow_x, y_cgs+1, SX(11), cgs_h-2), col_info);
      // #8: delta + age info next to trend arrow
      char info_str[32];
      if (cgm_age < 60)
        snprintf(info_str, sizeof(info_str), "%s %dm", s_cgm_delta, cgm_age);
      else
        snprintf(info_str, sizeof(info_str), "%s %dh", s_cgm_delta, cgm_age/60);
      graphics_context_set_text_color(ctx, col_info);
      graphics_draw_text(ctx, info_str, f_lbl,
                         GRect(arrow_x + SX(12), y_cgs+(cgs_h-9)/2, SX(21), 9),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    } else if (strlen(s_ns_url) > 0 && s_cgm_sgv > 0 && s_cgm_ts > 0) {
      // Stale reading: show how old it is next to the status box
      GColor col_info = color_from_int(s_color_cgm_info);
      char info_str[16];
      if (cgm_age < 60)       snprintf(info_str, sizeof(info_str), "%dm", cgm_age);
      else if (cgm_age < 6000) snprintf(info_str, sizeof(info_str), "%dh", cgm_age/60);
      else                    snprintf(info_str, sizeof(info_str), "--");
      graphics_context_set_text_color(ctx, col_info);
      graphics_draw_text(ctx, info_str, f_lbl,
                         GRect(SX(6)+box_w+SX(3), y_cgs+(cgs_h-9)/2, SX(24), 9),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "DOWN", f_lbl,
                       GRect(W-SX(6)-2*2-SX(34), y_cgs+(cgs_h-9)/2, SX(32), 9),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(W-SX(6), cgs_cy), 2, true, GColorWhite);
  }

  // (bottom red band is part of the ring drawn in 1b)

  // ── 14. E-Paper banner ────────────────────────────────────────────────
  int ban_h = H - y_ban;
  graphics_context_set_text_color(ctx, GColorYellow);
  graphics_draw_text(ctx, "E-PAPER DISPLAY", f_tiny,
                     GRect(SX(4), y_ban, W-SX(8), ban_h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

#undef SY
#undef SX
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

static void app_connection_handler(bool connected) {
  if (!connected) {
    s_cgm_status = CGM_STATUS_NO_CONN;
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
      s_cgm_trend[0] = '\0';
    }
  }
  GI(KEY_STEPS,s_steps); GI(KEY_HR,s_hr);
  GI(KEY_WEATHER_TEMP,s_weather_temp); GS(KEY_WEATHER_ICON,s_weather_icon);
  GI(KEY_BATT_PCT,s_batt_pct);
#undef GS
#undef GI
  // Persist only config messages (avoid flash wear on every 5-min CGM push);
  // config messages always carry KEY_COLOR_BG, data messages never do.
  if (!dict_find(iter, KEY_COLOR_BG)) {
    if (s_canvas) layer_mark_dirty(s_canvas);
    return;
  }
  persist_write_string(KEY_NS_URL,s_ns_url);
  persist_write_string(KEY_NS_TOKEN,s_ns_token);
  persist_write_int(KEY_NS_UNITS,s_ns_units);
  persist_write_int(KEY_NS_HIGH,s_ns_high);
  persist_write_int(KEY_NS_LOW,s_ns_low);
  persist_write_int(KEY_NS_STALE_MIN,s_ns_stale_min);
  persist_write_int(KEY_COLOR_BG,s_color_bg);
  persist_write_int(KEY_COLOR_FG,s_color_fg);
  persist_write_int(KEY_COLOR_ACCENT,s_color_accent);
  persist_write_int(KEY_COLOR_CGM_OK,s_color_cgm_ok);
  persist_write_int(KEY_COLOR_CGM_HIGH,s_color_cgm_high);
  persist_write_int(KEY_COLOR_CGM_LOW,s_color_cgm_low);
  persist_write_int(KEY_COMPLICATION,s_complication);
  persist_write_string(KEY_LABEL_TOP_LEFT,s_label_tl);
  persist_write_string(KEY_LABEL_TOP_RIGHT,s_label_tr);
  persist_write_string(KEY_LABEL_BOTTOM,s_label_bot);
  persist_write_int(KEY_FIRST_WEEKDAY,s_first_weekday);
  persist_write_int(KEY_DATE_FORMAT,s_date_format);
  persist_write_int(KEY_SHAKE_2ND,s_shake_2nd);
  persist_write_int(KEY_WDAY_LANG,s_wday_lang);
  persist_write_int(KEY_COLOR_GHOST,s_color_ghost);
  persist_write_int(KEY_COLOR_LABEL_TOP,s_color_label_top);
  persist_write_int(KEY_GHOST_ENABLED,s_ghost_enabled);
  persist_write_int(KEY_COLOR_CGM_BANNER,s_color_cgm_banner);
  persist_write_int(KEY_COLOR_TIME2_BG,s_color_time2_bg);
  persist_write_int(KEY_COLOR_CGM_INFO,s_color_cgm_info);
  persist_write_int(KEY_BACKLIGHT_ENABLED,s_backlight_enabled);
  persist_write_int(KEY_COLOR_BACKLIGHT,s_color_backlight);
  persist_write_int(KEY_CGM_BOX_ENABLED,s_cgm_box_enabled);
  persist_write_int(KEY_COLOR_CGM_BOX_BG,s_color_cgm_box_bg);
  persist_write_int(KEY_GHOST_COMP_ENABLED,s_ghost_comp_enabled);
  persist_write_int(KEY_SHOW_SECONDS,s_show_seconds);
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Persist load ──────────────────────────────────────────────────────────
static void load_persist(void) {
#define LS(k,d) if(persist_exists(k)) persist_read_string(k,d,sizeof(d))
#define LI(k,d) if(persist_exists(k)) d=persist_read_int(k)
  LS(KEY_NS_URL,s_ns_url); LS(KEY_NS_TOKEN,s_ns_token);
  LI(KEY_NS_UNITS,s_ns_units); LI(KEY_NS_HIGH,s_ns_high);
  LI(KEY_NS_LOW,s_ns_low); LI(KEY_NS_STALE_MIN,s_ns_stale_min);
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

// ── Window ────────────────────────────────────────────────────────────────
static void window_load(Window *w) {
  s_font_d14_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_TIME52));
  s_font_d14_time44 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_TIME44));
  s_font_d7_date  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_DATE20));
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
  if (s_font_d14_time44) { fonts_unload_custom_font(s_font_d14_time44); s_font_d14_time44 = NULL; }
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
