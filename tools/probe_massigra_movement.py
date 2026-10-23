"""Inspect folder-navigation behavior of a probe-owned official child on a private desktop."""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import uuid
import zipfile
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
output = root / '.cache/movement-reference' / ('run-' + uuid.uuid4().hex)
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
desktop_name = 'GVN-Movement-' + uuid.uuid4().hex
native.desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
native.check(native.desktop)
native.check(U.SetThreadDesktop(native.desktop))
assets = output / 'assets'
assets.mkdir()
for relative in ['root-1.png','root-2.png','a/a-1.png','a/a-2.png','a/child/child-1.png','a/child/child-2.png','c/c-1.png','c/c-2.png']:
    path=assets/relative
    path.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(root/'tests/fixtures/alpha.png',path)
(assets/'b-empty').mkdir()
report=dict(reference_sha256=native.sha(reference/'MassiGra.exe'),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),noninteractive=True,scope='Only owned official copies, synthetic image tree and private station clipboard; no startup benchmark or user windows',cases=[])
child=None
try:
    cases=[('a/a-1.png',40068,'a/child','child-1.png'),
           ('c/c-1.png',40067,'a/child','child-2.png'),
           ('a/a-1.png',40070,'b-empty','\\b-empty\\'),
           ('c/c-1.png',40069,'b-empty','\\b-empty\\'),
           ('a/a-1.png',40071,'.','root-2.png'),
           ('a/a-1.png',40064,'a','a-2.png'),
           ('a/a-2.png',40063,'a','a-1.png')]
    for initial,command,expected_folder,expected_title in cases:
        child=native.Child(reference/'MassiGra.exe',[assets/initial],station_name.value+'\\'+desktop_name)
        main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
        child.wait(lambda:Path(initial).name in native.window_text(main))
        child.post(main,0x111,command)
        child.ping(main)
        for h in child.windows():
            if h!=main and native.window_text(h,True)=='#32770':
                button=U.GetDlgItem(h,2) or U.GetDlgItem(h,1)
                if button:child.post(button,0xf5)
        previous=U.GetClipboardSequenceNumber()
        child.post(main,0x111,40424)
        child.wait(lambda:U.GetClipboardSequenceNumber()!=previous and U.IsClipboardFormatAvailable(13))
        native.check(U.OpenClipboard(None))
        try:
            handle=U.GetClipboardData(13);pointer=K.GlobalLock(handle);native.check(pointer)
            try:folder=c.wstring_at(pointer)
            finally:K.GlobalUnlock(handle)
        finally:U.CloseClipboard()
        title=native.window_text(main)
        assert Path(folder).resolve()==(assets/expected_folder).resolve(),(initial,command,folder)
        assert expected_title in title,(initial,command,title)
        report['cases'].append(dict(initial=initial,command=command,folder=folder,title=title,
                                    expected_folder=expected_folder,expected_title=expected_title,passed=True))
        child.close();child=None
    report['passed']=True
finally:
    if child:child.close()
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
    print(json.dumps(report,ensure_ascii=False))
    print(output)
