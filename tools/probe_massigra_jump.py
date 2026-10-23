"""Inspect movement settings of a probe-owned official child on a private desktop."""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import uuid
import zipfile
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
output = root / '.cache/jump-reference' / ('run-' + uuid.uuid4().hex)
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
desktop_name = 'GVN-Jump-' + uuid.uuid4().hex
native.desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
native.check(native.desktop)
native.check(U.SetThreadDesktop(native.desktop))
assets = output / 'assets'
assets.mkdir()
for index in range(1, 4):
    shutil.copyfile(root / 'tests/fixtures/alpha.png', assets / f'{index:02d}.png')
child = native.Child(reference / 'MassiGra.exe', [assets / '02.png'], station_name.value + '\\' + desktop_name)
report = dict(reference_sha256=native.sha(reference / 'MassiGra.exe'), tool_sha256=native.sha(__file__), helper_sha256=native.sha(native.__file__), noninteractive=True, scope='Only an owned official child, synthetic images and private station; no startup comparison or current user clipboard', cases=[])
def edit(hwnd, value):
    buffer=c.create_unicode_buffer(value)
    child.send(hwnd, 0x000c, 0, c.addressof(buffer))
    child.ping(hwnd)
def opened(main):
    child.post(main, 0x111, 40074)
    return child.wait(lambda:next((h for h in child.windows() if 'ジャンプ' in native.window_text(h) and all(U.GetDlgItem(h,i) for i in [2031,2032,2121,2021,2001])),None))
def control_text(hwnd):
    buffer=c.create_unicode_buffer(1024)
    child.send(hwnd,0x000d,1024,c.addressof(buffer))
    return buffer.value
def state(dialog):
    order=U.GetDlgItem(dialog,2032); bar=U.GetDlgItem(dialog,2121); quality=U.GetDlgItem(dialog,2021)
    choices=[]
    for i in range(child.send(quality,0x146)):
        buffer=c.create_unicode_buffer(256)
        child.send(quality,0x148,i,c.addressof(buffer)); choices.append(buffer.value)
    return dict(name=control_text(U.GetDlgItem(dialog,2031)), order=control_text(order), total=native.window_text(U.GetDlgItem(dialog,2091)), preview=bool(child.send(U.GetDlgItem(dialog,2001),0xf0)), quality=child.send(quality,0x147), choices=choices, scroll_position=U.GetScrollPos(bar,2))
native.api(U,'GetScrollPos',c.c_int,[native.P,c.c_int])
try:
    main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
    child.wait(lambda:'02.png' in native.window_text(main))
    dialog=opened(main); child.ping(dialog); initial=state(dialog)
    print('INITIAL',initial,flush=True)
    report['initial']=initial
    edit(U.GetDlgItem(dialog,2032),'3'); changed=state(dialog)
    print('CHANGED',changed,flush=True)
    assert changed['order']=='3' and changed['scroll_position']==3, changed
    report['cases'].append(dict(action='order to last',state=changed,passed=True))
    child.send(dialog,0x114,0,U.GetDlgItem(dialog,2121)); changed=state(dialog)
    report['cases'].append(dict(action='scroll line left',state=changed,passed=changed['order']=='2'))
    assert changed['order']=='2', changed
    preview=U.GetDlgItem(dialog,2001); child.send(preview,0xf5)
    child.send(U.GetDlgItem(dialog,2021),0x14e,0)
    child.send(dialog,0x111,2021|(1<<16),U.GetDlgItem(dialog,2021))
    changed=state(dialog); report['changed']=changed
    child.post(U.GetDlgItem(dialog,2),0xf5); child.wait(lambda:'ジャンプ' not in '\n'.join(native.window_text(h) for h in child.windows()))
    dialog=opened(main); report['after_cancel']=state(dialog)
    child.post(U.GetDlgItem(dialog,2),0xf5)
    report['passed']=all(x['passed'] for x in report['cases'])
finally:
    child.close()
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
    print(json.dumps(report,ensure_ascii=False));print(output)
