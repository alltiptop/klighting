#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a small CI image context from tool definitions and driver dependencies."""
import argparse
import hashlib
from pathlib import Path
import re

PACKAGE_NAME = re.compile(r'[a-z0-9][a-z0-9@._+\-]*')


def read_source(root, path):
    if path.resolve() != path or not path.is_file():
        raise ValueError(f'Expected a regular source file: {path.relative_to(root)}')
    return path.read_bytes()


def prepare_context(root, destination):
    root = root.resolve()
    image = root / '.github' / 'ci'
    manifests = [image / 'packages.arch', *sorted((root / 'src' / 'drivers').glob('*/dependencies.arch'))]
    packages = set()
    for path in manifests:
        lines = read_source(root, path).decode('utf-8').splitlines()
        if not lines or any(not PACKAGE_NAME.fullmatch(line) for line in lines):
            raise ValueError(f'Invalid package manifest: {path.relative_to(root)}')
        packages.update(lines)
    contents = {name: read_source(root, image / name) for name in ('Dockerfile', 'install.sh')}
    contents['packages.arch'] = ('\n'.join(sorted(packages)) + '\n').encode('utf-8')
    # A new directory prevents unrelated files or credentials entering the context.
    destination.mkdir(parents=True, exist_ok=False)
    digest = hashlib.sha256()
    for name, data in sorted(contents.items()):
        (destination / name).write_bytes(data)
        digest.update(name.encode('utf-8') + b'\0' + data + b'\0')
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path, help='new directory for the image context')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    print(prepare_context(root, args.destination))


if __name__ == '__main__':
    main()
