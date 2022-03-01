# GreenhouseTelegramBot

Telegram bot to monitor a greenhouse with an ESP8266. It lets you request temperature and humidity and sends you a message when the temperature crosses a certain boundary, so you can (get there to) take actions.
It also tracks the min/max temperatures of the past 24 hours (in half an hour blocks).

Available commands:
- Get : Gets the latest temperatur read.
- Read : Forces a new read from the sensor
- Logs : Gives an overview of the last 20 warning / info messages send to Telegram
- Trend : Gives an overview of the min / max temperatures of the last 24 hours.
