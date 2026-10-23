"""Inspect rotation in one probe-owned official0.45 child; private station/profile/clipboard.

No existing app handles, global input, source-file overwrite or startup measurements.
The unchanged, clock-preserving GIF observer captures small generated RGB paints.
"""
import ctypes as c
from ctypes import wintypes as w
import json, shutil, struct, subprocess, sys, time, uuid, zipfile
from pathlib import Path
from PIL import Image
import benchmark_navigation as native

phase = sys.argv[1] if len(sys.argv)>1 else 'single'
assert phase in {'single','comic','comic-wide','edit','edit-repeat','exif','save','selection-inspect','selection'}
root = Path(__file__).resolve().parents[1]
output = root/'.cache/rotation-reference'/('run-'+uuid.uuid4().hex)
output.mkdir(parents=True)
build_root = root/'.cache/reference-gif-observer'
build = json.loads((build_root/'build.json').read_text(encoding='utf-8-sig'))
assert not build['clock_replacement']
for name,digest in build['sources'].items(): assert native.sha(root/'tools/reference_gif'/name) == digest
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
for source in [Path(__file__),Path(native.__file__),*(root/'tools/reference_gif').iterdir()]:
    if source.is_file(): shutil.copyfile(source,sources/source.name)
assets = output/'assets'; assets.mkdir()
work = output/'work'; work.mkdir()
fixtures = []
for shape,(width,height) in [('portrait',(5,7)),('landscape',(7,5)),('square',(5,5))]:
    im = Image.new('RGB',(width,height))
    im.putdata([((x*31+y*11+20)%256,(x*13+y*29+40)%256,(x*17+y*19+60)%256)
                for y in range(height) for x in range(width)])
    path = assets/(shape+'.png'); im.save(path)
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
working=work/('01.jpg' if phase=='exif' else '01.png')
shutil.copyfile(root/'tests/fixtures/loading/exif-1.jpg' if phase=='exif' else fixtures[0][1],working)
if phase.startswith('comic'): shutil.copyfile(fixtures[0][1],work/'02.png')
child=native.Child(reference/'MassiGra.exe',[working],station_name.value+'\\'+desktop_name)
report=dict(phase=phase,passed=False,noninteractive=True,controlled_pid=child.info.pid,
            reference_sha256=native.sha(reference/'MassiGra.exe'),tool_sha256=native.sha(__file__),
            helper_sha256=native.sha(native.__file__),observer_build=build,cases=[],
            scope='Generated small opaque PNGs. Observed real menu commands, clipboard copy and RGB GDI source paints; no alpha, save, animation, comic or Exif completion claim.')

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
    header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',data)
    assert header in (40,108,124) and width>0 and height and planes==1 and bits in (24,32) and compression in (0,3)
    offset=header+(12 if header==40 and compression==3 else 0)+struct.unpack_from('<I',data,32)[0]*4
    stride=((width*bits+31)//32)*4
    im=Image.frombytes('RGB',(width,abs(height)),data[offset:],'raw','BGR' if bits==24 else 'BGRX',stride,-1 if height>0 else 1)
    path=output/(label+'.png'); im.save(path)
    return im,dict(file=path.name,sha256=native.sha(path),size=list(im.size),format=fmt,bits=bits)

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


def descendants(parent):
    result=[]
    @native.Callback
    def add(hwnd,_): result.append(child.own(hwnd)); return True
    U.EnumChildWindows(parent,add,0)
    return result

def configure_comic(main,before,mode):
    native.api(U,'IsWindow',w.BOOL,[native.P])
    native.api(U,'GetParent',native.P,[native.P])
    native.api(K,'VirtualAllocEx',native.P,[native.P,native.P,native.UP,w.DWORD,w.DWORD])
    native.api(K,'VirtualFreeEx',w.BOOL,[native.P,native.P,native.UP,w.DWORD])
    native.api(K,'ReadProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P])
    child.post(main,0x111,40101)
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and '設定' in native.window_text(h)),None))
    tab=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32'),None))
    remote=K.VirtualAllocEx(child.info.process,None,16,0x3000,4); native.check(remote)
    try:
        native.check(child.send(tab,0x130a,3,remote))
        rect=(c.c_int*4)(); native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None))
        x,y=(rect[0]+rect[2])//2,(rect[1]+rect[3])//2
        child.post(tab,0x201,1,x|y<<16); child.post(tab,0x202,0,x|y<<16)
    finally: K.VirtualFreeEx(child.info.process,remote,0,0x8000)
    child.wait(lambda:child.send(tab,0x130b)==3)
    page=child.wait(lambda:next((U.GetParent(h) for h in descendants(dialog)
        if U.GetDlgCtrlID(h)==2023 and native.window_text(h,True)=='ComboBox' and child.send(h,0x146)==3),None))
    box=child.own(U.GetDlgItem(page,2002))
    if 'default_before_comic' not in report:report['default_before_comic']=bool(child.send(box,0xf0))
    if bool(child.send(box,0xf0))!=before:child.send(box,0xf5)
    assert bool(child.send(box,0xf0))==before
    for cid,value in [(2023,mode),(2024,0),(2025,0)]:
        h=child.own(U.GetDlgItem(page,cid)); assert child.send(h,0x14e,value)==value
        child.send(page,0x111,cid|(1<<16),h); assert child.send(h,0x147)==value
    child.post(U.GetDlgItem(dialog,1),0xf5); child.wait(lambda:not U.IsWindow(dialog))

