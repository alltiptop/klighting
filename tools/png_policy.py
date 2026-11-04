# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded structural checks for the repository's static PNG subset.

This is not an image decoder or a steganography detector. Only image chunks and
small, fixed-format color/density chunks are accepted; arbitrary metadata is not.
"""
import struct
import zlib

SIGNATURE = b'\x89PNG\r\n\x1a\n'
MAX_FILE_BYTES = 16 * 1024 * 1024
MAX_DECODED_BYTES = 64 * 1024 * 1024
MAX_DIMENSION = 16384
ALLOWED = {b'IHDR', b'PLTE', b'IDAT', b'IEND', b'tRNS', b'sRGB', b'gAMA', b'cHRM', b'pHYs'}
REMOVABLE = {b'tEXt', b'zTXt', b'iTXt', b'eXIf', b'tIME'}
COLOR_CHUNKS = {b'sRGB', b'gAMA', b'cHRM'}
DEPTHS = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
ADAM7 = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
         (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))


class PngError(ValueError):
    pass


def chunks(data):
    """Yield (type, payload, original bytes), checking framing, CRCs and EOF."""
    if len(data) > MAX_FILE_BYTES:
        raise PngError('PNG exceeds the 16 MiB file limit')
    if not data.startswith(SIGNATURE):
        raise PngError('invalid PNG signature')
    offset = len(SIGNATURE)
    while offset < len(data):
        if offset + 12 > len(data):
            raise PngError('truncated PNG or missing IEND')
        length, kind = struct.unpack_from('>I4s', data, offset)
        end = offset + 12 + length
        if end > len(data):
            raise PngError('truncated PNG chunk')
        if not all(65 <= c <= 90 or 97 <= c <= 122 for c in kind) or kind[2] & 32:
            raise PngError('invalid PNG chunk type')
        payload = data[offset + 8:end - 4]
        crc = zlib.crc32(payload, zlib.crc32(kind))
        if crc != struct.unpack_from('>I', data, end - 4)[0]:
            raise PngError('PNG chunk CRC mismatch')
        yield kind, payload, data[offset:end]
        offset = end
        if kind == b'IEND':
            if length or offset != len(data):
                raise PngError('nonempty IEND or data after IEND')
            return
    raise PngError('missing IEND')


def scanline_layout(header):
    if len(header) != 13:
        raise PngError('invalid IHDR length')
    width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', header)
    if not (0 < width <= MAX_DIMENSION and 0 < height <= MAX_DIMENSION):
        raise PngError('PNG dimensions must be between 1 and 16384')
    if depth not in DEPTHS.get(color, ()) or compression or filtering or interlace not in (0, 1):
        raise PngError('unsupported PNG header values')
    passes = ADAM7 if interlace else ((0, 0, 1, 1),)
    layout = []
    for x, y, dx, dy in passes:
        columns = max(0, (width - x + dx - 1) // dx)
        rows = max(0, (height - y + dy - 1) // dy)
        if columns and rows:
            row_bytes = (columns * depth * CHANNELS[color] + 7) // 8
            layout.append((rows, row_bytes + 1))
    if sum(rows * stride for rows, stride in layout) > MAX_DECODED_BYTES:
        raise PngError('PNG exceeds the 64 MiB decoded limit')
    return depth, color, layout


def validate_png(data):
    seen = set()
    compressed = bytearray()
    palette_entries = 0
    previous = None
    for kind, payload, _ in chunks(data):
        if kind not in ALLOWED:
            raise PngError(f'disallowed PNG chunk {kind.decode("ascii")}; use tools/clean_png.py')
        if not seen and kind != b'IHDR':
            raise PngError('IHDR must be the first chunk')
        if kind in seen and kind != b'IDAT':
            raise PngError('duplicate PNG chunk')
        if kind == b'IHDR':
            depth, color, layout = scanline_layout(payload)
        elif kind == b'PLTE':
            if b'IDAT' in seen or b'tRNS' in seen or color in (0, 4):
                raise PngError('PLTE is not allowed here')
            palette_entries = len(payload) // 3
            if len(payload) % 3 or not 1 <= palette_entries <= 256:
                raise PngError('invalid palette length')
            if color == 3 and palette_entries > 2 ** depth:
                raise PngError('palette exceeds indexed bit depth')
        elif kind == b'tRNS':
            if b'IDAT' in seen:
                raise PngError('tRNS must precede IDAT')
            if color == 3:
                valid = 0 < len(payload) <= palette_entries
            elif color in (0, 2):
                valid = len(payload) == CHANNELS[color] * 2
                if valid:
                    valid = all(v[0] < 2 ** depth for v in struct.iter_unpack('>H', payload))
            else:
                valid = False
            if not valid:
                raise PngError('invalid transparency chunk')
        elif kind in COLOR_CHUNKS or kind == b'pHYs':
            if b'IDAT' in seen or (kind in COLOR_CHUNKS and b'PLTE' in seen):
                raise PngError('color/density chunk is out of order')
            if kind == b'sRGB':
                valid = len(payload) == 1 and payload[0] <= 3
            elif kind == b'gAMA':
                valid = len(payload) == 4 and 0 < int.from_bytes(payload, 'big') < 2 ** 31
            elif kind == b'cHRM':
                valid = len(payload) == 32
            else:
                valid = (len(payload) == 9 and payload[8] <= 1
                         and all(v[0] < 2 ** 31 for v in struct.iter_unpack('>I', payload[:8])))
            if not valid:
                raise PngError('invalid color/density chunk')
        elif kind == b'IDAT':
            if b'IDAT' in seen and previous != b'IDAT':
                raise PngError('IDAT chunks must be consecutive')
            if color == 3 and not palette_entries:
                raise PngError('indexed PNG requires a palette')
            compressed.extend(payload)
        elif kind == b'IEND' and b'IDAT' not in seen:
            raise PngError('PNG has no image data')
        seen.add(kind)
        previous = kind
    expected_size = sum(rows * stride for rows, stride in layout)
    inflater = zlib.decompressobj()
    try:
        decoded = inflater.decompress(compressed, expected_size + 1)
    except zlib.error as error:
        raise PngError('invalid PNG zlib stream') from error
    if len(decoded) != expected_size:
        raise PngError('decoded PNG size does not match its dimensions')
    if not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
        raise PngError('incomplete zlib stream or extra compressed data')
    offset = 0
    for rows, stride in layout:
        end = offset + rows * stride
        if any(value > 4 for value in decoded[offset:end:stride]):
            raise PngError('invalid PNG scanline filter')
        offset = end


def clean_png(data):
    """Remove metadata only; retain original image/color chunks byte for byte.

    Unknown chunks, animation and embedded profiles require an image editor.
    Corruption or trailing data is rejected, never silently repaired.
    """
    kept = [SIGNATURE]
    removed = []
    for kind, _, raw in chunks(data):
        if kind in REMOVABLE:
            removed.append(kind.decode('ascii'))
        else:
            kept.append(raw)
    result = b''.join(kept)
    validate_png(result)
    return result, removed
