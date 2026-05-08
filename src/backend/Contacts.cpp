#include "backend/Contacts.h"

#include "backend/AppDirs.h"
#include "backend/HttpClient.h"
#include "backend/WeaponItemTypes.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace apb {

using nlohmann::json;

namespace {

struct InputContactEntry {
    std::string titleKey;
    std::string titleValue;
    std::string descKey;
    std::string descValue;
    bool hasTitle = false;
    bool hasDesc = false;
};

struct ParsedInputContacts {
    std::vector<std::string> orderedContactKeys;
    std::map<std::string, InputContactEntry> entries;
};

struct OutputContactEntry {
    std::string contactKey;
    int category = 0;
    std::vector<std::string> lines;
};

struct MissionEntry {
    std::string label;
    std::string title;
    int minGroup = 0;
    int maxGroup = 0;
};

enum class ContactCategory {
    CRIMINAL_FINANCIAL = 0,
    CRIMINAL_WATERFRONT,
    ENFORCER_FINANCIAL,
    ENFORCER_WATERFRONT,
    ORGANISATIONS,
    MISC
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

static std::string normaliseColorClosingTags(std::string s) {
    static const std::string legacyTag = "</Color>";
    static const std::string apbTag = "<Color:/>";

    size_t pos = 0;
    while ((pos = s.find(legacyTag, pos)) != std::string::npos) {
        s.replace(pos, legacyTag.size(), apbTag);
        pos += apbTag.size();
    }
    return s;
}

static std::string stripUnlockPrefix(std::string s) {
    s = trim(std::move(s));
    const std::string prefix = "Unlock:";
    while (s.rfind(prefix, 0) == 0)
        s = trim(s.substr(prefix.size()));
    return s;
}

static std::string defaultOutputPath(
    const std::string& inputIntPath,
    ContactDescriptionMode mode)
{
    fs::path outDir(DownloadsDir());
    fs::create_directories(outDir);
    const char* suffix = mode == ContactDescriptionMode::MISSIONS ? "MISSIONS" : "UNLOCKS";
    return (outDir / (baseNoExt(inputIntPath) + "_" + suffix + ".INT")).string();
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

static int jsonInt(const json& obj, const char* key, int def) {
    const json& value = obj[key];
    return value.is_number() ? (int)value.get<double>() : def;
}

static std::string jsonString(const json& obj, const char* key, const std::string& def = {}) {
    const json& value = obj[key];
    return value.is_string() ? value.get<std::string>() : def;
}

static ParsedInputContacts parseInputContacts(const std::string& inputText) {
    ParsedInputContacts parsed;
    std::set<std::string> seen;
    std::istringstream ss(inputText);
    std::string line;
    const std::string prefix = "Contacts_";
    const std::string titleSuffix = "_Title";
    const std::string descSuffix = "_Description";

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        if (key.rfind(prefix, 0) != 0) continue;
        std::string contactKey;
        bool isTitle = false;
        bool isDesc = false;

        if (key.size() > prefix.size() + titleSuffix.size() &&
            key.compare(key.size() - titleSuffix.size(), titleSuffix.size(), titleSuffix) == 0) {
            contactKey = key.substr(prefix.size(), key.size() - prefix.size() - titleSuffix.size());
            isTitle = true;
        } else if (key.size() > prefix.size() + descSuffix.size() &&
            key.compare(key.size() - descSuffix.size(), descSuffix.size(), descSuffix) == 0) {
            contactKey = key.substr(prefix.size(), key.size() - prefix.size() - descSuffix.size());
            isDesc = true;
        }

        if (contactKey.empty()) continue;

        if (seen.insert(contactKey).second)
            parsed.orderedContactKeys.push_back(contactKey);

        InputContactEntry& entry = parsed.entries[contactKey];
        if (isTitle) {
            entry.titleKey = key;
            entry.titleValue = line.substr(eq + 1);
            entry.hasTitle = true;
        } else if (isDesc) {
            entry.descKey = key;
            entry.descValue = line.substr(eq + 1);
            entry.hasDesc = true;
        }
    }

    return parsed;
}

static json fetchContactDetail(const std::string& contactKey, int timeoutMs) {
    const std::string url =
        "https://api.apbdb.com/beacon/contacts/" + urlEncodePathSegment(contactKey);
    const HttpResponse resp = httpGet(url, timeoutMs);
    if (!resp.ok()) return {};

    bool ok = false;
    json root = json::parse(resp.body, ok);
    return ok && root.is_object() ? root : json{};
}

static void collectPackageItemNames(const json& package, std::vector<std::string>& out) {
    if (!package.is_object()) return;

    const json& items = package["eItems"];
    if (items.is_array()) {
        for (const auto& item : items) {
            const std::string name = stripUnlockPrefix(item.value("sDisplayName", ""));
            if (!name.empty()) out.push_back(name);
        }
    }

    collectPackageItemNames(package["eChildPackage"], out);
}

static std::string buildUnlocksDescription(const json& contact) {
    const json& levels = contact["aContactLevels"];
    if (!levels.is_array() || levels.empty()) return {};

    std::vector<const json*> orderedLevels;
    orderedLevels.reserve(levels.size());
    for (const auto& level : levels)
        if (level.is_object()) orderedLevels.push_back(&level);

    std::sort(orderedLevels.begin(), orderedLevels.end(), [](const json* a, const json* b) {
        return jsonInt(*a, "nLevel", 0) < jsonInt(*b, "nLevel", 0);
    });

    const std::string newline = "\xE2\x86\xB5";
    std::ostringstream out;
    bool wroteAny = false;

    for (const json* levelPtr : orderedLevels) {
        const json& level = *levelPtr;
        std::vector<std::string> names;
        collectPackageItemNames(level["eRewardPackage"], names);
        if (names.empty()) continue;

        if (wroteAny) out << newline;
        out << "Rank " << jsonInt(level, "nLevel", 0);
        for (const std::string& name : names)
            out << newline << "   Unlock: " << name;
        wroteAny = true;
    }

    return wroteAny ? out.str() : std::string{};
}

static std::string siblingGerPath(const std::string& inputIntPath) {
    std::error_code ec;
    fs::path p(inputIntPath);
    if (p.empty()) return {};

    fs::path candidate;
    if (p.has_parent_path() && p.parent_path().filename() == "INT" && p.parent_path().has_parent_path()) {
        candidate = p.parent_path().parent_path() / "GER" / (p.stem().string() + ".GER");
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
            return candidate.string();
    }

    candidate = p.parent_path() / (p.stem().string() + ".GER");
    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
        return candidate.string();

    return {};
}

static std::vector<std::string> splitDescriptionLines(const std::string& value) {
    static const std::string marker = "\xE2\x86\xB5";
    std::vector<std::string> lines;
    size_t begin = 0;
    while (begin <= value.size()) {
        const size_t pos = value.find(marker, begin);
        if (pos == std::string::npos) {
            lines.push_back(value.substr(begin));
            break;
        }
        lines.push_back(value.substr(begin, pos - begin));
        begin = pos + marker.size();
    }
    return lines;
}

static std::map<std::string, std::string> loadLegacyMissionLabels(const std::string& inputIntPath) {
    std::map<std::string, std::string> labels;
    const std::string gerPath = siblingGerPath(inputIntPath);
    if (gerPath.empty()) return labels;

    std::error_code ec;
    if (!fs::exists(gerPath, ec) || !fs::is_regular_file(gerPath, ec))
        return labels;

    const std::string text = readTextAny(gerPath);
    std::istringstream ss(text);
    std::string line;
    static const std::regex re(R"(^\[([A-Z]+)\]\s+(.+)\s+\(\d+/\d+\)$)");

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        if (key.rfind("Contacts_", 0) != 0 || key.find("_Description") == std::string::npos)
            continue;

        for (const std::string& part : splitDescriptionLines(line.substr(eq + 1))) {
            std::smatch match;
            if (!std::regex_match(part, match, re)) continue;
            labels.emplace(match[2].str(), match[1].str());
        }
    }

