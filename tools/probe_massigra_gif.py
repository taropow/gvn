"""Measure GIF behavior in a probe-owned official 0.45 child on a private desktop. No startup benchmarking."""
import ctypes as c
from ctypes import wintypes as w
import json, struct, sys, time, statistics, io, subprocess
from PIL import GifImagePlugin
from PIL import Image
from pathlib import Path
import shutil
import uuid
import zipfile
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
kind = sys.argv[1] if len(sys.argv)>1 else 'timing'
assert kind in ('timing','loops','background')
observer_build = root / '.cache/reference-gif-observer'
build = json.loads((observer_build / 'build.json').read_text(encoding='utf-8-sig'))
for name, digest in build['sources'].items(): assert native.sha(root / 'tools/reference_gif' / name) == digest
for name, digest in build['binaries'].items(): assert native.sha(observer_build / name) == digest
output = root / '.cache/gif-reference' / ('run-' + uuid.uuid4().hex)
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
desktop_name = 'GVN-GIF-Settings-' + uuid.uuid4().hex
native.desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
native.check(native.desktop)
native.check(U.SetThreadDesktop(native.desktop))

# Delays are explicit centiseconds, no Pillow animation optimization.
assets = output / 'assets'
assets.mkdir()
delays = [100, 0, 1, 2, 3, 4, 5, 9, 10, 11, 20]
colors = [(40 + i * 18, 200 - i * 11, 60 + i * 13) for i in range(len(delays))]
palette = sum((list(v) for v in colors), []) + [0] * (768 - 3 * len(colors))
frames = []
for index in range(len(delays)):
    frame = Image.new('P', (64, 64), index)
    frame.putpalette(palette)
    frames.append(frame)
header, _ = GifImagePlugin.getheader(frames[0], info={'loop': 0})
data = b''.join(header)
for frame, delay in zip(frames, delays):
    data += b''.join(GifImagePlugin.getdata(frame, duration=delay * 10, disposal=1))
data += b';'
path = assets / 'timing.gif'
path.write_bytes(data)
with Image.open(path) as image:
    for i, delay in enumerate(delays):
        image.seek(i)
        assert image.info.get('duration', 0) == delay * 10

child = native.Child(reference / 'MassiGra.exe', [path], station_name.value + '\\' + desktop_name)
report = dict(kind=kind, passed=False, reference_sha256=native.sha(reference / 'MassiGra.exe'),
              tool_sha256=native.sha(__file__), helper_sha256=native.sha(native.__file__),
              noninteractive=True, controlled_pid=child.info.pid, delays_cs=delays, colors=colors,
              asset_sha256=native.sha(path), cases=[], limitations=[
                  'Observed SetTimer arguments in the single probe-owned official child via a local test-only IAT hook; not startup or presentation latency.',
                  'Timer quantization means observed durations alone do not prove exact thresholds.',
                  'Drawn DIB pixels are RGB; original alpha is not proven by this reference capture.'])

def descendants(parent):
    result = []
    @native.Callback
    def add(hwnd, _):
        result.append(child.own(hwnd))
        return True
    U.EnumChildWindows(parent, add, 0)
    return result

