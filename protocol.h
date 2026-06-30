#ifndef FEATURE_FLAG_ENGINE_PROTOCOL_H_
#define FEATURE_FLAG_ENGINE_PROTOCOL_H_

#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <cstring>
#include <sys/socket.h>

// --- PROTOCOL CONSTANTS ---
constexpr uint8_t KProtocolMagic = 0x46; // 'F' in ASCII

// Commands
constexpr uint8_t KCmdCheck = 0x01;
constexpr uint8_t KCmdSubscribe = 0x02;
constexpr uint8_t KCmdSetFlag = 0x04;
constexpr uint8_t KCmdAuth = 0x05;

// Response Statuses
constexpr uint8_t KStatusOk = 0x00;
constexpr uint8_t KStatusError = 0x01;
constexpr uint8_t KStatusInvalidMsg = 0x02;
constexpr uint8_t KStatusUnauthorized = 0x03;
constexpr uint8_t KStatusForbidden = 0x04;

// Value Type Tags
constexpr uint8_t KTagBool = 0x01;
constexpr uint8_t KTagDouble = 0x02;
constexpr uint8_t KTagString = 0x03;

// Unified Remote Config Value Type
using FlagValue = std::variant<bool, double, std::string>;

// --- PACKET HEADERS ---

// Request Header: [Magic (1B)] [Command (1B)] [Payload Length (2B)]
struct RequestHeader {
    uint8_t magic;
    uint8_t command;
    uint16_t length;
};

// Response Header: [Magic (1B)] [Status (1B)] [Value (1B)]
struct ResponseHeader {
    uint8_t magic;
    uint8_t status;
    uint8_t value; // 0x01 = ON, 0x00 = OFF
};

// --- SERIALIZATION HELPERS ---

// Packs a 16-bit integer into a byte buffer (Big Endian)
inline void PackUint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = static_cast<uint8_t>((value >> 8) & 0xFF); // High byte
    buffer[1] = static_cast<uint8_t>(value & 0xFF);        // Low byte
}

// Unpacks a 16-bit integer from a byte buffer (Big Endian)
inline uint16_t UnpackUint16(const uint8_t* buffer) {
    return (static_cast<uint16_t>(buffer[0]) << 8) | static_cast<uint16_t>(buffer[1]);
}

// Serializes a FlagValue variant and appends it to a packet vector
inline void SerializeFlagValue(const FlagValue& val, std::vector<uint8_t>& packet) {
    if (std::holds_alternative<bool>(val)) {
        packet.push_back(KTagBool);
        packet.push_back(std::get<bool>(val) ? 0x01 : 0x00);
    } else if (std::holds_alternative<double>(val)) {
        packet.push_back(KTagDouble);
        double d = std::get<double>(val);
        uint8_t bytes[8];
        std::memcpy(bytes, &d, 8);
        packet.insert(packet.end(), bytes, bytes + 8);
    } else if (std::holds_alternative<std::string>(val)) {
        packet.push_back(KTagString);
        std::string s = std::get<std::string>(val);
        uint16_t len = s.length();
        packet.push_back((len >> 8) & 0xFF);
        packet.push_back(len & 0xFF);
        packet.insert(packet.end(), s.begin(), s.end());
    }
}

// Deserializes a FlagValue directly from a socket file descriptor.
// Returns true on success, false on socket error.
inline bool DeserializeFlagValueFromSocket(int fd, FlagValue& val) {
    uint8_t tag;
    if (recv(fd, &tag, 1, MSG_WAITALL) < 1) return false;

    if (tag == KTagBool) {
        uint8_t bool_val;
        if (recv(fd, &bool_val, 1, MSG_WAITALL) < 1) return false;
        val = (bool_val == 0x01);
    } else if (tag == KTagDouble) {
        double d_val;
        if (recv(fd, &d_val, 8, MSG_WAITALL) < 8) return false;
        val = d_val;
    } else if (tag == KTagString) {
        uint8_t len_buf[2];
        if (recv(fd, &len_buf, 2, MSG_WAITALL) < 2) return false;
        uint16_t len = UnpackUint16(len_buf);
        
        std::string s_val;
        if (len > 0) {
            s_val.resize(len);
            if (recv(fd, &s_val[0], len, MSG_WAITALL) < len) return false;
        }
        val = s_val;
    } else {
        return false; // Unknown type tag
    }
    return true;
}

// Deserializes a FlagValue from a memory buffer.
// Returns the number of bytes read, or -1 on error.
inline int DeserializeFlagValueFromMemory(const uint8_t* buffer, size_t max_len, FlagValue& val) {
    if (max_len < 1) return -1;
    uint8_t tag = buffer[0];
    int bytes_read = 1;

    if (tag == KTagBool) {
        if (max_len < 2) return -1;
        val = (buffer[1] == 0x01);
        bytes_read += 1;
    } else if (tag == KTagDouble) {
        if (max_len < 9) return -1;
        double d;
        std::memcpy(&d, buffer + 1, 8);
        val = d;
        bytes_read += 8;
    } else if (tag == KTagString) {
        if (max_len < 3) return -1;
        uint16_t len = UnpackUint16(buffer + 1);
        if (max_len < 3 + len) return -1;
        std::string s(reinterpret_cast<const char*>(buffer + 3), len);
        val = s;
        bytes_read += 2 + len;
    } else {
        return -1; // Unknown tag
    }
    return bytes_read;
}

#endif  // FEATURE_FLAG_ENGINE_PROTOCOL_H_
