"""Inspect the default cut color of a probe-owned official child on a private desktop."""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import uuid
import zipfile
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
output = root / '.cache/cut-color-reference' / ('run-' + uuid.uuid4().hex)
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
native.api(U,'GetParent',native.P,[native.P])
native.api(U,'GetDlgCtrlID',c.c_int,[native.P])
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
desktop_name = 'GVN-General-Settings-' + uuid.uuid4().hex
native.desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
native.check(native.desktop)
native.check(U.SetThreadDesktop(native.desktop))
assets = output / 'assets'
assets.mkdir()
for index in range(1, 4):
    shutil.copyfile(root / 'tests/fixtures/alpha.png', assets / f'{index:02d}.png')
child = native.Child(reference / 'MassiGra.exe', [assets / '02.png'], station_name.value + '\\' + desktop_name)
report = dict(reference_sha256=native.sha(reference / 'MassiGra.exe'),
              tool_sha256=native.sha(__file__), helper_sha256=native.sha(native.__file__),
              noninteractive=True, controlled_pid=child.info.pid)
try:
    main = child.wait(lambda: next((h for h in child.windows()
                                   if native.window_text(h, True) == 'TF811202_MassiGra_Main'), None))
    child.wait(lambda: '02.png' in native.window_text(main))
    child.post(main, 0x111, 40101)
    dialog = child.wait(lambda: next((h for h in child.windows() if h != main and
                                     '設定' in native.window_text(h)), None))
    child.ping(dialog)
    descendants = []
    @native.Callback
    def enumerate_child(hwnd, _):
        descendants.append(child.own(hwnd))
        return True
    U.EnumChildWindows(dialog, enumerate_child, 0)
    tab = next(h for h in descendants if native.window_text(h, True) == 'SysTabControl32')
    controls=[]
    parent=child.wait(lambda:next((U.GetParent(h) for h in descendants if U.GetDlgCtrlID(h)==2003 and '多重起動' in native.window_text(h)),None))
    for id in [2001,2002,2003,2004,2005,2006,2007,2008,2009]:
        h=child.own(U.GetDlgItem(parent,id))
        controls.append(dict(id=id,text=native.window_text(h),checked=bool(child.send(h,0xf0))))
    report['settings_class'] = native.window_text(dialog, True)
    report['controls'] = controls
    child.post(child.own(U.GetDlgItem(parent,2062)),0xf5)
    color=child.wait(lambda:next((h for h in child.windows() if U.GetDlgItem(h,706) and U.GetDlgItem(h,707) and U.GetDlgItem(h,708)),None))
    report['color_controls']=[]
    @native.Callback
    def record_control(h,unused):
        report['color_controls'].append(dict(id=U.GetDlgCtrlID(h),type=native.window_text(h,True),text=native.window_text(h)))
        return True
    U.EnumChildWindows(color,record_control,0)
    expand=U.GetDlgItem(color,719)
    if expand:child.post(child.own(expand),0xf5)
    def components_ready():
        values=[]
        for control in (706,707,708):
            value=c.create_unicode_buffer(32)
            child.send(child.own(U.GetDlgItem(color,control)),0xd,len(value),c.addressof(value))
            values.append(value.value)
        return values if all(value.isdigit() for value in values) else None
    texts=child.wait(components_ready)
    rgb=[]
    for value in texts:rgb.append(int(value))
    assert all(0<=value<=255 for value in rgb)
    report['cut_color_rgb']=rgb
    report['cut_color_picker_class']=native.window_text(color,True)
    child.post(child.own(U.GetDlgItem(color,2)),0xf5)
    report['passed'] = True
finally:
    child.close()
    (output / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    K.ReleaseMutex(mutex)
    K.CloseHandle(mutex)
    print(output)
