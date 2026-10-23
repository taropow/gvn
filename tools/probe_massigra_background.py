"""Inspect comic display settings of a probe-owned official child on a private desktop."""
import ctypes as c
from ctypes import wintypes as w
import json, struct, sys, time, subprocess
from PIL import Image
from pathlib import Path
import shutil
import uuid
import zipfile
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
output = root / '.cache/background-reference' / ('run-' + uuid.uuid4().hex)
output.mkdir(parents=True)
archive = root / '.cache/massigra-reference/MassiGra045.zip'
assert native.sha(archive) == '76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
reference = output / 'reference'
with zipfile.ZipFile(archive) as package:
    for name in package.namelist():
        if name == 'MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
            target = (reference / name).resolve()
            assert target.is_relative_to(reference.resolve())
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(package.read(name))
assert native.sha(reference / 'MassiGra.exe') == 'c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
U, K = native.U, native.K
native.api(U, 'IsWindowVisible', w.BOOL, [native.P])
native.api(U, 'IsWindow', w.BOOL, [native.P])
native.api(U, 'GetWindowLongW', w.LONG, [native.P, c.c_int])
native.api(U, 'GetParent', native.P, [native.P])
native.api(K, 'VirtualAllocEx', native.P, [native.P, native.P, native.UP, w.DWORD, w.DWORD])
native.api(K, 'VirtualFreeEx', w.BOOL, [native.P, native.P, native.UP, w.DWORD])
native.api(K, 'ReadProcessMemory', w.BOOL, [native.P, native.P, native.P, native.UP, native.P])
mutex = K.CreateMutexW(None, False, 'Local\\GVN.NoninteractiveClipboard')
native.check(mutex)
assert K.WaitForSingleObject(mutex, 30000) in (0, 0x80)
station = U.CreateWindowStationW(None, 0, 0x37f, None)
native.check(station)
flags = (w.DWORD * 3)()
needed = w.DWORD()
native.check(U.GetUserObjectInformationW(station, 1, flags, c.sizeof(flags), c.byref(needed)))
assert not flags[2] & 1
station_name = c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station, 2, station_name, c.sizeof(station_name), c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name = 'GVN-Comic-Settings-' + uuid.uuid4().hex
native.desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
native.check(native.desktop)
native.check(U.SetThreadDesktop(native.desktop))
assets = output / 'assets'
assets.mkdir()
method=0
second_height=17
assert 0<=method<=10 and second_height>7
for index, width, height in [(1,7,11),(2,5,second_height)]:
    image=Image.new('RGB',(width,height))
    image.putdata([(200,y,20) if index==1 else (10,y,220) for y in range(height) for x in range(width)])
    image.save(assets / f'{index:02d}.png')
child = native.Child(reference / 'MassiGra.exe', [assets / '01.png'], station_name.value + '\\' + desktop_name)
report = dict(reference_sha256=native.sha(reference / 'MassiGra.exe'),
              tool_sha256=native.sha(__file__), helper_sha256=native.sha(native.__file__),
              noninteractive=True, controlled_pid=child.info.pid)
def descendants(parent):
    result=[]
    @native.Callback
    def add(hwnd,_): result.append(child.own(hwnd));return True
    U.EnumChildWindows(parent,add,0)
    return result

def opened(main):
    child.post(main,0x111,40101)
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and '設定' in native.window_text(h)),None))
    tab=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32'),None))
    remote=K.VirtualAllocEx(child.info.process,None,16,0x3000,4);native.check(remote)
    try:
        native.check(child.send(tab,0x130a,3,remote))
        rect=(c.c_int*4)();native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None))
        x,y=(rect[0]+rect[2])//2,(rect[1]+rect[3])//2
        child.post(tab,0x201,1,x|y<<16);child.post(tab,0x202,0,x|y<<16)
    finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
    child.wait(lambda:child.send(tab,0x130b)==3)
    def comic_page():
        for h in descendants(dialog):
            if U.GetDlgCtrlID(h)==2023 and native.window_text(h,True)=='ComboBox' and child.send(h,0x146)==3:
                first=c.create_unicode_buffer(128);child.send(h,0x148,0,c.addressof(first))
                if '指定色' in first.value:return U.GetParent(h)
    page=child.wait(comic_page)
    return dialog,page

