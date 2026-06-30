#ifndef EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_FLAG_STORE_H_
#define EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_FLAG_STORE_H_

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>
#include <condition_variable>

#include "protocol.h"

struct UserContext {
    std::string user_id;
    std::string group;
};

// Represents a single Feature Flag / Remote Configuration
struct FeatureFlag {
    std::string key;
    bool enabled;                  // Master switch
    std::string target_group;      // If empty, no group restriction
    uint8_t rollout_percentage;    // 0 to 100
    FlagValue value;               // The value returned if active
};

// The Interface for the Flag Store
class FlagStore {
public:
    virtual ~FlagStore() = default;

    // Zero-allocation lookups using std::string_view
    virtual bool IsEnabled(std::string_view key, const UserContext& context = {}) = 0;
    virtual FeatureFlag GetFlag(std::string_view key) = 0;

    virtual std::vector<FeatureFlag> GetAllFlags() = 0;

    virtual void SetEnabled(const std::string& key, bool enabled, 
                           FlagValue value = false,
                           std::string target_group = "", 
                           uint8_t rollout_percentage = 100) = 0;
};

// Implementation A: Contiguous Vector Store with Reader-Writer Lock
class LockedFlagStore : public FlagStore {
public:
    bool IsEnabled(std::string_view key, const UserContext& context = {}) override;
    FeatureFlag GetFlag(std::string_view key) override;
    std::vector<FeatureFlag> GetAllFlags() override;
    void SetEnabled(const std::string& key, bool enabled, 
                    FlagValue value = false,
                    std::string target_group = "", 
                    uint8_t rollout_percentage = 100) override;

private:
    // Contiguous vector storage for cache locality
    std::vector<FeatureFlag> flags_;
    std::shared_mutex mutex_;
};

// Implementation B: Contiguous Vector Store with Copy-on-Write (COW)
class COWFlagStore : public FlagStore {
public:
    COWFlagStore();
    bool IsEnabled(std::string_view key, const UserContext& context = {}) override;
    FeatureFlag GetFlag(std::string_view key) override;
    std::vector<FeatureFlag> GetAllFlags() override;
    void SetEnabled(const std::string& key, bool enabled, 
                    FlagValue value = false,
                    std::string target_group = "", 
                    uint8_t rollout_percentage = 100) override;

private:
    using FlagVector = std::vector<FeatureFlag>;
    
    // Contiguous array stored behind an atomic shared pointer
    std::shared_ptr<FlagVector> flags_ptr_;
    std::mutex write_mutex_;
};

// Implementation C: File-backed Persistent COW Store
class PersistentCOWFlagStore : public COWFlagStore {
public:
    PersistentCOWFlagStore(std::string filepath);
    ~PersistentCOWFlagStore() override;

    void SetEnabled(const std::string& key, bool enabled, 
                    FlagValue value = false,
                    std::string target_group = "", 
                    uint8_t rollout_percentage = 100) override;

private:
    std::string filepath_;

    // Asynchronous saving structures
    std::thread writer_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> pending_save_;
    std::atomic<bool> stop_writer_;

    void LoadFromFile();
    void SaveToFile();
    void WriterWorkerLoop();
};

#endif  // EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_FLAG_STORE_H_

