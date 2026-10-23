"""Observe an owned official child; wallpaper/registry writes are virtualized before commands."""
from pathlib import Path
from ctypes import wintypes as w
import ctypes as c
import hashlib,json,shutil,struct,sys,time,uuid,winreg,zipfile
from PIL import Image
import benchmark_navigation as native
import subprocess

root=Path(__file__).resolve().parents[1]
scenario=sys.argv[1] if len(sys.argv)>1 else 'opaque'
assert scenario in {'opaque','wide','portrait','square','alpha','indexed','selection','close-image','empty'}
out=root/'.cache/wallpaper-reference'/('run-'+uuid.uuid4().hex);out.mkdir(parents=True)
archive=root/'.cache/massigra-reference/MassiGra045.zip'
assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
build=root/'.cache/reference-wallpaper-observer';manifest=json.loads((build/'build.json').read_text(encoding='utf-8-sig'))
assert manifest['architecture']=='x86' and manifest['wallpaper_setter_virtualized'] and not manifest['clock_replacement']
sources={p:native.sha(p) for p in [Path(__file__),Path(native.__file__),*(root/'tools/reference_wallpaper').iterdir()] if p.is_file()}
assert all(native.sha(root/'tools/reference_wallpaper'/name)==digest for name,digest in manifest['sources'].items())
assert all(native.sha(build/name)==digest for name,digest in manifest['binaries'].items())
reference=out/'reference'
with zipfile.ZipFile(archive) as package:
 for name in package.namelist():
  if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
   target=(reference/name).resolve();assert target.is_relative_to(reference.resolve())
   target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(package.read(name))
exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
shutil.copyfile(build/'observer.dll',out/'observe.dll')
assert native.sha(out/'observe.dll')==manifest['binaries']['observer.dll']
asset=out/'source.png';size={'wide':(43,11),'portrait':(11,43),'square':(17,17)}.get(scenario,(29,19))
image=Image.new('RGBA' if scenario=='alpha' else 'P' if scenario=='indexed' else 'RGB',size)
colors=[((x*31+y*11+20)%256,(x*13+y*29+40)%256,(x*17+y*19+60)%256) for y in range(size[1]) for x in range(size[0])]
if scenario=='alpha':image.putdata([(*rgb,[0,1,64,127,128,254,255][i%7]) for i,rgb in enumerate(colors)])
elif scenario=='indexed':
 image.putpalette([v for i in range(256) for v in [(i*37)%256,(i*71)%256,(i*109)%256]])
 image.putdata([(x+y*3)%16 for y in range(size[1]) for x in range(size[0])])
else:image.putdata(colors)
image.save(asset,**({'bits':4} if scenario=='indexed' else {}));asset_hash=native.sha(asset)
def desktop_registry_hash():
 with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Control Panel\Desktop',0,winreg.KEY_READ) as key:
  values={}
  for name in ['Wallpaper','WallpaperStyle','TileWallpaper']:
   try:values[name]=winreg.QueryValueEx(key,name)
   except FileNotFoundError:values[name]=None
 return hashlib.sha256(json.dumps(values,ensure_ascii=False,sort_keys=True).encode('utf-8')).hexdigest()
before=desktop_registry_hash()
U,K=native.U,native.K
for name,result,args in [('IsWindow',w.BOOL,[native.P]),('GetMenu',native.P,[native.P]),('GetSubMenu',native.P,[native.P,c.c_int]),('GetMenuItemCount',c.c_int,[native.P]),('GetMenuItemID',w.UINT,[native.P,c.c_int]),('GetMenuState',w.UINT,[native.P,w.UINT,w.UINT])]:native.api(U,name,result,args)
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
station_name=c.create_unicode_buffer(512);native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Wallpaper-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
child=None
report=dict(passed=False,scenario=scenario,reference_sha256=native.sha(exe),tool_sha256=native.sha(__file__),observer_build=manifest,noninteractive=True,real_wallpaper_setter_called=False,scope='Actual official0.45 command/menu/file behavior. Observer redirects AppData to private output, virtualizes desktop registry and SPI wallpaper setters, denies COM activation and file mutations outside owned root. Physical desktop appearance is not captured.',cases=[])
events_file=out/'wallpaper-events.jsonl'
def events():
 if not events_file.exists():return []
 data=events_file.read_bytes();lines=data.splitlines()
 if data and not data.endswith(b'\n'):lines=lines[:-1]
 return [json.loads(line.decode('utf-8')) for line in lines]
