#!/usr/bin/env python3
"""Upload the already-built component binaries using an explicitly selected USB port."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True,help='Explicit board port, e.g. /dev/cu.usbserial-0001 or COM4')
args=parser.parse_args()
cli=os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli')
bundled=Path('/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli')
if not cli and bundled.exists(): cli=str(bundled)
if not cli: parser.error('Arduino CLI/IDE not installed; see RUNNING.md')
if not (ROOT/'artifacts/firmware/SmartPlug_eru.ino.bin').exists(): parser.error('Run npm run firmware:build first')
print('Uploading to',args.port,'— verify this is your ESP32 and disconnect controlled loads before proceeding.',flush=True)
subprocess.run([cli,'upload','--fqbn','esp32:esp32:esp32:PartitionScheme=min_spiffs','--port',args.port,'--input-dir',str(ROOT/'artifacts/firmware'),str(ROOT/'SmartPlug_eru')],check=True)
