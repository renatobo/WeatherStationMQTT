# Changelog

## [0.3.0](https://github.com/renatobo/WeatherStationMQTT/tree/v0.3.0) - 2026-10-06

- Replace the active OpenWeatherMap path with key-free Open-Meteo conditions and
  daily forecasts for the same configured city. Retire the weather credential.
- Verify HTTPS server identity and certificate dates with ISRG Root X1; reject
  redirects and unsupported data instead of falling back to insecure transport.
- Bound DNS/TCP setup, TLS handshake, idle reads, total request time and response
  storage. Generate a hash-checked timeout overlay in each build directory;
  leave the pinned shared SDK unchanged.
- Service MQTT, sensor reads, HTTP, OTA and display while reading response data.
  Preserve the last good weather snapshot on failures and mark it stale.
- Validate JSON, unit labels, dates, WMO codes and forecast arrays before updating
  the complete cache. Show daily high/low values in enabled forecast panels.
- Add weather-stage, TLS, validation, request-duration, heap/fragmentation and
  reset diagnostics to `/info`; reserve its output buffer to reduce allocation churn.
- Remove the unused Weather Station and JsonStreamingParser dependencies; pin
  ArduinoJson 6.21.6 for fixed-capacity parsing and its maintenance security fix.
- Add sanitizer checks for decoder failures, cache preservation and request limits.

DNS/TCP/TLS setup remains synchronous and bounded; body/header reads are serviced
cooperatively. Long soak, physical outage tests, management authentication,
MQTT TLS and signed updates remain later-phase work.

## [0.2.0](https://github.com/renatobo/WeatherStationMQTT/tree/v0.2.0) - 2026-10-06

- Service MQTT every main-loop iteration; use one Wi-Fi-gated reconnect attempt
  with capped backoff/jitter and bounded DNS/TCP/MQTT socket waits.
- Schedule one DHT cycle per minute with at most three attempts spaced 2.5 seconds
  apart. Use the declared sensor type, nonblocking library reads and finite/range
  validation of coherent temperature/humidity pairs.
- Preserve the MQTT topics and semicolon schema while using acquisition timestamps
  and one unit policy for the sensor, weather, display and MQTT labels.
- Keep one bounded latest pending pair, retry only failed halves, reject oversized
  packets and expire pending data after five minutes. QoS0 write success remains
  distinct from database ingestion.
- Correct timestamp/clock buffer bounds; mark invalid/stale readings unavailable.
- Service HTTP/OTA independently of UI budget and queue presence callbacks instead
  of reconnecting or publishing from them.
- Expose sample freshness, sensor errors, reconnect/publish counts, pending state
  and maximum MQTT service gap through `/info`.
- Add sanitizer-backed host tests for formatting, units, retry policies, rollover,
  partial publication, expiration and simulated broker/Wi-Fi outages.

Still pending: total HTTP deadlines and weather isolation, management authentication,
encrypted transports, durable delivery, long soak and physical outage/recovery tests.

## [0.1.0](https://github.com/renatobo/WeatherStationMQTT/tree/v0.1.0) - 2026-10-06

- Establish the first tagged build baseline with exact platform, tool and library pins.
- Check in six nonsecret build profiles and private configuration examples.
- Add a private recovery archive command with SHA256 read-back verification.
- Show firmware version and its GitHub tag link, compilation date/time, device
  profile, sketch MD5, reset reason and Wi-Fi RSSI on `/info`.
- Save the modernization assessment, independent-cache build evidence and
  broker-to-database telemetry baseline.

Known issues: MQTT servicing, blocking reconnects, unchecked publishes, DHT
scheduling, formatting/buffer safety and management security remain as described
in the assessment. This version establishes the baseline; those repairs belong
to subsequent versions. Workshop is the canary; office remains on legacy firmware.
