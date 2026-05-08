#pragma once
#include "Formatter.h"
#include <atomic>
#include <string>
#include <vector>
#include <functional>

namespace apb {
std::string fetchApbdbVersion(int timeoutSec=15);
std::string buildWeaponHeader(const std::string& dbVersion);
std::vector<std::string> extractKeysFromInt(const std::string& text);
bool generateWeaponDescriptionsFile(
    const std::string& inputIntPath,const std::string& outputPath,
    Scheme scheme=Scheme::CLEAR,RGB single={},RGB gradStart={},RGB gradEnd={},
    int timeoutSec=15,int maxWorkers=8,
    std::function<void(int,int,const std::string&)> onProgress=nullptr,
    const std::atomic<bool>* cancelFlag=nullptr);
} // namespace apb