def state(page):
    out={}
    for id in [2002,2023,2024,2025,2053,2063,2093,2094,2095]:
        h=child.own(U.GetDlgItem(page,id))
        value=dict(style_visible=bool(U.GetWindowLongW(h,-16)&0x10000000))
        if id==2002:value['checked']=bool(child.send(h,0xf0))
        if id in [2023,2024,2025]:
            value['index']=child.send(h,0x147);value['choices']=[]
            for i in range(child.send(h,0x146)):
                text=c.create_unicode_buffer(256);child.send(h,0x148,i,c.addressof(text));value['choices'].append(text.value)
        out[str(id)]=value
    return out

def combo(page,id,value):
    h=U.GetDlgItem(page,id);child.send(h,0x14e,value);child.send(page,0x111,id|(1<<16),h)

def copied(main):
    before=U.GetClipboardSequenceNumber();child.post(main,0x111,40025)
    child.wait(lambda:U.GetClipboardSequenceNumber()!=before and (U.IsClipboardFormatAvailable(17) or U.IsClipboardFormatAvailable(8)))
    native.check(U.OpenClipboard(None))
    try:
        h=U.GetClipboardData(17 if U.IsClipboardFormatAvailable(17) else 8);ptr=K.GlobalLock(h);native.check(ptr)
        try:data=c.string_at(ptr,K.GlobalSize(h))
        finally:K.GlobalUnlock(h)
    finally:U.CloseClipboard()
    header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',data)
    assert header in (40,108,124) and planes==1 and bits in (24,32) and compression in (0,3)
    offset=header+(12 if header==40 and compression==3 else 0)+struct.unpack_from('<I',data,32)[0]*4
    stride=((width*bits+31)//32)*4
    return Image.frombytes('RGB',(width,abs(height)),data[offset:],'raw','BGR' if bits==24 else 'BGRX',stride,-1 if height>0 else 1)


native.api(U,'GetDlgCtrlID',c.c_int,[native.P])
native.api(U,'GetDpiForWindow',w.UINT,[native.P])
native.api(U,'IsWindowEnabled',w.BOOL,[native.P])
native.api(U,'RedrawWindow',w.BOOL,[native.P,native.P,native.P,w.UINT])

def control_text(hwnd):
    buffer=c.create_unicode_buffer(1024);child.send(hwnd,0xd,1024,c.addressof(buffer));return buffer.value

def open_background(main):
    child.post(main,0x111,40101)
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and '設定' in native.window_text(h)),None))
    tab=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32'),None))
    remote=K.VirtualAllocEx(child.info.process,None,16,0x3000,4);native.check(remote)
    try:
        native.check(child.send(tab,0x130a,1,remote));rect=(c.c_int*4)()
        native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None))
        x,y=(rect[0]+rect[2])//2,(rect[1]+rect[3])//2
        child.post(tab,0x201,1,x|y<<16);child.post(tab,0x202,0,x|y<<16)
    finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
    child.wait(lambda:child.send(tab,0x130b)==1)
    checkbox=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='Button' and 'タイル' in control_text(h)),None))
    return dialog,child.own(U.GetParent(checkbox))

def controls(parent):
    result=[]
    for hwnd in descendants(parent):
        cls=native.window_text(hwnd,True)
        if cls not in {'Button','Edit','Static','ComboBox','msctls_updown32'}:continue
        rect=(c.c_int*4)();native.check(U.GetWindowRect(child.own(hwnd),rect))
        row=dict(id=U.GetDlgCtrlID(hwnd),cls=cls,text=control_text(hwnd),visible=bool(U.IsWindowVisible(hwnd)),rect=list(rect))
        if cls=='Button':row['checked']=child.send(hwnd,0xf0)
        if cls=='Edit':row['text_limit']=child.send(hwnd,0xd5)
        if cls=='msctls_updown32':row['range16_raw']=child.send(hwnd,0x466)
        if cls=='ComboBox':
            row['selected']=child.send(hwnd,0x147);row['choices']=[]
            for index in range(min(256,child.send(hwnd,0x146))):
                buffer=c.create_unicode_buffer(1024);child.send(hwnd,0x148,index,c.addressof(buffer));row['choices'].append(buffer.value)
        result.append(row)
    return result

