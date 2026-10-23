"""Observe fresh official display policies on owned private windows, without startup timing."""
from pathlib import Path
from ctypes import wintypes as w
import ctypes as c
import json, re, shutil, sys, uuid, zipfile
from PIL import Image
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
out = root / '.cache/display-policy-reference' / ('run-' + uuid.uuid4().hex)
out.mkdir(parents=True)
archive = root / '.cache/massigra-reference/MassiGra045.zip'
assert native.sha(archive) == '76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
reference = out / 'reference'
with zipfile.ZipFile(archive) as package:
    for name in package.namelist():
        if name == 'MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
            target = (reference / name).resolve()
            assert target.is_relative_to(reference.resolve())
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(package.read(name))
exe = reference / 'MassiGra.exe'
assert native.sha(exe) == 'c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
(out / 'sources').mkdir()
for source in [Path(__file__), Path(native.__file__)]:
    shutil.copyfile(source, out / 'sources' / source.name)
asset = out / 'small.png'
Image.new('RGB', (128, 96), (211, 37, 173)).save(asset)
U, K = native.U, native.K
native.api(U, 'GetMenu', native.P, [native.P])
native.api(U, 'GetSubMenu', native.P, [native.P, c.c_int])
native.api(U, 'GetMenuItemCount', c.c_int, [native.P])
native.api(U, 'GetMenuItemID', w.UINT, [native.P, c.c_int])
native.api(U, 'GetMenuState', w.UINT, [native.P, w.UINT, w.UINT])
native.api(U, 'GetMenuStringW', c.c_int, [native.P, w.UINT, w.LPWSTR, c.c_int, w.UINT])
native.api(U, 'GetWindowRect', w.BOOL, [native.P, c.POINTER(w.RECT)])
native.api(U, 'GetClientRect', w.BOOL, [native.P, c.POINTER(w.RECT)])
native.api(U, 'PrintWindow', w.BOOL, [native.P, native.P, w.UINT])
native.api(U, 'GetDC', native.P, [native.P])
native.api(U, 'ReleaseDC', c.c_int, [native.P, native.P])
G = c.WinDLL('gdi32', use_last_error=True)
native.api(G, 'CreateCompatibleDC', native.P, [native.P])
native.api(G, 'CreateCompatibleBitmap', native.P, [native.P, c.c_int, c.c_int])
native.api(G, 'SelectObject', native.P, [native.P, native.P])
native.api(G, 'GetDIBits', c.c_int, [native.P, native.P, w.UINT, w.UINT, native.P, native.P, w.UINT])
native.api(G, 'DeleteObject', w.BOOL, [native.P])
native.api(G, 'DeleteDC', w.BOOL, [native.P])
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
desktop_name = 'GVN-Display-Policy-' + uuid.uuid4().hex
native.desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
native.check(native.desktop)
native.check(U.SetThreadDesktop(native.desktop))
child = native.Child(exe, [asset], station_name.value + '\\' + desktop_name)
report = dict(passed=False, reference_sha256=native.sha(exe), asset_sha256=native.sha(asset), tool_sha256=native.sha(__file__),
              helper_sha256=native.sha(native.__file__), controlled_pid=child.info.pid,
              station_visible=False, fresh_profile=True, startup_timing_measured=False,
              scope='Fresh official0.45 display policy menus and caption. PrintWindow captures are diagnostic only and must not be used as paint evidence without observed asset pixels. No user windows, clipboard, profile, global input or startup comparison.', cases=[])

def menu_rows(main, menu, parent=''):
    child.send(main, 0x117, menu)
    result = []
    for index in range(U.GetMenuItemCount(menu)):
        text = c.create_unicode_buffer(512)
        U.GetMenuStringW(menu, index, text, len(text), 0x400)
        label = text.value
        sub = U.GetSubMenu(menu, index)
        if sub:
            result.extend(menu_rows(main, sub, parent + label + '/'))
        else:
            command = U.GetMenuItemID(menu, index)
            state = U.GetMenuState(menu, index, 0x400)
            if command not in (0, 0xffffffff):
                result.append(dict(command=command, label=label, parent=parent,
                                   checked=bool(state & 8), enabled=not bool(state & 3)))
    return result

