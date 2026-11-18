"""Create and verify the standard ExeFS PFS0 and an installable SD archive."""
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TITLE = '42000000000000C1'


def require(condition, message):
    if not condition:
        raise SystemExit(message)


version_source = (ROOT / 'source/main.c').read_text(encoding='utf-8')
version_match = re.search(r'^#define VERSION "(\d+\.\d+\.\d+)"$', version_source, re.MULTILINE)
if version_match is None:
    raise SystemExit('Could not read MAJOR.MINOR.PATCH version from source/main.c')
VERSION = version_match.group(1)
dist = ROOT / 'dist'
contents = dist / 'atmosphere' / 'contents' / TITLE
(contents / 'flags').mkdir(parents=True, exist_ok=True)
files = [('main', (ROOT / 'build/main.nso').read_bytes()),
         ('main.npdm', (ROOT / 'build/main.npdm').read_bytes())]
require(files[0][1][:4] == b'NSO0', 'build/main.nso is not a valid NSO image')
require(files[1][1][:4] == b'META', 'build/main.npdm is not a valid NPDM image')
npdm = files[1][1]
aci, aci_size, acid, acid_size = struct.unpack_from('<IIII', npdm, 0x70)
require(aci + aci_size <= len(npdm) and acid + acid_size <= len(npdm),
        'NPDM contains out-of-range ACI or ACID data')
require(npdm[aci:aci+4] == b'ACI0' and npdm[acid+0x200:acid+0x204] == b'ACID',
        'NPDM has invalid ACI0 or ACID data')
require(struct.unpack_from('<Q', npdm, aci+0x10)[0] == int(TITLE, 16),
        'NPDM ACI title ID does not match the sysmodule')
require(struct.unpack_from('<QQ', npdm, acid+0x210) == (int(TITLE, 16), int(TITLE, 16)),
        'NPDM ACID title ID range does not match the sysmodule')
sac_off, sac_size = struct.unpack_from('<II', npdm, aci+0x28)
require(aci + sac_off + sac_size <= len(npdm), 'NPDM contains out-of-range service access data')
sac = npdm[aci+sac_off:aci+sac_off+sac_size]
services = []
while sac:
    control = sac[0]
    size = (control & 7)+1
    require(not control & 128, 'Module must not host services')
    require(len(sac) >= size + 1, 'NPDM contains truncated service access data')
    try:
        services.append(sac[1:1+size].decode('ascii'))
    except UnicodeDecodeError as error:
        raise SystemExit('NPDM service access data is not ASCII') from error
    sac = sac[1+size:]
require(sorted(services) == sorted(['fsp-srv', 'set:sys', 'i2c', 'usb:pm', 'omm', 'psc:m']),
        'NPDM service access list does not match the module requirements')
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
    require(reserved == 0, 'Generated ExeFS has a nonzero reserved field')
    require(strings[name_off:].split(b'\0', 1)[0].decode() == name,
            'Generated ExeFS entry name does not match')
    require(blob[base + off:base + off + size] == data,
            'Generated ExeFS entry data does not match its source')
(contents / 'exefs.nsp').write_bytes(blob)
(contents / 'flags/boot2.flag').write_bytes(b'')
(contents / 'toolbox.json').write_text(json.dumps({
    'name': 'USB PD UFP', 'tid': TITLE,
    'requires_reboot': True, 'version': VERSION}, indent=2) + '\n', encoding='utf-8')
config = dist / 'config/usb-pd-ufp'
config.mkdir(parents=True, exist_ok=True)
(config / 'config.ini').write_bytes((ROOT / 'config/config.ini').read_bytes())
(dist / 'README.md').write_bytes((ROOT / 'README.md').read_bytes())
(dist / 'LICENSE').write_bytes((ROOT / 'LICENSE').read_bytes())
(dist / 'THIRD_PARTY_NOTICES').write_bytes((ROOT / 'THIRD_PARTY_NOTICES').read_bytes())
archive = ROOT / f'usb-pd-ufp-{VERSION}.zip'
# Explicit allowlist: do not accidentally distribute logs or local SD files.
entries = ['README.md', 'LICENSE', 'THIRD_PARTY_NOTICES', 'config/usb-pd-ufp/config.ini',
           f'atmosphere/contents/{TITLE}/exefs.nsp',
           f'atmosphere/contents/{TITLE}/flags/boot2.flag',
           f'atmosphere/contents/{TITLE}/toolbox.json']
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for entry in entries:
        z.write(dist / entry, entry)
with zipfile.ZipFile(archive) as z:
    require(z.namelist() == entries, 'Generated archive contents do not match the required file list')
    corrupt_entry = z.testzip()
    require(corrupt_entry is None, f'Generated archive failed its CRC check: {corrupt_entry}')
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(ROOT / 'SHA256SUMS.txt').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
print(f'Package verified: {archive.name}; ExeFS {len(blob)} bytes; SHA256 {digest}')
