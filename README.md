# WeatherStationMQTT

Current tagged firmware: [v0.3.0](https://github.com/renatobo/WeatherStationMQTT/tree/v0.3.0).
See [CHANGELOG.md](CHANGELOG.md).

Additions to the already good [Weather Station](https://github.com/ThingPulse/esp8266-weather-station-color):

As provided by [neptune2](https://github.com/neptune2/esp8266-weather-station-oled-DST)

* DayLightSavings time
* WiFiManager
* OTA

Then I added

* **MQTT client** to log temperature, humidity, and PIR presence status
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

The device publishes to an MQTT broker. Store its address and the weather API
configuration in the ignored `include/mysecrets.h`, not in `settings.h`.

### Build baseline

Use PlatformIO Core **6.2.0** for the validated baseline. Keep one Core installation;
on this Mac the CLI and IDE both use `~/.platformio/penv/bin/pio`. On Apple Silicon,
the pinned Xtensa toolchain is Intel-only and requires Rosetta 2. No upload is
performed by `pio run`.

For a new checkout:

```sh
cp include/mysecrets.h.example include/mysecrets.h
cp mysecret_envs.ini.example mysecret_envs.ini
pio run
```

The examples deliberately use dummy identities, Berlin weather coordinates and a
nonresolving MQTT address. Replace them with private deployment values before
any upload, and verify wiring. `build-profiles.ini` defines all six profiles;
the ignored INI can override their private OTA destinations. Existing private
files should be retained rather than overwritten with examples.

Platform, tool versions, registry libraries and Git library commits are pinned
in `platformio.ini`. [PlatformIO dependency documentation](https://docs.platformio.org/en/latest/librarymanager/dependencies.html)
describes exact-version and Git-commit specifications.

To validate without reusing cached dependencies, copy the project into a new
temporary directory and use a new Core directory:

```sh
baseline_dir=$(mktemp -d)
cp -R src include lib scripts "$baseline_dir/"
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

In v0.2.0, MQTT and HTTP sample timestamps represent sensor acquisition time.
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

Run the weather checks after installing the pinned dependencies:

```sh
clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Iinclude -I.pio/libdeps/workshop/ArduinoJson/src \
  test/weather_test.cpp -o /tmp/weather-weather-tests
/tmp/weather-weather-tests test/fixtures/weather.json
```

Use `scripts/verify_device.py --weather` with the archived device image for a live
check of version, image identity, fresh validated weather, TLS status and heap
diagnostics.

For each subsequent release, bump `include/version.h`, update `CHANGELOG.md`,
build and verify all profiles, commit the intended changes, and create/publish
the matching annotated `vX.Y.Z` tag. Never move an existing tag. Build deployment
images from that exact tagged source and archive them before uploading. Record
the live sketch MD5 against the archived binary after the device reboots.
