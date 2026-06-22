#include <pebble.h>
#include <limits.h>

#define MAX_TEMPS         240
#define TITLE_HEIGHT      16
#define CLOUD_HEIGHT      12
#define TLABEL_HEIGHT     14
#define BOTTOM_PAD        -1
#define SETTINGS_KEY      1

/* ---------- settings (persisted) ---------- */

typedef struct {
  int version;
  int temp_unit;         /* 0=Celsius, 1=Fahrenheit */
  int wind_unit;         /* 0=m/s, 1=km/h, 2=mph */
  int precip_unit;       /* 0=mm, 1=inch */
  uint8_t show_cloud;
  uint8_t show_precip;
  uint8_t show_humidity;
  uint8_t show_wind;
  uint8_t show_uv;
  uint8_t show_dawn_dusk;
  uint8_t show_golden_hour;
  uint8_t show_darkness;
} AppSettings;

#define SETTINGS_VERSION 2

static AppSettings s_settings = {
  .version = SETTINGS_VERSION,
  .temp_unit = 0, .wind_unit = 0, .precip_unit = 0,
  .show_cloud = 1, .show_precip = 1, .show_humidity = 1,
  .show_wind = 1, .show_uv = 1,
  .show_dawn_dusk = 1, .show_golden_hour = 1, .show_darkness = 1,
};

static void prv_load_settings(void) {
  AppSettings loaded = {0};
  if (persist_read_data(SETTINGS_KEY, &loaded, sizeof(loaded)) > 0 &&
      loaded.version == SETTINGS_VERSION) {
    s_settings = loaded;
  }
}

