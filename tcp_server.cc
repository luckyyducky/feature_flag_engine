#include "tcp_server.h"
#include "protocol.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

TcpServer::TcpServer(int port, FlagStore* store, std::string client_token, std::string admin_token) 
    : port_(port), server_fd_(-1), store_(store), is_running_(false), 
      client_token_(client_token), admin_token_(admin_token), thread_pool_(4) {}

TcpServer::~TcpServer() {
    Stop();
}

void TcpServer::Start() {
    // 1. Create Socket
    // AF_INET = IPv4, SOCK_STREAM = TCP
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Error: Failed to create socket." << std::endl;
        return;
    }

    // 2. Set Socket Options: Allow Reusing Port Immediately
    // Without this, if you stop the server, you have to wait 2 minutes to run it again,
    // because the OS keeps the port locked in TIME_WAIT state.
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Error: setsockopt failed." << std::endl;
        return;
    }

    // 3. Bind to Port
    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Accept connections from any network interface
    address.sin_port = htons(port_);      // Convert port number to network byte order (Big Endian)

    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Error: Bind failed on port " << port_ << "." << std::endl;
        return;
    }

    // 4. Start Listening
    // 10 = Backlog size (how many client connections the OS will queue up for us)
    if (listen(server_fd_, 10) < 0) {
        std::cerr << "Error: Listen failed." << std::endl;
        return;
    }

    is_running_ = true;
    std::cout << "[Server] Listening on port " << port_ << "..." << std::endl;

    // 5. Connection Loop
    while (is_running_) {
        struct sockaddr_in client_address;
        socklen_t client_addr_len = sizeof(client_address);

        // accept() blocks until a client connects.
        // It returns a NEW file descriptor (client_fd) just for this client.
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_address, &client_addr_len);
        
        if (client_fd < 0) {
            if (is_running_) {
                std::cerr << "Error: Accept failed." << std::endl;
            }
            break;
        }
        
        std::cout << "[Server] Connection accepted. Socket FD: " << client_fd << std::endl;

        // Enqueue client socket handling into our Thread Pool.
        // This is non-blocking: we immediately return to accept() to take the next client.
        thread_pool_.Enqueue([this, client_fd] {
            HandleClient(client_fd);
        });
    }
}

