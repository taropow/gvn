"""Read the official 0.45 ZIP's dialog resources as data, without executing MassiGra.

Usage: python tools/inspect_massigra_reference.py ZIP OUTPUT_DIRECTORY
The downloaded executable stays in the caller's research cache, outside distributions.
"""
from pathlib import Path
import ctypes
import hashlib
import json
import struct
import sys
import zipfile

archive, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
assert digest == '76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25', 'Unexpected official 0.45 archive'
output.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(archive) as package:
    binary = package.read('MassiGra.exe')
path = output / 'MassiGra.exe'
path.write_bytes(binary)
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
pointer = ctypes.c_void_p
for name, arguments, result in [
    ('LoadLibraryExW', [ctypes.c_wchar_p, pointer, ctypes.c_uint], pointer),
    ('FindResourceW', [pointer, pointer, pointer], pointer),
    ('LoadResource', [pointer, pointer], pointer),
    ('LockResource', [pointer], pointer),
    ('SizeofResource', [pointer, pointer], ctypes.c_uint),
    ('FreeLibrary', [pointer], ctypes.c_int),
]:
    function = getattr(kernel, name)
    function.argtypes, function.restype = arguments, result
callback = ctypes.WINFUNCTYPE(ctypes.c_int, pointer, pointer, pointer, ctypes.c_ssize_t)
kernel.EnumResourceNamesW.argtypes = [pointer, pointer, callback, ctypes.c_ssize_t]
module = kernel.LoadLibraryExW(str(path), None, 0x22)  # DATAFILE | IMAGE_RESOURCE, never DllMain.
assert module, ctypes.get_last_error()
dialogs, errors = [], []


@callback
def read_dialog(module, kind, name, unused):
    try:
        resource = kernel.FindResourceW(module, name, 5)
        data = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, resource)),
                                kernel.SizeofResource(module, resource))
        cursor = 0

        def read(format):
            nonlocal cursor
            values = struct.unpack_from('<' + format, data, cursor)
            cursor += struct.calcsize('<' + format)
            return values[0] if len(values) == 1 else values

        def string():
            first = read('H')
            if first == 0:
                return ''
            if first == 65535:
                return {'ordinal': read('H')}
            words = [first]
            while (word := read('H')):
                words.append(word)
            return struct.pack('<' + 'H' * len(words), *words).decode('utf-16-le')

        extended_template = struct.unpack_from('<H', data, 2)[0] == 65535
        if extended_template:
            version, signature, helpid, extended, style, count, x, y, width, height = read('HHIIIHhhhh')
            assert version == 1 and signature == 65535
        else:
            style, extended, count, x, y, width, height = read('IIHhhhh')
        menu, window_class, title = string(), string(), string()
        font = None
        if style & 64:
            font = {'size': read('H')}
            if extended_template:
                font.update(weight=read('H'), italic=read('B'), charset=read('B'))
            font['family'] = string()
        controls = []
        for _ in range(count):
            cursor = (cursor + 3) & ~3
            if extended_template:
                helpid, extended, style, x, y, width, height, identity = read('IIIhhhhI')
            else:
                style, extended, x, y, width, height, identity = read('IIhhhhH')
            control_class, text = string(), string()
            extra = read('H')
            cursor += extra
            controls.append({'id': identity, 'class': control_class, 'text': text,
                             'rect': [x, y, width, height], 'style': style})
        dialogs.append({'id': name if name < 65536 else ctypes.wstring_at(name),
                        'title': title, 'font': font, 'controls': controls})
        return 1
    except Exception as error:
        errors.append(str(error))
        return 0


try:
    assert kernel.EnumResourceNamesW(module, 5, read_dialog, 0), (ctypes.get_last_error(), errors)
finally:
    kernel.FreeLibrary(module)
assert not errors and len(dialogs) == 50
(output / 'dialogs.json').write_text(json.dumps(dialogs, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
selected = {}
for dialog, control, key in [('DLG_PROP_04', 2083, 'overlay_position'),
                             ('DLG_PROP_04', 2084, 'overlay_background'),
                             ('DLG_PROP_04', 2081, 'fit_algorithms'),
                             ('DLG_PROP_10', 2081, 'caption_filename')]:
    page = next(d for d in dialogs if d['id'] == dialog)
    text = next(c['text'] for c in page['controls'] if c['id'] == control)
    selected[key] = {'dialog': dialog, 'control': control, 'choices': text.split('\t')}
evidence = {'version': '0.45', 'official_page': 'http://www.massigra.net/',
            'official_download_link': 'https://www.dropbox.com/s/vhd2xni16qfcder/MassiGra045.zip',
            'archive_bytes': archive.stat().st_size, 'archive_sha256': digest,
            'executable_sha256': hashlib.sha256(binary).hexdigest(),
            'method': 'Win32 PE dialog resources loaded as data; no reference executable launched',
            'selected': selected}
(output / 'selected.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f'Read {len(dialogs)} native dialog resources and {len(selected)} choice lists')