static void prv_save_settings(void) {
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

/* ---------- state ---------- */

typedef enum { STATUS_LOADING, STATUS_READY, STATUS_ERROR } AppStatus;

static Window    *s_window;
static Layer     *s_graph_layer;
static TextLayer *s_status_layer;

static AppStatus  s_status           = STATUS_LOADING;
static int8_t     s_temps[MAX_TEMPS];
static uint8_t    s_precip[MAX_TEMPS];
static uint8_t    s_wind_speed[MAX_TEMPS];  /* whole m/s, 255=NaN */
static uint8_t    s_wind_dir[MAX_TEMPS];    /* dir/360*254, 255=NaN */
static uint8_t    s_wind_gust[MAX_TEMPS];   /* whole m/s, 255=NaN */
static uint8_t    s_cloud[MAX_TEMPS];       /* 0-100%, 255=NaN */
static uint8_t    s_sun_cond[MAX_TEMPS];    /* 0=normal, 1=golden, 2=dark */
static uint8_t    s_uv[MAX_TEMPS];          /* 0-16, 255=NaN */
static uint8_t    s_humidity[MAX_TEMPS];    /* 0-100%, 255=NaN */
static int        s_temp_count       = 0;
static int        s_precip_count     = 0;
static int        s_wind_count       = 0;
static int        s_wind_gust_count  = 0;
static int        s_cloud_count      = 0;
static int        s_sun_count        = 0;
static int        s_uv_count         = 0;
static int        s_humidity_count   = 0;
static int        s_current_idx      = 0;
static int        s_current_min      = 0;
static int        s_local_start_h    = 0;
static int        s_local_start_wday = 0;  /* JS getDay(): 0=Sun */
static int        s_local_start_day  = 1;  /* day of month */
static int        s_local_start_mon  = 1;  /* month 1-12 */
static char       s_location[33]     = "";
static int        s_zoom_days        = 5;  /* 1 or 5 */
static int        s_view_count       = 120; /* animated: current view width in hours */
static Animation *s_zoom_anim        = NULL;
static int        s_scroll_offset    = 0;  /* hours scrolled forward */
static int        s_scroll_target    = 0;
static int        s_scroll_anim_start = 0;
static bool       s_animating        = false;  /* unused, kept for future use */
static Animation *s_scroll_anim      = NULL;

static GPath     *s_wind_arrow       = NULL;

#define MAX_DAYS 12
static int s_day_count = 0;
static int s_day_min_val[MAX_DAYS];
static int s_day_max_val[MAX_DAYS];
static int s_day_min_abs[MAX_DAYS];  /* absolute index into s_temps */
static int s_day_max_abs[MAX_DAYS];
static int s_day_abs_start[MAX_DAYS];
static int s_day_abs_end[MAX_DAYS];

/* ---------- helpers ---------- */

/* Compute per-calendar-day min/max from fixed midnight-aligned grid. Called once after data loads.
   Min = coldest in night (hours 0-7), Max = warmest in daytime (hours 8-19). Independent. */
static void prv_compute_daily_stats(void) {
  s_day_count = 0;
  if (s_temp_count < 2) return;
  int first_midnight = (24 - s_local_start_h) % 24;
  int seg_start = 0;
  int seg_len = (first_midnight == 0) ? 24 : first_midnight;
  while (seg_start < s_temp_count && s_day_count < MAX_DAYS) {
    int seg_end = seg_start + seg_len - 1;
    if (seg_end >= s_temp_count) seg_end = s_temp_count - 1;
    if (seg_end > seg_start) {  /* need at least 2 hours */
      /* Night range: hours 0..7 of the day */
      int night_end = seg_start + 7;
      if (night_end > seg_end) night_end = seg_end;
      int min_val = INT_MAX, min_abs = -1;
      for (int i = seg_start; i <= night_end; i++) {
        int t = (int)s_temps[i];
        if (t < min_val) { min_val = t; min_abs = i; }
      }
      /* Daytime range: hours 8..23 of the day */
      int day_s = seg_start + 8;
      int day_e = seg_start + 23;
      if (day_e > seg_end) day_e = seg_end;
      int max_val = INT_MIN, max_abs = -1;
      if (day_s <= seg_end) {
        for (int i = day_s; i <= day_e; i++) {
          int t = (int)s_temps[i];
          if (t > max_val) { max_val = t; max_abs = i; }
        }
      }
      s_day_min_val[s_day_count] = min_val;
      s_day_max_val[s_day_count] = max_val;
      s_day_min_abs[s_day_count] = min_abs;
      s_day_max_abs[s_day_count] = max_abs;
      s_day_abs_start[s_day_count] = seg_start;
      s_day_abs_end[s_day_count] = seg_end;
      s_day_count++;
    }
    seg_start = seg_end + 1;
    seg_len = 24;
  }
}

static void prv_request_data(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int8(iter, MESSAGE_KEY_REQUEST_DATA, 1);
  app_message_outbox_send();
}

static void prv_update_status_layer(void) {
  bool show = (s_status != STATUS_READY);
  layer_set_hidden(text_layer_get_layer(s_status_layer), !show);
  if (s_status == STATUS_ERROR) {
    text_layer_set_text(s_status_layer, "Could not load\nforecast.");
  } else {
    text_layer_set_text(s_status_layer, "Loading\nforecast...");
  }
}

/* ---------- AppMessage ---------- */

static void prv_inbox_received(DictionaryIterator *iter, void *ctx) {
  /* Handle Clay config settings (sent without STATUS) */
  Tuple *unit_t = dict_find(iter, MESSAGE_KEY_TEMP_UNIT);
  if (unit_t) {
    if (unit_t->type == TUPLE_CSTRING) {
      s_settings.temp_unit = atoi(unit_t->value->cstring);
    } else {
      s_settings.temp_unit = (int)unit_t->value->int32;
    }
    prv_save_settings();
    layer_mark_dirty(s_graph_layer);
  }
  Tuple *wu_t = dict_find(iter, MESSAGE_KEY_WIND_UNIT);
  if (wu_t) {
    s_settings.wind_unit = (wu_t->type == TUPLE_CSTRING)
      ? atoi(wu_t->value->cstring) : (int)wu_t->value->int32;
    prv_save_settings();
    layer_mark_dirty(s_graph_layer);
  }
  Tuple *pu_t = dict_find(iter, MESSAGE_KEY_PRECIP_UNIT);
  if (pu_t) {
    s_settings.precip_unit = (pu_t->type == TUPLE_CSTRING)
      ? atoi(pu_t->value->cstring) : (int)pu_t->value->int32;
    prv_save_settings();
    layer_mark_dirty(s_graph_layer);
  }

#define HANDLE_TOGGLE(key, field) do { \
  Tuple *_t = dict_find(iter, MESSAGE_KEY_##key); \
  if (_t) { \
    s_settings.field = (_t->type == TUPLE_CSTRING) \
      ? (uint8_t)atoi(_t->value->cstring) : (uint8_t)_t->value->int32; \
    prv_save_settings(); \
    layer_mark_dirty(s_graph_layer); \
  } \
} while(0)
  HANDLE_TOGGLE(SHOW_CLOUD,       show_cloud);
  HANDLE_TOGGLE(SHOW_PRECIP,      show_precip);
  HANDLE_TOGGLE(SHOW_HUMIDITY,    show_humidity);
  HANDLE_TOGGLE(SHOW_WIND,        show_wind);
  HANDLE_TOGGLE(SHOW_UV,          show_uv);
  HANDLE_TOGGLE(SHOW_DAWN_DUSK,   show_dawn_dusk);
  HANDLE_TOGGLE(SHOW_GOLDEN_HOUR, show_golden_hour);
  HANDLE_TOGGLE(SHOW_DARKNESS,    show_darkness);
#undef HANDLE_TOGGLE

  /* UV index may arrive as a separate follow-up message (no STATUS) */
  Tuple *uv_followup_t = dict_find(iter, MESSAGE_KEY_UV_INDEX);
  if (uv_followup_t && uv_followup_t->type == TUPLE_BYTE_ARRAY && !dict_find(iter, MESSAGE_KEY_STATUS)) {
    int n = (int)uv_followup_t->length < MAX_TEMPS ? (int)uv_followup_t->length : MAX_TEMPS;
    s_uv_count = n;
    const uint8_t *raw = (const uint8_t *)uv_followup_t->value->data;
    for (int i = 0; i < n; i++) s_uv[i] = raw[i];
    layer_mark_dirty(s_graph_layer);
  }

  Tuple *status_t = dict_find(iter, MESSAGE_KEY_STATUS);
  if (!status_t) return;

  s_status = (AppStatus)status_t->value->int32;

  if (s_status == STATUS_READY) {
    /* Reset optional component counts; only set if keys are present in message */
    s_wind_count = 0;
    s_wind_gust_count = 0;
    s_cloud_count = 0;
    s_sun_count = 0;
    s_humidity_count = 0;

    Tuple *temps_t  = dict_find(iter, MESSAGE_KEY_TEMPERATURES);
    Tuple *precip_t = dict_find(iter, MESSAGE_KEY_PRECIPITATION);
    Tuple *wspd_t   = dict_find(iter, MESSAGE_KEY_WIND_SPEED);
    Tuple *wdir_t   = dict_find(iter, MESSAGE_KEY_WIND_DIRECTION);
    Tuple *idx_t    = dict_find(iter, MESSAGE_KEY_CURRENT_INDEX);
    Tuple *sh_t     = dict_find(iter, MESSAGE_KEY_LOCAL_START_HOUR);
    Tuple *wd_t     = dict_find(iter, MESSAGE_KEY_LOCAL_START_WEEKDAY);
    Tuple *loc_t    = dict_find(iter, MESSAGE_KEY_LOCATION_NAME);

    if (temps_t && temps_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)temps_t->length < MAX_TEMPS ? (int)temps_t->length : MAX_TEMPS;
      s_temp_count = n;
      const uint8_t *raw = (const uint8_t *)temps_t->value->data;
      for (int i = 0; i < n; i++) s_temps[i] = (int8_t)raw[i];
    }
    if (precip_t && precip_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)precip_t->length < MAX_TEMPS ? (int)precip_t->length : MAX_TEMPS;
      s_precip_count = n;
      const uint8_t *raw = (const uint8_t *)precip_t->value->data;
      for (int i = 0; i < n; i++) s_precip[i] = raw[i];
    }
    if (wspd_t && wspd_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)wspd_t->length < MAX_TEMPS ? (int)wspd_t->length : MAX_TEMPS;
      s_wind_count = n;
      const uint8_t *raw = (const uint8_t *)wspd_t->value->data;
      for (int i = 0; i < n; i++) s_wind_speed[i] = raw[i];
    }
    if (wdir_t && wdir_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)wdir_t->length < MAX_TEMPS ? (int)wdir_t->length : MAX_TEMPS;
      if (n > s_wind_count) s_wind_count = n;
      const uint8_t *raw = (const uint8_t *)wdir_t->value->data;
      for (int i = 0; i < n; i++) s_wind_dir[i] = raw[i];
    }
    Tuple *cloud_t = dict_find(iter, MESSAGE_KEY_CLOUD_COVER);
    if (cloud_t && cloud_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)cloud_t->length < MAX_TEMPS ? (int)cloud_t->length : MAX_TEMPS;
      s_cloud_count = n;
      const uint8_t *raw = (const uint8_t *)cloud_t->value->data;
      for (int i = 0; i < n; i++) s_cloud[i] = raw[i];
    }
    Tuple *sun_t = dict_find(iter, MESSAGE_KEY_SUN_CONDITION);
    if (sun_t && sun_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)sun_t->length < MAX_TEMPS ? (int)sun_t->length : MAX_TEMPS;
      s_sun_count = n;
      const uint8_t *raw = (const uint8_t *)sun_t->value->data;
      for (int i = 0; i < n; i++) s_sun_cond[i] = raw[i];
    }
    Tuple *uv_t = dict_find(iter, MESSAGE_KEY_UV_INDEX);
    if (uv_t && uv_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)uv_t->length < MAX_TEMPS ? (int)uv_t->length : MAX_TEMPS;
      s_uv_count = n;
      const uint8_t *raw = (const uint8_t *)uv_t->value->data;
      for (int i = 0; i < n; i++) s_uv[i] = raw[i];
    }
    Tuple *hum_t = dict_find(iter, MESSAGE_KEY_RELATIVE_HUMIDITY);
    if (hum_t && hum_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)hum_t->length < MAX_TEMPS ? (int)hum_t->length : MAX_TEMPS;
      s_humidity_count = n;
      const uint8_t *raw = (const uint8_t *)hum_t->value->data;
      for (int i = 0; i < n; i++) s_humidity[i] = raw[i];
    }
    Tuple *wgust_t = dict_find(iter, MESSAGE_KEY_WIND_GUST);
    if (wgust_t && wgust_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)wgust_t->length < MAX_TEMPS ? (int)wgust_t->length : MAX_TEMPS;
      s_wind_gust_count = n;
      const uint8_t *raw = (const uint8_t *)wgust_t->value->data;
      for (int i = 0; i < n; i++) s_wind_gust[i] = raw[i];
    }
    if (idx_t) s_current_idx      = (int)idx_t->value->int32;
    Tuple *min_t = dict_find(iter, MESSAGE_KEY_CURRENT_MINUTE);
    if (min_t) s_current_min = (int)min_t->value->int32;
    if (sh_t)  s_local_start_h    = (int)sh_t->value->int32;
    if (wd_t)  s_local_start_wday = (int)wd_t->value->int32;
    Tuple *day_t = dict_find(iter, MESSAGE_KEY_LOCAL_START_DAY);
    Tuple *mon_t = dict_find(iter, MESSAGE_KEY_LOCAL_START_MONTH);
    if (day_t) s_local_start_day = (int)day_t->value->int32;
    if (mon_t) s_local_start_mon = (int)mon_t->value->int32;
    if (loc_t) snprintf(s_location, sizeof(s_location), "%s", loc_t->value->cstring);
    int initial_offset = s_current_idx - 36;
    s_scroll_offset = (initial_offset > 0) ? initial_offset : 0;
    s_scroll_target = s_scroll_offset;
    prv_compute_daily_stats();
  }

  prv_update_status_layer();
  layer_mark_dirty(s_graph_layer);
}

