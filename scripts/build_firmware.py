#!/usr/bin/env python3
"""Reproducible ESP32 build. Downloads only Arduino-registry libraries, into this repo."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CORE = "esp32:esp32@3.3.12"
FQBN = "esp32:esp32:esp32:PartitionScheme=min_spiffs"
cli = os.environ.get("ARDUINO_CLI") or shutil.which("arduino-cli")
if not cli and Path("/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli").exists():
    cli = "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
if not cli:
    sys.exit("Install Arduino CLI or Arduino IDE, then run this again. See RUNNING.md.")

# Refuse to silently use a different core after Arduino IDE updates it.
platforms=json.loads(subprocess.check_output([cli,"core","list","--json"],text=True))
installed=next((p.get("installed_version") for p in platforms.get("platforms",[]) if p.get("id")=="esp32:esp32"),None)
if installed!=CORE.split("@")[1]:
    sys.exit("Install the verified core: arduino-cli core install "+CORE+" --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json")

libs = ROOT / "firmware-libs"
libs.mkdir(exist_ok=True)
for lib in json.loads((ROOT / "scripts/firmware-libraries.json").read_text()):
    folder = libs / lib["folder"]
    if folder.exists():
        continue
    print("Downloading", lib["name"], lib["version"], flush=True)
    payload = urllib.request.urlopen(lib["url"], timeout=90).read()
    if hashlib.sha256(payload).hexdigest() != lib["sha256"]:
        sys.exit("Library checksum mismatch: " + lib["name"])
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        for entry in archive.namelist():
            if Path(entry).is_absolute() or ".." in Path(entry).parts:
                sys.exit("Unsafe library archive")
        archive.extractall(libs)

output = ROOT / "artifacts/firmware"
output.mkdir(parents=True, exist_ok=True)
cmd = [cli, "compile", "--fqbn", FQBN, "--libraries", str(libs),
       "--build-path", str(ROOT / ".arduino-build"), "--output-dir", str(output),
       "--jobs", "4", str(ROOT / "SmartPlug_eru")]
print("Build core:", CORE, "Board:", FQBN, flush=True)
subprocess.run(cmd, check=True)
print("Firmware binaries:", output)
