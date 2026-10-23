"""Read save-option choices from an isolated, probe-owned official MassiGra copy."""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import uuid
import zipfile
import hashlib
import random
import struct
import zlib
from PIL import Image
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
output = root/'.cache/save-options-reference'/('run-'+uuid.uuid4().hex)
output.mkdir(parents=True)
archive = root/'.cache/massigra-reference/MassiGra045.zip'
assert native.sha(archive) == '76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
reference = output/'reference'
with zipfile.ZipFile(archive) as package:
    for name in package.namelist():
        if name == 'MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
            path = (reference/name).resolve()
            assert path.is_relative_to(reference.resolve())
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(package.read(name))
assert native.sha(reference/'MassiGra.exe') == 'c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
U, K = native.U, native.K
native.api(U, 'GetDlgCtrlID', c.c_int, [native.P])
native.api(U, 'GetParent', native.P, [native.P])
native.api(U, 'GetWindowLongW', c.c_long, [native.P, c.c_int])
mutex = K.CreateMutexW(None, False, 'Local\\GVN.NoninteractiveClipboard')
native.check(mutex)
assert K.WaitForSingleObject(mutex, 30000) in (0, 0x80)
station = U.CreateWindowStationW(None, 0, 0x37f, None)
native.check(station)
flags, needed = (w.DWORD * 3)(), w.DWORD()
native.check(U.GetUserObjectInformationW(station, 1, flags, c.sizeof(flags), c.byref(needed)))
assert not flags[2] & 1
name = c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station, 2, name, c.sizeof(name), c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name = 'GVN-SaveOptions-'+uuid.uuid4().hex
native.desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
native.check(native.desktop)
native.check(U.SetThreadDesktop(native.desktop))
assets = output/'assets'
assets.mkdir()
source = assets/'fixture.png'
rng = random.Random(80421)
image = Image.new('RGB', (96, 64))
image.putdata([((x*3+y+rng.randrange(5))%256, (x+y*3)%256, (x//8*16+y//4*4)%256)
               for y in range(image.height) for x in range(image.width)])
image.save(source)
child = native.Child(reference/'MassiGra.exe', [source], name.value+'\\'+desktop_name)
report = dict(reference_sha256=native.sha(reference/'MassiGra.exe'), tool_sha256=native.sha(__file__),
              helper_sha256=native.sha(native.__file__), noninteractive=True, controlled_pid=child.info.pid)

def descendants(window):
    result = []
    @native.Callback
    def add(hwnd, _):
        result.append(child.own(hwnd))
        return True
    U.EnumChildWindows(window, add, 0)
    return result

def text(hwnd):
    buffer = c.create_unicode_buffer(32768)
    child.send(hwnd, 0xd, len(buffer), c.addressof(buffer))
    return buffer.value

def combo_items(hwnd):
    count = child.send(hwnd, 0x146)
    if count > 1024:
        return []
    items = []
    for index in range(count):
        buffer = c.create_unicode_buffer(32768)
        size = child.send(hwnd, 0x149, index)
        assert size < len(buffer), 'Unexpected combo label length'
        child.send(hwnd, 0x148, index, c.addressof(buffer))
        items.append(buffer.value)
    return items

def snapshot(window):
    records = []
    for hwnd in descendants(window):
        cls = native.window_text(hwnd, True)
        if cls not in ('Button', 'ComboBox', 'Edit', 'Static', '#32770'):
            continue
        row = dict(id=U.GetDlgCtrlID(hwnd), cls=cls, text=text(hwnd), visible=locally_visible(hwnd),
                   visible_style=bool(U.GetWindowLongW(hwnd, -16) & 0x10000000))
        if cls == 'Button': row['checked'] = child.send(hwnd, 0xf0)
        if cls == 'ComboBox': row.update(items=combo_items(hwnd), selected=child.send(hwnd, 0x147))
        records.append(row)
    return records

def locally_visible(hwnd):
    while True:
        style = U.GetWindowLongW(hwnd, -16)
        if not style & 0x10000000: return False
        if not style & 0x40000000: return True
        hwnd = U.GetParent(hwnd)
        child.own(hwnd)

def select_combo(hwnd, index):
    child.send(hwnd, 0x14f, 1)
    child.post(hwnd, 0x100, 0x24, 1)
    for step in range(index): child.post(hwnd, 0x100, 0x28, 1)
    child.post(hwnd, 0x100, 0xd, 1)

def saved_png_ready(path):
    try:
        with Image.open(path) as decoded:
            decoded.load()
        return True
    except (OSError, ValueError):
        return False

try:
    main = child.wait(lambda: next((h for h in child.windows() if native.window_text(h, True) == 'TF811202_MassiGra_Main'), None))
    child.wait(lambda: source.name in native.window_text(main))
    child.post(main, 0x111, 40008)
    dialog = child.wait(lambda: next((h for h in child.windows() if h != main and '保存' in native.window_text(h)), None))
    child.ping(dialog)
    report['dialog_title'] = native.window_text(dialog)
    combos = [h for h in descendants(dialog) if native.window_text(h, True) == 'ComboBox']
    filetype = next(h for h in combos if any('BMP' in s.upper() for s in combo_items(h)) and any('GIF' in s.upper() for s in combo_items(h)))
    formats = combo_items(filetype)
    report['formats'] = formats
    report['initial_type_index'] = child.send(filetype, 0x147)
    report['pages'] = []
    for index, label in enumerate(formats):
        select_combo(filetype, index)
        child.ping(dialog)
        aliases = ['JPG', 'JPEG'] if index == 2 else [label.split(' - ')[0]]
        child.wait(lambda: any('保存設定' in text(h) and any(s in text(h) for s in aliases)
                               for h in descendants(dialog) if native.window_text(h, True) == 'Button' and locally_visible(h)), timeout=5)
        controls = snapshot(dialog)
        assert child.send(filetype, 0x147) == index
        report['pages'].append(dict(format=label, controls=controls))
    report['png_saves'] = []
    for preset in range(3):
        if preset:
            child.post(main, 0x111, 40008)
            dialog = child.wait(lambda: next((h for h in child.windows() if h != main and '保存' in native.window_text(h)), None))
            child.ping(dialog)
            filetype = child.wait(lambda: next((h for h in descendants(dialog) if native.window_text(h, True) == 'ComboBox' and U.GetDlgCtrlID(h) == 1136), None))
            select_combo(filetype, 3)
        filter_combo = child.wait(lambda: next((h for h in descendants(dialog) if native.window_text(h, True) == 'ComboBox' and
                                               locally_visible(h) and U.GetDlgCtrlID(h) == 2021 and '速度を優先' in combo_items(h)), None))
        select_combo(filter_combo, preset)
        child.ping(dialog)
        filename = next(h for h in descendants(dialog) if native.window_text(h, True) == 'Edit' and U.GetDlgCtrlID(h) == 1152)
        target = assets/f'filter-{preset}.png'
        assert not target.exists() and target.resolve().is_relative_to(assets.resolve())
        name = c.create_unicode_buffer(str(target))
        child.send(filename, 0xc, 0, c.addressof(name))
        child.post(dialog, 0x111, 1)
        child.wait(lambda: target.exists() and dialog not in child.windows())
        child.wait(lambda: saved_png_ready(target))
        child.ping(main)
        with Image.open(target) as decoded:
            assert decoded.convert('RGB').tobytes() == image.tobytes()
        data = target.read_bytes()
        width, height, depth, color, compression, filtering, interlace = struct.unpack_from('>IIBBBBB', data, 16)
        assert (width, height, depth, color, interlace) == (96, 64, 8, 2, 0)
        packed, offset = bytearray(), 8
        while offset < len(data):
            length = struct.unpack_from('>I', data, offset)[0]
            if data[offset+4:offset+8] == b'IDAT': packed.extend(data[offset+8:offset+8+length])
            offset += 12+length
        rows = zlib.decompress(packed)
        stride = width*3+1
        assert len(rows) == stride*height
        counts = {value:sum(rows[y*stride] == value for y in range(height)) for value in range(5)}
        report['png_saves'].append(dict(preset=preset, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                                        filters=counts, rgb_equal=True))
    report['passed'] = True
finally:
    child.close()
    (output/'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    shutil.copyfile(__file__, output/'tool.py.txt')
    K.ReleaseMutex(mutex)
    K.CloseHandle(mutex)
    print(output, flush=True)
