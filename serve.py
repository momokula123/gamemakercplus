import http.server
import os
os.chdir(r'C:\Users\Administrator\Documents\gamma')
handler = http.server.SimpleHTTPRequestHandler
server = http.server.HTTPServer(('127.0.0.1', 18081), handler)
print('Serving on http://localhost:18081/')
server.serve_forever()
