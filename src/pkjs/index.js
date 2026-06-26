/* global Pebble, XMLHttpRequest */

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, function() {
  var clayConfig = this;

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    /* Section heading left padding */
    var style = document.createElement('style');
    style.textContent = '.component-heading { padding-left: 8px; }';
    document.head.appendChild(style);

    /* Autocomplete + single-line layout for preset location inputs */
    for (var i = 1; i <= 5; i++) {
      var item = clayConfig.getItemByMessageKey('PRESET_NAME_' + i);
      if (!item) continue;

      /* Inline-override the block layout to make label+field one row */
      var lbl = item.$element[0].querySelector('label.tap-highlight');
      if (lbl) {
        lbl.style.cssText = 'display:flex;align-items:center;padding:8px 16px;';
        var labelSpan = lbl.querySelector('.label');
        if (labelSpan) {
          labelSpan.style.cssText = 'flex-shrink:0;padding:0;margin-right:10px;white-space:nowrap;';
        }
        var inputSpan = lbl.querySelector('.input');
        if (inputSpan) {
          inputSpan.style.cssText = 'flex:1;min-width:0;margin:0;position:relative;';
        }
      }

      addGeoAutocomplete(item.$manipulatorTarget[0]);
    }

    /* Inline layout for cache max age input */
    var cacheItem = clayConfig.getItemByMessageKey('CACHE_MAX_AGE_HOURS');
    if (cacheItem) {
      var cacheLbl = cacheItem.$element[0].querySelector('label.tap-highlight');
      if (cacheLbl) {
        cacheLbl.style.cssText = 'display:flex;align-items:center;padding:8px 16px;';
        var cacheLabelSpan = cacheLbl.querySelector('.label');
        if (cacheLabelSpan) {
          cacheLabelSpan.style.cssText = 'flex-shrink:0;padding:0;margin-right:10px;white-space:nowrap;';
        }
        var cacheInputSpan = cacheLbl.querySelector('.input');
        if (cacheInputSpan) {
          cacheInputSpan.style.cssText = 'flex:1;min-width:0;margin:0;';
          var cacheInput = cacheInputSpan.querySelector('input');
          if (cacheInput) cacheInput.style.textAlign = 'right';
        }
      }
    }

    /* Side-by-side 1d/5d toggles for each data selection item */
    var pairs = [
      ['SHOW_CLOUD_Z1',        'SHOW_CLOUD_Z5',        'Cloud cover'],
      ['SHOW_PRECIP_Z1',       'SHOW_PRECIP_Z5',       'Precipitation'],
      ['SHOW_WEATHER_IND_Z1',  'SHOW_WEATHER_IND_Z5',  'Snow & lightning'],
      ['SHOW_HUMIDITY_Z1',     'SHOW_HUMIDITY_Z5',     'Relative humidity'],
      ['SHOW_WIND_Z1',         'SHOW_WIND_Z5',         'Wind'],
      ['SHOW_UV_Z1',           'SHOW_UV_Z5',           'UV index'],
      ['SHOW_GOLDEN_HOUR_Z1',  'SHOW_GOLDEN_HOUR_Z5',  'Golden hour'],
      ['SHOW_DARKNESS_Z1',     'SHOW_DARKNESS_Z5',     'Darkness'],
      ['SHOW_SUNRISE_SUNSET_Z1',    'SHOW_SUNRISE_SUNSET_Z5',    'Sunrise & sunset ticks']
    ];

    pairs.forEach(function(p) {
      var item1 = clayConfig.getItemByMessageKey(p[0]);
      var item5 = clayConfig.getItemByMessageKey(p[1]);
      if (!item1 || !item5) return;

      var el1 = item1.$element[0];
      var el5 = item5.$element[0];

      /* Move the input+graphic spans before the Z5 row is hidden */
      var inputSpan1 = el1.querySelector('.input');
      var inputSpan5 = el5.querySelector('.input');

      /* Replace the outer <label> with a <div> to prevent auto-toggle-on-click */
      var tapHL = el1.querySelector('.tap-highlight');
      var rowDiv = document.createElement('div');
      rowDiv.style.cssText = 'display:flex;align-items:center;padding:10px 16px;box-sizing:border-box;';
      /* Discard existing children — we'll build fresh */
      tapHL.parentNode.replaceChild(rowDiv, tapHL);

      /* Name label */
      var nameSpan = document.createElement('span');
      nameSpan.textContent = p[2];
      nameSpan.style.cssText = 'flex:1;font-size:16px;color:#fff;';
      rowDiv.appendChild(nameSpan);

      /* Remove the input spans from their current parents before moving */
      if (inputSpan1 && inputSpan1.parentNode) inputSpan1.parentNode.removeChild(inputSpan1);
      if (inputSpan5 && inputSpan5.parentNode) inputSpan5.parentNode.removeChild(inputSpan5);

      /* Build a toggle group: label caption above the toggle switch */
      function makeToggleGroup(caption, inputSpan) {
        var lbl = document.createElement('label');
        lbl.style.cssText = 'display:flex;flex-direction:column;align-items:center;' +
          'cursor:pointer;margin-left:14px;padding:0;';
        var cap = document.createElement('span');
        cap.textContent = caption;
        cap.style.cssText = 'font-size:10px;color:#888;margin-bottom:3px;line-height:1;';
        lbl.appendChild(cap);
        lbl.appendChild(inputSpan);
        return lbl;
      }

      rowDiv.appendChild(makeToggleGroup('1d', inputSpan1));
      rowDiv.appendChild(makeToggleGroup('5d', inputSpan5));

      /* Hide Z5 row (its checkbox was moved; the row is now empty) */
      el5.style.display = 'none';
    });

    /* Fix the Save button to the bottom of the viewport */
    var submitEl = document.querySelector('.submit-button, [type="submit"], .component-submit');
    if (!submitEl) {
      var allBtns = document.querySelectorAll('button, input[type="submit"]');
      for (var bi = 0; bi < allBtns.length; bi++) {
        var txt = allBtns[bi].textContent || allBtns[bi].value || '';
        if (txt.trim().toLowerCase() === 'save') { submitEl = allBtns[bi]; break; }
      }
    }
    if (submitEl) {
      var submitWrapper = submitEl;
      while (submitWrapper && submitWrapper.tagName !== 'LI' && submitWrapper.tagName !== 'BODY') {
        submitWrapper = submitWrapper.parentNode;
      }
      var fixedEl = (submitWrapper && submitWrapper.tagName === 'LI') ? submitWrapper : submitEl;
      var scrollContainer = fixedEl.parentNode;
      /* Fix the button to the bottom IN PLACE (keep inside the form so submission still works) */
      setTimeout(function() {
        var bg = (scrollContainer && window.getComputedStyle(scrollContainer).backgroundColor) ||
                 window.getComputedStyle(document.body).backgroundColor ||
                 '#1c1c1c';
        fixedEl.style.cssText = 'position:fixed;bottom:0;left:0;right:0;z-index:1000;' +
          'margin:0;border-top:1px solid #333;background:' + bg + ';';
        var btnH = fixedEl.offsetHeight || 56;
        document.body.style.cssText += 'margin:0;padding:0;overflow:hidden;height:100vh;';
        document.documentElement.style.cssText += 'overflow:hidden;height:100vh;';
        if (scrollContainer) {
          scrollContainer.style.cssText += 'overflow-y:auto;' +
            'height:calc(100vh - ' + btnH + 'px);' +
            'box-sizing:border-box;display:block;';
        }
      }, 0);
    }
  });

  function addGeoAutocomplete(input) {
    if (!input) return;
    var timer = null;

    /* Wrapper needs relative positioning for the dropdown */
    var wrapper = input.parentNode;
    wrapper.style.position = 'relative';

    var dropdown = document.createElement('div');
    dropdown.style.cssText = [
      'position:absolute', 'top:100%', 'left:0', 'right:0', 'z-index:9999',
      'background:#fff', 'border:1px solid #ccc', 'border-top:none',
      'box-shadow:0 2px 6px rgba(0,0,0,0.15)', 'max-height:180px',
      'overflow-y:auto', 'display:none', 'border-radius:0 0 4px 4px'
    ].join(';');
    wrapper.appendChild(dropdown);

    input.addEventListener('input', function() {
      clearTimeout(timer);
      var val = (input.value || '').trim();
      if (val.length < 2) { dropdown.style.display = 'none'; return; }
      timer = setTimeout(function() { fetchSuggestions(val); }, 300);
    });

    input.addEventListener('blur', function() {
      /* Small delay so click on dropdown item fires first */
      setTimeout(function() { dropdown.style.display = 'none'; }, 150);
    });

    function fetchSuggestions(query) {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', 'https://geocoding-api.open-meteo.com/v1/search?name=' +
        encodeURIComponent(query) + '&count=5&language=en&format=json');
      xhr.onload = function() {
        try {
          var data = JSON.parse(xhr.responseText);
          var results = data.results || [];
          dropdown.innerHTML = '';
          if (!results.length) { dropdown.style.display = 'none'; return; }
          results.forEach(function(r) {
            var parts = [r.name];
            if (r.admin1) parts.push(r.admin1);
            if (r.country) parts.push(r.country);
            var label = parts.join(', ');
            var row = document.createElement('div');
            row.textContent = label;
            row.style.cssText = 'padding:7px 10px;cursor:pointer;font-size:14px;' +
              'border-bottom:1px solid #f0f0f0;color:#222;';
            row.addEventListener('mousedown', function(e) {
              e.preventDefault();
              /* Store the place name as typed — geocoding will use this on the watch */
              input.value = r.name + (r.country ? ', ' + r.country : '');
              input.dispatchEvent(new Event('change', {bubbles: true}));
              dropdown.style.display = 'none';
            });
            row.addEventListener('mouseover', function() { row.style.background = '#e8f0fe'; });
            row.addEventListener('mouseout',  function() { row.style.background = ''; });
            dropdown.appendChild(row);
          });
          dropdown.style.display = 'block';
        } catch(e) { dropdown.style.display = 'none'; }
      };
      xhr.onerror = function() { dropdown.style.display = 'none'; };
      xhr.send();
    }
  }
});

