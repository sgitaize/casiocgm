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
#define KEY_SHOW_SECONDS    20   // 0=show, 1=hide
#define KEY_CGM_VALUE       50
#define KEY_CGM_DELTA       51
#define KEY_CGM_TREND       52
#define KEY_CGM_AGE         53
#define KEY_STEPS           54
#define KEY_HR              55
#define KEY_WEATHER_TEMP    56
#define KEY_WEATHER_ICON    57
#define KEY_BATT_PCT        58

#define SHAKE_CYCLE_COUNT 6

// ── State ─────────────────────────────────────────────────────────────────
static Window   *s_window;
static Layer    *s_canvas;
static AppTimer *s_shake_timer;
static bool      s_shake_active = false;
static int       s_shake_slot   = 0;


static char s_ns_url[128]     = "";
static char s_ns_token[64]    = "";
static int  s_ns_units        = 0;
static int  s_ns_high         = 180;
static int  s_ns_low          = 70;
static int  s_ns_stale_min    = 10;

static int  s_color_bg        = 0xEEEEEE;
static int  s_color_fg        = 0x000044;
static int  s_color_accent    = 0xFFAA00;
static int  s_color_cgm_ok    = 0x005500;
static int  s_color_cgm_high  = 0xAA5500;
static int  s_color_cgm_low   = 0xAA0000;

static int  s_complication    = 0;
static char s_label_tl[32]    = "QUARTZ";
static char s_label_tr[32]    = "TIME 2";
static char s_label_bot[32]   = "Enabled";
static int  s_first_weekday   = 0;
static int  s_date_format     = 0;
static int  s_shake_2nd       = 0;  // secondary row shown on shake
static int  s_wday_lang       = 0;  // 0=EN, 1=DE
static int  s_show_seconds    = 0;  // 0=show, 1=hide

static char s_cgm_value[16]   = "---";
static char s_cgm_delta[16]   = "";
static char s_cgm_trend[8]    = "-";
static int  s_cgm_age         = 999;

static int  s_steps           = 0;
static int  s_hr              = 0;
static int  s_weather_temp    = 0;
static char s_weather_icon[8] = "";
static int  s_batt_pct        = 100;

// ── Helpers ───────────────────────────────────────────────────────────────
static GColor color_from_int(int v) {
#ifdef PBL_COLOR
  return GColorFromRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
#else
  (void)v; return GColorBlack;
#endif
}

// 67% bg + 33% fg – clearly visible "unlit" LCD segment ghost
static GColor ghost_color(void) {
#ifdef PBL_COLOR
  int br=(s_color_bg>>16)&0xFF, bg_=(s_color_bg>>8)&0xFF, bb=s_color_bg&0xFF;
  int fr=(s_color_fg>>16)&0xFF, fg_=(s_color_fg>>8)&0xFF, fb=s_color_fg&0xFF;
  return GColorFromRGB((br*2+fr)/3,(bg_*2+fg_)/3,(bb*2+fb)/3);
#else
  return GColorLightGray;
#endif
}

