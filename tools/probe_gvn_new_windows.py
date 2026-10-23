"""Actual owned GVN parent/children: new-window routes or focusedDPI transfer on a private desktop."""
from pathlib import Path
from ctypes import wintypes as w
import ctypes as c
import io,json,shutil,struct,sys,time,uuid,re,argparse
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import benchmark_navigation as native
parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,default=Path('.cache/new-window-processes'));parser.add_argument('--dpi',action='store_true');args=parser.parse_args();dpi_mode=args.dpi
root=Path(__file__).resolve().parents[1];out=args.output.resolve()/('run-'+uuid.uuid4().hex);out.mkdir(parents=True)
(out/'sources').mkdir()
for source in [Path(__file__),Path(native.__file__)]:shutil.copyfile(source,out/'sources'/source.name)
exe=args.executable.resolve()
work=out/'work'/'Folder 日本語 space';work.mkdir(parents=True)
asset=work/'many.parts_日本語 image.png';original=Image.new('RGBA',(3,2));original.putdata([(7,11,13,0),(31,59,61,1),(67,71,73,127),(79,83,89,128),(97,101,103,254),(107,109,113,255)]);original.save(asset,**({'dpi':(144.5,288.25)} if dpi_mode else {}))
with Image.open(asset) as loaded:source_dpi=loaded.info.get('dpi')
other=work/'previous.bmp';shutil.copyfile(root/'tests/fixtures/rgb.bmp',other)
config=out/'settings 日本語.ini';config.write_text('[Viewer]\nallow_multiple=0\nreopen_last=1\nlast_file='+str(other)+'\nconfirm_unsaved=0\nprefetch_enabled=0\ndisplay_mode=1\n',encoding='utf-16')
ids={};value=0
for item in (root/'include/gvn/commands.h').read_text().split('enum Command {',1)[1].split('};',1)[0].split(','):
 m=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
 if not m:continue
 name,number=m.groups()
 if number:value=int(number)
 ids[name]=value;value+=1
assert [ids[n] for n in ['NewWindowFile','NewWindowSend','NewWindowPaste']]==[289,290,291]
commands={name:ids[key] for name,key in [('blank','NewWindow'),('file','NewWindowFile'),('send','NewWindowSend'),('paste','NewWindowPaste')]}
U, K = native.U, native.K
for name, result, args in [
    ('RegisterClipboardFormatW',w.UINT,[w.LPCWSTR]),('GetMenu', native.P, [native.P]), ('GetSubMenu', native.P, [native.P,c.c_int]),
    ('GetMenuItemCount', c.c_int, [native.P]), ('GetMenuItemID', w.UINT, [native.P,c.c_int]),
    ('GetMenuStringW', c.c_int, [native.P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),
    ('GetMenuState', w.UINT, [native.P,w.UINT,w.UINT]), ('IsWindow', w.BOOL, [native.P])]: native.api(U,name,result,args)
native.api(K,'GetPrivateProfileIntW',w.UINT,[w.LPCWSTR,w.LPCWSTR,c.c_int,w.LPCWSTR])
native.api(K,'CreateToolhelp32Snapshot',native.P,[w.DWORD,w.DWORD])
native.api(K,'OpenProcess',native.P,[w.DWORD,w.BOOL,w.DWORD])
native.api(K,'QueryFullProcessImageNameW',w.BOOL,[native.P,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)])
class Entry(c.Structure):
    _fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',native.UP),('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),('flags',w.DWORD),('exe',w.WCHAR*260)]
native.api(K,'Process32FirstW',w.BOOL,[native.P,c.POINTER(Entry)])
native.api(K,'Process32NextW',w.BOOL,[native.P,c.POINTER(Entry)])
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD();native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)))
assert not flags[2]&1
station_name=c.create_unicode_buffer(512);native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-NewWindow-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
parent=native.Child(exe,['--settings',config,asset],station_name.value+'\\'+desktop_name)
children=[]
report=dict(passed=False,scope='Actual owned normalGVN parent and direct children, private station/profile/clipboard. allow_multipleOFF and reopen_lastON; four routes and exactRGBA source identity. No external editor or startup comparison claim.',parent_pid=parent.info.pid,executable_sha256=native.sha(exe),asset_sha256=native.sha(asset),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),commands=commands,cases=[])

def main(child):
    return next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None)

def menu_items(menu):
    result=[]
    for i in range(U.GetMenuItemCount(menu)):
        label=c.create_unicode_buffer(512);U.GetMenuStringW(menu,i,label,512,0x400)
        command=U.GetMenuItemID(menu,i)
        result.append(dict(label=label.value,command=command))
        sub=U.GetSubMenu(menu,i)
        if sub: result+=menu_items(sub)
    return result