def comic_cases(main,timer):
    pairs=[]
    for name,sizes in [('PP',[(5,7),(3,9)]),('LL',[(7,5),(9,3)]),('PL',[(5,7),(7,5)]),('SS',[(5,5),(3,3)])]:
        paths=[]
        for index,(width,height) in enumerate(sizes):
            im=Image.new('RGB',(width,height))
            im.putdata([((x*31+y*11+20+index*83)%256,(x*13+y*29+40+index*91)%256,(x*17+y*19+60+index*53)%256)
                        for y in range(height) for x in range(width)])
            path=assets/f'{name}-{index+1}.png'; im.save(path); paths.append(path)
        pairs.append((name,paths))
    shutil.copyfile(pairs[0][1][1],work/'02.png')
    report['default_wide_single']=command_checked(main,41085)
    assert not report['default_wide_single']
    if phase=='comic-wide':child.send(main,0x111,41085)
    assert command_checked(main,41085)==(phase=='comic-wide')
    child.send(main,0x111,40088)
    assert command_checked(main,40088)
    configure_comic(main,False,0)
    child.send(main,0x111,41084); child.send(main,0x111,40002)
    initial,proof=copy_pixels(main,'comic-precondition')
    report['comic_precondition']=proof
    assert initial.size==(8,9),('Two generated portrait files must actually compose before collecting cases',initial.size)
    for before in (False,True):
        for height_mode in range(3):
            configure_comic(main,before,height_mode)
            for rtl in (False,True):
                child.send(main,0x111,41083 if rtl else 41084)
                for pair,paths in pairs:
                    child.send(main,0x111,41061)
                    for index,path in enumerate(paths):shutil.copyfile(path,work/f'{index+1:02d}.png')
                    child.send(main,0x111,40002)
                    source=Image.open(paths[0]).convert('RGB')
                    for mode in range(8):
                        begin=timer.stat().st_size
                        child.send(main,0x111,41061+mode); child.send(main,0x111,40002)
                        label=f'{pair}-before{int(before)}-height{height_mode}-rtl{int(rtl)}-rotation{mode}'
                        im,proof=copy_pixels(main,label)
                        state=menu_state(main); assert state[mode]['checked']
                        assert all(native.sha(work/f'{i+1:02d}.png')==native.sha(p) for i,p in enumerate(paths))
                        report['cases'].append(dict(pair=pair,before=before,height_mode=height_mode,rtl=rtl,mode=mode,
                            assets=[dict(file=p.name,sha256=native.sha(p)) for p in paths],copy=proof,
                            paints=paints_since(main,begin,source)))
                print('comic',before,height_mode,rtl,'cases',len(report['cases']),flush=True)
                (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    assert len(report['cases'])==384
    report['scope']='Generated opaque comic pairs:2rotation orders x3height modes x2reading directions x4shape pairs x8rotation modes. Copy and source GDI RGB; no alpha/Exif/save/animation completion claim.'

def edit_cases(main,timer):
    native.api(U,'IsWindowEnabled',w.BOOL,[native.P])
    native.api(U,'IsWindow',w.BOOL,[native.P])
    source=Image.open(fixtures[0][1]).convert('RGB')
    def action(command):
        child.post(main,0x111,command)
        time.sleep(.12)
        dialogs=[]
        for dialog in child.windows():
            if native.window_text(dialog,True)!='#32770':continue
            texts=[native.window_text(h) for h in descendants(dialog)]
            dialogs.append(dict(title=native.window_text(dialog),texts=texts))
            no=U.GetDlgItem(dialog,7)
            assert no and any('保存' in t for t in texts),('Unexpected owned modal',texts)
            child.post(no,0xf5)
            child.wait(lambda:not U.IsWindow(dialog))
        child.wait(lambda:U.IsWindowEnabled(child.own(main)))
        return dialogs
    def record(label,command):
        begin=timer.stat().st_size
        dialogs=action(command) if command else []
        image,proof=copy_pixels(main,label)
        case=dict(label=label,command=command,copy=proof,matches=matching(image,source),
                  dialogs=dialogs,paints=paints_since(main,begin,source),title=native.window_text(main))
        report['cases'].append(case)
        print(label,case['matches'],'dialogs',len(dialogs),flush=True)
        return image
    child.send(main,0x111,41061);child.send(main,0x111,40002)
    if phase=='edit-repeat':
        record('repeat-original',0)
        record('repeat-edited-right',40602)
        record('repeat-same-none',41061)
        record('repeat-change-left',41062)
        record('repeat-left-then-edit-right',40602)
        record('repeat-same-left',41062)
        assert native.sha(working)==native.sha(fixtures[0][1])
        report['scope']='Generated opaque PNG: selecting the current view-rotation option after editing. RGB orientation only; no alpha/save/animation claim.'
        return
    record('original',0)
    edited=record('edited-right',40602)
    assert matching(edited,source)==['right']
    record('edited-change-view-left',41062)
    record('undo-after-view-change',40021)
    child.send(main,0x111,41061);child.send(main,0x111,40002)
    record('clipboard-payload-original',0)
    action(41062) # Keep the original private clipboard payload; do not copy it again.
    record('new-paste-with-left-option',40029)
    record('pasted-change-view-right',41063)
    record('undo-after-pasted-view-change',40021)
    assert native.sha(working)==native.sha(fixtures[0][1])
    report['scope']='Generated opaque source: editing, view-option changes, Undo and private clipboard NewPaste. RGB orientation only; no user data, alpha or save completion claim.'

def configure_exif(main,enabled):
    native.api(U,'IsWindow',w.BOOL,[native.P])
    native.api(K,'VirtualAllocEx',native.P,[native.P,native.P,native.UP,w.DWORD,w.DWORD])
    native.api(K,'VirtualFreeEx',w.BOOL,[native.P,native.P,native.UP,w.DWORD])
    native.api(K,'ReadProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P])
    child.post(main,0x111,40101)
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and '設定' in native.window_text(h)),None))
    tab=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32'),None))
    remote=K.VirtualAllocEx(child.info.process,None,16,0x3000,4);native.check(remote)
    try:
        native.check(child.send(tab,0x130a,9,remote))
        rect=(c.c_int*4)();native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None))
        x,y=(rect[0]+rect[2])//2,(rect[1]+rect[3])//2
        child.post(tab,0x201,1,x|y<<16);child.post(tab,0x202,0,x|y<<16)
    finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
    child.wait(lambda:child.send(tab,0x130b)==9)
    box=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='Button' and 'Exif' in native.window_text(h)),None))
    if 'default_exif' not in report:report['default_exif']=bool(child.send(box,0xf0))
    report['exif_control']=dict(id=U.GetDlgCtrlID(box),label=native.window_text(box))
    if bool(child.send(box,0xf0))!=enabled:child.send(box,0xf5)
    assert bool(child.send(box,0xf0))==enabled
    child.post(U.GetDlgItem(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog))

