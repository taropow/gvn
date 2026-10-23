"""Owned stock GVN processes: temporary slideshow fullscreen and persisted startup state.

Uses the real WM_COMMAND and toolbar state, private settings and a private desktop.
No injected DLL, replaced API, global input, ComputerUse or startup timing comparison.
"""
from pathlib import Path
import argparse, configparser, ctypes as c, json, re, shutil, uuid
from ctypes import wintypes as w
import benchmark_navigation as native

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('--output', type=Path, default=Path('.cache/window-startup-processes'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
output = args.output.resolve() / ('run-' + uuid.uuid4().hex)
output.mkdir(parents=True)
exe = args.executable.resolve()
U, K = native.U, native.K
native.api(U, 'IsZoomed', w.BOOL, [native.P])
native.api(U, 'GetDpiForWindow', w.UINT, [native.P])
native.api(U, 'GetSystemMetricsForDpi', c.c_int, [c.c_int, w.UINT])
native.api(U, 'MonitorFromPoint', native.P, [w.POINT, w.DWORD])
native.api(U, 'MonitorFromWindow', native.P, [native.P, w.DWORD])
native.api(U, 'GetMonitorInfoW', w.BOOL, [native.P, native.P])
native.api(K, 'GetExitCodeProcess', w.BOOL, [native.P, c.POINTER(w.DWORD)])
mutex = K.CreateMutexW(None, False, 'Local\\GVN.NoninteractiveClipboard')
native.check(mutex)
assert K.WaitForSingleObject(mutex, 30000) in (0, 0x80)
station = U.CreateWindowStationW(None, 0, 0x37f, None)
native.check(station)
flags, needed = (w.DWORD * 3)(), w.DWORD()
native.check(U.GetUserObjectInformationW(station, 1, flags, c.sizeof(flags), c.byref(needed)))
assert not flags[2] & 1
station_name = c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station, 2, station_name, c.sizeof(station_name), c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name = 'GVN-WindowStartup-' + uuid.uuid4().hex
native.desktop = U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
native.check(native.desktop)
native.check(U.SetThreadDesktop(native.desktop))
desktop = station_name.value + '\\' + desktop_name
children = []  # Keep exited process handles alive until finally; no PID reuse in this run.
commands = {}
value = 0
enum = (root / 'include/gvn/commands.h').read_text(encoding='utf-8').split('enum Command {', 1)[1].split('};', 1)[0]
for item in enum.split(','):
    match = re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*', item)
    if not match:
        continue
    key, number = match.groups()
    if number:
        value = int(number)
    commands[key] = value
    value += 1
report = dict(passed=False, private_station=True, api_replacement=False, startup_timing_measured=False,
              executable_sha256=native.sha(exe), tool_sha256=native.sha(__file__),
              helper_sha256=native.sha(native.__file__), processes=[], cases=[],
              scope='Actual stock GVN processes, WM_COMMAND, native toolbar checked state, graceful exit and next-process INI restoration; owned synthetic image, private profile and noninteractive station/desktop. No physical display/DPI or startup timing claim.')


def config_file(name, automatic, prior):
    config = output / (name + '.ini')
    config.write_text('[Viewer]\nallow_multiple=1\nreopen_last=1\nconfirm_unsaved=0\n'
                      'prefetch_enabled=0\ndisplay_mode=1\nfull_display_mode=1\n'
                      'caption=1\nmenu=0\nfull_menu=0\nslide_random=0\ninterval=600000\n'
                      'startup_position=2\nwindow_x=110\nwindow_y=120\nwindow_width=700\nwindow_height=500\n'
                      'restore_maximized=1\nrestore_fullscreen=1\n'
                      f'startup_state={2 if prior == 1 else 3 if prior == 2 else 0}\n'
                      f'slide_fullscreen={int(automatic)}\n', encoding='utf-16')
    return config


def launch(config, asset=None, expect_image=True):
    child = native.Child(exe, ['--settings', config, *([asset] if asset else [])], desktop)
    children.append(child)
    record = dict(pid=child.info.pid, graceful_exit=False, config=config.name)
    report['processes'].append(record)
    window = child.wait(lambda: next((h for h in child.windows() if native.window_text(h, True) == 'GVN.ImageViewer'), None))
    if expect_image:
        child.wait(lambda: image.name in native.window_text(window))
    child.ping(window)
    if not expect_image:
        assert image.name not in native.window_text(window)
    toolbars = []

    @native.Callback
    def add(hwnd, _):
        if native.window_text(hwnd, True).lower() == 'toolbarwindow32':
            if child.send(child.own(hwnd), 0x400 + 25, commands['Slideshow']) < 1000:
                toolbars.append(hwnd)
        return True

    U.EnumChildWindows(child.own(window), add, 0)
    assert len(toolbars) == 1, toolbars
    child.main_toolbar = child.own(toolbars[0])
    child.record = record
    return child, window


def state(child, window):
    window = child.own(window)
    full = not bool(U.GetWindowLongW(window, -16) & 0x00c00000)
    rect = w.RECT()
    native.check(U.GetWindowRect(window, c.byref(rect)))
    return dict(full=full, maximized=bool(U.IsZoomed(window)),
                logical_state=2 if full else 1 if U.IsZoomed(window) else 0,
                slideshow=bool(child.send(child.main_toolbar, 0x400 + 10, commands['Slideshow'])),
                rect=[rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top])


def command(child, window, name):
    child.send(window, 0x111, commands[name])
    child.ping(window)
    return state(child, window)


def close(child, window, config):
    child.post(window, 0x10)
    assert K.WaitForSingleObject(child.info.process, 10000) == 0
    code = w.DWORD()
    native.check(K.GetExitCodeProcess(child.info.process, c.byref(code)))
    assert code.value == 0
    child.record.update(graceful_exit=True, exit_code=code.value)
    saved = configparser.ConfigParser(interpolation=None)
    saved.read(config, encoding='utf-16')
    return saved


try:
    image = output / '日本語 窓とスライド.png'
    shutil.copyfile(root / 'tests/fixtures/display-alpha.png', image)
    image_sha = native.sha(image)
    native_cases = 0
    for line in (root / 'tests/fixtures/window-startup-reference/native-stop.tsv').read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        name, automatic, prior, started, stopped = line.split()
        automatic, prior, started, stopped = map(int, [automatic, prior, started, stopped])
        config = config_file(name, automatic, prior)
        child, window = launch(config, image)
        before = state(child, window)
        assert before['logical_state'] == prior and not before['slideshow'], before
        active = command(child, window, 'Slideshow')
        assert active['slideshow'] and active['logical_state'] == started, active
        after = command(child, window, 'Slideshow')
        assert not after['slideshow'] and after['logical_state'] == stopped, after
        assert after['rect'] == before['rect'], (before, after)
        saved = close(child, window, config)
        assert int(saved['Viewer']['last_state']) == stopped
        report['cases'].append(dict(name=name, passed=True, pid=child.info.pid, before=before, active=active, after=after))
        native_cases += 1
        print('PASS ' + name, flush=True)
    assert native_cases == 6
    for automatic in [False, True]:
        for prior in [0, 1, 2]:
            for toggles in [0, 1, 2]:
                name = f'close-active-auto{int(automatic)}-prior{prior}-toggles{toggles}'
                config = config_file(name, automatic, prior)
                child, window = launch(config, image)
                before = state(child, window)
                assert before['logical_state'] == prior
                active = command(child, window, 'Slideshow')
                assert active['slideshow'] and active['logical_state'] == (2 if automatic else prior)
                for _ in range(toggles):
                    active = command(child, window, 'Fullscreen')
                    assert active['slideshow']
                expected = prior if automatic else active['logical_state']
                saved = close(child, window, config)
                viewer = saved['Viewer']
                assert viewer['last_window_valid'] == '1' and int(viewer['last_state']) == expected
                assert int(viewer['last_width']) == 700 and int(viewer['last_height']) == 500
                viewer['startup_state'], viewer['startup_position'] = '0', '1'
                with config.open('w', encoding='utf-16') as file:
                    saved.write(file)
                reopened, window = launch(config)
                restored = state(reopened, window)
                assert restored['logical_state'] == expected and not restored['slideshow'], (name, expected, restored)
                close(reopened, window, config)
                report['cases'].append(dict(name=name, passed=True, first_pid=child.info.pid,
                                            next_pid=reopened.info.pid, expected=expected,
                                            before=before, active=active, restored=restored))
                print('PASS ' + name, flush=True)
    assert len(report['cases']) == 24 and len(children) == 42
    for name in ['fresh-empty', 'sparse-image', 'moved-previous', 'fresh-maximized', 'legacy-windows', 'legacy-user']:
        config = output / (name + '.ini')
        with_image = name in ['sparse-image', 'moved-previous']
        if name == 'sparse-image':
            config.write_text('[Viewer]\n', encoding='utf-16')
        elif name in ['legacy-windows', 'legacy-user']:
            position = 0 if name == 'legacy-windows' else 2
            config.write_text('[Viewer]\nrestore_maximized=0\nrestore_fullscreen=0\n'
                              'interval=3000\nlast_window_valid=1\nlast_state=1\n'
                              'last_x=40\nlast_y=50\nlast_width=620\nlast_height=420\n'
                              f'startup_position={position}\nwindow_x={130 if position else 100}\n'
                              f'window_y={90 if position else 100}\n'
                              f'window_width={640 if position else 1080}\n'
                              f'window_height={480 if position else 760}\n', encoding='utf-16')
        else:
            assert not config.exists()
        child, window = launch(config, image if with_image else None, expect_image=with_image)
        initial = state(child, window)
        assert initial['logical_state'] == 0 and not initial['slideshow'], initial
        if name == 'legacy-windows':
            assert initial['rect'][2:] == [1080, 760], initial
        elif name == 'legacy-user':
            assert initial['rect'] == [130, 90, 640, 480], initial
        else:
            assert initial['rect'] == [0, 0, 480, 360], initial
        if name == 'moved-previous':
            native.check(U.SetWindowPos(child.own(window), None, 110, 120, 700, 500, 0x14))
            child.ping(window)
        elif name == 'fresh-maximized':
            assert command(child, window, 'WindowMaximize')['logical_state'] == 1
        saved = close(child, window, config)
        viewer = saved['Viewer']
        if not name.startswith('legacy-'):
            assert viewer['startup_position'] == '1' and viewer['restore_maximized'] == '1'
            assert viewer['restore_fullscreen'] == '0' and viewer['interval'] == '5000'
            assert [int(viewer[key]) for key in ['window_x', 'window_y', 'window_width', 'window_height']] == [0, 0, 480, 360]
        else:
            assert viewer['restore_maximized'] == '0' and viewer['interval'] == '3000'
            assert viewer['startup_position'] == ('0' if name == 'legacy-windows' else '2')
        reopened, window = launch(config, image if with_image else None, expect_image=with_image)
        restored = state(reopened, window)
        expected_state = 1 if name == 'fresh-maximized' else 0
        assert restored['logical_state'] == expected_state and not restored['slideshow'], restored
        if name == 'moved-previous':
            assert restored['rect'] == [110, 120, 700, 500], restored
        elif name != 'fresh-maximized':
            if name == 'legacy-windows':
                assert restored['rect'][2:] == initial['rect'][2:], restored
            else:
                assert restored['rect'] == initial['rect'], restored
        close(reopened, window, config)
        report['cases'].append(dict(name=name, passed=True, first_pid=child.info.pid,
                                    next_pid=reopened.info.pid, before=initial, restored=restored))
        print('PASS ' + name, flush=True)
    assert len(report['cases']) == 30 and len(children) == 54
    class MonitorInfo(c.Structure):
        _fields_ = [('size', w.DWORD), ('monitor', w.RECT), ('work', w.RECT), ('flags', w.DWORD)]
    span_present = bool(U.MonitorFromPoint(w.POINT(-1, -1), 0))
    custom_cases = [
        ('custom-small', 37, 49, 100, 80, False),
        ('custom-one', 37, 49, 1, 1, False),
        ('custom-centered', 37, 49, 100, 80, True),
        ('custom-spanning', -1 if span_present else 37, -1 if span_present else 49, 640, 480, False),
        ('custom-large-coordinate', 2147483647, -1000000, 640, 480, False),
    ]
    for name, x, y, width, height, centered in custom_cases:
        config = config_file(name, False, 0)
        initial_profile = configparser.ConfigParser(interpolation=None)
        initial_profile.read(config, encoding='utf-16')
        initial_profile['Viewer'].update(window_x=str(x), window_y=str(y), window_width=str(width),
                                         window_height=str(height), window_center=str(int(centered)))
        with config.open('w', encoding='utf-16') as file:
            initial_profile.write(file)
        child, window = launch(config, image)
        before = state(child, window)
        dpi = U.GetDpiForWindow(child.own(window))
        expected_width = max(width, U.GetSystemMetricsForDpi(34, dpi))
        expected_height = max(height, U.GetSystemMetricsForDpi(35, dpi))
        assert before['rect'][2:] == [expected_width, expected_height], (name, before)
        if centered:
            info = MonitorInfo(); info.size = c.sizeof(info)
            native.check(U.GetMonitorInfoW(U.MonitorFromWindow(child.own(window), 2), c.byref(info)))
            expected_x = info.work.left + (info.work.right - info.work.left - expected_width) // 2
            expected_y = info.work.top + (info.work.bottom - info.work.top - expected_height) // 2
            assert before['rect'][:2] == [expected_x, expected_y], (name, before)
        elif name != 'custom-large-coordinate':
            assert before['rect'][:2] == [x, y], (name, before)
        else:
            assert before['rect'][0] < 2147483647 and before['rect'][1] > -1000000, before
        saved = close(child, window, config)
        assert [int(saved['Viewer'][key]) for key in ['window_x', 'window_y', 'window_width', 'window_height']] == [x, y, width, height]
        reopened, window = launch(config, image)
        restored = state(reopened, window)
        assert restored['rect'] == before['rect'] and not restored['slideshow'], (name, before, restored)
        close(reopened, window, config)
        report['cases'].append(dict(name=name, passed=True, first_pid=child.info.pid, next_pid=reopened.info.pid,
                                    before=before, restored=restored, dpi=dpi, spanning_environment=span_present))
        print('PASS ' + name, flush=True)
    assert len(report['cases']) == 35 and len(children) == 64
    assert all(p['graceful_exit'] for p in report['processes'])
    assert native.sha(image) == image_sha
    assert native.sha(exe) == report['executable_sha256'] and native.sha(__file__) == report['tool_sha256']
    assert native.sha(native.__file__) == report['helper_sha256']
    report.update(passed=True, native_cases=6, close_active_reopen_cases=18, fresh_and_legacy_default_cases=6,
                  custom_size_position_cases=5, custom_size_position_processes=10, stock_processes=64,
                  source_image_unchanged=True, source_image_sha256=image_sha)
finally:
    for child in children:
        child.close()
    K.ReleaseMutex(mutex)
    K.CloseHandle(mutex)
    (output / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('Evidence: ' + str(output), flush=True)
