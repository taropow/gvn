"""Exercise the native HWND/Direct2D path without changing the user's clipboard/settings."""
import json, subprocess, sys, tempfile
from pathlib import Path
exe=Path(sys.argv[1]);fixtures=Path(__file__).resolve().parents[1]/'tests'/'fixtures'
cases={'display-alpha.png':(512,384,1),'psd-16-3.psd':(3,2,1),'disposal.gif':(6,4,4),'blend.apng':(6,4,4),'excluded.png':(6,4,4),'images.zip':(3,2,1)}
with tempfile.TemporaryDirectory(prefix='GVN-gui-test-') as temporary:
    for name,expected in cases.items():
        report=Path(temporary)/(name+'.json')
        result=subprocess.run([str(exe),'--smoke',str(fixtures/name),str(report)],timeout=25)
        assert result.returncode==0,(name,result.returncode)
        data=json.loads(report.read_text())
        assert not data['error'],(name,data)
        assert data['rendered'],(name,data)
        assert (data['width'],data['height'],data['frames'])==expected,(name,data)
        assert data['toolbar_buttons']==27,(name,data)
        print('PASS native window/render',name)
    report=Path(temporary)/'invalid.json'
    result=subprocess.run([str(exe),'--smoke',str(fixtures/'missing.png'),str(report)],timeout=25)
    assert result.returncode!=0 and json.loads(report.read_text())['error']
    print('PASS missing image error')
    import shutil
    folder=Path(temporary)/'日本語 画像';folder.mkdir()
    path=folder/'透明 画像.png';shutil.copyfile(fixtures/'display-alpha.png',path)
    report=Path(temporary)/'unicode.json'
    result=subprocess.run([str(exe),'--smoke',str(path),str(report)],timeout=25)
    data=json.loads(report.read_text())
    assert result.returncode==0 and data['width']==512 and data['rendered'] and not data['error']
    print('PASS Unicode path with spaces')
    report=Path(temporary)/'profile.json'
    result=subprocess.run([str(exe),'--profile',str(fixtures/'display-alpha.png'),str(report)],timeout=30)
    data=json.loads(report.read_text())
    assert result.returncode==0 and data['rendered'] and not data['error']
    assert data['first_draw_succeeded'] and data['display_ready_ms']>=data['ready_ms']
    assert data['first_draw_ms']>0
    assert data['display_ready_ms']-data['ready_ms']+1>=data['first_draw_ms']
    assert data['conversion_cache_bytes']==0 and data['display_tile_bytes']>0
    assert data['software_target'] and data['draw_phases_ms']['target']>0
    assert sum(data['draw_phases_ms'].values())<=data['first_draw_ms']+0.1
    assert data['cache_idle'] and data['cache_bytes']<=data['cache_limit']==64*1024*1024
    print('PASS separate load-adoption and initial Direct2D draw timing')
    report=Path(temporary)/'empty-profile.json'
    result=subprocess.run([str(exe),'--profile-empty',str(report)],timeout=30)
    data=json.loads(report.read_text())
    assert result.returncode==0 and data['rendered'] and not data['error']
    assert data['width']==data['height']==data['frames']==0
    assert data['first_draw_succeeded'] and data['display_ready_ms']>=data['ready_ms']
    assert data['software_target'] and data['draw_phases_ms']['target']>0
    assert data['conversion_cache_bytes']==data['display_tile_bytes']==0
    assert data['cache_idle'] and data['cache_bytes']==0
    print('PASS empty initial checker rendering and phase timing')