static void prv_inbox_dropped(AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

/* ---------- graph layer ---------- */

static int prv_wind_conv(int ms) {
  if (s_settings.wind_unit == 1) return ms * 36 / 10;   /* km/h */
  if (s_settings.wind_unit == 2) return ms * 2237 / 1000; /* mph */
  return ms;  /* m/s */
}

static void prv_graph_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int w  = bounds.size.w;
  const int h  = bounds.size.h;
  const int gt = TITLE_HEIGHT + (s_settings.show_cloud && s_cloud_count > 0 ? CLOUD_HEIGHT : 0);
  const int gb = h - TLABEL_HEIGHT - BOTTOM_PAD;
  const int gh = gb - gt;
  const int precip_top = gt + 3;  /* small gap below cloud strip; also top of vertical grid lines */

  GFont f_small  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_medium = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_bold   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_tiny   = fonts_get_system_font(FONT_KEY_GOTHIC_09);

  /* White background */
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  /* Separator line under title bar */
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(0, TITLE_HEIGHT - 1), GPoint(w, TITLE_HEIGHT - 1));

  if (s_status != STATUS_READY || s_temp_count < 2) return;

  /* ---- zoom window ---- */
  const int view_count = s_view_count;
  const int view_start = s_scroll_offset;
  const int n = (view_start + view_count <= s_temp_count)
                ? view_count : (s_temp_count - view_start);
  if (n < 2) return;

