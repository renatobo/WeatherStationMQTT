"""Read device credentials without putting their values into command arguments."""
import configparser
import hashlib
import json
from pathlib import Path


DEVICES = ('office', 'workshop', 'Printer3d')


def device_for(environment):
    device = environment[:-4] if environment.endswith('_ota') else environment
    if device not in DEVICES:
        raise ValueError('Unknown security device; select office, workshop or Printer3d')
    return device


def read(root, device):
    if device not in DEVICES:
        raise ValueError('Unknown security device')
    config = configparser.ConfigParser(interpolation=None)
    config.read(Path(root) / 'mysecret_envs.ini')
    section = 'security:' + device
    values = {key: config.get(section, key) for key in ('ota_password', 'provisioning_password')}
    for name, password in values.items():
        if not 20 <= len(password) <= 63 or any(ord(c) < 33 or ord(c) > 126 for c in password):
            raise ValueError(name + ' must have 20-63 printable non-whitespace ASCII characters')
    if values['ota_password'] == values['provisioning_password']:
        raise ValueError('Use separate OTA and provisioning passwords')
    return config, values


def header(values):
    digest = hashlib.md5(values['ota_password'].encode()).hexdigest()
    return ('#pragma once\n// Generated private credentials; never commit.\n'
            '#define OTA_PASSWORD_HASH ' + json.dumps(digest) + '\n'
            '#define PROVISIONING_PASSWORD ' + json.dumps(values['provisioning_password']) + '\n')
