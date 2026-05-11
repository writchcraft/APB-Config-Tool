#include "backend/PlayerRoles.h"

#include "backend/AppDirs.h"
#include "backend/GradientMaker.h"
#include "backend/HttpClient.h"
#include "backend/WeaponItemTypes.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <ctime>

namespace fs = std::filesystem;
namespace apb {

using nlohmann::json;

namespace {

struct InputRoleEntry {
    std::string displayKey;
    std::string displayValue;
    std::string descKey;
    std::string descValue;
    bool hasDisplay = false;
    bool hasDesc = false;
};

struct ParsedInputRoles {
    std::vector<std::string> orderedRoleKeys;
    std::map<std::string, InputRoleEntry> entries;
};

struct DescriptionBuild {
    std::string text;
    bool hasRewards = false;
};

enum class RoleCategory {
    WEAPONS = 0,
    EQUIPMENT,
    EVENT,
    MISC
};

struct OutputRoleEntry {
    RoleCategory category = RoleCategory::MISC;
    std::string roleKey;
    std::vector<std::string> lines;
};

static std::string readTextAny(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    if (raw.size() >= 2) {
        const auto b0 = uint8_t(raw[0]);
        const auto b1 = uint8_t(raw[1]);
        if (b0 == 0xFF && b1 == 0xFE) {
            std::string out;
            for (size_t i = 2; i + 1 < raw.size(); i += 2) {
                uint16_t cp = uint8_t(raw[i]) | (uint16_t(uint8_t(raw[i + 1])) << 8);
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) {
                    out += char(0xC0 | (cp >> 6));
                    out += char(0x80 | (cp & 0x3F));
                } else {
                    out += char(0xE0 | (cp >> 12));
                    out += char(0x80 | ((cp >> 6) & 0x3F));
                    out += char(0x80 | (cp & 0x3F));
                }
            }
            return out;
        }
        if (b0 == 0xEF && b1 == 0xBB && raw.size() >= 3 && uint8_t(raw[2]) == 0xBF)
            return raw.substr(3);
    }
    return raw;
}

static void writeUtf16LE(const std::string& path, const std::string& utf8) {
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot write: " + path);

    const uint8_t bom[2] = {0xFF, 0xFE};
    f.write(reinterpret_cast<const char*>(bom), 2);

    for (size_t i = 0; i < utf8.size();) {
        uint8_t c = uint8_t(utf8[i]);
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            ++i;
        } else if (c < 0xE0 && i + 1 < utf8.size()) {
            cp = ((c & 0x1F) << 6) | (uint8_t(utf8[i + 1]) & 0x3F);
            i += 2;
        } else if (i + 2 < utf8.size()) {
            cp = ((c & 0x0F) << 12) |
                 ((uint8_t(utf8[i + 1]) & 0x3F) << 6) |
                 (uint8_t(utf8[i + 2]) & 0x3F);
            i += 3;
        } else {
            cp = c;
            ++i;
        }

        const uint8_t lo = uint8_t(cp & 0xFF);
        const uint8_t hi = uint8_t((cp >> 8) & 0xFF);
        f.put(char(lo));
        f.put(char(hi));
    }
}