#define X(i)  ((i) * w / n)
#define TC(c) (s_settings.temp_unit ? ((c) * 9 / 5 + 32) : (c))
#define Y(t)  (y_low - ((t) - g_low) * (y_low - y_high) / (g_high - g_low))
#define YC(t) ({ int _y = Y(t); _y < gt ? gt : (_y > gb ? gb : _y); })
#define DRAW_SHADOWED(text, font, rect, overflow, align) do { \
  if (!s_animating) { \
    graphics_context_set_text_color(ctx, GColorWhite); \
    graphics_draw_text(ctx, text, font, \
      GRect((rect).origin.x+1, (rect).origin.y+1, (rect).size.w, (rect).size.h), \
      overflow, align, NULL); \
  } \
  graphics_context_set_text_color(ctx, GColorBlack); \
  graphics_draw_text(ctx, text, font, rect, overflow, align, NULL); \
} while(0)

  /* ---- cloud cover strip (between title bar and graph) ---- */
  if (s_settings.show_cloud && s_cloud_count > 0) {
    const int cloud_gap = 2;                          /* px gap below title bar separator */
    const int cloud_max_h = CLOUD_HEIGHT - cloud_gap; /* 10px usable */
    const int strip_mid = TITLE_HEIGHT + cloud_gap + cloud_max_h / 2;
    graphics_context_set_fill_color(ctx, GColorLightGray);
    for (int i = 0; i < n; i++) {
      int abs_i = view_start + i;
      if (abs_i >= s_cloud_count) break;
      uint8_t cv = s_cloud[abs_i];
      if (cv == 255 || cv == 0) continue;  /* NaN or clear */
      /* Quantize to 5 steps of 20%: 2, 4, 6, 8, 10 px */
      int step = ((int)cv + 19) / 20;      /* 1..5 */
      int band_h = step * 2;               /* 2, 4, 6, 8, 10 px */
      if (band_h > cloud_max_h) band_h = cloud_max_h;
      int x0 = X(i);
      int x1 = X(i + 1);
      int bw = x1 - x0;
      if (bw < 1) bw = 1;
      int y_top = strip_mid - band_h / 2;
      graphics_fill_rect(ctx, GRect(x0, y_top, bw, band_h), 0, GCornerNone);
    }
  }

  /* ---- temperature scale: anchor grid lines to data ---- */
  int min_t = TC((int)s_temps[0]), max_t = TC((int)s_temps[0]);
  for (int i = 1; i < s_temp_count; i++) {
    int t = TC((int)s_temps[i]);
    if (t < min_t) min_t = t;
    if (t > max_t) max_t = t;
  }
  const int t_step = s_settings.temp_unit ? 10 : 5;
  /* g_low: nearest step floor of min_t; g_high: nearest step ceiling of max_t */
  int g_low  = (min_t / t_step) * t_step - (min_t < 0 && min_t % t_step ? t_step : 0);
  if (g_low > min_t) g_low -= t_step;
  int g_high = (max_t / t_step) * t_step + (max_t > 0 && max_t % t_step ? t_step : 0);
  if (g_high < max_t) g_high += t_step;
  if (g_high == g_low) g_high = g_low + t_step;  /* guard against flat data */
  /* Fix pixel positions: g_low just above weekday labels, g_high with room for top label */
  bool sun_visible = s_sun_count > 0 &&
    (s_settings.show_dawn_dusk || s_settings.show_golden_hour || s_settings.show_darkness);
  const int y_low  = gb - 3 - TLABEL_HEIGHT - (sun_visible ? 8 : 0);
  const int y_high = gt + 18;                      /* room for f_medium label + gap */

  /* ---- grid lines (every t_step degrees) ---- */
  {
    for (int t = g_low; t <= g_high; t += t_step) {
      int y = Y(t);
      if (y <= gt || y >= gb) continue;
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_line(ctx, GPoint(0, y), GPoint(w, y));
    }
  }

  /* ---- x-axis tick lines ---- */
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, gb, w, h - gb), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_context_set_stroke_width(ctx, 1);

  if (s_zoom_days == 1) {
    int view_start_h = (s_local_start_h + view_start) % 24;
    static const int target_hours[] = {0, 3, 6, 9, 12, 15, 18, 21};
    for (int j = 0; j < 8; j++) {
      int th  = target_hours[j];
      if (th != 0 && s_zoom_anim) continue;  /* hide 3h grid lines during zoom animation */
      int idx = (th - view_start_h + 24) % 24;
      if (idx >= n) continue;
      int tx = X(idx);
      graphics_draw_line(ctx, GPoint(tx, precip_top), GPoint(tx, h));
    }
  } else {
    int first_midnight_abs = (24 - s_local_start_h) % 24;
    for (int k = 0; ; k++) {
      int abs_idx = first_midnight_abs + k * 24;
      int rel_idx = abs_idx - view_start;
      if (rel_idx < 0) continue;
      if (rel_idx >= n) break;
      int tx = X(rel_idx);
      graphics_draw_line(ctx, GPoint(tx, precip_top), GPoint(tx, h));
    }
  }

  /* ---- "now" dotted vertical marker (drawn after grid lines, under all other content) ---- */
  {
    int now_i = s_current_idx - view_start;
    if (now_i >= 0 && now_i < n) {
      /* Interpolate between hour tick and next for minute precision */
      int nx = X(now_i) + (X(now_i + 1) - X(now_i)) * s_current_min / 60;
      graphics_context_set_stroke_color(ctx, GColorDarkGray);
      graphics_context_set_stroke_width(ctx, 2);
      for (int y = precip_top; y < gb + 1; y += 10) {
        int y2 = y + 2;
        if (y2 > gb + 1) y2 = gb + 1;
        graphics_draw_line(ctx, GPoint(nx, y), GPoint(nx, y2));
      }
    }
  }

  /* ---- precipitation scale (based on max 3h sum, consistent across zoom levels) ---- */
  /* Each scale (precip top, wind bottom) uses slightly less than half gh.
     tiny_lbl_h reserves space for the "mm" / "m/s" unit labels within that half. */
  const int scale_half  = gh * 9 / 20;
  const int tiny_lbl_h  = 12;
  const int wind_top_y  = gb - scale_half + tiny_lbl_h;

  int precip_max_p = 20;
  int precip_max_bar_h = scale_half - tiny_lbl_h;
  if (s_settings.show_precip && s_precip_count > 0) {
    for (int i = 0; i < s_precip_count; i++) {
      int p = (int)s_precip[i];
      if (p > precip_max_p) precip_max_p = p;
    }
  }
  /* Snap to fixed scale ceilings */
  int inch_tick_hund = 0;  /* tick step in hundredths-of-inch (0 = mm mode) */
  if (s_settings.precip_unit == 1) {
    /* Inch ceilings: 0.10in / 0.40in / 0.60in  (≈ mm 3/10/15) */
    if      (precip_max_p <= 25)  { precip_max_p =  25; inch_tick_hund =  5; }
    else if (precip_max_p <= 101) { precip_max_p = 101; inch_tick_hund = 10; }
    else                          { precip_max_p = 152; inch_tick_hund = 20; }
  } else {
    if      (precip_max_p <= 40)  precip_max_p = 30;
    else if (precip_max_p <= 100) precip_max_p = 100;
    else                          precip_max_p = 150;
  }

  /* ---- precipitation axis ticks (lines only, labels drawn later on top) ---- */
  if (s_settings.show_precip && s_precip_count > 0) {
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, GPoint(w - 13, precip_top), GPoint(w, precip_top));
    if (inch_tick_hund > 0) {
      for (int t = inch_tick_hund; t * 254 / 100 <= precip_max_p; t += inch_tick_hund) {
        int p  = t * 254 / 100;
        int by = precip_top + p * precip_max_bar_h / precip_max_p;
        if (by < precip_top || by > precip_top + precip_max_bar_h) continue;
        graphics_draw_line(ctx, GPoint(w - 13, by), GPoint(w, by));
      }
    } else {
      int tick_step = (precip_max_p <= 100) ? 10 : 30;
      for (int p = tick_step; p <= precip_max_p; p += tick_step) {
        int by = precip_top + p * precip_max_bar_h / precip_max_p;
        if (by < precip_top || by > precip_top + precip_max_bar_h) continue;
        graphics_draw_line(ctx, GPoint(w - 13, by), GPoint(w, by));
      }
    }
  }

  /* ---- precipitation bars (hanging from precip_top, growing downward) ---- */
  if (s_settings.show_precip && s_precip_count > 0) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorDarkGray));
    if (s_zoom_days == 1) {
      for (int i = 0; i < n; i++) {
        int p = (int)s_precip[view_start + i];
        if (p == 0) continue;
        int bh = p * precip_max_bar_h / precip_max_p;
        if (bh < 1) bh = 1;
        int bx = X(i);
        int bw = X(i + 1) - bx;
        if (bw < 1) bw = 1;
        graphics_fill_rect(ctx, GRect(bx, precip_top, bw, bh), 0, GCornerNone);
      }
    } else {
      for (int i = 0; i < n; i++) {
        int p = (int)s_precip[view_start + i];
        if (p == 0) continue;
        int bh = p * precip_max_bar_h / precip_max_p;
        if (bh < 1) bh = 1;
        int bx = X(i);
        int bw = X(i + 1) - bx;
        if (bw < 1) bw = 1;
        graphics_fill_rect(ctx, GRect(bx, precip_top, bw, bh), 0, GCornerNone);
      }
    }
  }

  /* ---- wind speed/gust bars + direction arrows ---- */
  int wind_max_spd_ms = 5;    /* raw m/s — hoisted for label section */
  int wind_max_disp   = 5;    /* display-unit max — hoisted for label section */
  int wind_step       = 5;    /* display-unit tick step — hoisted */
  int wind_scale_top_y = -1;  /* hoisted: needed for unit label position */
  if (s_settings.show_wind && s_wind_count > 0 && s_wind_gust_count > 0) {
    for (int i = 0; i < s_wind_count; i++) {
      if (s_wind_speed[i] != 255 && (int)s_wind_speed[i] > wind_max_spd_ms)
        wind_max_spd_ms = s_wind_speed[i];
    }
    for (int i = 0; i < s_wind_gust_count; i++) {
      if (s_wind_gust[i] != 255 && (int)s_wind_gust[i] > wind_max_spd_ms)
        wind_max_spd_ms = s_wind_gust[i];
    }
    wind_max_spd_ms = ((wind_max_spd_ms + 4) / 5) * 5;
    wind_step = (s_settings.wind_unit == 1) ? 20 :
                (s_settings.wind_unit == 2) ? 20 : 5;
    wind_max_disp = prv_wind_conv(wind_max_spd_ms);
    wind_max_disp = ((wind_max_disp + wind_step - 1) / wind_step) * wind_step;
    const int wind_top = wind_top_y;
    const int wind_bot = y_low;
    const int wind_h   = wind_bot - wind_top;
#define WY(v) (wind_bot - (v) * wind_h / wind_max_disp)

    /* Wind bars (light gray, speed→gust range) */
    graphics_context_set_fill_color(ctx, GColorLightGray);
    for (int i = 0; i < n; i++) {
      int abs_i = view_start + i;
      if (abs_i >= s_wind_count || abs_i >= s_wind_gust_count) continue;
      uint8_t spd  = s_wind_speed[abs_i];
      uint8_t gust = s_wind_gust[abs_i];
      if (spd == 255 || gust == 255) continue;
      if (gust < spd) gust = spd;  /* gust should be >= speed */
      int y_spd  = WY(prv_wind_conv(spd));
      int y_gust = WY(prv_wind_conv(gust));
      if (y_gust < wind_top) y_gust = wind_top;
      if (y_spd  > wind_bot) y_spd  = wind_bot;
      int bar_h = y_spd - y_gust;
      if (bar_h < 1) bar_h = 1;
      int bx = X(i);
      int bw = X(i + 1) - bx;
      if (bw < 1) bw = 1;
      graphics_fill_rect(ctx, GRect(bx, y_gust, bw, bar_h), 0, GCornerNone);
    }

    /* Direction arrows (zoomed-in only, not during animations) */
    if (s_zoom_days == 1 && s_wind_arrow && !s_animating) {
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      graphics_context_set_stroke_color(ctx, GColorDarkGray);
      graphics_context_set_stroke_width(ctx, 1);
      for (int i = 0; i < n; i++) {
        int abs_i = view_start + i;
        if (abs_i >= s_wind_count || abs_i >= s_wind_gust_count) continue;
        uint8_t spd  = s_wind_speed[abs_i];
        uint8_t gust = s_wind_gust[abs_i];
        uint8_t dir  = s_wind_dir[abs_i];
        if (spd == 255 || gust == 255 || dir == 255) continue;
        if (gust < spd) gust = spd;
        int y_spd  = WY(prv_wind_conv(spd));
        int y_gust = WY(prv_wind_conv(gust));
        int cx = X(i) + (X(i + 1) - X(i)) / 2;
        int cy = (y_gust + y_spd) / 2;
        /* Decode direction: from_deg → to_deg (where wind goes) */
        int32_t from_deg = (int32_t)dir * 360 / 254;
        int32_t to_deg   = (from_deg + 180) % 360;
        gpath_rotate_to(s_wind_arrow, (int32_t)(TRIG_MAX_ANGLE) * to_deg / 360);
        gpath_move_to(s_wind_arrow, GPoint(cx, cy));
        graphics_context_set_fill_color(ctx, GColorDarkGray);
        gpath_draw_filled(ctx, s_wind_arrow);
        graphics_context_set_stroke_color(ctx, GColorDarkGray);
        gpath_draw_outline(ctx, s_wind_arrow);
      }
    }

    /* Wind scale ticks on right (drawn on top of bars) */
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    wind_scale_top_y = -1;
    for (int s = wind_step; s <= wind_max_disp; s += wind_step) {
      int sy = WY(s);
      if (sy < wind_top || sy > wind_bot) continue;
      graphics_draw_line(ctx, GPoint(w - 13, sy), GPoint(w, sy));
      if (wind_scale_top_y < 0 || sy < wind_scale_top_y) wind_scale_top_y = sy;
    }
    /* 0 tick at wind_bot */
    graphics_draw_line(ctx, GPoint(w - 13, wind_bot), GPoint(w, wind_bot));

#undef WY
  }

  /* ---- temperature curve (no fill) ---- */
  graphics_context_set_stroke_color(ctx,
    PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack));
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < n - 1; i++) {
    graphics_draw_line(ctx,
      GPoint(X(i),     YC(TC((int)s_temps[view_start + i]))),
      GPoint(X(i + 1), YC(TC((int)s_temps[view_start + i + 1]))));
  }

  /* ---- Relative humidity curve (blue, top half of graph area) ---- */
  if (s_settings.show_humidity && s_humidity_count > 0) {
    const int hum_top_y = precip_top;            /* 0% = top of precip area */
    const int hum_bot_y = y_high + (y_low - y_high) / 3; /* 100% = 1/3 down graph area */
#define HUM_Y(rh) (hum_top_y + (rh) * (hum_bot_y - hum_top_y) / 100)
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorDarkGray));
    graphics_context_set_stroke_width(ctx, 2);
    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < n; i++) {
      int abs_i = view_start + i;
      if (abs_i >= s_humidity_count) break;
      uint8_t rh = s_humidity[abs_i];
      if (rh == 255) { prev_x = prev_y = -1; continue; }
      int cx = X(i), cy = HUM_Y(rh);
      if (cy < hum_top_y) cy = hum_top_y;
      if (cy > hum_bot_y) cy = hum_bot_y;
      if (prev_x >= 0)
        graphics_draw_line(ctx, GPoint(prev_x, prev_y), GPoint(cx, cy));
      prev_x = cx; prev_y = cy;
    }
    /* ---- humidity local peak labels (zoomed-in view only, ±6h window) ---- */
    if (s_zoom_days == 1) {
      for (int i = 0; i < n; i++) {
        int abs_i = view_start + i;
        if (abs_i >= s_humidity_count) break;
        uint8_t rh = s_humidity[abs_i];
        if (rh == 255) continue;
        /* Is this the maximum in [abs_i-6, abs_i+6]? Use leftmost occurrence for ties. */
        bool is_peak = true;
        for (int j = abs_i - 6; j <= abs_i + 6 && is_peak; j++) {
          if (j == abs_i || j < 0 || j >= s_humidity_count) continue;
          uint8_t rhj = s_humidity[j];
          if (rhj == 255) continue;
          if ((int)rhj > (int)rh || ((int)rhj == (int)rh && j < abs_i))
            is_peak = false;
        }
        if (!is_peak) continue;
        int cx = X(i);
        int cy = HUM_Y(rh);
        if (cy < hum_top_y) cy = hum_top_y;
        if (cy > hum_bot_y) cy = hum_bot_y;
        char lbl[5]; snprintf(lbl, sizeof(lbl), "%d%%", (int)rh);
        int lx = cx - 20;
        if (lx >= 18 && lx + 40 <= w - 18) {
          DRAW_SHADOWED(lbl, f_medium, GRect(lx, cy + 2, 40, 16),
                        GTextOverflowModeWordWrap, GTextAlignmentCenter);
        }
      }
    }
