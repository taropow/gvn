"""Test-only APNG oracle: Pillow decodes isolated PNG frames and alpha_composite blends them.

This avoids Pillow 12.0's APNG OVER path, whose masked paste also interpolates alpha.
The production decoder uses libplum, not this parser or Pillow.
"""
from io import BytesIO
from PIL import Image
import struct
import zlib

SIGNATURE = b'\x89PNG\r\n\x1a\n'

def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))

def render(data):
    position = 8
    header = None
    frames = []
    current = None
    while position < len(data):
        length = struct.unpack_from('>I', data, position)[0]
        kind, payload = data[position+4:position+8], data[position+8:position+8+length]
        position += length + 12
        if kind == b'IHDR': header = payload
        elif kind == b'fcTL':
            current = [struct.unpack('>IIIIIHHBB', payload), bytearray()]
            frames.append(current)
        elif kind == b'IDAT' and current is not None: current[1].extend(payload)
        elif kind == b'fdAT': current[1].extend(payload[4:])
    width, height = struct.unpack_from('>II', header)
    canvas = Image.new('RGBA', (width, height))
    result = []
    for control, compressed in frames:
        _, w, h, x, y, numerator, denominator, disposal, blend = control
        raw = SIGNATURE + chunk(b'IHDR', struct.pack('>II', w, h)+header[8:]) + chunk(b'IDAT', compressed) + chunk(b'IEND', b'')
        with Image.open(BytesIO(raw)) as decoded: patch = decoded.convert('RGBA')
        previous = canvas.copy()
        if blend: canvas.alpha_composite(patch, (x, y))
        else: canvas.paste(patch, (x, y))
        result.append((canvas.copy(), int(1000*numerator/(denominator or 100))))
        if disposal == 1: canvas.paste((0,0,0,0), (x,y,x+w,y+h))
        elif disposal == 2: canvas = previous
    return result
