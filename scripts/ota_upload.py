#!/usr/bin/env python3
"""Authenticated OTA with credentials read in-process from ignored configuration."""
import argparse
import hashlib
import importlib.util
import logging
import os
from pathlib import Path
import secrets
import socket
import sys

from security_config import device_for, read


def load_uploader(path=None):
    directory = Path(os.environ.get('PLATFORMIO_CORE_DIR', str(Path.home() / '.platformio')))
    path = Path(path) if path else directory / 'packages' / 'framework-arduinoespressif8266' / 'tools' / 'espota.py'
    spec = importlib.util.spec_from_file_location('pinned_espota', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.PROGRESS = False
    return module


def probe(host, password):
    # Authentication-only rejection test: no image and no listening TCP server.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.settimeout(10)
        udp.sendto(b'0 9 1 00000000000000000000000000000000\n', (host, 8266))
        response, sender = udp.recvfrom(128)
        if not response.startswith(b'AUTH '):
            raise RuntimeError('Device did not require OTA authentication')
        nonce = response.decode().split()[1]
        cnonce = secrets.token_hex(16)
        pass_hash = hashlib.md5(password.encode()).hexdigest()
        answer = hashlib.md5((pass_hash + ':' + nonce + ':' + cnonce).encode()).hexdigest()
        udp.sendto(('200 ' + cnonce + ' ' + answer + '\n').encode(), sender)
        response, _ = udp.recvfrom(128)
        if response != b'Authentication Failed':
            raise RuntimeError('Device did not reject invalid authentication')
    print('OTA authentication required; invalid password rejected; no firmware transferred.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--env', required=True)
    parser.add_argument('--binary')
    parser.add_argument('--uploader', help='Pinned SDK espota.py path supplied by PlatformIO')
    parser.add_argument('--filesystem', action='store_true')
    parser.add_argument('--bootstrap', action='store_true', help='First migration from existing unauthenticated firmware only')
    parser.add_argument('--probe', choices=('wrong', 'empty'), help='Test rejection without sending an image')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    config, values = read(root, device_for(args.env))
    if any(value.startswith(('EXAMPLE_', 'REPLACE_')) for value in values.values()):
        raise ValueError('Replace example credentials before any live upload or probe')
    host = config.get('env:' + args.env, 'upload_port')
    if args.probe:
        probe(host, '' if args.probe == 'empty' else secrets.token_urlsafe(24))
        return 0
    if not args.binary or not Path(args.binary).is_file():
        parser.error('--binary must name an existing image')
    uploader = load_uploader(args.uploader)
    logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')
    password = '' if args.bootstrap else values['ota_password']
    # Invoke the pinned SDK uploader directly, never its CLI option/debug logger.
    return uploader.serve(host, '0.0.0.0', 8266, secrets.randbelow(50000) + 10000,
                          password, args.binary, uploader.SPIFFS if args.filesystem else uploader.FLASH)


if __name__ == '__main__':
    sys.exit(main())
