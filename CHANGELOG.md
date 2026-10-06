# Changelog

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
