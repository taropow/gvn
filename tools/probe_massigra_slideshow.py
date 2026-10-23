"""Owned official0.45 slideshow policy through observed native timer messages."""
from pathlib import Path
from ctypes import wintypes as w
import ctypes as c,configparser,json,re,shutil,subprocess,sys,time,uuid,zipfile
from PIL import Image
import benchmark_navigation as native
root=Path(__file__).resolve().parents[1]
lifecycle=True
empty_start='--empty' in sys.argv[1:]
initial_restart='--initial-restart' in sys.argv[1:]
assert not (lifecycle and empty_start)
out=root/'.cache/window-startup-reference'/('run-'+uuid.uuid4().hex);out.mkdir(parents=True)
archive=root/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
reference=out/'reference'
observer=root/'.cache/reference-slideshow-observer'
observer_manifest=json.loads((observer/'build.json').read_text(encoding='utf-8-sig')) if lifecycle else None
if lifecycle:
 assert observer_manifest['architecture']=='x86' and not observer_manifest['clock_replacement'] and observer_manifest['api_observation_only']
 assert all(native.sha(root/'tools/reference_slideshow'/name)==digest for name,digest in observer_manifest['sources'].items())
 assert all(native.sha(observer/name)==digest for name,digest in observer_manifest['binaries'].items())
 shutil.copyfile(observer/'observer.dll',out/'observe.dll')
with zipfile.ZipFile(archive) as package:
 for name in package.namelist():
  if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
   target=(reference/name).resolve();assert target.is_relative_to(reference.resolve());target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(package.read(name))
exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
assets=out/'images';assets.mkdir()
for index,color in enumerate([(211,37,173),(19,137,83)]):Image.new('RGB',(64,48),color).save(assets/f'image{index}.png')
source_files={p:native.sha(p) for p in [Path(__file__),Path(native.__file__),root/'.cache/massigra-research/text/d_prop_wnd.htm.txt',root/'.cache/massigra-research/text/d_autonext.htm.txt']}
if lifecycle:source_files.update({p:native.sha(p) for p in (root/'tools/reference_slideshow').iterdir() if p.is_file()})
U,K=native.U,native.K
for name,result,args in [('IsWindow',w.BOOL,[native.P]),('IsWindowEnabled',w.BOOL,[native.P]),('IsWindowVisible',w.BOOL,[native.P]),('GetParent',native.P,[native.P]),('GetDlgCtrlID',c.c_int,[native.P]),('IsZoomed',w.BOOL,[native.P]),('IsIconic',w.BOOL,[native.P]),('GetMenu',native.P,[native.P]),('GetSubMenu',native.P,[native.P,c.c_int]),('GetMenuItemCount',c.c_int,[native.P]),('GetMenuItemID',w.UINT,[native.P,c.c_int]),('GetMenuStringW',c.c_int,[native.P,w.UINT,w.LPWSTR,c.c_int,w.UINT])]:native.api(U,name,result,args)
for name,result,args in [('VirtualAllocEx',native.P,[native.P,native.P,native.UP,w.DWORD,w.DWORD]),('VirtualFreeEx',w.BOOL,[native.P,native.P,native.UP,w.DWORD]),('ReadProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P]),('GetExitCodeProcess',w.BOOL,[native.P,c.POINTER(w.DWORD)])]:native.api(K,name,result,args)
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station);flags=(w.DWORD*3)();needed=w.DWORD();native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
station_name=c.create_unicode_buffer(512);native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)));native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-WindowReference-'+uuid.uuid4().hex;native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
child=None;main=None;children=[];barrier_serial=0
report=dict(passed=False,reference_sha256=native.sha(exe),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),noninteractive=True,startup_timing_measured=False,scope='Official0.45 controls read on an owned private station/desktop/profile with synthetic sources. No ComputerUse/global input/user windows or startup-speed comparison. Window positions/states are OS values; physical DWM/monitor changes are not tested.',cases=[],processes=[],lifecycle_cases=[])

def descendants(parent):
 result=[]
 @native.Callback
 def add(window,_):result.append(child.own(window));return True
 U.EnumChildWindows(child.own(parent),add,0);return result

