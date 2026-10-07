#!/usr/bin/env python3
"""Read /info and verify a deployed device against its local firmware image."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import urllib.request


def verify(host, binary, check_weather=False):
    root = Path(__file__).resolve().parents[1]
    version = re.search(r'#define FIRMWARE_VERSION "([^"]+)"',
                        (root / 'include' / 'version.h').read_text()).group(1)
    tag_url = 'https://github.com/renatobo/WeatherStationMQTT/tree/v' + version
    with urllib.request.urlopen('http://' + host + '/info', timeout=15) as response:
        html = response.read().decode()
    text = re.sub(r'<[^>]*>', '\n', html)

    def field(label):
        match = re.search(re.escape(label) + r':\s*([^\n]+)', text)
        if not match:
            raise RuntimeError('Missing diagnostic field: ' + label)
        return match.group(1).strip()

    expected = hashlib.md5(Path(binary).read_bytes()).hexdigest()
    actual = field('Sketch MD5')
    if actual != expected:
        raise RuntimeError('Running sketch MD5 differs from archived binary')
    if field('Firmware version') != 'v' + version or 'href="' + tag_url + '"' not in html:
        raise RuntimeError('Firmware version or GitHub tag link mismatch')
    rssi = field('Wi-Fi RSSI')
    if not re.fullmatch(r'-\d+ dBm', rssi):
        raise RuntimeError('Wi-Fi RSSI is not a connected numeric dBm reading')
    reset = field('Reset reason')
    if not reset or reset == 'unavailable':
        raise RuntimeError('Reset reason unavailable')
    result = {
        'host': host, 'version': version, 'github_tag': tag_url,
        'sketch_md5': actual, 'matches_local_binary': True,
        'build_date': field('SW build date'),
        'profile': field('Device profile'), 'reset_reason': reset,
        'wifi_rssi': rssi, 'uptime': field('Device uptime'),
    }
    if check_weather:
        state = field('Weather state')
        attempts, successes, failures = map(int, field('Weather attempts/successes/failures').split('/'))
        if state not in ('fresh', 'updating') or successes < 1 or field('Weather HTTP status') != '200':
            raise RuntimeError('No successful validated weather response on device')
        if field('Weather last error') != 'none' or int(field('Weather TLS error')) != 0:
            raise RuntimeError('Weather request or TLS validation failed')
        if int(field('Weather last good age seconds')) >= 1200:
            raise RuntimeError('Weather cache is stale')
        result['weather'] = {'state': state, 'attempts': attempts, 'successes': successes,
                             'failures': failures, 'verified_https_and_data': True,
                             'heap_free_minimum': field('Heap free/minimum bytes'),
                             'heap_largest_minimum': field('Heap largest/minimum block bytes')}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='Device hostname or IP address')
    parser.add_argument('--binary', required=True, help='Archived firmware.bin to compare')
    parser.add_argument('--weather', action='store_true', help='Require a fresh validated HTTPS weather response')
    args = parser.parse_args()
    verify(args.host, args.binary, args.weather)