var MAX_TEMPS = 240;
var DEBUG_FORCE_OPENMETEO = false;  /* set true to always use Open-Meteo endpoint */
var DEBUG_WEATHER_IND = false;      /* set true to inject fake snow & lightning ranges */
var DEBUG_COORDS = null;  /* set to an object to use test coordinates */
// var DEBUG_COORDS = { lat: 48.0941, lon: 7.9604, name: 'Waldkirch' };  /* Germany */
// var DEBUG_COORDS = { lat: 62.2426, lon: 25.7473, name: 'Jyvaskyla' };  /* Jyväskylä center */
// var DEBUG_COORDS = { lat: -33.9249, lon: 18.4241, name: 'Cape Town' };  /* Southern hemisphere */
var FMI_WFS_BASE = 'https://opendata.fmi.fi/wfs?service=WFS&version=2.0.0' +
  '&request=getFeature' +
  '&storedquery_id=fmi::forecast::edited::weather::scandinavia::point::timevaluepair' +
  '&parameters=Temperature,Precipitation1h,WindSpeedMS,WindDirection,HourlyMaximumGust,TotalCloudCover,Humidity,WeatherSymbol3&timestep=60';

var pendingFetch = false;

function solarElevationDeg(latDeg, lonDeg, date) {
  var JD = date.getTime() / 86400000.0 + 2440587.5;
  var T = (JD - 2451545.0) / 36525.0;
  var L0 = ((280.46646 + 36000.76983 * T) % 360 + 360) % 360;
  var M  = ((357.52911 + 35999.05029 * T) % 360 + 360) % 360;
  var Mrad = M * Math.PI / 180;
  var C = (1.914602 - 0.004817 * T) * Math.sin(Mrad)
        + 0.019993 * Math.sin(2 * Mrad)
        + 0.000289 * Math.sin(3 * Mrad);
  var sunLon = L0 + C;
  var omega = 125.04 - 1934.136 * T;
  var appLon = (sunLon - 0.00569 - 0.00478 * Math.sin(omega * Math.PI / 180)) * Math.PI / 180;
  var eps = (23.439291111 - 0.013004167 * T + 0.00256 * Math.cos(omega * Math.PI / 180)) * Math.PI / 180;
  var dec = Math.asin(Math.sin(eps) * Math.sin(appLon));
  var RA  = Math.atan2(Math.cos(eps) * Math.sin(appLon), Math.cos(appLon));
  var JD0 = Math.floor(JD - 0.5) + 0.5;
  var D0  = JD0 - 2451545.0;
  var T0  = D0 / 36525.0;
  var UT = date.getUTCHours() + date.getUTCMinutes() / 60.0 + date.getUTCSeconds() / 3600.0;
  var GMST = ((6.697374558 + 2400.0513369 * T0 + 1.00273790935 * UT) % 24 + 24) % 24;
  var HA = ((GMST * 15 + lonDeg - RA * 180 / Math.PI) % 360 + 360) % 360;
  if (HA > 180) HA -= 360;
  var latRad = latDeg * Math.PI / 180;
  var sinAlt = Math.sin(latRad) * Math.sin(dec) + Math.cos(latRad) * Math.cos(dec) * Math.cos(HA * Math.PI / 180);
  return Math.asin(Math.max(-1, Math.min(1, sinAlt))) * 180 / Math.PI;
}

