import urllib.request
import json

class FeatureFlagClient:
    def __init__(self, server_url="http://localhost:8000"):
        self.server_url = server_url
        self.flags = {}

    def fetch_flags(self):
        """Fetch flags from the server and update our local cache."""
        try:
            url = f"{self.server_url}/flags"
            # urllib.request is python's built-in HTTP client tool
            with urllib.request.urlopen(url, timeout=2) as response:
                if response.status == 200:
                    data = response.read().decode("utf-8")
                    self.flags = json.loads(data)
                    print("[SDK] Successfully fetched and cached flags from server.")
                else:
                    print(f"[SDK] Failed to fetch flags. Server returned status: {response.status}")
        except Exception as e:
            print(f"[SDK] Error fetching flags from server: {e}")

    def is_enabled(self, flag_name, default=False):
        """Check if a boolean flag is enabled."""
        val = self.flags.get(flag_name, default)
        return bool(val)

    def get_config(self, config_name, default=None):
        """Get a configuration value (like an integer or string)."""
        return self.flags.get(config_name, default)

# Quick local test demo
if __name__ == "__main__":
    client = FeatureFlagClient()
    
    # 1. Fetch flags
    client.fetch_flags()
    
    # 2. Query flags
    print(f"Is 'enable_new_checkout' enabled? -> {client.is_enabled('enable_new_checkout')}")
    print(f"Is 'enable_beta_theme' enabled? -> {client.is_enabled('enable_beta_theme')}")
    print(f"Discount percentage: -> {client.get_config('discount_percentage')}%")
    print(f"Unknown flag status (defaulting to False): -> {client.is_enabled('unknown_flag', default=False)}")
