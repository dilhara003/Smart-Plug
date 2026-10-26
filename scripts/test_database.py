#!/usr/bin/env python3
"""Run against an isolated local PostgreSQL DB, never the hosted user database."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
dsn=os.environ.get('SMARTPLUG_TEST_DB','host=/private/tmp/eru-smartplug-pg port=55439 user=postgres dbname=postgres')
if 'supabase' in dsn.lower(): raise SystemExit('Use an isolated local test database.')
for file in ['tests/database.sql','supabase/migrations/202610260001_smartplug.sql','supabase/migrations/202610260001_smartplug.sql','tests/database_assertions.sql']:
 result=subprocess.run(['psql',dsn,'-X','-v','ON_ERROR_STOP=1','-f',str(ROOT/file)],capture_output=True,text=True)
 if result.returncode:
  print(result.stdout);print(result.stderr);raise SystemExit(result.returncode)
 print('PASS:',file)
 if file.endswith('assertions.sql'): print(result.stdout[-450:])