def text(window):
 value=c.create_unicode_buffer(4096);child.send(window,0xd,len(value),c.addressof(value));return value.value

def button(parent,identity):
 target=child.wait(lambda:U.GetDlgItem(parent,identity));child.post(child.own(target),0xf5)

def combo(parent,identity):
 window=child.own(U.GetDlgItem(parent,identity));count=child.send(window,0x146);assert count<64
 choices=[]
 for index in range(count):
  value=c.create_unicode_buffer(512);child.send(window,0x148,index,c.addressof(value));choices.append(value.value)
 return dict(selected=child.send(window,0x147),choices=choices,enabled=bool(U.IsWindowEnabled(window)))

def choose(parent,identity,index):
 window=child.own(U.GetDlgItem(parent,identity));assert child.send(window,0x14e,index)==index;child.send(parent,0x111,identity|(1<<16),window)

def window_controls(page):
 return dict(position=combo(page,2021),state=combo(page,2022),checks={str(i):dict(checked=bool(child.send(child.own(U.GetDlgItem(page,i)),0xf0)),enabled=bool(U.IsWindowEnabled(U.GetDlgItem(page,i)))) for i in [2001,2002,2003,2004]},user={str(i):dict(text=text(child.own(U.GetDlgItem(page,i))),enabled=bool(U.IsWindowEnabled(U.GetDlgItem(page,i)))) for i in [2031,2032,2033,2034]},current_enabled=bool(U.IsWindowEnabled(U.GetDlgItem(page,2062))))

def open_window_page():
 child.post(main,0x111,40101);dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and '設定' in native.window_text(h)),None))
 tab=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32'),None));remote=K.VirtualAllocEx(child.info.process,None,16,0x3000,4);native.check(remote)
 try:
  native.check(child.send(tab,0x130a,2,remote));rect=(c.c_int*4)();native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None));x,y=(rect[0]+rect[2])//2,(rect[1]+rect[3])//2
  child.post(tab,0x201,1,x|y<<16);child.post(tab,0x202,0,x|y<<16)
 finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
 child.wait(lambda:child.send(tab,0x130b)==2)
 report['property_page_diagnostic']=[dict(id=U.GetDlgCtrlID(h),cls=native.window_text(h,True),text=text(h),visible=bool(U.IsWindowVisible(h))) for h in descendants(dialog)]
 # This private-station reference reports its modal root as non-visible. Identify the
 # actual page by its unique label/control IDs and selected tab, not a paint claim.
 report['property_dialog_visible']=bool(U.IsWindowVisible(dialog))
 field=child.wait(lambda:next((h for h in descendants(dialog) if U.GetDlgCtrlID(h)==2001 and 'ウィンドウを' in text(h)),None))
 page=child.own(U.GetParent(field));assert text(page)=='ウィンドウ';return dialog,page

def menu_rows(menu,parent=''):
 result=[]
 for index in range(U.GetMenuItemCount(menu)):
  label=c.create_unicode_buffer(512);U.GetMenuStringW(menu,index,label,len(label),0x400)
  sub=U.GetSubMenu(menu,index)
  if sub:result.extend(menu_rows(sub,parent+label.value+'/'))
  else:
   identity=U.GetMenuItemID(menu,index)
   if identity not in (0,0xffffffff):result.append(dict(id=identity,label=label.value,parent=parent))
 return result

def events():
 path=out/f'window-events-{child.info.pid}.jsonl'
 if not path.exists():return []
 data=path.read_bytes();lines=data.splitlines()
 if data and not data.endswith(b'\n'):lines=lines[:-1]
 return [json.loads(line) for line in lines]

def barrier():
 global barrier_serial
 if not lifecycle:return
 barrier_serial+=1;marker=barrier_serial;child.post(main,0x8000+77,child.info.pid,marker)
 child.wait(lambda:any(e['api']=='queue-barrier' and e['id']==marker for e in events()))

