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
  uint8_t show_cloud_z1;
  uint8_t show_cloud_z5;
  uint8_t show_precip_z1;
  uint8_t show_precip_z5;
  uint8_t show_humidity_z1;
  uint8_t show_humidity_z5;
  uint8_t show_wind_z1;
  uint8_t show_wind_z5;
  uint8_t show_uv_z1;
  uint8_t show_uv_z5;
  uint8_t show_weather_ind_z1;
  uint8_t show_weather_ind_z5;
  uint8_t show_sunrise_sunset_z1;
  uint8_t show_sunrise_sunset_z5;
  uint8_t show_golden_hour_z1;
  uint8_t show_golden_hour_z5;
  uint8_t show_darkness_z1;
  uint8_t show_darkness_z5;
  int time_format;       /* 0=24h, 1=12h */
  int date_format;       /* 0=DD.MM., 1=MM/DD */
  float cache_max_age_hours; /* hours before cached forecast is stale */
  int startup_view;      /* 0=1-day, 1=5-day, 2=last_viewed */
} AppSettings;

#define SETTINGS_VERSION 7
#define LAST_VIEW_KEY 90  /* persist last zoom state (outside cache key range) */
#define IDLE_EXIT_KEY 91  /* idle auto-exit seconds (outside cache key range) */

static AppSettings s_settings = {
  .version = SETTINGS_VERSION,
  .temp_unit = 0, .wind_unit = 0, .precip_unit = 0,
  .show_cloud_z1 = 1, .show_cloud_z5 = 1,
  .show_precip_z1 = 1, .show_precip_z5 = 1,
  .show_humidity_z1 = 0, .show_humidity_z5 = 0,
  .show_wind_z1 = 1, .show_wind_z5 = 1,
  .show_uv_z1 = 0, .show_uv_z5 = 0,
  .show_weather_ind_z1 = 1, .show_weather_ind_z5 = 1,
  .show_sunrise_sunset_z1 = 0, .show_sunrise_sunset_z5 = 0,
  .show_golden_hour_z1 = 0, .show_golden_hour_z5 = 0,
  .show_darkness_z1 = 0, .show_darkness_z5 = 0,
  .time_format = 0, .date_format = 0,
  .cache_max_age_hours = 24.0f,
  .startup_view = 1,  /* default to 5-day */
};

static void prv_save_settings(void);  /* forward declaration */

