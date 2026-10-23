"""Deterministic synthetic APNG/PSD inputs for old/new decoder comparison."""
from pathlib import Path
import argparse, struct, zlib, json, hashlib
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);out=p.parse_args().output
out.mkdir(parents=True,exist_ok=False);files=[]
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
def save(name,data):
    target=out/name;target.write_bytes(data);files.append({'path':name,'sha256':hashlib.sha256(data).hexdigest()})
def png_rows(width,height,kind,bits,seed,interlace):
    maximum=(1<<bits)-1;colors=min(1<<bits,8)
    def pixel(x,y):
        n=(x*7919+y*373+seed*1031)&65535
        color=[n&maximum,(n*17+31)&maximum,(n*29+71)&maximum]
        alpha=[0,maximum,maximum//2][(x+y+seed)%3]
        if kind==3:return [(x*3+y+seed)%colors]
        if kind==0:return [maximum if (x+y+seed)%5==0 else color[0]]
        if kind==4:return [color[0],alpha]
        if kind==2:return [0,0,0] if (x+y+seed)%5==0 else color
        return color+[alpha]
    passes=[(0,0,1,1)] if not interlace else [(0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),(0,2,2,4),(1,0,2,2),(0,1,1,2)]
    result=bytearray()
    for x0,y0,dx,dy in passes:
        xs=list(range(x0,width,dx))
        if not xs:continue
        for y in range(y0,height,dy):
            values=[v for x in xs for v in pixel(x,y)];result.append(0)
            if bits==16:result.extend(struct.pack('>'+'H'*len(values),*values))
            elif bits==8:result.extend(values)
            else:
                for at in range(0,len(values),8//bits):
                    byte=0
                    for i,v in enumerate(values[at:at+8//bits]):byte|=v<<(8-bits*(i+1))
                    result.append(byte)
    return zlib.compress(bytes(result),6)
for kind,bits in [(6,8),(6,16),(2,8),(2,16),(0,1),(0,4),(0,8),(0,16),(3,1),(3,4),(3,8),(4,8),(4,16)]:
 for interlace in [0,1]:
  for poster in [False,True]:
   for disposal in range(3):
    for blend in range(2):
     header=struct.pack('>IIBBBBB',7,5,bits,kind,0,0,interlace)
     data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',header)+chunk(b'acTL',struct.pack('>II',4,2))
     maximum=(1<<bits)-1
     if kind==3:
         count=min(1<<bits,8);data+=chunk(b'PLTE',bytes(v for i in range(count) for v in ((i*37+11)%256,(i*71+19)%256,(i*113+29)%256)))
         data+=chunk(b'tRNS',bytes([0,127]+[255]*(count-2)))+chunk(b'bKGD',bytes([1]))
     elif kind in (0,4):
         data+=chunk(b'bKGD',struct.pack('>H',maximum//3))
         if kind==0:data+=chunk(b'tRNS',struct.pack('>H',maximum))
     else:
         data+=chunk(b'bKGD',struct.pack('>HHH',maximum//3,maximum//5,maximum//7))
         if kind==2:data+=chunk(b'tRNS',b'\x00'*6)
     if poster:data+=chunk(b'IDAT',png_rows(7,5,kind,bits,99,interlace))
     sequence=0
     for frame in range(4):
         width,height,x,y=(7,5,0,0) if frame==0 and not poster else [(4,3,1,1),(5,4,0,0),(4,3,3,2),(3,4,2,0)][frame]
         num,den=[(0,100),(1,1000),(1,0),(5,24)][frame]
         data+=chunk(b'fcTL',struct.pack('>IIIIIHHBB',sequence,width,height,x,y,num,den,(disposal+frame)%3,(blend+frame)%2));sequence+=1
         encoded=png_rows(width,height,kind,bits,frame+3,interlace);parts=[encoded[:len(encoded)//2],encoded[len(encoded)//2:]]
         for part in parts:
             if frame==0 and not poster:data+=chunk(b'IDAT',part)
             else:data+=chunk(b'fdAT',struct.pack('>I',sequence)+part);sequence+=1
     data+=chunk(b'IEND',b'')
     save(f'apng-{kind}-{bits}-i{interlace}-p{int(poster)}-d{disposal}-b{blend}.apng',data)
for bits in [8,16]:
 for mode in [1,3]:
  for channels in ([1,2,4] if mode==1 else [3,4,8]):
   for compression in range(4):
    planes=[];rows=[];predicted=[]
    for channel in range(channels):
     for y in range(5):
      values=[(x*7919+y*373+channel*1031)&((1<<bits)-1) for x in range(7)]
      raw=bytes(values) if bits==8 else struct.pack('>7H',*values);rows.append(raw)
      previous=0;differences=[]
      for v in values:differences.append((v-previous)&((1<<bits)-1));previous=v
      predicted.append(bytes(differences) if bits==8 else struct.pack('>7H',*differences))
    raw=b''.join(rows)
    if compression==0:body=raw
    elif compression==1:
        packed=[b'\x80'+bytes([len(row)-1])+row for row in rows]
        body=b''.join(struct.pack('>H',len(row)) for row in packed)+b''.join(packed)
    else:body=zlib.compress(b''.join(predicted) if compression==3 else raw)
    data=b'8BPS'+struct.pack('>H',1)+b'\x00'*6+struct.pack('>HIIHH',channels,5,7,bits,mode)+b'\x00'*12+struct.pack('>H',compression)+body
    name=f'psd-{bits}-m{mode}-c{channels}-z{compression}.psd';save(name,data)
    if channels in (2,4):save('truncated-'+name,data[:-1])
(out/'manifest.json').write_text(json.dumps({'cases':files,'description':'Synthetic RGBA/gray/palette 1-16bit, Adam7, SOURCE/OVER, all disposal modes, excluded poster; PSD RAW/RLE/ZIP/predictor and extra channels.'},indent=2)+'\n',encoding='utf-8')
print('Generated',len(files),'synthetic cases')
