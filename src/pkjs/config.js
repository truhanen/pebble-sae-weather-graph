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
    "type": "submit",
    "defaultValue": "Save"
  }
];