def exif_cases(main,timer):
    raw={}
    operations={2:Image.Transpose.FLIP_LEFT_RIGHT,3:Image.Transpose.ROTATE_180,
                4:Image.Transpose.FLIP_TOP_BOTTOM,5:Image.Transpose.TRANSPOSE,
                6:Image.Transpose.ROTATE_270,7:Image.Transpose.TRANSVERSE,8:Image.Transpose.ROTATE_90}
    for enabled in [False,True]:
        configure_exif(main,enabled)
        for orientation in range(1,9):
            path=assets/f'exif-{orientation}.jpg'
            if not path.exists():shutil.copyfile(root/'tests/fixtures/loading'/path.name,path)
            child.send(main,0x111,41061)
            shutil.copyfile(path,working);child.send(main,0x111,40002)
            baseline,proof=copy_pixels(main,f'exif-{int(enabled)}-{orientation}-baseline')
            if not enabled:raw[orientation]=baseline.copy()
            source=raw[orientation]
            if enabled and orientation in operations:source=source.transpose(operations[orientation])
            for mode in range(8):
                begin=timer.stat().st_size;child.send(main,0x111,41061+mode)
                actual,copy=copy_pixels(main,f'exif-{int(enabled)}-{orientation}-{mode}')
                turn=mode if mode in (0,1,2,7) else (1 if mode in (3,5) else 2)
                if mode in (3,4) and not source.width<source.height:turn=0
                if mode in (5,6) and not source.width>source.height:turn=0
                expected=source if turn==0 else source.transpose({1:Image.Transpose.ROTATE_90,2:Image.Transpose.ROTATE_270,7:Image.Transpose.ROTATE_180}[turn])
                report['cases'].append(dict(enabled=enabled,orientation=orientation,mode=mode,asset=path.name,
                    asset_sha256=native.sha(path),copy=copy,baseline=proof,
                    matches_exif_then_view=actual.size==expected.size and actual.tobytes()==expected.tobytes(),
                    paints=paints_since(main,begin,expected)))
                assert native.sha(path)==native.sha(working),'Source JPEG changed'
            (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            print('exif',enabled,orientation,'matches',sum(c['matches_exif_then_view'] for c in report['cases'][-8:]),'/8',flush=True)
    assert len(report['cases'])==128
    report['scope']='Generated JPEG Exif tags1..8, actual Exif checkbox OFF/ON and eight view-rotation choices:128 RGB copy observations, against independent Exif-then-view model. Model mismatches are retained, not hidden. No alpha/animation/save/OLE claim.'

def reference_file_dialog(main,command,word):
    child.post(main,0x111,command)
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770' and word in native.window_text(h)),None))
    field=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='Edit' and U.GetDlgCtrlID(h)==1152),None))
    return dialog,field

