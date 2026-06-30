#include "flag_client.h"
#include "protocol.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>

FeatureFlagClient::FeatureFlagClient(std::string server_ip, int port, std::string client_token) 
    : server_ip_(server_ip), 
      port_(port), 
      client_token_(client_token),
      socket_fd_(-1), 
      is_running_(false),
      cache_("client_cache_fallback.txt") {}

FeatureFlagClient::~FeatureFlagClient() {
    Stop();
}

bool FeatureFlagClient::AuthenticateAndSubscribe(int fd) {
    // 1. Send CMD_AUTH header & token
    uint8_t auth_header[4];
    auth_header[0] = KProtocolMagic;
    auth_header[1] = KCmdAuth;
    PackUint16(auth_header + 2, client_token_.length());

    if (send(fd, auth_header, 4, 0) < 0) return false;
    if (!client_token_.empty()) {
        if (send(fd, client_token_.data(), client_token_.length(), 0) < 0) return false;
    }

    // 2. Read Auth Response
    uint8_t auth_resp_buf[3];
    if (recv(fd, auth_resp_buf, 3, MSG_WAITALL) < 3) return false;
    if (auth_resp_buf[0] != KProtocolMagic || auth_resp_buf[1] != KStatusOk) {
        std::cerr << "[SDK] Auth rejected by server. Status: " << (int)auth_resp_buf[1] << std::endl;
        return false;
    }

    // 3. Send CMD_SUBSCRIBE header
    uint8_t sub_header[4];
    sub_header[0] = KProtocolMagic;
    sub_header[1] = KCmdSubscribe;
    PackUint16(sub_header + 2, 0);

    if (send(fd, sub_header, 4, 0) < 0) return false;

    // 4. Read Subscription Response
    uint8_t sub_resp_buf[3];
    if (recv(fd, sub_resp_buf, 3, MSG_WAITALL) < 3) return false;
    if (sub_resp_buf[0] != KProtocolMagic || sub_resp_buf[1] != KStatusOk) {
        std::cerr << "[SDK] Subscription rejected by server. Status: " << (int)sub_resp_buf[1] << std::endl;
        return false;
    }

    return true;
}

bool FeatureFlagClient::Start() {
    is_running_ = true;
    bool initially_connected = false;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);

        if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) > 0) {
            if (connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) >= 0) {
                if (AuthenticateAndSubscribe(fd)) {
                    socket_fd_ = fd;
                    initially_connected = true;
                    std::cout << "[SDK] Connected, authenticated, and subscribed to Feature Flag Server." << std::endl;
                }
            }
        }
        if (!initially_connected) {
            close(fd);
        }
    }

    if (!initially_connected) {
        std::cout << "[SDK] Warning: Server at " << server_ip_ << ":" << port_ 
                  << " unreachable/auth failed. Operating in Offline Fallback mode using client_cache_fallback.txt" << std::endl;
    }

    // Spawn the background connection manager thread
    listener_thread_ = std::thread(&FeatureFlagClient::ConnectionManagerLoop, this);
    return true;
}

bool FeatureFlagClient::IsEnabled(std::string_view key, const UserContext& context) {
    return cache_.IsEnabled(key, context);
}

void FeatureFlagClient::ConnectionManagerLoop() {
    int backoff_sec = 1;
    while (is_running_) {
        if (socket_fd_ != -1) {
            ReceiveUpdatesLoop(); // Run push listener blocking
            backoff_sec = 1; // Reset backoff upon clean session end
        } else {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            bool success = false;
            if (fd >= 0) {
                struct sockaddr_in server_addr;
                std::memset(&server_addr, 0, sizeof(server_addr));
                server_addr.sin_family = AF_INET;
                server_addr.sin_port = htons(port_);
                
                if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) > 0) {
                    if (connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) >= 0) {
                        if (AuthenticateAndSubscribe(fd)) {
                            std::cout << "[SDK] Connected to Feature Flag Server (Background Sync Restored)." << std::endl;
                            socket_fd_ = fd;
                            backoff_sec = 1;
                            success = true;
                        }
                    }
                }
                if (!success) {
                    close(fd);
                }
            }
            if (!success) {
                std::cout << "[SDK] Reconnect failed. Retrying in " << backoff_sec << " seconds..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(backoff_sec));
                backoff_sec = std::min(backoff_sec * 2, 60); // Cap at 60 seconds
            }
        }
    }
}


void FeatureFlagClient::ReceiveUpdatesLoop() {
    while (is_running_ && socket_fd_ != -1) {
        // 1. Read push update header (5 bytes)
        uint8_t header_buf[5];
        int bytes_received = recv(socket_fd_, header_buf, 5, MSG_WAITALL);
        if (bytes_received < 5) {
            if (is_running_) {
                std::cout << "[SDK] Connection to server lost. Reconnecting in background..." << std::endl;
            }
            break;
        }

        uint8_t magic = header_buf[0];
        uint8_t event_type = header_buf[1];
        uint8_t value = header_buf[2];
        uint8_t rollout_percentage = header_buf[3];
        uint8_t group_len = header_buf[4];

        if (magic != KProtocolMagic) {
            continue;
        }

        if (event_type == 0x03) { // Update event
            std::string target_group;
            if (group_len > 0) {
                target_group.resize(group_len);
                bytes_received = recv(socket_fd_, &target_group[0], group_len, MSG_WAITALL);
                if (bytes_received < group_len) {
                    break;
                }
            }

            uint8_t len_buf[2];
            bytes_received = recv(socket_fd_, len_buf, 2, MSG_WAITALL);
            if (bytes_received < 2) {
                break;
            }
            uint16_t name_len = UnpackUint16(len_buf);

            std::string flag_name;
            flag_name.resize(name_len);
            bytes_received = recv(socket_fd_, &flag_name[0], name_len, MSG_WAITALL);
            if (bytes_received < name_len) {
                break;
            }
            bool enabled = (value == 0x01);

            FlagValue flag_val;
            if (!DeserializeFlagValueFromSocket(socket_fd_, flag_val)) {
                break;
            }

            cache_.SetEnabled(flag_name, enabled, flag_val, target_group, rollout_percentage);
            
            std::cout << "[SDK] Cache Updated: '" << flag_name << "' -> enabled=" 
                      << (enabled ? "ON" : "OFF")
                      << ", group='" << target_group << "'"
                      << ", rollout=" << (int)rollout_percentage << "%";

            if (std::holds_alternative<bool>(flag_val)) {
                std::cout << ", value=" << (std::get<bool>(flag_val) ? "true" : "false");
            } else if (std::holds_alternative<double>(flag_val)) {
                std::cout << ", value=" << std::get<double>(flag_val);
            } else if (std::holds_alternative<std::string>(flag_val)) {
                std::cout << ", value='" << std::get<std::string>(flag_val) << "'";
            }
            std::cout << std::endl;
        }
    }

    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void FeatureFlagClient::Stop() {
    is_running_ = false;
    if (socket_fd_ != -1) {
        shutdown(socket_fd_, SHUT_RDWR);
        close(socket_fd_);
        socket_fd_ = -1;
    }
    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
}
