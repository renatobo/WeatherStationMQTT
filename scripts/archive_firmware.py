#!/usr/bin/env python3
"""Archive built firmware and private configuration locally; never upload."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive(label):
    root = Path(__file__).resolve().parents[1]
    output = root / '.recovery' / label
    profiles = ('office', 'office_ota', 'workshop', 'workshop_ota',
                'Printer3d', 'Printer3d_ota')
    for profile in profiles:
        for name in ('firmware.bin', 'firmware.elf'):
            source = root / '.pio' / 'build' / profile / name
            if not source.exists():
                raise FileNotFoundError('Build all six profiles first: ' + str(source))
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    output.parent.chmod(0o700)
    manifest = {
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source_commit': subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'platformio_core': subprocess.check_output(
            ['pio', '--version'], cwd=root, text=True).strip(),
        'source_worktree_dirty': bool(subprocess.check_output(
            ['git', 'status', '--porcelain'], cwd=root, text=True).strip()),
        'sha256': {},
        'libraries': {},
        'note': 'Build artifacts only. Deployment identity requires upload evidence; libraries list cached inventory.',
    }
    sources = [root / n for n in ('src', 'include', 'lib', 'test', 'scripts') if (root / n).exists()]
    files = [p for folder in sources for p in folder.rglob('*') if p.is_file()]
    files += [root / n for n in ('platformio.ini', 'build-profiles.ini',
                                 'mysecret_envs.ini') if (root / n).exists()]
    for profile in profiles:
        for name in ('firmware.bin', 'firmware.elf'):
            source = root / '.pio' / 'build' / profile / name
            files.append(source)
        overlay = root / '.pio' / 'build' / profile / 'bounded-sdk' / 'WiFiClientSecureBearSSL.cpp'
        if overlay.exists():
            files.append(overlay)
        libraries = {}
        for library in (root / '.pio' / 'libdeps' / profile).iterdir():
            if not library.is_dir() or library.name.startswith('.'):
                continue
            if (library / '.git').exists():
                libraries[library.name] = subprocess.check_output(
                    ['git', '-C', str(library), 'rev-parse', 'HEAD'], text=True).strip()
            elif (library / '.piopm').exists():
                libraries[library.name] = json.loads(
                    (library / '.piopm').read_text())['version']
        manifest['libraries'][profile] = libraries
    for source in files:
        relative = source.relative_to(root)
        target = output / relative
        target.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
        shutil.copy2(source, target)
        target.chmod(0o600)
        manifest['sha256'][str(relative)] = digest(target)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'manifest.json').chmod(0o600)
    # Read every copy back before reporting success.
    for name, expected in manifest['sha256'].items():
        if digest(output / name) != expected:
            raise RuntimeError('Archive checksum mismatch: ' + name)
    print('Verified private archive:', output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('label', help='New archive directory name under .recovery')
    args = parser.parse_args()
    if Path(args.label).name != args.label or args.label in ('.', '..'):
        parser.error('label must be a single directory name')
    archive(args.label)
