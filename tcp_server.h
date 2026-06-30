#ifndef EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_TCP_SERVER_H_
#define EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_TCP_SERVER_H_

#include "flag_store.h"
#include "thread_pool.h"
#include <string>
#include <vector>
#include <mutex>

class TcpServer {
public:
    enum class AuthLevel {
        NONE = 0,
        CLIENT = 1,
        ADMIN = 2
    };

    TcpServer(int port, FlagStore* store, 
              std::string client_token = "client_secret_token_123", 
              std::string admin_token = "admin_secret_token_999");
    ~TcpServer();

    void Start();
    void Stop();

    size_t GetSubscriberCount();

    void UpdateFlagAndNotify(const std::string& key, bool enabled, 
                             FlagValue value = false,
                             std::string target_group = "", uint8_t rollout_percentage = 100);

private:
    int port_;
    int server_fd_;
    FlagStore* store_;
    bool is_running_;
    
    std::string client_token_;
    std::string admin_token_;

    ThreadPool thread_pool_;

    std::vector<int> subscribers_;
    std::mutex subscribers_mutex_;

    void HandleClient(int client_fd);
    void PushUpdate(const std::string& key);
    void SendAllFlagsToClient(int client_fd);
};

#endif  // EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_TCP_SERVER_H_
