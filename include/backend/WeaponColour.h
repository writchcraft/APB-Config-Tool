#pragma once
#include "Colors.h"
#include <atomic>
#include <string>
#include <map>
#include <vector>
#include <functional>

namespace apb {
std::vector<std::string> availableFonts();
inline std::string defaultFont(){return "None";}
std::map<std::string,RGB> writchPreset();
std::map<std::string,RGB> spellboundPreset();
std::map<std::string,RGB> presetByIndex(int presetIdx);
std::vector<std::string>  presetCategories();
enum class ColourMode {
    SOLID,
    STEPPED,
    SMOOTH,
    TRIPLE,

    // Legacy aliases kept so older UI code continues to compile if included.
    PRESET = SOLID,
    SINGLE = SOLID,
    GRADIENT = SMOOTH
};
struct GunTypeDefinition {
    std::string label;
    std::string shortLabel;
    std::string categoryToken;
    std::string keyPrefix;
};
struct GunTypeColourSettings {
    std::string categoryToken;
    bool enabled=false;
    ColourMode mode=ColourMode::SOLID;
    RGB solid{1,1,1};
    std::vector<RGB> stepped;
    RGB smoothStart{1,1,1}, smoothEnd{1,1,1};
    RGB tripleStart{1,1,1}, tripleMiddle{1,1,1}, tripleEnd{1,1,1};
};
std::vector<GunTypeDefinition> weaponGunTypes();
struct ColourResult {
    std::string text,inputPath,outputPath,encodingUsed;
    int weaponsTotal=0,alreadyColoured=0,newlyColoured=0,skippedRules=0;
    bool cancelled=false;
};
ColourResult applyColourToGerFile(
    const std::string& inputPath,
    const std::vector<GunTypeColourSettings>& settings,
    const std::string& fontTag="None",
    const std::vector<std::string>& ignoredKeys={},
    std::function<void(const std::string&)> log=nullptr,
    const std::atomic<bool>* cancelFlag=nullptr);
} // namespace apb
