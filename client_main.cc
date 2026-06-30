#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include "flag_client.h"

// Helper struct to print user context cleanly
struct TestUser {
    std::string name;
    UserContext context;
};

int main() {
    std::cout << "=== Launching C++ Feature Flag Client (Rules Testing) ===" << std::endl;

    FeatureFlagClient client("127.0.0.1", 8888);

    if (!client.Start()) {
        std::cerr << "[Main Client] Failed to start client SDK. Exiting." << std::endl;
        return 1;
    }

    // Define 4 test users with different IDs and groups
    std::vector<TestUser> test_users = {
        {"User 1 (Beta)",      UserContext{"user_01", "beta"}},
        {"User 2 (Regular)",   UserContext{"user_02", "regular"}},
        {"User 3 (Regular)",   UserContext{"user_03", "regular"}},
        {"User 4 (Regular)",   UserContext{"user_04", "regular"}}
    };

    std::cout << "[Main Client] Waiting for updates in real-time..." << std::endl;

    // Loop for 120 seconds to observe updates
    for (int i = 0; i < 120; ++i) {
        std::cout << "\n--- Check " << i << " ---" << std::endl;
        
        for (const auto& user : test_users) {
            bool alpha_on = client.IsEnabled("feature_alpha", user.context);
            std::string alpha_val = client.GetValue<std::string>("feature_alpha", "default", user.context);

            bool beta_on = client.IsEnabled("feature_beta", user.context);
            double beta_val = client.GetValue<double>("feature_beta", 0.0, user.context);

            std::cout << "  " << user.name 
                      << " | 'feature_alpha': " << (alpha_on ? "ON" : "OFF") << " (val='" << alpha_val << "')"
                      << " | 'feature_beta': " << (beta_on ? "ON" : "OFF") << " (val=" << beta_val << ")"
                      << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n[Main Client] Stopping client SDK..." << std::endl;
    client.Stop();
    
    std::cout << "=== C++ Client Finished ===" << std::endl;
    return 0;
}
