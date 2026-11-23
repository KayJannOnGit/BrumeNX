#!/usr/bin/env python3
# Petit serveur LOCAL (127.0.0.1 seulement) pour « cuire » les ressources de Brume depuis la maquette.
#  - GET  /render-sounds.html, /bake-assets.html  -> les pages de cuisson (dans brume/tools/)
#  - GET  /m1/...                                  -> les fichiers de la maquette (docs/M1/)
#  - POST /save/<nom>                              -> un WAV  -> brume/assets/sounds/<nom>.wav
#  - POST /asset/<dossier>/<nom>                   -> binaire -> brume/assets/<dossier>/<nom> (models, ui)
# Lancer :  python3 brume/tools/sound_server.py   (port 8767)
import http.server, os, re, sys, posixpath, urllib.parse

TOOLS = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(TOOLS, '..', 'assets')
M1 = os.path.join(TOOLS, '..', '..', 'docs', 'M1')
MAX = 32 * 1024 * 1024          # un fichier ne dépasse jamais 32 Mo

class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=TOOLS, **k)

    def translate_path(self, path):
        p = urllib.parse.urlparse(path).path
        if p.startswith('/m1/'):
            rel = posixpath.normpath(urllib.parse.unquote(p[4:])).lstrip('/')
            if rel.startswith('..'): return os.path.join(TOOLS, '__interdit__')
            return os.path.join(M1, *rel.split('/'))
        return super().translate_path(path)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def write(self, folder, name, data):
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, name), 'wb') as f:
            f.write(data)
        print('enregistré', name, len(data), 'octets', flush=True)
        self.send_response(200); self.end_headers(); self.wfile.write(b'ok')

    def do_POST(self):
        n = int(self.headers.get('Content-Length', '0'))
        if n <= 0 or n > MAX:
            self.send_error(400); return
        m = re.fullmatch(r'/save/([a-z0-9_-]{1,40})', self.path)
        if m:
            data = self.rfile.read(n)
            if n <= 44 or data[:4] != b'RIFF' or data[8:12] != b'WAVE':
                self.send_error(400); return
            return self.write(os.path.join(ASSETS, 'sounds'), m.group(1) + '.wav', data)
        m = re.fullmatch(r'/asset/(models|ui)/([a-z0-9_.-]{1,48})', self.path)
        if m:
            return self.write(os.path.join(ASSETS, m.group(1)), m.group(2), self.rfile.read(n))
        self.send_error(400)

http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1]) if len(sys.argv) > 1 else 8767), H).serve_forever()
