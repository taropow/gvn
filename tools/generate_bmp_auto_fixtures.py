"""Generate small, independent RGB555/565 BMP fixtures with every component code."""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'tests/fixtures/auto-bmp'


def bitmap(name, rgb565, header=40, bitfields=True, top=False, sparse=False):
    width, height = (2, 2) if sparse else (65, 3)
    stride = (width * 2 + 3) & ~3
    masks = (0xf800, 0x07e0, 0x001f) if rgb565 else (0x7c00, 0x03e0, 0x001f)
    extra = 12 if bitfields and header == 40 else 0
    offset = 14 + header + extra
    data = bytearray(offset + stride * height)
    struct.pack_into('<2sIHHI', data, 0, b'BM', len(data), 0, 0, offset)
    struct.pack_into('<IiiHHIIiiII', data, 14, header, width, -height if top else height,
                     1, 16, 3 if bitfields else 0, stride * height, 11811, 5906, 0, 0)
    if bitfields:
        struct.pack_into('<III', data, 54, *masks)
    for y in range(height):
        for x in range(width):
            n = y * width + x
            if sparse:
                r = b = 31 if n % 2 else 0
                g = (63 if rgb565 else 31) if n % 2 else 0
            else:
                r, b = n % 32, (31 - n * 3) % 32
                g = (n * 5) % (64 if rgb565 else 32)
            value = (r << (11 if rgb565 else 10)) | (g << 5) | b
            struct.pack_into('<H', data, offset + (y if top else height - 1 - y) * stride + x * 2, value)
    path = OUT / name
    if path.exists():
        assert path.read_bytes() == data, f'Refusing to overwrite different fixture: {path}'
    else:
        path.write_bytes(data)


if __name__ == '__main__':
    OUT.mkdir(exist_ok=True)
    bitmap('rgb555.bmp', False, bitfields=False)
    bitmap('rgb555-fields.bmp', False, top=True)
    bitmap('rgb565.bmp', True)
    bitmap('rgb565-v4.bmp', True, header=108, top=True)
    bitmap('rgb565-sparse.bmp', True, sparse=True)
    print(json.dumps(dict(fixtures=5, complete_component_codes=True)))
