#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Allow UTF-8 source text and validated static PNGs in a clean checkout/export.

Only the root .git entry is skipped. Ignore files cannot exempt committed blobs.
No source files are executed, no Git commands are invoked, and nothing is changed.
"""
import argparse
import json
import os
from pathlib import Path
import re
import stat
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from png_policy import MAX_FILE_BYTES, SIGNATURE, validate_png

CONTROL_CHARACTERS = re.compile(r'[\x00-\x08\x0b\x0c\x0e-\x1f\x7f-\x9f]')
BINARY_SIGNATURES = (b'%PDF-', b'!<arch>\n', b'!<thin>\n')
LFS_SIGNATURE = b'version https://git-lfs.github.com/spec/v1'


def check_file(path, data):
    if path.suffix.lower() == '.png':
        validate_png(data)
        return 'png'
    if data.startswith(SIGNATURE):
        raise ValueError('PNG images must use the .png extension')
    if data.startswith(BINARY_SIGNATURES):
        raise ValueError('binary/document/archive format is not allowed')
    if data.startswith(LFS_SIGNATURE):
        raise ValueError('Git LFS pointers are not allowed')
    try:
        content = data.decode('utf-8')
    except UnicodeDecodeError:
        raise ValueError('binary file: expected UTF-8 source text or a validated PNG') from None
    if CONTROL_CHARACTERS.search(content):
        raise ValueError('binary/control bytes in a text file')
    return 'text'


def check_tree(root):
    if root.is_symlink() or not root.is_dir():
        raise ValueError('source must be a real directory')
    files = images = failures = 0

    def walk_error(error):
        raise error

    for directory, folders, names in os.walk(root, onerror=walk_error):
        if Path(directory) == root:
            folders[:] = [name for name in folders if name != '.git']
            names = [name for name in names if name != '.git']
        for name in folders + names:
            path = Path(directory) / name
            label = json.dumps(str(path.relative_to(root)))
            try:
                if name in ('.git', '.gitmodules'):
                    raise ValueError('nested repositories and submodules are not allowed')
                info = path.lstat()
                if stat.S_ISDIR(info.st_mode):
                    continue
                if not stat.S_ISREG(info.st_mode):
                    raise ValueError('links and special files are not allowed')
                if info.st_size > MAX_FILE_BYTES:
                    raise ValueError('file exceeds the 16 MiB limit')
                with path.open('rb') as source:
                    data = source.read(MAX_FILE_BYTES + 1)
                if len(data) > MAX_FILE_BYTES:
                    raise ValueError('file exceeds the 16 MiB limit')
                images += check_file(path, data) == 'png'
                files += 1
            except (OSError, ValueError) as error:
                reason = f'file operation failed (errno {error.errno})' if isinstance(error, OSError) else str(error)
                print(f'{label}: {reason}', file=sys.stderr)
                failures += 1
    if failures:
        print(f'Source file policy failed: {failures} rejected entries.', file=sys.stderr)
        return 1
    print(f'Source file policy passed: {files} files, {images} PNGs.')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='clean checkout or source export (before building)')
    args = parser.parse_args()
    try:
        return check_tree(args.source.absolute())
    except (OSError, ValueError) as error:
        reason = f'file operation failed (errno {error.errno})' if isinstance(error, OSError) else str(error)
        print(f'Source file policy could not complete: {reason}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
