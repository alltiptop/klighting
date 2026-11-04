#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Clean PNG metadata in an explicit file or directory (non-recursive), in place."""
import argparse
import json
import os
from pathlib import Path
import stat
import sys
import tempfile

# Also support isolated Python (-I), with imports confined to this tool directory.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from png_policy import MAX_FILE_BYTES, clean_png


def clean_file(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode):
        raise ValueError('expected a regular file, not a symbolic link')
    if path.suffix.lower() != '.png':
        raise ValueError('expected a .png path')
    with path.open('rb') as source:
        original = source.read(MAX_FILE_BYTES + 1)
    cleaned, removed = clean_png(original)
    if cleaned == original:
        return []
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.png-clean-', delete=False) as output:
            temporary = Path(output.name)
            output.write(cleaned)
            output.flush()
            os.fchmod(output.fileno(), stat.S_IMODE(info.st_mode))
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return removed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path, help='one PNG file or a directory of PNGs (no recursion)')
    args = parser.parse_args()
    try:
        if args.path.is_symlink():
            raise ValueError('symbolic links are not supported')
        paths = (sorted(p for p in args.path.iterdir() if p.suffix.lower() == '.png' and p.is_file())
                 if args.path.is_dir() else [args.path])
    except (OSError, ValueError):
        print('Cannot read the supplied path; expected a file or directory, not a symbolic link.', file=sys.stderr)
        return 1
    failed = False
    for path in paths:
        label = json.dumps(str(path))
        try:
            removed = clean_file(path)
            print(f'{label}: removed {", ".join(removed)}' if removed else f'{label}: already clean')
        except (OSError, ValueError) as error:
            reason = f'file operation failed (errno {error.errno})' if isinstance(error, OSError) else str(error)
            print(f'{label}: {reason}', file=sys.stderr)
            failed = True
    if not paths:
        print('No PNG files directly in the supplied directory.')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
