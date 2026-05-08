#pragma once

#include "backend/Colors.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace apb {

enum class PlayerRoleNameStyle {
    NONE = 0,
    SOLID,
    STEPPED,
    SMOOTH,
    TRIPLE
};

struct PlayerRolesResult {
    std::string outputPath;
    std::vector<std::string> updatedKeys;
    std::vector<std::string> failedKeys;
    bool cancelled = false;
};

PlayerRolesResult generatePlayerRolesFile(
    const std::string& inputIntPath,
    const std::string& outputPath = {},
    PlayerRoleNameStyle nameStyle = PlayerRoleNameStyle::SOLID,
    bool useShortEquipmentNames = false,
    RGB solid = {1.0, 1.0, 1.0},
    RGB gradStart = {0.08, 0.0, 0.78},
    RGB gradMiddle = {0.36, 0.08, 0.82},
    RGB gradEnd = {0.65, 0.022353, 0.4},
    std::function<void(int, int, const std::string&)> onProgress = nullptr,
    const std::atomic<bool>* cancelFlag = nullptr);

} // namespace apb
