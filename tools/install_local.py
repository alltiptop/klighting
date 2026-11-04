#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify and atomically install a trusted, locally built CMake bundle."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile


def relative_path(value):
    path = PurePosixPath(value)
    if not path.parts or not value or path.is_absolute() or '..' in path.parts or str(path) != value:
        raise ValueError(f'Invalid relative path: {value!r}')
    return Path(value)


def verified_bundle(bundle):
    """Verify the entire payload before loading code or modifying destinations."""
    manifest = bundle / 'SHA256SUMS'
    if manifest.is_symlink():
        raise ValueError('The manifest must be a regular file')
    declared = {}
    for line in manifest.read_text().splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  (.+)', line)
        if not match:
            raise ValueError('Invalid SHA256SUMS entry')
        path = relative_path(match[2])
        if path in declared or path == Path('SHA256SUMS'):
            raise ValueError(f'Duplicate or invalid manifest entry: {path}')
        declared[path] = match[1]
    actual = set()
    for path in bundle.rglob('*'):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f'Not a regular bundle entry: {path}')
        if path.is_file() and path != manifest:
            actual.add(path.relative_to(bundle))
    if actual != declared.keys():
        raise ValueError('Bundle files do not match SHA256SUMS')
    for path, expected in declared.items():
        with (bundle / path).open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if digest != expected:
            raise ValueError(f'Checksum mismatch: {path}')
    if not {Path('bundle.json'), Path('klighting-plugin-check')} <= actual:
        raise ValueError('Missing bundle metadata or plugin checker')
    if any(path.parts[0] != 'root' and path not in (Path('bundle.json'), Path('klighting-plugin-check'))
           for path in actual):
        raise ValueError('Unexpected bundle artifact outside root/')
    metadata = json.loads((bundle / 'bundle.json').read_text())
    if metadata.get('version') != 1:
        raise ValueError('Unsupported bundle version')
    for key in ('plugins', 'data'):
        metadata[key] = relative_path(metadata[key])
    payload = sorted(path.relative_to('root') for path in actual if path.parts[0] == 'root')
    if not payload:
        raise ValueError('Empty installation payload')
    return payload, metadata


def obsolete_plugins(bundle, payload, metadata, destination):
    """Declarations belong to modules; only narrowly named plugin aliases retire."""
    plugins = metadata['plugins']
    declarations = bundle / 'root' / metadata['data'] / 'klighting/drivers'
    aliases = []
    for declaration in sorted(declarations.glob('*/legacy-plugins.txt')):
        for entry in declaration.read_text().splitlines():
            if not re.fullmatch(r'v[1-9][0-9]*/[a-zA-Z0-9_-]+', entry):
                raise ValueError(f'Invalid legacy plugin alias in {declaration}: {entry!r}')
            version, name = entry.split('/')
            aliases.append((plugins / 'ambient-led/drivers' / version, name))
    aliases.append((plugins / 'kwin/effects/plugins', 'ambientled'))
    old = set()
    for directory, name in aliases:
        for candidate in (destination / directory).glob(f'{name}*.so'):
            if re.fullmatch(re.escape(name) + r'(_dev[0-9]+)?\.so', candidate.name):
                old.add(directory / candidate.name)
    legacy_config = plugins / 'kwin/effects/configs/kwin_ambientled_config.so'
    if (destination / legacy_config).exists():
        old.add(legacy_config)
    return sorted(old - set(payload))


def check_plugins(bundle, payload, metadata):
    plugins = [bundle / 'root' / path for path in payload
               if path.is_relative_to(metadata['plugins']) and path.suffix == '.so']
    if not plugins:
        return
    command = [str(bundle / 'klighting-plugin-check'), *map(str, plugins)]
    if os.geteuid() == 0:
        user = os.environ.get('SUDO_USER')
        if not user or user == 'root':
            raise ValueError('Run with sudo from a regular account; plugin checks must not run as root')
        command = ['runuser', '-u', user, '--', *command]
    subprocess.run(command, check=True, timeout=10 + 7 * len(plugins))


def install(bundle, payload, obsolete, destination):
    """Stage complete files, preserving old mapped inodes and path-qualified backups."""
    for relative in [*payload, *obsolete]:
        target = destination / relative
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise ValueError(f'Installation target is not a regular file: {target}')
    backups = destination / 'var/lib/klighting/backups'
    backups.mkdir(parents=True, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix='install.', dir=backups))
    staged = []
    try:
        for relative in [*payload, *obsolete]:
            target = destination / relative
            if target.exists():
                saved = backup / relative
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, saved)
        for relative in payload:
            source, target = bundle / 'root' / relative, destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temporary = tempfile.mkstemp(prefix='.klighting-install.', dir=target.parent)
            os.close(descriptor)
            staged.append((Path(temporary), target))
            shutil.copyfile(source, temporary)
            os.chmod(temporary, 0o755 if source.stat().st_mode & 0o111 else 0o644)
        for temporary, target in staged:
            os.replace(temporary, target)
        for relative in obsolete:
            (destination / relative).unlink()
    finally:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)
    return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify without installing')
    parser.add_argument('bundle', type=Path, help='CMake local_bundle output directory')
    args = parser.parse_args()
    if not args.check and os.geteuid() != 0:
        raise ValueError('This installer writes system files. Run it yourself with sudo')
    bundle = args.bundle.resolve(strict=True)
    payload, metadata = verified_bundle(bundle)
    obsolete = obsolete_plugins(bundle, payload, metadata, Path('/'))
    check_plugins(bundle, payload, metadata)
    if args.check:
        print('Bundle checks passed. No files installed.')
        return
    backup = install(bundle, payload, obsolete, Path('/'))
    print(f'Installed {len(payload)} files. Backup: {backup}')
    print('Reopen Lighting settings. Run activation commands as your regular user:')
    print('  systemctl --user daemon-reload')
    print('  systemctl --user restart klightingd.service')
    print('  qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect ambientled')
    print('  qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect ambientled')
    print('  klightingd --status')
    print('Unload any ambientled_devN aliases first. Qt may retain old native code;')
    print('log out and back in to ensure updated KWin plugins are loaded.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