def launch(path=None):
 global child,main
 child=native.Child(exe,[] if empty_start else [path or assets/'image0.png'],station_name.value+'\\'+desktop_name)
 children.append(child)
 report['processes'].append(dict(pid=child.info.pid,graceful_exit=False))
 main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
 if not empty_start:child.wait(lambda:(path or assets/'image0.png').name in native.window_text(main))
 child.ping(main)
 if lifecycle:
  result=subprocess.run([str(observer/'attach_owned.exe'),str(child.info.pid),str(exe.resolve()),str((out/'observe.dll').resolve())],capture_output=True,timeout=25)
  (out/f'attach-{child.info.pid}.txt').write_bytes(result.stdout+result.stderr);assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
  assert any(e['api']=='Install' for e in events());barrier()

def configuration():
 paths=list(reference.rglob('MassiGra.cfg'));assert len(paths)==1
 raw=paths[0].read_bytes()
 if raw.startswith((b'\xff\xfe',b'\xfe\xff')):content=raw.decode('utf-16')
 else:
  try:content=raw.decode('utf-8-sig')
  except UnicodeDecodeError:content=raw.decode('cp932')
 parsed=configparser.ConfigParser(interpolation=None,strict=False);parsed.optionxform=str;parsed.read_string(content)
 return dict(path=paths[0].relative_to(reference).as_posix(),sha256=native.sha(paths[0]),window={key:parsed['Wnd'][key] for key in ['Rect','Show','Full','UserRect','WndCent','IniPos','IniShow','IfMax','IfFull']},other={key:value for key,value in parsed['Other'].items() if 'Auto' in key or 'Nx' in key})

def close_current():
 global child,main
 if K.WaitForSingleObject(child.info.process,0)!=0:child.post(main,0x10)
 assert K.WaitForSingleObject(child.info.process,10000)==0
 code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0
 report['processes'][-1].update(graceful_exit=True,exit_code=0,configuration=configuration())
 child=None;main=None

def slide_active():
 timers={}
 for event in events():
  if event['window']!=int(main):continue
  if event['api']=='SetTimer' and event['result']:timers[event['result']]=event['interval']
  elif event['api']=='KillTimer' and event['result']:timers.pop(event['id'],None)
 return 600000 in timers.values()

def current_state():
 barrier()
 rect=w.RECT();native.check(U.GetWindowRect(child.own(main),c.byref(rect)))
 return dict(pid=child.info.pid,rect=[rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top],full=not bool(U.GetWindowLongW(main,-16)&0x00c00000),maximized=bool(U.IsZoomed(main)),minimized=bool(U.IsIconic(main)),slideshow=slide_active())

def record(name,**fields):
 item=dict(name=name,state=current_state(),**fields);report['lifecycle_cases'].append(item);print('OBSERVED '+name,flush=True);return item['state']

def normal():
 if not U.GetWindowLongW(main,-16)&0x00c00000:child.send(main,0x111,40081)
 if U.IsZoomed(main) or U.IsIconic(main):child.send(main,0x112,0xf120)
 child.wait(lambda:bool(U.GetWindowLongW(main,-16)&0x00c00000) and not U.IsZoomed(main) and not U.IsIconic(main))

def set_check(parent,identity,value):
 control=child.own(U.GetDlgItem(parent,identity))
 if bool(child.send(control,0xf0))!=value:
  child.post(control,0xf5);child.wait(lambda:bool(child.send(control,0xf0))==value)

def configure(position=0,state=0,center=False,restore_max=True,restore_full=True):
 dialog,page=open_window_page();choose(page,2021,1);choose(page,2022,0)
 for identity,value in [(2031,37),(2032,49),(2033,620),(2034,420)]:
  value=c.create_unicode_buffer(str(value));child.send(child.own(U.GetDlgItem(page,identity)),0xc,0,c.addressof(value))
 for identity,value in [(2001,center),(2003,restore_max),(2004,restore_full)]:set_check(page,identity,value)
 choose(page,2021,position);choose(page,2022,state);button(dialog,1);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)

