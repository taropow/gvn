"""Inspect rotation in one probe-owned official0.45 child; private station/profile/clipboard.

No existing app handles, global input, source-file overwrite or startup measurements.
The unchanged, clock-preserving GIF observer captures small generated RGB paints.
"""
import ctypes as c
from ctypes import wintypes as w
import io, json, shutil, struct, subprocess, sys, time, uuid, zipfile
from pathlib import Path
from PIL import Image
import benchmark_navigation as native

phase = sys.argv[1] if len(sys.argv)>1 else 'png-alpha'
assert phase in {'png-alpha','gif','gif-transparent'}
root = Path(__file__).resolve().parents[1]
output = root/'.cache/rotation-formats-reference'/('run-'+uuid.uuid4().hex)
output.mkdir(parents=True)
build_root = root/'.cache/reference-rotation-observer'
build = json.loads((build_root/'build.json').read_text(encoding='utf-8-sig'))
assert not build['clock_replacement']
for name,digest in build['sources'].items(): assert native.sha(root/'tools/reference_rotation'/name) == digest
for name,digest in build['binaries'].items(): assert native.sha(build_root/name) == digest
archive = root/'.cache/massigra-reference/MassiGra045.zip'
assert native.sha(archive) == '76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
reference = output/'reference'
with zipfile.ZipFile(archive) as package:
    for name in package.namelist():
        if name == 'MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
            target = (reference/name).resolve()
            assert target.is_relative_to(reference.resolve())
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(package.read(name))
assert native.sha(reference/'MassiGra.exe') == 'c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
sources = output/'sources'; sources.mkdir()
for source in [Path(__file__),Path(native.__file__),*(root/'tools/reference_rotation').iterdir()]:
    if source.is_file(): shutil.copyfile(source,sources/source.name)
assets = output/'assets'; assets.mkdir()
work = output/'work'; work.mkdir()
fixtures = []
for shape,(width,height) in [('portrait',(5,7)),('landscape',(7,5)),('square',(5,5))]:
    if phase=='png-alpha':
        im=Image.new('RGBA',(width,height))
        alpha=[0,1,64,128,254,255]
        im.putdata([((x*31+y*11+20)%256,(x*13+y*29+40)%256,(x*17+y*19+60)%256,alpha[(y*width+x)%6]) for y in range(height) for x in range(width)])
        im.putpixel((width-1,height-1),(241,19,83,255))
        path=assets/(shape+'.png');im.save(path)
    else:
        im=Image.new('P',(width,height))
        im.putpalette([channel for i in range(256) for channel in ((i*31+20)%256,(i*53+40)%256,(i*97+60)%256)])
        im.putdata([0 if phase=='gif-transparent' and i%5==0 else i+1 for i in range(width*height)])
        im.putpixel((width-1,height-1),240)
        path=assets/(shape+'.gif');im.save(path,optimize=False,**({'transparency':0} if phase=='gif-transparent' else {}))
    fixtures.append((shape,path))

U,K = native.U,native.K
native.api(U,'GetMenu',native.P,[native.P])
native.api(U,'GetSubMenu',native.P,[native.P,c.c_int])
native.api(U,'GetMenuItemCount',c.c_int,[native.P])
native.api(U,'GetMenuItemID',w.UINT,[native.P,c.c_int])
native.api(U,'GetMenuState',w.UINT,[native.P,w.UINT,w.UINT])
native.api(U,'GetMenuStringW',c.c_int,[native.P,w.UINT,w.LPWSTR,c.c_int,w.UINT])
native.api(U,'RedrawWindow',w.BOOL,[native.P,native.P,native.P,w.UINT])
mutex = K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard'); native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station = U.CreateWindowStationW(None,0,0x37f,None); native.check(station)
flags=(w.DWORD*3)(); needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)))
assert not flags[2]&1
station_name=c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Rotation-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None)
native.check(native.desktop); native.check(U.SetThreadDesktop(native.desktop))
working=work/('01.png' if phase=='png-alpha' else '01.gif')
shutil.copyfile(fixtures[0][1],working)
child=native.Child(reference/'MassiGra.exe',[working],station_name.value+'\\'+desktop_name)
report=dict(phase=phase,passed=False,noninteractive=True,controlled_pid=child.info.pid,
            reference_sha256=native.sha(reference/'MassiGra.exe'),tool_sha256=native.sha(__file__),
            helper_sha256=native.sha(native.__file__),observer_build=build,cases=[],
            scope='Generated PNG with partial Alpha, opaque GIF or transparent GIF; all eight display-rotation options on three shapes. Reference copy RGB and observed opaque GDI source pixels; Alpha preservation in GVN is an independent user requirement, not inferred from reference RGB. No animation/OLE or startup-speed claim.')