    return labels;
}

static std::string fallbackMissionLabel(
    const std::string& missionKey,
    const std::string& firstCategory,
    const std::string& lastStage,
    int ownerProfile)
{
    if (lastStage == "Escort") return "VIP";
    if (lastStage == "Take Over Deathmatch") return "TDM";
    if (lastStage == "Deathmatch") return "DM";
    if (lastStage == "Delivery") return "SCAV";
    if (lastStage == "Bombing") return "BOMB";
    if (lastStage == "Graffiti" || lastStage == "Vandalism") return "SPRAY";
    if (lastStage == "Territory Control") return "AREA";
    if (lastStage == "Moving Target") {
        if (ownerProfile == 8 ||
            firstCategory == "VehicleCargo" ||
            firstCategory == "VehicleTheft" ||
            missionKey.find("DelV") != std::string::npos) {
            return "TRUCK";
        }
        return "HOLD";
    }

    if (firstCategory == "VehicleCargo" || firstCategory == "VehicleTheft") return "TRUCK";
    if (firstCategory == "Graffiti" || firstCategory == "Vandalism") return "SPRAY";
    if (firstCategory == "Bombing" || firstCategory == "BombDisposal") return "BOMB";
    if (ownerProfile == 8) return "VIP";
    return "AREA";
}

static std::string resolveMissionLabel(
    const json& mission,
    std::map<std::string, std::string>& legacyTitleLabels,
    std::map<std::string, std::string>& missionKeyLabelCache)
{
    const std::string title = mission.value("sMissionTitle", "");
    if (!title.empty()) {
        auto legacyIt = legacyTitleLabels.find(title);
        if (legacyIt != legacyTitleLabels.end()) return legacyIt->second;
    }

    const std::string missionKey = mission.value("sAPBDB", "");
    if (missionKey.empty()) return "AREA";

    auto cachedIt = missionKeyLabelCache.find(missionKey);
    if (cachedIt != missionKeyLabelCache.end()) return cachedIt->second;

    const HttpResponse resp =
        httpGet("https://api.apbdb.com/beacon/missions/" + urlEncodePathSegment(missionKey), 15000);
    if (!resp.ok()) {
        missionKeyLabelCache[missionKey] = "AREA";
        return "AREA";
    }

    bool ok = false;
    json detail = json::parse(resp.body, ok);
    if (!ok || !detail.is_object()) {
        missionKeyLabelCache[missionKey] = "AREA";
        return "AREA";
    }

    std::string firstCategory;
    const json& stages = detail["aStages"];
    if (stages.is_array() && !stages.empty()) {
        const json& firstStage = stages[0];
        firstCategory = firstStage["eOperation"]["eTaskOperationCategory"].value("sAPBDB", "");
    }

    const std::string label = fallbackMissionLabel(
        missionKey,
        firstCategory,
        detail.value("sLastStage", ""),
        jsonInt(detail, "eMissionUIOwnerProfile", 0));

    missionKeyLabelCache[missionKey] = label;
    if (!title.empty()) legacyTitleLabels.emplace(title, label);
    return label;
}

static int missionLabelOrder(const std::string& label) {
    static const std::map<std::string, int> order = {
        {"AREA", 0},
        {"HOLD", 1},
        {"SCAV", 2},
        {"BOMB", 3},
        {"SPRAY", 4},
        {"DM", 5},
        {"TDM", 6},
        {"VIP", 7},
        {"TRUCK", 8}
    };
    auto it = order.find(label);
    return it == order.end() ? 99 : it->second;
}

static std::string buildMissionsDescription(
    const json& contact,
    std::map<std::string, std::string>& legacyTitleLabels,
    std::map<std::string, std::string>& missionKeyLabelCache)
{
    const json& missions = contact["aMissions"];
    if (!missions.is_array() || missions.empty()) return {};

    std::vector<MissionEntry> rows;
    rows.reserve(missions.size());

    for (const auto& mission : missions) {
        if (!mission.is_object()) continue;
        const std::string title = mission.value("sMissionTitle", "");
        if (title.empty()) continue;

        MissionEntry row;
        row.label = resolveMissionLabel(mission, legacyTitleLabels, missionKeyLabelCache);
        row.title = title;
        row.minGroup = jsonInt(mission, "nGroupSizeMin", 0);
        row.maxGroup = jsonInt(mission, "nGroupSizeMax", 0);
        rows.push_back(std::move(row));
    }

    if (rows.empty()) return {};

    std::sort(rows.begin(), rows.end(), [](const MissionEntry& a, const MissionEntry& b) {
        const int ao = missionLabelOrder(a.label);
        const int bo = missionLabelOrder(b.label);
        if (ao != bo) return ao < bo;
        return a.title < b.title;
    });

    const std::string newline = "\xE2\x86\xB5";
    std::ostringstream out;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) out << newline;
        out << '[' << rows[i].label << "] "
            << rows[i].title << " ("
            << rows[i].minGroup << '/' << rows[i].maxGroup << ')';
    }
    return out.str();
}

