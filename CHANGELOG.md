# Changelog

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
