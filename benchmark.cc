#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <numeric>
#include <iomanip>
#include <memory>
#include <functional>
#include <algorithm>
#include "flag_store.h"

// Scenarios to benchmark
struct BenchResult {
    std::string config_name;
    double throughput_mops; // Millions of operations per second
    double avg_latency_ns;  // Average latency in nanoseconds
};

// ============================================================================
// 1. BASELINE NAIVE STORE: Locked Map + Naive Heap-Allocating Hashing
// ============================================================================
class NaiveLockedMapStore : public FlagStore {
public:
    bool IsEnabled(std::string_view key, const UserContext& context = {}) override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        auto it = flags_.find(std::string(key));
        if (it == flags_.end()) return false;
        
        const auto& flag = it->second;
        if (!flag.enabled) return false;
        if (!flag.target_group.empty() && context.group != flag.target_group) return false;
        if (flag.rollout_percentage < 100) {
            if (context.user_id.empty()) return false;
            // Naive string concatenation heap allocations!
            std::string combined_key = context.user_id + "_" + flag.key;
            size_t hash_val = std::hash<std::string>{}(combined_key);
            if ((hash_val % 100) >= flag.rollout_percentage) return false;
        }
        return true;
    }

    FeatureFlag GetFlag(std::string_view key) override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        auto it = flags_.find(std::string(key));
        if (it == flags_.end()) return FeatureFlag{std::string(key), false, "", 100, false};
        return it->second;
    }

    std::vector<FeatureFlag> GetAllFlags() override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        std::vector<FeatureFlag> res;
        for (const auto& pair : flags_) res.push_back(pair.second);
        return res;
    }

    void SetEnabled(const std::string& key, bool enabled, FlagValue value = false,
                    std::string target_group = "", uint8_t rollout_percentage = 100) override {
        std::unique_lock<std::shared_mutex> write_lock(mutex_);
        flags_[key] = FeatureFlag{key, enabled, target_group, rollout_percentage, value};
    }

private:
    std::unordered_map<std::string, FeatureFlag> flags_;
    std::shared_mutex mutex_;
};

// ============================================================================
// 2. INNOVATION 1 ONLY: Contiguous Memory Vector + Mutex + Naive Hashing
// ============================================================================
class ContiguousNaiveStore : public FlagStore {
public:
    bool IsEnabled(std::string_view key, const UserContext& context = {}) override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        auto it = std::lower_bound(flags_.begin(), flags_.end(), key, 
                                   [](const FeatureFlag& flag, std::string_view k) {
                                       return flag.key < k;
                                   });
        if (it == flags_.end() || it->key != key) return false;
        
        const auto& flag = *it;
        if (!flag.enabled) return false;
        if (!flag.target_group.empty() && context.group != flag.target_group) return false;
        if (flag.rollout_percentage < 100) {
            if (context.user_id.empty()) return false;
            // Naive string concatenation heap allocations!
            std::string combined_key = context.user_id + "_" + flag.key;
            size_t hash_val = std::hash<std::string>{}(combined_key);
            if ((hash_val % 100) >= flag.rollout_percentage) return false;
        }
        return true;
    }

    FeatureFlag GetFlag(std::string_view key) override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        auto it = std::lower_bound(flags_.begin(), flags_.end(), key, 
                                   [](const FeatureFlag& flag, std::string_view k) {
                                       return flag.key < k;
                                   });
        if (it == flags_.end() || it->key != key) return FeatureFlag{std::string(key), false, "", 100, false};
        return *it;
    }

    std::vector<FeatureFlag> GetAllFlags() override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        return flags_;
    }

    void SetEnabled(const std::string& key, bool enabled, FlagValue value = false,
                    std::string target_group = "", uint8_t rollout_percentage = 100) override {
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

private:
    std::vector<FeatureFlag> flags_;
    std::shared_mutex mutex_;
};