def dialog():
 return next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770'),None)
def press(window,accepted):
 def button():
  yes=U.GetDlgItem(window,6);no=U.GetDlgItem(window,7)
  return yes if accepted and yes else no if not accepted and no else U.GetDlgItem(window,1 if accepted else 2)
 target=child.wait(button)
 child.post(child.own(target),0xf5);child.wait(lambda:not U.IsWindow(window))
def menu_state(identity):
 menu=U.GetMenu(child.own(main));child.send(main,0x116,menu)
 def find(parent):
  for i in range(U.GetMenuItemCount(parent)):
   if U.GetMenuItemID(parent,i)==identity:return parent,i
   sub=U.GetSubMenu(parent,i)
   if sub:
    result=find(sub)
    if result:return result
 target=find(menu);assert target,identity
 child.send(main,0x117,target[0],0)
 state=U.GetMenuState(target[0],identity,0);assert state!=0xffffffff
 return state
def selection():
 child.post(main,0x111,40023)
 window=child.wait(lambda:next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770' and '選択' in native.window_text(h)),None))
 child.wait(lambda:all(U.GetDlgItem(window,key) for key in [2031,2032,2033,2034,2035,2036,1,2]));child.ping(window)
 for key,value in zip([2031,2032,2035,2036],[3,2,11,7]):
  field=child.own(U.GetDlgItem(window,key));child.send(window,0x28,field,1)
  text=c.create_unicode_buffer(str(value));child.send(field,0xc,0,c.addressof(text));child.send(window,0x28,0,0)
 child.post(child.own(U.GetDlgItem(window,1)),0xf5);child.wait(lambda:not U.IsWindow(window))
try:
 child=native.Child(exe,[] if scenario=='empty' else [asset],station_name.value+'\\'+desktop_name)
 main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
 if scenario!='empty':child.wait(lambda:'source.png' in native.window_text(main))
 child.ping(main)
 if not U.GetMenu(main):child.send(main,0x111,41008)
 assert U.GetMenu(main)
 subprocess.run([str(build/'attach_owned.exe'),str(child.info.pid),str(exe.resolve()),str((out/'observe.dll').resolve())],check=True,timeout=25)
 installed=events();assert installed and installed[-1]['api']=='Install'
 report['controlled_pid']=child.info.pid;report['installed_hooks']=[e['name'] for e in installed if e['api']=='hook']
 report['delete_initially_disabled']=bool(menu_state(41204)&3);assert report['delete_initially_disabled']
 report['initial_mode_checks']={str(key):bool(menu_state(key)&8) for key in [41201,41202,41203,41205,41206]}
 if scenario=='selection':selection();report['selection_xywh']=[3,2,11,7]
 exported=out/'exported';exported.mkdir()
 modes=[('center',41201),('tile',41202),('stretch',41203),('width',41205),('height',41206)]
 if scenario=='empty':
  report['all_modes_disabled_without_image']=all(menu_state(identity)&3 for _,identity in modes)
  assert report['all_modes_disabled_without_image']
 for name,identity in ([] if scenario=='empty' else modes):
  assert not menu_state(identity)&3,(name,'Disabled command')
  start=len(events());child.post(main,0x111,identity)
  def outcome():
   rows=events()[start:]
   d=dialog()
   return ('dialog',d) if d else ('done',rows) if any(e['api'] in {'SystemParametersInfoW','SystemParametersInfoA'} for e in rows) else None
  result=child.wait(outcome)
  if result[0]=='dialog':
   report.setdefault('set_dialogs',[]).append(dict(mode=name,title=native.window_text(result[1])))
   press(result[1],True);child.wait(lambda:any(e['api'] in {'SystemParametersInfoW','SystemParametersInfoA'} for e in events()[start:]))
  child.ping(main);rows=events()[start:]
  blocked=[e for e in rows if e['api'].startswith('blocked-')];assert not blocked,(name,blocked)
  setters=[e for e in rows if e['api'] in {'SystemParametersInfoW','SystemParametersInfoA'}];assert len(setters)==1,(name,setters)
  setter=setters[0];path=Path(setter['value']).resolve();assert path.is_relative_to(out.resolve()) and path.is_file()
  result_image=Image.open(path).convert('RGB');copy=exported/(name+'.bmp');shutil.copyfile(path,copy)
  values={e['name']:e['value'] for e in rows if e['api']=='RegSetValueExW'}
  bmp=copy.read_bytes();header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',bmp,14)
  report['cases'].append(dict(mode=name,command=identity,passed=True,spi_action=int(setter['name']),spi_flags=setter['number'],registry_values=values,generated_path=path.relative_to(out.resolve()).as_posix(),output_size=list(result_image.size),bmp_header=header,bmp_bits=bits,bmp_compression=compression,source_rgb_exact=result_image.size==image.size and result_image.tobytes()==image.convert('RGB').tobytes(),bmp_sha256=native.sha(copy),delete_enabled_after_set=not bool(menu_state(41204)&3),events=rows))
  assert report['cases'][-1]['delete_enabled_after_set'];print('PASS '+name,flush=True)
  report['cases'][-1]['mode_checks']={str(key):bool(menu_state(key)&8) for key in [41201,41202,41203,41205,41206]}
 if scenario=='close-image':
  child.send(main,0x111,40030);child.ping(main)
  report['all_modes_disabled_after_close']=all(menu_state(identity)&3 for _,identity in modes)
  report['delete_enabled_after_close']=not bool(menu_state(41204)&3)
  assert report['all_modes_disabled_after_close'] and report['delete_enabled_after_close']
 if scenario!='empty':
  wallpaper=path;start=len(events());child.post(main,0x111,41204)
  result=child.wait(lambda:('dialog',d) if (d:=dialog()) else ('deleted',None) if not wallpaper.exists() else None)
  report['delete_confirmation']=result[0]=='dialog'
  if result[0]=='dialog':
   report['delete_dialog_title']=native.window_text(result[1]);before_cancel=native.sha(wallpaper);press(result[1],False);assert wallpaper.exists() and native.sha(wallpaper)==before_cancel
   report['delete_cancel_keeps_generated_file']=True;child.post(main,0x111,41204);d=child.wait(dialog);press(d,True)
  child.wait(lambda:not wallpaper.exists());child.ping(main)
  report['delete_events']=events()[start:];assert not any(e['api'].startswith('blocked-') for e in report['delete_events'])
 report['delete_finally_disabled']=bool(menu_state(41204)&3);assert report['delete_finally_disabled']
 assert native.sha(asset)==asset_hash
 child.post(main,0x10);assert K.WaitForSingleObject(child.info.process,5000)==0
 report['graceful_exit']=True;report['passed']=True
except BaseException as error:
 report['error']=type(error).__name__+': '+str(error)
 if child:report['owned_windows_at_failure']=[dict(title=native.window_text(h),cls=native.window_text(h,True)) for h in child.windows()]
 raise
finally:
 if child:child.close()
 report['desktop_registry_unchanged']=desktop_registry_hash()==before
 report['source_and_binary_guards_passed']=all(native.sha(p)==digest for p,digest in sources.items()) and all(native.sha(build/name)==digest for name,digest in manifest['binaries'].items()) and native.sha(out/'observe.dll')==manifest['binaries']['observer.dll'] and native.sha(exe)==report['reference_sha256'] and native.sha(asset)==asset_hash
 report['events']=events()
 copies=out/'sources';copies.mkdir()
 for p in sources:shutil.copyfile(p,copies/p.name)
 (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print('Evidence: '+str(out),flush=True);K.ReleaseMutex(mutex);K.CloseHandle(mutex)
 assert report['desktop_registry_unchanged'] and report['source_and_binary_guards_passed']
