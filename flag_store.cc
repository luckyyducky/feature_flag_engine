#include "flag_store.h"
#include <functional>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace {

// Helper function to evaluate targeting rules.
bool EvaluateRules(const FeatureFlag& flag, const UserContext& context) {
    if (!flag.enabled) {
        return false;
    }

    if (!flag.target_group.empty()) {
        if (context.group != flag.target_group) {
            return false;
        }
    }

    if (flag.rollout_percentage < 100) {
        if (context.user_id.empty()) {
            return false;
        }
        
        // Zero-allocation hash combination
        size_t h1 = std::hash<std::string>{}(context.user_id);
        size_t h2 = std::hash<std::string>{}(flag.key);
        size_t hash_val = h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        uint8_t bucket = hash_val % 100;
        
        if (bucket >= flag.rollout_percentage) {
            return false;
        }
    }

    return true;
}

} // namespace

// --- IMPLEMENTING LOCKED FLAG STORE (Sorted Vector) ---

bool LockedFlagStore::IsEnabled(std::string_view key, const UserContext& context) {
    std::shared_lock<std::shared_mutex> read_lock(mutex_);

    // Binary search on sorted vector
    auto it = std::lower_bound(flags_.begin(), flags_.end(), key, 
                               [](const FeatureFlag& flag, std::string_view k) {
                                   return flag.key < k;
                               });
    if (it == flags_.end() || it->key != key) {
        return false;
    }
    return EvaluateRules(*it, context);
}

FeatureFlag LockedFlagStore::GetFlag(std::string_view key) {
    std::shared_lock<std::shared_mutex> read_lock(mutex_);
    auto it = std::lower_bound(flags_.begin(), flags_.end(), key, 
                               [](const FeatureFlag& flag, std::string_view k) {
                                   return flag.key < k;
                               });
    if (it == flags_.end() || it->key != key) {
        return FeatureFlag{std::string(key), false, "", 100, false};
    }
    return *it;
}

std::vector<FeatureFlag> LockedFlagStore::GetAllFlags() {
    std::shared_lock<std::shared_mutex> read_lock(mutex_);
    return flags_;
}

void LockedFlagStore::SetEnabled(const std::string& key, bool enabled, 
                                 FlagValue value, std::string target_group, uint8_t rollout_percentage) {
    std::unique_lock<std::shared_mutex> write_lock(mutex_);
    auto it = std::lower_bound(flags_.begin(), flags_.end(), key, 
                               [](const FeatureFlag& flag, std::string_view k) {
                                   return flag.key < k;
                               });
    FeatureFlag new_flag{key, enabled, target_group, rollout_percentage, value};
    if (it != flags_.end() && it->key == key) {
        *it = new_flag;
    } else {
        flags_.insert(it, new_flag);
    }
}

// --- IMPLEMENTING COPY-ON-WRITE (COW) FLAG STORE (Sorted Vector) ---

COWFlagStore::COWFlagStore() {
    auto empty_vec = std::make_shared<FlagVector>();
    std::atomic_store(&flags_ptr_, empty_vec);
}

bool COWFlagStore::IsEnabled(std::string_view key, const UserContext& context) {
    std::shared_ptr<FlagVector> current_vec = std::atomic_load(&flags_ptr_);

    // Binary search on sorted atomic vector copy
    auto it = std::lower_bound(current_vec->begin(), current_vec->end(), key, 
                               [](const FeatureFlag& flag, std::string_view k) {
                                   return flag.key < k;
                               });
    if (it == current_vec->end() || it->key != key) {
        return false;
    }
    return EvaluateRules(*it, context);
}

FeatureFlag COWFlagStore::GetFlag(std::string_view key) {
    std::shared_ptr<FlagVector> current_vec = std::atomic_load(&flags_ptr_);
    auto it = std::lower_bound(current_vec->begin(), current_vec->end(), key, 
                               [](const FeatureFlag& flag, std::string_view k) {
                                   return flag.key < k;
                               });
    if (it == current_vec->end() || it->key != key) {
        return FeatureFlag{std::string(key), false, "", 100, false};
    }
    return *it;
}

std::vector<FeatureFlag> COWFlagStore::GetAllFlags() {
    std::shared_ptr<FlagVector> current_vec = std::atomic_load(&flags_ptr_);
    return *current_vec;
}