// ============================================================================
// 3. INNOVATION 2 ONLY: Lock-Free COW Map + Naive Hashing
// ============================================================================
class COWMapNaiveStore : public FlagStore {
public:
    using MapPtr = std::shared_ptr<std::unordered_map<std::string, FeatureFlag>>;
    
    COWMapNaiveStore() {
        auto empty_map = std::make_shared<std::unordered_map<std::string, FeatureFlag>>();
        std::atomic_store(&flags_ptr_, empty_map);
    }

    bool IsEnabled(std::string_view key, const UserContext& context = {}) override {
        MapPtr current_map = std::atomic_load(&flags_ptr_);
        auto it = current_map->find(std::string(key));
        if (it == current_map->end()) return false;
        
        const auto& flag = it->second;
        if (!flag.enabled) return false;
        if (!flag.target_group.empty() && context.group != flag.target_group) return false;
        if (flag.rollout_percentage < 100) {
            if (context.user_id.empty()) return false;
            // Naive string concatenation heap allocations!
            std::string combined_key = context.user_id + "_" + flag.key;
            size_t hash_val = std::hash<std::string>{}(combined_key);
            if ((hash_val % 100) >= flag.rollout_percentage) return false;
        }
        return true;
    }

    FeatureFlag GetFlag(std::string_view key) override {
        MapPtr current_map = std::atomic_load(&flags_ptr_);
        auto it = current_map->find(std::string(key));
        if (it == current_map->end()) return FeatureFlag{std::string(key), false, "", 100, false};
        return it->second;
    }

    std::vector<FeatureFlag> GetAllFlags() override {
        MapPtr current_map = std::atomic_load(&flags_ptr_);
        std::vector<FeatureFlag> res;
        for (const auto& pair : *current_map) res.push_back(pair.second);
        return res;
    }

    void SetEnabled(const std::string& key, bool enabled, FlagValue value = false,
                    std::string target_group = "", uint8_t rollout_percentage = 100) override {
        std::lock_guard<std::mutex> lock(write_mutex_);
        MapPtr current_map = std::atomic_load(&flags_ptr_);
        auto new_map = std::make_shared<std::unordered_map<std::string, FeatureFlag>>(*current_map);
        (*new_map)[key] = FeatureFlag{key, enabled, target_group, rollout_percentage, value};
        std::atomic_store(&flags_ptr_, new_map);
    }

private:
    MapPtr flags_ptr_;
    std::mutex write_mutex_;
};

// ============================================================================
// 4. INNOVATION 3 ONLY: Locked Map + Stack-based Zero-Allocation Hashing
// ============================================================================
class LockedMapStackHashStore : public FlagStore {
public:
    bool IsEnabled(std::string_view key, const UserContext& context = {}) override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        auto it = flags_.find(std::string(key));
        if (it == flags_.end()) return false;
        
        const auto& flag = it->second;
        if (!flag.enabled) return false;
        if (!flag.target_group.empty() && context.group != flag.target_group) return false;
        if (flag.rollout_percentage < 100) {
            if (context.user_id.empty()) return false;
            // Zero-allocation stack hash combiner
            size_t h1 = std::hash<std::string>{}(context.user_id);
            size_t h2 = std::hash<std::string>{}(flag.key);
            size_t hash_val = h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            if ((hash_val % 100) >= flag.rollout_percentage) return false;
        }
        return true;
    }

    FeatureFlag GetFlag(std::string_view key) override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        auto it = flags_.find(std::string(key));
        if (it == flags_.end()) return FeatureFlag{std::string(key), false, "", 100, false};
        return it->second;
    }

    std::vector<FeatureFlag> GetAllFlags() override {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        std::vector<FeatureFlag> res;
        for (const auto& pair : flags_) res.push_back(pair.second);
        return res;
    }

    void SetEnabled(const std::string& key, bool enabled, FlagValue value = false,
                    std::string target_group = "", uint8_t rollout_percentage = 100) override {
        std::unique_lock<std::shared_mutex> write_lock(mutex_);
        flags_[key] = FeatureFlag{key, enabled, target_group, rollout_percentage, value};
    }

