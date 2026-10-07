// SPDX-License-Identifier: MIT
// Derived from the ThingPulse weather station; see LICENSE for copyright and attribution.
// Project modifications and modernization by Renato Bonomini (renatobo).
//
// Wi-Fi provisioning, saved-network recovery and authenticated OTA.
// Owns the Wi-Fi retry clock. Provisioning/OTA screens are delegated to
// StationDisplay; ordinary HTTP requests are serviced by HttpDiagnostics.

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
  // Saved Wi-Fi is tried first. If setup times out, close the protected AP and
  // continue STA retries; reboot loops would repeatedly reopen provisioning.
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
  // The build hook generates this digest from the ignored private INI. The
  // SDK checks OTA authentication; this does not encrypt or sign the firmware.
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

// Called by both the normal loop and the cooperative weather reader.
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
