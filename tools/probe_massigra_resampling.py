"""Optional MassiGra 0.45 pixel oracle; never controls an existing user process.

Requires the official ZIP at .cache/massigra-reference/MassiGra045.zip (SHA checked).
Arguments: target_width [comma_separated_methods [target_height [source_height
           [impulse|edge|ramp|grid|noise [source_width]]]]]. Defaults: 85, all 11, 3, 3, impulse, 17.
Outputs only to a fresh .cache/massigra-reference/runtime-* directory. The reference
executable and DLLs stay in that cache. Test fixture exports must include PNGs and
report.json only. A noninteractive station and shared test mutex isolate clipboard use.
"""
import ctypes as c
from ctypes import wintypes as w
import hashlib, io, json, shutil, subprocess, sys, time, uuid, zipfile
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

U=c.WinDLL('user32',use_last_error=True); K=c.WinDLL('kernel32',use_last_error=True)
P=c.c_void_p; UP=c.c_size_t; SP=c.c_ssize_t
def api(d,name,rest,args):
 f=getattr(d,name);f.restype=rest;f.argtypes=args;return f
api(U,'CreateWindowStationW',P,[w.LPCWSTR,w.DWORD,w.DWORD,P])
api(U,'SetProcessWindowStation',w.BOOL,[P])
api(U,'CreateDesktopW',P,[w.LPCWSTR,P,P,w.DWORD,w.DWORD,P])
api(U,'SetThreadDesktop',w.BOOL,[P])
api(U,'GetUserObjectInformationW',w.BOOL,[P,c.c_int,P,w.DWORD,c.POINTER(w.DWORD)])
api(U,'EnumWindows',w.BOOL,[P,SP]);api(U,'EnumChildWindows',w.BOOL,[P,P,SP])
api(U,'EnumDesktopWindows',w.BOOL,[P,P,SP])
api(U,'GetWindowThreadProcessId',w.DWORD,[P,c.POINTER(w.DWORD)])
api(U,'GetWindowTextW',c.c_int,[P,w.LPWSTR,c.c_int]);api(U,'GetClassNameW',c.c_int,[P,w.LPWSTR,c.c_int])
api(U,'GetMenu',P,[P]);api(U,'GetSubMenu',P,[P,c.c_int]);api(U,'GetMenuItemCount',c.c_int,[P])
api(U,'GetMenuItemID',w.UINT,[P,c.c_int]);api(U,'GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT])
api(U,'LoadMenuW',P,[P,P]);api(U,'DestroyMenu',w.BOOL,[P])
api(U,'GetDlgItem',P,[P,c.c_int]);api(U,'GetDlgCtrlID',c.c_int,[P])
api(U,'SendMessageTimeoutW',UP,[P,w.UINT,UP,SP,w.UINT,w.UINT,c.POINTER(UP)])
api(U,'PostMessageW',w.BOOL,[P,w.UINT,UP,SP])
api(U,'OpenClipboard',w.BOOL,[P]);api(U,'CloseClipboard',w.BOOL,[])
api(U,'GetClipboardData',P,[w.UINT]);api(U,'IsClipboardFormatAvailable',w.BOOL,[w.UINT])
api(K,'GlobalLock',P,[P]);api(K,'GlobalUnlock',w.BOOL,[P]);api(K,'GlobalSize',UP,[P])
api(K,'WaitForSingleObject',w.DWORD,[P,w.DWORD]);api(K,'TerminateProcess',w.BOOL,[P,w.UINT])
api(K,'CloseHandle',w.BOOL,[P])
api(K,'CreateMutexW',P,[P,w.BOOL,w.LPCWSTR]);api(K,'ReleaseMutex',w.BOOL,[P])
api(K,'LoadLibraryExW',P,[w.LPCWSTR,P,w.DWORD]);api(K,'FreeLibrary',w.BOOL,[P])
api(K,'EnumResourceNamesW',w.BOOL,[P,P,P,SP])
class STARTUP(c.Structure):
 _fields_=[('cb',w.DWORD),('reserved',w.LPWSTR),('desktop',w.LPWSTR),('title',w.LPWSTR),
 ('x',w.DWORD),('y',w.DWORD),('cx',w.DWORD),('cy',w.DWORD),('charsx',w.DWORD),('charsy',w.DWORD),
 ('fill',w.DWORD),('flags',w.DWORD),('show',w.WORD),('reserved2',w.WORD),('reserved3',P),
 ('stdin',P),('stdout',P),('stderr',P)]
class PROCESS(c.Structure):
 _fields_=[('process',P),('thread',P),('pid',w.DWORD),('tid',w.DWORD)]
api(K,'CreateProcessW',w.BOOL,[w.LPCWSTR,w.LPWSTR,P,P,w.BOOL,w.DWORD,P,w.LPCWSTR,c.POINTER(STARTUP),c.POINTER(PROCESS)])
CALLBACK=c.WINFUNCTYPE(w.BOOL,P,SP)
def check(ok):
 if not ok: raise c.WinError(c.get_last_error())
def text(h,cls=False):
 b=c.create_unicode_buffer(1024)
 (U.GetClassNameW if cls else U.GetWindowTextW)(h,b,len(b));return b.value
def own(h):
 assert K.WaitForSingleObject(process.process,0)!=0,'The controlled child process exited'
 pid=w.DWORD();U.GetWindowThreadProcessId(h,c.byref(pid))
 assert h and pid.value==process.pid,('Foreign HWND rejected',h,pid.value)
 return h
def send(h,msg,wp=0,lp=0):
 own(h);result=UP()
 check(U.SendMessageTimeoutW(h,msg,wp,lp,2,3000,c.byref(result)))
 return result.value
def post(h,msg,wp=0,lp=0): check(U.PostMessageW(own(h),msg,wp,lp))
def windows(parent=None):
 result=[]
 @CALLBACK
 def add(h,_):
  pid=w.DWORD();U.GetWindowThreadProcessId(h,c.byref(pid))
  if pid.value==process.pid: result.append(h)
  return True
 if parent: U.EnumChildWindows(own(parent),add,0)
 else: check(U.EnumDesktopWindows(desktop,add,0))
 return result
def wait_for(fn,seconds=8):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  value=fn()
  if value:return value
  if K.WaitForSingleObject(process.process,0)==0: raise RuntimeError('Reference process exited')
  time.sleep(.02)
 raise TimeoutError('Reference native operation timed out')
def menu_items(menu,path=()):
 result=[]
 for i in range(U.GetMenuItemCount(menu)):
  b=c.create_unicode_buffer(1024);U.GetMenuStringW(menu,i,b,len(b),0x400)
  names=path+(b.value,);child=U.GetSubMenu(menu,i)
  if child:result.extend(menu_items(child,names))
  else:result.append({'id':U.GetMenuItemID(menu,i),'path':list(names)})
 return result
def edit(dialog,id,value):
 h=own(U.GetDlgItem(dialog,id));b=c.create_unicode_buffer(str(value))
 send(h,0x000C,0,c.addressof(b));send(dialog,0x111,id|(0x300<<16),h)
def choose(dialog,id,index):
 h=own(U.GetDlgItem(dialog,id))
 assert send(h,0x146)==11,'Reference algorithm count changed'
 assert send(h,0x14E,index)==index
 send(dialog,0x111,id|(1<<16),h)
 assert send(h,0x147)==index,'Reference selection was not applied'
def copy_pixels(main,command):
 sequence=U.GetClipboardSequenceNumber()
 post(main,0x111,command)
 wait_for(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8))
 check(U.OpenClipboard(None))
 try:
  h=U.GetClipboardData(8);size=K.GlobalSize(h);ptr=K.GlobalLock(h);check(ptr)
  try: data=c.string_at(ptr,size)
  finally:K.GlobalUnlock(h)
 finally:U.CloseClipboard()
 return Image.open(io.BytesIO(data)).convert('RGB')

