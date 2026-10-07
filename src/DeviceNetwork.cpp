/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

#include "DeviceNetwork.h"
#include <ESP8266WiFi.h>
#include "settings.h"
#include "device_security.h"
#include "TelemetryState.h"
#include "StationDisplay.h"
#include "HttpDiagnostics.h"
#include "ArduinoOTA.h"
#include "WiFiManager.h"

String hostname(HOSTNAME);
static uint32_t lastWifiRetryAt = 0;

void beginWiFi() {
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  // Uncomment for testing wifi manager
  // wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setDebugOutput(false);
  wifiManager.setShowPassword(false);
  wifiManager.setConnectTimeout(20);
  WiFi.hostname(hostname);
  WiFi.setAutoReconnect(true);

  // Timeout after 5 minutes: in case of power failure, this prevents the device from being stuck on waiting for AP information
  wifiManager.setConfigPortalTimeout(300);

  //or use this for auto generated name ESP + ChipID
  if (!wifiManager.autoConnect(hostname.c_str(), PROVISIONING_PASSWORD)) {
    Serial.println("Provisioning window closed; retrying saved WiFi");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin();
  }

  // Manual Wifi for debugging
  // WiFi.begin(SSID, PASSWORD);

  // hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);

  Serial.print("My hostname is: "+ hostname);

  lastWifiRetryAt = millis();
}

void beginOTA() {
// Setup OTA
#ifdef DEBUG
  Serial.println("Hostname: " + hostname);
#endif
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.setPasswordHash(OTA_PASSWORD_HASH);
  ArduinoOTA.onProgress(drawOtaProgress);
  ArduinoOTA.begin();
}

void serviceWiFi() {
  if (WiFi.status() != WL_CONNECTED && Telemetry::elapsed(millis(), lastWifiRetryAt, 30000)) {
    lastWifiRetryAt = millis();
    WiFi.begin();
  }
}

void serviceLocalNetwork() {
  ArduinoOTA.handle();
  serviceHTTP();
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality()
{
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100)
  {
    return 0;
  }
  else if (dbm >= -50)
  {
    return 100;
  }
  else
  {
    return 2 * (dbm + 100);
  }
}