function elToSunCond(el) {
  if (el < -18) return 2;
  if (el >= -4 && el <= 6) return 1;
  return 0;
}

/* Maps WMO weather code to indicator byte: 0=none, 1=snow/sleet, 2=lightning */
function weatherCodeToIndicator(code) {
  if (code === null || code === undefined || isNaN(code)) return 0;
  code = Math.round(code);
  if (code >= 95) return 2;  /* thunderstorm — lightning takes priority */
  if ((code >= 56 && code <= 57) ||  /* freezing drizzle */
      (code >= 66 && code <= 67) ||  /* freezing rain */
      (code >= 70 && code <= 79) ||  /* snow */
      (code >= 85 && code <= 86))    /* snow showers */
    return 1;
  return 0;
}

/* Returns 0/1/2 for normal/golden/dark, or 100+min for sunrise golden,
   160+min for sunset golden (min = 0-59 within the hour).
   Sunrise/sunset defined as upper limb crossing the horizon:
   solar center at -0.833° (= -0.267° radius + -0.567° refraction).
   Also handles steep-rise/set case where el enters golden AND crosses the
   horizon in the same hour. */
function sunConditionWithTick(lat, lon, startMs, i) {
  var el0 = solarElevationDeg(lat, lon, new Date(startMs + i * 3600000));
  var el1 = solarElevationDeg(lat, lon, new Date(startMs + (i + 1) * 3600000));
  var cond = elToSunCond(el0);
  var HORIZON = -0.833;  /* upper limb correction */
  function crossMin(thr) {
    return Math.max(0, Math.min(59, Math.round((thr - el0) / (el1 - el0) * 60)));
  }
  if (cond === 1) {
    /* el0 already in golden range: check for horizon crossing */
    if (el0 < HORIZON && el1 >= HORIZON) return 100 + crossMin(HORIZON);  /* sunrise */
    if (el0 >= HORIZON && el1 < HORIZON) return 160 + crossMin(HORIZON);  /* sunset  */
  } else {
    /* el0 outside golden: check if el enters golden AND crosses horizon this hour */
    if (el0 < -4 && el1 >= -4 && el1 > HORIZON) return 100 + crossMin(HORIZON);  /* steep sunrise */
    if (el0 > 6  && el1 <= 6  && el1 < HORIZON) return 160 + crossMin(HORIZON);  /* steep sunset  */
  }
  return cond;
}

