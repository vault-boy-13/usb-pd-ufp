"""Create and verify the standard ExeFS PFS0 and an installable SD archive."""
import hashlib
import json
import struct
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TITLE = '42000000000000C1'
VERSION = '0.1.2'
dist = ROOT / 'dist'
contents = dist / 'atmosphere' / 'contents' / TITLE
(contents / 'flags').mkdir(parents=True, exist_ok=True)
files = [('main', (ROOT / 'build/main.nso').read_bytes()),
         ('main.npdm', (ROOT / 'build/main.npdm').read_bytes())]
assert files[0][1][:4] == b'NSO0'
assert files[1][1][:4] == b'META'
npdm = files[1][1]
aci, aci_size, acid, acid_size = struct.unpack_from('<IIII', npdm, 0x70)
assert aci + aci_size <= len(npdm) and acid + acid_size <= len(npdm)
assert npdm[aci:aci+4] == b'ACI0' and npdm[acid+0x200:acid+0x204] == b'ACID'
assert struct.unpack_from('<Q', npdm, aci+0x10)[0] == int(TITLE, 16)
assert struct.unpack_from('<QQ', npdm, acid+0x210) == (int(TITLE,16), int(TITLE,16))
sac_off, sac_size = struct.unpack_from('<II', npdm, aci+0x28)
sac = npdm[aci+sac_off:aci+sac_off+sac_size]
services = []
while sac:
    control = sac[0]
    size = (control & 7)+1
    assert not control & 128, 'Module must not host services'
    services.append(sac[1:1+size].decode('ascii'))
    sac = sac[1+size:]
assert sorted(services) == sorted(['fsp-srv','set:sys','i2c','usb:pm','omm','psc:m'])
strings = b'main\0main.npdm\0'
strings += bytes((-len(strings)) % 16)
header = struct.pack('<4sIII', b'PFS0', 2, len(strings), 0)
offset = 0
for name_offset, (_, data) in zip((0, 5), files):
    header += struct.pack('<QQII', offset, len(data), name_offset, 0)
    offset += len(data)
blob = header + strings + b''.join(data for _, data in files)
# Independent extraction check against both original files.
base = 16 + 24 * 2 + len(strings)
for i, (name, data) in enumerate(files):
    off, size, name_off, reserved = struct.unpack_from('<QQII', blob, 16 + i * 24)
    assert reserved == 0
    assert strings[name_off:].split(b'\0', 1)[0].decode() == name
    assert blob[base + off:base + off + size] == data
(contents / 'exefs.nsp').write_bytes(blob)
(contents / 'flags/boot2.flag').write_bytes(b'')
(contents / 'toolbox.json').write_text(json.dumps({
    'name': 'USB PD UFP', 'tid': TITLE,
    'requires_reboot': True, 'version': VERSION}, indent=2) + '\n', encoding='utf-8')
config = dist / 'config/usb-pd-ufp'
config.mkdir(parents=True, exist_ok=True)
(config / 'config.ini').write_bytes((ROOT / 'config/config.ini').read_bytes())
(dist / 'README.md').write_bytes((ROOT / 'README.md').read_bytes())
archive = ROOT / f'usb-pd-ufp-{VERSION}.zip'
# Explicit allowlist: do not accidentally distribute logs or local SD files.
entries = ['README.md', 'LICENSE', 'THIRD_PARTY_NOTICES', 'config/usb-pd-ufp/config.ini',
           f'atmosphere/contents/{TITLE}/exefs.nsp',
           f'atmosphere/contents/{TITLE}/flags/boot2.flag',
           f'atmosphere/contents/{TITLE}/toolbox.json']
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for entry in entries:
        z.write(dist / entry, entry)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(ROOT / 'SHA256SUMS.txt').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
print(f'Package verified: {archive.name}; ExeFS {len(blob)} bytes; SHA256 {digest}')
