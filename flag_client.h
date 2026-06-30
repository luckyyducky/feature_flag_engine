#ifndef EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_FLAG_CLIENT_H_
#define EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_FLAG_CLIENT_H_

#include <string>
#include <thread>
#include <atomic>
#include "flag_store.h"

class FeatureFlagClient {
public:
    // Initializes the client with server address and port
    FeatureFlagClient(std::string server_ip, int port, std::string client_token = "client_secret_token_123");
    ~FeatureFlagClient();

    // Connects to the server, sends subscription request, and spawns the background thread.
    // Returns true if connection and subscription succeeded.
    bool Start();

    // Stops the listener thread and closes the socket
    void Stop();

    bool IsEnabled(std::string_view key, const UserContext& context = {});

    // High-performance Remote Configuration lookup.
    // Returns the value cast to T if the flag is enabled and matches type T.
    // Otherwise, returns default_value.
    template<typename T>
    T GetValue(std::string_view key, T default_value, const UserContext& context = {}) {
        if (!IsEnabled(key, context)) {
            return default_value;
        }

        FeatureFlag flag = cache_.GetFlag(key);
        if (std::holds_alternative<T>(flag.value)) {
            return std::get<T>(flag.value);
        }
        return default_value;
    }

private:
    std::string server_ip_;
    int port_;
    std::string client_token_;
    int socket_fd_;
    bool is_running_;

    // Our local thread-safe cache backed by a fallback file
    PersistentCOWFlagStore cache_;

    // Background thread that manages connection and listens for pushes
    std::thread listener_thread_;

    // Thread manager for background reconnect loop
    void ConnectionManagerLoop();

    // The function that runs to receive updates once socket is active
    void ReceiveUpdatesLoop();

    // Authenticate and subscribe helper
    bool AuthenticateAndSubscribe(int fd);
};

#endif  // EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_FLAG_CLIENT_H_