root=Path(__file__).resolve().parents[1]
script_sha=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
output=root/'.cache/massigra-reference'/('runtime-'+uuid.uuid4().hex)
output.mkdir()
archive=root/'.cache/massigra-reference/MassiGra045.zip'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
with zipfile.ZipFile(archive) as z:
 executable=z.read('MassiGra.exe')
 assert hashlib.sha256(executable).hexdigest()=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
 (output/'MassiGra.exe').write_bytes(executable)
 for name in z.namelist():
  if name.startswith('data/') and not name.endswith('/'):
   target=(output/name).resolve();assert target.is_relative_to(output.resolve())
   target.parent.mkdir(exist_ok=True,parents=True);target.write_bytes(z.read(name))
destination_width=int(sys.argv[1]) if len(sys.argv)>1 else 85
methods=list(map(int,sys.argv[2].split(','))) if len(sys.argv)>2 else list(range(11))
destination_height=int(sys.argv[3]) if len(sys.argv)>3 else 3
source_height=int(sys.argv[4]) if len(sys.argv)>4 else 3
pattern=sys.argv[5] if len(sys.argv)>5 else 'impulse'
source_width=int(sys.argv[6]) if len(sys.argv)>6 else 17
assert methods and len(set(methods))==len(methods) and all(0<=m<=10 for m in methods)
assert pattern in ('impulse','edge','ramp','grid','noise')
assert all(1<=n<=65535 for n in [source_width,source_height,destination_width,destination_height])
assert source_width*source_height<=16*1024*1024 and destination_width*destination_height<=16*1024*1024
source=Image.new('RGB',(source_width,source_height),(96,96,96))
for y in range(source_height):
 if pattern in ('ramp','grid','noise'):
  for x in range(source_width):
   if pattern=='ramp': pixel=((x*31+y*7)%256,(x*13+y*29)%256,(x*19+y*5)%256)
   elif pattern=='grid':pixel=(255*(x>=source_width//2),255*(y>=source_height//2),255*((x>=source_width//2)!=(y>=source_height//2)))
   else:
    value=(x*1664525+y*1013904223+12345)&0xffffffff
    value^=value>>16;value=(value*2246822519)&0xffffffff;value^=value>>13
    pixel=(value&255,(value>>8)&255,(value>>16)&255)
   source.putpixel((x,y),pixel)
 else:source.putpixel((0 if pattern=='edge' else source_width//2,y),(192,192,192))
source.save(output/'source.png')
source_png_sha=hashlib.sha256((output/'source.png').read_bytes()).hexdigest()
clipboard_mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');check(clipboard_mutex)
assert K.WaitForSingleObject(clipboard_mutex,30000) in (0,0x80),'Another isolated clipboard test is active'
station=U.CreateWindowStationW(None,0,0x37F,None);check(station)
station_buffer=c.create_unicode_buffer(512);station_size=w.DWORD()
check(U.GetUserObjectInformationW(station,2,station_buffer,c.sizeof(station_buffer),c.byref(station_size)))
station_name=station_buffer.value
class OBJECT_FLAGS(c.Structure):
 _fields_=[('inherit',w.BOOL),('reserved',w.BOOL),('flags',w.DWORD)]
station_flags=OBJECT_FLAGS()
check(U.GetUserObjectInformationW(station,1,c.byref(station_flags),c.sizeof(station_flags),c.byref(station_size)))
assert not station_flags.flags&1,'An interactive station is forbidden for this probe'
check(U.SetProcessWindowStation(station))
desktop_name='Reference'+uuid.uuid4().hex
desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);check(desktop)
check(U.SetThreadDesktop(desktop))
startup=STARTUP();startup.cb=c.sizeof(startup);startup.desktop=station_name+'\\'+desktop_name
startup.flags=1;startup.show=1
process=PROCESS()
args=c.create_unicode_buffer(subprocess.list2cmdline([str(output/'MassiGra.exe'),str(output/'source.png')]))
check(K.CreateProcessW(str(output/'MassiGra.exe'),args,None,None,False,0,None,str(output),c.byref(startup),c.byref(process)))
try:
 time.sleep(.8)
 print('INITIAL',json.dumps([{'title':text(h),'class':text(h,True)} for h in windows()]),flush=True)
 main=wait_for(lambda:next((h for h in windows() if text(h,True)=='TF811202_MassiGra_Main'),None))
 print('WINDOWS',json.dumps([{'title':text(h),'class':text(h,True)} for h in windows()]),flush=True)
 module=K.LoadLibraryExW(str(output/'MassiGra.exe'),None,0x22);check(module)
 menus=[]
 ENUM_RESOURCE=c.WINFUNCTYPE(w.BOOL,P,P,P,SP)
 @ENUM_RESOURCE
 def resource(mod,kind,name,_):
  menu=U.LoadMenuW(mod,name)
  if menu:
   menus.extend(menu_items(menu));U.DestroyMenu(menu)
  return True
 check(K.EnumResourceNamesW(module,4,resource,0));K.FreeLibrary(module)
 (output/'menus.json').write_text(json.dumps(menus,ensure_ascii=False,indent=2),encoding='utf-8')
 print('MAIN',text(main),text(main,True),flush=True)
 resize=next(x['id'] for x in menus if any('拡大縮小' in s for s in x['path']))
 undo=next(x['id'] for x in menus if any('元に戻' in s for s in x['path']))
 copy=next(x['id'] for x in menus if any('コピー' in s for s in x['path']) and not any('ファイル' in s for s in x['path']))
 print('COMMANDS',resize,copy,flush=True)
 results=[]
 for sample_index,method in enumerate(methods):
  if sample_index:
   post(main,0x111,undo)
   time.sleep(.05)
  post(main,0x111,resize)
  dialog=wait_for(lambda:next((h for h in windows() if U.GetDlgItem(h,2031) and U.GetDlgItem(h,2024)),None))
  controls=[{'id':U.GetDlgCtrlID(h),'class':text(h,True),'text':text(h)} for h in windows(dialog)]
  (output/'resize-controls.json').write_text(json.dumps(controls,ensure_ascii=False,indent=2),encoding='utf-8')
  for id in (2002,2003):
   if send(U.GetDlgItem(dialog,id),0xF0):send(U.GetDlgItem(dialog,id),0xF5)
  if not send(U.GetDlgItem(dialog,2001),0xF0):send(U.GetDlgItem(dialog,2001),0xF5)
  edit(dialog,2031,destination_width);edit(dialog,2032,destination_height)
  choose(dialog,2023,method);choose(dialog,2024,method)
  post(dialog,0x111,1)
  wait_for(lambda:not U.GetDlgItem(dialog,2031))
  image=copy_pixels(main,copy);image.save(output/f'result-{method}.png')
  assert image.size==(destination_width,destination_height),'Resize did not return the requested dimensions'
  pixels=[image.getpixel((x,image.height//2))[0] for x in range(image.width)]
  results.append({'method':method,'size':image.size,'pixels':pixels})
  print('PIXELS',method,image.size,pixels[30:55],flush=True)
 (output/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
 assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==script_sha,'Probe source changed during this run'
 assert hashlib.sha256((output/'source.png').read_bytes()).hexdigest()==source_png_sha,'Source asset was overwritten'
 report={'reference_version':'0.45','created_at':datetime.now(timezone.utc).isoformat(),
 'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'executable_sha256':hashlib.sha256(executable).hexdigest(),
 'probe':'tools/probe_massigra_resampling.py','probe_sha256':script_sha,'arguments':sys.argv[1:],
 'station_visible':bool(station_flags.flags&1),'clipboard_serialized':True,'controlled_pid_created_by_probe':int(process.pid),
 'source_size':[source_width,source_height],'target_size':[destination_width,destination_height],'pattern':pattern,
 'methods':methods,'source_png_sha256':hashlib.sha256((output/'source.png').read_bytes()).hexdigest(),
 'outputs':{f'result-{m}.png':hashlib.sha256((output/f'result-{m}.png').read_bytes()).hexdigest() for m in methods}}
 (output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print('EVIDENCE',str(output),flush=True)
except Exception:
 (output/'failure-windows.json').write_text(json.dumps([{'title':text(h),'class':text(h,True),'controls':[{'id':U.GetDlgCtrlID(k),'class':text(k,True),'text':text(k)} for k in windows(h)]} for h in windows()],ensure_ascii=False,indent=2),encoding='utf-8')
 print('FAILED_EVIDENCE',str(output),flush=True)
 raise
finally:
 # Only this created child is eligible for termination. Never enumerate/close user processes.
 if K.WaitForSingleObject(process.process,0)!=0:K.TerminateProcess(process.process,0)
 K.WaitForSingleObject(process.process,3000)
 K.CloseHandle(process.thread);K.CloseHandle(process.process)
 K.ReleaseMutex(clipboard_mutex);K.CloseHandle(clipboard_mutex)
