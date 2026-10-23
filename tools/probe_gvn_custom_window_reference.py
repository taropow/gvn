# Stock GVN versus immutable official custom-window geometry observations.
from pathlib import Path
import argparse, configparser, ctypes as c, json, re, shutil, uuid
from ctypes import wintypes as w
import benchmark_navigation as native

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('--output', type=Path, default=Path('.cache/custom-window-reference-processes'))
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
desktop_name = 'GVN-CustomWindow-' + uuid.uuid4().hex
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


import csv
class MonitorInfo(c.Structure):
    _fields_=[('size',w.DWORD),('monitor',w.RECT),('work',w.RECT),('flags',w.DWORD)]
MonitorCallback=c.WINFUNCTYPE(w.BOOL,native.P,native.P,c.POINTER(w.RECT),native.SP)
native.api(U,'EnumDisplayMonitors',w.BOOL,[native.P,native.P,MonitorCallback,native.SP])
def monitors():
    result=[]
    @MonitorCallback
    def add(handle,_,rect,__):
        info=MonitorInfo();info.size=c.sizeof(info);native.check(U.GetMonitorInfoW(handle,c.byref(info)))
        result.append(dict(monitor=[info.monitor.left,info.monitor.top,info.monitor.right,info.monitor.bottom],
                           work=[info.work.left,info.work.top,info.work.right,info.work.bottom],primary=bool(info.flags&1)))
        return True
    native.check(U.EnumDisplayMonitors(None,None,add,0));return result

fixture=root/'tests/fixtures/window-custom-reference/geometry.tsv'
manifest=json.loads((fixture.parent/'manifest.json').read_text(encoding='utf-8'))
assert native.sha(fixture)==manifest['files']['geometry.tsv']
report.update(scope='Actual stock GVN processes compared with28 immutable official0.45 restart geometries on the same read-only monitor topology; private station/config/synthetic source only, no DLL/API replacement/global input or timing claim.',
              geometry_fixture_sha256=native.sha(fixture),monitor_context=monitors(),physical_dwm_tested=False)
assert report['monitor_context']==manifest['monitor_context'],'Reference monitor topology changed; recapture before comparison'
rows=[row for row in csv.reader(fixture.read_text(encoding='utf-8').splitlines(),delimiter='\t') if row and not row[0].startswith('#')]
assert len(rows)==28
try:
    image=output/'日本語 起動配置 Alpha.png';shutil.copyfile(root/'tests/fixtures/display-alpha.png',image);image_sha=native.sha(image)
    for row in rows:
        name=row[0];centered,x,y,width,height=map(int,row[1:6]);expected=list(map(int,row[6:10]))
        config=config_file(name,False,0);profile=configparser.ConfigParser(interpolation=None);profile.read(config,encoding='utf-16')
        profile['Viewer'].update(window_center=str(centered),window_x=str(x),window_y=str(y),window_width=str(width),window_height=str(height))
        with config.open('w',encoding='utf-16') as stream:profile.write(stream)
        child,window=launch(config,image);actual=state(child,window)
        assert actual['rect']==expected,(name,'expected',expected,'actual',actual)
        if row[10]!='unknown':assert U.GetDpiForWindow(child.own(window))==int(row[10])
        saved=close(child,window,config)
        assert [int(saved['Viewer'][key]) for key in ['window_x','window_y','window_width','window_height']]==[x,y,width,height]
        reopened,window=launch(config,image);restored=state(reopened,window)
        assert restored['rect']==expected,(name,expected,restored)
        close(reopened,window,config)
        report['cases'].append(dict(name=name,passed=True,expected=expected,first=actual,restored=restored,
                                    first_pid=child.info.pid,next_pid=reopened.info.pid))
        print('PASS '+name,flush=True)
    assert len(children)==56 and len(report['cases'])==28 and all(p['graceful_exit'] for p in report['processes'])
    assert monitors()==report['monitor_context'] and native.sha(fixture)==report['geometry_fixture_sha256']
    assert native.sha(image)==image_sha and native.sha(exe)==report['executable_sha256'] and native.sha(__file__)==report['tool_sha256'] and native.sha(native.__file__)==report['helper_sha256']
    report.update(passed=True,stock_processes=56,reference_geometries=28,source_image_unchanged=True,whole_goal_complete=False)
finally:
    for child in children:child.close()
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Evidence: '+str(output),flush=True)
