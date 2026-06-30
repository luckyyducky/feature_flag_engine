#include <iostream>
#include <csignal>
#include "flag_store.h"
#include "tcp_server.h"

// Global pointer to server for signal handling
TcpServer* g_server = nullptr;

// Signal handler to allow clean shutdown on Ctrl+C (SIGINT)
void SignalHandler(int signum) {
    std::cout << "\n[Server] Signal (" << signum << ") received. Shutting down..." << std::endl;
    if (g_server) {
        g_server->Stop();
    }
}

int main() {
    std::cout << "=== Starting Feature Flag Sync Server ===" << std::endl;

    // Register signal handler
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    // Initialize File-Backed Store locally
    std::string config_file = "flags_config.txt";
    PersistentCOWFlagStore store(config_file);

    // Pre-populate with default flags if the file is fresh/empty
    if (store.GetAllFlags().empty()) {
        std::cout << "[Server] Config file empty. Pre-populating default flags..." << std::endl;
        store.SetEnabled("feature_alpha", true, std::string("v1.0.0"), "beta", 100);
        store.SetEnabled("feature_beta", true, 42.0, "", 100);
    }

    TcpServer server(8888, &store);
    g_server = &server;

    // Start the server (blocking call)
    server.Start();

    std::cout << "=== Server Terminated Cleanly ===" << std::endl;
    return 0;
}
