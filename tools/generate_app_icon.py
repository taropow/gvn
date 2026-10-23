"""Draw the application's vector-style identity and emit optically sized Windows icons.

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
    def layer_round(bounds,radius,color):
        layer=Image.new('RGBA',(n,n));ImageDraw.Draw(layer).rounded_rectangle(box(bounds),round(radius*factor),fill=color)
        return layer
    image=Image.new('RGBA',(n,n))
    # A dark frame stays readable on both light and dark taskbars.
    image.alpha_composite(layer_round((14,14,242,242),52,'#102D43'))
    rim=layer_round((18,18,238,238),48,'#164058');image.alpha_composite(rim)
    if size>=24:
        image.alpha_composite(layer_round((69,39,221,202),20,'#78DCD8'))
    image.alpha_composite(layer_round((34,53,211,219),23,'#F3FCFF'))
    viewport=Image.new('RGBA',(n,n));pixels=ImageDraw.Draw(viewport)
    top,bottom=(112,215,247),(45,125,213)
    for y in range(round(65*factor),round(193*factor)+1):
        t=(y/factor-65)/128
        rgb=tuple(round(a+(b-a)*t) for a,b in zip(top,bottom))
        pixels.line((round(46*factor),y,round(199*factor),y),fill=(*rgb,255))
    pixels.ellipse(box((148,80,178,110)),fill='#FFD36B')
    pixels.polygon([box(p) for p in [(46,172),(99,111),(153,177),(175,150),(199,174),(199,198),(46,198)]],fill='#286BAE')
    pixels.polygon([box(p) for p in [(46,185),(88,146),(119,172),(152,129),(199,184),(199,199),(46,199)]],fill='#20B7A4')
    mask=Image.new('L',(n,n));ImageDraw.Draw(mask).rounded_rectangle(box((46,65,199,192)),round(13*factor),fill=255)
    viewport.putalpha(mask);image.alpha_composite(viewport)
    # The small footer suggests image/alpha pixels without relying on tiny letters.
    d=ImageDraw.Draw(image)
    d.rounded_rectangle(box((48,201,72,208)),round(3*factor),fill='#9DBBC8')
    d.rounded_rectangle(box((80,201,104,208)),round(3*factor),fill='#CEE0E7')
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