#undef HUM_Y
  }

  /* ---- UV index curve (orange, bottom half of graph area) ---- */
  if (s_settings.show_uv && s_uv_count > 0) {
    const int uv_bot = y_low;
    const int uv_top = (y_high + y_low) / 2;
#define UV_Y(uv) (uv_bot - (uv) * (uv_bot - uv_top) / 16)
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorOrange, GColorDarkGray));
    graphics_context_set_stroke_width(ctx, 2);
    int prev_x = -1, prev_y = -1, prev_uv = -1;
    for (int i = 0; i < n; i++) {
      int abs_i = view_start + i;
      if (abs_i >= s_uv_count) break;
      uint8_t uv = s_uv[abs_i];
      if (uv == 255) { prev_x = prev_y = prev_uv = -1; continue; }
      int cx = X(i), cy = UV_Y(uv);
      if (cy < uv_top) cy = uv_top;
      if (cy > uv_bot) cy = uv_bot;
      /* draw segment if at least one endpoint is non-zero */
      if (prev_x >= 0 && (uv > 0 || prev_uv > 0))
        graphics_draw_line(ctx, GPoint(prev_x, prev_y), GPoint(cx, cy));
      prev_x = cx; prev_y = cy; prev_uv = uv;
    }
    /* ---- UV daily peak labels (zoomed-in view only) ---- */
    if (s_zoom_days == 1) {
      graphics_context_set_text_color(ctx, GColorBlack);
      int day_max_uv = -1, day_max_rel = -1;
      for (int i = 0; i <= n; i++) {
        int abs_i = view_start + i;
        bool boundary = (i == n) || ((s_local_start_h + abs_i) % 24 == 0);
        if (boundary && i > 0 && day_max_uv > 0 && day_max_rel >= 0) {
          int cx = X(day_max_rel);
          int cy = UV_Y(day_max_uv);
          if (cy < uv_top) cy = uv_top;
          if (cy > uv_bot) cy = uv_bot;
          char lbl[3]; snprintf(lbl, sizeof(lbl), "%d", day_max_uv);
          int num_w = (day_max_uv >= 10) ? 16 : 8;
          int uv_lx = cx + 2;
          int uv_rx = cx + 2 + num_w + 20; /* right edge of "UV" text */
          if (uv_lx >= 18 && uv_rx <= w - 18) {
            DRAW_SHADOWED(lbl, f_medium, GRect(cx + 2, cy - 16, 20, 18),
                          GTextOverflowModeWordWrap, GTextAlignmentLeft);
            DRAW_SHADOWED("UV", f_tiny, GRect(cx + 2 + num_w, cy - 14, 20, 10),
                          GTextOverflowModeWordWrap, GTextAlignmentLeft);
          }
          day_max_uv = -1; day_max_rel = -1;
        }
        if (i == n) break;
        if (abs_i < s_uv_count) {
          uint8_t uv = s_uv[abs_i];
          if (uv != 255 && (int)uv > day_max_uv) { day_max_uv = uv; day_max_rel = i; }
        }
      }
    }
