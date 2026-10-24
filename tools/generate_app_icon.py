"""Draw a bold white V and viewfinder on a blue panel for Windows icons.

Only Pillow is needed at development time; the application loads its embedded ICO.
Toolbar command icons remain independent from this application identity.
"""
from pathlib import Path
import io,struct
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
SIZES=(16,20,24,32,40,48,64,96,128,256)

def draw_icon(size):
    scale=4
    n=size*scale
    factor=n/256
    def box(values):return tuple(round(v*factor) for v in values)
    image=Image.new('RGBA',(n,n))
    ImageDraw.Draw(image).rounded_rectangle(box((4,4,252,252)),round(38*factor),fill='#1677E8')
    # Font-independent geometry keeps the chosen V + frame design identical
    # across machines and legible at small window-icon resolutions.
    glyph=ImageDraw.Draw(image)
    for points in [
        [(37,87),(37,37),(87,37),(87,55),(55,55),(55,87)],
        [(169,37),(219,37),(219,87),(201,87),(201,55),(169,55)],
        [(37,169),(55,169),(55,201),(87,201),(87,219),(37,219)],
        [(201,169),(219,169),(219,219),(169,219),(169,201),(201,201)],
        [(68,79),(105,79),(128,151),(151,79),(188,79),(148,183),(108,183)],
    ]:
        glyph.polygon([box(point) for point in points],fill='white')
    return image.resize((size,size),Image.Resampling.LANCZOS)

def main():
    output=ROOT/'src/assets';output.mkdir(exist_ok=True)
    frames=[]
    for size in SIZES:
        image=draw_icon(size);stream=io.BytesIO();image.save(stream,format='PNG');frames.append((size,stream.getvalue()))
    offset=6+16*len(frames);header=struct.pack('<HHH',0,1,len(frames));entries=[]
    for size,data in frames:
        entries.append(struct.pack('<BBBBHHII',size if size<256 else 0,size if size<256 else 0,0,0,1,32,len(data),offset));offset+=len(data)
    (output/'gvn.ico').write_bytes(header+b''.join(entries)+b''.join(data for _,data in frames))
    draw_icon(256).save(output/'gvn-icon.png')
    # This preview is generated development output, not part of the application.
    preview=Image.new('RGB',(720,224),'#F3F5F7');d=ImageDraw.Draw(preview);d.rectangle((0,112,720,224),fill='#20252D')
    x=20
    for size in (16,24,32,48,64,96,128):
        icon=draw_icon(size)
        shown=min(size,96)
        if size>shown:icon=icon.resize((shown,shown),Image.Resampling.LANCZOS)
        preview.paste(icon,(x,8+(96-shown)//2),icon);preview.paste(icon,(x,120+(96-shown)//2),icon)
        x+=shown+24
    path=ROOT/'.cache/app-icon-preview.png';path.parent.mkdir(exist_ok=True);preview.save(path)
    print(output/'gvn.ico')

if __name__=='__main__':main()
