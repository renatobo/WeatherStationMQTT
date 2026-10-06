# Build recovery and workshop OTA

Follow-up to the assessment, 2026-10-06. The earlier failed-build evidence remains historical.

- Installed Apple Rosetta 2 successfully. The existing x86_64 Xtensa GCC 10.3.0 now executes on this arm64 Mac.
- `pio run` under Core 6.1.19 compiled and linked all six profiles successfully: office, office_ota, workshop, workshop_ota, Printer3d, Printer3d_ota. RAM approximately 42.7%; flash approximately 43.6%. Full local log: `/private/tmp/weather-build-rosetta.log`.
- Compiler warnings remain: timestamp/time formatting in WeatherStation.cpp and operator precedence in simpleDSTadjust. A passing build does not resolve the assessment's runtime defects.
- Removed obsolete pipx PlatformIO Core 6.1.19; linked shell pio/platformio commands to the retained IDE installation at `~/.platformio/penv/bin`. `pio --version` confirms 6.2.0.
- User explicitly authorized workshop OTA after the original assessment. `pio run -e workshop_ota -t upload` under Core 6.2.0 rebuilt and uploaded successfully to `druino-workshop.local`; OTA returned `Result: OK`. Full local log: `/private/tmp/weather-workshop-ota.log`.
- Post-upload `/info` and `/temp` returned HTTP 200. Device uptime was 16 seconds, consistent with reboot. The displayed SW build date remains March 3, 2024 because firmware uses `__TIMESTAMP__` (source-file modification time), not actual compilation time. It is not a reliable firmware identity check.

Firmware source and dependency declarations were unchanged. No proposed modernization fixes were included. Other devices were not flashed. MQTT delivery after reboot was not verified.