static void prv_load_settings(void) {
  int stored_size = persist_get_size(SETTINGS_KEY);
  if (stored_size <= 0) return;

  AppSettings loaded = s_settings;  /* keep defaults for fields missing from older versions */
  int read_size = stored_size < (int)sizeof(loaded) ? stored_size : (int)sizeof(loaded);
  if (persist_read_data(SETTINGS_KEY, &loaded, read_size) != read_size) return;

  if (loaded.version == SETTINGS_VERSION) {
    s_settings = loaded;
  } else if (loaded.version > 0 && loaded.version < SETTINGS_VERSION) {
    loaded.version = SETTINGS_VERSION;
    s_settings = loaded;
    prv_save_settings();
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
static uint8_t    s_weather_ind[MAX_TEMPS]; /* 0=none, 1=snow/sleet, 2=lightning */
static int        s_weather_ind_count  = 0;
static uint8_t    s_cloud[MAX_TEMPS];       /* 0-100%, 255=NaN */
static uint8_t    s_sun_cond[MAX_TEMPS];    /* 0=normal, 1=golden, 2=dark */
static uint8_t    s_sun_bmin[MAX_TEMPS];    /* boundary minute encoding */
static int        s_sun_bmin_count     = 0;
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
static char       s_preset_names[5][33] = {"","","","",""};
static int        s_selected_preset  = 0;  /* 0=GPS, 1-5=preset */
static int        s_zoom_days        = 5;  /* 1 or 5 */
static int        s_view_count       = 120; /* animated: current view width in hours */
static Animation *s_zoom_anim        = NULL;
static int        s_scroll_offset    = 0;  /* hours scrolled forward */
static int        s_scroll_target    = 0;
static int        s_scroll_anim_start = 0;
static bool       s_animating        = false;  /* unused, kept for future use */
static Animation *s_scroll_anim      = NULL;

static bool       s_touch_active     = false;
static int        s_touch_abs        = 0;   /* absolute hour index shown in popup */

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
/* ---------- persistent state helpers ---------- */

static void prv_save_last_view(void) {
  persist_write_int(LAST_VIEW_KEY, s_zoom_days);
}

static int prv_load_last_view(void) {
  if (persist_exists(LAST_VIEW_KEY)) {
    int v = persist_read_int(LAST_VIEW_KEY);
    return (v == 1 || v == 5) ? v : 5;
  }
  return 5;  /* default to 5-day if not set */
}

/* ---------- idle auto-exit ----------
   After s_idle_timeout_sec of no interaction in the graph / location views, return
   to the watchface (same idiom as a confirmed action). 0 = feature off; default 15. */

static int       s_idle_timeout_sec = 15;   /* 0 = off */
static AppTimer *s_idle_timer       = NULL;
static bool      s_config_open      = false; /* true while the phone config page is open (pauses idle) */

static void idle_cancel(void) {
  if (s_idle_timer) { app_timer_cancel(s_idle_timer); s_idle_timer = NULL; }
}

static void idle_fire(void *ctx) {
  (void)ctx;
  s_idle_timer = NULL;
  exit_reason_set(APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY);
  window_stack_pop_all(true);  /* lands on the watchface */
}

static void idle_reset(void) {  /* arm or reschedule */
  if (s_config_open) return;    /* never (re)arm while the phone config page is open */
  if (s_idle_timeout_sec <= 0) { idle_cancel(); return; }
  if (s_idle_timer) app_timer_reschedule(s_idle_timer, s_idle_timeout_sec * 1000);
  else              s_idle_timer = app_timer_register(s_idle_timeout_sec * 1000, idle_fire, NULL);
}

/* Type-tolerant seconds reader: default Clay auto-send delivers a select as a
   string. Hand-rolled digit loop — never atoi/strtol (not exported by Core fw). */
static int idle_read_seconds(Tuple *t) {
  if (!t) return -1;  /* key absent -> leave unchanged */
  if (t->type == TUPLE_CSTRING) {
    int v = 0; const char *p = t->value->cstring;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v;
  }
  return (int)t->value->int32;
}

/* Window handlers shared by both windows: arm on show, cancel on hide.
   A child window (location menu) pushing over the graph makes it disappear,
   which cancels the timer for free; the child arms its own. */
static void prv_idle_appear(Window *window)    { (void)window; idle_reset(); }
static void prv_idle_disappear(Window *window) { (void)window; idle_cancel(); }

/* ---------- data processing ---------- */

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

/* ---------- forecast cache (persisted per preset slot) ---------- */

#define CACHE_KEY_BASE      10
#define CACHE_KEYS_PER_SLOT 12
#define NUM_CACHE_SLOTS     6   /* 0=GPS, 1-5=presets */

typedef struct __attribute__((packed)) {
  uint32_t timestamp;
  int16_t  temp_count, precip_count, wind_count, wind_gust_count;
  int16_t  cloud_count, sun_count, sun_bmin_count;
  int16_t  uv_count, humidity_count, weather_ind_count;
  int8_t   local_start_h, local_start_wday, local_start_day, local_start_mon;
  int16_t  current_idx;
  uint8_t  current_min;
  char     location[33];
} CacheMetadata; /* ~65 bytes */

static void prv_save_cache(int preset) {
  if (preset < 0 || preset >= NUM_CACHE_SLOTS) return;
  int base = CACHE_KEY_BASE + preset * CACHE_KEYS_PER_SLOT;
  CacheMetadata meta = {
    .timestamp         = (uint32_t)time(NULL),
    .temp_count        = (int16_t)s_temp_count,
    .precip_count      = (int16_t)s_precip_count,
    .wind_count        = (int16_t)s_wind_count,
    .wind_gust_count   = (int16_t)s_wind_gust_count,
    .cloud_count       = (int16_t)s_cloud_count,
    .sun_count         = (int16_t)s_sun_count,
    .sun_bmin_count    = (int16_t)s_sun_bmin_count,
    .uv_count          = (int16_t)s_uv_count,
    .humidity_count    = (int16_t)s_humidity_count,
    .weather_ind_count = (int16_t)s_weather_ind_count,
    .local_start_h     = (int8_t)s_local_start_h,
    .local_start_wday  = (int8_t)s_local_start_wday,
    .local_start_day   = (int8_t)s_local_start_day,
    .local_start_mon   = (int8_t)s_local_start_mon,
    .current_idx       = (int16_t)s_current_idx,
    .current_min       = (uint8_t)s_current_min,
  };
  snprintf(meta.location, sizeof(meta.location), "%s", s_location);
  persist_write_data(base + 0,  &meta,         sizeof(meta));
  persist_write_data(base + 1,  s_temps,       MAX_TEMPS);
  persist_write_data(base + 2,  s_precip,      MAX_TEMPS);
  persist_write_data(base + 3,  s_wind_speed,  MAX_TEMPS);
  persist_write_data(base + 4,  s_wind_dir,    MAX_TEMPS);
  persist_write_data(base + 5,  s_wind_gust,   MAX_TEMPS);
  persist_write_data(base + 6,  s_weather_ind, MAX_TEMPS);
  persist_write_data(base + 7,  s_cloud,       MAX_TEMPS);
  persist_write_data(base + 8,  s_sun_cond,    MAX_TEMPS);
  persist_write_data(base + 9,  s_sun_bmin,    MAX_TEMPS);
  persist_write_data(base + 10, s_uv,          MAX_TEMPS);
  persist_write_data(base + 11, s_humidity,    MAX_TEMPS);
}

static bool prv_load_cache(int preset) {
  if (preset < 0 || preset >= NUM_CACHE_SLOTS) return false;
  int base = CACHE_KEY_BASE + preset * CACHE_KEYS_PER_SLOT;
  CacheMetadata meta;
  if (persist_read_data(base + 0, &meta, sizeof(meta)) != (int)sizeof(meta)) return false;
  time_t age = (time_t)time(NULL) - (time_t)meta.timestamp;
  if (age < 0 || age > (time_t)(s_settings.cache_max_age_hours * 3600.0f)) return false;
  persist_read_data(base + 1,  s_temps,       MAX_TEMPS);
  persist_read_data(base + 2,  s_precip,      MAX_TEMPS);
  persist_read_data(base + 3,  s_wind_speed,  MAX_TEMPS);
  persist_read_data(base + 4,  s_wind_dir,    MAX_TEMPS);
  persist_read_data(base + 5,  s_wind_gust,   MAX_TEMPS);
  persist_read_data(base + 6,  s_weather_ind, MAX_TEMPS);
  persist_read_data(base + 7,  s_cloud,       MAX_TEMPS);
  persist_read_data(base + 8,  s_sun_cond,    MAX_TEMPS);
  persist_read_data(base + 9,  s_sun_bmin,    MAX_TEMPS);
  persist_read_data(base + 10, s_uv,          MAX_TEMPS);
  persist_read_data(base + 11, s_humidity,    MAX_TEMPS);
  s_temp_count        = meta.temp_count;
  s_precip_count      = meta.precip_count;
  s_wind_count        = meta.wind_count;
  s_wind_gust_count   = meta.wind_gust_count;
  s_cloud_count       = meta.cloud_count;
  s_sun_count         = meta.sun_count;
  s_sun_bmin_count    = meta.sun_bmin_count;
  s_uv_count          = meta.uv_count;
  s_humidity_count    = meta.humidity_count;
  s_weather_ind_count = meta.weather_ind_count;
  s_local_start_h     = meta.local_start_h;
  s_local_start_wday  = meta.local_start_wday;
  s_local_start_day   = meta.local_start_day;
  s_local_start_mon   = meta.local_start_mon;
  s_current_idx       = meta.current_idx;
  s_current_min       = meta.current_min;
  /* Advance the now-indicator by the time elapsed since the cache was saved */
  time_t elapsed = (time_t)time(NULL) - (time_t)meta.timestamp;
  if (elapsed > 0) {
    s_current_idx += (int)(elapsed / 3600);
    s_current_min  = (int)((elapsed % 3600) / 60);
  }
  snprintf(s_location, sizeof(s_location), "%s", meta.location);
  int initial_offset = s_current_idx - 36;
  s_scroll_offset = (initial_offset > 0) ? initial_offset : 0;
  s_scroll_target = s_scroll_offset;
  prv_compute_daily_stats();
  return true;
}

static void prv_request_data(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int8(iter, MESSAGE_KEY_REQUEST_DATA, 1);
  dict_write_int8(iter, MESSAGE_KEY_SELECTED_PRESET, (int8_t)s_selected_preset);
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
  HANDLE_TOGGLE(SHOW_CLOUD_Z1,        show_cloud_z1);
  HANDLE_TOGGLE(SHOW_CLOUD_Z5,        show_cloud_z5);
  HANDLE_TOGGLE(SHOW_PRECIP_Z1,       show_precip_z1);
  HANDLE_TOGGLE(SHOW_PRECIP_Z5,       show_precip_z5);
  HANDLE_TOGGLE(SHOW_WEATHER_IND_Z1,  show_weather_ind_z1);
  HANDLE_TOGGLE(SHOW_WEATHER_IND_Z5,  show_weather_ind_z5);
  HANDLE_TOGGLE(SHOW_HUMIDITY_Z1,     show_humidity_z1);
  HANDLE_TOGGLE(SHOW_HUMIDITY_Z5,     show_humidity_z5);
  HANDLE_TOGGLE(SHOW_WIND_Z1,         show_wind_z1);
  HANDLE_TOGGLE(SHOW_WIND_Z5,         show_wind_z5);
  HANDLE_TOGGLE(SHOW_UV_Z1,           show_uv_z1);
  HANDLE_TOGGLE(SHOW_UV_Z5,           show_uv_z5);
  HANDLE_TOGGLE(SHOW_SUNRISE_SUNSET_Z1,    show_sunrise_sunset_z1);
  HANDLE_TOGGLE(SHOW_SUNRISE_SUNSET_Z5,    show_sunrise_sunset_z5);
  HANDLE_TOGGLE(SHOW_GOLDEN_HOUR_Z1,  show_golden_hour_z1);
  HANDLE_TOGGLE(SHOW_GOLDEN_HOUR_Z5,  show_golden_hour_z5);
  HANDLE_TOGGLE(SHOW_DARKNESS_Z1,     show_darkness_z1);
  HANDLE_TOGGLE(SHOW_DARKNESS_Z5,     show_darkness_z5);
#undef HANDLE_TOGGLE
  Tuple *tf_t = dict_find(iter, MESSAGE_KEY_TIME_FORMAT);
  if (tf_t) {
    s_settings.time_format = (tf_t->type == TUPLE_CSTRING)
      ? atoi(tf_t->value->cstring) : (int)tf_t->value->int32;
    prv_save_settings(); layer_mark_dirty(s_graph_layer);
  }
  Tuple *df_t = dict_find(iter, MESSAGE_KEY_DATE_FORMAT);
  if (df_t) {
    s_settings.date_format = (df_t->type == TUPLE_CSTRING)
      ? atoi(df_t->value->cstring) : (int)df_t->value->int32;
    prv_save_settings(); layer_mark_dirty(s_graph_layer);
  }
  Tuple *cma_t = dict_find(iter, MESSAGE_KEY_CACHE_MAX_AGE_HOURS);
  if (cma_t) {
    float v = -1.0f;
    if (cma_t->type == TUPLE_CSTRING) {
      /* Simple float parse: avoids pulling in atof/libc */
      const char *p = cma_t->value->cstring;
      long int_part = 0;
      float frac = 0.0f, frac_div = 1.0f;
      bool neg = (*p == '-');
      if (neg || *p == '+') p++;
      while (*p >= '0' && *p <= '9') int_part = int_part * 10 + (*p++ - '0');
      if (*p == '.' || *p == ',') {
        p++;
        while (*p >= '0' && *p <= '9') { frac = frac * 10.0f + (*p++ - '0'); frac_div *= 10.0f; }
      }
      v = (float)int_part + frac / frac_div;
      if (neg) v = -v;
    } else {
      v = (float)cma_t->value->int32;
    }
    if (v >= 0.0f) { s_settings.cache_max_age_hours = v; prv_save_settings(); }
  }

  /* Startup view */
  Tuple *sv_t = dict_find(iter, MESSAGE_KEY_STARTUP_VIEW);
  if (sv_t) {
    int new_view = (sv_t->type == TUPLE_CSTRING)
      ? atoi(sv_t->value->cstring) : (int)sv_t->value->int32;
    if (new_view >= 0 && new_view <= 2) {
      s_settings.startup_view = new_view;
      prv_save_settings();
    }
  }

  /* Idle auto-exit seconds (0 = off) */
  int idle_sec = idle_read_seconds(dict_find(iter, MESSAGE_KEY_IDLE_EXIT_SEC));
  if (idle_sec >= 0) {
    s_idle_timeout_sec = idle_sec;
    persist_write_int(IDLE_EXIT_KEY, idle_sec);
    idle_reset();  /* apply new timeout immediately (or cancel if 0) */
  }

  /* Pause the idle auto-exit while the phone config page is open (no watch buttons
     are pressed during config, so the idle timer would otherwise fire and kill the
     app -- and PKJS with it -- closing the config page and losing unsaved changes). */
  Tuple *co = dict_find(iter, MESSAGE_KEY_CFG_OPEN);
  if (co) {
    s_config_open = (co->value->int32 != 0);
    if (s_config_open) idle_cancel();   /* pause while phone config is open */
    else               idle_reset();    /* resume when config closes */
  }

  /* Preset location names from JS */
  {
    uint32_t preset_keys[5] = {
      MESSAGE_KEY_PRESET_NAME_1, MESSAGE_KEY_PRESET_NAME_2, MESSAGE_KEY_PRESET_NAME_3,
      MESSAGE_KEY_PRESET_NAME_4, MESSAGE_KEY_PRESET_NAME_5
    };
    for (int pi = 0; pi < 5; pi++) {
      Tuple *pt = dict_find(iter, preset_keys[pi]);
      if (pt && pt->type == TUPLE_CSTRING) {
        snprintf(s_preset_names[pi], sizeof(s_preset_names[pi]), "%s", pt->value->cstring);
      }
    }
  }

  /* UV index may arrive as a separate follow-up message (no STATUS) */
  Tuple *uv_followup_t = dict_find(iter, MESSAGE_KEY_UV_INDEX);
  if (uv_followup_t && uv_followup_t->type == TUPLE_BYTE_ARRAY && !dict_find(iter, MESSAGE_KEY_STATUS)) {
    int n = (int)uv_followup_t->length < MAX_TEMPS ? (int)uv_followup_t->length : MAX_TEMPS;
    s_uv_count = n;
    const uint8_t *raw = (const uint8_t *)uv_followup_t->value->data;
    for (int i = 0; i < n; i++) s_uv[i] = raw[i];
    prv_save_cache(s_selected_preset);
    layer_mark_dirty(s_graph_layer);
  }

  Tuple *status_t = dict_find(iter, MESSAGE_KEY_STATUS);
  if (!status_t) return;

  s_status = (AppStatus)status_t->value->int32;

  if (s_status == STATUS_READY) {
    /* Reset optional component counts; only set if keys are present in message */
    s_weather_ind_count = 0;
    s_wind_count = 0;
    s_wind_gust_count = 0;
    s_cloud_count = 0;
    s_sun_count = 0;
    s_sun_bmin_count = 0;
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
    Tuple *bmin_t = dict_find(iter, MESSAGE_KEY_SUN_BOUNDARY_MIN);
    if (bmin_t && bmin_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)bmin_t->length < MAX_TEMPS ? (int)bmin_t->length : MAX_TEMPS;
      s_sun_bmin_count = n;
      const uint8_t *raw = (const uint8_t *)bmin_t->value->data;
      for (int i = 0; i < n; i++) s_sun_bmin[i] = raw[i];
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
    Tuple *wind_t = dict_find(iter, MESSAGE_KEY_WEATHER_INDICATOR);
    if (wind_t && wind_t->type == TUPLE_BYTE_ARRAY) {
      int n = (int)wind_t->length < MAX_TEMPS ? (int)wind_t->length : MAX_TEMPS;
      s_weather_ind_count = n;
      const uint8_t *raw = (const uint8_t *)wind_t->value->data;
      for (int i = 0; i < n; i++) s_weather_ind[i] = raw[i];
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
    prv_save_cache(s_selected_preset);
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

static const char *prv_compass(uint8_t dir_byte) {
  static const char *dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
  int deg = (int)dir_byte * 360 / 254;
  return dirs[((deg + 22) / 45) % 8];
}

/* Format hour for x-axis label: "14" (24h) or "2p" (12h) */
static void prv_fmt_hour(char *buf, int sz, int h24) {
  if (s_settings.time_format == 0) {
    snprintf(buf, sz, "%02d", h24);
  } else {
    int h12 = h24 % 12; if (h12 == 0) h12 = 12;
    snprintf(buf, sz, "%d%c", h12, h24 < 12 ? 'a' : 'p');
  }
}

/* Format HH:MM for popup: "17:06" (24h) or "5:06p" (12h) */
static void prv_fmt_hhmm(char *buf, int sz, int h24, int min) {
  if (s_settings.time_format == 0) {
    snprintf(buf, sz, "%02d:%02d", h24, min);
  } else {
    int h12 = h24 % 12; if (h12 == 0) h12 = 12;
    snprintf(buf, sz, "%d:%02d%c", h12, min, h24 < 12 ? 'a' : 'p');
  }
}

/* Format date: "5.6." (DD.MM.) or "6/5" (MM/DD) */
static void prv_fmt_date(char *buf, int sz, int dd, int mm) {
  if (s_settings.date_format == 0) {
    snprintf(buf, sz, "%d.%d.", dd, mm);
  } else {
    snprintf(buf, sz, "%d/%d", mm, dd);
  }
}

/* Format popup header hour: "Mon 14:00" (24h) or "Mon 2PM" (12h) */
static void prv_fmt_hdr(char *buf, int sz, const char *day, int h24) {
  if (s_settings.time_format == 0) {
    snprintf(buf, sz, "%s %02d:00", day, h24);
  } else {
    int h12 = h24 % 12; if (h12 == 0) h12 = 12;
    snprintf(buf, sz, "%s %d%s", day, h12, h24 < 12 ? "am" : "pm");
  }
}

static void prv_touch_handler(const TouchEvent *event, void *ctx) {
  idle_reset();  /* touching/dragging the graph is active use */
  if (s_status != STATUS_READY || s_temp_count < 2) return;

  GRect bounds = layer_get_bounds(s_graph_layer);
  const int w  = bounds.size.w;
  const int vc = s_view_count;
  int n = (s_scroll_offset + vc <= s_temp_count) ? vc : (s_temp_count - s_scroll_offset);
  if (n < 1) return;

  if (event->type == TouchEvent_Liftoff) {
    s_touch_active = false;
    layer_mark_dirty(s_graph_layer);
    return;
  }

  int x = event->x;
  if (x < 0) x = 0;
  if (x >= w) x = w - 1;

  int hour_rel = x * n / w;
  int hour_abs = s_scroll_offset + hour_rel;
  if (hour_abs >= s_temp_count) hour_abs = s_temp_count - 1;

  if (event->type == TouchEvent_Touchdown) {
    s_touch_active = true;
    s_touch_abs    = hour_abs;
    layer_mark_dirty(s_graph_layer);
    return;
  }

  /* PositionUpdate: just update which hour we're pointing at */
  if (s_touch_abs != hour_abs) {
    s_touch_abs = hour_abs;
    layer_mark_dirty(s_graph_layer);
  }
}

#define SHOW(f) (s_zoom_days == 1 ? s_settings.show_##f##_z1 : s_settings.show_##f##_z5)

/* Returns a "nice" step for a scale with ≤ max_ticks intervals and step ≥ min_step.
   Steps follow the 1–2–5 × 10^n sequence.
   Sets *scale_max to ceil(max_val / step) * step. */
static int prv_nice_scale(int max_val, int max_ticks, int min_step, int *scale_max) {
  if (max_val < 1) max_val = 1;
  int raw = (max_val + max_ticks - 1) / max_ticks;
  if (raw < min_step) raw = min_step;
  /* Round raw up to nearest 1–2–5 × 10^n */
  int mag = 1;
  while (mag * 10 <= raw) mag *= 10;
  int n = (raw + mag - 1) / mag;  /* ceil(raw / mag), result in [1..10] */
  int nice;
  if      (n <= 1) nice = 1;
  else if (n <= 2) nice = 2;
  else if (n <= 5) nice = 5;
  else           { nice = 1; mag *= 10; }
  int step = nice * mag;
  *scale_max = ((max_val + step - 1) / step) * step;
  return step;
}

static void prv_graph_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int w  = bounds.size.w;
  const int h  = bounds.size.h;
  const int gt = TITLE_HEIGHT + (SHOW(cloud) && s_cloud_count > 0 ? CLOUD_HEIGHT : 0);
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
  if (SHOW(cloud) && s_cloud_count > 0) {
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
    (SHOW(sunrise_sunset) || SHOW(golden_hour) || SHOW(darkness));
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
  if (SHOW(precip) && s_precip_count > 0) {
    for (int i = 0; i < s_precip_count; i++) {
      int p = (int)s_precip[i];
      if (p > precip_max_p) precip_max_p = p;
    }
  }
  /* Apply nice scale to precip */
  int inch_tick_hund = 0;    /* tick step in hundredths-of-inch (0 = mm mode) */
  int precip_tick_step = 0;  /* tick step in tenths-of-mm (mm mode only) */
  if (s_settings.precip_unit == 1) {
    /* Inch: work in hundredths-of-inch */
    int max_hund = (precip_max_p * 100 + 253) / 254;
    int scale_hund;
    inch_tick_hund = prv_nice_scale(max_hund, 4, 5, &scale_hund);
    precip_max_p = (scale_hund * 254 + 99) / 100;
  } else {
    /* mm: work in tenths-of-mm, min step = 10 tenths (= 1 mm) */
    precip_tick_step = prv_nice_scale(precip_max_p, 4, 10, &precip_max_p);
  }

  /* ---- precipitation axis ticks (lines only, labels drawn later on top) ---- */
  if (SHOW(precip) && s_precip_count > 0) {
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
      for (int p = precip_tick_step; p <= precip_max_p; p += precip_tick_step) {
        int by = precip_top + p * precip_max_bar_h / precip_max_p;
        if (by < precip_top || by > precip_top + precip_max_bar_h) continue;
        graphics_draw_line(ctx, GPoint(w - 13, by), GPoint(w, by));
      }
    }
  }

  /* ---- precipitation bars (hanging from precip_top, growing downward) ---- */
  if (SHOW(precip) && s_precip_count > 0) {
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

  /* ---- weather indicators (snow/sleet and lightning) ---- */
  if (SHOW(weather_ind) && s_weather_ind_count > 0) {
    int icon_r = 5;  /* half-size for overlap detection */
    int last_icon_right = -999;
    int run_start = -1;
    int run_type  = 0;

    for (int i = 0; i <= n; i++) {
      int abs_i = view_start + i;
      int ind = (i < n && abs_i < s_weather_ind_count) ? (int)s_weather_ind[abs_i] : 0;

      /* Flush completed run */
      if (run_type > 0 && (ind != run_type || i == n)) {
        int run_end = i;  /* exclusive */
        int cx = (X(run_start) + X(run_end)) / 2;

        /* Max precip bar height in run → y position */
        int max_bh = 0;
        if (SHOW(precip) && s_precip_count > 0 && precip_max_p > 0) {
          for (int j = run_start; j < run_end; j++) {
            int aj = view_start + j;
            if (aj < s_precip_count) {
              int bh = (int)s_precip[aj] * precip_max_bar_h / precip_max_p;
              if (bh > max_bh) max_bh = bh;
            }
          }
        }
        int iy = (SHOW(precip) && s_precip_count > 0)
                 ? (precip_top + max_bh + icon_r + 2)
                 : (gt + icon_r + 2);

        if (iy + icon_r <= gb && cx - icon_r > last_icon_right) {
          GColor ind_color = (run_type == 2)
            ? PBL_IF_COLOR_ELSE(GColorOrange,             GColorWhite)
            : PBL_IF_COLOR_ELSE(GColorVividCerulean,   GColorWhite);
          graphics_context_set_stroke_color(ctx, ind_color);

          /* Range lines, leaving a gap around the icon */
          int lx0 = X(run_start);
          int lx1 = X(run_end);
          if (cx - icon_r - 2 > lx0)
            graphics_draw_line(ctx, GPoint(lx0, iy), GPoint(cx - icon_r - 2, iy));
          if (cx + icon_r + 2 < lx1)
            graphics_draw_line(ctx, GPoint(cx + icon_r + 2, iy), GPoint(lx1 - 1, iy));

          if (run_type == 1) {
            /* Snowflake: 4 crossed lines (horizontal, vertical, two diagonals) */
            int a = 4;
            graphics_draw_line(ctx, GPoint(cx - a, iy),     GPoint(cx + a, iy));
            graphics_draw_line(ctx, GPoint(cx, iy - a),     GPoint(cx, iy + a));
            graphics_draw_line(ctx, GPoint(cx - a + 1, iy - a + 1), GPoint(cx + a - 1, iy + a - 1));
            graphics_draw_line(ctx, GPoint(cx + a - 1, iy - a + 1), GPoint(cx - a + 1, iy + a - 1));
          } else if (run_type == 2) {
            /* Lightning bolt: Z-shaped zigzag */
            graphics_context_set_stroke_width(ctx, 2);
            graphics_draw_line(ctx, GPoint(cx + 1, iy - 4), GPoint(cx - 2, iy));
            graphics_draw_line(ctx, GPoint(cx - 2, iy),     GPoint(cx + 2, iy));
            graphics_draw_line(ctx, GPoint(cx + 2, iy),     GPoint(cx - 1, iy + 4));
            graphics_context_set_stroke_width(ctx, 1);
          }

          last_icon_right = cx + icon_r;
        }

        run_start = -1;
        run_type  = 0;
      }

      /* Start new run */
      if (i < n && ind > 0 && run_type == 0) {
        run_start = i;
        run_type  = ind;
      }
    }
  }

  /* ---- wind speed/gust bars + direction arrows ---- */
  int wind_max_spd_ms = 5;    /* raw m/s — hoisted for label section */
  int wind_max_disp   = 5;    /* display-unit max — hoisted for label section */
  int wind_step       = 5;    /* display-unit tick step — hoisted */
  int wind_scale_top_y = -1;  /* hoisted: needed for unit label position */
  if (SHOW(wind) && s_wind_count > 0 && s_wind_gust_count > 0) {
    for (int i = 0; i < s_wind_count; i++) {
      if (s_wind_speed[i] != 255 && (int)s_wind_speed[i] > wind_max_spd_ms)
        wind_max_spd_ms = s_wind_speed[i];
    }
    for (int i = 0; i < s_wind_gust_count; i++) {
      if (s_wind_gust[i] != 255 && (int)s_wind_gust[i] > wind_max_spd_ms)
        wind_max_spd_ms = s_wind_gust[i];
    }
    wind_max_spd_ms = ((wind_max_spd_ms + 4) / 5) * 5;
    int wind_min_step = (s_settings.wind_unit == 1) ? 20 :
                        (s_settings.wind_unit == 2) ? 10 : 5;
    wind_max_disp = prv_wind_conv(wind_max_spd_ms);
    wind_step = prv_nice_scale(wind_max_disp, 4, wind_min_step, &wind_max_disp);
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
  if (SHOW(humidity) && s_humidity_count > 0) {
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
    /* ---- humidity local peak labels removed (available in touch panel) ---- */
#undef HUM_Y
  }

  /* ---- UV index curve (orange, bottom half of graph area) ---- */
  if (SHOW(uv) && s_uv_count > 0) {
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
    /* ---- UV daily peak labels removed (available in touch panel) ---- */
#undef UV_Y
  }

  /* ---- wind scale labels (drawn after temp curve so they appear on top) ---- */
  if (SHOW(wind) && s_wind_count > 0 && s_wind_gust_count > 0) {
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
  if (SHOW(precip) && s_precip_count > 0) {
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
      for (int p = precip_tick_step; p <= precip_max_p; p += precip_tick_step) {
        int by = precip_top + p * precip_max_bar_h / precip_max_p;
        if (by < precip_top || by > precip_top + precip_max_bar_h) continue;
        if (mm_bot_by < 0 || by > mm_bot_by) mm_bot_by = by;
        if (mm_top_by < 0 || by < mm_top_by) mm_top_by = by;
        char lbl[6]; snprintf(lbl, sizeof(lbl), "%d", p / 10);
        DRAW_SHADOWED(lbl, f_medium, GRect(w - 30, by - 16, 28, 18),
                      GTextOverflowModeWordWrap, GTextAlignmentRight);
      }
    }
    if (mm_bot_by >= 0) {
      const char *plbl = (s_settings.precip_unit == 1) ? "in" : "mm";
      DRAW_SHADOWED(plbl, f_tiny, GRect(w - 26, mm_bot_by, 26, 12),
                    GTextOverflowModeWordWrap, GTextAlignmentRight);
    }
  }

  /* ---- sun condition bars + sunrise/sunset ticks ---- */
  if (s_sun_count > 0 && (SHOW(golden_hour) || SHOW(darkness) || SHOW(sunrise_sunset))) {
    const int sun_y    = y_low + 2;   /* top of 2px bar */
    const int bar_sw = 2;
    const int bar_y  = sun_y + bar_sw / 2;  /* center of bar for stroke drawing */
    graphics_context_set_stroke_width(ctx, bar_sw);
    for (int i = 0; i < n; i++) {
      int abs_i = view_start + i;
      if (abs_i >= s_sun_count) break;
      uint8_t sc = s_sun_cond[abs_i];

      /* decode base condition and optional tick */
      uint8_t base = sc;
      int tick_min = -1;
      bool tick_rise = false;
      if (sc >= 160) { base = 1; tick_min = sc - 160; tick_rise = false; }
      else if (sc >= 100) { base = 1; tick_min = sc - 100; tick_rise = true; }

      int bx = X(i);
      int bw = X(i + 1) - bx;
      if (bw < 1) bw = 1;

      /* determine bar color, and whether this is a golden or dark bar slot */
      bool is_golden = (base == 1);
      bool is_dark   = (base == 2);

      /* For sc==0 hours: check if a boundary starts here (partial bar at end of slot) */
      if (!is_golden && !is_dark && abs_i < s_sun_bmin_count) {
        uint8_t bv = s_sun_bmin[abs_i];
        if (bv <= 59)                     { is_golden = true; }
        else if (bv >= 128 && bv <= 187)  { is_dark   = true; }
      }

      /* Compute x range within this slot, clipped to minute boundaries */
      int x_start = bx;
      int x_end   = bx + bw - 1;
      if (is_golden || is_dark) {
        if (abs_i < s_sun_bmin_count) {
          uint8_t bv = s_sun_bmin[abs_i];
          if (is_golden && bv <= 59) {
            /* golden starts partway through this (non-golden) hour */
            x_start = bx + bw * bv / 60;
          } else if (is_golden && bv >= 64 && bv <= 123) {
            /* golden ends partway through this (golden) hour */
            x_end = bx + bw * (bv - 64) / 60 - 1;
          } else if (is_dark && bv >= 128 && bv <= 187) {
            /* dark starts partway through this (non-dark) hour */
            x_start = bx + bw * (bv - 128) / 60;
          } else if (is_dark && bv >= 192 && bv <= 251) {
            /* dark ends partway through this (dark) hour */
            x_end = bx + bw * (bv - 192) / 60 - 1;
          }
        }
      }

      /* draw bar */
      bool draw_bar = is_golden ? SHOW(golden_hour) : (is_dark ? SHOW(darkness) : false);
      if (draw_bar && x_end >= x_start) {
        GColor bar_color = is_dark ? GColorDarkGray : GColorOrange;
        graphics_context_set_stroke_color(ctx, bar_color);
        graphics_draw_line(ctx, GPoint(x_start, bar_y), GPoint(x_end, bar_y));
      } else if (base == 1 && !SHOW(golden_hour) && SHOW(sunrise_sunset) && tick_min >= 0) {
        /* Stub when show_golden_hour off: short bar around tick position */
        int tx = bx + bw * tick_min / 60;
        graphics_context_set_stroke_color(ctx, GColorOrange);
        graphics_draw_line(ctx, GPoint(tx - 5, bar_y), GPoint(tx + 4, bar_y));
      }

      /* draw tick */
      if (SHOW(sunrise_sunset) && tick_min >= 0) {
        int tx = bx + bw * tick_min / 60;
        graphics_context_set_stroke_color(ctx, GColorOrange);
        graphics_context_set_stroke_width(ctx, 2);
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
      char lbl[5]; prv_fmt_hour(lbl, sizeof(lbl), th);
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
        char date_part[8]; prv_fmt_date(date_part, sizeof(date_part), dd, mm);
        snprintf(day_lbl, sizeof(day_lbl), "%s %s", day_names[weekday], date_part);
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
      char date_lbl[8]; prv_fmt_date(date_lbl, sizeof(date_lbl), dd, mm);
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
        /* Keep label above the time axis tick labels */
        const int min_lbl_y_max = gb - TLABEL_HEIGHT - 14;
        if (lbl_y > min_lbl_y_max) lbl_y = min_lbl_y_max;
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

  /* ---- touch popup ---- */
  if (s_touch_active && s_temp_count >= 2) {
    const int abs_i   = s_touch_abs;
    const int popup_w = 74;                                  /* fits box+value+unit snugly */
    const int touch_px = (abs_i - view_start) * w / n;
    const bool popup_left = (touch_px >= w / 2);             /* popup left when touch is right of center */
    const int px = popup_left ? 0 : (w - popup_w);

    /* White panel */
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(px, TITLE_HEIGHT, popup_w, h - TITLE_HEIGHT), 0, GCornerNone);
    /* Border on the inner edge */
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    int border_x = popup_left ? popup_w : (w - popup_w);
    graphics_draw_line(ctx, GPoint(border_x, TITLE_HEIGHT), GPoint(border_x, h));

    /* Vertical indicator at touched hour */
    graphics_context_set_stroke_color(ctx, GColorDarkCandyAppleRed);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, GPoint(touch_px, TITLE_HEIGHT), GPoint(touch_px, h));

    /* Content fonts */
    GFont fp = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    GFont ft = fonts_get_system_font(FONT_KEY_GOTHIC_09);   /* units only */

    /* box(6) + gap(2) + value(36) + gap(2) + unit(26) = 72px total from px */
    const int c_bx = px + 4;
    const int c_vx = px + 13, c_vw = 36;                   /* left-aligned */
    const int c_ux = px + 48, c_uw = 24;

    int row = 0;
    const int row_h = 14;

    /* Header: fixed position just inside the panel */
    const int hdr_y  = TITLE_HEIGHT + 2;
    const int sep_y  = hdr_y + row_h + 2;       /* 2px gap below header text */
    const int y0     = sep_y + 4;               /* data rows start 4px below separator */

#define PROW(val_, unit_, box_color_) do { \
  int _y = y0 + (row) * row_h; \
  graphics_context_set_fill_color(ctx, box_color_); \
  graphics_fill_rect(ctx, GRect(c_bx, _y + (row_h - 6) / 2 + 1, 6, 6), 0, GCornerNone); \
  graphics_context_set_text_color(ctx, GColorBlack); \
  graphics_draw_text(ctx, val_, fp, GRect(c_vx, _y - 2, c_vw, row_h), \
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL); \
  graphics_context_set_text_color(ctx, GColorBlack); \
  graphics_draw_text(ctx, unit_, ft, GRect(c_ux, _y + 2, c_uw, row_h), \
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL); \
  row++; \
} while(0)

    /* Header: weekday + hour */
    {
      static const char *pday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      int ah  = s_local_start_h + abs_i;
      int wd  = (s_local_start_wday + ah / 24) % 7;
      int hod = ah % 24;
      char hdr[12]; prv_fmt_hdr(hdr, sizeof(hdr), pday[wd], hod);
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, hdr, fp,
        GRect(px + 3, hdr_y, popup_w - 6, row_h),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
      /* Separator */
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_line(ctx, GPoint(px + 2, sep_y), GPoint(px + popup_w - 3, sep_y));
    }

    /* Cloud */
    if (abs_i < s_cloud_count && s_cloud[abs_i] != 255) {
      char val[5]; snprintf(val, sizeof(val), "%d", (int)s_cloud[abs_i]);
      PROW(val, "%", GColorLightGray);
    }
    /* Precipitation */
    if (abs_i < s_precip_count) {
      int p = (int)s_precip[abs_i];
      char val[8];
      const char *unit;
      if (s_settings.precip_unit == 1) {
        int hund = p * 254 / 1000;
        snprintf(val, sizeof(val), "%d.%02d", hund / 100, hund % 100);
        unit = "in";
      } else {
        snprintf(val, sizeof(val), "%d.%d", p / 10, p % 10);
        unit = "mm";
      }
      PROW(val, unit, GColorVividCerulean);
    }
    /* Humidity */
    if (abs_i < s_humidity_count && s_humidity[abs_i] != 255) {
      char val[5]; snprintf(val, sizeof(val), "%d", (int)s_humidity[abs_i]);
      PROW(val, "%", GColorCobaltBlue);
    }
    /* Temperature */
    if (abs_i < s_temp_count) {
      int tv = s_settings.temp_unit
        ? ((int)s_temps[abs_i] * 9 / 5 + 32) : (int)s_temps[abs_i];
      char val[6]; snprintf(val, sizeof(val), "%d", tv);
      PROW(val, s_settings.temp_unit ? "\xc2\xb0""F" : "\xc2\xb0""C", GColorDarkCandyAppleRed);
    }
    /* Wind: "spd-gust" range in value column, unit + arrow */
    if (abs_i < s_wind_count && s_wind_speed[abs_i] != 255) {
      int spd_disp = prv_wind_conv((int)s_wind_speed[abs_i]);
      const char *wunit = (s_settings.wind_unit == 1) ? "km/h" :
                          (s_settings.wind_unit == 2) ? "mph" : "m/s";
      char val[12];
      bool has_gust = (abs_i < s_wind_gust_count && s_wind_gust[abs_i] != 255);
      int gust_disp = has_gust ? prv_wind_conv((int)s_wind_gust[abs_i]) : spd_disp;
      if (has_gust && gust_disp != spd_disp) {
        snprintf(val, sizeof(val), "%d-%d", spd_disp, gust_disp);
      } else {
        snprintf(val, sizeof(val), "%d", spd_disp);
      }
      PROW(val, wunit, GColorClear);   /* box drawn below as arrow */
      if (s_wind_arrow && abs_i < s_wind_count && s_wind_dir[abs_i] != 255) {
        int32_t from_deg = (int32_t)s_wind_dir[abs_i] * 360 / 254;
        int32_t to_deg   = (from_deg + 180) % 360;
        int arrow_x = c_bx + 3;   /* centre of the box slot */
        int arrow_y = y0 + (row - 1) * row_h + row_h / 2 + 1;
        gpath_rotate_to(s_wind_arrow, (int32_t)(TRIG_MAX_ANGLE) * to_deg / 360);
        gpath_move_to(s_wind_arrow, GPoint(arrow_x, arrow_y));
        graphics_context_set_fill_color(ctx, GColorDarkGray);
        gpath_draw_filled(ctx, s_wind_arrow);
        graphics_context_set_stroke_color(ctx, GColorDarkGray);
        gpath_draw_outline(ctx, s_wind_arrow);
      }
    }
    /* UV: value + "UVI" unit */
    if (abs_i < s_uv_count && s_uv[abs_i] != 255) {
      char val[4]; snprintf(val, sizeof(val), "%d", (int)s_uv[abs_i]);
      PROW(val, "UVI", GColorOrange);
    }
    /* Sun rows — sunrise/sunset tick, golden hour range, darkness range */
    if (abs_i < s_sun_count) {
      uint8_t sc   = s_sun_cond[abs_i];
      uint8_t base = (sc >= 100) ? 1 : sc;
      /* If sc==0 but sun boundary starts during this hour, show that range */
      bool is_start_boundary = false;
      if (base == 0 && abs_i < s_sun_bmin_count) {
        uint8_t bv = s_sun_bmin[abs_i];
        if (bv <= 59)                    { base = 1; is_start_boundary = true; }
        else if (bv >= 128 && bv <= 187) { base = 2; is_start_boundary = true; }
      }
      /* Precompute range bounds for base condition */
      int r0 = abs_i, r1 = abs_i;
      if (base > 0) {
        if (is_start_boundary) {
          /* abs_i is the non-golden/dark hour containing the start; first full hour is abs_i+1 */
          r0 = abs_i + 1;
          r1 = r0;
          while (r1+1 < s_sun_count && (((s_sun_cond[r1+1] >= 100) ? 1u : (uint8_t)s_sun_cond[r1+1]) == base)) r1++;
        } else {
          r0 = abs_i; r1 = abs_i;
          while (r0 > 0 && (((s_sun_cond[r0-1] >= 100) ? 1u : (uint8_t)s_sun_cond[r0-1]) == base)) r0--;
          while (r1+1 < s_sun_count && (((s_sun_cond[r1+1] >= 100) ? 1u : (uint8_t)s_sun_cond[r1+1]) == base)) r1++;
        }
      }
      /* Detect steep-entry: sc>=100 but sunBoundaryMin says golden/dark STARTS this hour,
         meaning abs_i itself is the entry hour (el0 was outside golden/dark) */
      bool steep_entry = (sc >= 100 && abs_i < s_sun_bmin_count && s_sun_bmin[abs_i] <= 59 && base == 1);
      /* Golden hour start / sunrise-sunset tick / golden hour end — three rows */
      if (base == 1) {
        /* Golden start: in abs_i itself (steep entry) or in the hour before r0 */
        int bmin_idx = steep_entry ? abs_i : (r0 - 1);
        int sh, sm = 0;
        if (bmin_idx >= 0 && bmin_idx < s_sun_bmin_count && s_sun_bmin[bmin_idx] <= 59) {
          sh = (s_local_start_h + bmin_idx) % 24;
          sm = s_sun_bmin[bmin_idx];
        } else {
          sh = (s_local_start_h + r0) % 24;
        }
        char tstart[8]; prv_fmt_hhmm(tstart, sizeof(tstart), sh, sm);
        PROW(tstart, "gold>", GColorOrange);
      }
      if (sc >= 100) {
        int tick_min = (sc < 160) ? (sc - 100) : (sc - 160);
        bool is_rise = (sc < 160);
        int tick_h   = (s_local_start_h + abs_i) % 24;
        char tval[8]; prv_fmt_hhmm(tval, sizeof(tval), tick_h, tick_min);
        PROW(tval, is_rise ? "rise" : "set", GColorOrange);
      } else if (base == 1) {
        /* Scan golden range for the tick hour and show it */
        for (int ri = r0; ri <= r1; ri++) {
          if (ri < s_sun_count && s_sun_cond[ri] >= 100) {
            uint8_t rsc = s_sun_cond[ri];
            int tick_min = (rsc < 160) ? (rsc - 100) : (rsc - 160);
            bool is_rise = (rsc < 160);
            int tick_h   = (s_local_start_h + ri) % 24;
            char tval[8]; prv_fmt_hhmm(tval, sizeof(tval), tick_h, tick_min);
            PROW(tval, is_rise ? "rise" : "set", GColorOrange);
            break;
          }
        }
      }
      if (base == 1) {
        int eh = (s_local_start_h + r1) % 24, em = 0;
        if (r1 < s_sun_bmin_count && s_sun_bmin[r1] >= 64 && s_sun_bmin[r1] <= 123) {
          em = s_sun_bmin[r1] - 64;
        } else {
          eh = (s_local_start_h + r1 + 1) % 24;
        }
        char tend[8]; prv_fmt_hhmm(tend, sizeof(tend), eh, em);
        PROW(tend, "<gold", GColorOrange);
      }
      /* Darkness: show start and end on separate rows */
      if (base == 2) {
        /* Darkness start is in the hour BEFORE r0 (el crosses into dark there) */
        int sbmin_idx = r0 - 1;
        int sh, sm = 0;
        if (sbmin_idx >= 0 && sbmin_idx < s_sun_bmin_count &&
            s_sun_bmin[sbmin_idx] >= 128 && s_sun_bmin[sbmin_idx] <= 187) {
          sh = (s_local_start_h + sbmin_idx) % 24;
          sm = s_sun_bmin[sbmin_idx] - 128;
        } else {
          sh = (s_local_start_h + r0) % 24;
        }
        int eh = (s_local_start_h + r1) % 24, em = 0;
        if (r1 < s_sun_bmin_count && s_sun_bmin[r1] >= 192 && s_sun_bmin[r1] <= 251) {
          em = s_sun_bmin[r1] - 192;
        } else {
          eh = (s_local_start_h + r1 + 1) % 24;
        }
        char tstart[8]; prv_fmt_hhmm(tstart, sizeof(tstart), sh, sm);
        char tend[8];   prv_fmt_hhmm(tend,   sizeof(tend),   eh, em);
        PROW(tstart, "dark>", GColorDarkGray);
        PROW(tend,   "<dark", GColorDarkGray);
      }
    }

#undef PROW
  }

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
    char tstr[8];
    if (s_settings.time_format == 0) {
      strftime(tstr, sizeof(tstr), "%H:%M", lt);
    } else {
      int h12 = lt->tm_hour % 12; if (h12 == 0) h12 = 12;
      snprintf(tstr, sizeof(tstr), "%d:%02d%s", h12, lt->tm_min, lt->tm_hour < 12 ? "am" : "pm");
    }
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, tstr, f_small,
                       GRect(w - 48, -1, 46, 14),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
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
  idle_reset();
  s_zoom_days = (s_zoom_days == 1) ? 5 : 1;
  int vc_target = s_zoom_days * 24;
  /* Fix the data index at 1/4 screen width */
  s_zoom_anchor = s_scroll_offset + s_view_count / 4;
  prv_start_zoom_anim(vc_target);
}

static void prv_down_click(ClickRecognizerRef r, void *ctx) {
  idle_reset();
  int step = (s_zoom_days == 1) ? 6 : 24;
  int view_count = (s_zoom_days == 1) ? 24 : (s_zoom_days * 24);
  int max_scroll = s_temp_count - view_count;
  if (max_scroll < 0) max_scroll = 0;
  int target = s_scroll_target + step;
  if (target > max_scroll) target = max_scroll;
  prv_start_scroll_anim(target);
}

static void prv_up_click(ClickRecognizerRef r, void *ctx) {
  idle_reset();
  int step = (s_zoom_days == 1) ? 6 : 24;
  int target = s_scroll_target - step;
  if (target < 0) target = 0;
  prv_start_scroll_anim(target);
}

/* ---------- location menu ---------- */

#define MAX_MENU_ROWS 6  /* GPS + 5 presets */

static Window          *s_loc_menu_window = NULL;
static SimpleMenuLayer *s_loc_menu_layer  = NULL;
static SimpleMenuItem   s_loc_menu_items[MAX_MENU_ROWS];
static SimpleMenuSection s_loc_menu_section;
static int              s_loc_menu_count  = 0;
static int              s_loc_menu_indices[MAX_MENU_ROWS]; /* maps row → preset idx (0=GPS) */



static void prv_loc_menu_select(int index, void *ctx) {
  idle_reset();
  s_selected_preset = s_loc_menu_indices[index];
  window_stack_pop(true);
  s_status = STATUS_LOADING;
  prv_update_status_layer();
  prv_request_data();
}

static void prv_loc_menu_window_load(Window *window) {
  s_loc_menu_count = 0;

  /* Row 0: GPS */
  s_loc_menu_items[s_loc_menu_count] = (SimpleMenuItem){
    .title = "GPS location",
    .callback = prv_loc_menu_select,
  };
  s_loc_menu_indices[s_loc_menu_count] = 0;
  s_loc_menu_count++;

  /* Rows 1-5: configured presets */
  for (int i = 0; i < 5; i++) {
    if (s_preset_names[i][0] != '\0') {
      s_loc_menu_items[s_loc_menu_count] = (SimpleMenuItem){
        .title = s_preset_names[i],
        .callback = prv_loc_menu_select,
      };
      s_loc_menu_indices[s_loc_menu_count] = i + 1;
      s_loc_menu_count++;
    }
  }

  s_loc_menu_section = (SimpleMenuSection){
    .title = "Location",
    .items = s_loc_menu_items,
    .num_items = s_loc_menu_count,
  };

  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_loc_menu_layer = simple_menu_layer_create(bounds, window, &s_loc_menu_section, 1, NULL);

  /* Highlight current selection */
  for (int i = 0; i < s_loc_menu_count; i++) {
    if (s_loc_menu_indices[i] == s_selected_preset) {
      menu_layer_set_selected_index(simple_menu_layer_get_menu_layer(s_loc_menu_layer),
                                    MenuIndex(0, i), MenuRowAlignCenter, false);
      break;
    }
  }
  layer_add_child(root, simple_menu_layer_get_layer(s_loc_menu_layer));
}

static void prv_loc_menu_window_unload(Window *window) {
  simple_menu_layer_destroy(s_loc_menu_layer);
  s_loc_menu_layer = NULL;
  window_destroy(s_loc_menu_window);
  s_loc_menu_window = NULL;
}

static void prv_open_loc_menu(ClickRecognizerRef r, void *ctx) {
  idle_reset();
  if (s_loc_menu_window) return;  /* already open */
  s_loc_menu_window = window_create();
  window_set_window_handlers(s_loc_menu_window, (WindowHandlers){
    .load      = prv_loc_menu_window_load,
    .unload    = prv_loc_menu_window_unload,
    .appear    = prv_idle_appear,
    .disappear = prv_idle_disappear,
  });
  window_stack_push(s_loc_menu_window, true);
}

static void prv_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, prv_open_loc_menu, NULL);
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

  touch_service_subscribe(prv_touch_handler, NULL);
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
  touch_service_unsubscribe();
  gpath_destroy(s_wind_arrow);
  s_wind_arrow = NULL;
  layer_destroy(s_graph_layer);
  text_layer_destroy(s_status_layer);
}

/* ---------- init / deinit ---------- */

static void prv_init(void) {
  prv_load_settings();

  if (persist_exists(IDLE_EXIT_KEY)) {
    s_idle_timeout_sec = persist_read_int(IDLE_EXIT_KEY);  /* else keep default 15 */
  }

  /* Apply startup view setting */
  if (s_settings.startup_view == 0) {
    s_zoom_days = 1;
  } else if (s_settings.startup_view == 1) {
    s_zoom_days = 5;
  } else if (s_settings.startup_view == 2) {
    s_zoom_days = prv_load_last_view();
  }
  /* Keep initial render window in sync with selected startup zoom. */
  s_view_count = s_zoom_days * 24;

  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_open(app_message_inbox_size_maximum(),
                   app_message_outbox_size_maximum());

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load      = prv_window_load,
    .unload    = prv_window_unload,
    .appear    = prv_idle_appear,     /* arms the idle timer on launch/re-reveal */
    .disappear = prv_idle_disappear,
  });
  window_stack_push(s_window, true);

  if (prv_load_cache(s_selected_preset)) {
    s_status = STATUS_READY;
    prv_update_status_layer();
    layer_mark_dirty(s_graph_layer);
  }

  prv_request_data();
}

static void prv_deinit(void) {
  prv_save_last_view();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
