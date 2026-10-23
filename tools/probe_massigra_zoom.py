"""Inspect/measure zoom controls of a probe-owned official0.45 process; private desktop only."""
from pathlib import Path
from ctypes import wintypes as w
import ctypes as c
import hashlib,json,re,shutil,struct,sys,time,uuid,zipfile
from PIL import Image
import benchmark_navigation as native

root=Path(__file__).resolve().parents[1]
output=root/'.cache/zoom-reference'/('run-'+uuid.uuid4().hex);output.mkdir(parents=True)
archive=root/'.cache/massigra-reference/MassiGra045.zip'
assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
reference=output/'reference'
with zipfile.ZipFile(archive) as package:
 for name in package.namelist():
  if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
   path=(reference/name).resolve();assert path.is_relative_to(reference.resolve())
   path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(package.read(name))
assert native.sha(reference/'MassiGra.exe')=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
U,K=native.U,native.K
for name,result,args in [('GetDlgCtrlID',c.c_int,[native.P]),('GetParent',native.P,[native.P]),
 ('IsWindow',w.BOOL,[native.P]),('IsWindowEnabled',w.BOOL,[native.P]),('IsWindowVisible',w.BOOL,[native.P]),('GetDpiForWindow',w.UINT,[native.P]),('GetWindowLongW',w.LONG,[native.P,c.c_int])]:native.api(U,name,result,args)
native.api(K,'VirtualAllocEx',native.P,[native.P,native.P,native.UP,w.DWORD,w.DWORD])
native.api(K,'VirtualFreeEx',w.BOOL,[native.P,native.P,native.UP,w.DWORD])
native.api(K,'ReadProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P])
native.api(K,'WriteProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P])
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
station_name=c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Zoom-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
asset=output/'source.png';Image.new('RGB',(1,1),(41,77,113)).save(asset)
child=native.Child(reference/'MassiGra.exe',[asset],station_name.value+'\\'+desktop_name)
report=dict(passed=False,noninteractive=True,reference_sha256=native.sha(reference/'MassiGra.exe'),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),controlled_pid=child.info.pid,
 scope='Own private-station process/profile/1x1source; actual native settings and zoom controls. No ComputerUse/global input/user profile or clipboard/startup comparison.')

def descendants(parent):
 result=[]
 @native.Callback
 def add(hwnd,_):result.append(child.own(hwnd));return True
 U.EnumChildWindows(child.own(parent),add,0);return result

def text(hwnd):
 buffer=c.create_unicode_buffer(2048);child.send(hwnd,0xd,len(buffer),c.addressof(buffer));return buffer.value

def controls(parent):
 result=[]
 for hwnd in descendants(parent):
  cls=native.window_text(hwnd,True)
  if cls not in {'Button','Edit','Static','ComboBox','msctls_updown32'}:continue
  row=dict(id=U.GetDlgCtrlID(hwnd),cls=cls,text=text(hwnd),enabled=bool(U.IsWindowEnabled(hwnd)),visible=bool(U.IsWindowVisible(hwnd)),style_visible=bool(U.GetWindowLongW(hwnd,-16)&0x10000000))
  if cls=='ComboBox':
   row['selected']=child.send(hwnd,0x147);row['choices']=[]
   for i in range(child.send(hwnd,0x146)):
    buffer=c.create_unicode_buffer(2048);child.send(hwnd,0x148,i,c.addressof(buffer));row['choices'].append(buffer.value)
  if cls=='Button':row['checked']=child.send(hwnd,0xf0)
  if cls=='Edit':row['text_limit']=child.send(hwnd,0xd5)
  if cls=='msctls_updown32':row['range16_raw']=child.send(hwnd,0x466)
  result.append(row)
 return result

def settings(main):
 child.post(main,0x111,40101)
 dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and native.window_text(h).startswith('MassiGra - 設定') and U.GetDlgItem(h,1)),None))
 tab=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32'),None))
 remote=K.VirtualAllocEx(child.info.process,None,1024,0x3000,4);native.check(remote)
 try:
  tabs=[]
  for i in range(child.send(tab,0x1304)):
   # TCITEMW belongs to the32-bit reference process; pointers are DWORDs.
   value=c.create_string_buffer(struct.pack('<IIIIiii',1,0,0,remote+64,256,0,0))
   native.check(K.WriteProcessMemory(child.info.process,remote,value,28,None))
   native.check(child.send(tab,0x133c,i,remote));value=c.create_string_buffer(512)
   native.check(K.ReadProcessMemory(child.info.process,remote+64,value,512,None))
   tabs.append(value.raw.decode('utf-16-le').split('\0')[0])
  report['settings_tabs']=tabs;assert tabs.count('ズーム')==1,tabs;zoom_index=tabs.index('ズーム')
  native.check(child.send(tab,0x130a,zoom_index,remote));rect=(c.c_int*4)()
  native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None));x,y=(rect[0]+rect[2])//2,(rect[1]+rect[3])//2
  child.post(tab,0x201,1,x|y<<16);child.post(tab,0x202,0,x|y<<16)
 finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
 child.wait(lambda:child.send(tab,0x130b)==zoom_index);child.ping(dialog)
 label=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='Button' and text(h)=='ズームステップ'),None))
 page=child.own(U.GetParent(label));combo=child.own(U.GetDlgItem(page,2021))
 assert child.send(combo,0x146)>0
 return dialog,page

