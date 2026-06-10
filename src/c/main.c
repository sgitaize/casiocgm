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
#define KEY_SHOW_SECONDS    20   // reserved (not used on emery)
#define KEY_COLOR_GHOST     21   // ghost segment color
#define KEY_COLOR_LABEL_TOP 22   // top banner label color
#define KEY_CGM_VALUE       50
#define KEY_CGM_DELTA       51
#define KEY_CGM_TREND       52
#define KEY_CGM_AGE         53
#define KEY_STEPS           54
#define KEY_HR              55
#define KEY_WEATHER_TEMP    56
#define KEY_WEATHER_ICON    57
#define KEY_BATT_PCT        58

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
static int  s_color_accent    = 0xFFAA00;
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
static int  s_color_ghost     = 0xADADAD;  // ghost segment color
static int  s_color_label_top = 0xFFFFFF;  // top banner label color

static char s_cgm_value[16]   = "---";
static char s_cgm_delta[16]   = "";
static char s_cgm_trend[8]    = "-";
static int  s_cgm_age         = 999;

static int  s_steps           = 0;
static int  s_hr              = 0;
static int  s_weather_temp    = 0;
static char s_weather_icon[8] = "";
static int  s_batt_pct        = 100;

// ── Custom fonts (loaded in window_load) ──────────────────────────────────
static GFont s_font_d14_time = NULL;  // DSEG14 52px : time
static GFont s_font_d7_date  = NULL;  // DSEG14 20px : date
static GFont s_font_d7_comp  = NULL;  // DSEG14 22px : comp box