void TcpServer::HandleClient(int client_fd) {
    std::cout << "[Server] HandleClient running for Socket FD: " << client_fd << std::endl;
    AuthLevel auth_level = AuthLevel::NONE;
    bool keep_connection_open = false;

    while (is_running_) {
        // 1. Read Request Header (exactly 4 bytes)
        uint8_t header_buf[4];
        int bytes_received = recv(client_fd, header_buf, 4, MSG_WAITALL);
        if (bytes_received <= 0) {
            break; // Connection closed or socket error
        }
        if (bytes_received < 4) {
            std::cerr << "[Server] Failed to read 4-byte header from FD: " << client_fd << std::endl;
            ResponseHeader resp{KProtocolMagic, KStatusInvalidMsg, 0};
            send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            break;
        }

        uint8_t magic = header_buf[0];
        uint8_t command = header_buf[1];
        uint16_t length = UnpackUint16(header_buf + 2);

        if (magic != KProtocolMagic) {
            ResponseHeader resp{KProtocolMagic, KStatusInvalidMsg, 0};
            send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            break;
        }

        // 2. Read Payload if length > 0
        std::vector<uint8_t> payload_buf(length);
        if (length > 0) {
            bytes_received = recv(client_fd, payload_buf.data(), length, MSG_WAITALL);
            if (bytes_received < length) {
                std::cerr << "[Server] Failed to read payload from FD: " << client_fd << std::endl;
                break;
            }
        }

        // 3. Security check: If not authenticated, the only allowed command is CMD_AUTH!
        if (auth_level == AuthLevel::NONE && command != KCmdAuth) {
            std::cerr << "[Server] Unauthorized command access attempt (Cmd: " 
                      << (int)command << ") by unauthenticated client FD " << client_fd << std::endl;
            ResponseHeader resp{KProtocolMagic, KStatusUnauthorized, 0};
            send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            break; // Disconnect immediately
        }

        // 4. Execute Command
        if (command == KCmdAuth) {
            std::string token(payload_buf.begin(), payload_buf.end());
            if (token == admin_token_) {
                auth_level = AuthLevel::ADMIN;
                std::cout << "[Server] Client FD " << client_fd << " authenticated as ADMIN." << std::endl;
                ResponseHeader resp{KProtocolMagic, KStatusOk, 0};
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            } else if (token == client_token_) {
                auth_level = AuthLevel::CLIENT;
                std::cout << "[Server] Client FD " << client_fd << " authenticated as CLIENT." << std::endl;
                ResponseHeader resp{KProtocolMagic, KStatusOk, 0};
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            } else {
                std::cerr << "[Server] Auth failed for client FD " << client_fd << std::endl;
                ResponseHeader resp{KProtocolMagic, KStatusUnauthorized, 0};
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
                break; // Disconnect
            }
        } else if (command == KCmdCheck) {
            std::string flag_name(payload_buf.begin(), payload_buf.end());
            bool is_enabled = store_->IsEnabled(flag_name);
            ResponseHeader resp{KProtocolMagic, KStatusOk, static_cast<uint8_t>(is_enabled ? 1 : 0)};
            send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            break; // Check is single-shot, close connection
        } else if (command == KCmdSubscribe) {
            {
                std::lock_guard<std::mutex> lock(subscribers_mutex_);
                subscribers_.push_back(client_fd);
            }
            ResponseHeader resp{KProtocolMagic, KStatusOk, 0};
            send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);

            std::cout << "[Server] New client subscribed. Socket FD: " << client_fd << std::endl;
            SendAllFlagsToClient(client_fd);

            keep_connection_open = true;
            break; // Transition to update stream, stop reading commands
        } else if (command == KCmdSetFlag) {
            // Security check: Only admins can write flags!
            if (auth_level != AuthLevel::ADMIN) {
                std::cerr << "[Server] Forbidden command access attempt (CMD_SET_FLAG) by non-admin client FD " << client_fd << std::endl;
                ResponseHeader resp{KProtocolMagic, KStatusForbidden, 0};
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
                break;
            }

            if (length < 5) {
                ResponseHeader resp{KProtocolMagic, KStatusInvalidMsg, 0};
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
                break;
            }

            uint8_t enabled_val = payload_buf[0];
            uint8_t rollout = payload_buf[1];
            uint8_t group_len = payload_buf[2];

            if (length < 5 + group_len) {
                ResponseHeader resp{KProtocolMagic, KStatusInvalidMsg, 0};
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
                break;
            }
            std::string target_group(payload_buf.begin() + 3, payload_buf.begin() + 3 + group_len);

            uint16_t name_len = UnpackUint16(payload_buf.data() + 3 + group_len);
            if (length < 5 + group_len + name_len + 1) {
                ResponseHeader resp{KProtocolMagic, KStatusInvalidMsg, 0};
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
                break;
            }
            std::string flag_name(payload_buf.begin() + 5 + group_len, payload_buf.begin() + 5 + group_len + name_len);

            size_t val_offset = 5 + group_len + name_len;
            FlagValue value;
            int val_bytes = DeserializeFlagValueFromMemory(payload_buf.data() + val_offset, length - val_offset, value);
            if (val_bytes < 0) {
                ResponseHeader resp{KProtocolMagic, KStatusInvalidMsg, 0};
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
                break;
            }

            std::cout << "[Server] Admin Request: Set '" << flag_name << "' -> enabled=" 
                      << (enabled_val == 0x01 ? "ON" : "OFF") << std::endl;

            UpdateFlagAndNotify(flag_name, enabled_val == 0x01, value, target_group, rollout);

            ResponseHeader resp{KProtocolMagic, KStatusOk, 0};
            send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            break; // Operation completed, close connection
        } else {
            ResponseHeader resp{KProtocolMagic, KStatusInvalidMsg, 0};
            send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            break;
        }
    }

    if (!keep_connection_open) {
        close(client_fd);
    }
}