private:
    std::unordered_map<std::string, FeatureFlag> flags_;
    std::shared_mutex mutex_;
};

// Helper to fill stores with N flags
void PopulateFlags(FlagStore* store, int count) {
    for (int i = 0; i < count; ++i) {
        std::string key = "flag_" + std::to_string(i);
        // Set rollout_percentage < 100 (e.g. 50%) to force hash rule evaluation path
        store->SetEnabled(key, true, false, "group", 50);
    }
}

// Worker functions
void ReaderWorker(FlagStore* store, const std::string& search_key, std::atomic<bool>& stop_flag, uint64_t& local_read_count) {
    uint64_t reads = 0;
    UserContext context{"user_99999", "group"};
    
    while (!stop_flag.load(std::memory_order_relaxed)) {
        store->IsEnabled(search_key, context);
        reads++;
    }
    local_read_count = reads;
}

void WriterWorker(FlagStore* store, const std::string& search_key, int write_interval_ms, std::atomic<bool>& stop_flag) {
    bool state = false;
    while (!stop_flag.load(std::memory_order_relaxed)) {
        state = !state;
        store->SetEnabled(search_key, state, false, "group", 50);
        if (write_interval_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(write_interval_ms));
        }
    }
}

// Perform single scenario benchmark
BenchResult RunBenchmark(const std::string& config_name, std::unique_ptr<FlagStore> store,
                         int flag_count, int num_readers, int write_interval_ms, int duration_seconds) {
    
    PopulateFlags(store.get(), flag_count);
    std::string search_key = "flag_" + std::to_string(flag_count / 2); // Search middle element
    
    std::atomic<bool> stop_flag(false);
    std::vector<std::thread> readers;
    std::vector<uint64_t> read_counts(num_readers, 0);

    std::thread writer;
    if (write_interval_ms >= 0) {
        writer = std::thread(WriterWorker, store.get(), std::ref(search_key), write_interval_ms, std::ref(stop_flag));
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_readers; ++i) {
        readers.emplace_back(ReaderWorker, store.get(), std::ref(search_key), std::ref(stop_flag), std::ref(read_counts[i]));
    }

    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    stop_flag.store(true);

    for (auto& t : readers) t.join();
    if (writer.joinable()) writer.join();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000000.0;
    uint64_t total_reads = std::accumulate(read_counts.begin(), read_counts.end(), 0ULL);

    double throughput_mops = (total_reads / actual_duration) / 1000000.0;
    double avg_latency_ns = (actual_duration * 1e9 / total_reads) * num_readers;

    return BenchResult{config_name, throughput_mops, avg_latency_ns};
}

void PrintScenarioHeader(const std::string& title, int flags, int readers, const std::string& writes) {
    std::cout << "\n========================================================================" << std::endl;
    std::cout << "SCENARIO: " << title << std::endl;
    std::cout << "          [Database Size: " << flags << " flags] [Reader Threads: " << readers 
              << "] [Writers: " << writes << "]" << std::endl;
    std::cout << "========================================================================" << std::endl;
    std::cout << "| Implementation Configuration        | Throughput (M ops/s) | Latency (ns) |" << std::endl;
    std::cout << "|-------------------------------------|----------------------|--------------|" << std::endl;
}

void PrintResult(const BenchResult& r) {
    std::cout << "| " << std::setw(35) << std::left << r.config_name 
              << " | " << std::setw(20) << std::right << std::fixed << std::setprecision(2) << r.throughput_mops
              << " | " << std::setw(12) << std::fixed << std::setprecision(1) << r.avg_latency_ns 
              << " |" << std::endl;
}