def open_reference_working(main):
    dialog,field=reference_file_dialog(main,40001,'開く')
    value=c.create_unicode_buffer(str(working));child.send(field,0xc,0,c.addressof(value))
    child.post(dialog,0x111,1);child.wait(lambda:not U.IsWindow(dialog))
    child.wait(lambda:working.name in native.window_text(main));child.ping(main)

def save_reference_png(main,target):
    assert target.resolve().is_relative_to(work.resolve()) and target.name!='01.png' and not target.exists()
    dialog,field=reference_file_dialog(main,40008,'保存')
    filetype=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='ComboBox' and U.GetDlgCtrlID(h)==1136),None))
    child.send(filetype,0x14f,1);child.post(filetype,0x100,0x24,1)
    for _ in range(3):child.post(filetype,0x100,0x28,1)
    child.post(filetype,0x100,0xd,1);child.wait(lambda:child.send(filetype,0x147)==3)
    value=c.create_unicode_buffer(target.name);child.send(field,0xc,0,c.addressof(value))
    child.post(dialog,0x111,1);child.wait(lambda:target.exists() and not U.IsWindow(dialog))
    def decoded():
        try:
            with Image.open(target) as saved:saved.load();return saved.convert('RGB')
        except (OSError,ValueError):return None
    return child.wait(decoded)

