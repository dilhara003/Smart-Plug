from http.server import HTTPServer,BaseHTTPRequestHandler
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class Handler(BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path!='/migration.txt': self.send_error(404); return
  data=(ROOT/'supabase/migrations/202610260001_smartplug.sql').read_bytes()
  self.send_response(200); self.send_header('Content-Type','text/plain; charset=utf-8'); self.send_header('Cache-Control','no-store'); self.end_headers(); self.wfile.write(data)
HTTPServer(('127.0.0.1',5174),Handler).serve_forever()
