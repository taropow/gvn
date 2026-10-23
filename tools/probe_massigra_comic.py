"""Inspect comic display settings of a probe-owned official child on a private desktop."""
import ctypes as c
from ctypes import wintypes as w
import json, struct, sys
from PIL import Image
from pathlib import Path
import shutil
import uuid
import zipfile
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
output = root / '.cache/comic-reference' / ('run-' + uuid.uuid4().hex)
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
method=int(sys.argv[1]) if len(sys.argv)>1 else 0
second_height=int(sys.argv[2]) if len(sys.argv)>2 else 17
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

try:
    main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
    child.wait(lambda:'01.png' in native.window_text(main))
    report['cases']=[]
    report['method']=method;report['second_height']=second_height
    for mode in range(3):
        dialog,page=opened(main)
        if mode==0:report['initial']=state(page)
        combo(page,2023,mode);report['cases'].append(dict(mode=mode,state=state(page)))
        combo(page,2024,method);combo(page,2025,method)
        child.post(U.GetDlgItem(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog))
        if mode==0:child.send(main,0x111,40088)
        else:child.send(main,0x111,40002)
        image=copied(main);image.save(output/f'mode-{mode}.png')
        report['cases'][-1].update(size=list(image.size),red_bounds=[min((x for y in range(image.height) for x in range(image.width) if image.getpixel((x,y))[0]==200),default=-1),max((x for y in range(image.height) for x in range(image.width) if image.getpixel((x,y))[0]==200),default=-1)],passed=True)
    report['passed']=True
finally:
    child.close()
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
    print(output)