def save_cases(main,timer):
    native.api(U,'IsWindow',w.BOOL,[native.P])
    for shape,path in fixtures:
        source=Image.open(path).convert('RGB')
        for mode in range(8):
            shutil.copyfile(path,working);open_reference_working(main)
            child.send(main,0x111,41061+mode)
            actual,copy=copy_pixels(main,f'save-{shape}-{mode}-copy')
            target=work/f'{shape}-{mode}.png'
            saved=save_reference_png(main,target)
            case=dict(shape=shape,mode=mode,asset=path.name,asset_sha256=native.sha(path),copy=copy,
                saved=dict(file=target.relative_to(output).as_posix(),sha256=native.sha(target),size=list(saved.size)),
                saved_matches_display=saved.size==actual.size and saved.tobytes()==actual.tobytes(),
                copied_matches=matching(actual,source),title_after_save=native.window_text(main))
            report['cases'].append(case)
            assert native.sha(working)==native.sha(path),'Original generated PNG was overwritten'
            (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            print('save',shape,mode,'display',actual.size,'saved',saved.size,'equal',case['saved_matches_display'],flush=True)
    assert len(report['cases'])==24
    open_reference_working(main);child.post(main,0x111,40023)
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and '選択' in native.window_text(h)),None))
    report['selection_dialog_controls']=[dict(id=U.GetDlgCtrlID(h),cls=native.window_text(h,True),text=native.window_text(h)) for h in descendants(dialog)]
    child.post(dialog,0x111,2);child.wait(lambda:not U.IsWindow(dialog))
    report['scope']='Generated opaque portrait/landscape/square PNG x eight view modes:24 actual PNG SaveAs outputs versus copied displayed RGB; source hashes unchanged. Selection dialog controls inspected only. No alpha/animation/OLE claim.'

def copied_reference_path(main):
    sequence=U.GetClipboardSequenceNumber();child.post(main,0x111,40422)
    child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(13))
    child.wait(lambda:bool(U.OpenClipboard(None)),timeout=3)
    try:
        handle=U.GetClipboardData(13);pointer=K.GlobalLock(handle);native.check(pointer)
        try:return c.string_at(pointer,K.GlobalSize(handle)).decode('utf-16le').split('\0',1)[0]
        finally:K.GlobalUnlock(handle)
    finally:U.CloseClipboard()

