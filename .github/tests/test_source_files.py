# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

TOOLS = Path(__file__).resolve().parents[2] / 'tools'
sys.path.insert(0, str(TOOLS))
from png_policy import PngError, clean_png, validate_png

PNG = b'\x89PNG\r\n\x1a\n'
HEADER = struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0)
PIXEL = b'\x00\x12\x34\x56\xff'  # One unfiltered, opaque RGBA pixel.


def chunk(kind, payload=b''):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


def png(*extra, header=HEADER, compressed=None):
    return (PNG + chunk(b'IHDR', header) + b''.join(extra)
            + chunk(b'IDAT', zlib.compress(PIXEL) if compressed is None else compressed) + chunk(b'IEND'))


class PngPolicyTests(unittest.TestCase):
    def test_static_png_types_and_interlace(self):
        # Independent, explicit one-pixel scanlines: grey, RGB, indexed,
        # grey+alpha and 16-bit RGBA, plus the single nonempty Adam7 pass.
        cases = [(1, 0, 0, b'\0\x80', []), (8, 2, 0, b'\0\x12\x34\x56', []),
                 (2, 3, 0, b'\0\0', [chunk(b'PLTE', b'\x12\x34\x56'), chunk(b'tRNS', b'\xff')]),
                 (8, 4, 0, b'\0\x12\xff', []), (16, 6, 0, b'\0' + b'\xff' * 8, []),
                 (8, 6, 1, PIXEL, [])]
        for depth, color, interlace, raw, extra in cases:
            with self.subTest(depth=depth, color=color, interlace=interlace):
                header = struct.pack('>IIBBBBB', 1, 1, depth, color, 0, 0, interlace)
                validate_png(png(*extra, header=header, compressed=zlib.compress(raw)))

    def test_safe_color_chunks_and_split_idat(self):
        validate_png(png(chunk(b'sRGB', b'\0'), chunk(b'gAMA', struct.pack('>I', 45455)),
                         chunk(b'pHYs', struct.pack('>IIB', 3780, 3780, 1))))
        compressed = zlib.compress(PIXEL)
        validate_png(PNG + chunk(b'IHDR', HEADER) + chunk(b'IDAT', compressed[:3])
                     + chunk(b'IDAT', compressed[3:]) + chunk(b'IEND'))

    def test_metadata_is_removed_without_changing_image_bytes(self):
        extra = [chunk(b'tEXt', b'Comment\0private metadata'), chunk(b'zTXt', b'Comment\0\0example'),
                 chunk(b'iTXt', b'Comment\0example'), chunk(b'eXIf', b'example'), chunk(b'tIME', b'example')]
        cleaned, removed = clean_png(png(*extra))
        self.assertEqual(cleaned, png())
        self.assertEqual(removed, ['tEXt', 'zTXt', 'iTXt', 'eXIf', 'tIME'])
        self.assertEqual(clean_png(cleaned), (cleaned, []))
        for item in extra:
            with self.subTest(chunk=item[4:8]):
                with self.assertRaises(PngError):
                    validate_png(png(item))

    def test_rejects_corruption_hidden_payloads_and_invalid_structure(self):
        valid = png()
        oversized = struct.pack('>IIBBBBB', 16384, 16384, 8, 6, 0, 0, 0)
        cases = {
            'signature': b'not a PNG', 'trailing data': valid + b'payload',
            'CRC': valid[:-1] + bytes([valid[-1] ^ 1]), 'truncated chunk': valid[:-3],
            'missing end': valid[:-12], 'duplicate header': png(chunk(b'IHDR', HEADER)),
            'wrong first chunk': PNG + chunk(b'IDAT') + valid[8:],
            'huge chunk': PNG + struct.pack('>I4s', 0xffffffff, b'IHDR'),
            'oversized decoded data': png(header=oversized),
            'bad header': png(header=b'invalid'),
            'unknown chunk': png(chunk(b'vpAg', b'payload')),
            'animation': png(chunk(b'acTL', struct.pack('>II', 1, 0))),
            'profile': png(chunk(b'iCCP', b'profile\0\0payload')),
            'wrong transparency': png(chunk(b'tRNS', b'\0\0')),
            'bad density': png(chunk(b'pHYs', b'payload')),
            'bad filter': png(compressed=zlib.compress(b'\x05' + PIXEL[1:])),
            'wrong decoded size': png(compressed=zlib.compress(PIXEL[:-1])),
            'expansion beyond dimensions': png(compressed=zlib.compress(PIXEL + b'x' * 100000)),
            'extra zlib stream': png(compressed=zlib.compress(PIXEL) + zlib.compress(b'payload')),
            'truncated zlib': png(compressed=zlib.compress(PIXEL)[:-1]),
            'invalid zlib': png(compressed=b'not zlib'),
            'late metadata': valid[:-12] + chunk(b'pHYs', struct.pack('>IIB', 1, 1, 0)) + valid[-12:],
        }
        for name, data in cases.items():
            with self.subTest(name=name):
                with self.assertRaises(PngError):
                    validate_png(data)
                with self.assertRaises(PngError):
                    clean_png(data)


class SourceFileToolsTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def run_tool(self, name, *args):
        return subprocess.run([sys.executable, '-I', '-B', str(TOOLS / name), *map(str, args)],
                              capture_output=True, text=True, timeout=10, check=False)

    def test_clean_tree_and_source_is_not_executed(self):
        (self.root / 'text.txt').write_text('UTF-8 translations: \u043f\u0440\u0438\u0432\u0435\u0442 \u4e2d\u6587\n', encoding='utf-8')
        (self.root / 'image.png').write_bytes(png())
        (self.root / '.git').mkdir()
        (self.root / '.git' / 'index').write_bytes(b'\0ignored Git metadata')
        (self.root / 'png_policy.py').write_text('raise RuntimeError("untrusted code was executed")\n')
        result = self.run_tool('check_source_files.py', self.root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_binaries_metadata_and_lfs_are_rejected_despite_gitignore(self):
        (self.root / 'build').mkdir()
        (self.root / '.gitignore').write_text('build/\n')
        for name, data in [('image.png', png(chunk(b'tEXt', b'Comment\0payload'))),
                           ('fake.png', b'plain text'), ('image.dat', png()),
                           ('library.a', b'!<arch>\n'), ('paper.pdf', b'%PDF-1.7'),
                           ('app', b'\x7fELF\0'), ('data', b'\xff\xfe'), ('control.txt', b'hello\x1b'),
                           ('pointer', b'version https://git-lfs.github.com/spec/v1\noid sha256:example\n')]:
            with self.subTest(name=name):
                path = self.root / 'build' / name
                path.write_bytes(data)
                result = self.run_tool('check_source_files.py', self.root)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                path.unlink()

    def test_links_and_submodules_are_rejected(self):
        link = self.root / 'link'
        link.symlink_to(self.root, target_is_directory=True)
        self.assertEqual(self.run_tool('check_source_files.py', self.root).returncode, 1)
        link.unlink()
        (self.root / '.gitmodules').write_text('[submodule "example"]\n')
        self.assertEqual(self.run_tool('check_source_files.py', self.root).returncode, 1)

    def test_cleaner_is_atomic_on_validation_failure_and_idempotent(self):
        path = self.root / 'image.png'
        for damaged in (png(chunk(b'tEXt', b'Comment\0private')) + b'trailing',
                        png(chunk(b'iCCP', b'profile\0\0payload'))):
            path.write_bytes(damaged)
            self.assertEqual(self.run_tool('clean_png.py', path).returncode, 1)
            self.assertEqual(path.read_bytes(), damaged)
        path.write_bytes(png(chunk(b'tEXt', b'Comment\0private')))
        path.chmod(0o640)
        for _ in range(2):
            result = self.run_tool('clean_png.py', path)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(path.read_bytes(), png())
            self.assertEqual(path.stat().st_mode & 0o777, 0o640)
        self.assertEqual(sorted(p.name for p in self.root.iterdir()), ['image.png'])

    def test_cleaner_directory_is_not_recursive(self):
        original = png(chunk(b'tEXt', b'Comment\0private'))
        (self.root / 'image.PNG').write_bytes(original)
        (self.root / 'other.txt').write_bytes(original)
        (self.root / 'nested').mkdir()
        (self.root / 'nested' / 'image.png').write_bytes(original)
        result = self.run_tool('clean_png.py', self.root)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'image.PNG').read_bytes(), png())
        self.assertEqual((self.root / 'other.txt').read_bytes(), original)
        self.assertEqual((self.root / 'nested' / 'image.png').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
