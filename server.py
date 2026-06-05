import json
from http.server import BaseHTTPRequestHandler, HTTPServer

# In-memory dictionary representing our current feature flags
FEATURE_FLAGS = {
    "enable_new_checkout": True,
    "discount_percentage": 15,
    "enable_beta_theme": False,
}

class FeatureFlagHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        # We only want to handle requests at the path "/flags"
        if self.path == "/flags":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            
            # Serialize our python dictionary to a JSON string and send it
            response_data = json.dumps(FEATURE_FLAGS)
            self.wfile.write(response_data.encode("utf-8"))
        else:
            # Return 404 Not Found for any other path
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Not Found")

def run(port=8000):
    server_address = ("", port)
    httpd = HTTPServer(server_address, FeatureFlagHandler)
    print(f"Feature Flag Server running on port {port}...")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
        httpd.server_close()

if __name__ == "__main__":
    run()