static std::string baseNoExt(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = base.rfind('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

static std::string defaultOutputPath(const std::string& inputIntPath) {
    fs::path outDir(DownloadsDir());
    fs::create_directories(outDir);
    return (outDir / (baseNoExt(inputIntPath) + "_Detailed.INT")).string();
}

static std::string todayStr() {
    time_t t = time(nullptr);
    struct tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

static std::string trim(std::string s) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static std::string stripUnlockPrefix(std::string s) {
    s = trim(std::move(s));
    const std::string prefix = "Unlock:";
    while (s.rfind(prefix, 0) == 0)
        s = trim(s.substr(prefix.size()));
    return s;
}

static bool isTitleItem(const json& item) {
    if (!item.is_object()) return false;
    const std::string sapbdb = item.value("sAPBDB", "");
    if (sapbdb.rfind("Unlock_Title_", 0) == 0) return true;
    const json& infraCategory = item["eInfraCategory"];
    return infraCategory.is_number() && int(infraCategory.get<double>()) == 137;
}

static std::string shortEquipmentAlias(const std::string& roleKey, const std::string& rewardName) {
    static const std::map<std::string, std::string> globalAliases = {
        {"GA5 5 Liter Can", "Gas Can 2"},
        {"GA5 7 Liter Can", "Gas Can 3"},
        {"GA5 9 Liter Can", "Gas Can 4"},
        {"Blistor QE 240MM", "Crowbar 2"},
        {"Blistor QE 355MM", "Crowbar 3"},
        {"Blistor QE 425MM", "Crowbar 4"},
        {"Street Jacker F109", "Brass Knuckles 2"},
        {"Street Jacker G101", "Brass Knuckles 3"},
        {"Street Jacker H169", "Brass Knuckles 4"},
        {"Universal Key S2", "Universal Key 2"},
        {"Universal Key S3", "Universal Key 3"},
        {"Universal Key S4", "Universal Key 4"},
        {"Pro Range Spray Paint", "Spray Can 2"},
        {"Rattle 'n Roll Spray Paint", "Spray Can 3"},
        {"Artisan Spray Paint", "Spray Can 4"},
        {"Ferrite Spraymate 450", "Spray Can 2"},
        {"Ferrite Spraymate 600", "Spray Can 3"},
        {"Ferrite Spraymate 1000", "Spray Can 4"},
        {"F-Series SJ Mk II", "Slim Jim 2"},
        {"F-Series SJ Mk III", "Slim Jim 3"},
        {"F-Series SJ Mk IIII", "Slim Jim 4"},
        {"Wirepal 200MM", "Wirecutters 2"},
        {"Wirepal 240MM", "Wirecutters 3"},
        {"Wirepal 300MM", "Wirecutters 4"}
    };

    auto globalIt = globalAliases.find(rewardName);
    if (globalIt != globalAliases.end()) return globalIt->second;

    if (rewardName == "Somatic XAG GD 2")
        return roleKey == "Role2_CrimBombings" ? "Bomb 2" : "Netbook 2";
    if (rewardName == "Somatic XAG GD 3")
        return roleKey == "Role2_CrimBombings" ? "Bomb 3" : "Netbook 3";
    if (rewardName == "Somatic XAG GD 4")
        return roleKey == "Role2_CrimBombings" ? "Bomb 4" : "Netbook 4";

    if (rewardName == "The Ram Man SO1E")
        return roleKey == "Role2_Enf_ForcedEntry" ? "Battering Ram 2" : "Camera 2";
    if (rewardName == "The Ram Man SO2C")
        return roleKey == "Role2_Enf_ForcedEntry" ? "Battering Ram 3" : "Camera 3";
    if (rewardName == "The Ram Man SO3A")
        return roleKey == "Role2_Enf_ForcedEntry" ? "Battering Ram 4" : "Camera 4";

    return {};
}

static std::string normaliseRewardName(
    const json& item,
    const std::string& roleKey,
    bool useShortEquipmentNames)
{
    std::string name = stripUnlockPrefix(item.value("sDisplayName", ""));
    if (name.empty()) return {};
    if (useShortEquipmentNames) {
        const std::string alias = shortEquipmentAlias(roleKey, name);
        if (!alias.empty()) name = alias;
    }
    if (isTitleItem(item) && name.find("(Title)") == std::string::npos)
        name += " (Title)";
    return name;
}

static std::vector<std::string> collectRewardNames(
    const json& milestone,
    const std::string& roleKey,
    bool useShortEquipmentNames)
{
    std::set<std::string> orderedSet;
    std::vector<std::string> ordered;

    auto addName = [&](const std::string& raw) {
        const std::string name = trim(raw);
        if (name.empty()) return;
        if (orderedSet.insert(name).second) ordered.push_back(name);
    };

    const json& reward = milestone["eReward"];
    if (!reward.is_object()) return ordered;

    const json& directItems = reward["eItems"];
    if (directItems.is_array()) {
        for (const auto& item : directItems)
            addName(normaliseRewardName(item, roleKey, useShortEquipmentNames));
    }

    const json& childItems = reward["eChildPackage"]["eItems"];
    if (childItems.is_array() && !childItems.empty()) {
        for (const auto& item : childItems) {
            std::string name = stripUnlockPrefix(item.value("sDisplayName", ""));
            if (name.empty()) continue;
            if (useShortEquipmentNames) {
                const std::string alias = shortEquipmentAlias(roleKey, name);
                if (!alias.empty()) name = alias;
            }
            addName(name);
        }
    }

    return ordered;
}

static std::string formatInteger(double value) {
    long long v = (long long)std::llround(value);
    const bool neg = v < 0;
    if (neg) v = -v;
    std::string digits = std::to_string(v);
    for (int i = (int)digits.size() - 3; i > 0; i -= 3)
        digits.insert((size_t)i, ",");
    return neg ? "-" + digits : digits;
}

static std::string metricLabelForRole(const std::string& roleKey) {
    if (roleKey == "Role1_Cop") return "arrests";
    if (roleKey.rfind("Role1_", 0) == 0) return "kills";
    return "actions";
}

static DescriptionBuild buildDescription(
    const std::string& roleKey,
    const json& role,
    bool useShortEquipmentNames)
{
    const json& milestones = role["aMilestones"];
    if (!milestones.is_array() || milestones.empty())
        return {role.value("sDescription", ""), false};

    const std::string metric = metricLabelForRole(roleKey);
    const std::string newline = "\xE2\x86\xB5";

    std::ostringstream out;
    long long previousTotal = 0;
    int rank = 0;
    bool hasRewards = false;

    for (const auto& milestone : milestones) {
        ++rank;
        const long long total = (long long)std::llround(milestone.value("fPassMark_0", 0.0));
        const long long delta = std::max<long long>(0, total - previousTotal);
        previousTotal = total;

        out << "Rank " << rank << ": " << formatInteger((double)delta) << ' ' << metric
            << " // " << formatInteger((double)total) << " total " << metric;

        const std::vector<std::string> rewardNames =
            collectRewardNames(milestone, roleKey, useShortEquipmentNames);
        for (const auto& rewardName : rewardNames) {
            hasRewards = true;
            out << newline << "   Unlock: " << rewardName;
        }

        if (rank < (int)milestones.size()) out << newline;
    }

    if (hasRewards)
        out << newline << newline << newline << newline << newline;
    return {out.str(), hasRewards};
}

static std::vector<RGB> steppedPalette(const RGB& start, const RGB& end) {
    std::vector<RGB> palette;
    palette.reserve(6);
    for (int i = 0; i < 6; ++i) {
        const double t = i / 5.0;
        palette.push_back(lerpRGB(start, end, t));
    }
    return palette;
}

static std::string buildDisplayName(
    const std::string& displayName,
    PlayerRoleNameStyle nameStyle,
    const RGB& solid,
    const RGB& gradStart,
    const RGB& gradMiddle,
    const RGB& gradEnd)
{
    switch (nameStyle) {
        case PlayerRoleNameStyle::NONE:
            return displayName;
        case PlayerRoleNameStyle::STEPPED:
            return hardGradientString(displayName, steppedPalette(gradStart, gradEnd), true);
        case PlayerRoleNameStyle::SMOOTH:
            return smoothGradientString(displayName, gradStart, gradEnd, true);
        case PlayerRoleNameStyle::TRIPLE:
            return tripleGradientString(displayName, gradStart, gradMiddle, gradEnd, true);
        case PlayerRoleNameStyle::SOLID:
        default:
            return solidText(displayName, solid);
    }
}

static ParsedInputRoles parseInputRoles(const std::string& inputText) {
    ParsedInputRoles parsed;
    std::set<std::string> seen;
    std::istringstream ss(inputText);
    std::string line;
    const std::string prefix = "PlayerRoles_";
    const std::string displaySuffix = "_DisplayName";
    const std::string descSuffix = "_Description";

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        if (key.rfind(prefix, 0) != 0) continue;

        const std::string value = line.substr(eq + 1);
        std::string roleKey;
        bool isDisplay = false;
        bool isDesc = false;

        if (key.size() > prefix.size() + displaySuffix.size() &&
            key.compare(key.size() - displaySuffix.size(), displaySuffix.size(), displaySuffix) == 0) {
            roleKey = key.substr(prefix.size(), key.size() - prefix.size() - displaySuffix.size());
            isDisplay = true;
        } else if (key.size() > prefix.size() + descSuffix.size() &&
            key.compare(key.size() - descSuffix.size(), descSuffix.size(), descSuffix) == 0) {
            roleKey = key.substr(prefix.size(), key.size() - prefix.size() - descSuffix.size());
            isDesc = true;
        }

        if (roleKey.empty()) continue;
        if (seen.insert(roleKey).second)
            parsed.orderedRoleKeys.push_back(roleKey);

        InputRoleEntry& entry = parsed.entries[roleKey];
        if (isDisplay) {
            entry.displayKey = key;
            entry.displayValue = value;
            entry.hasDisplay = true;
        } else if (isDesc) {
            entry.descKey = key;
            entry.descValue = value;
            entry.hasDesc = true;
        }
    }

    return parsed;
}

static std::string urlEncodePathSegment(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out += char(ch);
        } else {
            out += '%';
            out += hex[(ch >> 4) & 0x0F];
            out += hex[ch & 0x0F];
        }
    }
    return out;
}

