module.exports = [
  {
    "type": "heading",
    "defaultValue": "Weather Graph"
  },
  {
    "type": "select",
    "messageKey": "TEMP_UNIT",
    "defaultValue": 0,
    "label": "Temperature Unit",
    "options": [
      { "label": "Celsius (°C)", "value": 0 },
      { "label": "Fahrenheit (°F)", "value": 1 }
    ]
  },
  {
    "type": "select",
    "messageKey": "WIND_UNIT",
    "defaultValue": 0,
    "label": "Wind Speed Unit",
    "options": [
      { "label": "m/s", "value": 0 },
      { "label": "km/h", "value": 1 },
      { "label": "mph", "value": 2 }
    ]
  },
  {
    "type": "select",
    "messageKey": "PRECIP_UNIT",
    "defaultValue": 0,
    "label": "Precipitation Unit",
    "options": [
      { "label": "mm", "value": 0 },
      { "label": "inch", "value": 1 }
    ]
  },
  {
    "type": "heading",
    "defaultValue": "Layers"
  },
  {
    "type": "toggle",
    "messageKey": "SHOW_CLOUD",
    "label": "Cloud cover",
    "defaultValue": true
  },
  {
    "type": "toggle",
    "messageKey": "SHOW_PRECIP",
    "label": "Precipitation",
    "defaultValue": true
  },
  {
    "type": "toggle",
    "messageKey": "SHOW_HUMIDITY",
    "label": "Relative humidity",
    "defaultValue": true
  },
  {
    "type": "toggle",
    "messageKey": "SHOW_WIND",
    "label": "Wind speed & direction",
    "defaultValue": true
  },
  {
    "type": "toggle",
    "messageKey": "SHOW_UV",
    "label": "UV index",
    "defaultValue": true
  },
  {
    "type": "toggle",
    "messageKey": "SHOW_GOLDEN_HOUR",
    "label": "Golden hour",
    "defaultValue": true
  },
  {
    "type": "toggle",
    "messageKey": "SHOW_DARKNESS",
    "label": "Darkness",
    "defaultValue": true
  },
  {
    "type": "toggle",
    "messageKey": "SHOW_DAWN_DUSK",
    "label": "Dawn & dusk ticks",
    "defaultValue": true
  },
  {
    "type": "heading",
    "defaultValue": "Preset Locations"
  },
  {
    "type": "input",
    "messageKey": "PRESET_NAME_1",
    "defaultValue": "",
    "label": "Location 1",
    "attributes": { "placeholder": "e.g. Paris", "maxlength": 32 }
  },
  {
    "type": "input",
    "messageKey": "PRESET_NAME_2",
    "defaultValue": "",
    "label": "Location 2",
    "attributes": { "placeholder": "e.g. Tokyo", "maxlength": 32 }
  },
  {
    "type": "input",
    "messageKey": "PRESET_NAME_3",
    "defaultValue": "",
    "label": "Location 3",
    "attributes": { "placeholder": "", "maxlength": 32 }
  },
  {
    "type": "input",
    "messageKey": "PRESET_NAME_4",
    "defaultValue": "",
    "label": "Location 4",
    "attributes": { "placeholder": "", "maxlength": 32 }
  },
  {
    "type": "input",
    "messageKey": "PRESET_NAME_5",
    "defaultValue": "",
    "label": "Location 5",
    "attributes": { "placeholder": "", "maxlength": 32 }
  },
  {
    "type": "heading",
    "defaultValue": "Formats"
  },  {
    "type": "select",
    "messageKey": "TIME_FORMAT",
    "defaultValue": 0,
    "label": "Time format",
    "options": [
      { "label": "24h", "value": 0 },
      { "label": "12h (AM/PM)", "value": 1 }
    ]
  },
  {
    "type": "select",
    "messageKey": "DATE_FORMAT",
    "defaultValue": 0,
    "label": "Date format",
    "options": [
      { "label": "DD.MM.", "value": 0 },
      { "label": "MM/DD", "value": 1 }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
