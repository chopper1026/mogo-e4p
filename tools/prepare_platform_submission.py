"""Prepare a reproducible E4P submission from one production build."""
import hashlib
import json
import os
from pathlib import Path

version = os.environ['PICO_PROJECT_VER']
files = {
    'bootloader': Path('build/bootloader/bootloader.bin'),
    'partition-table': Path('build/partition_table/partition-table.bin'),
    'app': Path('build/Read_Pico.bin'),
    'ota-data-initial': Path('build/ota_data_initial.bin'),
}
app = files['app'].read_bytes()
if len(app) < 288 or app[0] != 0xe9 or int.from_bytes(app[12:14], 'little') != 0x20:
    raise SystemExit('Not an ESP32-S31 application image')
if int.from_bytes(app[32:36], 'little') != 0xabcd5432:
    raise SystemExit('Missing application description')
if app[48:80].split(b'\0', 1)[0].decode('ascii') != version:
    raise SystemExit('Embedded application version differs from tag')
if len(app) > 0x500000:
    raise SystemExit('Application image exceeds OTA slot')
if len(files['ota-data-initial'].read_bytes()) != 8192:
    raise SystemExit('Invalid OTA data image size')
hashes = {}
for part, path in files.items():
    data = path.read_bytes()
    hashes[part] = hashlib.sha256(data).hexdigest()
    print(f'{part}: {len(data)} bytes, sha256={hashes[part]}')
meta = {
    'device': 'mogo-e4p', 'version': version,
    'channel': 'beta' if '-rc' in version else 'stable',
    'repository': os.environ['GITHUB_REPOSITORY'], 'tag': os.environ['GITHUB_REF_NAME'],
    'commit': os.environ['GITHUB_SHA'], 'runId': os.environ['GITHUB_RUN_ID'],
    'releaseNotes': f'Mogo E4P {version}', 'sha256': hashes,
}
Path('build/platform-submission.json').write_text(json.dumps(meta), encoding='utf-8')