def capture(main, label, menu):
    child.ping(main)
    outer, client = w.RECT(), w.RECT()
    native.check(U.GetWindowRect(child.own(main), c.byref(outer)))
    native.check(U.GetClientRect(child.own(main), c.byref(client)))
    width, height = outer.right - outer.left, outer.bottom - outer.top
    dc = U.GetDC(child.own(main)); native.check(dc)
    memory = G.CreateCompatibleDC(dc); native.check(memory)
    bitmap = G.CreateCompatibleBitmap(dc, width, height); native.check(bitmap)
    old = G.SelectObject(memory, bitmap); native.check(old)
    try:
        native.check(U.PrintWindow(child.own(main), memory, 0))
        info = (w.DWORD * 10)()
        info[0], info[1], info[2], info[3] = 40, width, (-height) & 0xffffffff, 1 | (32 << 16)
        pixels = (c.c_ubyte * (width * height * 4))()
        G.SelectObject(memory, old)
        assert G.GetDIBits(memory, bitmap, 0, height, pixels, info, 0) == height
        image = Image.frombytes('RGB', (width, height), bytes(pixels), 'raw', 'BGRX')
        image.save(out / (label + '.png'))
        bounds = [width, height, -1, -1]
        count = 0
        for y in range(height):
            for x in range(width):
                if image.getpixel((x, y)) == (211, 37, 173):
                    bounds = [min(bounds[0], x), min(bounds[1], y), max(bounds[2], x), max(bounds[3], y)]
                    count += 1
        rows = menu_rows(main, menu)
        policy = [row for row in rows if '表示オプション' in row['parent']]
        assert policy and sum(row['checked'] for row in policy) == 1, policy
        case = dict(label=label, caption=native.window_text(main),
                    window=[outer.left, outer.top, outer.right, outer.bottom],
                    client=[client.right, client.bottom], image_color_bounds=bounds, image_color_pixels=count,
                    capture_contains_asset_pixels=count > 0,
                    displayed_color_size=[bounds[2] - bounds[0] + 1, bounds[3] - bounds[1] + 1] if count else None,
                    policy=policy, capture=label + '.png', capture_sha256=native.sha(out / (label + '.png')))
        report['cases'].append(case)
        print(json.dumps(case, ensure_ascii=True), flush=True)
        return rows
    finally:
        G.SelectObject(memory, old)
        G.DeleteObject(bitmap)
        G.DeleteDC(memory)
        U.ReleaseDC(main, dc)

try:
    main = child.wait(lambda: next((hwnd for hwnd in child.windows() if native.window_text(hwnd, True) == 'TF811202_MassiGra_Main'), None))
    child.wait(lambda: asset.name in native.window_text(main))
    if not U.GetMenu(child.own(main)):
        child.send(main, 0x111, 41008)
    menu = U.GetMenu(child.own(main)); native.check(menu)
    child.send(main, 0x116, menu)
    rows = capture(main, 'normal-small', menu)
    full = [row for row in rows if '全画面' in re.sub(r'[\s　]', '', row['label']) and '表示' in row['label']]
    assert len(full) == 1, full
    report['fullscreen_command'] = full[0]['command']
    child.send(main, 0x111, full[0]['command'])
    capture(main, 'fullscreen-small', menu)
    child.send(main, 0x111, full[0]['command'])
    capture(main, 'normal-restored', menu)
    checked = [next(row['command'] for row in case['policy'] if row['checked']) for case in report['cases']]
    assert checked == [41023, 41022, 41023], checked
    assert '<Fit ' in report['cases'][1]['caption']
    report['observed_default_policy_indices'] = [2, 1, 2]
    report['paint_equivalence_claimed'] = False
    assert native.sha(asset) == report['asset_sha256']
    assert native.sha(__file__) == report['tool_sha256']
    report['passed'] = True
finally:
    if K.WaitForSingleObject(child.info.process, 0) != 0:
        native.check(K.TerminateProcess(child.info.process, 0))
    report['owned_child_terminal'] = K.WaitForSingleObject(child.info.process, 3000) == 0
    assert report['owned_child_terminal']
    child.close()
    report['owned_child_closed'] = True
    (out / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    K.ReleaseMutex(mutex); K.CloseHandle(mutex)
    print('Evidence: ' + str(out), flush=True)
