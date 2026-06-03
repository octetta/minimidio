#!/bin/bash

# Define the Python code as a variable
PYTHON_SERVER=$(cat <<EOF
from http import server

class ThreadedHTTPServer(server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

# Use 0.0.0.0 to allow access from other devices if needed, or 127.0.0.1 for local only
server.test(HandlerClass=ThreadedHTTPServer, port=8000)
EOF
)

echo "Starting Skred Dev Server on http://localhost:8000"
echo "COOP/COEP headers enabled for Wasm pthreads."

# Execute Python and pass the string via stdin
python3 -c "$PYTHON_SERVER"