def selection_inspect(main,timer):
    native.api(U,'IsWindow',w.BOOL,[native.P])
    child.send(main,0x111,41061);child.send(main,0x111,40002)
    child.post(main,0x111,40023)
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770' and '選択' in native.window_text(h)),None))
    child.wait(lambda:U.GetDlgItem(dialog,1) and U.GetDlgItem(dialog,2) and
               sum(native.window_text(h,True)=='Edit' for h in descendants(dialog))>=6)
    previous=[];stable_since=time.perf_counter()
    def stable_controls():
        nonlocal previous,stable_since
        current=descendants(dialog)
        if current!=previous:previous=current;stable_since=time.perf_counter();return None
        return current if time.perf_counter()-stable_since>=.05 else None
    handles=child.wait(stable_controls)
    controls=[]
    for h in handles:
        cls=native.window_text(h,True);cid=U.GetDlgCtrlID(h)
        if cls not in ('Edit','Static','Button'):continue
        value=c.create_unicode_buffer(1024);child.send(h,0xd,1024,c.addressof(value))
        controls.append(dict(id=cid,cls=cls,text=value.value))
    report['selection_dialog_controls']=controls
    print('selection controls',json.dumps(controls,ensure_ascii=False),flush=True)
    child.post(dialog,0x111,2);child.wait(lambda:not U.IsWindow(dialog))
    before=copied_reference_path(main);assert Path(before).resolve()==working.resolve(),before
    target=work/'association.png';save_reference_png(main,target);after=copied_reference_path(main)
    report['save_source_path']=dict(before=before,after=after,saved_file=target.relative_to(output).as_posix())
    print('save source path',report['save_source_path'],flush=True)
    report['scope']='Owned selection-dialog control inspection and actual CopyPath before/after SaveAs to a new private leaf. No selection-pixel, reload or binding-policy completion claim.'

def reference_selection(main,x,y,width,height):
    child.post(main,0x111,40023)
    dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770' and '選択' in native.window_text(h)),None))
    child.wait(lambda:U.GetDlgItem(dialog,2036) and U.GetDlgItem(dialog,1) and U.GetDlgItem(dialog,2))
    time.sleep(.05);child.ping(dialog)
    for cid,value in [(2031,x),(2032,y),(2035,width),(2036,height)]:
        field=child.own(U.GetDlgItem(dialog,cid))
        child.send(dialog,0x28,field,1)
        text=c.create_unicode_buffer(str(value));child.send(field,0xc,0,c.addressof(text))
        child.send(dialog,0x28,0,0)
    values={}
    for cid in range(2031,2037):
        text=c.create_unicode_buffer(64);child.send(U.GetDlgItem(dialog,cid),0xd,64,c.addressof(text));values[str(cid)]=text.value
    child.post(U.GetDlgItem(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog))
    return values

def selection_cases(main,timer):
    native.api(U,'IsWindow',w.BOOL,[native.P])
    source=Image.open(fixtures[0][1]).convert('RGB')
    child.send(main,0x111,41061);child.send(main,0x111,40002)
    def capture(label,expected=None):
        actual,copy=copy_pixels(main,label)
        case=dict(label=label,copy=copy,matches_original_rotation=matching(actual,source))
        if expected is not None:case['matches_expected']=actual.size==expected.size and actual.tobytes()==expected.tobytes()
        report['cases'].append(case);print(label,actual.size,case,flush=True)
        return actual
    capture('selection-original',source)
    report['first_selection_controls']=reference_selection(main,1,1,2,3)
    region=source.crop((1,1,3,4));capture('selection-none-region',region)
    selected_save=work/'selected-none.png';saved=save_reference_png(main,selected_save)
    report['selected_save_none']=dict(file=selected_save.relative_to(output).as_posix(),sha256=native.sha(selected_save),size=list(saved.size),is_full=saved.size==source.size and saved.tobytes()==source.tobytes(),is_selection=saved.size==region.size and saved.tobytes()==region.tobytes())
    child.send(main,0x111,41062);left=source.transpose(Image.Transpose.ROTATE_90)
    capture('selection-change-left',left)
    report['left_selection_controls']=reference_selection(main,1,1,2,3)
    region=left.crop((1,1,3,4));capture('selection-left-region',region)
    selected_save=work/'selected-left.png';saved=save_reference_png(main,selected_save)
    report['selected_save_left']=dict(file=selected_save.relative_to(output).as_posix(),sha256=native.sha(selected_save),size=list(saved.size),is_full=saved.size==left.size and saved.tobytes()==left.tobytes(),is_selection=saved.size==region.size and saved.tobytes()==region.tobytes())
    child.send(main,0x111,41062);capture('selection-same-left',region)
    child.send(main,0x111,41068);capture('selection-change-half',source.transpose(Image.Transpose.ROTATE_180))
    child.send(main,0x111,41061)
    before=copied_reference_path(main);target=work/'association.png';save_reference_png(main,target);after=copied_reference_path(main)
    changed=source.copy();changed.putpixel((0,0),(250,1,200));changed.save(assets/'mutated-working.png');changed.save(working)
    child.send(main,0x111,40002);actual=capture('save-as-reload-original',changed)
    report['save_source_association']=dict(before=before,after=after,saved_file=target.relative_to(output).as_posix(),saved_sha256=native.sha(target),mutated_source='assets/mutated-working.png',reload_reads_mutated_original=actual.size==changed.size and actual.tobytes()==changed.tobytes())
    report['scope']='Generated opaque PNG: numeric selection, copy and SaveAs before/after view changes; same-mode selection retention; CopyPath and reload after SaveAs. Only private working copy intentionally mutated for reload identity; immutable assets kept. No alpha/GIF/OLE claim.'
    print('selected saves',report['selected_save_none'],report['selected_save_left'],flush=True)
    print('source association',report['save_source_association'],flush=True)

