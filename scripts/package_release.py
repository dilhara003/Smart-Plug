#!/usr/bin/env python3
"""Package sources, production UI and component firmware, excluding private data."""
from pathlib import Path
import hashlib,json,zipfile
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts'
OUT.mkdir(exist_ok=True)
version=json.loads((ROOT/'package.json').read_text())['version']
archive=OUT/('ERU-SmartPlug-'+version+'.zip')
roots=['SmartPlug_eru','web','scripts','tests','supabase','dist']
files=['README.md','RUNNING.md','package.json','package-lock.json','tsconfig.json','vite.config.ts','playwright.config.ts','.env.example','.gitignore','ERU_Smart_Plug_README.md','SMARTPLUG_REVIEW.md']
entries=[ROOT/f for f in files]
for folder in roots:
 entries.extend(p for p in (ROOT/folder).rglob('*') if p.is_file() and 'node_modules' not in p.parts and '__pycache__' not in p.parts and 'build' not in p.relative_to(ROOT).parts and not p.name.endswith('.local.h'))
entries.extend(p for p in (OUT/'firmware').glob('*.bin') if not p.name.endswith('.merged.bin'))
metadata={'version':version,'board':'ESP32 Dev Module, 4MB','core':'esp32:esp32@3.3.12','fqbn':'esp32:esp32:esp32:PartitionScheme=min_spiffs','files':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in entries if p.suffix=='.bin'}}
(OUT/'BUILD_MANIFEST.json').write_text(json.dumps(metadata,indent=2)+'\n')
entries.append(OUT/'BUILD_MANIFEST.json')
entries.extend(OUT/name for name in ['VERIFICATION.md','live-verification.json','dashboard-1440.png','dashboard-390.png','dashboard-320.png'] if (OUT/name).exists())
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(set(entries)):
  relative=p.relative_to(ROOT)
  assert 'owner-login' not in str(relative).lower() and 'OWNER_LOGIN' not in str(relative)
  z.write(p,Path('ERU-SmartPlug')/relative)
print(archive)
print('Size:',round(archive.stat().st_size/1048576,2),'MiB')
