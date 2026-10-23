"""Measure GIF behavior in a probe-owned official 0.45 child on a private desktop. No startup benchmarking."""
import ctypes as c
from ctypes import wintypes as w
import json, struct, sys, time, statistics, io, subprocess, os
from PIL import GifImagePlugin
from PIL import Image
from pathlib import Path
import shutil
import uuid
import zipfile
import benchmark_navigation as native

root = Path(__file__).resolve().parents[1]
kind = 'stall'
costs = [int(v) for v in (sys.argv[1] if len(sys.argv)>1 else '0,350').split(',')]
assert costs and len(costs)<=10 and all(0<=v<=1000 for v in costs)
next_delay_cs = int(sys.argv[2]) if len(sys.argv)>2 else 10
assert 1 <= next_delay_cs <= 50
assert kind == 'stall'
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
delays = [30, 10, next_delay_cs, 10, 10, 50]
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

    report['method'] = 'Observe real GDI paints with an optional one-shot 350ms rendering cost in the owned child; no clock replacement.'
    report['stall_target_frame'] = 1
    token = 0
    for mode in range(5):
        dialog, page = opened(main)
        h = U.GetDlgItem(page, 2026)
        child.send(h, 0x14e, mode)
        child.send(page, 0x111, 2026 | (1 << 16), h)
        assert child.send(h, 0x147) == mode
        child.post(U.GetDlgItem(dialog, 1), 0xf5)
        child.wait(lambda: not U.IsWindow(dialog))
        for cost in costs:
            token += 1
            color = colors[1][0] | colors[1][1] << 8 | colors[1][2] << 16
            pending = output / 'stall-pending.bin'
            pending.write_bytes(struct.pack('<III', token, cost, color))
            os.replace(pending, output / 'stall-config.bin')
            begin = timer_file.stat().st_size
            child.send(main, 0x111, 40002)
            time.sleep(1.9)
            child.ping(main)
            calls = [[float(line.split(',')[0]), *map(int, line.split(',')[1:])]
                     for line in timer_file.read_bytes()[begin:].decode('ascii').splitlines()]
            transitions = []
            for row in calls:
                if row[2] not in (4,5) or abs(row[3]) != 64 or abs(row[4]) != 64:
                    continue
                rgb = [row[5] & 255, (row[5] >> 8) & 255, (row[5] >> 16) & 255]
                if rgb in [list(c) for c in colors]:
                    frame = [list(c) for c in colors].index(rgb)
                    if not transitions or transitions[-1][1] != frame:
                        transitions.append([row[0],frame])
            events = [row for row in calls if row[2] in (7,8)]
            assert all(any(frame == i for _,frame in transitions) for i in (0,1,5)), 'Anchor frames not observed'
            if cost:
                assert len(events)==2 and [row[2] for row in events]==[7,8]
                assert max(0,cost-5) <= events[1][0]-events[0][0] <= cost+1000
            else:
                assert not events
            relative = [[round(t-transitions[0][0],3),frame] for t,frame in transitions]
            case = dict(mode=mode,label=choices[mode],artificial_render_cost_ms=cost,
                        calls=calls,transitions=transitions,relative_transitions=relative,stall_events=events)
            report['cases'].append(case)
            (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
            print(mode,cost,relative[:12],flush=True)
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