void TcpServer::Stop() {
    if (is_running_) {
        is_running_ = false;
        if (server_fd_ != -1) {
            // shutdown unblocks accept() on Linux
            shutdown(server_fd_, SHUT_RDWR);
            close(server_fd_);
            server_fd_ = -1;
        }
        
        // Clean up all subscriber sockets
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        for (int fd : subscribers_) {
            close(fd);
        }
        subscribers_.clear();

        std::cout << "[Server] Stopped." << std::endl;
    }
}

size_t TcpServer::GetSubscriberCount() {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    return subscribers_.size();
}

void TcpServer::UpdateFlagAndNotify(const std::string& key, bool enabled, 
                                     FlagValue value,
                                     std::string target_group, uint8_t rollout_percentage) {
    // 1. Update the master store
    store_->SetEnabled(key, enabled, value, target_group, rollout_percentage);
    // 2. Push update packet containing rules and values to all active subscribers
    PushUpdate(key);
}

void TcpServer::PushUpdate(const std::string& key) {
    FeatureFlag flag = store_->GetFlag(key);

    std::lock_guard<std::mutex> lock(subscribers_mutex_);

    // Construct the update event packet:
    std::vector<uint8_t> packet;
    packet.push_back(KProtocolMagic);
    packet.push_back(0x03); // Event type: 0x03 (Update)
    packet.push_back(flag.enabled ? 0x01 : 0x00);
    packet.push_back(flag.rollout_percentage);

    uint8_t group_len = static_cast<uint8_t>(flag.target_group.length());
    packet.push_back(group_len);
    packet.insert(packet.end(), flag.target_group.begin(), flag.target_group.end());

    uint16_t name_len = flag.key.length();
    packet.push_back((name_len >> 8) & 0xFF);
    packet.push_back(name_len & 0xFF);
    packet.insert(packet.end(), flag.key.begin(), flag.key.end());

    // Serialize the configuration value
    SerializeFlagValue(flag.value, packet);

    std::cout << "[Server] Pushing update: '" << flag.key 
              << "' -> enabled=" << (flag.enabled ? "ON" : "OFF")
              << ", group='" << flag.target_group << "'"
              << ", rollout=" << (int)flag.rollout_percentage << "%"
              << " to " << subscribers_.size() << " clients." << std::endl;

    // Send the packet to all active client sockets
    auto it = subscribers_.begin();
    while (it != subscribers_.end()) {
        int client_fd = *it;
        
        // MSG_NOSIGNAL prevents the OS from killing our server with a SIGPIPE signal
        // if the client disconnected. Instead, send() just returns -1.
        int bytes_sent = send(client_fd, packet.data(), packet.size(), MSG_NOSIGNAL);
        
        if (bytes_sent < 0) {
            std::cout << "[Server] Client disconnected (lazy cleanup). Closing socket FD: " 
                      << client_fd << std::endl;
            close(client_fd);
            it = subscribers_.erase(it); // Remove socket and get next valid iterator
        } else {
            ++it;
        }
    }
}

void TcpServer::SendAllFlagsToClient(int client_fd) {
    std::vector<FeatureFlag> flags = store_->GetAllFlags();
    std::cout << "[Server] Bootstrapping client FD " << client_fd 
              << " with " << flags.size() << " flags." << std::endl;
              
    for (const auto& flag : flags) {
        // Construct the update event packet:
        std::vector<uint8_t> packet;
        packet.push_back(KProtocolMagic);
        packet.push_back(0x03); // Event type: 0x03 (Update)
        packet.push_back(flag.enabled ? 0x01 : 0x00);
        packet.push_back(flag.rollout_percentage);

        uint8_t group_len = static_cast<uint8_t>(flag.target_group.length());
        packet.push_back(group_len);
        packet.insert(packet.end(), flag.target_group.begin(), flag.target_group.end());

        uint16_t name_len = flag.key.length();
        packet.push_back((name_len >> 8) & 0xFF);
        packet.push_back(name_len & 0xFF);
        packet.insert(packet.end(), flag.key.begin(), flag.key.end());

        // Serialize the configuration value
        SerializeFlagValue(flag.value, packet);

        send(client_fd, packet.data(), packet.size(), MSG_NOSIGNAL);
    }
}
