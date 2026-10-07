# WeatherStationMQTT

Current tagged firmware: [v0.5.0](https://github.com/renatobo/WeatherStationMQTT/tree/v0.5.0).
See [CHANGELOG.md](CHANGELOG.md).

ESP8266 weather and indoor-climate logger maintained by **Renato Bonomini**.
It reads a DHT sensor, publishes temperature/humidity and PIR presence over MQTT,
and displays a clock, indoor readings and Open-Meteo weather on an OLED screen.
PIR activity controls screen brightness and sleep; HTTP endpoints expose readings
and diagnostics. Provisioning and OTA updates use separate per-device passwords.

## Validation

The checked-in profiles build with the pinned PlatformIO toolchain. Host tests
cover telemetry scheduling, formatting, weather validation and credential handling.
A successful build does not prove hardware operation or downstream database
persistence; device checks, outage/recovery tests and longer soaks are separate.

## Hardware

- ESP8266 / Wemos D1 mini.
- DHT22 temperature/humidity sensor.
- SSD1306-compatible 128×64 I²C OLED.
- PIR motion sensor, such as the AM312.

Pin assignments vary by device. Check `include/mysecrets.h.example` and the
actual wiring before setting private values. The acquisition module currently
supports DHT sensors. AHT10 support is not implemented; declaring `AHT10` in a
configuration does not enable it, and compilation does not validate sensor hardware.

## License and attribution

The project derives from Daniel Eichhorn / ThingPulse's weather station and
Neptune's customizations. Renato Bonomini added the MQTT, PIR and HTTP integration
and subsequent project modernization. See [LICENSE](LICENSE) for the full MIT
notice and attribution; source files carry short attribution headers.

## Software

The device publishes to an MQTT broker. Store its address and the weather API
configuration in the ignored `include/mysecrets.h`, not in `settings.h`. Store
OTA/provisioning passwords and private upload destinations in `mysecret_envs.ini`
as described below. Both private files are excluded from Git.

### Firmware modules

`src/WeatherStation.cpp` owns startup and the cooperative service order. Device
profiles, topics, endpoints, sample timing and OTA credentials keep their existing
configuration sources. Runtime configuration storage and the DST clock are defined
once in `DeviceConfiguration.cpp`; `settings.h` contains guarded declarations and
compile-time settings.

| Module | Responsibility |
| --- | --- |
| `DeviceConfiguration.cpp` | Device configuration storage and DST clock |
| `IndoorSensor.cpp` | DHT hardware, bounded reads, sample validity and formatting |
| `MqttTelemetry.cpp` | Broker connection, retries, queued sample pairs and presence publishing |
| `DeviceNetwork.cpp` | Saved Wi-Fi, protected provisioning and authenticated OTA |
| `HttpDiagnostics.cpp` | HTTP routes and device diagnostics |
| `WeatherService.cpp` | Weather scheduling, verified HTTPS requests and cache updates |
| `StationDisplay.cpp` | OLED frames, provisioning/OTA screens and PIR display timers |
| `SystemHealth.cpp` | Heap measurements used by diagnostics and weather guards |

Each module owns its hardware clients, timers and internal retry state. The small
headers expose service entry points and diagnostic/sample data for consumers.
Weather reads call back into the application to service MQTT, sensors, HTTP, OTA
and frames in the same order as the main loop. Timer callbacks only signal or
schedule work.
The existing telemetry and weather policy tests remain the regression checks.

### Build baseline

Use PlatformIO Core **6.2.0** for the validated baseline. Keep one Core installation;
on this Mac the CLI and IDE both use `~/.platformio/penv/bin/pio`. On Apple Silicon,
the pinned Xtensa toolchain is Intel-only and requires Rosetta 2. No upload is
performed by `pio run`.

For a new checkout:

```sh
cp include/mysecrets.h.example include/mysecrets.h
cp mysecret_envs.ini.example mysecret_envs.ini
chmod 600 mysecret_envs.ini
pio run
```

The examples deliberately use dummy identities, Berlin weather coordinates and a
nonresolving MQTT address. Replace them with private deployment values before
any upload: configure device identity, wiring, broker/location values, OTA targets
and unique OTA/provisioning passwords. `build-profiles.ini` defines all six profiles;
the ignored INI can override their private OTA destinations. Existing private
files should be retained rather than overwritten with examples.

Platform, tool versions, registry libraries and Git library commits are pinned
in `platformio.ini`. [PlatformIO dependency documentation](https://docs.platformio.org/en/latest/librarymanager/dependencies.html)
describes exact-version and Git-commit specifications.

To validate without reusing cached dependencies, copy the project into a new
temporary directory and use a new Core directory:

```sh
baseline_dir=$(mktemp -d)
cp -R src include scripts "$baseline_dir/"
cp platformio.ini build-profiles.ini "$baseline_dir/"
cp include/mysecrets.h.example "$baseline_dir/include/mysecrets.h"
cp mysecret_envs.ini.example "$baseline_dir/mysecret_envs.ini"
PLATFORMIO_CORE_DIR="$baseline_dir/core" pio run -d "$baseline_dir"
```

This downloads the pinned packages again and compiles all six profiles with
placeholder configuration. It verifies repeatable dependency resolution and
compilation, not byte-identical binaries or live hardware behavior.

After building all six profiles with the real private configuration, archive them:

```sh
python3 scripts/archive_firmware.py YYYY-MM-DD-baseline
```

Archives include firmware BIN/ELF files, source, private configuration, dependency
identifiers and a read-back-verified SHA256 manifest. `.recovery/` is Git-ignored
and private; never publish these archives because firmware embeds configuration.
Keep an encrypted copy outside this computer for recovery. A successful build
alone does not prove a binary has booted or delivered readings.

### Firmware identity and versions

`/info` reports the version with a link to its immutable GitHub tag, compilation
date/time, device profile, running sketch MD5, last reset reason and current
Wi-Fi RSSI in dBm. Compilation time uses the build host's time zone. MD5 identifies
the running image; the private recovery manifest uses SHA256 for artifact checks.
Reset reason describes the last boot, and RSSI is a point-in-time sample.

Since v0.2.0, MQTT and HTTP sample timestamps represent sensor acquisition time.
Samples are coherent temperature/humidity pairs; the display and HTTP endpoint
mark them unavailable after a failed sensor read or two minutes without a valid
sample. One DHT cycle runs per minute with up to three attempts spaced 2.5 seconds
apart. `METRIC` controls both values and unit labels throughout the application.

MQTT retains only the latest scheduled pair. Successful halves are not repeated
when the other half fails; pending data expires five minutes after acquisition.
Reconnect attempts use Wi-Fi gating, backoff and jitter. Publish success means
the client accepted the write, not that a broker or database acknowledged it.
The existing semicolon payload and topics are preserved.

`/info` adds sample age/validity, sensor errors, connection and publication counts,
pending replacements/expiration, skipped scheduled samples and maximum MQTT
service gap. Weather behavior and its limits are described below.

Run the host policy tests with sanitizers:

```sh
clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Iinclude test/telemetry_state_test.cpp \
  -o /tmp/weather-telemetry-tests
/tmp/weather-telemetry-tests
```

These test the shared scheduling, formatting and pending-sample policies using
fake clocks and clients. They do not replace hardware outage or long-soak tests.

### Weather and diagnostics

Weather data comes from [Open-Meteo](https://open-meteo.com/), under
[CC BY 4.0 and its free personal-use terms](https://open-meteo.com/en/terms).
No API key is required. Configure `WEATHER_LATITUDE`, `WEATHER_LONGITUDE` and
`WEATHER_TIMEZONE` in the ignored private header; the previous OpenWeatherMap
credential is no longer used. Coordinates in the configuration example are
public sample data, not deployment settings.

The current display uses model-derived temperature and WMO condition codes;
enabled forecast panels show daily high/low temperatures for the location's local
calendar days. These are not identical to OpenWeatherMap's station conditions
and three-hour/noon forecast values. The indoor sensor MQTT schema is unchanged.

HTTPS verifies the hostname and certificate dates using the public ISRG Root X1
anchor. An unsynchronized clock defers requests. There is no insecure fallback.
Refresh is every ten minutes, with failed attempts retried at most once per minute;
weather is deferred while MQTT is disconnected or heap headroom is insufficient.

The request budget is eight seconds, the idle budget 1.5 seconds, and storage is
limited to 2048 response bytes plus its terminator. DNS/TCP setup allow one second
each; the TLS handshake is capped at four seconds. Individual SDK calls can finish
after a budget check, so the budget is checked again before cache commit. Initial
DNS/TCP/TLS setup is synchronous; subsequent reads pump the application services.
Errors preserve the last good complete snapshot and mark it stale. Weather never
requests a firmware restart.

`scripts/bounded_tls.py` verifies the SHA256 of the pinned SDK source and generates
the bounded copy under `.pio/build/<profile>/bounded-sdk/`. It does not change the
shared framework cache. Review this overlay when changing framework versions.
The CA anchor in `include/WeatherTrust.h` must also be reviewed if the provider
changes its certificate chain. Oversized TLS records fail closed.

`/info` reports weather state, attempt/success/failure counts, TLS/HTTP status,
validation stage, durations and cache age, plus current/minimum free heap,
largest free blocks, fragmentation, continuation-stack margin and reset details.
Heap minima are sampled, not continuous allocator instrumentation.

Run the weather checks after installing the pinned dependencies. Set
`build_profile` to the environment used to install those dependencies:

```sh
build_profile="YOUR_BUILD_PROFILE"
clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Iinclude -I".pio/libdeps/$build_profile/ArduinoJson/src" \
  test/weather_test.cpp -o /tmp/weather-weather-tests
/tmp/weather-weather-tests test/fixtures/weather.json
```

Use `scripts/verify_device.py` with an archived device image for a read-only live
check. Add `--weather` to require fresh validated weather, TLS status and heap
diagnostics. Keep the checkout version aligned with the release being verified:

```sh
device_host="YOUR_DEVICE_HOSTNAME"
archived_binary=".recovery/RELEASE/.pio/build/OTA_PROFILE/firmware.bin"
python3 scripts/verify_device.py "$device_host" \
  --binary "$archived_binary" --weather
```

Set `device_host` to the private target and `archived_binary` to its matching
archived image. A successful identity/weather check is separate from MQTT
ingestion or physical display tests.

For each subsequent release, bump `include/version.h`, update `CHANGELOG.md`,
build and verify all profiles, commit the intended changes, and create/publish
the matching annotated `vX.Y.Z` tag. Never move an existing tag. Build deployment
images from that exact tagged source and archive them before uploading. Record
the live sketch MD5 against the archived binary after the device reboots.

### OTA authentication and provisioning

Each device has separate `ota_password` and `provisioning_password` entries under
`[security:<device>]` sections matching the configured profiles in the ignored
`mysecret_envs.ini`. Keep this file owner-readable/writable only (0600). Use unique,
strong random passwords of 20-63 non-whitespace ASCII characters. The example file
contains synthetic placeholders for compile checks and must not be deployed.

The build generates `.pio/build/<profile>/private/device_security.h`, containing
the OTA password digest and the setup-hotspot password. Passwords are not compiler
flags. Firmware necessarily contains the hotspot password; treat images and private
recovery archives as sensitive. Do not print full PlatformIO configuration or enable
credential-bearing uploader debug logs.

Ordinary `pio run -e "$ota_profile" -t upload` commands use
`scripts/ota_upload.py`. It reads the INI and invokes the pinned SDK uploader
in-process, so credentials are not command arguments. The one-time `--bootstrap`
option is only for installing protected firmware onto an existing unprotected device;
subsequent uploads authenticate normally. Wrong/empty probes send only an OTA
authentication exchange, not firmware.

```sh
ota_profile="YOUR_OTA_PROFILE"
~/.platformio/penv/bin/python scripts/ota_upload.py --env "$ota_profile" --probe wrong
~/.platformio/penv/bin/python scripts/ota_upload.py --env "$ota_profile" --probe empty
```

The setup hotspot uses the device hostname and its separate provisioning password.
Normal startup uses saved Wi-Fi settings. If connection fails, the protected portal
opens for five minutes. After timeout the device closes the hotspot and retries saved
Wi-Fi every 30 seconds; it does not reset or reopen the portal continuously. Power-cycle
the device to reopen a setup window when needed. Existing home Wi-Fi credentials are
preserved. Portal behavior must be physically tested on a spare device before relying
on it for remote recovery; live credentials are not cleared as an automatic test.

Store the INI and protected recovery images in a private backup. Lost OTA credentials
require physical USB/serial recovery. Restoring a legacy v0.3.0 image removes these
protections, so use a protected rollback image when available. SDK Digest-MD5 is basic
authentication; it does not encrypt the image transfer or replace signed firmware.

Run the synthetic credential checks with `python3 test/security_config_test.py`.

### Deploy an archived release

Flash one device first and verify it after reboot before updating other targets. Use the
PlatformIO Python environment for the direct uploader; it is the environment used
for the verified Mac uploads. Adjust its path if Core is installed elsewhere.
Normal `pio run -e "$ota_profile" -t upload` builds and uploads the current checkout;
the command below uploads the exact archived image instead. Set `ota_profile`,
`device_host` and `archived_binary` to the private environment, target and image
for the release being deployed:

```sh
ota_profile="YOUR_OTA_PROFILE"
device_host="YOUR_DEVICE_HOSTNAME"
archived_binary=".recovery/RELEASE/.pio/build/OTA_PROFILE/firmware.bin"
~/.platformio/penv/bin/python scripts/ota_upload.py --env "$ota_profile" \
  --binary "$archived_binary"
python3 scripts/verify_device.py "$device_host" \
  --binary "$archived_binary" --weather
```

Weather may take a minute to become available while the clock synchronizes. Check
`/info` for valid indoor readings and MQTT connection state as well. Keep the prior
protected image and its matching private configuration for rollback. Recovery
archives, credentials and local assessment evidence must not be pushed.

### Remaining work

- Physical provisioning-password, timeout, Wi-Fi outage and serial-recovery tests.
- HTTP management protection and per-device MQTT credentials/topic permissions.
- MQTT TLS, signed firmware and encrypted OTA transport.
- Longer hardware soak and broker-to-database delivery verification.

Module separation is complete for v0.5.0; these validation and security tasks remain
open. Source comments and attribution improvements after the tag are documentation
changes and do not require reflashing the deployed images.