static std::string categoryDivider(const std::string& text) {
    const std::string ornamentStart =
        "\xE2\x9B\xA7\xCB\x9A\xE2\x82\x8A\xE2\x80\xA7[ ";
    const std::string ornamentEnd =
        " ]\xE2\x80\xA7\xE2\x82\x8A\xCB\x9A\xE2\x9B\xA7";
    return ";\n;                       " + ornamentStart + text + ornamentEnd + "\n;";
}

static ContactCategory categoriseContact(const std::string& contactKey, const json& contact) {
    if (contactKey == "None" ||
        contactKey == "Pinky" ||
        contactKey == "Binky" ||
        contactKey == "Inky" ||
        contactKey == "Winky" ||
        contactKey == "Clyde" ||
        contactKey.find("DefaultOrganisation") != std::string::npos ||
        contactKey.rfind("Organisation_", 0) == 0) {
        return contactKey.find("Organisation") != std::string::npos
            ? ContactCategory::ORGANISATIONS
            : ContactCategory::MISC;
    }

    std::string faction = jsonString(contact["eOrganisation"]["eFaction"], "sDisplayName");
    std::string district = jsonString(contact["eDistrict"], "sDisplayName");

    if (faction.empty()) {
        if (contactKey.find("_C") != std::string::npos) faction = "Criminal";
        else if (contactKey.find("_E") != std::string::npos) faction = "Enforcer";
    }

    if (district.empty()) {
        if (contactKey.rfind("Financial_", 0) == 0) district = "Financial";
        else if (contactKey.rfind("Waterfront_", 0) == 0) district = "Waterfront";
    }

    if (faction == "Criminal" && district == "Financial")
        return ContactCategory::CRIMINAL_FINANCIAL;
    if (faction == "Criminal" && district == "Waterfront")
        return ContactCategory::CRIMINAL_WATERFRONT;
    if (faction == "Enforcer" && district == "Financial")
        return ContactCategory::ENFORCER_FINANCIAL;
    if (faction == "Enforcer" && district == "Waterfront")
        return ContactCategory::ENFORCER_WATERFRONT;
    if (contactKey.find("Organisation") != std::string::npos)
        return ContactCategory::ORGANISATIONS;
    return ContactCategory::MISC;
}