/* Returns minute-precision boundary info for golden/dark transitions:
   0-59:    golden starts at minute (el crosses -4° up or +6° down)
   64-123:  golden ends at minute (val-64, el crosses +6° up or -4° down)
   128-187: dark starts at minute (val-128, el crosses -18° down)
   192-251: dark ends at minute (val-192, el crosses -18° up)
   255: no boundary */
function sunBoundaryMin(lat, lon, startMs, i) {
  var el0 = solarElevationDeg(lat, lon, new Date(startMs + i * 3600000));
  var el1 = solarElevationDeg(lat, lon, new Date(startMs + (i + 1) * 3600000));
  function crossMin(thr) {
    return Math.max(0, Math.min(59, Math.round((thr - el0) / (el1 - el0) * 60)));
  }
  if (el1 - el0 === 0) return 255;
  // Golden start: enters [-4,+6] from below (-4°) or above (+6°)
  if (el0 < -4 && el1 >= -4) return crossMin(-4);
  if (el0 > 6  && el1 <= 6 && el1 >= -4) return crossMin(6);
  // Golden end: leaves [-4,+6] upward (+6°) or downward (-4°)
  if (el0 <= 6  && el0 >= -4 && el1 > 6)  return 64 + crossMin(6);
  if (el0 >= -4 && el0 <= 6  && el1 < -4) return 64 + crossMin(-4);
  // Dark start/end: crosses -18°
  if (el0 > -18 && el1 <= -18) return 128 + crossMin(-18);
  if (el0 <= -18 && el1 > -18) return 192 + crossMin(-18);
  return 255;
}

function getPresetNames() {
  try {
    var settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
    return [
      (settings.PRESET_NAME_1 || '').trim(),
      (settings.PRESET_NAME_2 || '').trim(),
      (settings.PRESET_NAME_3 || '').trim(),
      (settings.PRESET_NAME_4 || '').trim(),
      (settings.PRESET_NAME_5 || '').trim()
    ];
  } catch(e) { return ['','','','','']; }
}

function sendPresetNames() {
  var names = getPresetNames();
  var msg = {};
  for (var i = 0; i < 5; i++) {
    if (names[i]) msg['PRESET_NAME_' + (i + 1)] = names[i].substring(0, 32);
  }
  if (Object.keys(msg).length > 0) {
    Pebble.sendAppMessage(msg,
      function() { console.log('Preset names sent'); },
      function(e) { console.log('Preset names send error: ' + JSON.stringify(e)); }
    );
  }
}

function geocodeAndFetch(name, presetIndex) {
  var url = 'https://geocoding-api.open-meteo.com/v1/search?name=' +
    encodeURIComponent(name) + '&count=1&language=en&format=json';
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url);
  xhr.onload = function() {
    try {
      var data = JSON.parse(xhr.responseText);
      if (data.results && data.results.length > 0) {
        var r = data.results[0];
        var displayName = r.name + (r.country ? ', ' + r.country : '');
        console.log('Geocoded "' + name + '" → ' + displayName + ' (' + r.latitude + ',' + r.longitude + ')');
        fetchForLatLon(r.latitude, r.longitude, displayName, presetIndex);
      } else {
        console.log('Geocoding: no results for "' + name + '"');
        sendStatus(2);
        pendingFetch = false;
      }
    } catch(e) {
      console.log('Geocoding parse error: ' + e);
      sendStatus(2);
      pendingFetch = false;
    }
  };
  xhr.onerror = function() {
    console.log('Geocoding request failed for "' + name + '"');
    sendStatus(2);
    pendingFetch = false;
  };
  xhr.send();
}

function getTempUnit() {
  try {
    var settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
    return parseInt(settings.TEMP_UNIT) || 0;  /* 0=Celsius, 1=Fahrenheit */
  } catch(e) {
    return 0;
  }
}

Pebble.addEventListener('ready', function () {
  console.log('PebbleKit JS ready');
  sendPresetNames();
  fetchForecast();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload.REQUEST_DATA !== undefined) {
    var preset = e.payload.SELECTED_PRESET;
    pendingFetch = false;  /* explicit watch request always overrides in-progress fetch */
    fetchForecast(typeof preset === 'number' ? preset : 0);
  }
});

function fetchForecast(presetIndex) {
  if (pendingFetch) return;
  pendingFetch = true;
  presetIndex = presetIndex || 0;

  if (DEBUG_COORDS) {
    if (DEBUG_FORCE_OPENMETEO) {
      fetchOpenMeteo(DEBUG_COORDS.lat, DEBUG_COORDS.lon, DEBUG_COORDS.name, presetIndex);
    } else {
      fetchForLatLon(DEBUG_COORDS.lat, DEBUG_COORDS.lon, DEBUG_COORDS.name, presetIndex);
    }
    return;
  }

  if (presetIndex >= 1 && presetIndex <= 5) {
    var names = getPresetNames();
    var name = names[presetIndex - 1];
    if (name) {
      geocodeAndFetch(name, presetIndex);
      return;
    }
    /* preset slot empty — fall through to GPS */
  }

  if (navigator.geolocation) {
    navigator.geolocation.getCurrentPosition(
      function (pos) {
        fetchForLatLon(pos.coords.latitude, pos.coords.longitude, null, presetIndex);
      },
      function () {
        fetchForLatLon(60.1699, 24.9384, 'Helsinki', presetIndex);
      },
      { timeout: 10000, maximumAge: 300000 }
    );
  } else {
    fetchForLatLon(60.1699, 24.9384, 'Helsinki', presetIndex);
  }
}

