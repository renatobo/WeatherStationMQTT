"""Build a timeout-bounded copy of the pinned SDK TLS source; never edit its cache."""
from pathlib import Path
import hashlib

Import('env')

EXPECTED_SHA256 = 'c993ccd21b962c67e3e07ff4747ab4d812ecbf619223aaf54443e971cfd5c487'


def bounded_tls(build_env, node):
    source = Path(node.srcnode().get_abspath())
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        raise RuntimeError('Pinned BearSSL source changed; review timeout overlay before building')
    text = data.decode()
    if text.count('_timeout = 15000;') != 2 or text.count('WiFi.hostByName(name, remote_addr)') != 2:
        raise RuntimeError('Unexpected BearSSL timeout implementation')
    # Constructor and _freeSSL reset the handshake timeout; Stream::setTimeout alone is insufficient.
    text = text.replace('_timeout = 15000;', '_timeout = 4000;')
    text = text.replace('WiFi.hostByName(name, remote_addr)',
                        'WiFi.hostByName(name, remote_addr, 1000)')
    text = text.replace('if (ret) _timeout = 5000;', 'if (ret) _timeout = 250;')
    target = Path(build_env.subst('$BUILD_DIR')) / 'bounded-sdk' / source.name
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text() != text:
        target.write_text(text)
    return build_env.File(str(target))


env.AddBuildMiddleware(bounded_tls, '*WiFiClientSecureBearSSL.cpp')
