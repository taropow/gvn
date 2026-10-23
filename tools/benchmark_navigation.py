"""Compare real viewer processes on a private desktop, never the user's windows.

Same generated files and WM_COMMAND routes. The timed metric is loaded-file
title plus UI response. Decoded dimensions and center colors are verified outside
the timers. The desktop has no DWM presentation; visual latency is not measured.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import struct
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import time
import uuid
import zipfile
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
U = c.WinDLL('user32', use_last_error=True)
K = c.WinDLL('kernel32', use_last_error=True)
P, UP, SP = c.c_void_p, c.c_size_t, c.c_ssize_t


def api(lib, name, result, arguments):
    f = getattr(lib, name)
    f.restype, f.argtypes = result, arguments
    return f


api(U, 'CreateWindowStationW', P, [w.LPCWSTR, w.DWORD, w.DWORD, P])
api(U, 'SetProcessWindowStation', w.BOOL, [P])
api(U, 'CreateDesktopW', P, [w.LPCWSTR, P, P, w.DWORD, w.DWORD, P])
api(U, 'SetThreadDesktop', w.BOOL, [P])
api(U, 'GetUserObjectInformationW', w.BOOL, [P, c.c_int, P, w.DWORD, c.POINTER(w.DWORD)])
api(U, 'EnumDesktopWindows', w.BOOL, [P, P, SP])
api(U, 'EnumChildWindows', w.BOOL, [P, P, SP])
api(U, 'GetWindowThreadProcessId', w.DWORD, [P, c.POINTER(w.DWORD)])
api(U, 'GetWindowTextW', c.c_int, [P, w.LPWSTR, c.c_int])
api(U, 'GetClassNameW', c.c_int, [P, w.LPWSTR, c.c_int])
api(U, 'GetWindowLongW', w.LONG, [P, c.c_int])
api(U, 'GetDlgItem', P, [P, c.c_int])
api(U, 'GetClientRect', w.BOOL, [P, P])
api(U, 'GetWindowRect', w.BOOL, [P, P])
api(U, 'ClientToScreen', w.BOOL, [P, P])
api(U, 'SetWindowPos', w.BOOL, [P, P, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT])
api(U, 'PostMessageW', w.BOOL, [P, w.UINT, UP, SP])
api(U, 'SendMessageTimeoutW', UP, [P, w.UINT, UP, SP, w.UINT, w.UINT, c.POINTER(UP)])
api(U, 'PrintWindow', w.BOOL, [P, P, w.UINT])
api(U, 'GetClipboardSequenceNumber', w.DWORD, [])
api(U, 'OpenClipboard', w.BOOL, [P])
api(U, 'CloseClipboard', w.BOOL, [])
api(U, 'GetClipboardData', P, [w.UINT])
api(U, 'IsClipboardFormatAvailable', w.BOOL, [w.UINT])
api(K, 'GlobalLock', P, [P])
api(K, 'GlobalUnlock', w.BOOL, [P])
api(K, 'GlobalSize', UP, [P])
api(K, 'WaitForSingleObject', w.DWORD, [P, w.DWORD])
api(K, 'TerminateProcess', w.BOOL, [P, w.UINT])
api(K, 'CloseHandle', w.BOOL, [P])
api(K, 'CreateMutexW', P, [P, w.BOOL, w.LPCWSTR])
api(K, 'ReleaseMutex', w.BOOL, [P])


class Startup(c.Structure):
    _fields_ = [('cb', w.DWORD), ('reserved', w.LPWSTR), ('desktop', w.LPWSTR), ('title', w.LPWSTR),
                ('x', w.DWORD), ('y', w.DWORD), ('cx', w.DWORD), ('cy', w.DWORD),
                ('charsx', w.DWORD), ('charsy', w.DWORD), ('fill', w.DWORD), ('flags', w.DWORD),
                ('show', w.WORD), ('reserved2', w.WORD), ('reserved3', P),
                ('stdin', P), ('stdout', P), ('stderr', P)]


class Process(c.Structure):
    _fields_ = [('process', P), ('thread', P), ('pid', w.DWORD), ('tid', w.DWORD)]


api(K, 'CreateProcessW', w.BOOL, [w.LPCWSTR, w.LPWSTR, P, P, w.BOOL, w.DWORD, P,
                                w.LPCWSTR, c.POINTER(Startup), c.POINTER(Process)])
Callback = c.WINFUNCTYPE(w.BOOL, P, SP)


def check(ok):
    if not ok:
        raise c.WinError(c.get_last_error())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def window_text(hwnd, class_name=False):
    text = c.create_unicode_buffer(1024)
    (U.GetClassNameW if class_name else U.GetWindowTextW)(hwnd, text, len(text))
    return text.value


class Child:
    def __init__(self, executable, arguments, desktop_name):
        self.info = Process()
        startup = Startup()
        startup.cb, startup.desktop, startup.flags, startup.show = c.sizeof(startup), desktop_name, 1, 1
        line = c.create_unicode_buffer(subprocess.list2cmdline([str(executable), *map(str, arguments)]))
        self.started = time.perf_counter()
        check(K.CreateProcessW(str(executable), line, None, None, False, 0, None,
                               str(executable.parent), c.byref(startup), c.byref(self.info)))

    def own(self, hwnd):
        pid = w.DWORD()
        U.GetWindowThreadProcessId(hwnd, c.byref(pid))
        assert hwnd and pid.value == self.info.pid, 'Foreign HWND rejected'
        return hwnd

    def windows(self):
        result = []

        @Callback
        def add(hwnd, _):
            pid = w.DWORD()
            U.GetWindowThreadProcessId(hwnd, c.byref(pid))
            if pid.value == self.info.pid:
                result.append(hwnd)
            return True

        c.set_last_error(0)
        if not U.EnumDesktopWindows(desktop, add, 0) and c.get_last_error():
            check(False)
        return result

    def wait(self, predicate, timeout=20):
        end = time.perf_counter() + timeout
        while time.perf_counter() < end:
            assert K.WaitForSingleObject(self.info.process, 0) != 0, 'Owned child exited'
            result = predicate()
            if result:
                return result
            time.sleep(.001)
        raise TimeoutError('Native operation timed out: ' + repr([(window_text(h), window_text(h, True))
                                                                  for h in self.windows()]))

    def post(self, hwnd, message, wp=0, lp=0):
        check(U.PostMessageW(self.own(hwnd), message, wp, lp))

    def ping(self, hwnd):
        value = UP()
        check(U.SendMessageTimeoutW(self.own(hwnd), 0, 0, 0, 2, 20000, c.byref(value)))

    def send(self, hwnd, message, wp=0, lp=0):
        value = UP()
        check(U.SendMessageTimeoutW(self.own(hwnd), message, wp, lp, 2, 20000, c.byref(value)))
        return value.value

    def decoded_center(self, hwnd, command):
        # Verification is outside the navigation timer. It is not a display capture.
        previous = U.GetClipboardSequenceNumber()
        self.post(hwnd, 0x111, command)
        self.wait(lambda: U.GetClipboardSequenceNumber() != previous and
                  (U.IsClipboardFormatAvailable(17) or U.IsClipboardFormatAvailable(8)))
        check(U.OpenClipboard(None))
        try:
            handle = U.GetClipboardData(17 if U.IsClipboardFormatAvailable(17) else 8)
            pointer = K.GlobalLock(handle)
            check(pointer)
            try:
                data = c.string_at(pointer, K.GlobalSize(handle))
            finally:
                K.GlobalUnlock(handle)
        finally:
            U.CloseClipboard()
        header, width, height, planes, bits, compression = struct.unpack_from('<IiiHHI', data)
        assert header in (40, 108, 124) and planes == 1 and bits in (24, 32) and compression in (0, 3)
        offset = header + (12 if header == 40 and compression == 3 else 0)
        offset += struct.unpack_from('<I', data, 32)[0] * 4
        stride = ((width * bits + 31) // 32) * 4
        y = abs(height) // 2
        if height > 0:
            y = height - 1 - y
        location = offset + y * stride + width // 2 * (bits // 8)
        b, g, r = data[location:location+3]
        return (width, abs(height)), (r, g, b)

    def close(self):
        if K.WaitForSingleObject(self.info.process, 0) != 0:
            K.TerminateProcess(self.info.process, 0)  # Only the process handle created above.
        K.WaitForSingleObject(self.info.process, 3000)
        K.CloseHandle(self.info.thread)
        K.CloseHandle(self.info.process)


def assets(directory):
    directory.mkdir(parents=True, exist_ok=True)
    manifest_path = directory / 'manifest.json'
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        assert all(sha(directory / f['path']) == f['sha256'] for group in manifest for f in group['files'])
        return manifest
    manifest = []
    for name, size, format in [('png-1k', 1024, 'PNG'), ('jpeg-4k', 3840, 'JPEG'), ('png-4k', 3840, 'PNG')]:
        folder = directory / name
        folder.mkdir(exist_ok=True)
        height = size if size == 1024 else 2160
        noise = Image.effect_noise((size, height), 36).convert('RGB')
        group = dict(name=name, size=[size, height], files=[])
        for index in range(10):
            color = (40 + 19 * index, 210 - 17 * index, 35 + 13 * index)
            frame = noise.copy()
            draw = ImageDraw.Draw(frame)
            radius = min(size, height) // 5
            draw.rectangle((size // 2 - radius, height // 2 - radius,
                            size // 2 + radius, height // 2 + radius), fill=color)
            path = folder / f'image-{index:02d}.{format.lower()}'
            frame.save(path, format=format, **(dict(quality=93, subsampling=0) if format == 'JPEG'
                                               else dict(compress_level=1)))
            # JPEG's actual center is the oracle, avoiding lossy-color assumptions.
            with Image.open(path) as decoded:
                expected = decoded.convert('RGB').getpixel((size // 2, height // 2))
            group['files'].append(dict(path=str(path.relative_to(directory)), sha256=sha(path),
                                        bytes=path.stat().st_size, center=list(expected)))
        manifest.append(group)
        print('ASSETS', name, sum(f['bytes'] for f in group['files']), flush=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def stats(samples):
    ordered = sorted(samples)
    return dict(median_ms=statistics.median(ordered), min_ms=ordered[0], max_ms=ordered[-1],
                p95_ms=ordered[max(0, (len(ordered) * 95 + 99) // 100 - 1)])


def main():
    global desktop
    parser = argparse.ArgumentParser()
    parser.add_argument('development_executable', type=Path)
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--dataset', action='append')
    args = parser.parse_args()
    assert 1 <= args.trials <= 5
    output = ROOT / '.cache/navigation-benchmark' / ('run-' + uuid.uuid4().hex)
    output.mkdir(parents=True)
    datasets = assets(ROOT / '.cache/navigation-benchmark/assets-v1')
    if args.dataset:
        datasets = [g for g in datasets if g['name'] in args.dataset]
    archive = ROOT / '.cache/massigra-reference/MassiGra045.zip'
    assert sha(archive) == '76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    reference = output / 'reference'
    reference.mkdir()
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if name == 'MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                target = (reference / name).resolve()
                assert target.is_relative_to(reference.resolve())
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(package.read(name))
    assert sha(reference / 'MassiGra.exe') == 'c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    mutex = K.CreateMutexW(None, False, 'Local\\GVN.NoninteractiveClipboard')
    check(mutex)
    assert K.WaitForSingleObject(mutex, 30000) in (0, 0x80)
    station = U.CreateWindowStationW(None, 0, 0x37f, None)
    check(station)
    flags = (w.DWORD * 3)()
    needed = w.DWORD()
    check(U.GetUserObjectInformationW(station, 1, flags, c.sizeof(flags), c.byref(needed)))
    assert not flags[2] & 1
    name = c.create_unicode_buffer(512)
    check(U.GetUserObjectInformationW(station, 2, name, c.sizeof(name), c.byref(needed)))
    check(U.SetProcessWindowStation(station))
    desktop_name = 'GVN-Benchmark-' + uuid.uuid4().hex
    desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
    check(desktop)
    check(U.SetThreadDesktop(desktop))
    apps = [dict(name='MassiGra 0.45', exe=reference / 'MassiGra.exe', cls='TF811202_MassiGra_Main',
                 next=40062, previous=40061, fit=40087, settings=40101, copy=40025),
            dict(name='GVN 0.8.0', exe=ROOT / 'dist/GVN-0.8.0/GVN.exe', cls='GVN.ImageViewer',
                 next=104, previous=103, fit=110, settings=188, copy=125),
            dict(name='GVN development', exe=args.development_executable.resolve(), cls='GVN.ImageViewer',
                 next=104, previous=103, fit=110, settings=188, copy=125)]
    source_config = Path.home() / 'AppData/Local/GVN/settings.ini'
    config_snapshot = output / 'settings-source.ini'
    if source_config.exists():
        shutil.copyfile(source_config, config_snapshot)
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), tool_sha256=sha(__file__),
                  station_visible=False, method='Native WM_COMMAND on owned child PIDs, private desktop. '
                  'Same files/order, 1080x760 outer window and fit. Navigation metric is loaded-file title '
                  'plus UI-thread response. Actual decoded dimensions and center color are checked separately '
                  'via private clipboard outside the timers. Monitor/DWM presentation is not measured. No global input. '
                  'OS file cache is not cleared; default neighboring prefetch remains enabled.',
                  applications=[dict(name=a['name'], executable_sha256=sha(a['exe'])) for a in apps],
                  datasets=datasets, runs=[], gvn_configuration_sha256=sha(config_snapshot) if config_snapshot.exists() else None)
    try:
        for group in datasets:
            for trial in range(args.trials):
                # Rotate app order to reduce warm-file-cache bias.
                order = apps[trial % len(apps):] + apps[:trial % len(apps)]
                for app in order:
                    initial = ROOT / '.cache/navigation-benchmark/assets-v1' / group['files'][0]['path']
                    config = output / ('gvn-' + uuid.uuid4().hex + '.ini')
                    if app['name'].startswith('GVN') and config_snapshot.exists():
                        shutil.copyfile(config_snapshot, config)
                    arguments = [initial] if app['name'].startswith('MassiGra') else ['--settings', config, initial]
                    child = Child(app['exe'], arguments, name.value + '\\' + desktop_name)
                    sample = dict(app=app['name'], dataset=group['name'], trial=trial,
                                  controlled_child_pid=int(child.info.pid), steps=[])
                    try:
                        main = child.wait(lambda: next((h for h in child.windows()
                                          if window_text(h, True) == app['cls'] and U.GetWindowLongW(h, -16) & 0x10000000), None))
                        child.ping(main)
                        sample['window_ready_ms'] = (time.perf_counter() - child.started) * 1000
                        child.wait(lambda: initial.name in window_text(main))
                        child.ping(main)
                        check(U.SetWindowPos(child.own(main), None, 20, 20, 1080, 760, 0x14))
                        child.send(main, 0x111, app['fit'])
                        sample['first_frame_ready_ms'] = (time.perf_counter() - child.started) * 1000
                        dimensions, actual = child.decoded_center(main, app['copy'])
                        expected = group['files'][0]['center']
                        assert list(dimensions) == group['size'] and max(abs(a-b) for a,b in zip(actual, expected)) <= 3, ('Wrong initial decoded frame', app['name'], actual, expected)
                        for mode, delay in [('burst', 0), ('paced-250ms', .25)]:
                            moves = [(i, 'next') for i in range(1, 10)] + [(i, 'previous') for i in range(8, -1, -1)]
                            for index, direction in moves:
                                if delay:
                                    time.sleep(delay)
                                target = group['files'][index]
                                target_name = Path(target['path']).name
                                start = time.perf_counter()
                                child.post(main, 0x111, app[direction])
                                child.wait(lambda: target_name in window_text(main))
                                child.ping(main)
                                title_ms = (time.perf_counter() - start) * 1000
                                sample['steps'].append(dict(mode=mode, target=target_name, direction=direction,
                                                            title_ready_ms=title_ms))
                        sample['decoded_spot_checks'] = []
                        for index in range(1, 10):
                            child.post(main, 0x111, app['next'])
                            child.wait(lambda: Path(group['files'][index]['path']).name in window_text(main))
                            child.ping(main)
                            if index in (4, 9):
                                dimensions, actual = child.decoded_center(main, app['copy'])
                                target = group['files'][index]
                                assert list(dimensions) == group['size'] and max(abs(a-b) for a,b in zip(actual, target['center'])) <= 3, ('Wrong decoded frame', app['name'], index, actual, target['center'])
                                sample['decoded_spot_checks'].append(dict(index=index, dimensions=dimensions, center=actual))
                        sample['settings_ready_ms'] = []
                        for _ in range(3):
                            start = time.perf_counter()
                            child.post(main, 0x111, app['settings'])
                            dialog = child.wait(lambda: next((h for h in child.windows() if h != main
                                                and '設定' in window_text(h)
                                                and U.GetWindowLongW(h, -16) & 0x10000000), None))
                            child.ping(dialog)
                            sample['settings_ready_ms'].append((time.perf_counter()-start)*1000)
                            cancel = child.own(U.GetDlgItem(dialog, 2))
                            child.post(cancel, 0xf5)
                            child.wait(lambda: dialog not in child.windows())
                        sample['passed'] = True
                        print('PASS', group['name'], app['name'], trial,
                              'title median ms', round(statistics.median(s['title_ready_ms'] for s in sample['steps']), 2), flush=True)
                    except Exception:
                        sample['failure_windows'] = [dict(title=window_text(h), cls=window_text(h, True)) for h in child.windows()]
                        raise
                    finally:
                        child.close()
                        report['runs'].append(sample)
                        (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        report['summary'] = []
        for group in datasets:
            for app in apps:
                rows = [r for r in report['runs'] if r['app'] == app['name'] and r['dataset'] == group['name']]
                for mode in ['burst', 'paced-250ms']:
                    samples = [s for r in rows for s in r['steps'] if s['mode'] == mode]
                    report['summary'].append(dict(app=app['name'], dataset=group['name'], mode=mode,
                        count=len(samples), title_ready=stats([s['title_ready_ms'] for s in samples])))
        report['passed'] = True
    finally:
        assert sha(__file__) == report['tool_sha256'], 'Benchmark source changed during execution'
        (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        print('EVIDENCE', output, flush=True)
        K.ReleaseMutex(mutex)
        K.CloseHandle(mutex)


if __name__ == "__main__":
    main()
