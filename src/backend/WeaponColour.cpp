#include "backend/WeaponColour.h"
#include "backend/GradientMaker.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <windows.h>

namespace apb {

std::vector<std::string> availableFonts(){
    return {"None","APBMenus_Font.APB_Helvetica_Regular_11","APBMenus_Font.APB_Helvetica_Regular_12",
            "APBMenus_Font.APB_Helvetica_Regular_14","APBMenus_Font.APB_Helvetica_Regular_16",
            "APBMenus_Font.APB_Helvetica_Bold_11","APBMenus_Font.APB_Helvetica_Bold_13",
            "APBMenus_Font.APB_Helvetica_Bold_14","APBMenus_Font.APB_Helvetica_Bold_24"};
}

std::vector<std::string> presetCategories(){
    return {"Rifle","SMG","AssaultRifle","SniperRifle","Shotgun","LMG",
            "Explosive","LTL","Pistol","Grenade"};
}

std::vector<GunTypeDefinition> weaponGunTypes(){
    return {
        {"Marksman", "Marksman", "Rifle", "InventoryItemTypes_Weapon_Rifle_"},
        {"SMG", "SMG", "SMG", "InventoryItemTypes_Weapon_SMG_"},
        {"Assault", "Assault", "AssaultRifle", "InventoryItemTypes_Weapon_AssaultRifle_"},
        {"Sniper", "Sniper", "SniperRifle", "InventoryItemTypes_Weapon_SniperRifle_"},
        {"Shotgun", "Shotgun", "Shotgun", "InventoryItemTypes_Weapon_Shotgun_"},
        {"LMG", "LMG", "LMG", "InventoryItemTypes_Weapon_LMG_"},
        {"Explosive", "Explosive", "Explosive", "InventoryItemTypes_Weapon_Explosive_"},
        {"Less Than Lethal / LTL", "LTL", "LTL", "InventoryItemTypes_Weapon_LTL_"},
        {"Pistol", "Pistol", "Pistol", "InventoryItemTypes_Weapon_Pistol_"},
        {"Grenade / Throwables", "Grenade", "Grenade", "InventoryItemTypes_Weapon_Grenade_"}
    };
}

std::map<std::string,RGB> writchPreset(){
    return {{"Rifle",{0.66,0.00,0.03}},{"SMG",{0.55,0.00,0.20}},{"AssaultRifle",{0.44,0.00,0.36}},
            {"SniperRifle",{0.33,0.00,0.54}},{"Shotgun",{0.22,0.00,0.72}},{"LMG",{0.20,0.00,0.90}},
            {"Pistol",{0.48,0.00,0.90}},{"SpecialPrimary",{0.66,0.00,0.66}},
            {"SpecialSecondary",{0.80,0.00,0.50}},{"Grenade",{0.90,0.00,0.30}},
            {"Consumable",{1.00,0.00,0.10}},{"Vehicle",{0.60,0.00,0.60}}};
}
std::map<std::string,RGB> spellboundPreset(){
    return {{"Rifle",{0.65,0.02,0.40}},{"SMG",{0.56,0.08,0.73}},{"AssaultRifle",{0.55,0.15,0.85}},
            {"SniperRifle",{0.24,0.04,0.81}},{"Shotgun",{0.14,0.01,0.68}},{"LMG",{0.08,0.00,0.78}},
            {"Pistol",{0.40,0.05,0.80}},{"SpecialPrimary",{0.50,0.10,0.75}},
            {"SpecialSecondary",{0.60,0.05,0.55}},{"Grenade",{0.70,0.02,0.45}},
            {"Consumable",{0.80,0.01,0.35}},{"Vehicle",{0.45,0.05,0.70}}};
}

std::map<std::string,RGB> presetByIndex(int presetIdx){
    return (presetIdx == 1) ? spellboundPreset() : writchPreset();
}

static const std::map<std::string,std::string> CATEGORY_ALIASES = {
    {"rifle","Rifle"},{"smg","SMG"},{"assaultrifle","AssaultRifle"},
    {"sniperrifle","SniperRifle"},{"shotgun","Shotgun"},{"lmg","LMG"},
    {"explosive","Explosive"},{"ltl","LTL"},{"pistol","Pistol"},{"grenade","Grenade"}
};

static std::string toLower(std::string s){for(char& c:s)c=(char)std::tolower((unsigned char)c);return s;}

static std::string normCat(const std::string& cat){
    auto it=CATEGORY_ALIASES.find(toLower(cat));
    return it!=CATEGORY_ALIASES.end()?it->second:cat;
}

// Read file auto-detecting UTF-16 LE / UTF-8
static std::string readAny(const std::string& path){
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open: "+path);
    std::string raw((std::istreambuf_iterator<char>(f)),{});
    // UTF-16 LE BOM
    if(raw.size()>=2&&(unsigned char)raw[0]==0xFF&&(unsigned char)raw[1]==0xFE){
        int wlen=(int)((raw.size()-2)/2);
        std::wstring ws(wlen,0);
        memcpy(ws.data(),raw.data()+2,wlen*2);
        int n=WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,nullptr,0,nullptr,nullptr);
        std::string s(n,0);
        WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,s.data(),n,nullptr,nullptr);
        if(!s.empty()&&s.back()==0)s.pop_back();
        return s;
    }
    if(raw.size()>=3&&(unsigned char)raw[0]==0xEF&&(unsigned char)raw[1]==0xBB&&(unsigned char)raw[2]==0xBF)
        return raw.substr(3);
    return raw;
}

