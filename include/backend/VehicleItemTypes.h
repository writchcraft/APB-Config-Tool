#pragma once
#include "Formatter.h"
#include <atomic>
#include <string>
#include <functional>

namespace apb {
struct VStats {
    double maxHealth=0,maxSpeedMps=0,maxReverseSpeedMps=0;
    double cargoCapacity=0,vehicleWeight=0,explosionMaxDamage=0,explosionRadiusCm=0;
};
struct VStatsResult { VStats stats; bool ok=false; };
VStatsResult vehicleStatsFromApbdb(const std::string& key,int timeoutSec=15);
std::string buildVehicleHeader(const std::string& dbVersion);
bool generateVehicleDescriptionsFile(
    const std::string& inputIntPath,const std::string& outputPath,
    Scheme scheme=Scheme::CLEAR,RGB single={},RGB gradStart={},RGB gradEnd={},
    int timeoutSec=15,int maxWorkers=8,
    std::function<void(int,int,const std::string&)> onProgress=nullptr,
    const std::atomic<bool>* cancelFlag=nullptr);
} // namespace apb
