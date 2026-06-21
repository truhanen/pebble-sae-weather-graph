/* global Pebble, XMLHttpRequest */

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var MAX_TEMPS = 240;
var DEBUG_FORCE_OPENMETEO = false;  /* set true to always use Open-Meteo endpoint */
var DEBUG_COORDS = null;  /* set to an object to use test coordinates */
// var DEBUG_COORDS = { lat: 48.0941, lon: 7.9604, name: 'Waldkirch' };  /* Germany */
// var DEBUG_COORDS = { lat: 62.2426, lon: 25.7473, name: 'Jyvaskyla' };  /* Jyväskylä center */
var FMI_WFS_BASE = 'https://opendata.fmi.fi/wfs?service=WFS&version=2.0.0' +
  '&request=getFeature' +
  '&storedquery_id=fmi::forecast::edited::weather::scandinavia::point::timevaluepair' +
  '&parameters=Temperature,Precipitation1h,WindSpeedMS,WindDirection,HourlyMaximumGust,TotalCloudCover&timestep=60';

var pendingFetch = false;

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
  fetchForecast();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload.REQUEST_DATA !== undefined) {
    fetchForecast();
  }
});

function fetchForecast() {
  if (pendingFetch) return;
  pendingFetch = true;

  if (DEBUG_COORDS) {
    if (DEBUG_FORCE_OPENMETEO) {
      fetchOpenMeteo(DEBUG_COORDS.lat, DEBUG_COORDS.lon, DEBUG_COORDS.name);
    } else {
      fetchForLatLon(DEBUG_COORDS.lat, DEBUG_COORDS.lon, DEBUG_COORDS.name);
    }
    return;
  }

  if (navigator.geolocation) {
    navigator.geolocation.getCurrentPosition(
      function (pos) {
        fetchForLatLon(pos.coords.latitude, pos.coords.longitude, null);
      },
      function () {
        fetchForLatLon(60.1699, 24.9384, 'Helsinki');
      },
      { timeout: 10000, maximumAge: 300000 }
    );
  } else {
    fetchForLatLon(60.1699, 24.9384, 'Helsinki');
  }
}

function fetchForLatLon(lat, lon, fallbackName) {
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
        parseAndSend(xhr.responseText, startTime, fallbackName);
      } else {
        console.log('Scandinavia endpoint returned no data, trying Open-Meteo');
        fetchOpenMeteo(lat, lon, fallbackName);
      }
    } else {
      console.log('HTTP error: ' + xhr.status + ', trying Open-Meteo');
      fetchOpenMeteo(lat, lon, fallbackName);
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

function fetchOpenMeteo(lat, lon, fallbackName) {
  var now = new Date();
  var startTime = new Date(now);
  startTime.setHours(0, 0, 0, 0);
  startTime.setDate(startTime.getDate() - 1);  /* yesterday midnight local */

  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat.toFixed(4) +
    '&longitude=' + lon.toFixed(4) +
    '&hourly=temperature_2m,precipitation,wind_speed_10m,wind_direction_10m,wind_gusts_10m,cloud_cover' +
    '&wind_speed_unit=ms' +
    '&timeformat=unixtime' +
    '&forecast_days=9' +
    '&past_days=1';
  console.log('Fetching Open-Meteo: ' + url);

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    pendingFetch = false;
    if (xhr.status === 200) {
      parseAndSendOpenMeteo(xhr.responseText, startTime, fallbackName);
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

function parseAndSendOpenMeteo(json, startTime, fallbackName) {
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
  var wspdRaw  = h.wind_speed_10m;
  var wdirRaw  = h.wind_direction_10m;
  var wgustRaw = h.wind_gusts_10m;
  var cloudRaw = h.cloud_cover;

  /* Find index of startTime (yesterday midnight local = UTC midnight - offset) */
  var startSec = startTime.getTime() / 1000;
  var startIdx = 0;
  for (var i = 0; i < times.length; i++) {
    if (times[i] >= startSec) { startIdx = i; break; }
  }

  var count = Math.min(MAX_TEMPS, times.length - startIdx);
  if (count < 1) { console.log('Open-Meteo: no usable data'); sendStatus(2); return; }

  var temperatures = [];
  var precipByteArray = [], wspdByteArray = [], wdirByteArray = [], wgustByteArray = [], cloudByteArray = [];

  for (var i = 0; i < count; i++) {
    var idx = startIdx + i;
    var t = tempRaw[idx];
    if (t === null || t === undefined || isNaN(t)) break;
    temperatures.push(Math.round(t));

    var p = (precipRaw[idx] !== null && !isNaN(precipRaw[idx])) ? precipRaw[idx] : 0;
    precipByteArray.push(Math.min(255, Math.ceil(Math.max(0, p) * 10)));

    var s = wspdRaw[idx];
    wspdByteArray.push((s === null || isNaN(s)) ? 255 : Math.min(254, Math.round(s)));

    var d = wdirRaw[idx];
    wdirByteArray.push((d === null || isNaN(d)) ? 255 : Math.min(254, Math.round(d / 360 * 254)));

    var g = wgustRaw[idx];
    wgustByteArray.push((g === null || isNaN(g)) ? 255 : Math.min(254, Math.round(g)));

    var c = cloudRaw[idx];
    cloudByteArray.push((c === null || isNaN(c)) ? 255 : Math.min(100, Math.round(c)));
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

function parseAndSend(xml, startTime, fallbackName) {
  var tempRaw   = extractSeries(xml, 'Temperature');
  var precipRaw = extractSeries(xml, 'Precipitation1h');
  var wspdRaw   = extractSeries(xml, 'WindSpeedMS');
  var wdirRaw   = extractSeries(xml, 'WindDirection');
  var wgustRaw  = extractSeries(xml, 'HourlyMaximumGust');
  var cloudRaw  = extractSeries(xml, 'TotalCloudCover');

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

function toIsoString(date) {
  return date.toISOString().replace(/\.\d{3}Z$/, 'Z');
}