def own_new_child():
    snapshot=K.CreateToolhelp32Snapshot(2,0);native.check(snapshot)
    try:
        e=Entry();e.size=c.sizeof(e);ok=K.Process32FirstW(snapshot,c.byref(e));known={x.info.pid for x in children}
        while ok:
            if e.parent==parent.info.pid and e.pid not in known and e.exe.lower()==exe.name.lower():
                handle=K.OpenProcess(0x101001,False,e.pid);native.check(handle)
                path=c.create_unicode_buffer(32768);length=w.DWORD(32768)
                if not K.QueryFullProcessImageNameW(handle,0,path,c.byref(length)) or Path(path.value).resolve()!=exe.resolve():
                    K.CloseHandle(handle);raise AssertionError('Unexpected GVN child executable')
                value=native.Child.__new__(native.Child);value.info=native.Process();value.info.process=handle;value.info.pid=e.pid;value.started=time.perf_counter();children.append(value);return value
            ok=K.Process32NextW(snapshot,c.byref(e))
    finally:K.CloseHandle(snapshot)

def clipboard_copy(child,window,label,expected_dpi=None):
    seq=U.GetClipboardSequenceNumber();child.post(window,0x111,ids['Copy'])
    child.wait(lambda:U.GetClipboardSequenceNumber()!=seq and (U.IsClipboardFormatAvailable(17) or U.IsClipboardFormatAvailable(8)))
    child.wait(lambda:bool(U.OpenClipboard(None)))
    try:
        fmt=17 if U.IsClipboardFormatAvailable(17) else 8;handle=U.GetClipboardData(fmt);p=K.GlobalLock(handle);native.check(p)
        try:data=c.string_at(p,K.GlobalSize(handle))
        finally:K.GlobalUnlock(handle)
        if expected_dpi is not None:
            png=U.RegisterClipboardFormatW('PNG');assert U.IsClipboardFormatAvailable(png)
            png_handle=U.GetClipboardData(png);png_pointer=K.GlobalLock(png_handle);native.check(png_pointer)
            try:png_bytes=c.string_at(png_pointer,K.GlobalSize(png_handle))
            finally:K.GlobalUnlock(png_handle)
            with Image.open(io.BytesIO(png_bytes)) as png_image:
                dpi=png_image.info.get('dpi');assert dpi and all(abs(a-b)<.02 for a,b in zip(dpi,expected_dpi)),(dpi,expected_dpi)
    finally:U.CloseClipboard()
    header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',data)
    assert header in (40,108,124) and planes==1 and bits in (24,32) and compression in (0,3)
    offset=header+(12 if header==40 and compression==3 else 0)+struct.unpack_from('<I',data,32)[0]*4
    assert fmt==17 and bits==32 and header==124
    im=Image.frombytes('RGBA',(width,abs(height)),data[offset:],'raw','BGRA',width*4,-1 if height>0 else 1)
    im.save(out/(label+'.png'));return im

def copied_path(child,window):
    sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,ids['CopyPath'])
    child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(13))
    child.wait(lambda:bool(U.OpenClipboard(None)))
    try:
        handle=U.GetClipboardData(13);pointer=K.GlobalLock(handle);native.check(pointer)
        try:return c.string_at(pointer,K.GlobalSize(handle)).decode('utf-16le').split('\0',1)[0]
        finally:K.GlobalUnlock(handle)
    finally:U.CloseClipboard()

def info_resolution(child,window,expected):
    child.post(window,0x111,ids['Info'])
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=window and U.GetDlgItem(h,306) and U.GetDlgItem(h,1)),None))
    field=child.own(U.GetDlgItem(dialog,306));buffer=c.create_unicode_buffer(1024);child.send(field,0xd,len(buffer),c.addressof(buffer));label=buffer.value
    numbers=re.findall(r'[0-9]+(?:\.[0-9]+)?',label);assert len(numbers)>=2;actual=tuple(map(float,numbers[:2]));assert all(abs(a-b)<.002 for a,b in zip(actual,expected)),(label,expected)
    child.post(dialog,0x111,1);child.wait(lambda:not U.IsWindow(dialog));return dict(text=label,dpi=list(actual),expected=list(expected))

