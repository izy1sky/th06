# Serves gen.html and saves the atlases it POSTs back (atlas PNG + JSON rects).
import http.server
import json
import os
import base64
import sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers["Content-Length"])
        body = json.loads(self.rfile.read(length))
        name = os.path.basename(body["name"])
        png = base64.b64decode(body["png"].split(",", 1)[1])
        with open(os.path.join(OUT, name + ".png"), "wb") as f:
            f.write(png)
        with open(os.path.join(OUT, name + ".json"), "w") as f:
            json.dump(body["rects"], f, ensure_ascii=False, indent=1)
        print("saved", name, len(png), "bytes", len(body["rects"]), "sprites", flush=True)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")


os.chdir(os.path.dirname(os.path.abspath(__file__)))
http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