def rotation_menu(menu,command=41061):
    for i in range(U.GetMenuItemCount(menu)):
        if U.GetMenuItemID(menu,i)==command: return menu
        sub=U.GetSubMenu(menu,i)
        if sub:
            found=rotation_menu(sub,command)
            if found:return found

def menu_state(main):
    menu=rotation_menu(U.GetMenu(child.own(main)))
    assert menu,'Rotation menu unavailable'
    child.send(main,0x116,U.GetMenu(child.own(main)))
    child.send(main,0x117,menu)
    states=[]
    for command in range(41061,41069):
        value=U.GetMenuState(menu,command,0); assert value!=0xffffffff
        label=c.create_unicode_buffer(256); U.GetMenuStringW(menu,command,label,256,0)
        states.append(dict(command=command,label=label.value,checked=bool(value&8),enabled=not bool(value&3)))
    report['last_menu']=states
    assert sum(s['checked'] for s in states)==1,states
    return states

def command_checked(main,command):
    menu=rotation_menu(U.GetMenu(child.own(main)),command); assert menu,command
    child.send(main,0x116,U.GetMenu(child.own(main))); child.send(main,0x117,menu)
    value=U.GetMenuState(menu,command,0); assert value!=0xffffffff
    return bool(value&8)

def copy_pixels(main,label):
    sequence=U.GetClipboardSequenceNumber(); child.post(main,0x111,40025)
    child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and
               (U.IsClipboardFormatAvailable(17) or U.IsClipboardFormatAvailable(8)))
    # The owner changes the sequence before it closes its clipboard handle.
    # Wait for that owned operation to finish; this station has no user clipboard.
    child.wait(lambda:bool(U.OpenClipboard(None)),timeout=3)
    try:
        fmt=17 if U.IsClipboardFormatAvailable(17) else 8
        handle=U.GetClipboardData(fmt); ptr=K.GlobalLock(handle); native.check(ptr)
        try:data=c.string_at(ptr,K.GlobalSize(handle))
        finally:K.GlobalUnlock(handle)
    finally:U.CloseClipboard()
    (output/(label+'-clipboard.bin')).write_bytes(data)
    header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',data)
    assert header in (40,108,124) and width>0 and height and planes==1 and bits in (1,4,8,16,24,32) and compression in (0,3),(header,width,height,planes,bits,compression)
    colors=struct.unpack_from('<I',data,32)[0]
    if bits<=8:colors=colors or 1<<bits
    offset=header+(12 if header==40 and compression==3 else 0)+colors*4
    file_header=struct.pack('<2sIHHI',b'BM',14+len(data),0,0,14+offset)
    im=Image.open(io.BytesIO(file_header+data)).convert('RGB')
    path=output/(label+'.png'); im.save(path)
    return im,dict(file=path.name,sha256=native.sha(path),size=list(im.size),format=fmt,bits=bits,colors=colors,dib_header=header,compression=compression,raw_dib=label+'-clipboard.bin')

def matching(im,source):
    versions={'none':source,'left':source.transpose(Image.Transpose.ROTATE_90),
              'right':source.transpose(Image.Transpose.ROTATE_270),'half':source.transpose(Image.Transpose.ROTATE_180)}
    return [name for name,other in versions.items() if im.size==other.size and im.tobytes()==other.tobytes()]

def paints_since(main,begin,source):
    native.check(U.RedrawWindow(child.own(main),None,None,0x1|0x80|0x100))
    timer=output/'timers.csv'
    lines=timer.read_bytes()[begin:].decode('ascii').splitlines()
    paints=[]
    for line in lines:
        values=line.split(',')
        if len(values)!=6 or values[2]!='6':continue
        path=output/f'snapshot-{int(values[3]):05d}.bmp'
        with Image.open(path) as im:
            im=im.convert('RGB')
            paints.append(dict(file=path.name,sha256=native.sha(path),size=list(im.size),matches=matching(im,source)))
    return paints


def rotated(image,mode):
    portrait=image.width<image.height;landscape=image.width>image.height
    transform=Image.Transpose.ROTATE_90 if mode==1 or mode==3 and portrait or mode==5 and landscape else Image.Transpose.ROTATE_270 if mode==2 or mode==4 and portrait or mode==6 and landscape else Image.Transpose.ROTATE_180 if mode==7 else None
    return image.copy() if transform is None else image.transpose(transform)