def launch(window,command,label,expected=None,identity=None,dpi=None):
    sequence=U.GetClipboardSequenceNumber();parent.post(window,0x111,command)
    new=parent.wait(own_new_child);hwnd=new.wait(lambda:main(new));new.ping(hwnd)
    if expected is not None: new.wait(lambda:'%' in native.window_text(hwnd))
    case=dict(label=label,command=command,pid=new.info.pid,title=native.window_text(hwnd),clipboard_changed=U.GetClipboardSequenceNumber()!=sequence)
    assert case['clipboard_changed']==(command==commands['send']),case
    if expected is not None:
        actual=clipboard_copy(new,hwnd,label,dpi)
        case.update(size=list(actual.size),expected_size=list(expected.size),pixels_equal=actual.size==expected.size and actual.tobytes()==expected.tobytes())
        assert case['pixels_equal'],case
        if dpi is not None:case['resolution']=info_resolution(new,hwnd,dpi);case['recopied_png_dpi_verified']=True
        case['file_identity']=copied_path(new,hwnd)
        if identity is not None:assert Path(case['file_identity']).resolve()==identity.resolve(),case
        else:assert case['file_identity']=='クリップボード画像',case
    if expected is None:
        time.sleep(.2);new.ping(hwnd);assert native.window_text(hwnd)=='GVN Image Viewer'
        seq=U.GetClipboardSequenceNumber();new.send(hwnd,0x111,ids['Copy']);assert U.GetClipboardSequenceNumber()==seq
        case['blank_after_owned_queue_checks']=True
    new.post(hwnd,0x10);assert K.WaitForSingleObject(new.info.process,10000)==0,'Owned new viewer failed graceful close'
    assert K.GetPrivateProfileIntW('Viewer','allow_multiple',-1,str(config))==0
    assert K.GetPrivateProfileIntW('Viewer','reopen_last',-1,str(config))==1
    new.close()
    new.info.process=None
    case['passed']=True
    case['graceful_close_and_instance_options_preserved']=True
    report['cases'].append(case);print(json.dumps(case,ensure_ascii=False),flush=True)
    return case

try:
    window=parent.wait(lambda:main(parent));parent.wait(lambda:asset.name in native.window_text(window));parent.ping(window)
    if not U.GetMenu(parent.own(window)):parent.send(window,0x111,ids['ToggleMenu'])
    menu=menu_items(U.GetMenu(parent.own(window)));assert all(value in {m['command'] for m in menu} for value in commands.values());report['menu']=menu
    if dpi_mode:
        report['scope']='Actual owned normalGVN parent and2children onprivate station/profile/clipboard. DefinednonuniformDPI withquarterrotation;Send/Paste exactRGBA,childInfoDPI andrecopiedPNG resolution. No userapps/editor/performance claim.'
        assert source_dpi
        assert clipboard_copy(parent,window,'dpi-original',source_dpi).tobytes()==original.tobytes()
        parent.send(window,0x111,ids['RotateRight']);edited=original.transpose(Image.Transpose.ROTATE_270);dpi=(source_dpi[1],source_dpi[0])
        assert clipboard_copy(parent,window,'dpi-edited',dpi).tobytes()==edited.tobytes()
        launch(window,commands['send'],'send-edited-alpha-dpi',edited,dpi=dpi)
        clipboard_copy(parent,window,'dpi-before-paste',dpi)
        launch(window,commands['paste'],'paste-alpha-dpi-without-clipboard-write',edited,dpi=dpi)
        assert clipboard_copy(parent,window,'dpi-parent-after',dpi).tobytes()==edited.tobytes()
        assert Path(copied_path(parent,window)).resolve()==asset.resolve()
    else:
        assert clipboard_copy(parent,window,'original').tobytes()==original.tobytes()
        launch(window,commands['blank'],'empty-multiple-off-reopen-on')
        launch(window,commands['file'],'current-file',original,asset)
        parent.send(window,0x111,ids['RotateRight']);edited=original.transpose(Image.Transpose.ROTATE_270)
        assert clipboard_copy(parent,window,'edited').tobytes()==edited.tobytes()
        launch(window,commands['file'],'edited-opens-original',original,asset)
        launch(window,commands['send'],'send-edited-alpha',edited)
        clipboard_copy(parent,window,'clipboard-before-paste')
        launch(window,commands['paste'],'paste-alpha-without-clipboard-write',edited)
        assert clipboard_copy(parent,window,'parent-after-launches').tobytes()==edited.tobytes()
        assert Path(copied_path(parent,window)).resolve()==asset.resolve()
        parent.send(window,0x111,ids['CloseImage']);launch(window,commands['blank'],'empty-from-empty-multiple-off')
    assert native.sha(asset)==report['asset_sha256']
    assert native.sha(exe)==report['executable_sha256']
    report['passed']=True
finally:
    for child in reversed(children):
        if child.info.process:child.close();child.info.process=None
    parent.close();assert native.sha(__file__)==report['tool_sha256']
    (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Evidence: '+str(out),flush=True)
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