def capture_sources(hwnd,label,sequence):
    begin=timer.stat().st_size
    (output/'background-capture.bin').write_bytes(struct.pack('<II',sequence,16))
    native.check(U.RedrawWindow(child.own(hwnd),None,None,0x1|0x80|0x100));child.ping(hwnd)
    deadline=time.monotonic()+.5
    while time.monotonic()<deadline:
        lines=timer.read_bytes()[begin:].decode('ascii').splitlines()
        if any(len(line.split(','))==6 and line.split(',')[2]=='9' for line in lines):break
        time.sleep(.01)
    proofs=[]
    for line in timer.read_bytes()[begin:].decode('ascii').splitlines():
        values=line.split(',')
        if len(values)!=6 or values[2]!='9':continue
        path=output/f'background-{int(values[3]):05d}.bmp'
        with Image.open(path) as opened:
            image=opened.convert('RGB');colors=image.getcolors(image.width*image.height)
        proofs.append(dict(file=path.name,sha256=native.sha(path),size=list(image.size),colors=sorted(colors,reverse=True)[:12]))
    return dict(label=label,sequence=sequence,source_paints=proofs)

def print_owned(hwnd,label):
    g=c.WinDLL('gdi32',use_last_error=True)
    native.api(g,'CreateCompatibleDC',native.P,[native.P]);native.api(g,'DeleteDC',w.BOOL,[native.P])
    native.api(g,'CreateDIBSection',native.P,[native.P,native.P,w.UINT,c.POINTER(native.P),native.P,w.DWORD])
    native.api(g,'SelectObject',native.P,[native.P,native.P]);native.api(g,'DeleteObject',w.BOOL,[native.P])
    rect=(c.c_int*4)();native.check(U.GetWindowRect(child.own(hwnd),rect));width,height=rect[2]-rect[0],rect[3]-rect[1]
    assert 0<width<=1600 and 0<height<=1200
    header=c.create_string_buffer(struct.pack('<IiiHHIIIIII',40,width,-height,1,32,0,width*height*4,0,0,0,0))
    memory=g.CreateCompatibleDC(None);native.check(memory);bits=native.P()
    bitmap=g.CreateDIBSection(memory,header,0,c.byref(bits),None,0);native.check(bitmap)
    previous=g.SelectObject(memory,bitmap)
    try:
        returned=bool(U.PrintWindow(child.own(hwnd),memory,2))
        image=Image.frombytes('RGB',(width,height),c.string_at(bits,width*height*4),'raw','BGRX',width*4,1)
        path=output/(label+'.png');image.save(path)
        return dict(file=path.name,sha256=native.sha(path),returned=returned,colors=len(image.getcolors(width*height) or []),diagnostic_only=True)
    finally:g.SelectObject(memory,previous);g.DeleteObject(bitmap);g.DeleteDC(memory)

