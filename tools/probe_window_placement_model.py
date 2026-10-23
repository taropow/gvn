"""Compare original Windows placement APIs with owned official viewer observations."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,json,uuid
import benchmark_navigation as native

parser=argparse.ArgumentParser();parser.add_argument('reference_report',type=Path)
parser.add_argument('--extra-report',type=Path)
parser.add_argument('--output',type=Path,default=Path('.cache/window-placement-model'))
args=parser.parse_args();root=Path(__file__).resolve().parents[1]
source=args.reference_report.resolve();assert source.is_relative_to((root/'.cache/window-startup-reference').resolve())
reference=json.loads(source.read_text(encoding='utf-8'));assert reference['passed'] and reference['source_and_binary_guards_passed']
assert reference['reference_sha256']=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
out=args.output.resolve()/('run-'+uuid.uuid4().hex);out.mkdir(parents=True)
U,K=native.U,native.K
for name,result,arguments in [('GetModuleHandleW',native.P,[w.LPCWSTR]),('GetCurrentProcessId',w.DWORD,[])]:native.api(K,name,result,arguments)
class WindowClass(c.Structure):
 _fields_=[('style',w.UINT),('procedure',native.P),('class_extra',c.c_int),('window_extra',c.c_int),('instance',native.P),('icon',native.P),('cursor',native.P),('brush',native.P),('menu',w.LPCWSTR),('name',w.LPCWSTR)]
class Placement(c.Structure):
 _fields_=[('length',w.UINT),('flags',w.UINT),('show',w.UINT),('minimum',w.POINT),('maximum',w.POINT),('normal',w.RECT)]
assert c.sizeof(Placement)==44
for name,result,arguments in [
 ('RegisterClassW',w.WORD,[c.POINTER(WindowClass)]),('UnregisterClassW',w.BOOL,[w.LPCWSTR,native.P]),
 ('CreateWindowExW',native.P,[w.DWORD,w.LPCWSTR,w.LPCWSTR,w.DWORD,c.c_int,c.c_int,c.c_int,c.c_int,native.P,native.P,native.P,native.P]),
 ('SetWindowPlacement',w.BOOL,[native.P,c.POINTER(Placement)]),('DestroyWindow',w.BOOL,[native.P])]:native.api(U,name,result,arguments)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD();native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
native.check(U.SetProcessWindowStation(station))
desktop=U.CreateDesktopW('GVN-PlacementModel-'+uuid.uuid4().hex,None,None,0,0x10000000,None);native.check(desktop);native.check(U.SetThreadDesktop(desktop))
instance=K.GetModuleHandleW(None);identity='GVN.PlacementModel.'+uuid.uuid4().hex
cls=WindowClass();cls.instance=instance;cls.name=identity;cls.procedure=c.cast(U.DefWindowProcW,native.P)
native.check(U.RegisterClassW(c.byref(cls)));windows=[]
report=dict(passed=False,scope='Original SetWindowPos/SetWindowPlacement on verified self-owned DefWindowProc windows in an owned noninteractive station/desktop. Reference is an immutable prior official0.45 observation. No global input, user windows, setting changes or timing measurement.',reference_sha256=native.sha(source),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),cases=[],method_matches={})
rows=list(reference['window_limit_cases'])
if args.extra_report:
 extra=args.extra_report.resolve();assert extra.is_relative_to((root/'.cache/window-startup-reference').resolve())
 data=json.loads(extra.read_text(encoding='utf-8'));assert data['passed'] and data['source_and_binary_guards_passed'] and data['reference_sha256']==reference['reference_sha256']
 rows.extend(v for v in data['window_limit_cases'] if 'after_restart' in v)
 report['extra_reference_sha256']=native.sha(extra)
def own(window):
 pid=w.DWORD();U.GetWindowThreadProcessId(window,c.byref(pid));assert pid.value==K.GetCurrentProcessId();return window
try:
 current=[U.GetSystemMetrics(i) for i in [76,77,78,79]]
 assert current==reference['virtual_screen'],(current,reference['virtual_screen'])
 for method in ['SetWindowPos','SetWindowPlacement','VirtualBoundsThenPlacement']:
  matched=0
  for case in rows:
   window=U.CreateWindowExW(0,identity,'owned placement model',0xcf0000,0,0,480,360,None,None,instance,None);native.check(window);windows.append(own(window))
   x,y,width,height=map(int,case['requested'])
   if method=='SetWindowPos':native.check(U.SetWindowPos(own(window),None,x,y,width,height,0x14))
   else:
    if method=='VirtualBoundsThenPlacement':
     left,top,virtual_width,virtual_height=current
     width=min(width,virtual_width);height=min(height,virtual_height)
     x=max(left,min(x,left+virtual_width-width));y=max(top,min(y,top+virtual_height-height))
    placement=Placement();placement.length=c.sizeof(placement);placement.show=4;placement.normal=w.RECT(x,y,x+width,y+height)
    native.check(U.SetWindowPlacement(own(window),c.byref(placement)))
   bounds=w.RECT();native.check(U.GetWindowRect(own(window),c.byref(bounds)))
   actual=[bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top];expected=case['after_restart']['rect'];same=actual==expected
   report['cases'].append(dict(method=method,name=case['name'],requested=case['requested'],actual=actual,official=expected,matched=same));matched+=same
   native.check(U.DestroyWindow(own(window)));windows.remove(window)
  report['method_matches'][method]=dict(matched=matched,total=len(rows))
 report['passed']=True
finally:
 for window in windows:U.DestroyWindow(own(window))
 U.UnregisterClassW(identity,instance)
 (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(report['method_matches']),flush=True);print('Evidence: '+str(out),flush=True)
assert report['passed'] and native.sha(source)==report['reference_sha256']