function fetchForLatLon(lat, lon, fallbackName, presetIndex) {
  var now = new Date();
  var startTime = new Date(now);
  startTime.setHours(0, 0, 0, 0);  /* 00:00 of current day */
  startTime.setDate(startTime.getDate() - 1);  /* back one day → 00:00 yesterday */

  var endTime = new Date(startTime.getTime() + MAX_TEMPS * 60 * 60 * 1000);

  var suffix = '&latlon=' + lat.toFixed(4) + ',' + lon.toFixed(4) +
    '&starttime=' + toIsoString(startTime) +
    '&endtime=' + toIsoString(endTime);

  var url = FMI_WFS_BASE + suffix;
  console.log('Fetching FMI Scandinavia: ' + url);

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (xhr.status === 200) {
      var temps = extractSeries(xhr.responseText, 'Temperature').filter(function(v) { return !isNaN(v); });
      if (!DEBUG_FORCE_OPENMETEO && temps.length > 0) {
        pendingFetch = false;
        parseAndSend(xhr.responseText, startTime, lat, lon, fallbackName, presetIndex);
        fetchAndSendUV(lat, lon, startTime);
      } else {
        console.log('Scandinavia endpoint returned no data, trying Open-Meteo');
        fetchOpenMeteo(lat, lon, fallbackName, presetIndex);
      }
    } else {
      console.log('HTTP error: ' + xhr.status + ', trying Open-Meteo');
      fetchOpenMeteo(lat, lon, fallbackName, presetIndex);
    }
  };
  xhr.onerror = function () {
    pendingFetch = false;
    console.log('XHR error');
    sendStatus(2);
  };
  xhr.open('GET', url);
  xhr.send();
}

function fetchOpenMeteo(lat, lon, fallbackName, presetIndex) {
  var now = new Date();
  var startTime = new Date(now);
  startTime.setHours(0, 0, 0, 0);
  startTime.setDate(startTime.getDate() - 1);  /* yesterday midnight local */

  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat.toFixed(4) +
    '&longitude=' + lon.toFixed(4) +
    '&hourly=temperature_2m,precipitation,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m,cloud_cover,uv_index,relative_humidity_2m' +
    '&wind_speed_unit=ms' +
    '&timeformat=unixtime' +
    '&forecast_days=9' +
    '&past_days=1';
  console.log('Fetching Open-Meteo: ' + url);

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    pendingFetch = false;
    if (xhr.status === 200) {
      parseAndSendOpenMeteo(xhr.responseText, startTime, fallbackName, presetIndex);
    } else {
      console.log('Open-Meteo HTTP error: ' + xhr.status);
      sendStatus(2);
    }
  };
  xhr.onerror = function () {
    pendingFetch = false;
    console.log('Open-Meteo XHR error');
    sendStatus(2);
  };
  xhr.open('GET', url);
  xhr.send();
}


function fetchAndSendUV(lat, lon, startTime) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat.toFixed(4) +
    '&longitude=' + lon.toFixed(4) +
    '&hourly=uv_index' +
    '&timeformat=unixtime' +
    '&forecast_days=9' +
    '&past_days=1';
  console.log('Fetching UV index: ' + url);
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (xhr.status !== 200) { console.log('UV fetch HTTP error: ' + xhr.status); return; }
    var data;
    try { data = JSON.parse(xhr.responseText); } catch(e) { console.log('UV JSON parse error: ' + e); return; }
    var times = data.hourly.time;
    var uvRaw = data.hourly.uv_index;
    var startSec = startTime.getTime() / 1000;
    var startIdx = 0;
    for (var i = 0; i < times.length; i++) { if (times[i] >= startSec) { startIdx = i; break; } }
    var uvByteArray = [];
    for (var i = 0; i < MAX_TEMPS && (startIdx + i) < uvRaw.length; i++) {
      var u = uvRaw[startIdx + i];
      uvByteArray.push((u === null || u === undefined || isNaN(u)) ? 255 : Math.min(16, Math.round(u)));
    }
    var nonNaN = uvByteArray.filter(function(v) { return v !== 255; }).length;
    if (nonNaN === 0) { console.log('UV: no valid values'); return; }
    console.log('Sending UV index: ' + uvByteArray.length + ' values, non-NaN=' + nonNaN);
    Pebble.sendAppMessage(
      { UV_INDEX: uvByteArray },
      function() { console.log('UV sent OK'); },
      function(err) { console.log('UV send error: ' + JSON.stringify(err)); }
    );
  };
  xhr.onerror = function() { console.log('UV fetch XHR error'); };
  xhr.open('GET', url);
  xhr.send();
}

