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

// Custom DSEG fonts – loaded in window_load, freed in window_unload
static GFont s_f_dseg_lg = NULL;  // DSEG_30_BOLD – main time HH:MM
static GFont s_f_dseg_sm = NULL;  // DSEG_25_BOLD – comp box, date, seconds

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
      bool stale = (s_cgm_age > s_ns_stale_min) || (atoi(s_cgm_value) <= 0);
      snprintf(buf, len, stale ? "-----" : "%s", s_cgm_value);
      break;
    }
    case 1: snprintf(buf, len, "%d", s_steps); break;
    case 2: snprintf(buf, len, s_hr > 0 ? "%d" : "-----", s_hr); break;
    case 3: snprintf(buf, len, "%d%s", s_weather_temp, s_ns_units ? "C" : "F"); break;
    case 4: snprintf(buf, len, "%d%%", s_batt_pct); break;
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

static void draw_tri(GContext *ctx, GPoint a, GPoint b, GPoint c, GColor col) {
  GPoint pts[3] = {a, b, c};
  GPathInfo info = {.num_points = 3, .points = pts};
  GPath *p = gpath_create(&info);
  if (!p) return;
  graphics_context_set_fill_color(ctx, col);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// ── DRAW ──────────────────────────────────────────────────────────────────
//
// Y layout (ref H=168, W=144):
//  0..10    labels: QUARTZ / TIME 2
//  10..17   top red stripe (GCornersTop, stripe_rad=SX(20), pill arc)
//  17..33   LIGHT ◄  pebble  UP ►
//  33..127  outer LCD rect (lrad=SX(8), double-border with inner at 37..123)
//    39..67   date (no border, DSEG_25) side-by-side with comp box (border, DSEG_25)
//    67..101  HH:MM (DSEG_30) + right col: :SS (DSEG_25) + trend arrow
//    101..111 battery segments (full width, col_fg bars)
//    111..123 weekday strip (today filled, others ghost)
//  127..152 CGM status: line1="CGM" line2=status + trend arrow + DOWN ►
//  152..159 bottom red stripe (GCornersBottom, stripe_rad, opposite arc to top)
//  159..168 E-Paper banner: accent bg, configurable text
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
  GFont f_tiny    = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_sm      = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_dseg_lg = s_f_dseg_lg ? s_f_dseg_lg
                                 : fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont f_dseg_sm = s_f_dseg_sm ? s_f_dseg_sm
                                 : fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);

  // ── Y anchors (ref H=168) ─────────────────────────────────────────────
  int y_rs1     = SY(10);
  int y_pbl     = SY(17);
  int y_lcd     = SY(33);
  int y_in      = SY(37);
  int y_dr      = SY(39);   // date+comp row start
  int y_time    = SY(67);   // HH:MM start
  int y_bat     = SY(101);  // battery start
  int y_wday    = SY(111);  // weekday start
  int y_in_end  = SY(123);  // inner LCD bottom border
  int y_lcd_end = SY(127);  // outer LCD bottom
  int y_cgs     = SY(127);  // CGM status area start
  int y_cgs2    = SY(140);  // CGM status line 2
  int y_rs2     = SY(152);  // bottom stripe start
  int y_ban     = SY(159);  // E-Paper banner start

  // ── Radii ─────────────────────────────────────────────────────────────
  int stripe_rad = SX(20);  // pronounced pill arc for both red stripes
  int lrad       = SX(8);   // outer LCD rect corners
  int irad       = SX(6);   // inner LCD rect corners

  // ── X anchors ─────────────────────────────────────────────────────────
  int lx     = SX(1);
  int lw     = W - SX(2);
  int ix     = lx + SX(4);
  int iw     = lw - SX(8);
  int x_l    = ix + SX(1);        // inner content left
  int x_r    = ix + iw - SX(1);   // inner content right

  // Comp box: right-aligned in date row, width for 5 DSEG_25 chars + border
  int comp_w = SX(68);
  int comp_x = x_r - comp_w;
  int date_w = comp_x - x_l - SX(2);  // date fills left of comp box

  // Right column (seconds, trend): alongside the time digits
  int rcol_w = SX(30);
  int rcol_x = x_r - rcol_w;
  int time_w = rcol_x - x_l - SX(1);

  int tri_s = SY(3); if (tri_s < 2) tri_s = 2;

  // ── 1. Black background ───────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── 2. Top labels: QUARTZ (white) + TIME 2 (accent) ──────────────────
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_label_tl, f_tiny,
                     GRect(SX(4), SY(0), SX(72), SY(10)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, col_acc);
  graphics_draw_text(ctx, s_label_tr, f_tiny,
                     GRect(W-SX(76), SY(0), SX(72), SY(10)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── 3. Top red stripe – GCornersTop, pronounced pill arc ─────────────
  graphics_context_set_fill_color(ctx, col_red);
  graphics_fill_rect(ctx, GRect(lx, y_rs1, lw, y_pbl-y_rs1), stripe_rad, GCornersTop);

  // ── 4. LIGHT ◄  pebble  UP ► row ─────────────────────────────────────
  int pbl_cy = y_pbl + (y_lcd - y_pbl) / 2;

  draw_tri(ctx,
           GPoint(SX(4)+tri_s*2, pbl_cy-tri_s),
           GPoint(SX(4)+tri_s*2, pbl_cy+tri_s),
           GPoint(SX(4),          pbl_cy),
           GColorWhite);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "LIGHT", f_tiny,
                     GRect(SX(4)+tri_s*2+SX(2), y_pbl, SX(30), y_lcd-y_pbl),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  graphics_context_set_text_color(ctx, col_acc);
  graphics_draw_text(ctx, "pebble", f_sm,
                     GRect(SX(40), y_pbl-SY(1), SX(64), y_lcd-y_pbl+SY(2)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  int up_tri_x = W - SX(4) - tri_s*2;
  draw_tri(ctx,
           GPoint(up_tri_x,         pbl_cy-tri_s),
           GPoint(up_tri_x,         pbl_cy+tri_s),
           GPoint(up_tri_x+tri_s*2, pbl_cy),
           GColorWhite);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "UP", f_tiny,
                     GRect(W-SX(4)-tri_s*2-SX(22), y_pbl, SX(20), y_lcd-y_pbl),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── 5. Outer LCD: fill with bg color + outer border ───────────────────
  int lh = y_lcd_end - y_lcd;
  graphics_context_set_fill_color(ctx, col_bg);
  graphics_fill_rect(ctx, GRect(lx, y_lcd, lw, lh), lrad, GCornersAll);
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_round_rect(ctx, GRect(lx, y_lcd, lw, lh), lrad);

  // ── 6. Inner LCD: border only ─────────────────────────────────────────
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_round_rect(ctx, GRect(ix, y_in, iw, y_in_end-y_in), irad);

  // ── 7. Date text (DSEG_25, no border, left of comp box) ───────────────
  time_t now_t = time(NULL);
  struct tm *tnow = localtime(&now_t);
  char date_str[8];
  unsigned dd=(unsigned)tnow->tm_mday, dm=(unsigned)(tnow->tm_mon+1);
  if (s_date_format==0) snprintf(date_str,sizeof(date_str),"%02u-%02u",dd,dm);
  else                  snprintf(date_str,sizeof(date_str),"%02u-%02u",dm,dd);

  int dr_h = y_time - y_dr;
  lcd_text(ctx, "88-88", date_str, f_dseg_sm,
           GRect(x_l, y_dr, date_w, dr_h),
           col_ghost, col_fg, GTextAlignmentLeft);

  // ── 8. Comp box (DSEG_25, WITH border, right-aligned in date row) ─────
  int cslot = s_shake_active ? s_shake_slot : s_complication;
  char cstr[24];
  complication_str(cslot, cstr, sizeof(cstr));
  bool cgm_valid = (cslot==0) && cgm_fresh;
  GColor creal = cgm_valid ? col_cgm : col_fg;

  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_rect(ctx, GRect(comp_x, y_dr, comp_w, dr_h));
  lcd_text(ctx, "88888", cstr, f_dseg_sm,
           GRect(comp_x+SX(1), y_dr+SY(1), comp_w-SX(2), dr_h-SY(2)),
           col_ghost, creal, GTextAlignmentCenter);

  // ── 9. Time HH:MM (DSEG_30, large, left portion) ─────────────────────
  char time_str[8];
  if (clock_is_24h_style()) {
    snprintf(time_str,sizeof(time_str),"%02d:%02d",tnow->tm_hour,tnow->tm_min);
  } else {
    int hh=tnow->tm_hour%12; if (!hh) hh=12;
    snprintf(time_str,sizeof(time_str),"%02d:%02d",hh,tnow->tm_min);
  }
  int time_h = y_bat - y_time;
  lcd_text(ctx, "88:88", time_str, f_dseg_lg,
           GRect(x_l, y_time, time_w, time_h),
           col_ghost, col_fg, GTextAlignmentLeft);

  // ── 10. Right column: :SS seconds (DSEG_25) + trend ──────────────────
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

  const char *arrow = (strlen(s_cgm_trend)==0||strcmp(s_cgm_trend,"-")==0)
                      ? "\xe2\x80\x93" : s_cgm_trend;
  graphics_context_set_text_color(ctx, col_cgm);
  graphics_draw_text(ctx, arrow, f_sm,
                     GRect(rcol_x, ry, rcol_w, y_bat-ry),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // ── 11. Battery – segmented bar (col_fg dark bars, ghost = empty) ─────
  int n_seg  = 8;
  int bat_bx = x_l;
  int bat_bw = x_r - x_l;
  int bat_bh = y_wday - y_bat - SY(1);
  if (bat_bh < 3) bat_bh = 3;
  int bat_y  = y_bat + SY(1);
  int seg_gap = 1;
  int seg_w  = (bat_bw - (n_seg+1)*seg_gap) / n_seg;
  int lit    = s_batt_pct * n_seg / 100;

  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_rect(ctx, GRect(bat_bx, bat_y, bat_bw, bat_bh));
  for (int s = 0; s < n_seg; s++) {
    int sx = bat_bx + seg_gap + s*(seg_w+seg_gap);
    graphics_context_set_fill_color(ctx, s<lit ? col_fg : col_ghost);
    graphics_fill_rect(ctx, GRect(sx, bat_y+1, seg_w, bat_bh-2), 0, GCornerNone);
  }

  // ── 12. Weekday strip (inside inner LCD, below battery) ───────────────
  static const char *D_SUN[] = {"S","M","T","W","T","F","S"};
  static const char *D_MON[] = {"M","T","W","T","F","S","S"};
  const char **days_arr = (s_first_weekday==1) ? D_MON : D_SUN;
  int today_idx = tnow->tm_wday;
  if (s_first_weekday==1) today_idx = (today_idx+6)%7;

  int wday_h    = y_in_end - y_wday - SY(1);
  int day_step  = iw / 7;
  for (int i = 0; i < 7; i++) {
    int dx = ix + i*day_step + (day_step - SX(10))/2;
    int dy = y_wday + SY(1);
    if (i == today_idx) {
      graphics_context_set_fill_color(ctx, col_fg);
      graphics_fill_rect(ctx, GRect(dx-SX(1), dy, SX(12), wday_h), SX(2), GCornersAll);
      graphics_context_set_text_color(ctx, col_bg);
    } else {
      graphics_context_set_text_color(ctx, col_ghost);
    }
    graphics_draw_text(ctx, days_arr[i], f_tiny,
                       GRect(dx, dy, SX(10), wday_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ── 13. CGM status – 2 lines + trend arrow + DOWN ► ─────────────────
  int cgs_cy     = y_cgs + (y_rs2-y_cgs)/2;
  int cgs_text_w = W - SX(76);

  // Line 1: "CGM" in white
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "CGM", f_tiny,
                     GRect(SX(4), y_cgs, cgs_text_w, SY(13)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Line 2: status string in appropriate color
  char cgs2[32];
  GColor cgs_col;
  if (strlen(s_ns_url) == 0) {
    snprintf(cgs2, sizeof(cgs2), "No URL");
    cgs_col = color_from_int(s_color_cgm_low);
  } else if (!cgm_fresh) {
    snprintf(cgs2, sizeof(cgs2), "offline");
    cgs_col = color_from_int(s_color_cgm_low);
  } else {
    snprintf(cgs2, sizeof(cgs2), "%s", s_label_bot);
    cgs_col = color_from_int(s_color_cgm_ok);
  }
  graphics_context_set_text_color(ctx, cgs_col);
  graphics_draw_text(ctx, cgs2, f_tiny,
                     GRect(SX(4), y_cgs2, cgs_text_w, SY(13)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Trend arrow (center of CGM row)
  graphics_context_set_text_color(ctx, col_cgm);
  graphics_draw_text(ctx, arrow, f_sm,
                     GRect(W-SX(62), y_cgs, SX(20), y_rs2-y_cgs),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // DOWN + right triangle
  int dtri_x = W - SX(4) - tri_s*2;
  draw_tri(ctx,
           GPoint(dtri_x,          cgs_cy-tri_s),
           GPoint(dtri_x,          cgs_cy+tri_s),
           GPoint(dtri_x+tri_s*2,  cgs_cy),
           GColorWhite);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "DOWN", f_tiny,
                     GRect(W-SX(4)-tri_s*2-SX(34), y_cgs, SX(32), y_rs2-y_cgs),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── 14. Bottom red stripe – GCornersBottom, same pill arc as top ──────
  graphics_context_set_fill_color(ctx, col_red);
  graphics_fill_rect(ctx, GRect(lx, y_rs2, lw, y_ban-y_rs2), stripe_rad, GCornersBottom);

  // ── 15. E-Paper banner – accent bg, one configurable text line ────────
  int ban_h = H - y_ban;
  graphics_context_set_fill_color(ctx, col_acc);
  graphics_fill_rect(ctx, GRect(0, y_ban, W, ban_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_label_bot, f_tiny,
                     GRect(SX(4), y_ban, W-SX(8), ban_h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

#undef SY
#undef SX
}

// ── Shake ─────────────────────────────────────────────────────────────────
static void shake_timer_cb(void *ctx) {
  s_shake_active = false; s_shake_timer = NULL;
  layer_mark_dirty(s_canvas);
}
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (!s_shake_active) { s_shake_slot=(s_complication+1)%SHAKE_CYCLE_COUNT; s_shake_active=true; }
  else                   s_shake_slot=(s_shake_slot+1)%SHAKE_CYCLE_COUNT;
  if (s_shake_timer) app_timer_cancel(s_shake_timer);
  s_shake_timer = app_timer_register(5000, shake_timer_cb, NULL);
  layer_mark_dirty(s_canvas);
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
  layer_mark_dirty(s_canvas);
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
  layer_mark_dirty(s_canvas);
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
#undef LS
#undef LI
}

// ── Window ────────────────────────────────────────────────────────────────
static void window_load(Window *w) {
  s_f_dseg_lg = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_DSEG_30_BOLD));
  s_f_dseg_sm = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_DSEG_25_BOLD));

  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) {
  layer_destroy(s_canvas);
  if (s_f_dseg_lg) { fonts_unload_custom_font(s_f_dseg_lg); s_f_dseg_lg = NULL; }
  if (s_f_dseg_sm) { fonts_unload_custom_font(s_f_dseg_sm); s_f_dseg_sm = NULL; }
}

// ── App lifecycle ─────────────────────────────────────────────────────────
static void init(void) {
  load_persist();
  s_window = window_create();
  window_set_window_handlers(s_window,(WindowHandlers){
    .load   = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(SECOND_UNIT|MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(accel_tap_handler);
  app_message_open(512, 64);
  app_message_register_inbox_received(inbox_received);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
