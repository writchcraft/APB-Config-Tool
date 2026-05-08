#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace apb {

enum class ContactDescriptionMode {
    UNLOCKS = 0,
    MISSIONS
};

struct ContactDescriptionsResult {
    std::string outputPath;
    std::vector<std::string> updatedKeys;
    std::vector<std::string> failedKeys;
    bool cancelled = false;
};

ContactDescriptionsResult generateContactDescriptionsFile(
    const std::string& inputIntPath,
    const std::string& outputPath = {},
    ContactDescriptionMode mode = ContactDescriptionMode::UNLOCKS,
    std::function<void(int, int, const std::string&)> onProgress = nullptr,
    const std::atomic<bool>* cancelFlag = nullptr);

} // namespace apb