static void writeUtf16LE(const std::string& path,const std::string& utf8){
    int wn=MultiByteToWideChar(CP_UTF8,0,utf8.c_str(),-1,nullptr,0);
    std::wstring ws(wn,0);
    MultiByteToWideChar(CP_UTF8,0,utf8.c_str(),-1,ws.data(),wn);
    if(!ws.empty()&&ws.back()==0)ws.pop_back();
    std::ofstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot write: "+path);
    const char bom[2]={'\xff','\xfe'};
    f.write(bom,2);
    f.write(reinterpret_cast<const char*>(ws.data()),ws.size()*2);
}

static std::string dirOf(const std::string& p){
    auto pos=p.find_last_of("/\\");
    return pos==std::string::npos?".":p.substr(0,pos);
}
static std::string baseOf(const std::string& p){
    auto pos=p.find_last_of("/\\");
    return pos==std::string::npos?p:p.substr(pos+1);
}

static std::string stripTags(const std::string& value){
    return std::regex_replace(value, std::regex("<[^>]*>"), "");
}

static std::string prettifyToken(std::string token){
    for(char& ch : token){
        if(ch == '_') ch = ' ';
    }
    return token;
}

static std::string trim(std::string value){
    size_t start=0;
    while(start<value.size()&&std::isspace((unsigned char)value[start])) ++start;
    size_t end=value.size();
    while(end>start&&std::isspace((unsigned char)value[end-1])) --end;
    return value.substr(start,end-start);
}

static std::vector<std::string> normalizeIgnoredKeys(const std::vector<std::string>& ignoredKeys){
    std::vector<std::string> out;
    for(auto item : ignoredKeys){
        item=trim(item);
        if(item.empty()) continue;
        auto eq=item.find('=');
        if(eq!=std::string::npos) item=item.substr(0,eq);
        item=trim(item);
        if(item.empty()) continue;
        out.push_back(toLower(item));
    }
    return out;
}

static bool isIgnoredKey(const std::string& fullKey,const std::vector<std::string>& ignoredKeys){
    std::string key=toLower(fullKey);
    for(const auto& ignored : ignoredKeys){
        if(key==ignored || key.find(ignored)!=std::string::npos)
            return true;
    }
    return false;
}

static std::string correctedCategoryForKey(const std::string& fullKey,const std::string& parsedCategory){
    std::string key=toLower(fullKey);
    if(key.find("inventoryitemtypes_weapon_assaultrifle_issr-stock_kingdom")==0)
        return "Rifle";
    if(key.find("inventoryitemtypes_weapon_smg_aces-rifle")==0)
        return "AssaultRifle";
    return parsedCategory;
}

static std::string applyMode(const std::string& text,const GunTypeColourSettings& s){
    if(s.mode == ColourMode::STEPPED)
        return hardGradientString(text, s.stepped, true);
    if(s.mode == ColourMode::SMOOTH)
        return smoothGradientString(text, s.smoothStart, s.smoothEnd, true);
    if(s.mode == ColourMode::TRIPLE)
        return tripleGradientString(text, s.tripleStart, s.tripleMiddle, s.tripleEnd, true);
    char buf[128];
    snprintf(buf,sizeof(buf),"<Color:R=%.6f G=%.6f B=%.6f>",s.solid.r,s.solid.g,s.solid.b);
    return std::string(buf)+text+"<Color:/>";
}

