"""Actual normal GVN zoom/settings dialogs on an owned private desktop; no global input."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,configparser,ctypes as c,json,re,shutil,struct,uuid
from PIL import Image
import benchmark_navigation as native

parser=argparse.ArgumentParser()
parser.add_argument('executable',type=Path)
parser.add_argument('--output',type=Path,default=Path('.cache/zoom-processes'))
args=parser.parse_args();root=Path(__file__).resolve().parents[1]
out=args.output.resolve()/('run-'+uuid.uuid4().hex);out.mkdir(parents=True)
exe=args.executable.resolve();U,K=native.U,native.K
for name,result,arguments in [('IsWindow',w.BOOL,[native.P]),('IsWindowEnabled',w.BOOL,[native.P]),
                              ('GetParent',native.P,[native.P])]:native.api(U,name,result,arguments)
for name,result,arguments in [
 ('GetExitCodeProcess',w.BOOL,[native.P,c.POINTER(w.DWORD)]),
 ('VirtualAllocEx',native.P,[native.P,native.P,native.UP,w.DWORD,w.DWORD]),
 ('VirtualFreeEx',w.BOOL,[native.P,native.P,native.UP,w.DWORD]),
 ('ReadProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P])]:native.api(K,name,result,arguments)

def enum(text):
 result={};value=0
 for item in text.split(','):
  match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
  if not match:continue
  key,number=match.groups()
  if number:value=int(number)
  result[key]=value;value+=1
 return result

ids=enum((root/'include/gvn/commands.h').read_text().split('enum Command {',1)[1].split('};',1)[0])
fields=enum((root/'include/gvn/preferences.h').read_text().split('enum : int {',1)[1].split('};',1)[0])
assert fields['ZoomMode']==255 and fields['KeepZoom']==215
fixture=root/'tests/fixtures/zoom-step-reference/cases.tsv'
manifest=json.loads((fixture.parent/'manifest.json').read_text())
assert native.sha(fixture)==manifest['files']['cases.tsv']
rows=[line.split('\t') for line in fixture.read_text().splitlines()];assert len(rows)==474
(out/'sources').mkdir()
for source in [Path(__file__),Path(native.__file__)]:shutil.copyfile(source,out/'sources'/source.name)
asset=out/'日本語 Alpha source.png';original=Image.new('RGBA',(3,2))
original.putdata([(7,11,13,0),(31,59,61,1),(67,71,73,127),(79,83,89,128),(97,101,103,254),(107,109,113,255)])
original.save(asset)
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
station_name=c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Zoom-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None)
native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
desktop=station_name.value+'\\'+desktop_name
children=[];child=None;window=None
report=dict(passed=False,scope='Normal GVN processes; actual numeric and settings dialogs, WM_COMMAND routes, same private profile before/after graceful restart, full RGBA. Own noninteractive station/desktop/clipboard only; no ComputerUse/Sky/global input or startup speed comparison.',
 executable_sha256=native.sha(exe),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),
 fixture_sha256=native.sha(fixture),asset_sha256=native.sha(asset),processes=[],cases=[],transitions=[])

def case(name,**details):
 report['cases'].append(dict(name=name,passed=True,**details));print('PASS '+name,flush=True)

def start(profile):
 global child,window
 child=native.Child(exe,['--settings',profile,asset],desktop);children.append(child)
 report['processes'].append(dict(pid=child.info.pid,graceful_exit=False))
 window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None))
 child.wait(lambda:asset.name in native.window_text(window));child.ping(window)

def close():
 child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0,'Owned GVN did not close'
 code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0,code.value
 report['processes'][-1].update(graceful_exit=True,exit_code=code.value)
 child.close();children.remove(child)

def text(control):
 value=c.create_unicode_buffer(4096);child.send(control,0xd,len(value),c.addressof(value));return value.value

def button(parent,id):
 control=child.own(U.GetDlgItem(parent,id));child.post(control,0xf5)

def numeric():
 child.post(window,0x111,ids['ZoomNumber'])
 return child.wait(lambda:next((h for h in child.windows() if native.window_text(h)=='表示倍率' and U.GetDlgItem(h,101) and U.GetDlgItem(h,1)),None))

def percent():
 title=native.window_text(window);dialog=numeric();value=float(text(child.own(U.GetDlgItem(dialog,101))))
 button(dialog,2);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
 assert title==native.window_text(window),'Reading zoom changed the image title'
 return value

def set_percent(value):
 dialog=numeric();control=child.own(U.GetDlgItem(dialog,101));buffer=c.create_unicode_buffer(str(value))
 child.send(control,0xc,0,c.addressof(buffer));assert text(control)==str(value)
 button(dialog,1);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
 actual=percent();assert abs(actual-float(value))<1e-7,(value,actual)

def settings():
 child.post(window,0x111,ids['Preferences'])
 dialog=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='#32770' and U.GetDlgItem(h,100)),None))
 tab=child.own(U.GetDlgItem(dialog,100));remote=K.VirtualAllocEx(child.info.process,None,32,0x3000,4);native.check(remote)
 try:
  native.check(child.send(tab,0x130a,4,remote));rectangle=(c.c_int*4)()
  native.check(K.ReadProcessMemory(child.info.process,remote,rectangle,16,None))
  x,y=(rectangle[0]+rectangle[2])//2,(rectangle[1]+rectangle[3])//2
  child.post(tab,0x201,1,x|y<<16);child.post(tab,0x202,0,x|y<<16)
 finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
 child.wait(lambda:child.send(tab,0x130b)==4)
 page=child.wait(lambda:U.GetDlgItem(dialog,1004))
 child.wait(lambda:all(U.GetDlgItem(page,fields[name]) for name in ['ZoomMode','ZoomStep','ZoomBelow','ZoomAbove','ZoomSimple','KeepZoom','ZoomReduce','ZoomEnlarge','DragSimple']))
 child.ping(dialog)
 return dialog,child.own(page)

def control(page,name):return child.own(U.GetDlgItem(page,fields[name]))

def choose(page,mode):
 combo=control(page,'ZoomMode');assert child.send(combo,0x14e,mode)==mode
 child.send(page,0x111,fields['ZoomMode']|(1<<16),combo)
 assert bool(U.IsWindowEnabled(control(page,'ZoomStep')))==(mode==0)
 assert bool(U.IsWindowEnabled(control(page,'ZoomBelow')))==(mode==1)
 assert bool(U.IsWindowEnabled(control(page,'ZoomAbove')))==(mode==1)

def set_field(page,name,value):
 field=control(page,name);assert U.IsWindowEnabled(field),name
 buffer=c.create_unicode_buffer(str(value));child.send(field,0xc,0,c.addressof(buffer));assert text(field)==str(value)

def toggle(page,name,value):
 field=control(page,name)
 if bool(child.send(field,0xf0))!=value:child.send(field,0xf5)
 assert bool(child.send(field,0xf0))==value

def read(page):
 return {**{key:int(text(control(page,key))) for key in ['ZoomStep','ZoomBelow','ZoomAbove','ZoomSimple']},
         **{key:bool(child.send(control(page,key),0xf0)) for key in ['KeepZoom','ZoomReduce','ZoomEnlarge','DragSimple']},
         'ZoomMode':child.send(control(page,'ZoomMode'),0x147)}

def close_settings(dialog,accept):
 button(dialog,1 if accept else 2);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)

def configure(mode,below,above):
 dialog,page=settings();choose(page,1);set_field(page,'ZoomBelow',below);set_field(page,'ZoomAbove',above)
 choose(page,mode);close_settings(dialog,True)

def alpha(label):
 sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,ids['Copy'])
 child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(17))
 child.wait(lambda:bool(U.OpenClipboard(None)))
 try:
  handle=U.GetClipboardData(17);pointer=K.GlobalLock(handle);native.check(pointer)
  try:data=c.string_at(pointer,K.GlobalSize(handle))
  finally:K.GlobalUnlock(handle)
 finally:U.CloseClipboard()
 header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',data)
 assert (header,width,abs(height),planes,bits,compression)==(124,3,2,1,32,3)
 actual=Image.frombytes('RGBA',(3,2),data[124:],'raw','BGRA',12,-1 if height>0 else 1)
 assert actual.tobytes()==original.tobytes(),'Zoom changed copied RGBA'
 actual.save(out/(label+'.png'));case(label,rgba_pixels=6,alpha_values=[0,1,127,128,254,255])

def saved(profile):
 ini=configparser.ConfigParser(interpolation=None);ini.read(profile,encoding='utf-16');return dict(ini['Viewer'])

defaults=dict(ZoomStep=25,ZoomBelow=10,ZoomAbove=25,ZoomSimple=0,KeepZoom=False,ZoomReduce=True,ZoomEnlarge=True,DragSimple=False,ZoomMode=2)
try:
 profile=out/'新規 settings.ini';assert not profile.exists();start(profile)
 dialog,page=settings();assert read(page)==defaults;close_settings(dialog,False)
 case('fresh_native_controls_match_reference_defaults',controls=9)
 dialog,page=settings();choose(page,0);set_field(page,'ZoomStep',75);choose(page,1)
 set_field(page,'ZoomBelow',3);set_field(page,'ZoomAbove',5000);set_field(page,'ZoomSimple',750)
 for name,value in [('KeepZoom',True),('ZoomReduce',False),('ZoomEnlarge',False),('DragSimple',True)]:toggle(page,name,value)
 custom=read(page);close_settings(dialog,True);set_percent(100)
 child.send(window,0x111,ids['ZoomIn']);assert percent()==5000
 child.send(window,0x111,ids['ZoomOut']);assert percent()==100
 case('native_high_step_5000_applies_and_keeps_disabled_legacy_value',controls=custom)
 alpha('alpha_before_restart');close();before=saved(profile)
 assert before['zoom_mode']=='1' and before['zoom_above']=='5000' and before['zoom_step']=='75'
 start(profile);dialog,page=settings();assert read(page)==custom;close_settings(dialog,False)
 case('same_profile_graceful_restart_preserves_all_nine_zoom_fields')
 previous=None
 for mode,below,above,before,enlarge,expected,label in rows:
  key=(int(mode),int(below),int(above))
  if key!=previous:configure(*key);previous=key
  set_percent(before);child.send(window,0x111,ids['ZoomIn' if enlarge=='1' else 'ZoomOut']);actual=percent()
  report['transitions'].append(dict(label=label,mode=key[0],below=key[1],above=key[2],before=float(before),enlarge=enlarge=='1',expected=float(expected),actual=actual))
  assert abs(actual-float(expected))<1e-7,report['transitions'][-1]
 assert len(report['transitions'])==474;case('all_474_reference_transitions_through_actual_numeric_dialogs')
 alpha('alpha_after_474_transitions')
 dialog,page=settings();before_reset=read(page);button(dialog,102);child.wait(lambda:read(page)==defaults)
 close_settings(dialog,False);dialog,page=settings();assert read(page)==before_reset
 case('page_reset_cancel_preserves_custom_values');button(dialog,102);child.wait(lambda:read(page)==defaults)
 close_settings(dialog,True);close();start(profile);dialog,page=settings();assert read(page)==defaults
 close_settings(dialog,False);case('page_reset_apply_and_graceful_restart_restore_nine_defaults');close()
 legacy=out/'legacy.ini';legacy.write_text('[Viewer]\nzoom_mode=0\nzoom_step=75\ndrag_simple=1\n',encoding='utf-16')
 start(legacy);dialog,page=settings();values=read(page)
 assert values['ZoomMode']==0 and values['DragSimple'] and values['ZoomStep']==75
 close_settings(dialog,False);set_percent(100);child.send(window,0x111,ids['ZoomIn']);assert percent()==175
 child.send(window,0x111,ids['ZoomOut']);assert percent()==100
 case('explicit_legacy_relative_mode_and_drag_quality_preserved');alpha('alpha_legacy');close()
 assert native.sha(asset)==report['asset_sha256'];report['passed']=True
except BaseException as error:
 report['error']=type(error).__name__+': '+str(error)
 if child and child in children:report['owned_windows_at_failure']=[dict(cls=native.window_text(h,True),title=native.window_text(h)) for h in child.windows()]
 raise
finally:
 for item in reversed(children):item.close()
 report['guards_passed']=native.sha(exe)==report['executable_sha256'] and native.sha(__file__)==report['tool_sha256'] and native.sha(native.__file__)==report['helper_sha256'] and native.sha(fixture)==report['fixture_sha256']
 (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print('Evidence: '+str(out),flush=True);K.ReleaseMutex(mutex);K.CloseHandle(mutex)
 assert report['guards_passed']