def opened(main):
    child.post(main, 0x111, 40101)
    dialog = child.wait(lambda: next((h for h in child.windows() if h != main and '設定' in native.window_text(h)), None))
    tab = child.wait(lambda: next((h for h in descendants(dialog) if native.window_text(h, True) == 'SysTabControl32'), None))
    remote = K.VirtualAllocEx(child.info.process, None, 16, 0x3000, 4)
    native.check(remote)
    try:
        native.check(child.send(tab, 0x130a, 3, remote))
        rect = (c.c_int * 4)()
        native.check(K.ReadProcessMemory(child.info.process, remote, rect, 16, None))
        x, y = (rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2
        child.post(tab, 0x201, 1, x | y << 16)
        child.post(tab, 0x202, 0, x | y << 16)
    finally:
        K.VirtualFreeEx(child.info.process, remote, 0, 0x8000)
    child.wait(lambda: child.send(tab, 0x130b) == 3)
    page = child.wait(lambda: next((U.GetParent(h) for h in descendants(dialog)
        if U.GetDlgCtrlID(h) == 2026 and native.window_text(h, True) == 'ComboBox'), None))
    return dialog, page


try:
    main = child.wait(lambda: next((h for h in child.windows() if native.window_text(h, True) == 'TF811202_MassiGra_Main'), None))
    child.wait(lambda: path.name in native.window_text(main))
    child.ping(main)
    hook = output / 'observe.dll'
    shutil.copyfile(observer_build / 'observer.dll', hook)
    subprocess.run([str(observer_build / 'attach_owned.exe'), str(child.info.pid),
                    str(reference / 'MassiGra.exe'), str(hook)], check=True, timeout=25)
    report['observer_sha256'] = native.sha(hook)
    report['observer_build'] = build
    sources = output / 'sources'
    sources.mkdir()
    for source in [Path(__file__), Path(native.__file__), *(root / 'tools/reference_gif').iterdir()]:
        if source.is_file(): shutil.copyfile(source, sources / source.name)
    timer_file = output / 'timers.csv'
    dialog, page = opened(main)
    h = U.GetDlgItem(page, 2026)
    report['default_mode'] = child.send(h, 0x147)
    choices = []
    for i in range(child.send(h, 0x146)):
        text = c.create_unicode_buffer(256)
        child.send(h, 0x148, i, c.addressof(text))
        choices.append(text.value)
    report['choices'] = choices
    child.post(U.GetDlgItem(dialog, 2), 0xf5)
    child.wait(lambda: not U.IsWindow(dialog))
    if kind == 'timing':
        for mode in range(5):
            dialog, page = opened(main)
            h = U.GetDlgItem(page, 2026)
            child.send(h, 0x14e, mode)
            child.send(page, 0x111, 2026 | (1 << 16), h)
            assert child.send(h, 0x147) == mode
            child.post(U.GetDlgItem(dialog, 1), 0xf5)
            child.wait(lambda: not U.IsWindow(dialog))
            begin = timer_file.stat().st_size
            child.send(main, 0x111, 40002)
            time.sleep(5.5)
            calls = [[float(line.split(',')[0]), *map(int, line.split(',')[1:])] for line in timer_file.read_bytes()[begin:].decode('ascii').splitlines()]
            case = dict(mode=mode, label=choices[mode], timer_calls=calls)
            report['cases'].append(case)
            (output / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            print('timing',mode,len(calls),'draw calls',flush=True)
            assert calls, 'No animation paint/timer call observed'
    elif kind == 'loops':
        delays = [5,5,5]
        colors = colors[:3]
        frames = frames[:3]
        for mode in range(5):
            dialog, page = opened(main)
            h = U.GetDlgItem(page, 2026)
            child.send(h, 0x14e, mode)
            child.send(page, 0x111, 2026 | (1 << 16), h)
            child.post(U.GetDlgItem(dialog, 1), 0xf5)
            child.wait(lambda: not U.IsWindow(dialog))
            for loops in [None, 0, 1, 2, 3]:
                header, _ = GifImagePlugin.getheader(frames[0], info={} if loops is None else {'loop': loops})
                data = b''.join(header)
                for frame, delay in zip(frames, delays):
                    data += b''.join(GifImagePlugin.getdata(frame, duration=delay * 10, disposal=1))
                data += b';'
                path.write_bytes(data)
                (assets / ('loops-' + str(loops) + '.gif')).write_bytes(data)
                begin = timer_file.stat().st_size
                child.send(main, 0x111, 40002)
                time.sleep(1.8)
                calls = [[float(line.split(',')[0]), *map(int, line.split(',')[1:])]
                         for line in timer_file.read_bytes()[begin:].decode('ascii').splitlines()]
                sequence = []
                for row in calls:
                    if row[2] not in (4, 5) or abs(row[3]) != 64 or abs(row[4]) != 64:
                        continue
                    rgb = (row[5] & 255, (row[5] >> 8) & 255, (row[5] >> 16) & 255)
                    if rgb in colors:
                        index = colors.index(rgb)
                        if not sequence or sequence[-1] != index:
                            sequence.append(index)
                case = dict(mode=mode, label=choices[mode], loop_field=loops,
                            timer_calls=calls, painted_frames=sequence, asset_sha256=native.sha(path))
                report['cases'].append(case)
                (output / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
                print('loops',mode,loops,len(sequence),'frames',flush=True)
                assert len(sequence) >= 3, 'Animation loop frames not observed'
    elif kind == 'background':
        palette = [12, 34, 56, 220, 20, 40, 20, 210, 60, 30, 50, 220, 190, 200, 210] + [0] * (768 - 15)
        for mode in range(5):
            dialog, page = opened(main)
            h = U.GetDlgItem(page, 2026)
            child.send(h, 0x14e, mode)
            child.send(page, 0x111, 2026 | (1 << 16), h)
            child.post(U.GetDlgItem(dialog, 1), 0xf5)
            child.wait(lambda: not U.IsWindow(dialog))
            for case_id, disposal, transparency in [(f'disposal-{i}', i, [None]*3) for i in range(8)] + [
                    ('transparent-all', 2, [4, 4, 4]),
                    ('transparent-first-only', 2, [4, None, None]),
                    ('transparent-second-only', 2, [None, 4, None]),
                    ('transparent-first-previous', 3, [4, None, None])]:
                first = Image.new('P', (8, 4), 1)
                second = Image.new('P', (4, 4), 2)
                third = Image.new('P', (4, 4), 3)
                for frame in (first, second, third): frame.putpalette(palette)
                header, _ = GifImagePlugin.getheader(first, info={'background': 0})
                data = b''.join(header)
                for index, (frame, pos, disp, trans) in enumerate(zip(
                        (first, second, third), ((0,0), (0,0), (4,0)),
                        (1, disposal, 1), transparency)):
                    info = {'duration': 100, 'disposal': disp}
                    if trans is not None: info['transparency'] = trans
                    data += b''.join(GifImagePlugin.getdata(frame, offset=pos, **info))
                data += b';'
                path.write_bytes(data)
                (assets / (case_id + '.gif')).write_bytes(data)
                begin = timer_file.stat().st_size
                child.send(main, 0x111, 40002)
                time.sleep(.55)
                calls = [[float(line.split(',')[0]), *map(int, line.split(',')[1:])]
                         for line in timer_file.read_bytes()[begin:].decode('ascii').splitlines()]
                snapshots = []
                for row in calls:
                    if row[2] != 6: continue
                    file = output / f'snapshot-{row[3]:05d}.bmp'
                    with Image.open(file) as im:
                        assert im.size == (8, 4), im.size
                        pixels = list(im.convert('RGB').getdata())
                        if not snapshots or snapshots[-1]['pixels'] != pixels:
                            snapshots.append({'file': file.name, 'sha256': native.sha(file), 'pixels': pixels, 'time_ms': row[0]})
                case = dict(mode=mode, label=choices[mode], id=case_id, disposal=disposal,
                            transparency=transparency, draw_calls=calls, frames=snapshots,
                            asset_sha256=native.sha(path))
                report['cases'].append(case)
                (output / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
                print('background',mode,case_id,len(snapshots),'frames',flush=True)
                assert len(snapshots) == 3, 'Expected exactly three composed frames'
    report['passed'] = True
finally:
    if K.WaitForSingleObject(child.info.process,0) != 0:
        native.check(K.TerminateProcess(child.info.process,0))
    assert K.WaitForSingleObject(child.info.process,3000)==0, 'Owned reference is not terminal'
    child.close()
    report['child_terminal'] = True
    (output / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    K.ReleaseMutex(mutex)
    K.CloseHandle(mutex)
    print(output, flush=True)