static const char* categoryLabel(ContactCategory category) {
    switch (category) {
        case ContactCategory::CRIMINAL_FINANCIAL:  return "Financial Criminal Contacts";
        case ContactCategory::CRIMINAL_WATERFRONT: return "Waterfront Criminal Contacts";
        case ContactCategory::ENFORCER_FINANCIAL:  return "Financial Enforcer Contacts";
        case ContactCategory::ENFORCER_WATERFRONT: return "Waterfront Enforcer Contacts";
        case ContactCategory::ORGANISATIONS:       return "Organisation Contacts";
        case ContactCategory::MISC:
        default:                                   return "Misc Contacts";
    }
}

static std::string buildOutputText(
    const std::vector<OutputContactEntry>& changedContacts,
    const std::string& dbVersion,
    ContactDescriptionMode mode)
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
    out << "; DO NOT modify this file, modify the SDD table: Contact\n";
    out << "; Generated on: " << todayStr() << "\n";
    out << "; Database Version: " << dbVersion << "\n";
    out << "; Mode: " << (mode == ContactDescriptionMode::MISSIONS ? "MISSIONS" : "UNLOCKS") << "\n";
    out << ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n\n";
    out << "[Contacts]\n";

    const ContactCategory order[] = {
        ContactCategory::CRIMINAL_FINANCIAL,
        ContactCategory::CRIMINAL_WATERFRONT,
        ContactCategory::ENFORCER_FINANCIAL,
        ContactCategory::ENFORCER_WATERFRONT,
        ContactCategory::ORGANISATIONS,
        ContactCategory::MISC
    };

    for (ContactCategory category : order) {
        bool categoryHasLines = false;
        for (const auto& contact : changedContacts) {
            if (contact.category == (int)category && !contact.lines.empty()) {
                categoryHasLines = true;
                break;
            }
        }
        if (!categoryHasLines) continue;

        out << categoryDivider(categoryLabel(category)) << "\n";
        bool wroteContact = false;
        for (const auto& contact : changedContacts) {
            if (contact.category != (int)category || contact.lines.empty()) continue;
            if (wroteContact) out << "\n";
            for (const std::string& line : contact.lines)
                out << line << "\n";
            wroteContact = true;
        }
    }
    return normaliseColorClosingTags(out.str());
}

} // namespace