def in_process_capture(hwnd,label,sequence,modes=(1,2,3,4),roi=None):
    captures=[]
    client=(c.c_int*4)();native.check(U.GetClientRect(child.own(hwnd),client))
    for mode in modes:
        values=(child.own(hwnd),sequence,mode)
        if roi is not None:
            assert mode==4 and len(roi)==4
            x,y,width,height=roi
            assert x>=0 and y>=0 and 0<width<=1600 and 0<height<=1200 and x+width<=client[2] and y+height<=client[3]
            values+=tuple(roi)
        (output/'capture-owned.bin').write_bytes(struct.pack('<'+'I'*len(values),*values))
        subprocess.run([str(build_root/'attach_owned.exe'),str(child.info.pid),str(reference/'MassiGra.exe'),str(hook),'capture'],check=True,timeout=25)
        path=output/f'owned-{sequence:05d}-mode{mode}.bmp'
        with Image.open(path) as opened:
            image=opened.convert('RGB');colors=image.getcolors(image.width*image.height)
        assert list(image.size)==([client[2],client[3]] if roi is None else list(roi[2:]))
        captures.append(dict(file=path.name,sha256=native.sha(path),mode=mode,size=list(image.size),actual_client=list(client),roi=list(roi) if roi is not None else None,colors=len(colors or []),dominant=sorted(colors or [],reverse=True)[:12],scope='Process-local memory DC; requested own client painting, not physical desktop presentation.'))
    return dict(label=label,captures=captures)

def choose_color(dialog,page,button,rgb=None):
    child.post(U.GetDlgItem(page,button),0xf5)
    picker=child.wait(lambda:next((h for h in child.windows() if h!=dialog and native.window_text(h,True)=='#32770' and U.GetDlgItem(h,706)),None))
    report['last_picker_controls']=controls(picker)
    if not all(control_text(child.own(U.GetDlgItem(picker,706+index))) for index in range(3)):
        expand=next((h for h in descendants(picker) if native.window_text(h,True)=='Button' and '作成' in control_text(h)),None)
        if expand and U.IsWindowEnabled(expand):child.post(expand,0xf5)
        child.wait(lambda:all(control_text(child.own(U.GetDlgItem(picker,706+index))) for index in range(3)),timeout=3)
    if rgb is not None:
        for index,value in enumerate(rgb):
            edit=child.own(U.GetDlgItem(picker,706+index));text=c.create_unicode_buffer(str(value))
            child.send(edit,0xc,0,c.addressof(text));child.send(picker,0x111,(706+index)|(0x300<<16),edit)
    values=[int(control_text(child.own(U.GetDlgItem(picker,706+index)))) for index in range(3)]
    if rgb is not None:assert values==list(rgb),values
    child.post(U.GetDlgItem(picker,1 if rgb is not None else 2),0xf5);child.wait(lambda:not U.IsWindow(picker))
    return values

