from pathlib import Path
from urllib.request import Request, urlopen
import json

root = Path(__file__).resolve().parents[1]
cache = root / '.cache' / 'dependencies'
cache.mkdir(parents=True, exist_ok=True)

def get(url):
    with urlopen(Request(url, headers={'User-Agent': 'GVN-build'}), timeout=40) as response:
        return response.read()

for repo in ('aaaaaa123456789/libplum', 'MolecularMatters/psd_sdk', 'nothings/stb'):
    name = repo.split('/')[-1]
    data = json.loads(get('https://api.github.com/repos/' + repo + '/commits/master'))
    sha = data['sha']
    (cache / (name + '-revision.json')).write_text(json.dumps({'repo': repo, 'revision': sha}), encoding='utf-8')
    for file in ('README.md', 'LICENSE', 'CMakeLists.txt'):
        try:
            (cache / (name + '-' + file)).write_bytes(get('https://raw.githubusercontent.com/' + repo + '/' + sha + '/' + file))
        except Exception:
            pass
    print(name, sha)
repo = 'aaaaaa123456789/libplum'
data = json.loads(get('https://api.github.com/repos/' + repo + '/releases/latest'))
(cache / 'libplum-release.json').write_text(json.dumps(data), encoding='utf-8')
print('libplum release', data['tag_name'])
for asset in data['assets']:
    print(asset['name'], asset['browser_download_url'])
for filename in ('tutorial.md', 'metadata.md', 'functions.md', 'structs.md', 'constants.md', 'formats.md'):
    try:
        (cache / ('libplum-' + filename)).write_bytes(get('https://raw.githubusercontent.com/' + repo + '/master/docs/' + filename))
    except Exception:
        pass
