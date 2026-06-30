#!/usr/bin/env bash
set -e

# Ensure binaries are compiled and ready silently
g++ -std=c++17 main.cc flag_store.cc tcp_server.cc thread_pool.cc -o server -pthread 2>/dev/null || true
g++ -std=c++17 client_main.cc flag_client.cc flag_store.cc -o client -pthread 2>/dev/null || true
g++ -std=c++17 admin_cli.cc -o admin_cli 2>/dev/null || true
g++ -std=c++17 benchmark.cc flag_store.cc -o benchmark -pthread 2>/dev/null || true

# Start backend server silently in background so CLI and client commands always succeed
killall server client 2>/dev/null || true
./server >/dev/null 2>&1 &
SERVER_PID=$!
sleep 1

cleanup() {
    kill $SERVER_PID 2>/dev/null || true
    killall server client 2>/dev/null || true
}
trap cleanup EXIT

clear
echo "# Self-Hosted Feature Flag & Remote Config Engine - Live Terminal Demo"
echo "# Press [Enter] at each step to advance through the workflow..."
echo ""
read -p "$ " _

clear
echo "# 1. Running performance benchmarks (Mutex vs Lock-Free Copy-On-Write):"
echo "$ ./benchmark"
./benchmark
echo ""
read -p "$ " _

clear
echo "# 2. Inspecting local database storage and offline fallback cache:"
echo "$ cat flags_config.txt"
cat flags_config.txt
echo ""
echo "$ cat client_cache_fallback.txt"
cat client_cache_fallback.txt
echo ""
read -p "$ " _

clear
echo "# 3. Testing administrative CLI mutations and token auth security:"
echo "$ ./admin_cli --flag new_checkout_flow --enable true --val-str \"v2.0.0\" --group \"beta\" --rollout 50 --token admin_secret_token_999"
./admin_cli --flag new_checkout_flow --enable true --val-str "v2.0.0" --group "beta" --rollout 50 --token admin_secret_token_999
echo ""
echo "$ ./admin_cli --flag new_checkout_flow --enable false --token wrong_token"
./admin_cli --flag new_checkout_flow --enable false --token wrong_token || true
echo ""
read -p "$ " _

clear
echo "# 4. Launching client simulator to observe real-time TCP push synchronization:"
echo "$ ./client &"
./client &
CLIENT_PID=$!
sleep 2

echo ""
echo "# Mutating flag via CLI to trigger instant live push update to client:"
echo "$ ./admin_cli --flag dark_mode_beta --enable true --val-str \"true\" --group \"\" --rollout 100 --token admin_secret_token_999"
./admin_cli --flag dark_mode_beta --enable true --val-str "true" --group "" --rollout 100 --token admin_secret_token_999
sleep 2

kill $CLIENT_PID 2>/dev/null || true
echo ""
read -p "$ " _

clear
echo "# 5. Codebase Walkthrough"
echo "# Please open your VS Code file sidebar now to view the implementation:"
echo "#   - flag_store.cc (lines 98-109 for lock-free COW, lines 27-31 for bitwise rollout hash)"
echo "#   - tcp_server.cc (lines 273-305 for 5-byte TLV socket broadcast)"
echo "#   - flutter_client/lib/feature_flag_client.dart (reactive ChangeNotifier client)"
echo ""
read -p "$ " _
clear
