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
    "type": "submit",
    "defaultValue": "Save"
  }
];
