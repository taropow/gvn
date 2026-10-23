# Stock GVN slideshow, actual controls and original OS timer notifications.
from pathlib import Path
import argparse, configparser, ctypes as c, json, re, shutil, uuid
from ctypes import wintypes as w
import benchmark_navigation as native

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('--output', type=Path, default=Path('.cache/slideshow-processes'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
output = args.output.resolve() / ('run-' + uuid.uuid4().hex)
output.mkdir(parents=True)
exe = args.executable.resolve()
U, K = native.U, native.K
native.api(U, 'IsZoomed', w.BOOL, [native.P])
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
desktop_name = 'GVN-Slideshow-' + uuid.uuid4().hex
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
        child.wait(lambda: (asset or image).name in native.window_text(window))
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


for name,result,arguments in [
 ('GetMenu',native.P,[native.P]),('GetSubMenu',native.P,[native.P,c.c_int]),
 ('GetMenuItemCount',c.c_int,[native.P]),('GetMenuItemID',w.UINT,[native.P,c.c_int]),
 ('GetMenuState',w.UINT,[native.P,w.UINT,w.UINT])]:native.api(U,name,result,arguments)
for name,result,arguments in [
 ('VirtualAllocEx',native.P,[native.P,native.P,native.UP,w.DWORD,w.DWORD]),
 ('VirtualFreeEx',w.BOOL,[native.P,native.P,native.UP,w.DWORD]),
 ('ReadProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P])]:native.api(K,name,result,arguments)
fixture=root/'tests/fixtures/slideshow-reference/native-policies.json'
manifest=json.loads((fixture.parent/'manifest.json').read_text())
assert native.sha(fixture)==manifest['files'][fixture.name]
oracle=json.loads((root/manifest['source_report']).read_text(encoding='utf-8'))
assert native.sha(root/manifest['source_report'])==manifest['source_report_sha256']
report.update(scope='Stock GVN, owned private desktop/profile/PNG tree, actual toolbar and sort-menu checks, actual status text, explicit WM_TIMER notifications and one real-OS-timer case. No DLL injection, API replacement, global input, ComputerUse or startup-speed comparison.',oracle_sha256=native.sha(fixture),synthetic_timer_notifications=True,real_os_timer_cases=1)
assets=output/'images';assets.mkdir()
paths={}
for name in ['flat/flat-0.png','flat/flat-1.png','flat/flat-2.png','flat/flat-3.png',
             'tree/root-0.png','tree/a/a-0.png','tree/a/a-1.png','tree/a/c/c-0.png','tree/b/b-0.png']:
 path=assets/name;path.parent.mkdir(parents=True,exist_ok=True)
 shutil.copyfile(root/'tests/fixtures/display-alpha.png',path);paths[path.name]=path
image=paths['flat-0.png'];asset_hashes={str(p):native.sha(p) for p in paths.values()}

def profile(name,loop,next_folder,subtree,random_order,sort=0,interval=600000,fullscreen=False):
 config=output/(name+'.ini')
 config.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\n'
                   'menu=1\nfull_menu=1\ncaption=1\nrestore_fullscreen=0\nstartup_state=0\n'
                   f'interval={interval}\nslide_fullscreen={int(fullscreen)}\nslide_loop={int(loop)}\n'
                   f'slide_next_folder={int(next_folder)}\nslide_subtree={int(subtree)}\n'
                   f'slide_random={int(random_order)}\nsort={sort}\nend_action=1\n',encoding='utf-16')
 return config

def attach_status(child,window):
 controls=[]
 @native.Callback
 def add(hwnd,_):
  if native.window_text(hwnd,True).lower()=='msctls_statusbar32':controls.append(child.own(hwnd))
  return True
 U.EnumChildWindows(child.own(window),add,0);assert len(controls)==1,controls
 child.status=controls[0];child.status_memory=K.VirtualAllocEx(child.info.process,None,4096,0x3000,4)
 native.check(child.status_memory)

def status(child,part=4):
 child.send(child.status,0x400+13,part,child.status_memory)
 value=c.create_unicode_buffer(2048)
 native.check(K.ReadProcessMemory(child.info.process,child.status_memory,value,4096,None));return value.value

def finish(child,window,config):
 native.check(K.VirtualFreeEx(child.info.process,child.status_memory,0,0x8000));child.status_memory=None
 return close(child,window,config)

def sort_checks(child,window):
 checked=[]
 def walk(menu):
  for index in range(U.GetMenuItemCount(menu)):
   submenu=U.GetSubMenu(menu,index)
   if submenu:walk(submenu)
   else:
    identity=U.GetMenuItemID(menu,index)
    if commands['SortName']<=identity<=commands['SortRandom'] and U.GetMenuState(menu,identity,0)&8:
     checked.append(identity-commands['SortName'])
 menu=U.GetMenu(child.own(window));assert menu;walk(menu);assert len(checked)==1,checked
 return checked[0]

def settled(child,window):
 child.wait(lambda:'読み込み中' not in status(child))
 value=state(child,window);title=native.window_text(window)
 value.update(file=next((name for name in paths if name in title),None),sort=sort_checks(child,window),position=status(child,1))
 def slide_state(menu):
  result=[]
  for index in range(U.GetMenuItemCount(menu)):
   submenu=U.GetSubMenu(menu,index)
   if submenu:result.extend(slide_state(submenu))
   elif U.GetMenuItemID(menu,index)==commands['Slideshow']:
    result.append(bool(U.GetMenuState(menu,commands['Slideshow'],0)&8))
  return result
 checks=slide_state(U.GetMenu(child.own(window)));assert len(checks)==1,checks
 value['menu_slideshow']=checks[0]
 assert value['menu_slideshow']==value['slideshow'],value
 assert value['file'],title
 return value

def step(child,window):
 assert state(child,window)['slideshow']
 child.send(window,0x113,2,0)
 return settled(child,window)

try:
 for native_case in oracle['cases']:
  name=native_case['name'];options=native_case['options'];prior_random=40805 in native_case['before']['checked_sort_commands']
  config=profile(name,options['loop'],options['next_folder'],options['subtree'],options['random'],4 if prior_random else 0)
  child,window=launch(config,paths[native_case['before']['file']]);attach_status(child,window)
  before=settled(child,window);command(child,window,'Slideshow');started=settled(child,window)
  case=dict(name=name,before=before,started=started,steps=[]);report['cases'].append(case)
  assert started['slideshow'] and started['sort']==(4 if prior_random or options['random'] else 0)
  random_case=prior_random or options['random']
  for expected in native_case['steps']:
   actual=step(child,window);case['steps'].append(actual)
   assert actual['slideshow']==expected['slideshow'],(name,expected,actual)
   if not random_case:assert actual['file']==expected['file'],(name,expected,actual)
  if random_case:
   sequence=[started['file']]+[p['file'] for p in case['steps']]
   if options['loop']:
    groups=[sequence[i:i+4] for i in range(0,len(sequence)-3,4)]
    assert len(groups)==4 and all(len(set(group))==4 for group in groups),(name,sequence)
    assert len({tuple(group) for group in groups})>1,(name,sequence)
   else:
    assert len(set(sequence[:4]))==4 and sequence[-1]==sequence[-2],sequence
  if state(child,window)['slideshow']:command(child,window,'Slideshow')
  case['after']=settled(child,window);assert not case['after']['slideshow'] and case['after']['sort']==(4 if prior_random else 0)
  saved=finish(child,window,config);assert int(saved['Viewer']['sort'])==(4 if prior_random else 0)
  case['passed']=True;print('PASS '+name,flush=True)
 assert len(report['cases'])==10 and len(children)==10
 config=profile('close-active-random',True,False,False,True,fullscreen=True)
 child,window=launch(config,image);attach_status(child,window);command(child,window,'Slideshow')
 active=settled(child,window);assert active['full'] and active['sort']==4
 saved=finish(child,window,config)
 assert saved['Viewer']['sort']=='0' and saved['Viewer']['last_state']=='0' and saved['Viewer']['slide_random']=='1'
 child,window=launch(config,image);attach_status(child,window);restored=settled(child,window)
 assert not restored['slideshow'] and not restored['full'] and restored['sort']==0
 finish(child,window,config);report['cases'].append(dict(name='close-active-random-restart',passed=True,active=active,restored=restored))
 print('PASS close-active-random-restart',flush=True)
 config=profile('real-os-timer-stop',False,False,False,False,interval=250)
 child,window=launch(config,image);attach_status(child,window);command(child,window,'Slideshow')
 child.wait(lambda:not state(child,window)['slideshow']);after=settled(child,window)
 assert after['file']=='flat-3.png' and not after['slideshow'];finish(child,window,config)
 report['cases'].append(dict(name='real-os-timer-stop',passed=True,after=after,synthetic_timer_notifications=False))
 print('PASS real-os-timer-stop',flush=True)
 assert len(report['cases'])==12 and len(children)==13 and all(p['graceful_exit'] for p in report['processes'])
 assert all(native.sha(Path(name))==digest for name,digest in asset_hashes.items())
 assert native.sha(exe)==report['executable_sha256'] and native.sha(__file__)==report['tool_sha256'] and native.sha(native.__file__)==report['helper_sha256']
 report.update(passed=True,stock_processes=13,source_images_unchanged=True,whole_goal_complete=False)
finally:
 for child in children:child.close()
 K.ReleaseMutex(mutex);K.CloseHandle(mutex)
 (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print('Evidence: '+str(output),flush=True)