static void complication_str(int slot, char *buf, size_t len) {
  switch (slot) {
    case 0: {
      // CGM: just the numeric value (trend arrow is drawn separately)
      bool stale = (s_cgm_age > s_ns_stale_min) || (atoi(s_cgm_value) <= 0);
      if (stale) snprintf(buf, len, "----");
      else       snprintf(buf, len, "%s", s_cgm_value);
      break;
    }
    case 1: snprintf(buf, len, "%d", s_steps); break;
    case 2: snprintf(buf, len, s_hr > 0 ? "%d" : "----", s_hr); break;
    case 3: snprintf(buf, len, "%d", s_weather_temp); break;  // no unit – LECO compat
    case 4: snprintf(buf, len, "%d", s_batt_pct); break;      // no % – LECO compat
    case 5: {
      time_t n=time(NULL); struct tm *t=localtime(&n);
      unsigned d=(unsigned)t->tm_mday, m=(unsigned)(t->tm_mon+1);
      if (s_date_format==0) snprintf(buf,len,"%02u-%02u",d,m);
      else                  snprintf(buf,len,"%02u-%02u",m,d);
      break;
    }
    default: snprintf(buf, len, "-----");
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

// Draw a small arrow (◄ or ►) using lines only – no heap alloc in draw path.
// tip = the pointy vertex; half_h = half the base height; right = ► else ◄
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

// ── Battery callback (accurate, immediate updates) ───────────────────────
static void battery_state_handler(BatteryChargeState state) {
  s_batt_pct = (int)state.charge_percent;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── DRAW ──────────────────────────────────────────────────────────────────
//
// Y layout (ref H=168, W=144):
//   0..14    black label strip: QUARTZ (left) / TIME 2 (right)
//  14..32    TOP RED BAND (thin separator), GCornersBottom, radius=band_h/2
//  32..130   outer LCD rect (thick double-border)
//    36..125   inner LCD rect (border includes info strip)
//      38..62    date (LECO, centered) + comp box (LECO, right-aligned)
//      62..112   HH:MM (LECO_60 on emery, LECO_38 on color)
//                right col: :SS (hidden if s_show_seconds=1) + trend arrow
//     112..125   info strip: BAT bar (left) | DOW letters (right)
//             separator line at y_info
//  130..148  CGM status + DOWN ►
//  148..161  BOTTOM RED BAND, GCornersTop, radius=band_h/2
//  161..168  E-Paper banner: accent bg, label_bot text
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
  GColor col_acc   = color_from_int(s_color_accent);
  GColor col_ghost = ghost_color();
#ifdef PBL_COLOR
  GColor col_red = GColorRed;
#else
  GColor col_red = GColorBlack;
#endif

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
  GFont f_tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_sm   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#if defined(PBL_PLATFORM_EMERY)
  GFont f_dseg_lg = fonts_get_system_font(FONT_KEY_LECO_60_BOLD_NUMBERS_AM_PM);
  GFont f_dseg_sm = NULL; (void)f_dseg_sm;
  GFont f_comp    = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
  GFont f_date    = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
#elif defined(PBL_COLOR)
  GFont f_dseg_lg = fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS);
  GFont f_dseg_sm = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
  GFont f_comp    = fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  GFont f_date    = fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
#else
  GFont f_dseg_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_dseg_sm = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont f_comp    = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_date    = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#endif

  // ── Y anchors (ref H=168) ─────────────────────────────────────────────
  int y_rs1     = SY(14);   // top of red band (black label area above)
  int y_lcd     = SY(32);   // outer LCD rect top = bottom of top red band
  int y_in      = SY(36);   // inner LCD rect top
  int y_dr      = SY(38);   // date+comp row start
  int y_time    = SY(62);   // HH:MM start
  int y_info    = SY(112);  // info strip start (separator line here)
  int y_in_end  = SY(125);  // inner LCD bottom (info strip included)
  int y_lcd_end = SY(129);  // outer LCD bottom
  int y_cgs     = SY(129);  // CGM status area start
  int y_cgs2    = SY(141);  // CGM status line 2
  int y_rs2     = SY(148);  // bottom red band start
  int y_ban     = SY(161);  // E-Paper banner start

  // ── Radii ─────────────────────────────────────────────────────────────
  int lrad = SX(8);   // outer LCD rect corners
  int irad = SX(6);   // inner LCD rect corners

  // ── X anchors ─────────────────────────────────────────────────────────
  int lx = SX(1);
  int lw = W - SX(2);
  int ix = lx + SX(4);
  int iw = lw - SX(8);
  int x_l = ix + SX(1);        // inner content left
  int x_r = ix + iw - SX(1);   // inner content right

  // Comp box: fixed width at the right of the date row
  int comp_w = SX(68);
  int comp_x = x_r - comp_w;
  int date_w = comp_x - x_l - SX(2);

  // Right column (seconds, trend): only on non-emery platforms
#if !defined(PBL_PLATFORM_EMERY)
  int rcol_w = SX(30);
  int rcol_x = x_r - rcol_w;
  int time_w = rcol_x - x_l - SX(1);
#endif

  int tri_s = SY(3); if (tri_s < 2) tri_s = 2;

  // ── 1. Black background ───────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── 2. QUARTZ / TIME2 labels in black area above the band ────────────
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_label_tl, f_tiny,
                     GRect(SX(4), SY(1), SX(72), y_rs1-SY(1)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, col_acc);
  graphics_draw_text(ctx, s_label_tr, f_tiny,
                     GRect(W-SX(76), SY(1), SX(72), y_rs1-SY(1)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── 3. Top red band – thin, GCornersBottom (curves toward LCD) ───────
  //   Radius = half the band height → full semicircle on bottom corners.
  {
    int top_band_h = y_lcd - y_rs1;
    int srad = top_band_h / 2;
    // constraint: srad <= min(W, top_band_h) / 2  → already = top_band_h/2 ✓
    graphics_context_set_fill_color(ctx, col_red);
    graphics_fill_rect(ctx, GRect(0, y_rs1, W, top_band_h), srad, GCornersBottom);
  }

  // ── 4. LIGHT ◄  pebble  UP ► row inside the red band ─────────────────
  int pbl_cy = y_rs1 + (y_lcd - y_rs1) / 2;
  draw_arrow(ctx, GPoint(SX(4), pbl_cy), tri_s, false, GColorWhite);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "LIGHT", f_tiny,
                     GRect(SX(4)+tri_s*2+SX(2), y_rs1, SX(30), y_lcd-y_rs1),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, GColorYellow);
  graphics_draw_text(ctx, "pebble", f_sm,
                     GRect(SX(40), y_rs1-SY(1), SX(64), y_lcd-y_rs1+SY(2)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  int up_tri_x = W - SX(4) - tri_s*2;
  draw_arrow(ctx, GPoint(up_tri_x + tri_s*2, pbl_cy), tri_s, true, GColorWhite);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "UP", f_tiny,
                     GRect(W-SX(4)-tri_s*2-SX(22), y_rs1, SX(20), y_lcd-y_rs1),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── 5. Outer LCD: col_fg fill + thick col_bg inner fill ──────────────
  //   Border thickness = SX(4) ≈ 6px on emery for a clearly visible frame.
  int lh = y_lcd_end - y_lcd;
  int bd = SX(4);  // border thickness
  graphics_context_set_fill_color(ctx, col_fg);
  graphics_fill_rect(ctx, GRect(lx, y_lcd, lw, lh), lrad, GCornersAll);
  graphics_context_set_fill_color(ctx, col_bg);
  graphics_fill_rect(ctx, GRect(lx+bd, y_lcd+SY(4), lw-2*bd, lh-SY(8)),
                     lrad > bd ? lrad-bd : 0, GCornersAll);
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_round_rect(ctx, GRect(lx, y_lcd, lw, lh), lrad);

  // ── 6. Inner LCD border – now encloses the info strip too ────────────
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_round_rect(ctx, GRect(ix, y_in, iw, y_in_end-y_in), irad);
  // Horizontal separator between time area and info strip
  graphics_draw_line(ctx, GPoint(ix+1, y_info), GPoint(ix+iw-1, y_info));

  // ── 7. Date row (left of comp box) – or shake secondary row ──────────
  time_t now_t = time(NULL);
  struct tm *tnow = localtime(&now_t);
  char date_str[8];
  unsigned dd=(unsigned)tnow->tm_mday, dm=(unsigned)(tnow->tm_mon+1);
  if (s_date_format==0) snprintf(date_str,sizeof(date_str),"%02u-%02u",dd,dm);
  else                  snprintf(date_str,sizeof(date_str),"%02u-%02u",dm,dd);

  int dr_h = y_time - y_dr;

  if (s_shake_active && s_shake_2nd != 5) {
    char sh2[20] = "";
    switch (s_shake_2nd) {
      case 0: snprintf(sh2,sizeof(sh2),"%s",s_cgm_delta); break;
      case 1: snprintf(sh2,sizeof(sh2),"%d",s_steps); break;
      case 2: snprintf(sh2,sizeof(sh2),s_hr>0?"%d":"---",s_hr); break;
      case 3: snprintf(sh2,sizeof(sh2),"%d",s_batt_pct); break;
      case 4: snprintf(sh2,sizeof(sh2),"%d",s_weather_temp); break;
    }
    graphics_context_set_text_color(ctx, col_acc);
    graphics_draw_text(ctx, sh2, f_date,
                       GRect(x_l, y_dr, date_w, dr_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else {
    // Date: LECO font, centered in available space (not flush-left)
    lcd_text(ctx, "88-88", date_str, f_date,
             GRect(x_l, y_dr, date_w, dr_h),
             col_ghost, col_fg, GTextAlignmentCenter);
  }

  // ── 8. Comp box – LECO digits right-aligned; CGM = 4 digits + arrow ──
  int cslot = s_shake_active ? s_shake_slot : s_complication;
  char cstr[24];
  complication_str(cslot, cstr, sizeof(cstr));
  bool cgm_slot  = (cslot == 0);
  bool cgm_valid = cgm_slot && cgm_fresh;
  GColor creal   = cgm_valid ? col_cgm : col_fg;

  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_rect(ctx, GRect(comp_x, y_dr, comp_w, dr_h));

  if (cgm_slot) {
    // CGM mode: 4-digit number right-aligned + trend arrow at far right
    int arw_w  = SX(16);
    int num_w  = comp_w - arw_w - SX(2);
    lcd_text(ctx, "8888", cstr, f_comp,
             GRect(comp_x + SX(1), y_dr + SY(1), num_w, dr_h - SY(2)),
             col_ghost, creal, GTextAlignmentRight);
    const char *tarrow = (strlen(s_cgm_trend)==0||strcmp(s_cgm_trend,"-")==0)
                         ? "" : s_cgm_trend;
    graphics_context_set_text_color(ctx, creal);
    graphics_draw_text(ctx, tarrow, f_tiny,
                       GRect(comp_x + SX(1) + num_w, y_dr, arw_w, dr_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else {
    // All other slots: 5-digit area, right-aligned
    lcd_text(ctx, "88888", cstr, f_comp,
             GRect(comp_x + SX(1), y_dr + SY(1), comp_w - SX(2), dr_h - SY(2)),
             col_ghost, creal, GTextAlignmentRight);
  }

  // ── 9+10. Time HH:MM + seconds/trend ─────────────────────────────────
  char time_str[8];
  if (clock_is_24h_style()) {
    snprintf(time_str,sizeof(time_str),"%02d:%02d",tnow->tm_hour,tnow->tm_min);
  } else {
    int hh=tnow->tm_hour%12; if (!hh) hh=12;
    snprintf(time_str,sizeof(time_str),"%02d:%02d",hh,tnow->tm_min);
  }
  int time_h = y_info - y_time;

  const char *arrow = (strlen(s_cgm_trend)==0||strcmp(s_cgm_trend,"-")==0)
                      ? "-" : s_cgm_trend;

#if defined(PBL_PLATFORM_EMERY)
  {
    int full_tw = x_r - x_l - SX(1);
    lcd_text(ctx, "88:88", time_str, f_dseg_lg,
             GRect(x_l, y_time, full_tw, time_h),
             col_ghost, col_fg, GTextAlignmentLeft);
  }
#else
  lcd_text(ctx, "88:88", time_str, f_dseg_lg,
           GRect(x_l, y_time, time_w, time_h),
           col_ghost, col_fg, GTextAlignmentLeft);

  if (s_show_seconds == 0) {
    char sec_str[4];
    snprintf(sec_str,sizeof(sec_str),"%02d",tnow->tm_sec);
    int sec_h = SY(28);
    lcd_text(ctx, "88", sec_str, f_dseg_sm,
             GRect(rcol_x, y_time, rcol_w, sec_h),
             col_ghost, col_fg, GTextAlignmentCenter);

    int ry = y_time + sec_h + SY(1);
    if (!clock_is_24h_style()) {
      const char *ar = tnow->tm_hour<12 ? "AM" : "PM";
      const char *ag = tnow->tm_hour<12 ? "PM" : "AM";
      lcd_text(ctx, ag, ar, f_tiny,
               GRect(rcol_x, ry, rcol_w, SY(12)),
               col_ghost, col_fg, GTextAlignmentCenter);
      ry += SY(13);
    }
    if (y_info > ry) {
      graphics_context_set_text_color(ctx, col_cgm);
      graphics_draw_text(ctx, arrow, f_sm,
                         GRect(rcol_x, ry, rcol_w, y_info-ry),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  } else {
    // Seconds hidden: show trend arrow in right column, time uses full width
    graphics_context_set_text_color(ctx, col_cgm);
    graphics_draw_text(ctx, arrow, f_sm,
                       GRect(rcol_x, y_time, rcol_w, time_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
#endif

  // ── 11+12. Info strip: BAT bar (left) | DOW letters (right) ──────────
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
  int lit = (s_batt_pct * n_seg + 50) / 100;  // rounded, not truncated
  if (lit > n_seg) lit = n_seg;
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_rect(ctx, GRect(bat_bx, bat_top, bat_bw, bat_h));
  for (int s = 0; s < n_seg; s++) {
    int sx = bat_bx + seg_gap + s*(seg_w+seg_gap);
    graphics_context_set_fill_color(ctx, s < lit ? col_fg : col_ghost);
    graphics_fill_rect(ctx, GRect(sx, bat_top+1, seg_w, bat_h-2), 0, GCornerNone);
  }

  // Day of week – EN or DE depending on s_wday_lang
  static const char *D_SUN_EN[] = {"S","M","T","W","T","F","S"};
  static const char *D_MON_EN[] = {"M","T","W","T","F","S","S"};
  static const char *D_SUN_DE[] = {"S","M","D","M","D","F","S"};
  static const char *D_MON_DE[] = {"M","D","M","D","F","S","S"};
  const char **days_arr;
  if (s_wday_lang == 1) {
    days_arr = (s_first_weekday==1) ? D_MON_DE : D_SUN_DE;
  } else {
    days_arr = (s_first_weekday==1) ? D_MON_EN : D_SUN_EN;
  }
  int today_idx = tnow->tm_wday;
  if (s_first_weekday==1) today_idx = (today_idx+6)%7;
  int wday_x   = mid_x + SX(2);
  int wday_w   = x_r - wday_x;
  int day_step = wday_w / 7;
  for (int i = 0; i < 7; i++) {
    int dw = SX(10);
    int dx = wday_x + i*day_step + (day_step - dw)/2;
    if (i == today_idx) {
      int r = 2; if (r > info_h/2) r = info_h/2; if (r > dw/2) r = dw/2;
      graphics_context_set_fill_color(ctx, col_fg);
      graphics_fill_rect(ctx, GRect(dx-1, info_top, dw+2, info_h), r, GCornersAll);
      graphics_context_set_text_color(ctx, col_bg);
    } else {
      graphics_context_set_text_color(ctx, col_ghost);
    }
    graphics_draw_text(ctx, days_arr[i], f_tiny,
                       GRect(dx, info_top, dw, info_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ── 13. CGM status – 2 lines + trend arrow + DOWN ► ──────────────────
  int cgs_cy     = y_cgs + (y_rs2-y_cgs)/2;
  int cgs_text_w = W - SX(76);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "CGM", f_tiny,
                     GRect(SX(4), y_cgs, cgs_text_w, SY(13)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  char cgs2[32];
  GColor cgs_col;
  if (strlen(s_ns_url) == 0) {
    snprintf(cgs2, sizeof(cgs2), "No URL");
    cgs_col = color_from_int(s_color_cgm_low);
  } else if (!cgm_fresh) {
    snprintf(cgs2, sizeof(cgs2), "offline");
    cgs_col = color_from_int(s_color_cgm_low);
  } else {
    snprintf(cgs2, sizeof(cgs2), "Active");
    cgs_col = color_from_int(s_color_cgm_ok);
  }
  graphics_context_set_text_color(ctx, cgs_col);
  graphics_draw_text(ctx, cgs2, f_tiny,
                     GRect(SX(4), y_cgs2, cgs_text_w, SY(13)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  graphics_context_set_text_color(ctx, col_cgm);
  graphics_draw_text(ctx, arrow, f_sm,
                     GRect(W-SX(62), y_cgs, SX(20), y_rs2-y_cgs),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  draw_arrow(ctx, GPoint(W-SX(4)-tri_s*2+tri_s*2, cgs_cy), tri_s, true, GColorWhite);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "DOWN", f_tiny,
                     GRect(W-SX(4)-tri_s*2-SX(34), y_cgs, SX(32), y_rs2-y_cgs),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── 14. Bottom red band – thin, GCornersTop (curves toward LCD) ───────
  {
    int bot_h = y_ban - y_rs2;
    int brad  = bot_h / 2;  // = half band height → full semicircle on top corners
    graphics_context_set_fill_color(ctx, col_red);
    graphics_fill_rect(ctx, GRect(0, y_rs2, W, bot_h), brad, GCornersTop);
  }

  // ── 15. E-Paper banner ────────────────────────────────────────────────
  int ban_h = H - y_ban;
  graphics_context_set_fill_color(ctx, col_acc);
  graphics_fill_rect(ctx, GRect(0, y_ban, W, ban_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  const char *ban_text = (strlen(s_label_bot) > 0) ? s_label_bot : "E-PAPER DISPLAY";
  graphics_draw_text(ctx, ban_text, f_tiny,
                     GRect(SX(4), y_ban, W-SX(8), ban_h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // ── 16. Case seam lines (color only) ─────────────────────────────────
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, y_rs1,   W, 1), 0, GCornerNone);  // top of red band
  graphics_fill_rect(ctx, GRect(0, y_ban-1, W, 1), 0, GCornerNone);  // bottom of bottom band
#endif

#undef SY
#undef SX
}

// ── Shake ─────────────────────────────────────────────────────────────────
static void shake_timer_cb(void *ctx) {
  s_shake_active = false; s_shake_timer = NULL;
  if (s_canvas) layer_mark_dirty(s_canvas);
}
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (!s_shake_active) { s_shake_slot=(s_complication+1)%SHAKE_CYCLE_COUNT; s_shake_active=true; }
  else                   s_shake_slot=(s_shake_slot+1)%SHAKE_CYCLE_COUNT;
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
  GI(KEY_WDAY_LANG,s_wday_lang); GI(KEY_SHOW_SECONDS,s_show_seconds);
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
  LI(KEY_WDAY_LANG,s_wday_lang); LI(KEY_SHOW_SECONDS,s_show_seconds);
#undef LS
#undef LI
}

// ── Window ────────────────────────────────────────────────────────────────
static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

// ── App lifecycle ─────────────────────────────────────────────────────────
static void init(void) {
  load_persist();
  // Accurate battery reading: subscribe for immediate updates + read current value
  battery_state_service_subscribe(battery_state_handler);
  s_batt_pct = (int)battery_state_service_peek().charge_percent;
  s_window = window_create();
  window_set_window_handlers(s_window,(WindowHandlers){
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