def slide(full):
 before=len(events())
 child.post(main,0x111,40072);dialog=child.wait(lambda:next((h for h in child.windows() if '自動スライド' in native.window_text(h) and U.GetDlgItem(h,2031)),None))
 for identity,value in [(2001,full),(2002,True),(2003,False),(2005,False)]:set_check(dialog,identity,value)
 value=c.create_unicode_buffer('600.0');child.send(child.own(U.GetDlgItem(dialog,2031)),0xc,0,c.addressof(value));button(dialog,1)
 child.wait(lambda:not U.IsWindow(dialog));child.wait(lambda:any(e['api']=='command-end' and e['id']==40072 for e in events()[before:]));barrier()
 report.setdefault('slideshow_timer_observations',[]).append(dict(pid=child.info.pid,fullscreen_option=full,events=events()[before:]))
 assert any(e['api']=='SetTimer' and e['window']==int(main) and e['interval']==600000 and e['result'] for e in events()[before:]),'No600s slideshow timer observed'
 child.wait(slide_active)

native.api(U,'GetMenuState',w.UINT,[native.P,w.UINT,w.UINT])
report.update(scope='Owned pinned official0.45 reference, synthetic PNG tree, observed original SetTimer/KillTimer/title messages and explicit owned WM_TIMER notifications. No clock/API-result replacement, global input, user windows or startup-speed comparison.',synthetic_timer_notifications=True,slideshow_cases=[])
flat=assets/'flat';flat.mkdir()
for index in range(4):Image.new('RGB',(64,48),(30+index*40,200-index*30,80+index*15)).save(flat/f'flat-{index}.png')
tree=assets/'tree'
for folder,name,color in [('', 'root-0',(100,20,30)),('a','a-0',(110,20,40)),('a','a-1',(120,20,50)),('a/c','c-0',(130,20,60)),('b','b-0',(140,20,70))]:
 directory=tree/folder;directory.mkdir(parents=True,exist_ok=True);Image.new('RGB',(64,48),color).save(directory/(name+'.png'))
known_files=[p.name for p in assets.rglob('*.png')]

def active_timer():
 timers={}
 for event in events():
  if event['window']!=int(main):continue
  if event['api']=='SetTimer' and event['result']:timers[event['result']]=event
  elif event['api']=='KillTimer' and event['result']:timers.pop(event['id'],None)
 return next((e for e in timers.values() if e['interval']==600000),None)

def sorts():
 menu=U.GetMenu(child.own(main));assert menu
 def find_sort(current,parent=''):
  for index in range(U.GetMenuItemCount(current)):
   label=c.create_unicode_buffer(512);U.GetMenuStringW(current,index,label,len(label),0x400)
   submenu=U.GetSubMenu(current,index)
   if not submenu:continue
   if '並び' in label.value or '並べ' in label.value:return submenu,index,parent+label.value+'/'
   found=find_sort(submenu,parent+label.value+'/')
   if found:return found
 found=find_sort(menu)
 if not found:report['unmatched_menu_rows']=menu_rows(menu)
 assert found,'Sort submenu was not found'
 submenu,index,parent=found
 child.send(main,0x117,submenu,index)
 result=[dict(id=row['id'],label=row['label'],parent=row['parent'],checked=bool(U.GetMenuState(submenu,row['id'],0)&8)) for row in menu_rows(submenu,parent)]
 assert result,'Sort submenu was not found'
 return result

def snapshot():
 barrier();title=native.window_text(child.own(main))
 file=next((name for name in known_files if name in title),None);assert file,title
 return dict(file=file,title=title,slideshow=active_timer() is not None,full=not bool(U.GetWindowLongW(main,-16)&0x00c00000),sorts=sorts())

def set_sort(random_order):
 rows=sorts();candidates=[row for row in rows if ('ランダム' in row['label'] if random_order else row['id']==40801 and '名前' in row['label'])]
 assert len(candidates)==1,candidates
 child.send(main,0x111,candidates[0]['id']);barrier()
 assert next(row for row in sorts() if row['id']==candidates[0]['id'])['checked']