def paint_proof(main,begin,baseline,source_rgba,mode):
    paints=paints_since(main,begin,baseline)
    expected=rotated(baseline,mode);mask=rotated(source_rgba,mode).getchannel('A')
    opaque=[i for i,a in enumerate(mask.tobytes()) if a==255]
    for paint in paints:
        actual=Image.open(output/paint['file']).convert('RGB')
        paint['opaque_pixels_match']=actual.size==expected.size and bool(opaque) and all(actual.tobytes()[3*i:3*i+3]==expected.tobytes()[3*i:3*i+3] for i in opaque)
    return paints

try:
    main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
    child.wait(lambda:working.name in native.window_text(main));child.ping(main)
    if not U.GetMenu(child.own(main)):child.send(main,0x111,41008)
    report['initial_menu']=menu_state(main)
    hook=output/'observe.dll';shutil.copyfile(build_root/'observer.dll',hook)
    subprocess.run([str(build_root/'attach_owned.exe'),str(child.info.pid),str(reference/'MassiGra.exe'),str(hook)],check=True,timeout=25)
    report['observer_sha256']=native.sha(hook);timer=output/'timers.csv'
    for shape,path in fixtures:
        source=Image.open(path).convert('RGBA')
        child.send(main,0x111,41061);shutil.copyfile(path,working);child.send(main,0x111,40002)
        baseline,proof=copy_pixels(main,shape+'-baseline')
        assert baseline.size==source.size,(phase,shape,baseline.size,source.size)
        input_alpha=source.getchannel('A').histogram()
        models={'straight_rgb':source.convert('RGB'),'white_composite':Image.alpha_composite(Image.new('RGBA',source.size,'white'),source).convert('RGB'),'black_composite':Image.alpha_composite(Image.new('RGBA',source.size,'black'),source).convert('RGB')}
        errors={name:max(abs(a-b) for a,b in zip(baseline.tobytes(),im.tobytes())) for name,im in models.items()}
        report.setdefault('baselines',[]).append(dict(shape=shape,input=path.name,input_sha256=native.sha(path),copy=proof,transparent=input_alpha[0],partial=sum(input_alpha[1:255]),opaque=input_alpha[255],copy_rgb_model_max_errors=errors))
        for mode in range(8):
            begin=timer.stat().st_size;child.send(main,0x111,41061+mode)
            actual,immediate=copy_pixels(main,f'{shape}-{mode}-immediate')
            expected=rotated(baseline,mode)
            assert actual.size==expected.size and actual.tobytes()==expected.tobytes(),(phase,shape,mode,'immediate')
            paints=paint_proof(main,begin,baseline,source,mode)
            begin=timer.stat().st_size;child.send(main,0x111,40002)
            actual,reloaded=copy_pixels(main,f'{shape}-{mode}-reloaded')
            assert actual.size==expected.size and actual.tobytes()==expected.tobytes(),(phase,shape,mode,'reload')
            reload_paints=paint_proof(main,begin,baseline,source,mode)
            menu=menu_state(main);assert menu[mode]['checked'] and all(item['enabled'] for item in menu)
            assert native.sha(working)==native.sha(path)
            case=dict(shape=shape,mode=mode,input=path.name,input_sha256=native.sha(path),immediate=immediate,reloaded=reloaded,copy_rgb_exact=True,first_paints=paints,reload_paints=reload_paints,menu=menu)
            report['cases'].append(case)
            (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            print(phase,shape,mode,'copy_exact',sum(p['opaque_pixels_match'] for p in paints+reload_paints),'opaque GDI matches',flush=True)
    assert len(report['cases'])==24
    assert native.sha(__file__)==report['tool_sha256']
    report['gdi_opaque_matched_cases']=sum(any(p['opaque_pixels_match'] for p in case['first_paints']+case['reload_paints']) for case in report['cases'])
    report['reference_alpha_preservation_claimed']=False;report['passed']=True
except BaseException as error:
    report['error']=repr(error);raise
finally:
    if K.WaitForSingleObject(child.info.process,0)!=0:native.check(K.TerminateProcess(child.info.process,0))
    report['child_terminal']=K.WaitForSingleObject(child.info.process,3000)==0
    child.close();assert report['child_terminal']
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
    print('Evidence: '+str(output),flush=True)