void COWFlagStore::SetEnabled(const std::string& key, bool enabled, 
                             FlagValue value, std::string target_group, uint8_t rollout_percentage) {
    std::lock_guard<std::mutex> lock(write_mutex_);

    std::shared_ptr<FlagVector> current_vec = std::atomic_load(&flags_ptr_);
    auto new_vec = std::make_shared<FlagVector>(*current_vec);

    auto it = std::lower_bound(new_vec->begin(), new_vec->end(), key, 
                               [](const FeatureFlag& flag, std::string_view k) {
                                   return flag.key < k;
                               });
    FeatureFlag new_flag{key, enabled, target_group, rollout_percentage, value};
    if (it != new_vec->end() && it->key == key) {
        *it = new_flag;
    } else {
        new_vec->insert(it, new_flag);
    }

    std::atomic_store(&flags_ptr_, new_vec);
}

// --- IMPLEMENTING PERSISTENT COW FLAG STORE ---

PersistentCOWFlagStore::PersistentCOWFlagStore(std::string filepath) 
    : filepath_(filepath), pending_save_(false), stop_writer_(false) {
    LoadFromFile();
    writer_thread_ = std::thread(&PersistentCOWFlagStore::WriterWorkerLoop, this);
}

PersistentCOWFlagStore::~PersistentCOWFlagStore() {
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        stop_writer_ = true;
    }
    cv_.notify_one();
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
}

void PersistentCOWFlagStore::SetEnabled(const std::string& key, bool enabled, 
                                         FlagValue value, std::string target_group, uint8_t rollout_percentage) {
    COWFlagStore::SetEnabled(key, enabled, value, target_group, rollout_percentage);
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        pending_save_ = true;
    }
    cv_.notify_one();
}


void PersistentCOWFlagStore::LoadFromFile() {
    std::ifstream file(filepath_);
    if (!file.is_open()) {
        std::cout << "[Store] No config file found at '" << filepath_ << "'. Starting with empty store." << std::endl;
        return;
    }

    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string key, enabled_str, group, rollout_str, type_str, val_str;

        std::getline(ss, key, ',');
        std::getline(ss, enabled_str, ',');
        std::getline(ss, group, ',');
        std::getline(ss, rollout_str, ',');
        std::getline(ss, type_str, ',');
        std::getline(ss, val_str, ',');

        bool enabled = (enabled_str == "1");
        uint8_t rollout = 100;
        try { rollout = static_cast<uint8_t>(std::stoi(rollout_str)); } catch (...) {}

        FlagValue value = false;
        if (type_str == "b") {
            value = (val_str == "1");
        } else if (type_str == "n") {
            try { value = std::stod(val_str); } catch (...) { value = 0.0; }
        } else if (type_str == "s") {
            value = val_str;
        }

        COWFlagStore::SetEnabled(key, enabled, value, group, rollout);
        count++;
    }
    std::cout << "[Store] Loaded " << count << " flags from config file: " << filepath_ << std::endl;
}

void PersistentCOWFlagStore::SaveToFile() {
    std::ofstream file(filepath_);
    if (!file.is_open()) {
        std::cerr << "[Store] Error: Failed to open config file for writing: " << filepath_ << std::endl;
        return;
    }

    std::vector<FeatureFlag> flags = GetAllFlags();
    for (const auto& flag : flags) {
        file << flag.key << ","
             << (flag.enabled ? "1" : "0") << ","
             << flag.target_group << ","
             << static_cast<int>(flag.rollout_percentage) << ",";
             
        if (std::holds_alternative<bool>(flag.value)) {
            file << "b," << (std::get<bool>(flag.value) ? "1" : "0");
        } else if (std::holds_alternative<double>(flag.value)) {
            file << "n," << std::get<double>(flag.value);
        } else if (std::holds_alternative<std::string>(flag.value)) {
            file << "s," << std::get<std::string>(flag.value);
        }
        file << "\n";
    }
    file.close();
}

void PersistentCOWFlagStore::WriterWorkerLoop() {
    while (true) {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait(lock, [this] { return stop_writer_ || pending_save_; });

        if (stop_writer_ && !pending_save_) {
            break;
        }

        pending_save_ = false;

        // Unlock during active file write to keep SetEnabled thread-free
        lock.unlock();
        SaveToFile();
    }
}

