#!/usr/bin/env bash
set -e

# Ensure binaries are compiled and ready
g++ -std=c++17 main.cc flag_store.cc tcp_server.cc -o server -pthread 2>/dev/null || true
g++ -std=c++17 client_main.cc flag_client.cc flag_store.cc -o client -pthread 2>/dev/null || true
g++ -std=c++17 admin_cli.cc -o admin_cli 2>/dev/null || true
g++ -std=c++17 benchmark.cc flag_store.cc -o benchmark -pthread 2>/dev/null || true

clear
echo "================================================================================="
echo "        SELF-HOSTED FEATURE FLAG & REMOTE CONFIGURATION ENGINE - DEMO"
echo "================================================================================="
echo ""
echo "Welcome! In this live automated presentation, we will demonstrate:"
echo "  1. Multi-Threaded Performance Benchmarks & Lock-Free COW Scaling"
echo "  2. Database Persistence & Offline Fallback Cache Files"
echo "  3. Administrative API Mutation Endpoints & Security Rejection"
echo "  4. Live Backend TCP Server & Real-Time Client Push Synchronization"
echo "  5. Start-to-Finish Codebase & Architectural Walkthrough"
echo ""
echo "---------------------------------------------------------------------------------"
read -p ">> Press [ENTER] on your keyboard when ready to start Part 1..." _
echo ""

clear
echo "================================================================================="
echo "       [PART 1]: MULTI-THREADED BENCHMARK HARNESS & LOCK-FREE COW SCALING"
echo "================================================================================="
echo "Executing: ./benchmark"
echo "---------------------------------------------------------------------------------"
./benchmark
echo "---------------------------------------------------------------------------------"
echo ""
read -p ">> Press [ENTER] on your keyboard when ready to start Part 2..." _
echo ""

clear
echo "================================================================================="
echo "       [PART 2]: DATABASE PERSISTENCE & OFFLINE CACHE RESILIENCE"
echo "================================================================================="
echo "1. Displaying Central Server Database (flags_config.txt):"
echo "---------------------------------------------------------------------------------"
cat flags_config.txt
echo "---------------------------------------------------------------------------------"
echo ""
echo "2. Displaying Client Backup Offline Cache (client_cache_fallback.txt):"
echo "---------------------------------------------------------------------------------"
cat client_cache_fallback.txt
echo "---------------------------------------------------------------------------------"
echo ""
read -p ">> Press [ENTER] on your keyboard when ready to start Part 3..." _
echo ""

clear
echo "================================================================================="
echo "       [PART 3]: ADMINISTRATIVE API ENDPOINT & SECURITY REJECTION"
echo "================================================================================="
echo "1. Testing Valid API Mutation (Turning new_checkout_flow ON for beta users):"
echo "---------------------------------------------------------------------------------"
./admin_cli --flag new_checkout_flow --enable true --val-str "v2.0.0" --group "beta" --rollout 50 --token admin_secret_token_999
echo "---------------------------------------------------------------------------------"
echo ""
echo "2. Testing Security Rejection with Invalid Token ('wrong_token'):"
echo "---------------------------------------------------------------------------------"
./admin_cli --flag new_checkout_flow --enable false --token wrong_token || true
echo "---------------------------------------------------------------------------------"
echo ""
read -p ">> Press [ENTER] on your keyboard when ready to start Part 4..." _
echo ""

clear
echo "================================================================================="
echo "       [PART 4]: LIVE BACKEND SERVER & REAL-TIME PUSH SYNCHRONIZATION"
echo "================================================================================="
echo "1. Starting Central C++ TCP Server in Background (Port 8888)..."
./server &
SERVER_PID=$!
sleep 1

echo ""
echo "2. Starting C++ Client Simulator (Connecting & Synchronizing)..."
echo "---------------------------------------------------------------------------------"
./client &
CLIENT_PID=$!
sleep 2

echo ""
echo "3. Mutating Feature Flag in Real-Time via API (Broadcasts to Client via TCP)..."
echo "---------------------------------------------------------------------------------"
./admin_cli --flag new_checkout_flow --enable false --group "beta" --rollout 50 --token admin_secret_token_999
sleep 2

# Clean up background processes
kill $CLIENT_PID 2>/dev/null || true
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "---------------------------------------------------------------------------------"
echo ""
read -p ">> Press [ENTER] on your keyboard when ready to start Part 5..." _
echo ""

clear
echo "================================================================================="
echo "       [PART 5]: START-TO-FINISH CODEBASE ARCHITECTURAL WALKTHROUGH"
echo "================================================================================="
echo ""
echo "Please open your code editor or GitHub repo in your browser now to walk"
echo "through the complete system pipeline from start to finish:"
echo ""
echo "  1. Protocol Layer ('protocol.h' & 'protocol.dart'):"
echo "     -> Custom 4-byte TLV headers and type tags (bool, double, string)."
echo ""
echo "  2. Core Engine ('flag_store.h' & 'flag_store.cc'):"
echo "     -> Zero-allocation std::string_view lookups in COWFlagStore (lines 98-109)."
echo "     -> Bitwise stack math rollout hashing in EvaluateRules (lines 27-31)."
echo "     -> Asynchronous disk persistence in PersistentCOWFlagStore."
echo ""
echo "  3. Network Layer ('main.cc' & 'tcp_server.cc'):"
echo "     -> Stateful socket authentication (Read-Only CLIENT vs Write ADMIN)."
echo "     -> Real-time 5-byte TLV TCP loopback broadcasts (lines 273-305)."
echo ""
echo "  4. Control Plane ('admin_cli.cc' & 'admin_tui.py'):"
echo "     -> CI/CD scripting CLI and interactive Python curses dashboard."
echo ""
echo "  5. Client Integration ('flag_client.cc' & 'flutter_client/'):"
echo "     -> Reactive Flutter SDK extending ChangeNotifier with auto-reconnect."
echo "     -> Zero-downtime offline cache failover ('client_cache_fallback.txt')."
echo ""
echo "================================================================================="
echo ""
read -p ">> Press [ENTER] to exit presentation..." _
clear