def check_background(proof,colors,cell,tiled):
    with Image.open(output/proof['file']) as opened:image=opened.convert('RGB')
    original=Image.open(assets/'01.png').convert('RGB')
    foreground=set(original.getdata());actual=list(image.getdata())
    points=[(index%image.width,index//image.width) for index,value in enumerate(actual) if value in foreground]
    assert len(points)==original.width*original.height,('foreground count',len(points),proof)
    left=min(x for x,y in points);top=min(y for x,y in points)
    for y in range(original.height):
        for x in range(original.width):assert image.getpixel((left+x,top+y))==original.getpixel((x,y))
    if tiled and cell is None:
        cell=next((x for x in range(1,image.width) if image.getpixel((x,0))!=tuple(colors[0])),None)
        assert cell and image.getpixel((cell,0))==tuple(colors[1]),('size not observed',proof)
    count=0
    for y in range(image.height):
        for x in range(image.width):
            if left<=x<left+original.width and top<=y<top+original.height:continue
            expected=tuple(colors[((x//cell+y//cell)&1) if tiled else 0])
            assert image.getpixel((x,y))==expected,('background',x,y,cell,tiled,image.getpixel((x,y)),expected)
            count+=1
    return dict(cell=cell,background_pixels=count,source_rect=[left,top,original.width,original.height],all_rgb_exact=True)

def measure_background(main,viewer):
    report['measure_cases']=[];sizes={};sequence=1000
    palettes=[[(32,32,32),(48,48,48)],[(23,45,67),(89,101,113)]]
    dialog,page=open_background(main)
    report['initial_picker_rgb']=[choose_color(dialog,page,2062),choose_color(dialog,page,2063)]
    assert report['initial_picker_rgb']==[[32,32,32],[48,48,48]]
    child.post(U.GetDlgItem(dialog,2),0xf5);child.wait(lambda:not U.IsWindow(dialog))
    for palette_index,colors in enumerate(palettes):
        for preset in range(5):
            for tiled in (True,False):
                dialog,page=open_background(main)
                if preset==0 and tiled:
                    choose_color(dialog,page,2062,colors[0]);choose_color(dialog,page,2063,colors[1])
                check=child.own(U.GetDlgItem(page,2001))
                if bool(child.send(check,0xf0))!=tiled:child.send(check,0xf5)
                combo(page,2021,preset)
                state=dict(tiled=bool(child.send(check,0xf0)),preset=child.send(U.GetDlgItem(page,2021),0x147),
                           size_enabled=bool(U.IsWindowEnabled(U.GetDlgItem(page,2021))),color2_enabled=bool(U.IsWindowEnabled(U.GetDlgItem(page,2063))))
                assert state['tiled']==tiled and state['preset']==preset
                child.post(U.GetDlgItem(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)
                sequence+=1;immediate=in_process_capture(viewer,'immediate',sequence,(4,))['captures'][0]
                first=check_background(immediate,colors,sizes.get(preset),tiled)
                if tiled:
                    if preset in sizes:assert first['cell']==sizes[preset]
                    else:sizes[preset]=first['cell']
                child.send(main,0x111,40002)
                sequence+=1;reloaded=in_process_capture(viewer,'reloaded',sequence,(4,))['captures'][0]
                second=check_background(reloaded,colors,sizes.get(preset),tiled)
                assert first==second
                report['measure_cases'].append(dict(palette=palette_index,colors=colors,preset=preset,tiled=tiled,state=state,
                                                     immediate=immediate,reloaded=reloaded,pixel_proof=second))
                (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                print('PASS background',palette_index,preset,tiled,'cell',sizes.get(preset),flush=True)
    report['preset_cells']=sizes
    dialog,page=open_background(main);child.send(U.GetDlgItem(page,2061),0xf5);child.ping(dialog)
    assert child.send(U.GetDlgItem(page,2001),0xf0)==1 and child.send(U.GetDlgItem(page,2021),0x147)==2
    report['reset_picker_rgb']=[choose_color(dialog,page,2062),choose_color(dialog,page,2063)]
    assert report['reset_picker_rgb']==[[32,32,32],[48,48,48]]
    report['preview_capture']=in_process_capture(child.own(U.GetDlgItem(page,2051)),'preview-default',2000,(4,))
    child.post(U.GetDlgItem(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)
    restored=in_process_capture(viewer,'after-reset',2001,(4,))['captures'][0]
    report['reset_pixel_proof']=check_background(restored,palettes[0],sizes[2],True);report['reset_capture']=restored
    assert len(report['measure_cases'])==20

def measure_contexts(main):
    import re
    report['context_cases']=[];sequence=3000
    def viewer():
        return child.wait(lambda:next((h for h in descendants(main) if native.window_text(h,True)=='TF811202_MassiGra_MaCl'),None))
    def size():
        rect=(c.c_int*4)();native.check(U.GetClientRect(viewer(),rect));return rect[2],rect[3]
    def capture(label,colors,cell,tiled,roi=None,expected_source=None):
        nonlocal sequence
        sequence+=1;proof=in_process_capture(viewer(),label,sequence,(4,),roi)['captures'][0]
        image=Image.open(output/proof['file']).convert('RGB')
        data=list(image.getdata());width,height=image.size
        points=[(i%width,i//width) for i,p in enumerate(data) if p[0]==200 and p[2]==20 and 0<=p[1]<=10]
        box=None
        if points:
            left=min(p[0] for p in points);right=max(p[0] for p in points)+1
            top=min(p[1] for p in points);bottom=max(p[1] for p in points)+1
            box=[left,top,right-left,bottom-top]
            assert len(points)==(right-left)*(bottom-top),('source rectangle has holes',label)
        if expected_source is not None:assert len(points)==expected_source,(label,len(points),expected_source)
        origin=roi[:2] if roi else (0,0)
        count=0
        for y in range(height):
            for x in range(width):
                if box and box[0]<=x<box[0]+box[2] and box[1]<=y<box[1]+box[3]:continue
                expected=tuple(colors[(((x+origin[0])//cell+(y+origin[1])//cell)&1) if tiled else 0])
                actual=data[y*width+x]
                assert actual==expected,('context background',label,x,y,origin,actual,expected,proof['file'])
                count+=1
        assert count>0
        row=dict(label=label,title=native.window_text(main),colors=colors,cell=cell,tiled=tiled,capture=proof,
                 source_rect=box,source_pixels=len(points),background_pixels=count,all_background_rgb_exact=True)
        report['context_cases'].append(row)
        (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('PASS context',label,'client',proof['actual_client'],'roi',roi,'source',box,flush=True)
        return row
    def reopen():
        child.post(main,0x111,40001)
        dialog=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='#32770' and '開く' in native.window_text(h)),None))
        field=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='Edit' and U.GetDlgCtrlID(h)==1152),None))
        text=c.create_unicode_buffer(str(assets/'01.png'));child.send(field,0xc,0,c.addressof(text))
        child.post(dialog,0x111,1);child.wait(lambda:not U.IsWindow(dialog))
        child.wait(lambda:'01.png' in native.window_text(main));child.ping(main)
    def percent():
        found=re.search(r'([0-9.]+)\s*%',native.window_text(main))
        assert found,('no zoom percentage',native.window_text(main))
        return float(found.group(1))
    conditions=[('default',[(32,32,32),(48,48,48)],2,True),
                ('custom-small',[(23,45,67),(89,101,113)],0,True),
                ('custom-large',[(23,45,67),(89,101,113)],4,True),
                ('custom-solid',[(23,45,67),(89,101,113)],4,False)]
    for index,(name,colors,preset,tiled) in enumerate(conditions):
        if index:reopen()
        native.check(U.SetWindowPos(child.own(main),None,0,0,640,480,0x4|0x10))
        dialog,page=open_background(main)
        check=child.own(U.GetDlgItem(page,2001))
        if not child.send(check,0xf0):child.send(check,0xf5)
        choose_color(dialog,page,2062,colors[0]);choose_color(dialog,page,2063,colors[1]);combo(page,2021,preset)
        if not tiled:child.send(check,0xf5)
        child.post(U.GetDlgItem(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)
        cell=[8,16,32,64,128][preset]
        child.send(main,0x111,40086)
        capture(name+'-actual',colors,cell,tiled,expected_source=77)
        child.send(main,0x111,40082)
        zoomed=capture(name+'-zoom',colors,cell,tiled)
        assert zoomed['source_pixels']>77
        for attempt in range(40):
            if percent()>=4000:break
            child.send(main,0x111,40082)
        assert 4000<=percent()<=10000,('unbounded zoom',native.window_text(main))
        before=capture(name+'-before-pan',colors,cell,tiled)
        child.send(main,0x111,45092)
        after=capture(name+'-after-pan',colors,cell,tiled)
        assert before['capture']['sha256']!=after['capture']['sha256'],('scroll command had no effect',name)
        child.send(main,0x111,40086);child.send(main,0x111,40081);child.ping(main)
        width,height=size();assert width>0 and height>0
        rw,rh=min(620,width),min(387,height)
        rois=[(0,0,rw,rh),((width-rw)//2,(height-rh)//2,rw,rh),(width-rw,height-rh,rw,rh)]
        capture(name+'-fullscreen-entry-fit',colors,cell,tiled,rois[1])
        child.send(main,0x111,40086);child.ping(main)
        for region,roi in enumerate(rois):
            capture(name+'-fullscreen-'+str(region),colors,cell,tiled,roi,77 if region==1 else 0)
        child.send(main,0x111,40030);child.ping(main)
        for region,roi in enumerate(rois):capture(name+'-fullscreen-empty-'+str(region),colors,cell,tiled,roi,0)
        child.send(main,0x111,40081);child.ping(main)
        capture(name+'-empty-restored',colors,cell,tiled,expected_source=0)
        native.check(U.SetWindowPos(child.own(main),None,0,0,357,299,0x4|0x10))
        capture(name+'-empty-resized',colors,cell,tiled,expected_source=0)
    assert len(report['context_cases'])==52

try:
    main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
    child.wait(lambda:'01.png' in native.window_text(main));child.ping(main)
    native.check(U.SetWindowPos(child.own(main),None,0,0,640,480,0x4|0x10))
    build_root=root/'.cache/reference-background-observer'
    build=json.loads((build_root/'build.json').read_text(encoding='utf-8-sig'));assert not build['clock_replacement']
    for name,digest in build['sources'].items():assert native.sha(root/'tools/reference_background'/name)==digest
    for name,digest in build['binaries'].items():assert native.sha(build_root/name)==digest
    sources=output/'sources';sources.mkdir()
    for source in [Path(__file__),Path(native.__file__),*(root/'tools/reference_background').iterdir()]:
        if source.is_file():shutil.copyfile(source,sources/source.name)
    report.update(passed=False,tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),observer_build=build,dpi=U.GetDpiForWindow(main),
                  scope='Probe-owned official0.45 process/private station, generated images, actual Background controls and bounded requested GDI memory-DC source captures. PrintWindow is diagnostic only; no global input, user profile/clipboard or startup timing.')
    hook=output/'observe.dll';shutil.copyfile(build_root/'observer.dll',hook)
    subprocess.run([str(build_root/'attach_owned.exe'),str(child.info.pid),str(reference/'MassiGra.exe'),str(hook)],check=True,timeout=25)
    timer=output/'timers.csv'
    report['main_default']=capture_sources(main,'main-default',1)
    report['main_print_diagnostic']=print_owned(main,'main-default-diagnostic')
    report['main_in_process']=in_process_capture(main,'main-default',100)
    report['main_children']=[]
    for index,hwnd in enumerate(descendants(main)):
        rect=(c.c_int*4)();native.check(U.GetWindowRect(child.own(hwnd),rect))
        row=dict(hwnd=hwnd,cls=native.window_text(hwnd,True),text=native.window_text(hwnd),rect=list(rect))
        if rect[2]-rect[0]>=200 and rect[3]-rect[1]>=150:
            row['painting']=in_process_capture(hwnd,'main-child',300+index)
        report['main_children'].append(row)
    dialog,page=open_background(main)
    report['controls']=controls(page)
    report['dialog_default']=capture_sources(dialog,'dialog-default',2)
    report['dialog_print_diagnostic']=print_owned(dialog,'background-dialog-diagnostic')
    report['dialog_in_process']=in_process_capture(dialog,'dialog-default',200)
    child.post(U.GetDlgItem(dialog,2),0xf5);child.wait(lambda:not U.IsWindow(dialog))
    report['source_captures']=len(report['main_default']['source_paints'])+len(report['dialog_default']['source_paints'])
    phase=sys.argv[1] if len(sys.argv)>1 else 'inspect';assert phase in {'inspect','measure','contexts'}
    report['phase']=phase
    if phase=='measure':
        viewer=child.wait(lambda:next((h for h in descendants(main) if native.window_text(h,True)=='TF811202_MassiGra_MaCl'),None))
        measure_background(main,viewer)
    if phase=='contexts':measure_contexts(main)
    report['passed']=True
    print(json.dumps(dict(dpi=report['dpi'],controls=len(report['controls']),source_captures=report['source_captures']),ensure_ascii=False),flush=True)
except BaseException as error:
    report['error']=repr(error);raise
finally:
    if K.WaitForSingleObject(child.info.process,0)!=0:native.check(K.TerminateProcess(child.info.process,0))
    report['child_terminal']=K.WaitForSingleObject(child.info.process,3000)==0
    child.close();assert report['child_terminal']
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
    print('Evidence: '+str(output),flush=True)