static json fetchRoleDetail(const std::string& roleKey, int timeoutMs) {
    const std::string url =
        "https://api.apbdb.com/beacon/roles/" + urlEncodePathSegment(roleKey);
    const HttpResponse resp = httpGet(url, timeoutMs);
    if (!resp.ok()) return {};

    bool ok = false;
    json root = json::parse(resp.body, ok);
    return ok && root.is_object() ? root : json{};
}

static std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });
    return s;
}

static RoleCategory categoriseRole(const std::string& roleKey) {
    if (roleKey.rfind("Role1_", 0) == 0)
        return RoleCategory::WEAPONS;
    if (roleKey.rfind("Role2_", 0) == 0 || roleKey.rfind("Role21_", 0) == 0)
        return RoleCategory::EQUIPMENT;

    const std::string lower = toLowerAscii(roleKey);
    static const char* eventKeywords[] = {
        "holiday", "halloween", "christmas", "easter",
        "anniversary", "nutcracker", "infection", "valentine"
    };
    for (const char* keyword : eventKeywords) {
        if (lower.find(keyword) != std::string::npos)
            return RoleCategory::EVENT;
    }
    return RoleCategory::MISC;
}

static const char* categoryLabel(RoleCategory category) {
    switch (category) {
        case RoleCategory::WEAPONS: return "Weapons Roles";
        case RoleCategory::EQUIPMENT: return "Equipment Roles";
        case RoleCategory::EVENT: return "Event Roles";
        case RoleCategory::MISC:
        default: return "Misc Roles";
    }
}