int main() {
    std::cout << "=== Starting Multi-Dimensional Performance Benchmark ===" << std::endl;
    std::cout << "Comparing each systems innovation independently and in summation." << std::endl;
    int duration = 2; // 2 seconds per test

    // ------------------------------------------------------------------------
    // SCENARIO 1: LOW SCALE (Small DB, Single Thread, No writer contention)
    // ------------------------------------------------------------------------
    {
        int flags = 5;
        int readers = 1;
        int write_interval = -1; // No writer
        PrintScenarioHeader("LOW SCALE (Pure Reader, Single Thread, Small Dataset)", flags, readers, "NONE");

        PrintResult(RunBenchmark("Baseline (Naive Locked Map)", std::make_unique<NaiveLockedMapStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 1 (Contiguous Memory Vector)", std::make_unique<ContiguousNaiveStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 2 (Lock-Free COW Map)", std::make_unique<COWMapNaiveStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 3 (Zero-Alloc Hashing)", std::make_unique<LockedMapStackHashStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("All Combined (COW Vector + Hash)", std::make_unique<COWFlagStore>(), flags, readers, write_interval, duration));
    }

    // ------------------------------------------------------------------------
    // SCENARIO 2: DATASET SCALE (Large DB, Single Thread, No writes)
    // ------------------------------------------------------------------------
    {
        int flags = 2000;
        int readers = 1;
        int write_interval = -1;
        PrintScenarioHeader("DATASET SCALE (Binary Search Cache-Locality Check)", flags, readers, "NONE");

        PrintResult(RunBenchmark("Baseline (Naive Locked Map)", std::make_unique<NaiveLockedMapStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 1 (Contiguous Memory Vector)", std::make_unique<ContiguousNaiveStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 2 (Lock-Free COW Map)", std::make_unique<COWMapNaiveStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 3 (Zero-Alloc Hashing)", std::make_unique<LockedMapStackHashStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("All Combined (COW Vector + Hash)", std::make_unique<COWFlagStore>(), flags, readers, write_interval, duration));
    }

    // ------------------------------------------------------------------------
    // SCENARIO 3: CONCURRENCY SCALE (Small DB, 32 Threads, High Writer Contention)
    // ------------------------------------------------------------------------
    {
        int flags = 5;
        int readers = 32;
        int write_interval = 10; // Write once every 10ms
        PrintScenarioHeader("CONCURRENCY SCALE (Heavy Multi-thread Write Contention)", flags, readers, "100 updates/s");

        PrintResult(RunBenchmark("Baseline (Naive Locked Map)", std::make_unique<NaiveLockedMapStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 1 (Contiguous Memory Vector)", std::make_unique<ContiguousNaiveStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 2 (Lock-Free COW Map)", std::make_unique<COWMapNaiveStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 3 (Zero-Alloc Hashing)", std::make_unique<LockedMapStackHashStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("All Combined (COW Vector + Hash)", std::make_unique<COWFlagStore>(), flags, readers, write_interval, duration));
    }

    // ------------------------------------------------------------------------
    // SCENARIO 4: ULTIMATE PRODUCTION SCALE (Large DB, 32 Threads, High Writes)
    // ------------------------------------------------------------------------
    {
        int flags = 2000;
        int readers = 32;
        int write_interval = 10;
        PrintScenarioHeader("PRODUCTION SCALE (Large Dataset, High Contention Threads)", flags, readers, "100 updates/s");

        PrintResult(RunBenchmark("Baseline (Naive Locked Map)", std::make_unique<NaiveLockedMapStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 1 (Contiguous Memory Vector)", std::make_unique<ContiguousNaiveStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 2 (Lock-Free COW Map)", std::make_unique<COWMapNaiveStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("Innov 3 (Zero-Alloc Hashing)", std::make_unique<LockedMapStackHashStore>(), flags, readers, write_interval, duration));
        PrintResult(RunBenchmark("All Combined (COW Vector + Hash)", std::make_unique<COWFlagStore>(), flags, readers, write_interval, duration));
    }



    std::cout << "\n========================================================================" << std::endl;
    std::cout << "Benchmark complete." << std::endl;
    return 0;
}
