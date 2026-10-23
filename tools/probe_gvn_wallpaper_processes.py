"""Stock GVN commands; owned x64 child/desktop/clipboard; wallpaper setters virtualized."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,configparser,ctypes as c,hashlib,json,os,re,shutil,struct,subprocess,sys,time,uuid,winreg
from PIL import Image
import benchmark_navigation as native
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path)
parser.add_argument('--output',type=Path,default=root/'.cache/wallpaper-processes')
args=parser.parse_args();out=args.output.resolve()/('run-'+uuid.uuid4().hex+' 日本語');out.mkdir(parents=True)
observer=root/'.cache/gvn-wallpaper-observer';manifest=json.loads((observer/'build.json').read_text(encoding='utf-8-sig'))
assert manifest['architecture']=='x64' and manifest['wallpaper_setter_virtualized'] and manifest['wallpaper_mutex_virtualized']
assert all(native.sha(root/'tools/gvn_wallpaper_observer'/name)==digest for name,digest in manifest['sources'].items())
assert all(native.sha(observer/name)==digest for name,digest in manifest['binaries'].items())
exe=out/'GVN.exe';shutil.copyfile(args.executable,exe);shutil.copyfile(observer/'observer.dll',out/'observe.dll')
executable_hash=native.sha(exe);assert executable_hash==native.sha(args.executable)
sources={p:native.sha(p) for p in [Path(__file__),Path(native.__file__),root/'include/gvn/commands.h',*(root/'tools/gvn_wallpaper_observer').iterdir()] if p.is_file()}
ids={};number=0
for entry in (root/'include/gvn/commands.h').read_text().split('enum Command {',1)[1].split('};',1)[0].split(','):
 match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',entry)
 if not match:continue
 key,explicit=match.groups()
 if explicit:number=int(explicit)
 ids[key]=number;number+=1
assert [ids[k] for k in ['WallpaperCenter','WallpaperTile','WallpaperStretch','WallpaperWidth','WallpaperHeight','WallpaperDelete']]==list(range(301,307))
profile=out/'private 設定.ini';profile.write_text('[Viewer]\nallow_multiple=1\nreopen_last=0\nprefetch_enabled=0\nanimation_autoplay=0\nconfirm_unsaved=0\nsave_dialog=0\nmenu=1\n',encoding='utf-16')
(out/'virtual-desktop.ini').write_text('[Desktop]\nWallpaperStyle=0\nTileWallpaper=0\nWallpaper=\n',encoding='utf-16')
managed=out/'wallpaper';exports=out/'exports';exports.mkdir();inputs=out/'inputs';inputs.mkdir();temporary=out/'temp';temporary.mkdir()
originals={};fixture=root/'tests/fixtures/wallpaper-reference'
rows=[line.split('\t') for line in (fixture/'cases.tsv').read_text().splitlines()[1:]];assert len(rows)==40
for source in sorted({row[2] for row in rows}):
 target=inputs/source;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(fixture/source,target);originals[target]=native.sha(target)
for name in ['alpha.png','psd-16-3.psd','disposal.gif','blend.apng']:
 target=inputs/name;shutil.copyfile(root/'tests/fixtures'/name,target);originals[target]=native.sha(target)

def desktop_hash():
 with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Control Panel\Desktop',0,winreg.KEY_READ) as key:
  values={}
  for name in ['Wallpaper','WallpaperStyle','TileWallpaper']:
   try:values[name]=winreg.QueryValueEx(key,name)
   except FileNotFoundError:values[name]=None
 return hashlib.sha256(json.dumps(values,ensure_ascii=False,sort_keys=True).encode()).hexdigest()
desktop_before=desktop_hash();user_profile=Path(os.environ['LOCALAPPDATA'])/'GVN/settings.ini';profile_hash=native.sha(user_profile) if user_profile.exists() else None
U,K=native.U,native.K
for name,result,arguments in [('IsWindow',w.BOOL,[native.P]),('GetMenu',native.P,[native.P]),('GetSubMenu',native.P,[native.P,c.c_int]),('GetMenuItemCount',c.c_int,[native.P]),('GetMenuItemID',w.UINT,[native.P,c.c_int]),('GetMenuState',w.UINT,[native.P,w.UINT,w.UINT])]:native.api(U,name,result,arguments)
native.api(K,'GetExitCodeProcess',w.BOOL,[native.P,c.POINTER(w.DWORD)])
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station);flags=(w.DWORD*3)();needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
station_name=c.create_unicode_buffer(512);native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)));native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Wallpaper-'+uuid.uuid4().hex;native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
children=[];child=None;main=None;active_record=None
barrier_serial=0
report=dict(passed=False,scope='Unmodified stock x64 GVN copied byte-for-byte; normal App/native platform, real menus/dialogs and managed files. Owned private station/desktop/clipboard/profile only. Observer substitutes Desktop registry and SPI setter boundary and private wallpaper mutex; physical desktop rendering is unverified.',executable_sha256=executable_hash,tool_sha256=native.sha(__file__),observer_build=manifest,noninteractive=True,real_wallpaper_setter_called=False,processes=[],cases=[])

def case(name,**details):
 report['cases'].append(dict(name=name,passed=True,**details));print('PASS '+name,flush=True)

def events(which=None):
 process=which or child;path=out/f'wallpaper-events-{process.info.pid}.jsonl'
 if not path.exists():return []
 data=path.read_bytes();lines=data.splitlines()
 if data and not data.endswith(b'\n'):lines=lines[:-1]
 return [json.loads(line) for line in lines]

def all_events():return [item for process in children for item in events(process)]

def barrier():
 global barrier_serial
 barrier_serial+=1;marker=str(barrier_serial)
 child.post(main,0x8000+77,child.info.pid,barrier_serial)
 child.wait(lambda:any(e['api']=='queue-barrier' and e['name']==marker for e in events()))

def files():return sorted(managed.glob('GVN-*.bmp')) if managed.exists() else []

def state():
 ini=configparser.ConfigParser(interpolation=None);ini.read(out/'virtual-desktop.ini',encoding='utf-16')
 return dict(ini['Desktop']) if ini.has_section('Desktop') else dict(tilewallpaper='0',wallpaperstyle='0',wallpaper='')

def dialog():return next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770'),None)

def press(window,button):
 target=child.wait(lambda:U.GetDlgItem(window,button));child.post(child.own(target),0xf5);child.wait(lambda:not U.IsWindow(window));child.ping(main)
 if events():barrier()

def flattened(image):
 result=image.convert('RGBA');result.putdata([tuple((color*a+128*(255-a))//255 for color in (r,g,b))+(255,) for r,g,b,a in result.getdata()]);return result

def start(source=None,anonymous=False):
 global child,main,active_record
 previous={key:os.environ.get(key) for key in ['TEMP','TMP']}
 os.environ['TEMP']=os.environ['TMP']=str(temporary)
 try:child=native.Child(exe,['--settings',profile]+([source] if source else []),station_name.value+'\\'+desktop_name)
 finally:
  for key,value in previous.items():
   if value is None:os.environ.pop(key,None)
   else:os.environ[key]=value
 children.append(child);active_record=dict(pid=child.info.pid,source=str(source) if source else None,graceful_exit=False);report['processes'].append(active_record)
 main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None))
 if source:child.wait(lambda:source.name in native.window_text(main))
 child.ping(main)
 if anonymous:
  copy_rgba();child.send(main,0x111,ids['NewPaste']);child.ping(main)
  assert source.name not in native.window_text(main),'NewPaste retained original file title'
  active_record['anonymous_before_observer']=True
 # No wallpaper command is sent until Install and all required IAT hooks are proven.
 result=subprocess.run([str(observer/'attach_owned.exe'),str(child.info.pid),str(exe.resolve()),str((out/'observe.dll').resolve())],capture_output=True,timeout=25)
 (out/f'attach-{child.info.pid}.txt').write_bytes(result.stdout+result.stderr);assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
 installed=events();assert installed and any(e['api']=='Install' for e in installed) and any(e['api']=='window-dispatch-observer' for e in installed)
 hooks={e['name'] for e in installed if e['api']=='hook'}
 assert {'SystemParametersInfoW','RegOpenKeyExW','RegCreateKeyExW','RegQueryValueExW','RegSetValueExW','RegCloseKey','RegDeleteValueW','CreateFileW','DeleteFileW','CreateMutexW'}<=hooks
 active_record['installed_hooks']=sorted(hooks)
 if not U.GetMenu(main):child.send(main,0x111,ids['ToggleMenu'])
 assert U.GetMenu(main);barrier();return child,main,active_record

def close(expected_exit=0):
 child.post(main,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0,'Owned GVN did not exit gracefully'
 code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)))
 active_record.update(exit_code=code.value,expected_exit=expected_exit)
 assert code.value==expected_exit,(code.value,expected_exit)
 active_record.update(graceful_exit=True)

def copy_rgba():
 sequence=U.GetClipboardSequenceNumber();child.post(main,0x111,ids['Copy']);child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(17));child.wait(lambda:U.OpenClipboard(None))
 try:
  handle=U.GetClipboardData(17);pointer=K.GlobalLock(handle);native.check(pointer)
  try:data=c.string_at(pointer,K.GlobalSize(handle))
  finally:K.GlobalUnlock(handle)
 finally:U.CloseClipboard()
 header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',data);assert header==124 and planes==1 and bits==32 and compression==3
 result=Image.frombytes('RGBA',(width,abs(height)),data[header:],'raw','BGRA',width*4,-1 if height>0 else 1)
 if events():barrier()
 return result

def menu(identity):
 root_menu=U.GetMenu(child.own(main))
 def find(parent):
  for i in range(U.GetMenuItemCount(parent)):
   if U.GetMenuItemID(parent,i)==identity:return parent
   sub=U.GetSubMenu(parent,i)
   if sub:
    result=find(sub)
    if result:return result
 target=find(root_menu);assert target,identity;child.send(main,0x117,target,0)
 result=U.GetMenuState(target,identity,0);assert result!=0xffffffff;return result

def selection():
 child.post(main,0x111,ids['SelectNumbers']);window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h)=='範囲選択' and U.GetDlgItem(h,101)),None))
 text=c.create_unicode_buffer('1, 1, 2, 2');child.send(child.own(U.GetDlgItem(window,101)),0xc,0,c.addressof(text));press(window,1)

def set_wallpaper(mode,label,expected=None,bits=None,selection_ignored=False):
 identity=ids['WallpaperCenter']+mode;assert not menu(identity)&(3|8)
 before=len(events());sequence=U.GetClipboardSequenceNumber();child.post(main,0x111,identity)
 child.wait(lambda:any(e['api']=='virtual-wallpaper-applied' for e in events()[before:]));barrier()
 assert dialog() is None,(label,'Unexpected modal dialog')
 rows=events()[before:];assert not any(e['api'].startswith('blocked-') for e in rows),(label,rows)
 setters=[e for e in rows if e['api']=='SystemParametersInfoW'];assert len(setters)==1 and setters[0]['name']=='20' and setters[0]['number']==3
 writes={e['name']:e['value'] for e in rows if e['api']=='RegSetValueExW'}
 tile,style=[('0','0'),('1','0'),('0','2'),('0','10'),('0','6')][mode]
 assert writes==dict(TileWallpaper=tile,WallpaperStyle=style),(label,writes)
 locks=[e for e in rows if e['api']=='CreateMutexW'];assert len(locks)==1 and locks[0]['name']=='Local\\GVN.Wallpaper' and locks[0]['value'].startswith('Local\\GVN.Wallpaper.Observer.')
 path=Path(setters[0]['value']);assert path.parent==managed and path in files()
 assert state()==dict(tilewallpaper=tile,wallpaperstyle=style,wallpaper=str(path))
 actual=Image.open(path).convert('RGB');target=exports/(label+'.bmp');shutil.copyfile(path,target)
 if expected is not None:
  assert actual.size==expected.size and actual.tobytes()==expected.convert('RGB').tobytes(),label
 if bits is not None:assert struct.unpack_from('<H',target.read_bytes(),28)[0]==bits
 assert U.GetClipboardSequenceNumber()==sequence,'Wallpaper command changed clipboard'
 assert not menu(ids['WallpaperDelete'])&3 and all(not menu(ids['WallpaperCenter']+i)&8 for i in range(5))
 case(label,pid=child.info.pid,mode=mode,spi_action=20,spi_flags=3,registry_values=writes,bmp=str(target.relative_to(out)),bmp_sha256=native.sha(target),dimensions=list(actual.size),selection_ignored_proven=selection_ignored,full_rgb_verified=expected is not None)
 return path

def confirm_delete(yes):
 before=len(events());snapshot={p:native.sha(p) for p in files()};child.post(main,0x111,ids['WallpaperDelete']);window=child.wait(dialog)
 press(window,6 if yes else 7)
 if yes:
  assert not files() and state()==dict(tilewallpaper='0',wallpaperstyle='0',wallpaper='')
  rows=events()[before:];setter=[e for e in rows if e['api']=='SystemParametersInfoW'];assert len(setter)==1 and setter[0]['value']=='' and setter[0]['name']=='20' and setter[0]['number']==3
 else:assert {p:native.sha(p) for p in files()}==snapshot and not any(e['api']=='SystemParametersInfoW' for e in events()[before:])
 case('delete_yes' if yes else 'delete_no',pid=child.info.pid,files_before=len(snapshot),files_after=len(files()))

try:
 start();assert all(menu(ids['WallpaperCenter']+i)&3 for i in range(6));before=len(events())
 child.send(main,0x111,ids['WallpaperCenter']);child.send(main,0x111,ids['WallpaperDelete'])
 assert [(e['api'],e['name']) for e in events()[before:]]==[('WM_COMMAND-completed',str(ids['WallpaperCenter'])),('WM_COMMAND-completed',str(ids['WallpaperDelete']))]
 case('empty_start_disables_modes_and_delete_without_system_calls',pid=child.info.pid);close()
 for source in sorted({row[2] for row in rows}):
  subset=[row for row in rows if row[2]==source];start(inputs/source)
  reference=Image.open(inputs/source).convert('RGBA');assert copy_rgba().tobytes()==reference.tobytes()
  selected=source.startswith('alpha/') or source.startswith('selection/')
  if selected:selection()
  expected_copy=reference.crop((1,1,3,3)) if selected else reference
  assert copy_rgba().tobytes()==expected_copy.tobytes()
  for row in subset:
   with Image.open(fixture/row[3]) as expected:set_wallpaper(int(row[1]),'reference-'+row[0],expected,int(row[6]),selected)
   assert len(files())==1 and copy_rgba().tobytes()==expected_copy.tobytes()
  case('reference_source_and_selection_rgba_preserved_'+source.replace('/','-'),pid=child.info.pid);close()
 # Real required-format frames and anonymous clipboard data use the same default API boundary.
 sys.path.insert(0,str(root/'tools'));from apng_oracle import render
 for name in ['psd-16-3.psd','disposal.gif','blend.apng']:
  start(inputs/name);frames=range(2) if name!='psd-16-3.psd' else range(1)
  for frame in frames:
   if frame:child.send(main,0x111,ids['FrameNext'])
   original=copy_rgba();expected=original.copy();expected.putdata([tuple((color*a+128*(255-a))//255 for color in (r,g,b))+(255,) for r,g,b,a in original.getdata()])
   if name=='blend.apng':assert original.tobytes()==render((inputs/name).read_bytes())[frame][0].tobytes()
   for mode in range(5):set_wallpaper(mode,name+f'-frame{frame}-mode{mode}',expected)
   assert copy_rgba().tobytes()==original.tobytes()
  close()
 start(inputs/'alpha.png',True);original=copy_rgba();assert original.tobytes()==Image.open(inputs/'alpha.png').convert('RGBA').tobytes()
 set_wallpaper(2,'anonymous-clipboard-stretch',flattened(original));assert copy_rgba().tobytes()==original.tobytes()
 # Failed native SPI must trigger the real manager's rollback before the owned error dialog.
 previous=state();previous_files={p:native.sha(p) for p in files()};before=len(events());(out/'fail-next-apply').write_text('owned one-shot failure')
 child.post(main,0x111,ids['WallpaperTile']);window=child.wait(dialog);press(window,1)
 assert state()==previous and {p:native.sha(p) for p in files()}==previous_files
 assert any(e['api']=='injected-apply-failure' for e in events()[before:])
 assert len([e for e in events()[before:] if e['api']=='SystemParametersInfoW'])==2
 assert copy_rgba().tobytes()==original.tobytes();case('native_spi_failure_rolls_back_files_styles_path_and_alpha',pid=child.info.pid)
 confirm_delete(False);close(expected_exit=1) # Normal GVN preserves the handled-error exit status.
 # A second stock process sees files left by the first, even with no image.
 start();assert all(menu(ids['WallpaperCenter']+i)&3 for i in range(5)) and not menu(ids['WallpaperDelete'])&3
 case('graceful_restart_empty_process_detects_existing_generated_file',pid=child.info.pid);confirm_delete(True);close()
 # The currently opened managed BMP is retained when another BMP is generated from it.
 start(inputs/'alpha.png');generated=set_wallpaper(0,'managed-input-first',flattened(Image.open(inputs/'alpha.png')));close();original_hash=native.sha(generated)
 start(generated);set_wallpaper(4,'managed-input-retained',Image.open(generated));assert generated in files() and native.sha(generated)==original_hash and len(files())==2
 child.send(main,0x111,ids['CloseImage']);assert menu(ids['WallpaperCenter'])&3 and not menu(ids['WallpaperDelete'])&3
 case('managed_input_not_overwritten_or_removed_and_close_keeps_delete',pid=child.info.pid);confirm_delete(True);close()
 # Two live stock instances share virtual Desktop state and a private operation mutex.
 first=start(inputs/'alpha.png');second=start(inputs/'alpha.png');one,one_window,one_record=first;two,two_window,two_record=second
 one.post(one_window,0x111,ids['WallpaperWidth']);two.post(two_window,0x111,ids['WallpaperHeight'])
 one.wait(lambda:any(e['api']=='virtual-wallpaper-applied' for e in events(one)));two.wait(lambda:any(e['api']=='virtual-wallpaper-applied' for e in events(two)))
 child,main,active_record=first;barrier();child,main,active_record=second;barrier();assert len(files())==1
 assert state()['wallpaper'] in {e['name'] for p in [one,two] for e in events(p) if e['api']=='virtual-wallpaper-applied'}
 assert Image.open(files()[0]).convert('RGB').tobytes()==flattened(Image.open(inputs/'alpha.png')).convert('RGB').tobytes()
 assert not menu(ids['WallpaperDelete'])&3
 case('two_live_instances_serialize_private_mutex_and_share_one_generated_file',pids=[one.info.pid,two.info.pid]);confirm_delete(True);close()
 child,main,active_record=first;assert menu(ids['WallpaperDelete'])&3;case('other_instance_menu_refresh_observes_delete',pid=child.info.pid);close()
 report['passed']=True
finally:
 report['events']={str(process.info.pid):events(process) for process in children}
 report['owned_dialogs_at_exit']=[]
 for process in children:
  for window in process.windows():
   if native.window_text(window,True)!='#32770':continue
   texts=[]
   @native.Callback
   def collect(control,_):
    owner=w.DWORD();U.GetWindowThreadProcessId(control,c.byref(owner))
    if owner.value==process.info.pid:texts.append(native.window_text(control))
    return True
   U.EnumChildWindows(process.own(window),collect,0)
   report['owned_dialogs_at_exit'].append(dict(pid=process.info.pid,title=native.window_text(window),texts=texts))
 for process in children:process.close()
 report.update(desktop_registry_unchanged=desktop_hash()==desktop_before,user_gvn_profile_unchanged=(native.sha(user_profile) if user_profile.exists() else None)==profile_hash,original_sources_unchanged=all(native.sha(path)==digest for path,digest in originals.items()),source_and_binary_guards_passed=all(native.sha(path)==digest for path,digest in sources.items()) and native.sha(exe)==executable_hash and native.sha(out/'observe.dll')==manifest['binaries']['observer.dll'])
 report['all_processes_graceful']=bool(report['processes']) and all(item['graceful_exit'] for item in report['processes'])
 (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 K.ReleaseMutex(mutex);K.CloseHandle(mutex)
 print('Evidence: '+str(out),flush=True)
assert report['passed'] and report['desktop_registry_unchanged'] and report['user_gvn_profile_unchanged'] and report['original_sources_unchanged'] and report['source_and_binary_guards_passed'] and report['all_processes_graceful']