function parseAndSendOpenMeteo(json, startTime, fallbackName, presetIndex) {
  var data;
  try { data = JSON.parse(json); } catch(e) {
    console.log('Open-Meteo JSON parse error: ' + e);
    sendStatus(2);
    return;
  }

  var h = data.hourly;
  var times    = h.time;
  var tempRaw  = h.temperature_2m;
  var precipRaw = h.precipitation;
  var wcodeRaw = h.weather_code;
  var wspdRaw  = h.wind_speed_10m;
  var wdirRaw  = h.wind_direction_10m;
  var wgustRaw = h.wind_gusts_10m;
  var cloudRaw = h.cloud_cover;
  var uvRaw    = h.uv_index;
  var humRawOM = h.relative_humidity_2m;

  /* Find index of startTime (yesterday midnight local = UTC midnight - offset) */
  var startSec = startTime.getTime() / 1000;
  var startIdx = 0;
  for (var i = 0; i < times.length; i++) {
    if (times[i] >= startSec) { startIdx = i; break; }
  }

  var count = Math.min(MAX_TEMPS, times.length - startIdx);
  if (count < 1) { console.log('Open-Meteo: no usable data'); sendStatus(2); return; }

  var temperatures = [];
  var precipByteArray = [], wcodeByteArray = [], wspdByteArray = [], wdirByteArray = [], wgustByteArray = [], cloudByteArray = [], uvByteArray = [], humByteArrayOM = [];

  for (var i = 0; i < count; i++) {
    var idx = startIdx + i;
    var t = tempRaw[idx];
    if (t === null || t === undefined || isNaN(t)) break;
    temperatures.push(Math.round(t));

    var p = (precipRaw[idx] !== null && !isNaN(precipRaw[idx])) ? precipRaw[idx] : 0;
    precipByteArray.push(Math.min(255, Math.ceil(Math.max(0, p) * 10)));

    wcodeByteArray.push(weatherCodeToIndicator(wcodeRaw ? wcodeRaw[idx] : null));

    var s = wspdRaw[idx];
    wspdByteArray.push((s === null || isNaN(s)) ? 255 : Math.min(254, Math.round(s)));

    var d = wdirRaw[idx];
    wdirByteArray.push((d === null || isNaN(d)) ? 255 : Math.min(254, Math.round(d / 360 * 254)));

    var g = wgustRaw[idx];
    wgustByteArray.push((g === null || isNaN(g)) ? 255 : Math.min(254, Math.round(g)));

    var c = cloudRaw[idx];
    cloudByteArray.push((c === null || isNaN(c)) ? 255 : Math.min(100, Math.round(c)));

    var u = uvRaw ? uvRaw[idx] : null;
    uvByteArray.push((u === null || u === undefined || isNaN(u)) ? 255 : Math.min(16, Math.round(u)));

    var hv = humRawOM ? humRawOM[idx] : null;
    humByteArrayOM.push((hv === null || hv === undefined || isNaN(hv)) ? 255 : Math.min(100, Math.max(0, Math.round(hv))));
  }

  if (temperatures.length === 0) { console.log('Open-Meteo: empty temperatures'); sendStatus(2); return; }

  var locationName = fallbackName || (data.latitude.toFixed(2) + ',' + data.longitude.toFixed(2));

  var byteArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    var tv = Math.max(-128, Math.min(127, temperatures[i]));
    byteArray.push(tv < 0 ? tv + 256 : tv);
  }

  var nowMs = Date.now();
  var currentIndex = Math.floor((nowMs - startTime.getTime()) / 3600000);
  currentIndex = Math.max(0, Math.min(currentIndex, temperatures.length - 1));
  var currentMinute = new Date(nowMs).getMinutes();

  var nonNaNWind  = wspdByteArray.filter(function(v) { return v !== 255; }).length;
  var nonNaNGust  = wgustByteArray.filter(function(v) { return v !== 255; }).length;
  var nonNaNCloud = cloudByteArray.filter(function(v) { return v !== 255; }).length;
  console.log('Open-Meteo: ' + temperatures.length + ' temps, wind=' + nonNaNWind + ', cloud=' + nonNaNCloud + ', loc=' + locationName);

  var sunByteArray = [];
  var sunBminArray = [];
  var oLat = data.latitude, oLon = data.longitude;
  var startMs = startTime.getTime();
  for (var i = 0; i < temperatures.length; i++) {
    sunByteArray.push(sunConditionWithTick(oLat, oLon, startMs, i));
    sunBminArray.push(sunBoundaryMin(oLat, oLon, startMs, i));
  }

  var msg = {
    STATUS: 1,
    TEMPERATURES: byteArray,
    PRECIPITATION: precipByteArray,
    LOCAL_START_HOUR: startTime.getHours(),
    LOCAL_START_WEEKDAY: startTime.getDay(),
    LOCAL_START_DAY: startTime.getDate(),
    LOCAL_START_MONTH: startTime.getMonth() + 1,
    CURRENT_INDEX: currentIndex,
    CURRENT_MINUTE: currentMinute,
    LOCATION_NAME: locationName.substring(0, 32)
  };
  if (nonNaNWind > 0)  { msg.WIND_SPEED     = wspdByteArray; }
  if (nonNaNWind > 0)  { msg.WIND_DIRECTION = wdirByteArray; }
  if (nonNaNGust > 0)  { msg.WIND_GUST      = wgustByteArray; }
  if (nonNaNCloud > 0) { msg.CLOUD_COVER    = cloudByteArray; }
  msg.SUN_CONDITION   = sunByteArray;
  msg.SUN_BOUNDARY_MIN = sunBminArray;
  var nonNaNUV = uvByteArray.filter(function(v) { return v !== 255; }).length;
  if (nonNaNUV > 0)    { msg.UV_INDEX       = uvByteArray; }
  var nonNaNHumOM = humByteArrayOM.filter(function(v) { return v !== 255; }).length;
  if (nonNaNHumOM > 0) { msg.RELATIVE_HUMIDITY = humByteArrayOM; }
  var nonZeroInd = wcodeByteArray.filter(function(v) { return v > 0; }).length;
  if (DEBUG_WEATHER_IND) debugInjectWeatherInd(precipByteArray, wcodeByteArray);
  if (nonZeroInd > 0 || DEBUG_WEATHER_IND)  { msg.WEATHER_INDICATOR = wcodeByteArray; }

  Pebble.sendAppMessage(
    msg,
    function () { console.log('Open-Meteo data sent OK'); },
    function (err) {
      console.log('Send error: ' + JSON.stringify(err));
      sendStatus(2);
    }
  );
}

