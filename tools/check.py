#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check source formatting, QML types and local script syntax without Git or network access."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def tool(name):
    override = os.environ.get(name.upper().replace('-', '_'))
    found = shutil.which(override) if override else None
    if not override and name in ('qmllint', 'qmlformat'):
        for query in ('qtpaths6', '/usr/lib/qt6/bin/qtpaths', '/usr/lib64/qt6/bin/qtpaths', 'qtpaths'):
            qtpaths = shutil.which(query)
            if not qtpaths:
                continue
            version = subprocess.check_output([qtpaths, '--qt-version'], text=True).strip()
            if not version.startswith('6.'):
                continue
            bindir = subprocess.check_output([qtpaths, '--query', 'QT_INSTALL_BINS'], text=True).strip()
            candidate = Path(bindir) / name
            if candidate.is_file():
                found = str(candidate)
                break
    if not found and not override:
        found = shutil.which(name)
    if not found:
        raise RuntimeError(f'{name} is required; install it separately or set its uppercase environment variable.')
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--format', action='store_true', help='Apply the configured format before checking.')
    args = parser.parse_args()
    cpp = sorted(p for directory in ('src', 'tests', 'tools') for p in (ROOT / directory).rglob('*')
                 if p.suffix in ('.h', '.cpp'))
    qml = sorted((ROOT / 'ui').glob('*.qml'))
    clang = tool('clang-format')
    qmlformat = tool('qmlformat')
    qmllint = tool('qmllint')
    failed = []
    for paths, command in ((cpp, [clang, '--style=file']), (qml, [qmlformat, '--settings', str(ROOT / '.qmlformat.ini')])):
        for path in paths:
            result = subprocess.run([*command, str(path)], stdout=subprocess.PIPE, check=True)
            if result.stdout == path.read_bytes():
                continue
            if args.format:
                path.write_bytes(result.stdout)
            else:
                failed.append(str(path.relative_to(ROOT)))
    if failed:
        print('Formatting differs: ' + ', '.join(failed), file=sys.stderr)
        print('Run python3 tools/check.py --format.', file=sys.stderr)
    # KCM context objects are documented by narrowly scoped inline suppressions.
    lint = subprocess.run([qmllint, "--unqualified", "info", *map(str, qml)], cwd=ROOT, check=False)
    subprocess.run(['bash', '-n', str(ROOT / 'tools/install-local.sh')], check=True)
    for path in (ROOT / 'tools').glob('*.py'):
        compile(path.read_bytes(), str(path), 'exec')
    if failed or lint.returncode:
        return 1
    print(f'Formatting, QML and script checks passed ({len(cpp)} C++ files, {len(qml)} QML files).')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