static std::string categoryDivider(const std::string& text) {
    const std::string ornamentStart =
        "\xE2\x9B\xA7\xCB\x9A\xE2\x82\x8A\xE2\x80\xA7[ ";
    const std::string ornamentEnd =
        " ]\xE2\x80\xA7\xE2\x82\x8A\xCB\x9A\xE2\x9B\xA7";
    return ";\n;                       " + ornamentStart + text + ornamentEnd + "\n;";
}

static std::string buildOutputText(
    const std::vector<OutputRoleEntry>& changedRoles,
    const std::string& dbVersion)
{
    std::ostringstream out;
    out << "               _ _       _     \n";
    out << "              (_) |     | |    \n";
    out << "__      ___ __ _| |_ ___| |__  \n";
    out << "\\ \\ /\\ / / '__| | __/ __| '_ \\ \n";
    out << " \\ V  V /| |  | | || (__| | | |\n";
    out << "  \\_/\\_/ |_|  |_|\\__\\___|_| |_|\n";
    out << ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n";
    out << "; This is an automatically generated file\n";
    out << "; DO NOT modify this file, modify the SDD table: PlayerRole\n";
    out << "; Generated on: " << todayStr() << "\n";
    out << "; Database Version: " << dbVersion << "\n";
    out << ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n\n";
    out << "[PlayerRoles]\n";

    const RoleCategory order[] = {
        RoleCategory::WEAPONS,
        RoleCategory::EQUIPMENT,
        RoleCategory::EVENT,
        RoleCategory::MISC
    };

    bool wroteCategory = false;
    for (RoleCategory category : order) {
        bool categoryHasLines = false;
        for (const auto& role : changedRoles) {
            if (role.category == category && !role.lines.empty()) {
                categoryHasLines = true;
                break;
            }
        }
        if (!categoryHasLines) continue;

        out << categoryDivider(categoryLabel(category)) << "\n";
        bool wroteRole = false;
        for (const auto& role : changedRoles) {
            if (role.category != category || role.lines.empty()) continue;
            if (wroteRole) out << "\n";
            for (const std::string& line : role.lines)
                out << line << "\n";
            wroteRole = true;
        }
        wroteCategory = true;
    }

    return out.str();
}

} // namespace

