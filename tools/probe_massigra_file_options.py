"""Inspect file-operation behavior of a probe-owned official child on a private desktop."""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import uuid
import zipfile
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
output = root / '.cache/file-options-reference' / ('run-' + uuid.uuid4().hex)
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
desktop_name = 'GVN-FileOptions-' + uuid.uuid4().hex
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
    remote = K.VirtualAllocEx(child.info.process, None, 16, 0x3000, 4)
    native.check(remote)
    try:
        native.check(child.send(tab, 0x130a, 8, remote))
        rect = (c.c_int * 4)()
        native.check(K.ReadProcessMemory(child.info.process, remote, rect, 16, None))
        x, y = (rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2
        child.post(tab, 0x201, 1, x | y << 16)
        child.post(tab, 0x202, 0, x | y << 16)
    finally:
        K.VirtualFreeEx(child.info.process, remote, 0, 0x8000)
    child.wait(lambda: 'ファイル操作' in '\n'.join(native.window_text(h) for h in child.windows()) or
               child.send(tab, 0x130b) == 8)
    child.ping(dialog)
    controls = []
    @native.Callback
    def add(hwnd, _):
        child.own(hwnd)
        controls.append(dict(hwnd=hwnd, text=native.window_text(hwnd), cls=native.window_text(hwnd, True),
                             checked=child.send(hwnd, 0xf0) if native.window_text(hwnd, True) == 'Button' else None))
        return True
    U.EnumChildWindows(dialog, add, 0)
    report['settings_class'] = native.window_text(dialog, True)
    report['controls'] = controls
    report['passed'] = True
finally:
    child.close()
    (output / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    K.ReleaseMutex(mutex)
    K.CloseHandle(mutex)
    print(output)