std::vector<WeaponInventoryEntry> scanInventoryWeapons(const std::string& inputPath){
    std::string text = readAny(inputPath);
    std::regex lineRx("^(InventoryItemTypes_Weapon_([^_=]+)_(.+)_DisplayName)=(.*)$",
        std::regex::icase);

    std::vector<WeaponInventoryEntry> out;
    std::set<std::string> seen;
    std::istringstream ss(text);
    std::string line;
    while(std::getline(ss, line)){
        if(!line.empty() && line.back() == '\r') line.pop_back();

        std::smatch m;
        if(!std::regex_match(line, m, lineRx))
            continue;

        WeaponInventoryEntry entry;
        entry.fullKey = m[1].str();
        entry.categoryToken = normCat(correctedCategoryForKey(entry.fullKey, m[2].str()));
        entry.itemToken = m[3].str();
        entry.displayName = trim(stripTags(m[4].str()));
        if(entry.displayName.empty())
            entry.displayName = prettifyToken(entry.itemToken);

        const std::string dedupeKey = toLower(entry.fullKey);
        if(!seen.insert(dedupeKey).second)
            continue;

        out.push_back(std::move(entry));
    }

    std::sort(out.begin(), out.end(), [](const WeaponInventoryEntry& a, const WeaponInventoryEntry& b){
        if(a.categoryToken != b.categoryToken)
            return a.categoryToken < b.categoryToken;
        if(a.displayName != b.displayName)
            return a.displayName < b.displayName;
        return a.fullKey < b.fullKey;
    });

    return out;
}

ColourResult applyColourToGerFile(
    const std::string& inputPath,
    const std::vector<GunTypeColourSettings>& settings,
    const std::string& fontTag,
    const std::vector<std::string>& ignoredKeys,
    std::function<void(const std::string&)> log,
    const std::atomic<bool>* cancelFlag)
{
    std::string text=readAny(inputPath);

    // Output dir
    std::string outDir=dirOf(inputPath)+"\\Output";
    CreateDirectoryA(outDir.c_str(),nullptr);
    std::string outPath=outDir+"\\"+baseOf(inputPath);

    ColourResult res;
    res.inputPath=inputPath; res.outputPath=outPath;

    std::map<std::string,GunTypeColourSettings> byCategory;
    std::map<std::string,GunTypeColourSettings> byFullKey;
    for(const auto& s : settings){
        if(!s.enabled)
            continue;
        if(!s.fullKey.empty())
            byFullKey[toLower(s.fullKey)] = s;
        else if(!s.categoryToken.empty())
            byCategory[normCat(s.categoryToken)] = s;
    }
    std::vector<std::string> normalizedIgnoredKeys=normalizeIgnoredKeys(ignoredKeys);

    // Regex: InventoryItemTypes_Weapon_<category>_..._DisplayName=...
    std::regex lineRx("^(InventoryItemTypes_Weapon_([^_=]+)(?:_.*)?_DisplayName)=(.*)$",
        std::regex::icase);

    std::string out;
    std::istringstream ss(text);
    std::string line;

    while(std::getline(ss,line)){
        if(cancelFlag && cancelFlag->load()){
            res.cancelled=true;
            break;
        }
        // Strip \r
        if(!line.empty()&&line.back()=='\r')line.pop_back();

        std::smatch m;
        if(std::regex_match(line,m,lineRx)){
            std::string fullKey=m[1].str();
            if(isIgnoredKey(fullKey,normalizedIgnoredKeys)){
                res.skippedRules++;
                out+=line+"\n";
                if(log) log("Ignored: "+fullKey);
                continue;
            }

            std::string cat=normCat(correctedCategoryForKey(fullKey,m[2].str()));
            std::string val=m[3].str();
            res.weaponsTotal++;

            const GunTypeColourSettings* selected = nullptr;
            auto fullIt = byFullKey.find(toLower(fullKey));
            if(fullIt != byFullKey.end()){
                selected = &fullIt->second;
            } else {
                auto catIt = byCategory.find(cat);
                if(catIt != byCategory.end())
                    selected = &catIt->second;
            }

            if(!selected){
                res.skippedRules++;
                out+=line+"\n";
                continue;
            }

            std::string plain=stripTags(val);
            if(plain != val) res.alreadyColoured++;
            std::string coloured=applyMode(plain,*selected);

            std::string prefix;
            if(fontTag!="None"&&!fontTag.empty()) prefix="<Fonts:"+fontTag+">";
            out+=fullKey+"="+prefix+coloured+"\n";
            res.newlyColoured++;
            if(log) log("Coloured: "+fullKey);
        } else {
            out+=line+"\n";
        }
    }

    if(res.cancelled){
        res.text=out;
        return res;
    }

    writeUtf16LE(outPath,out);
    res.text=out;
    return res;
}
} // namespace apb
