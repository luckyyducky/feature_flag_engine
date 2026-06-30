#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include "protocol.h"

void PrintUsage() {
    std::cout << "Usage: admin_cli --flag <flag_name> --enable <true/false> "
              << "[--val-bool <true/false> | --val-num <number> | --val-str <string>] "
              << "[--group <group_name>] [--rollout <0-100>] [--token <auth_token>] [--port <number>]" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string flag_name;
    bool enable_val = false;
    bool enable_specified = false;
    std::string group = "";
    int rollout = 100;
    int port = 8888;
    std::string token = "admin_secret_token_999"; // Default token matching server default

    bool val_bool = false;
    bool val_bool_specified = false;
    double val_num = 0.0;
    bool val_num_specified = false;
    std::string val_str = "";
    bool val_str_specified = false;

    // 1. Parse Arguments manually
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--flag" && i + 1 < argc) {
            flag_name = argv[++i];
        } else if (arg == "--enable" && i + 1 < argc) {
            std::string val = argv[++i];
            enable_val = (val == "true" || val == "1");
            enable_specified = true;
        } else if (arg == "--group" && i + 1 < argc) {
            group = argv[++i];
        } else if (arg == "--token" && i + 1 < argc) {
            token = argv[++i];
        } else if (arg == "--rollout" && i + 1 < argc) {
            try {
                rollout = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid rollout value. Must be 0 to 100." << std::endl;
                return 1;
            }
        } else if (arg == "--port" && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid port number." << std::endl;
                return 1;
            }
        } else if (arg == "--val-bool" && i + 1 < argc) {
            std::string val = argv[++i];
            val_bool = (val == "true" || val == "1");
            val_bool_specified = true;
        } else if (arg == "--val-num" && i + 1 < argc) {
            try {
                val_num = std::stod(argv[++i]);
                val_num_specified = true;
            } catch (...) {
                std::cerr << "Error: Invalid numeric value for --val-num." << std::endl;
                return 1;
            }
        } else if (arg == "--val-str" && i + 1 < argc) {
            val_str = argv[++i];
            val_str_specified = true;
        }
    }

    if (flag_name.empty() || !enable_specified) {
        PrintUsage();
        return 1;
    }

    if (rollout < 0 || rollout > 100) {
        std::cerr << "Error: Rollout percentage must be between 0 and 100." << std::endl;
        return 1;
    }

    if (group.length() > 255) {
        std::cerr << "Error: Group name too long (max 255 chars)." << std::endl;
        return 1;
    }

    // Determine the configuration value to store
    FlagValue config_value = enable_val;
    if (val_bool_specified) {
        config_value = val_bool;
    } else if (val_num_specified) {
        config_value = val_num;
    } else if (val_str_specified) {
        config_value = val_str;
    }

    // 2. Connect to Server
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::cerr << "Error: Failed to create socket." << std::endl;
        return 1;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        std::cerr << "Error: Invalid loopback address." << std::endl;
        close(socket_fd);
        return 1;
    }

    std::cout << "[Admin] Connecting to server at 127.0.0.1:" << port << "..." << std::endl;
    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: Connect failed. Is the server running?" << std::endl;
        close(socket_fd);
        return 1;
    }

    // 2.5 Authenticate before sending administrative commands!
    uint8_t auth_header[4];
    auth_header[0] = KProtocolMagic;
    auth_header[1] = KCmdAuth;
    PackUint16(auth_header + 2, token.length());

    if (send(socket_fd, auth_header, 4, 0) < 0) {
        std::cerr << "Error: Failed to send auth header." << std::endl;
        close(socket_fd);
        return 1;
    }
    if (!token.empty()) {
        if (send(socket_fd, token.data(), token.length(), 0) < 0) {
            std::cerr << "Error: Failed to send auth token." << std::endl;
            close(socket_fd);
            return 1;
        }
    }

    uint8_t auth_resp[3];
    if (recv(socket_fd, auth_resp, 3, MSG_WAITALL) < 3) {
        std::cerr << "Error: Server closed connection during auth handshake." << std::endl;
        close(socket_fd);
        return 1;
    }
    if (auth_resp[0] != KProtocolMagic || auth_resp[1] != KStatusOk) {
        std::cerr << "Error: Authentication failed. Server response code: " << (int)auth_resp[1] << std::endl;
        if (auth_resp[1] == KStatusUnauthorized) {
            std::cerr << "Verify your --token argument matches the server's admin token." << std::endl;
        }
        close(socket_fd);
        return 1;
    }

    // 3. Serialize Command payload
    std::vector<uint8_t> payload;
    payload.push_back(enable_val ? 0x01 : 0x00);
    payload.push_back(static_cast<uint8_t>(rollout));
    
    uint8_t group_len = static_cast<uint8_t>(group.length());
    payload.push_back(group_len);
    payload.insert(payload.end(), group.begin(), group.end());

    uint16_t name_len = flag_name.length();
    payload.push_back((name_len >> 8) & 0xFF);
    payload.push_back(name_len & 0xFF);
    payload.insert(payload.end(), flag_name.begin(), flag_name.end());

    SerializeFlagValue(config_value, payload);

    // 4. Construct Request Header for SetFlag
    uint8_t header[4];
    header[0] = KProtocolMagic;
    header[1] = KCmdSetFlag;
    PackUint16(header + 2, payload.size());

    // 5. Send SetFlag Header + Payload
    if (send(socket_fd, header, 4, 0) < 0) {
        std::cerr << "Error: Failed to send header." << std::endl;
        close(socket_fd);
        return 1;
    }
    if (send(socket_fd, payload.data(), payload.size(), 0) < 0) {
        std::cerr << "Error: Failed to send payload." << std::endl;
        close(socket_fd);
        return 1;
    }

    // 6. Receive Response
    uint8_t resp_buf[3];
    int bytes_received = recv(socket_fd, resp_buf, 3, MSG_WAITALL);
    if (bytes_received < 3) {
        std::cerr << "Error: Server closed connection without sending response." << std::endl;
        close(socket_fd);
        return 1;
    }

    uint8_t magic = resp_buf[0];
    uint8_t status = resp_buf[1];

    if (magic != KProtocolMagic) {
        std::cerr << "Error: Mismatched magic byte in response." << std::endl;
        close(socket_fd);
        return 1;
    }

    if (status == KStatusOk) {
        std::cout << "[Admin] SUCCESS: Flag '" << flag_name << "' updated successfully!" << std::endl;
    } else {
        std::cerr << "[Admin] FAILURE: Server rejected update command with status: " << (int)status << std::endl;
    }

    close(socket_fd);
    return 0;
}