PlayerRolesResult generatePlayerRolesFile(
    const std::string& inputIntPath,
    const std::string& outputPath,
    PlayerRoleNameStyle nameStyle,
    bool useShortEquipmentNames,
    RGB solid,
    RGB gradStart,
    RGB gradMiddle,
    RGB gradEnd,
    std::function<void(int, int, const std::string&)> onProgress,
    const std::atomic<bool>* cancelFlag)
{
    const std::string inputText = readTextAny(inputIntPath);
    const ParsedInputRoles parsed = parseInputRoles(inputText);
    const int totalRoles = (int)parsed.orderedRoleKeys.size();

    if (parsed.orderedRoleKeys.empty())
        throw std::runtime_error("Could not find any PlayerRoles entries in: " + inputIntPath);

    PlayerRolesResult result;
    std::vector<OutputRoleEntry> changedRoles;
    int fetchedRoles = 0;

    for (int i = 0; i < totalRoles; ++i) {
        if (cancelFlag && cancelFlag->load()) {
            result.cancelled = true;
            return result;
        }

        const std::string& roleKey = parsed.orderedRoleKeys[i];
        const InputRoleEntry& inputEntry = parsed.entries.at(roleKey);
        json role = fetchRoleDetail(roleKey, 15000);
        if (!role.is_object()) {
            result.failedKeys.push_back(roleKey);
            if (onProgress) onProgress(i + 1, totalRoles, "[skip] " + roleKey);
            continue;
        }

        ++fetchedRoles;

        const std::string sourceDisplayName =
            inputEntry.hasDisplay ? inputEntry.displayValue : roleKey;
        const std::string displayName = role.value("sDisplayName", sourceDisplayName);
        const std::string generatedDisplay =
            buildDisplayName(displayName, nameStyle, solid, gradStart, gradMiddle, gradEnd);
        const bool isAchievement = roleKey.rfind("Ach", 0) == 0;
        const DescriptionBuild desc =
            isAchievement ? DescriptionBuild{} : buildDescription(roleKey, role, useShortEquipmentNames);
        const std::string generatedDesc =
            desc.text.empty() ? role.value("sDescription", inputEntry.descValue) : desc.text;

        OutputRoleEntry outputEntry;
        outputEntry.category = categoriseRole(roleKey);
        outputEntry.roleKey = roleKey;

        if (!inputEntry.hasDisplay || generatedDisplay != inputEntry.displayValue) {
            const std::string key = inputEntry.hasDisplay
                ? inputEntry.displayKey
                : "PlayerRoles_" + roleKey + "_DisplayName";
            outputEntry.lines.push_back(key + "=" + generatedDisplay);
        }
        if (!inputEntry.hasDesc || generatedDesc != inputEntry.descValue) {
            const std::string key = inputEntry.hasDesc
                ? inputEntry.descKey
                : "PlayerRoles_" + roleKey + "_Description";
            outputEntry.lines.push_back(key + "=" + generatedDesc);
        }

        if (!outputEntry.lines.empty()) {
            changedRoles.push_back(std::move(outputEntry));
            result.updatedKeys.push_back(roleKey);
        }

        if (onProgress) onProgress(i + 1, totalRoles, roleKey);
    }

    if (fetchedRoles == 0)
        throw std::runtime_error("Could not fetch any Player Roles from APBDB.");

    const std::string dbVersion = fetchApbdbVersion(15);
    result.outputPath = outputPath.empty() ? defaultOutputPath(inputIntPath) : outputPath;
    writeUtf16LE(result.outputPath, buildOutputText(changedRoles, dbVersion));
    return result;
}

} // namespace apb