function extractSeries(xml, paramName) {
  var blockRe = new RegExp(
    '<wml2:MeasurementTimeseries[^>]*' + paramName + '[\\s\\S]*?</wml2:MeasurementTimeseries>');
  var blockMatch = xml.match(blockRe);
  if (!blockMatch) return [];
  var values = [];
  var valueRe = /<wml2:value>([-\d.NaN]+)<\/wml2:value>/g;
  var match;
  while ((match = valueRe.exec(blockMatch[0])) !== null) {
    values.push(parseFloat(match[1]));
  }
  return values;
}

function parseAndSend(xml, startTime, lat, lon, fallbackName, presetIndex) {
  var tempRaw   = extractSeries(xml, 'Temperature');
  var precipRaw = extractSeries(xml, 'Precipitation1h');
  var wspdRaw   = extractSeries(xml, 'WindSpeedMS');
  var wdirRaw   = extractSeries(xml, 'WindDirection');
  var wgustRaw  = extractSeries(xml, 'HourlyMaximumGust');
  var cloudRaw  = extractSeries(xml, 'TotalCloudCover');
  var humRaw    = extractSeries(xml, 'Humidity');
  var wcodeRaw  = extractSeries(xml, 'WeatherSymbol3');

  // Truncate temperatures at first NaN
  var temperatures = [];
  for (var i = 0; i < tempRaw.length; i++) {
    if (isNaN(tempRaw[i])) break;
    temperatures.push(Math.round(tempRaw[i]));
  }

  // Limit to MAX_TEMPS
  if (temperatures.length > MAX_TEMPS) {
    temperatures = temperatures.slice(0, MAX_TEMPS);
  }

  if (temperatures.length === 0) {
    console.log('No temperature values parsed');
    sendStatus(2);
    return;
  }

  // Precipitation: 0 for NaN, same length as temperatures, tenths of mm as uint8
  var precipByteArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    var p = (i < precipRaw.length && !isNaN(precipRaw[i])) ? precipRaw[i] : 0;
    precipByteArray.push(Math.min(255, Math.ceil(Math.max(0, p) * 10)));
  }

  // Wind speed: whole m/s as uint8, 255=NaN
  var wspdByteArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    var s = (i < wspdRaw.length && !isNaN(wspdRaw[i])) ? wspdRaw[i] : NaN;
    wspdByteArray.push(isNaN(s) ? 255 : Math.min(254, Math.round(s)));
  }

  // Wind direction: 0-360 mapped to 0-254, 255=NaN
  var wdirByteArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    var d = (i < wdirRaw.length && !isNaN(wdirRaw[i])) ? wdirRaw[i] : NaN;
    wdirByteArray.push(isNaN(d) ? 255 : Math.min(254, Math.round(d / 360 * 254)));
  }

  // Wind gust: whole m/s as uint8, 255=NaN
  var wgustByteArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    var g = (i < wgustRaw.length && !isNaN(wgustRaw[i])) ? wgustRaw[i] : NaN;
    wgustByteArray.push(isNaN(g) ? 255 : Math.min(254, Math.round(g)));
  }

  // Cloud cover: 0-100% as uint8, 255=NaN
  var cloudByteArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    var c = (i < cloudRaw.length && !isNaN(cloudRaw[i])) ? cloudRaw[i] : NaN;
    cloudByteArray.push(isNaN(c) ? 255 : Math.min(100, Math.round(c)));
  }

  // Relative humidity: 0-100% as uint8, 255=NaN
  var humByteArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    var h = (i < humRaw.length && !isNaN(humRaw[i])) ? humRaw[i] : NaN;
    humByteArray.push(isNaN(h) ? 255 : Math.min(100, Math.max(0, Math.round(h))));
  }

  // Weather indicator: 0=none, 1=snow/sleet, 2=lightning
  var weatherIndArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    weatherIndArray.push(weatherCodeToIndicator(i < wcodeRaw.length ? wcodeRaw[i] : null));
  }

  // Sun condition: 0=normal, 1=golden, 2=dark; 100+min=sunrise golden, 160+min=sunset golden
  var sunByteArray = [];
  var sunBminArray = [];
  var startMs = startTime.getTime();
  for (var i = 0; i < temperatures.length; i++) {
    sunByteArray.push(sunConditionWithTick(lat, lon, startMs, i));
    sunBminArray.push(sunBoundaryMin(lat, lon, startMs, i));
  }

  // Extract location name
  var locMatch = xml.match(
    /<gml:name codeSpace="http:\/\/xml\.fmi\.fi\/namespace\/locationcode\/name">([^<]+)<\/gml:name>/
  );
  var locationName = (locMatch ? locMatch[1].trim() : null) || fallbackName || 'Unknown';

  // Encode temperatures as plain Array of unsigned bytes (int8 two's-complement)
  var byteArray = [];
  for (var i = 0; i < temperatures.length; i++) {
    var t = Math.max(-128, Math.min(127, temperatures[i]));
    byteArray.push(t < 0 ? t + 256 : t);
  }

  // Local hour of day for the first data point (phone local time)
  var localStartHour = startTime.getHours();

  // Which array index corresponds to "now"
  var nowMs = Date.now();
  var currentIndex = Math.floor((nowMs - startTime.getTime()) / 3600000);
  currentIndex = Math.max(0, Math.min(currentIndex, temperatures.length - 1));
  var currentMinute = new Date(nowMs).getMinutes();

  var nonZeroPrecip = precipByteArray.filter(function(v) { return v > 0; }).length;
  var nonNaNWind = wspdByteArray.filter(function(v) { return v !== 255; }).length;
  var nonNaNCloud = cloudByteArray.filter(function(v) { return v !== 255; }).length;
  var nonNaNGust = wgustByteArray.filter(function(v) { return v !== 255; }).length;
  console.log('Sending ' + temperatures.length + ' temps, ' + nonZeroPrecip + ' precip buckets, ' +
    nonNaNWind + ' wind values, ' + nonNaNGust + ' gust values, ' + nonNaNCloud + ' cloud values, current index=' + currentIndex + ', location=' + locationName);

  var msg = {
      STATUS: 1,
      TEMPERATURES: byteArray,
      PRECIPITATION: precipByteArray,
      LOCAL_START_HOUR: localStartHour,
      LOCAL_START_WEEKDAY: startTime.getDay(),
      LOCAL_START_DAY: startTime.getDate(),
      LOCAL_START_MONTH: startTime.getMonth() + 1,
      CURRENT_INDEX: currentIndex,
      CURRENT_MINUTE: currentMinute,
      LOCATION_NAME: locationName.substring(0, 32)
  };
  if (nonNaNWind > 0)  { msg.WIND_SPEED     = wspdByteArray; }
  if (nonNaNWind > 0)  { msg.WIND_DIRECTION = wdirByteArray; }
  if (nonNaNGust > 0)  { msg.WIND_GUST      = wgustByteArray; }
  if (nonNaNCloud > 0) { msg.CLOUD_COVER    = cloudByteArray; }
  msg.SUN_CONDITION    = sunByteArray;
  msg.SUN_BOUNDARY_MIN = sunBminArray;
  var nonNaNHum = humByteArray.filter(function(v) { return v !== 255; }).length;
  if (nonNaNHum > 0)   { msg.RELATIVE_HUMIDITY = humByteArray; }
  var nonZeroInd = weatherIndArray.filter(function(v) { return v > 0; }).length;
  if (DEBUG_WEATHER_IND) debugInjectWeatherInd(precipByteArray, weatherIndArray);
  if (nonZeroInd > 0 || DEBUG_WEATHER_IND) { msg.WEATHER_INDICATOR = weatherIndArray; }

  Pebble.sendAppMessage(
    msg,
    function () { console.log('Data sent OK'); },
    function (err) {
      console.log('Send error: ' + JSON.stringify(err));
      Pebble.sendAppMessage({
        STATUS: 1,
        TEMPERATURES: byteArray,
        PRECIPITATION: precipByteArray,
        LOCAL_START_HOUR: localStartHour,
        CURRENT_INDEX: currentIndex,
        LOCATION_NAME: 'FMI'
      });
    }
  );
}