try:
    main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
    child.wait(lambda:working.name in native.window_text(main)); child.ping(main)
    if not U.GetMenu(child.own(main)): child.send(main,0x111,41008)
    report['initial_menu']=menu_state(main)
    hook=output/'observe.dll'; shutil.copyfile(build_root/'observer.dll',hook)
    subprocess.run([str(build_root/'attach_owned.exe'),str(child.info.pid),str(reference/'MassiGra.exe'),str(hook)],check=True,timeout=25)
    report['observer_sha256']=native.sha(hook)
    timer=output/'timers.csv'
    if phase=='single':
        for shape,path in fixtures:
            source=Image.open(path).convert('RGB')
            child.send(main,0x111,41061)
            shutil.copyfile(path,working); child.send(main,0x111,40002)
            baseline,proof=copy_pixels(main,shape+'-baseline')
            assert baseline.size==source.size and baseline.tobytes()==source.tobytes(),shape
            for mode in range(8):
                begin=timer.stat().st_size
                child.send(main,0x111,41061+mode)
                immediate,immediate_proof=copy_pixels(main,f'{shape}-{mode}-immediate')
                first_paints=paints_since(main,begin,source)
                begin=timer.stat().st_size; child.send(main,0x111,40002)
                reloaded,reload_proof=copy_pixels(main,f'{shape}-{mode}-reload')
                states=menu_state(main)
                assert states[mode]['checked'] and all(s['enabled'] for s in states)
                assert native.sha(working)==native.sha(path),'Source PNG changed'
                case=dict(shape=shape,mode=mode,asset=path.name,asset_sha256=native.sha(path),
                          immediate=immediate_proof,immediate_matches=matching(immediate,source),
                          reloaded=reload_proof,reload_matches=matching(reloaded,source),
                          first_paints=first_paints,reload_paints=paints_since(main,begin,source),menu=states)
                report['cases'].append(case)
                (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                print(shape,mode,'immediate',case['immediate_matches'],'reload',case['reload_matches'],flush=True)
        assert len(report['cases'])==24
    elif phase.startswith('edit'):
        edit_cases(main,timer)
    elif phase=='exif':
        exif_cases(main,timer)
    elif phase=='save':
        save_cases(main,timer)
    elif phase=='selection-inspect':
        selection_inspect(main,timer)
    elif phase=='selection':
        selection_cases(main,timer)
    else:
        comic_cases(main,timer)
    assert native.sha(__file__)==report['tool_sha256'],'Probe source changed during run'
    report['passed']=True
finally:
    if K.WaitForSingleObject(child.info.process,0)!=0: native.check(K.TerminateProcess(child.info.process,0))
    report['child_terminal']=K.WaitForSingleObject(child.info.process,3000)==0
    child.close(); assert report['child_terminal']
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    K.ReleaseMutex(mutex); K.CloseHandle(mutex)
    print(output,flush=True)