def numeric_dialog(main):
 child.post(main,0x111,40084)
 return child.wait(lambda:next((h for h in child.windows() if h!=main and 'ズーム' in native.window_text(h)
                               and U.GetDlgItem(h,2031) and U.GetDlgItem(h,2161) and U.GetDlgItem(h,1)),None))

def scale(main):
 title=native.window_text(main);dialog=numeric_dialog(main);value=text(U.GetDlgItem(dialog,2031))
 child.send(dialog,0x10);child.wait(lambda:not U.IsWindow(dialog))
 assert native.window_text(main)==title,('Reading numeric zoom changed the title',title,native.window_text(main))
 number=float(value);assert 0<number<=100000,value
 return number

def set_scale(main,value):
 dialog=numeric_dialog(main);field=child.own(U.GetDlgItem(dialog,2031));buffer=c.create_unicode_buffer(str(value))
 child.send(field,0xc,0,c.addressof(buffer));child.post(U.GetDlgItem(dialog,1),0xf5)
 child.wait(lambda:not U.IsWindow(dialog))
 return scale(main)

def configure(main,mode,below=10,above=25):
 dialog,page=settings(main);combo=child.own(U.GetDlgItem(page,2021))
 child.send(combo,0x14e,mode);child.send(page,0x111,2021|(1<<16),combo)
 for id,value in [(2032,below),(2033,above)]:
  field=child.own(U.GetDlgItem(page,id));buffer=c.create_unicode_buffer(str(value));child.send(field,0xc,0,c.addressof(buffer))
  child.send(page,0x111,id|(0x300<<16),field)
 row=dict(mode=mode,below_text=text(U.GetDlgItem(page,2032)),above_text=text(U.GetDlgItem(page,2033)),
          below_enabled=bool(U.IsWindowEnabled(U.GetDlgItem(page,2032))),above_enabled=bool(U.IsWindowEnabled(U.GetDlgItem(page,2033))))
 assert row['below_enabled']==(mode==1) and row['above_enabled']==(mode==1)
 child.post(U.GetDlgItem(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)
 dialog,page=settings(main);row['saved_controls']=controls(page)
 child.post(U.GetDlgItem(dialog,2),0xf5);child.wait(lambda:not U.IsWindow(dialog))
 return row

def measure(main):
 report['zoom_configurations']=[];report['zoom_steps']=[];report['standard_sequences']={}
 def record(mode,below,above,requested,enlarge):
  before=set_scale(main,requested);child.send(main,0x111,40082 if enlarge else 40083);after=scale(main)
  row=dict(mode=mode,below=below,above=above,requested_percent=requested,before_percent=before,enlarge=enlarge,after_percent=after,title=native.window_text(main))
  report['zoom_steps'].append(row)
  (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 for mode,below,above in [(0,10,25),(1,10,25),(1,3,17),(1,100,5000)]:
  config=configure(main,mode,below,above);report['zoom_configurations'].append(config)
  for requested in [1,7,12,16,25,33,50,66,75,99,100,101,125,149,199,999,4999,5000]:
   for enlarge in [True,False]:record(mode,below,above,requested,enlarge)
  print('PASS zoom configuration',mode,below,above,'cases',len(report['zoom_steps']),flush=True)
 configure(main,0)
 for direction,command in [('out',40083),('in',40082)]:
  set_scale(main,100);sequence=[scale(main)]
  for attempt in range(60):
   child.send(main,0x111,command);value=scale(main);sequence.append(value)
   if value==sequence[-2]:break
  else:raise AssertionError(('standard sequence did not stop',direction,sequence))
  report['standard_sequences'][direction]=sequence;print('PASS standard',direction,sequence,flush=True)
 dialog,page=settings(main)
 for id in [2001,2002,2003]:child.send(U.GetDlgItem(page,id),0xf5)
 for id,value in [(2031,4321),(2032,77),(2033,999)]:
  field=child.own(U.GetDlgItem(page,id));buffer=c.create_unicode_buffer(str(value));child.send(field,0xc,0,c.addressof(buffer))
 combo=U.GetDlgItem(page,2021);child.send(combo,0x14e,1);child.send(page,0x111,2021|(1<<16),combo)
 report['before_reset']=controls(page);child.send(U.GetDlgItem(page,2061),0xf5);child.ping(dialog)
 report['after_reset']=controls(page)
 child.post(U.GetDlgItem(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)
 report['reset_step_from_100_in']=set_scale(main,100);child.send(main,0x111,40082);report['reset_step_from_100_in']=scale(main)
 assert len(report['zoom_steps'])==144

def fractional(main):
 report['fractional_steps']=[]
 for mode,below,above in [(0,10,25),(1,10,25),(1,3,17)]:
  configure(main,mode,below,above)
  for requested in [1.5,12.5,16.6667,33.3333,66.6667,99.25,99.99,100.01,117.01,125.125,4999.999,.5,.1,.01,.001]:
   for enlarge in [True,False]:
    before=set_scale(main,requested);child.send(main,0x111,40082 if enlarge else 40083);after=scale(main)
    report['fractional_steps'].append(dict(mode=mode,below=below,above=above,requested_percent=requested,before_percent=before,enlarge=enlarge,after_percent=after,title=native.window_text(main)))
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
  print('PASS fractional zoom',mode,below,above,'cases',len(report['fractional_steps']),flush=True)
 assert len(report['fractional_steps'])==90

def rounding(main):
 report['rounding_steps']=[]
 for mode,below,above in [(0,10,25),(1,10,25),(1,3,17)]:
  configure(main,mode,below,above)
  for base in [1,2,25,75,97,100,117,125]:
   for offset in [-.51,-.5,-.49,-.01,.01]:
    requested=round(base+offset,2)
    for enlarge in [True,False]:
     before=set_scale(main,requested);child.send(main,0x111,40082 if enlarge else 40083);after=scale(main)
     report['rounding_steps'].append(dict(mode=mode,below=below,above=above,requested_percent=requested,before_percent=before,enlarge=enlarge,after_percent=after,title=native.window_text(main)))
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
  print('PASS rounding zoom',mode,below,above,'cases',len(report['rounding_steps']),flush=True)
 assert len(report['rounding_steps'])==240

try:
 main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
 child.wait(lambda:'source.png' in native.window_text(main));child.ping(main)
 native.check(U.SetWindowPos(child.own(main),None,0,0,640,480,0x4|0x10))
 report['dpi']=U.GetDpiForWindow(main)
 sources=output/'sources';sources.mkdir();shutil.copyfile(__file__,sources/Path(__file__).name);shutil.copyfile(native.__file__,sources/Path(native.__file__).name)
 dialog,page=settings(main);report['zoom_settings_initial']=controls(page)
 child.post(U.GetDlgItem(dialog,2),0xf5);child.wait(lambda:not U.IsWindow(dialog))
 zoom=numeric_dialog(main)
 report['zoom_dialog_title']=native.window_text(zoom);report['zoom_dialog_controls']=controls(zoom)
 child.send(zoom,0x10);child.wait(lambda:not U.IsWindow(zoom))
 phase=sys.argv[1] if len(sys.argv)>1 else 'inspect';assert phase in {'inspect','measure','fractional','rounding'};report['phase']=phase
 if phase=='measure':measure(main)
 if phase=='fractional':fractional(main)
 if phase=='rounding':rounding(main)
 report['passed']=True
 print(json.dumps(dict(settings=report['zoom_settings_initial'],zoom_dialog=report['zoom_dialog_controls']),ensure_ascii=False),flush=True)
except BaseException as error:
 report['error']=repr(error);raise
finally:
 if K.WaitForSingleObject(child.info.process,0)!=0:native.check(K.TerminateProcess(child.info.process,0))
 report['child_terminal']=K.WaitForSingleObject(child.info.process,3000)==0;child.close();assert report['child_terminal']
 (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(output),flush=True)
