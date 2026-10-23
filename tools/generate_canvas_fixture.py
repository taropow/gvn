"""Generate a display-sized animation for native scrollbar/player layout tests."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1] / 'tests' / 'fixtures' / 'canvas'
root.mkdir(parents=True, exist_ok=True)
frames = []
for offset in (0, 80):
    image = Image.new('RGBA', (512, 384), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rectangle((32 + offset, 24, 220 + offset, 200), fill=(30, 130, 240, 255))
    draw.ellipse((160, 90 + offset, 440, 290 + offset), fill=(245, 95, 65, 128))
    frames.append(image)
frames[0].save(root / 'scroll.apng', save_all=True, append_images=frames[1:],
               duration=[200, 300], loop=0, disposal=[0, 0], blend=[0, 0])