function sendStatus(status) {
  Pebble.sendAppMessage({ STATUS: status });
}

/* Injects fake snow (ind=1) and lightning (ind=2) ranges for testing.
   Prefers hours with non-zero precipitation; falls back to fixed offsets. */
function debugInjectWeatherInd(precipArr, indArr) {
  /* Find up to 2 rain runs */
  var runs = [];
  var inRun = false;
  for (var i = 0; i < precipArr.length && runs.length < 2; i++) {
    if (precipArr[i] > 0 && !inRun) { runs.push({ start: i }); inRun = true; }
    else if (precipArr[i] === 0 && inRun) { runs[runs.length - 1].end = i; inRun = false; }
  }
  if (inRun) runs[runs.length - 1].end = precipArr.length;

  /* Fill any missing runs with fixed offsets */
  var base = Math.min(24, Math.floor(indArr.length / 3));
  if (!runs[0]) runs[0] = { start: base,     end: base + 4 };
  if (!runs[1]) runs[1] = { start: base + 6, end: base + 9 };

  /* Clamp to array length */
  for (var j = 0; j < runs.length; j++) {
    runs[j].start = Math.min(runs[j].start, indArr.length - 1);
    runs[j].end   = Math.min(runs[j].end,   indArr.length);
  }

  /* Inject: first run → snow (1), second run → lightning (2) */
  for (var k = runs[0].start; k < runs[0].end; k++) indArr[k] = 1;
  for (var k = runs[1].start; k < runs[1].end; k++) indArr[k] = 2;

  console.log('DEBUG_WEATHER_IND: snow=' + runs[0].start + '-' + runs[0].end +
              ', lightning=' + runs[1].start + '-' + runs[1].end);
  return indArr;
}

function toIsoString(date) {
  return date.toISOString().replace(/\.\d{3}Z$/, 'Z');
}