// ── Helpers ───────────────────────────────────────────────────────────────
static GColor color_from_int(int v) {
  return GColorFromRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

static void complication_str(int slot, char *buf, size_t len) {
  switch (slot) {
    case 0: {
      bool stale = (s_cgm_age > s_ns_stale_min) || (atoi(s_cgm_value) <= 0);
      if (stale) snprintf(buf, len, "----");
      else       snprintf(buf, len, "%s", s_cgm_value);
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

// Ghost segments behind real value – simulates unlit LCD segments
static void lcd_text(GContext *ctx, const char *ghost, const char *real,
                     GFont font, GRect r, GColor gc, GColor rc,
                     GTextAlignment align) {
  graphics_context_set_text_color(ctx, gc);
  graphics_draw_text(ctx, ghost, font, r,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
  graphics_context_set_text_color(ctx, rc);
  graphics_draw_text(ctx, real, font, r,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

// Casio-style tapered band: hexagon shape, tapers to a centre-height point
// on each edge. 20% of width on each side forms the diagonal taper.
static void draw_casio_band(GContext *ctx, int y_top, int band_h, int W,
                            GColor col) {
  int cy = y_top + band_h / 2;
  int cw = W / 5;
  GPoint pts[6] = {
    GPoint(0,     cy),
    GPoint(cw,    y_top),
    GPoint(W-cw,  y_top),
    GPoint(W,     cy),
    GPoint(W-cw,  y_top + band_h),
    GPoint(cw,    y_top + band_h),
  };
  GPathInfo info = {6, pts};
  GPath *gp = gpath_create(&info);
  if (gp) {
    graphics_context_set_fill_color(ctx, col);
    gpath_draw_filled(ctx, gp);
    gpath_destroy(gp);
  }
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
    case 'U':
      graphics_draw_line(ctx, GPoint(cx,    cy-1),      GPoint(cx-ah, cy+ah-1));
      graphics_draw_line(ctx, GPoint(cx,    cy-1),      GPoint(cx+ah, cy+ah-1));
      graphics_draw_line(ctx, GPoint(cx,    cy-ah-2),   GPoint(cx-ah, cy-2));
      graphics_draw_line(ctx, GPoint(cx,    cy-ah-2),   GPoint(cx+ah, cy-2));
      break;
    case 'u':
      graphics_draw_line(ctx, GPoint(cx,    cy-ah),     GPoint(cx-ah, cy+ah));
      graphics_draw_line(ctx, GPoint(cx,    cy-ah),     GPoint(cx+ah, cy+ah));
      graphics_draw_line(ctx, GPoint(cx-ah, cy+ah),     GPoint(cx+ah, cy+ah));
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
    case 'd':
      graphics_draw_line(ctx, GPoint(cx,    cy+ah),     GPoint(cx-ah, cy-ah));
      graphics_draw_line(ctx, GPoint(cx,    cy+ah),     GPoint(cx+ah, cy-ah));
      graphics_draw_line(ctx, GPoint(cx-ah, cy-ah),     GPoint(cx+ah, cy-ah));
      break;
    case 'D':
      graphics_draw_line(ctx, GPoint(cx,    cy+1),      GPoint(cx-ah, cy-ah+1));
      graphics_draw_line(ctx, GPoint(cx,    cy+1),      GPoint(cx+ah, cy-ah+1));
      graphics_draw_line(ctx, GPoint(cx,    cy+ah+2),   GPoint(cx-ah, cy+2));
      graphics_draw_line(ctx, GPoint(cx,    cy+ah+2),   GPoint(cx+ah, cy+2));
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
//   0..13    black label strip: QUARTZ (left) / TIME 2 (right)
//  13..17    TOP RED BAND (tapered)
//  17..30    button label zone: ◄LIGHT  pebble  UP► / SELECT►
//  30..126   outer LCD rect (thick double-border, DSEG14 fonts)
//    34..124   inner LCD rect (border + info strip)
//      36..61    date (DSEG14 20px, left) + comp box (DSEG14 22px, right)
//      61..112   HH:MM (DSEG14 52px, full width)
//     112..124   info strip: BAT bar (left) | DOW letters (right)
//  126..148  CGM status: [No URL/Offline/Active] [trend arrow] [DOWN►]
//  148..152  BOTTOM RED BAND (tapered)
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
  GColor col_red   = GColorRed;

  int cgm_int = atoi(s_cgm_value);
  bool cgm_fresh = (strlen(s_ns_url) > 0)
                && (s_cgm_age <= s_ns_stale_min)
                && (cgm_int > 0);
  GColor col_cgm = cgm_fresh
      ? ( cgm_int >= s_ns_high ? color_from_int(s_color_cgm_high)
        : cgm_int <= s_ns_low  ? color_from_int(s_color_cgm_low)
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

  // ── Y anchors (ref H=168) ─────────────────────────────────────────────
  int y_rs1     = SY(13);
  int y_lcd     = SY(30);
  int y_in      = SY(34);
  int y_dr      = SY(36);
  int y_time    = SY(61);
  int y_info    = SY(112);
  int y_in_end  = SY(124);
  int y_lcd_end = SY(126);
  int y_cgs     = SY(130);
  int y_rs2     = SY(148);
  int y_ban     = SY(152);

  // ── Radii ─────────────────────────────────────────────────────────────
  int lrad = SX(8);
  int irad = SX(6);

  // ── X anchors ─────────────────────────────────────────────────────────
  int lx = SX(1);
  int lw = W - SX(2);
  int ix = lx + SX(4);
  int iw = lw - SX(8);
  int x_l = ix + SX(3);
  int x_r = ix + iw - SX(2);

  // Comp box width; date_w fills the remainder
  int comp_w = SX(68);
  int comp_x = x_r - comp_w;
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

  // ── 2. Top banner labels ──────────────────────────────────────────────
  {
    GColor col_lbl = color_from_int(s_color_label_top);
    graphics_context_set_text_color(ctx, col_lbl);
    graphics_draw_text(ctx, s_label_tl, f_tiny,
                       GRect(SX(4), SY(1), SX(72), y_rs1-SY(1)),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, s_label_tr, f_tiny,
                       GRect(W-SX(76), SY(1), SX(72), y_rs1-SY(1)),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }

  // ── 3. Top red band ───────────────────────────────────────────────────
  draw_casio_band(ctx, y_rs1, SY(4), W, col_red);

  // ── 4. Button labels: ◄LIGHT · pebble · UP► / SELECT► ────────────────
  {
    int bl_y  = y_rs1 + SY(4);
    int bl_h  = y_lcd - bl_y;
    int row_h = bl_h / 2;
    int tri_b = 2;

    int bl_cy = bl_y + bl_h / 2;
    draw_arrow(ctx, GPoint(SX(4) + tri_b, bl_cy), tri_b, false, GColorWhite);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "LIGHT", f_lbl,
                       GRect(SX(4)+tri_b*2+SX(1), bl_y+(bl_h-9)/2, SX(32), 9),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    graphics_context_set_text_color(ctx, GColorYellow);
    graphics_draw_text(ctx, "pebble", f_tiny,
                       GRect(SX(42), bl_y+(bl_h-16)/2, W-SX(84), 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cy_up  = bl_y + row_h / 2;
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "UP", f_lbl,
                       GRect(W-SX(4)-tri_b*2-SX(16), bl_y, SX(14), row_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(W-SX(4), cy_up), tri_b, true, GColorWhite);

    int cy_sel = bl_y + row_h + row_h / 2;
    graphics_draw_text(ctx, "SELECT", f_lbl,
                       GRect(W-SX(4)-tri_b*2-SX(38), bl_y+row_h, SX(36), row_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(W-SX(4), cy_sel), tri_b, true, GColorWhite);
  }

  // ── 5. Outer LCD frame ────────────────────────────────────────────────
  int lh = y_lcd_end - y_lcd;
  int bd = SX(4);
  graphics_context_set_fill_color(ctx, col_fg);
  graphics_fill_rect(ctx, GRect(lx, y_lcd, lw, lh), lrad, GCornersAll);
  graphics_context_set_fill_color(ctx, col_bg);
  graphics_fill_rect(ctx, GRect(lx+bd, y_in, lw-2*bd, y_in_end-y_in),
                     lrad > bd ? lrad-bd : 0, GCornersAll);
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_round_rect(ctx, GRect(lx, y_lcd, lw, lh), lrad);

  // ── 6. Double border around white LCD area + separator ───────────────
  // Two concentric stroked rects, 3 px gap between them (filled col_bg).
  // Separator kept within the inner border.
  {
    int gap = 3;
    graphics_context_set_stroke_color(ctx, col_fg);
    graphics_draw_round_rect(ctx, GRect(ix, y_in, iw, y_in_end-y_in), irad);
    graphics_draw_round_rect(ctx,
      GRect(ix+gap, y_in+gap, iw-2*gap, (y_in_end-y_in)-2*gap),
      irad > gap ? irad-gap : 0);
    graphics_draw_line(ctx,
      GPoint(ix+gap+1, y_info), GPoint(ix+iw-gap-1, y_info));
  }

  // ── 7. Date ───────────────────────────────────────────────────────────
  time_t now_t = time(NULL);
  struct tm *tnow = localtime(&now_t);
  char date_str[8];
  unsigned dd = (unsigned)tnow->tm_mday, dm = (unsigned)(tnow->tm_mon+1);
  if (s_date_format == 0) snprintf(date_str, sizeof(date_str), "%02u-%02u", dd, dm);
  else                    snprintf(date_str, sizeof(date_str), "%02u-%02u", dm, dd);

  lcd_text(ctx, "88-88", date_str, f_date,
           GRect(x_l + SX(3), row_ty, date_w - SX(3), render_h),
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
    creal     = cgm_valid ? col_cgm : col_fg;
  }

  // Comp box border
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_rect(ctx, GRect(comp_x, y_dr, comp_w, dr_h));

  if (cgm_slot) {
    // CGM: 4-digit value + trend arrow; narrower arrow to fit 22px font
    int arw_w = SX(12);
    int num_w = comp_w - arw_w - SX(2);
    lcd_text(ctx, "8888", cstr, f_comp,
             GRect(comp_x+SX(1), row_ty, num_w, render_h_comp),
             col_ghost, creal, GTextAlignmentRight);
    if (cgm_valid) {
      draw_trend_arrow(ctx, s_cgm_trend[0],
                       GRect(comp_x+SX(1)+num_w, row_ty, arw_w, render_h_comp),
                       creal);
    }
  } else {
    lcd_text(ctx, "88888", cstr, f_comp,
             GRect(comp_x+SX(1), row_ty, comp_w-SX(2), render_h_comp),
             col_ghost, creal, GTextAlignmentRight);
  }

  // ── 9. Time HH:MM (DSEG14 52px, full LCD width) ───────────────────────
  char time_str[8];
  if (clock_is_24h_style()) {
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tnow->tm_hour, tnow->tm_min);
  } else {
    int hh = tnow->tm_hour % 12; if (!hh) hh = 12;
    snprintf(time_str, sizeof(time_str), "%02d:%02d", hh, tnow->tm_min);
  }
  {
    int tdy = y_time + (y_in_end - y_time - SY(42)) / 2;
    if (tdy < y_time) tdy = y_time;
    lcd_text(ctx, "88:88", time_str, f_dseg_lg,
             GRect(ix + SX(1), tdy, iw - SX(2), y_info - tdy),
             col_ghost, col_fg, GTextAlignmentLeft);
  }

  // ── 10+11. Info strip: BAT bar (left) | DOW letters (right) ──────────
  int info_top = y_info + SY(1);
  int info_h   = y_in_end - y_info - SY(2);
  if (info_h < 4) info_h = 4;
  int mid_x = ix + iw / 2;

  // Battery bar
  int bat_bx  = x_l;
  int bat_bw  = mid_x - x_l - SX(3);
  int bat_h   = (info_h * 3) / 5;
  if (bat_h < 3) bat_h = 3;
  int bat_top = info_top + (info_h - bat_h) / 2;
  int n_seg   = 6;
  int seg_gap = 2;
  int seg_w   = bat_bw > (n_seg+1)*seg_gap
                ? (bat_bw - (n_seg+1)*seg_gap) / n_seg : 2;
  if (seg_w < 2) seg_w = 2;
  if (seg_w > SX(8)) seg_w = SX(8);
  int lit = (s_batt_pct * n_seg + 50) / 100;
  if (lit > n_seg) lit = n_seg;
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_rect(ctx, GRect(bat_bx, bat_top, bat_bw, bat_h));
  for (int s = 0; s < n_seg; s++) {
    int sx = bat_bx + seg_gap + s*(seg_w+seg_gap);
    graphics_context_set_fill_color(ctx, s < lit ? col_fg : col_ghost);
    graphics_fill_rect(ctx, GRect(sx, bat_top+1, seg_w, bat_h-2), 0, GCornerNone);
  }

  // Day-of-week strip
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
  int wday_y = info_top - 2;  // shift weekday letters 2 px upward
  for (int i = 0; i < 7; i++) {
    int dw = SX(10);
    int dx = wday_x + i*day_step + (day_step - dw)/2;
    if (i == today_idx) {
      int r = 2; if (r > info_h/2) r = info_h/2; if (r > dw/2) r = dw/2;
      graphics_context_set_fill_color(ctx, col_fg);
      graphics_fill_rect(ctx, GRect(dx-1, wday_y, dw+2, info_h), r, GCornersAll);
      graphics_context_set_text_color(ctx, col_bg);
    } else {
      graphics_context_set_text_color(ctx, col_ghost);
    }
    graphics_draw_text(ctx, days_arr[i], f_tiny,
                       GRect(dx, wday_y, dw, info_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ── 12. CGM status + DOWN button label ───────────────────────────────
  {
    int cgs_h  = y_rs2 - y_cgs;
    int cgs_cy = y_cgs + cgs_h / 2;

    char cgs_txt[16];
    GColor cgs_col;
    if (strlen(s_ns_url) == 0) {
      snprintf(cgs_txt, sizeof(cgs_txt), "No URL");
      cgs_col = color_from_int(s_color_cgm_low);
    } else if (!cgm_fresh) {
      snprintf(cgs_txt, sizeof(cgs_txt), "CGM Offline");
      cgs_col = color_from_int(s_color_cgm_low);
    } else {
      snprintf(cgs_txt, sizeof(cgs_txt), "CGM Active");
      cgs_col = color_from_int(s_color_cgm_ok);
    }
    graphics_context_set_text_color(ctx, cgs_col);
    graphics_draw_text(ctx, cgs_txt, f_tiny,
                       GRect(SX(4), y_cgs, SX(68), cgs_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    if (cgm_fresh) {
      draw_trend_arrow(ctx, s_cgm_trend[0],
                       GRect(SX(74), y_cgs, SX(20), cgs_h), col_cgm);
    }

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "DOWN", f_lbl,
                       GRect(W-SX(4)-2*2-SX(34), y_cgs+(cgs_h-9)/2, SX(32), 9),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    draw_arrow(ctx, GPoint(W-SX(4), cgs_cy), 2, true, GColorWhite);
  }

  // ── 13. Bottom red band ───────────────────────────────────────────────
  draw_casio_band(ctx, y_rs2, y_ban - y_rs2, W, col_red);

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
  GS(KEY_CGM_VALUE,s_cgm_value); GS(KEY_CGM_DELTA,s_cgm_delta);
  GS(KEY_CGM_TREND,s_cgm_trend); GI(KEY_CGM_AGE,s_cgm_age);
  GI(KEY_STEPS,s_steps); GI(KEY_HR,s_hr);
  GI(KEY_WEATHER_TEMP,s_weather_temp); GS(KEY_WEATHER_ICON,s_weather_icon);
  GI(KEY_BATT_PCT,s_batt_pct);
#undef GS
#undef GI
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
#undef LS
#undef LI
}

// ── Window ────────────────────────────────────────────────────────────────
static void window_load(Window *w) {
  s_font_d14_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_TIME52));
  s_font_d7_date  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_DATE20));
  s_font_d7_comp  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_COMP22));

  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
  if (s_font_d14_time) { fonts_unload_custom_font(s_font_d14_time); s_font_d14_time = NULL; }
  if (s_font_d7_date)  { fonts_unload_custom_font(s_font_d7_date);  s_font_d7_date  = NULL; }
  if (s_font_d7_comp)  { fonts_unload_custom_font(s_font_d7_comp);  s_font_d7_comp  = NULL; }
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
  tick_timer_service_subscribe(SECOND_UNIT|MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(accel_tap_handler);
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
  battery_state_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
