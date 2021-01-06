# WeatherStationMQTT

Additions to the already good [Weather Station](https://github.com/ThingPulse/esp8266-weather-station-color):

As provided by [neptune2](https://github.com/neptune2/esp8266-weather-station-oled-DST)

* DayLightSavings time
* WiFiManager
* OTA

Then I added

* **MQTT client** to log temperature and humidity
* **a PIR sensor** integration to turn on the screen only when someone is in front of it:  to avoid burning the display (after 2 years, the OLED on my first device is almost burnt out) but also to eliminate the bright light at night time
* a minimal **web server** page 

and improved

* changed to [DHTNEW](https://github.com/RobTillaart/DHTNEW) for DHT22 readings

## Hardware

Components from the original [ThingPulse](https://www.amazon.com/gp/product/B01KE7BA3O)

* ESP8266
* DHT22
* SSD1603 128x64 OLED

Additional component

* PIR, for example [AM312](https://www.amazon.com/HiLetgo-Pyroelectric-Sensor-Infrared-Detector/dp/B07RT7MK7C)

Wiring diagram to come

## Software

First Follow instructions at [Weather Station](https://github.com/ThingPulse/esp8266-weather-station-color), then set up a kafka broker on another server and store its information in `settings.h`
