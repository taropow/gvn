"""Compare owned drive-end policy and actual elapsed slideshow intervals.

Only our private desktop/processes/profiles and generated PNGs are used. A free
DOS drive letter aliases the owned volume directory and is removed by exact target.
Reference timer APIs are observed with the pinned existing observer, never replaced.
"""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,json,re,shutil,statistics,subprocess,time,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station
ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),('GetMenuItemCount',c.c_int,[P]),('GetMenuItemID',w.UINT,[P,c.c_int]),('GetMenuState',w.UINT,[P,w.UINT,w.UINT]),('IsWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P])]:native.api(U,name,result,args)
for name,result,args in [('GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)]),('GetLogicalDrives',w.DWORD,[]),('QueryDosDeviceW',w.DWORD,[w.LPCWSTR,w.LPWSTR,w.DWORD]),('DefineDosDeviceW',w.BOOL,[w.DWORD,w.LPCWSTR,w.LPCWSTR])]:native.api(K,name,result,args)
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
class OwnedDrive:
    def __init__(self,path):
        self.path=path.resolve();assert self.path.is_relative_to((ROOT/'.cache').resolve()) and self.path.is_dir()
        self.device=None;self.target='\\??\\'+str(self.path)
    def query(self,device):
        buffer=c.create_unicode_buffer(32768);c.set_last_error(0)
        if K.QueryDosDeviceW(device,buffer,len(buffer)):return buffer.value
        assert c.get_last_error()==2,(device,c.get_last_error());return None
    def __enter__(self):
        drives=K.GetLogicalDrives();native.check(drives)
        for letter in 'ZYXWVUTSR':
            device=letter+':'
            if drives&(1<<(ord(letter)-ord('A'))) or self.query(device) is not None:continue
            native.check(K.DefineDosDeviceW(1|8,device,self.target));self.device=device
            assert self.query(device).casefold()==self.target.casefold();assert Path(device+'\\').is_dir()
            return self
        raise RuntimeError('No free owned drive letter')
    def __exit__(self,*_):
        if self.device:
            assert self.query(self.device).casefold()==self.target.casefold(),'Owned alias target changed'
            native.check(K.DefineDosDeviceW(1|2|4|8,self.device,self.target))
            assert self.query(self.device) is None,'Owned drive alias still present'
    def file(self,relative):return Path(self.device+'\\'+relative.replace('/','\\'))
class Viewer:
    def __init__(self,kind,exe,asset,out,desktop,interval,loop,next_folder,observer):
        self.kind=kind;self.out=out;self.interval=interval;self.observer=observer;self.closed=False
        self.exe=exe;self.commands={};value=0
        for item in (ROOT/'include/gvn/commands.h').read_text(encoding='utf-8').split('enum Command {',1)[1].split('};',1)[0].split(','):
            match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
            if match:
                if match[2]:value=int(match[2])
                self.commands[match[1]]=value;value+=1
        if kind=='gvn':
            self.config=out/(kind+'-'+str(interval)+'-'+str(int(loop))+'.ini')
            self.config.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\nmenu=1\ncaption=1\nstartup_state=0\nrestore_fullscreen=0\nslide_fullscreen=0\nslide_random=0\nslide_subtree=0\nsort=0\nend_action=1\n'+f'interval={interval}\nslide_loop={int(loop)}\nslide_next_folder={int(next_folder)}\n',encoding='utf-16')
            args=['--settings',self.config,asset];cls='GVN.ImageViewer'
        else:args=[asset];cls='TF811202_MassiGra_Main'
        self.child=native.Child(exe,args,desktop);self.window=self.child.wait(lambda:next((h for h in self.child.windows() if native.window_text(h,True)==cls),None))
        self.child.wait(lambda:asset.name in native.window_text(self.window));self.child.ping(self.window)
        if kind=='reference':
            if not U.GetMenu(self.window):self.child.send(self.window,0x111,41008)
            p=subprocess.run([str(observer/'attach_owned.exe'),str(self.child.info.pid),str(exe.resolve()),str((out/'observe.dll').resolve())],capture_output=True,timeout=25)
            (out/f'attach-{self.child.info.pid}.txt').write_bytes(p.stdout+p.stderr);assert p.returncode==0
            self.child.wait(lambda:any(e['api']=='Install' for e in self.events()))
        self.toolbar=None
        if kind=='gvn':
            bars=[]
            @native.Callback
            def add(h,_):
                if native.window_text(h,True).lower()=='toolbarwindow32' and self.child.send(self.child.own(h),0x400+25,self.commands['Slideshow'])<1000:bars.append(self.child.own(h))
                return True
            U.EnumChildWindows(self.window,add,0);assert len(bars)==1;self.toolbar=bars[0]
        self.record=dict(kind=kind,pid=self.child.info.pid,interval_ms=interval,loop=loop,next_folder=next_folder,subtree=False,source=str(asset),normal_close=False)
    def events(self):
        p=self.out/f'window-events-{self.child.info.pid}.jsonl'
        if not p.exists():return []
        data=p.read_bytes();rows=data.splitlines()
        if data and not data.endswith(b'\n'):rows=rows[:-1]
        return [json.loads(row) for row in rows]
    def timer(self):
        timers={}
        for event in self.events():
            if event['window']!=int(self.window):continue
            if event['api']=='SetTimer' and event['result']:timers[event['result']]=event
            elif event['api']=='KillTimer' and event['result']:timers.pop(event['id'],None)
        return next((r for r in timers.values() if r['interval']==self.interval),None)
    def active(self):
        if self.kind=='reference':return self.timer() is not None
        value=bool(self.child.send(self.toolbar,0x400+10,self.commands['Slideshow']))
        checks=[]
        def visit(menu):
            for i in range(U.GetMenuItemCount(menu)):
                sub=U.GetSubMenu(menu,i)
                if sub:visit(sub)
                elif U.GetMenuItemID(menu,i)==self.commands['Slideshow']:checks.append(bool(U.GetMenuState(menu,self.commands['Slideshow'],0)&8))
        visit(U.GetMenu(self.window));assert checks==[value],'Slideshow menu/toolbar differ';return value
    def state(self):
        title=native.window_text(self.window);names=['boundary-a.png','boundary-z.png']+[f'timing-{i}.png' for i in range(4)]
        found=[name for name in names if name in title];assert len(found)==1,title
        return dict(file=found[0],slideshow=self.active(),title=title)
    def start(self):
        if self.kind=='gvn':self.child.send(self.window,0x111,self.commands['Slideshow']);self.child.ping(self.window)
        else:
            self.child.post(self.window,0x111,40072)
            dialog=self.child.wait(lambda:next((h for h in self.child.windows() if '自動スライド' in native.window_text(h) and U.GetDlgItem(h,2031)),None))
            def check(identity,value):
                h=self.child.own(U.GetDlgItem(dialog,identity))
                if bool(self.child.send(h,0xf0))!=value:self.child.post(h,0xf5);self.child.wait(lambda:bool(self.child.send(h,0xf0))==value)
            check(2001,False);check(2002,self.record['loop']);check(2003,True)
            self.child.wait(lambda:bool(U.IsWindowEnabled(U.GetDlgItem(dialog,2004))));check(2004,False);check(2003,self.record['next_folder']);check(2005,False)
            self.record['actual_reference_checks']={str(i):bool(self.child.send(self.child.own(U.GetDlgItem(dialog,i)),0xf0)) for i in range(2001,2006)}
            text=c.create_unicode_buffer(f'{self.interval/1000:.1f}');self.child.send(self.child.own(U.GetDlgItem(dialog,2031)),0xc,0,c.addressof(text));self.child.post(self.child.own(U.GetDlgItem(dialog,1)),0xf5)
            self.child.wait(lambda:not U.IsWindow(dialog));self.child.wait(lambda:any(e['api']=='command-end' and e['id']==40072 for e in self.events()))
        self.child.wait(self.active);self.record['started']=self.state()
    def step(self):
        old=self.state();assert old['slideshow']
        if self.kind=='gvn':self.child.send(self.window,0x113,2,0)
        else:
            before=sum(e['api']=='timer-end' for e in self.events());timer=self.timer();assert timer and not timer['callback']
            self.child.post(self.window,0x113,timer['result'],0);self.child.wait(lambda:sum(e['api']=='timer-end' for e in self.events())>before)
        self.child.wait(lambda:native.window_text(self.window)!=old['title'] or not self.active());self.child.ping(self.window);return self.state()
    def close(self):
        if self.closed:return
        self.child.post(self.window,0x10);assert K.WaitForSingleObject(self.child.info.process,10000)==0
        code=w.DWORD();native.check(K.GetExitCodeProcess(self.child.info.process,c.byref(code)));assert code.value==0
        self.record.update(normal_close=True,exit_code=0)
        if self.kind=='reference':self.record['events']=self.events()
        self.child.close();self.closed=True
def main():
    parser=argparse.ArgumentParser();parser.add_argument('gvn',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    gvn=args.gvn.resolve();assert gvn.is_file();observer=ROOT/'.cache/reference-slideshow-observer';manifest=read(observer/'build.json')
    assert manifest['api_observation_only'] and not manifest['clock_replacement'] and manifest['architecture']=='x86'
    assert all(native.sha(ROOT/'tools/reference_slideshow'/n)==v for n,v in manifest['sources'].items()) and all(native.sha(observer/n)==v for n,v in manifest['binaries'].items())
    shutil.copyfile(observer/'observer.dll',out/'observe.dll');archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    reference=out/'reference'
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                p=(reference/name).resolve();assert p.is_relative_to(reference.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(package.read(name))
    exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    volume=out/'volume';timing=out/'timing';timing.mkdir()
    for folder,name,color in [('a','boundary-a.png',(200,20,50)),('z','boundary-z.png',(30,180,70))]:
        p=volume/folder/name;p.parent.mkdir(parents=True,exist_ok=True);Image.new('RGB',(64,48),color).save(p)
    for i in range(4):Image.new('RGB',(64,48),(i*50,200-i*30,40+i*20)).save(timing/f'timing-{i}.png')
    assets={p.relative_to(out).as_posix():native.sha(p) for p in [*volume.rglob('*.png'),*timing.glob('*.png')]}
    report=dict(passed=False,private_station=True,user_desktop_clipboard_or_artwork_used=False,gvn_sha256=native.sha(gvn),reference_sha256=native.sha(exe),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),observer=manifest,source_sha256=assets,cases=[],synthetic_timer_notifications_for_boundaries_only=True,timing_interval_ms=500,predeclared_elapsed_bounds_ms=[400,700],scope='Drive-end option priority via owned DOS alias and explicit timer notifications; actual elapsed small-PNG slideshow interval and final dwell at500ms. No startup,heavy-image precision,physical display or optimization claim.')
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();live=[];drive=None
    try:
        with OwnedDrive(volume) as drive:
            report['drive_alias']=dict(device=drive.device,target=drive.target,broadcast=False,removed=False)
            native.api(K,'GetVolumePathNameW',w.BOOL,[w.LPCWSTR,w.LPWSTR,w.DWORD]);native.api(K,'GetFileAttributesW',w.DWORD,[w.LPCWSTR])
            volume_name=c.create_unicode_buffer(32768);native.check(K.GetVolumePathNameW(str(drive.file('a')),volume_name,len(volume_name)))
            report['drive_alias'].update(volume_path=volume_name.value,entries=[p.name for p in Path(drive.device+'\\').iterdir()],attributes={s:int(K.GetFileAttributesW(str(drive.file(s)))) for s in ['a','z']})
            for loop in [False,True]:
                pair=[]
                for kind,binary in [('reference',exe),('gvn',gvn)]:
                    viewer=Viewer(kind,binary,drive.file('a/boundary-a.png'),out,desktop,600000,loop,True,observer);live.append(viewer)
                    viewer.start();states=[viewer.record['started']];viewer.record.update(name='unrestricted-drive-end-loop-'+str(int(loop)),steps=states,synthetic_timer_notifications=True);report['cases'].append(viewer.record)
                    states.append(viewer.step());states.append(viewer.step())
                    assert [s['file'] for s in states]==['boundary-a.png','boundary-z.png','boundary-z.png'] and [s['slideshow'] for s in states]==[True,True,False],states
                    viewer.close();pair.append(states);print('PASS',kind,'drive end loop='+str(loop),flush=True)
                assert [(s['file'],s['slideshow']) for s in pair[0]]==[(s['file'],s['slideshow']) for s in pair[1]]
        report['drive_alias']['removed']=True
        for kind,binary in [('reference',exe),('gvn',gvn)]:
            viewer=Viewer(kind,binary,timing/'timing-0.png',out,desktop,500,False,False,observer);live.append(viewer)
            viewer.start();started=time.perf_counter();changes=[dict(file='timing-0.png',elapsed_ms=0.)];last='timing-0.png'
            while viewer.active():
                assert time.perf_counter()-started<8,'Real slideshow timer did not stop'
                title=native.window_text(viewer.window);current=next((f'timing-{i}.png' for i in range(4) if f'timing-{i}.png' in title),None);assert current,title
                if current!=last:changes.append(dict(file=current,elapsed_ms=(time.perf_counter()-started)*1000));last=current
                time.sleep(.002)
            stopped=(time.perf_counter()-started)*1000;assert [r['file'] for r in changes]==[f'timing-{i}.png' for i in range(4)]
            intervals=[changes[i]['elapsed_ms']-changes[i-1]['elapsed_ms'] for i in range(1,4)];dwell=stopped-changes[-1]['elapsed_ms']
            assert all(400<=v<=700 for v in intervals+[dwell]),(kind,intervals,dwell)
            viewer.record.update(name='real-500ms-four-images',synthetic_timer_notifications=False,title_change_observations=changes,intervals_ms=intervals,median_interval_ms=statistics.median(intervals),final_dwell_ms=dwell,stopped_ms=stopped,after=viewer.state())
            viewer.close();report['cases'].append(viewer.record);print('PASS',kind,'real500ms',list(map(lambda v:round(v,2),intervals)),'dwell',round(dwell,2),flush=True)
        assert len(report['cases'])==6 and all(v['normal_close'] for v in report['cases']) and report['drive_alias']['removed']
        assert all(native.sha(out/n)==v for n,v in assets.items()) and native.sha(gvn)==report['gvn_sha256'] and native.sha(exe)==report['reference_sha256']
        report.update(passed=True,whole_goal_complete=False)
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        for viewer in live:
            if not viewer.closed:viewer.child.close()
        if drive and report.get('drive_alias'):report['drive_alias']['removed']=drive.query(drive.device) is None
        K.ReleaseMutex(mutex);K.CloseHandle(mutex);write(out/'result.json',report);print('Evidence:',out,flush=True)
if __name__=='__main__':main()