def start_slide(options):
 before=len(events());child.post(main,0x111,40072)
 dialog=child.wait(lambda:next((h for h in child.windows() if '自動スライド' in native.window_text(h) and U.GetDlgItem(h,2031)),None))
 set_check(dialog,2001,False);set_check(dialog,2002,options['loop'])
 set_check(dialog,2003,True);child.wait(lambda:bool(U.IsWindowEnabled(child.own(U.GetDlgItem(dialog,2004)))))
 set_check(dialog,2004,options['subtree']);set_check(dialog,2003,options['next_folder']);set_check(dialog,2005,options['random'])
 actual={str(identity):bool(child.send(child.own(U.GetDlgItem(dialog,identity)),0xf0)) for identity in range(2001,2006)}
 seconds=c.create_unicode_buffer('600.0');child.send(child.own(U.GetDlgItem(dialog,2031)),0xc,0,c.addressof(seconds));button(dialog,1)
 child.wait(lambda:not U.IsWindow(dialog));child.wait(lambda:any(e['api']=='command-end' and e['id']==40072 for e in events()[before:]));barrier()
 return actual

def step():
 timer=active_timer();assert timer
 child.post(main,0x113,timer['result'],timer['callback']);barrier()
 return snapshot()

def stop():
 if active_timer() is not None:
  child.post(main,0x111,40072);child.wait(lambda:active_timer() is None);barrier()
 return snapshot()

def run_case(name,path,loop=False,next_folder=False,subtree=False,random_order=False,prior_random=False,steps=8):
 launch(path)
 if not U.GetMenu(main):child.send(main,0x111,41008)
 normal();set_sort(prior_random)
 options=dict(loop=loop,next_folder=next_folder,subtree=subtree,random=random_order)
 case=dict(name=name,pid=child.info.pid,options=options,before=snapshot(),steps=[])
 report['slideshow_cases'].append(case)
 case['actual_checks']=start_slide(options);case['started']=snapshot()
 for _ in range(steps):
  if active_timer() is None:break
  case['steps'].append(step())
 case['ended_naturally']=active_timer() is None;case['after']=stop();case['events']=events()
 close_current();case['graceful_exit']=report['processes'][-1]['graceful_exit']
 print('OBSERVED '+name+': '+','.join([case['started']['file']]+[x['file'] for x in case['steps']])+' natural_stop='+str(case['ended_naturally']),flush=True)
 return case

try:
 first=run_case('flat-stop',flat/'flat-0.png',steps=7)
 assert first['ended_naturally'] and len({x['file'] for x in first['steps']})>1,'Synthetic timer notification did not advance and stop'
 second=run_case('flat-loop',flat/'flat-0.png',loop=True,steps=9)
 assert not second['ended_naturally'] and len({x['file'] for x in second['steps']})==4
 run_case('last-file-stop',flat/'flat-3.png',steps=3)
 if '--matrix' in sys.argv[1:]:
  run_case('temporary-random-loop',flat/'flat-0.png',loop=True,random_order=True,steps=16)
  run_case('temporary-random-stop',flat/'flat-0.png',random_order=True,steps=7)
  run_case('prior-random-loop',flat/'flat-0.png',loop=True,prior_random=True,steps=16)
  run_case('subtree-stop',tree/'root-0.png',next_folder=True,subtree=True,steps=9)
  run_case('subtree-loop',tree/'root-0.png',loop=True,next_folder=True,subtree=True,steps=13)
  run_case('nested-subtree-stop',tree/'a/a-0.png',next_folder=True,subtree=True,steps=7)
  run_case('nested-subtree-loop',tree/'a/a-0.png',loop=True,next_folder=True,subtree=True,steps=10)
 report['graceful_exit']=all(p['graceful_exit'] for p in report['processes']);assert report['graceful_exit']
 report['source_and_binary_guards_passed']=all(native.sha(path)==digest for path,digest in source_files.items());assert report['source_and_binary_guards_passed']
 report['passed']=True
finally:
 report['observer_build']=observer_manifest
 for value in children:value.close()
 K.ReleaseMutex(mutex);K.CloseHandle(mutex)
 (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print('Evidence: '+str(out),flush=True)