#undef UV_Y
  }

  /* ---- wind scale labels (drawn after temp curve so they appear on top) ---- */
  if (s_settings.show_wind && s_wind_count > 0 && s_wind_gust_count > 0) {
    const int wbot = y_low;
    const int wh   = wbot - wind_top_y;
    for (int s = wind_step; s <= wind_max_disp; s += wind_step) {
      int sy = wbot - s * wh / wind_max_disp;
      if (sy < wind_top_y || sy > wbot) continue;
      char lbl[5]; snprintf(lbl, sizeof(lbl), "%d", s);
      DRAW_SHADOWED(lbl, f_medium, GRect(w - 30, sy - 16, 28, 18),
                    GTextOverflowModeWordWrap, GTextAlignmentRight);
    }
    graphics_context_set_text_color(ctx, GColorBlack);
    DRAW_SHADOWED("0", f_medium, GRect(w - 30, wbot - 16, 28, 18),
                  GTextOverflowModeWordWrap, GTextAlignmentRight);
    if (wind_scale_top_y >= 0) {
      const char *wlbl = (s_settings.wind_unit == 1) ? "km/h" :
                         (s_settings.wind_unit == 2) ? "mph" : "m/s";
      DRAW_SHADOWED(wlbl, f_tiny, GRect(w - 26, wind_scale_top_y - 25, 26, 12),
                    GTextOverflowModeWordWrap, GTextAlignmentRight);
    }
  }

  /* ---- temperature y-axis labels (drawn on top) ---- */
  {
    for (int t = g_low; t <= g_high; t += t_step) {
      int y = Y(t);
      if (y <= gt || y >= gb) continue;
      char lbl[8]; snprintf(lbl, sizeof(lbl), "%d\xc2\xb0", t);
      DRAW_SHADOWED(lbl, f_medium, GRect(2, y - 16, 30, 18),
                    GTextOverflowModeWordWrap, GTextAlignmentLeft);
    }
  }

  /* ---- precipitation labels (drawn on top) ---- */
  int mm_bot_by = -1;
  if (s_settings.show_precip && s_precip_count > 0) {
    int mm_top_by = -1;
    if (inch_tick_hund > 0) {
      for (int t = inch_tick_hund; t * 254 / 100 <= precip_max_p; t += inch_tick_hund) {
        int p  = t * 254 / 100;
        int by = precip_top + p * precip_max_bar_h / precip_max_p;
        if (by < precip_top || by > precip_top + precip_max_bar_h) continue;
        if (mm_bot_by < 0 || by > mm_bot_by) mm_bot_by = by;
        if (mm_top_by < 0 || by < mm_top_by) mm_top_by = by;
        char lbl[6];
        snprintf(lbl, sizeof(lbl), "%d.%02d", t / 100, t % 100);
        DRAW_SHADOWED(lbl, f_medium, GRect(w - 30, by - 16, 28, 18),
                      GTextOverflowModeWordWrap, GTextAlignmentRight);
      }
    } else {
      int tick_step = (precip_max_p > 100) ? 30 : 10;
      for (int p = tick_step; p <= precip_max_p; p += tick_step) {
        int by = precip_top + p * precip_max_bar_h / precip_max_p;
        if (by < precip_top || by > precip_top + precip_max_bar_h) continue;
        if (mm_bot_by < 0 || by > mm_bot_by) mm_bot_by = by;
        if (mm_top_by < 0 || by < mm_top_by) mm_top_by = by;
        bool show_lbl = (precip_max_p <= 30)  ? (p % 10 == 0)
                      : (precip_max_p <= 100) ? (p % 20 == 0)
                      : (p % 30 == 0);
        if (show_lbl) {
          char lbl[6]; snprintf(lbl, sizeof(lbl), "%d", p / 10);
          DRAW_SHADOWED(lbl, f_medium, GRect(w - 30, by - 16, 28, 18),
                        GTextOverflowModeWordWrap, GTextAlignmentRight);
        }
      }
    }
    if (mm_bot_by >= 0) {
      const char *plbl = (s_settings.precip_unit == 1) ? "in" : "mm";
      DRAW_SHADOWED(plbl, f_tiny, GRect(w - 26, mm_bot_by, 26, 12),
                    GTextOverflowModeWordWrap, GTextAlignmentRight);
    }
  }

  /* ---- sun condition bars + sunrise/sunset ticks ---- */
  if (s_sun_count > 0 && (s_settings.show_golden_hour || s_settings.show_darkness || s_settings.show_dawn_dusk)) {
    const int sun_y    = y_low + 2;   /* top of 2px bar */
    const int tick_top = sun_y + 1;   /* overlaps bottom row of bar */
    const int bar_sw = s_zoom_days == 1 ? 2 : 1;
    const int bar_y  = sun_y + bar_sw / 2;  /* center of bar for stroke drawing */
    graphics_context_set_stroke_width(ctx, bar_sw);
    for (int i = 0; i < n; i++) {
      int abs_i = view_start + i;
      if (abs_i >= s_sun_count) break;
      uint8_t sc = s_sun_cond[abs_i];
      if (sc == 0) continue;

      /* decode base condition and optional tick */
      uint8_t base = sc;
      int tick_min = -1;
      bool tick_rise = false;
      if (sc >= 160) { base = 1; tick_min = sc - 160; tick_rise = false; }
      else if (sc >= 100) { base = 1; tick_min = sc - 100; tick_rise = true; }

      /* draw bar */
      int bx = X(i);
      int bw = X(i + 1) - bx;
      if (bw < 1) bw = 1;
      bool draw_full_bar = (base == 2) ? s_settings.show_darkness : s_settings.show_golden_hour;
      if (draw_full_bar) {
        GColor bar_color = (base == 2) ? GColorDarkGray : GColorOrange;
        graphics_context_set_stroke_color(ctx, bar_color);
        graphics_draw_line(ctx, GPoint(bx, bar_y), GPoint(bx + bw - 1, bar_y));
      } else if (base == 1 && s_settings.show_dawn_dusk && tick_min >= 0) {
        /* Stub length = tick diagonal = 4√2 ≈ 6px, centered at tick position */
        int tx = bx + bw * tick_min / 60;
        graphics_context_set_stroke_color(ctx, GColorOrange);
        graphics_draw_line(ctx, GPoint(tx - 4, bar_y), GPoint(tx + 4, bar_y));
      }

      /* draw tick */
      if (s_settings.show_dawn_dusk && tick_min >= 0) {
        int tx = bx + bw * tick_min / 60;
        graphics_context_set_stroke_color(ctx, GColorOrange);
        graphics_context_set_stroke_width(ctx, s_zoom_days == 1 ? 2 : 1);
        if (tick_rise) {
          /* "/" sunrise: crosses bar, slopes up left-to-right */
          graphics_draw_line(ctx, GPoint(tx - 2, sun_y + 3), GPoint(tx + 2, sun_y - 1));
        } else {
          /* "\" sunset: crosses bar, slopes down left-to-right */
          graphics_draw_line(ctx, GPoint(tx - 2, sun_y - 1), GPoint(tx + 2, sun_y + 3));
        }
        graphics_context_set_stroke_width(ctx, bar_sw);
      }
    }
  }

  /* ---- x-axis labels (drawn on top) ---- */
  graphics_context_set_text_color(ctx, GColorBlack);
  if (s_zoom_days == 1) {
    static const int target_hours[] = {0, 3, 6, 9, 12, 15, 18, 21};
    static const char *day_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    int view_start_h = (s_local_start_h + view_start) % 24;
    for (int j = 0; j < 8; j++) {
      int th  = target_hours[j];
      int idx = (th - view_start_h + 24) % 24;
      if (idx >= n) continue;
      int tx = X(idx);
      char lbl[4]; snprintf(lbl, sizeof(lbl), "%02d", th);
      graphics_draw_text(ctx, lbl, f_small,
                         GRect(tx + 4, gb - 3, 30, TLABEL_HEIGHT - 1),
                         GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
      if (th == 0) {
        int abs_idx = view_start + idx;
        int day_num = (s_local_start_h + abs_idx) / 24;
        int weekday = (s_local_start_wday + day_num) % 7;
        /* Compute actual date: add day_num days to start date */
        static const int days_in_month[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        int dd = s_local_start_day + day_num;
        int mm = s_local_start_mon;
        while (dd > days_in_month[mm]) { dd -= days_in_month[mm]; mm++; if (mm > 12) mm = 1; }
        char day_lbl[16];
        snprintf(day_lbl, sizeof(day_lbl), "%s %d.%d.", day_names[weekday], dd, mm);
        if (!s_animating) {
          graphics_context_set_text_color(ctx, GColorWhite);
          graphics_draw_text(ctx, day_lbl, f_small,
                             GRect(tx + 4, gb - 3 - TLABEL_HEIGHT - 1, 70, TLABEL_HEIGHT),
                             GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
          graphics_draw_text(ctx, day_lbl, f_small,
                             GRect(tx + 4, gb - 3 - TLABEL_HEIGHT + 1, 70, TLABEL_HEIGHT),
                             GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        }
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, day_lbl, f_small,
                           GRect(tx + 4, gb - 3 - TLABEL_HEIGHT, 70, TLABEL_HEIGHT),
                           GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
      }
    }
  } else {
    static const char *day_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    int first_midnight_abs = (24 - s_local_start_h) % 24;
    for (int k = -1; ; k++) {
      int abs_idx = first_midnight_abs + k * 24;
      int rel_idx = abs_idx - view_start;
      if (rel_idx >= n) break;
      if (rel_idx < -24) continue;  /* whole day off-screen */
      int tx = X(rel_idx);
      int weekday = (s_local_start_wday + (s_local_start_h + abs_idx) / 24) % 7;
      /* Compute date */
      static const int dim[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
      int day_num = (s_local_start_h + abs_idx) / 24;
      int dd = s_local_start_day + day_num;
      int mm = s_local_start_mon;
      while (dd > dim[mm]) { dd -= dim[mm]; mm++; if (mm > 12) mm = 1; }
      char date_lbl[8]; snprintf(date_lbl, sizeof(date_lbl), "%d.%d.", dd, mm);
      /* Weekday on top row, date on bottom row — with white shadow */
      if (!s_animating) {
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, day_names[weekday], f_small,
                           GRect(tx + 4, gb - 3 - TLABEL_HEIGHT - 1, 40, TLABEL_HEIGHT - 1),
                           GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, day_names[weekday], f_small,
                           GRect(tx + 4, gb - 3 - TLABEL_HEIGHT + 1, 40, TLABEL_HEIGHT - 1),
                           GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
      }
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, day_names[weekday], f_small,
                         GRect(tx + 4, gb - 3 - TLABEL_HEIGHT, 40, TLABEL_HEIGHT - 1),
                         GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
      graphics_draw_text(ctx, date_lbl, f_small,
                         GRect(tx + 4, gb - 3, 40, TLABEL_HEIGHT - 1),
                         GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    }
  }

  /* ---- daily min/max temperature labels (fixed calendar-day grid, precomputed) ---- */
  {
    graphics_context_set_text_color(ctx, GColorBlack);
    const int lbl_w = 28;
    /* Track all drawn label rects to avoid duplicates */
#define MAX_DRAWN_LBLS 20
    int drawn_lx[MAX_DRAWN_LBLS], drawn_ly[MAX_DRAWN_LBLS];
    int drawn_n = 0;
    for (int d = 0; d < s_day_count; d++) {
      if (s_day_abs_end[d] < view_start || s_day_abs_start[d] >= view_start + n) continue;
      if (s_zoom_days > 1) {
        /* Skip days entirely outside the visible window */
        if (s_day_abs_end[d] < view_start || s_day_abs_start[d] >= view_start + n) continue;
      }
      /* Day x bounds (clamped to view) */
      int day_rel_s = (s_day_abs_start[d] > view_start ? s_day_abs_start[d] : view_start) - view_start;
      int day_rel_e = (s_day_abs_end[d] < view_start + n - 1 ? s_day_abs_end[d] : view_start + n - 1) - view_start;
      int day_x0 = X(day_rel_s);
      int day_x1 = X(day_rel_e + 1);  /* right edge of last hour */
      /* Max label */
      {
        int abs_i = s_day_max_abs[d];
        if (abs_i < 0 || abs_i < view_start || abs_i >= view_start + n) goto skip_max;
        int lbl_y = YC(TC(s_day_max_val[d])) - 17;
        int lx = X(abs_i - view_start) - lbl_w / 2;
        if (lx < day_x0) lx = day_x0;
        if (lx + lbl_w > day_x1) lx = day_x1 - lbl_w;
        /* Skip if horizontally overlapping left (°) or right (mm/m/s) scale areas */
        if (lx < 18 || lx + lbl_w > w - 18) goto skip_max;
        /* Skip if overlapping with any previously drawn label */
        { bool overlap = false;
          for (int i = 0; i < drawn_n; i++) {
            if (lx < drawn_lx[i] + lbl_w && lx + lbl_w > drawn_lx[i] &&
                lbl_y < drawn_ly[i] + 30 && lbl_y + 30 > drawn_ly[i]) { overlap = true; break; }
          }
          if (overlap) goto skip_max; }
        char lbl[8]; snprintf(lbl, sizeof(lbl), "%d\xc2\xb0", TC(s_day_max_val[d]));
        DRAW_SHADOWED(lbl, f_small, GRect(lx, lbl_y, lbl_w, 14),
                      GTextOverflowModeWordWrap, GTextAlignmentCenter);
        if (drawn_n < MAX_DRAWN_LBLS) { drawn_lx[drawn_n] = lx; drawn_ly[drawn_n] = lbl_y; drawn_n++; }
      }
      skip_max:;
      /* Min label */
      {
        int abs_i = s_day_min_abs[d];
        if (abs_i < 0 || abs_i < view_start || abs_i >= view_start + n) goto skip_min;
        int lbl_y = YC(TC(s_day_min_val[d])) + 2;
        int lx = X(abs_i - view_start) - lbl_w / 2;
        if (lx < day_x0) lx = day_x0;
        if (lx + lbl_w > day_x1) lx = day_x1 - lbl_w;
        /* Skip if horizontally overlapping left (°) or right (mm/m/s) scale areas */
        if (lx < 18 || lx + lbl_w > w - 18) goto skip_min;
        /* Skip if overlapping with any previously drawn label */
        { bool overlap = false;
          for (int i = 0; i < drawn_n; i++) {
            if (lx < drawn_lx[i] + lbl_w && lx + lbl_w > drawn_lx[i] &&
                lbl_y < drawn_ly[i] + 30 && lbl_y + 30 > drawn_ly[i]) { overlap = true; break; }
          }
          if (overlap) goto skip_min; }
        char lbl[8]; snprintf(lbl, sizeof(lbl), "%d\xc2\xb0", TC(s_day_min_val[d]));
        DRAW_SHADOWED(lbl, f_small, GRect(lx, lbl_y, lbl_w, 14),
                      GTextOverflowModeWordWrap, GTextAlignmentCenter);
        if (drawn_n < MAX_DRAWN_LBLS) { drawn_lx[drawn_n] = lx; drawn_ly[drawn_n] = lbl_y; drawn_n++; }
      }
      skip_min:;
    }
#undef MAX_DRAWN_LBLS
  }

#undef X
#undef TC
#undef Y
#undef YC
#undef DRAW_SHADOWED

  /* ---- title bar (redrawn on top to cover any graph overflow) ---- */
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, w, TITLE_HEIGHT), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(0, TITLE_HEIGHT - 1), GPoint(w, TITLE_HEIGHT - 1));

  /* Current temperature (left) */
  graphics_context_set_text_color(ctx,
    PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack));
  if (s_current_idx >= 0 && s_current_idx < s_temp_count) {
    int closest = (s_current_min >= 30 && s_current_idx + 1 < s_temp_count)
                  ? s_current_idx + 1 : s_current_idx;
    char cur[8];
    snprintf(cur, sizeof(cur), "%d\xc2\xb0%s",
             s_settings.temp_unit ? ((int)s_temps[closest] * 9 / 5 + 32) : (int)s_temps[closest],
             s_settings.temp_unit ? "F" : "C");
    graphics_draw_text(ctx, cur, f_small,
                       GRect(3, -1, 36, 14),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }

  /* Location label (centre-left, after temp) */
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_location[0] ? s_location : "FMI Forecast", f_small,
                     GRect(0, -1, w, 14),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  /* Current time hh:mm (right) */
  {
    time_t now_t = time(NULL);
    struct tm *lt = localtime(&now_t);
    char tstr[6];
    strftime(tstr, sizeof(tstr), "%H:%M", lt);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, tstr, f_small,
                       GRect(w - 40, -1, 38, 14),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
  }
}

/* ---------- scroll animation ---------- */

static void prv_scroll_anim_update(Animation *anim, const AnimationProgress progress) {
  (void)anim;
  s_scroll_offset = s_scroll_anim_start +
    (int)((s_scroll_target - s_scroll_anim_start) * (int32_t)progress / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(s_graph_layer);
}

static void prv_scroll_anim_stopped(Animation *anim, bool finished, void *ctx) {
  (void)anim; (void)ctx;
  s_scroll_offset = s_scroll_target;
  s_animating = false;
  s_scroll_anim = NULL;
  layer_mark_dirty(s_graph_layer);  /* final redraw with text */
}

static void prv_start_scroll_anim(int target) {
  if (s_scroll_anim) {
    animation_unschedule(s_scroll_anim);
    animation_destroy(s_scroll_anim);
    s_scroll_anim = NULL;
  }
  s_scroll_anim_start = s_scroll_offset;
  s_scroll_target = target;
  s_animating = true;

  static const AnimationImplementation impl = {
    .update = prv_scroll_anim_update
  };
  s_scroll_anim = animation_create();
  animation_set_duration(s_scroll_anim, 75);
  animation_set_curve(s_scroll_anim, AnimationCurveEaseOut);
  animation_set_implementation(s_scroll_anim, &impl);
  static AnimationHandlers handlers = {
    .stopped = prv_scroll_anim_stopped
  };
  animation_set_handlers(s_scroll_anim, handlers, NULL);
  animation_schedule(s_scroll_anim);
}

static int s_zoom_vc_start  = 24;
static int s_zoom_vc_target = 24;
static int s_zoom_anchor    = 0;  /* data index at 1/4 screen width, fixed during zoom */

static void prv_zoom_anim_update(Animation *anim, const AnimationProgress progress) {
  (void)anim;
  s_view_count = s_zoom_vc_start +
    (int)((s_zoom_vc_target - s_zoom_vc_start) * (int32_t)progress / ANIMATION_NORMALIZED_MAX);
  if (s_view_count < 1) s_view_count = 1;
  /* Keep 1/4-screen data index fixed */
  int new_scroll = s_zoom_anchor - s_view_count / 4;
  int max_scroll = s_temp_count - s_view_count;
  if (max_scroll < 0) max_scroll = 0;
  if (new_scroll < 0) new_scroll = 0;
  if (new_scroll > max_scroll) new_scroll = max_scroll;
  s_scroll_offset = new_scroll;
  s_scroll_target = new_scroll;
  layer_mark_dirty(s_graph_layer);
}

static void prv_zoom_anim_stopped(Animation *anim, bool finished, void *ctx) {
  (void)anim; (void)ctx;
  s_view_count = s_zoom_vc_target;
  s_animating = false;
  s_zoom_anim = NULL;
  layer_mark_dirty(s_graph_layer);
}

static void prv_start_zoom_anim(int vc_target) {
  if (s_zoom_anim) {
    animation_unschedule(s_zoom_anim);
    animation_destroy(s_zoom_anim);
    s_zoom_anim = NULL;
  }
  s_zoom_vc_start  = s_view_count;
  s_zoom_vc_target = vc_target;
  s_animating = true;

  static const AnimationImplementation impl = {
    .update = prv_zoom_anim_update
  };
  s_zoom_anim = animation_create();
  animation_set_duration(s_zoom_anim, 66);
  animation_set_curve(s_zoom_anim, AnimationCurveEaseInOut);
  animation_set_implementation(s_zoom_anim, &impl);
  static AnimationHandlers handlers = {
    .stopped = prv_zoom_anim_stopped
  };
  animation_set_handlers(s_zoom_anim, handlers, NULL);
  animation_schedule(s_zoom_anim);
}

/* ---------- click handlers ---------- */

static void prv_select_click(ClickRecognizerRef r, void *ctx) {
  s_zoom_days = (s_zoom_days == 1) ? 5 : 1;
  int vc_target = s_zoom_days * 24;
  /* Fix the data index at 1/4 screen width */
  s_zoom_anchor = s_scroll_offset + s_view_count / 4;
  prv_start_zoom_anim(vc_target);
}

static void prv_down_click(ClickRecognizerRef r, void *ctx) {
  int step = (s_zoom_days == 1) ? 6 : 24;
  int view_count = (s_zoom_days == 1) ? 24 : (s_zoom_days * 24);
  int max_scroll = s_temp_count - view_count;
  if (max_scroll < 0) max_scroll = 0;
  int target = s_scroll_target + step;
  if (target > max_scroll) target = max_scroll;
  prv_start_scroll_anim(target);
}

static void prv_up_click(ClickRecognizerRef r, void *ctx) {
  int step = (s_zoom_days == 1) ? 6 : 24;
  int target = s_scroll_target - step;
  if (target < 0) target = 0;
  prv_start_scroll_anim(target);
}

static void prv_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_down_click);
  window_single_click_subscribe(BUTTON_ID_UP,     prv_up_click);
}

/* ---------- window ---------- */

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_graph_layer = layer_create(bounds);
  layer_set_update_proc(s_graph_layer, prv_graph_update);
  layer_add_child(root, s_graph_layer);

  s_status_layer = text_layer_create(
    GRect(bounds.size.w / 6, bounds.size.h / 2 - 30,
          bounds.size.w * 2 / 3, 60));
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, GColorBlack);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_text(s_status_layer, "Loading\nforecast...");
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  /* Direction arrow: tip up (north), rotated per bar */
  static const GPoint arrow_pts[] = {{0, -5}, {-3, 3}, {3, 3}};
  static const GPathInfo arrow_info = {3, (GPoint *)arrow_pts};
  s_wind_arrow = gpath_create(&arrow_info);
}

static void prv_window_unload(Window *window) {
  if (s_zoom_anim) {
    animation_unschedule(s_zoom_anim);
    animation_destroy(s_zoom_anim);
    s_zoom_anim = NULL;
  }
  if (s_scroll_anim) {
    animation_unschedule(s_scroll_anim);
    animation_destroy(s_scroll_anim);
    s_scroll_anim = NULL;
  }
  gpath_destroy(s_wind_arrow);
  s_wind_arrow = NULL;
  layer_destroy(s_graph_layer);
  text_layer_destroy(s_status_layer);
}

/* ---------- init / deinit ---------- */

static void prv_init(void) {
  prv_load_settings();
  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_open(app_message_inbox_size_maximum(),
                   app_message_outbox_size_maximum());

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  prv_request_data();
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