ContactDescriptionsResult generateContactDescriptionsFile(
    const std::string& inputIntPath,
    const std::string& outputPath,
    ContactDescriptionMode mode,
    std::function<void(int, int, const std::string&)> onProgress,
    const std::atomic<bool>* cancelFlag)
{
    const std::string inputText = readTextAny(inputIntPath);
    const ParsedInputContacts parsed = parseInputContacts(inputText);
    if (parsed.orderedContactKeys.empty())
        throw std::runtime_error("Could not find any Contacts entries in: " + inputIntPath);

    ContactDescriptionsResult result;
    std::vector<OutputContactEntry> changedContacts;
    std::map<std::string, std::string> legacyTitleLabels =
        mode == ContactDescriptionMode::MISSIONS ? loadLegacyMissionLabels(inputIntPath)
                                                 : std::map<std::string, std::string>{};
    std::map<std::string, std::string> missionKeyLabelCache;

    int fetchedContacts = 0;
    const int totalContacts = (int)parsed.orderedContactKeys.size();

    for (int i = 0; i < totalContacts; ++i) {
        if (cancelFlag && cancelFlag->load()) {
            result.cancelled = true;
            return result;
        }

        const std::string& contactKey = parsed.orderedContactKeys[i];
        const InputContactEntry& inputEntry = parsed.entries.at(contactKey);
        json contact = fetchContactDetail(contactKey, 15000);
        if (!contact.is_object()) {
            result.failedKeys.push_back(contactKey);
            if (onProgress) onProgress(i + 1, totalContacts, "[skip] " + contactKey);
            continue;
        }

        ++fetchedContacts;

        const std::string generatedDesc = mode == ContactDescriptionMode::MISSIONS
            ? buildMissionsDescription(contact, legacyTitleLabels, missionKeyLabelCache)
            : buildUnlocksDescription(contact);

        if (!generatedDesc.empty()) {
            OutputContactEntry outputEntry;
            outputEntry.contactKey = contactKey;
            outputEntry.category = (int)categoriseContact(contactKey, contact);

            if (inputEntry.hasTitle) {
                outputEntry.lines.push_back(
                    inputEntry.titleKey + "=" + normaliseColorClosingTags(inputEntry.titleValue));
            } else {
                outputEntry.lines.push_back("Contacts_" + contactKey + "_Title=" +
                    normaliseColorClosingTags(contact.value("sTitle", contactKey)));
            }

            const std::string descKey = inputEntry.hasDesc
                ? inputEntry.descKey
                : "Contacts_" + contactKey + "_Description";
            if (!inputEntry.hasDesc || generatedDesc != inputEntry.descValue) {
                outputEntry.lines.push_back(descKey + "=" + normaliseColorClosingTags(generatedDesc));
                changedContacts.push_back(std::move(outputEntry));
                result.updatedKeys.push_back(contactKey);
            }
        }

        if (onProgress) onProgress(i + 1, totalContacts, contactKey);
    }

    if (fetchedContacts == 0)
        throw std::runtime_error("Could not fetch any Contacts from APBDB.");

    const std::string dbVersion = fetchApbdbVersion(15);
    result.outputPath = outputPath.empty() ? defaultOutputPath(inputIntPath, mode) : outputPath;
    writeUtf16LE(result.outputPath, buildOutputText(changedContacts, dbVersion, mode));
    return result;
}

} // namespace apb
