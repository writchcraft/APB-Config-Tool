#include "backend/StatCalc.h"
#include "backend/HttpClient.h"
#include "nlohmann/json.hpp"
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <cctype>
#include <regex>
#include <fstream>
#include <sstream>
#include <mutex>
#include <filesystem>

namespace apb {
namespace fs = std::filesystem;

static const std::string kWeaponSep = "\xe2\x86\xb5";

int calculateStk(double dmg, double pool) {
    if(dmg<=0) return INT_MAX;
    return int(std::ceil(pool/dmg));
}
double calculateTtk(double dmgPerShot, double fi, double pool) {
    int stk=calculateStk(dmgPerShot,pool);
    if(stk==INT_MAX||stk==0) return INF_VAL;
    return std::round(std::max(0.0,double(stk-1)*std::max(0.0,fi))*1000.0)/1000.0;
}
static double perTriggerHP(const Stats& s){ return s.healthDamage  * std::max(1,s.nPellets); }
static double perTriggerST(const Stats& s){ return s.staminaDamage * std::max(1,s.nPellets); }

double calculateTtkWithWindup(const Stats& s, double pool){
    double base=calculateTtk(perTriggerHP(s),s.fireInterval,pool);
    if(std::isinf(base)) return INF_VAL;
    return std::round((s.windUpTime+base)*1000.0)/1000.0;
}
int calculateSts(double stam, double pool){
    if(stam<=0) return INT_MAX;
    return int(std::ceil(pool/stam));
}
double calculateTtkLtlWithStats(const Stats& s, double pool){
    int shots=calculateSts(perTriggerST(s),pool);
    if(shots==INT_MAX) return INF_VAL;
    if(shots<=1) return 0.0;
    if(s.nMagazineCapacity==1) return std::round((shots-1)*s.reloadTime*1000.0)/1000.0;
    return std::round((shots-1)*s.fireInterval*1000.0)/1000.0;
}
double burstCalculateTtk(const Stats& s){
    int B=std::max(1,s.burstShots);
    double perShot=s.healthDamage*std::max(1,s.nPellets);
    if(perShot<=0) return INF_VAL;
    int N=int(std::ceil(HEALTH_POOL/perShot));
    if(N<=1) return s.windUpTime;
    int full=N/B, last=N%B;
    if(last==0){--full;last=B;}
    double v=s.windUpTime+double(full)*(B-1)*s.fireInterval+double(last-1)*s.fireInterval+double(full)*s.burstInterval;
    return std::round(v*1000.0)/1000.0;
}
int  explosiveCalculateStk(double dmg,  double pool){ return dmg>0  ? int(std::ceil(pool/dmg))  : INT_MAX; }
int  explosiveCalculateSts(double stam, double pool){ return stam>0 ? int(std::ceil(pool/stam)) : INT_MAX; }
double explosiveCalculateTtkRust(const Stats& s, bool isLtl){
    int shots=isLtl ? explosiveCalculateSts(s.explosiveStaminaDamage,STAMINA_POOL)
                    : explosiveCalculateStk(s.explosiveMaxHealthDamage,HEALTH_POOL);
    if(shots<=1){
        if(s.armingTimer!=0.0&&s.windUpTime!=0.0) return std::round(std::min(s.windUpTime,s.armingTimer)*1000.0)/1000.0;
        if(s.windUpTime==0.0) return std::round(s.armingTimer*1000.0)/1000.0;
        return std::round(s.windUpTime*1000.0)/1000.0;
    }
    if(isLtl&&s.isGlLtl) return std::round(((shots-1)*s.fireInterval+s.armingTimer)*1000.0)/1000.0;
    if(s.nMagazineCapacity==1) return std::round(((shots-1)*s.reloadTime+s.armingTimer)*1000.0)/1000.0;
    return std::round(((shots-1)*s.fireInterval+s.armingTimer)*1000.0)/1000.0;
}
double calculateAirBurstRustParity(double spd, double fuse){
    if(spd<=0||fuse<=0) return 0.0;
    return std::round(spd*fuse/100.0*1000.0)/1000.0;
}

// ── JSON helpers ─────────────────────────────────────────────────────────────
static double jDbl(const json& v, double d=0.0){
    if(v.is_number()) return v.get_double();
    if(v.is_string()){
        try{ return std::stod(v.get_string()); } catch(...){}
    }
    return d;
}
static int jInt(const json& v, int d=0){
    if(v.is_number()) return int(std::round(v.get_double()));
    if(v.is_string()){
        try{ return int(std::round(std::stod(v.get_string()))); } catch(...){}
    }
    return d;
}

static std::string canonJsonKey(const std::string& s){
    std::string out;
    out.reserve(s.size());
    for(char c:s){
        if(std::isalnum((unsigned char)c))
            out += char(std::tolower((unsigned char)c));
    }
    return out;
}
static const json* findCanonKey(const json& node, const std::string& key){
    if(!node.is_object()) return nullptr;
    const std::string want = canonJsonKey(key);
    for(const auto& [k,v] : node.get_object()){
        if(canonJsonKey(k) == want) return &v;
    }
    return nullptr;
}
static double objDbl(const json& node, std::initializer_list<const char*> keys, double d=0.0){
    for(auto key : keys){
        if(const json* v = findCanonKey(node, key)) return jDbl(*v, d);
    }
    return d;
}
static int objInt(const json& node, std::initializer_list<const char*> keys, int d=0){
    for(auto key : keys){
        if(const json* v = findCanonKey(node, key)) return jInt(*v, d);
    }
    return d;
}
static std::string objStr(const json& node, std::initializer_list<const char*> keys,
    const std::string& d = {})
{
    for(auto key : keys){
        if(const json* v = findCanonKey(node, key)){
            if(v->is_string()) return v->get_string();
        }
    }
    return d;
}

// Case-insensitive deep get: path is list of keys
static const json* deepGetPtr(const json& node, const std::vector<std::string>& path){
    const json* cur=&node;
    for(auto& k:path){
        if(!cur->is_object()) return nullptr;
        const json* next=findCanonKey(*cur,k);
        if(!next) return nullptr;
        cur=next;
    }
    return cur;
}
static json deepGet(const json& node, const std::vector<std::string>& path){
    auto* p=deepGetPtr(node,path);
    return p ? *p : json{};
}

static void applyDoubleMod(double& ref, const ModEffect& mod){
    auto r3=[](double x){ return std::round(x*1000.0)/1000.0; };
    ref = r3(mod.fAddToResult==0.0 ? ref*mod.fEffectMultiplier : ref+mod.fAddToResult);
}
static void applyIntMod(int& ref, const ModEffect& mod){
    double value = double(ref);
    value = mod.fAddToResult==0.0 ? value*mod.fEffectMultiplier : value+mod.fAddToResult;
    ref = int(std::round(std::max(0.0, value)));
}
void applyMods(Stats& s, const std::vector<ModEffect>& mods, double& hm, double& aux){
    (void)aux;
    for(auto& m:mods){
        switch(m.eEffectType){
        case 72:
            applyDoubleMod(s.rampDistanceInM, m);
            s.ltlRange = s.rampDistanceInM;
            break;
        case 167:
            applyIntMod(s.burstShots, m);
            break;
        case 166:
            applyDoubleMod(s.burstInterval, m);
            break;
        case 173:
            applyDoubleMod(s.equipTime, m);
            break;
        case 174:
        case 175:
            applyDoubleMod(s.fireInterval, m);
            break;
        case 179:
            applyDoubleMod(hm, m);
            break;
        case 180:
        case 181:
            applyDoubleMod(s.healthDamage, m);
            break;
        case 185:
            applyIntMod(s.nMagazineCapacity, m);
            break;
        case 190:
            applyDoubleMod(s.reloadTime, m);
            break;
        case 198:
            applyDoubleMod(s.staminaDamage, m);
            break;
        case 75:
            applyIntMod(s.nPellets, m);
            break;
        default: break;
        }
    }
}
static std::string normLbl(const std::string& s){
    std::string n; n.reserve(s.size());
    for(char c:s){
        if(c==' '||c=='_'||c=='-'||c==':'||c=='%') continue;
        n+=char(std::tolower((unsigned char)c));
    }
    return n;
}
static const std::map<std::string,std::string> N2C = {
    {"fireinterval","fireinterval"},{"reloadtime","reloadtime"},
    {"equiptime","equiptime"},{"winduptime","winduptime"},
    {"winduptime","winduptime"},{"healthdamage","healthdamage"},
    {"staminadamage","staminadamage"},{"dropoffrange","effectiverange"},
    {"magazinecapacity","magazinecapacity"},{"burstinterval","burstinterval"},
    {"burstshots","burstshots"},{"pellets","pellets"},
    {"explosionradius","explosionradius"},{"radius","explosionradius"},
    {"maxdamageradius","maxdamageradius"},
    {"fusedelay","fusedelay"},{"fusetime","fusedelay"},{"fuse","fusedelay"},
    {"armingtimer","armingtimer"},{"speed","firingspeed"},
    {"projectilespeed","firingspeed"},{"velocity","firingspeed"},
    {"muzzlevelocity","firingspeed"},
    {"maxhealthdamage","explosivedamage"},{"maxstaminadamage","explosivestun"},
    {"harddamagemodifier(explosion)","explosivehardmod"}
};
static double interpSpeed(double v){ return v<=0?0:v<=200.0?v*100.0:v; }

static std::optional<double> coerce(const json& v){
    if(v.is_number()) return v.get_double();
    if(v.is_string()){
        std::string s=v.get_string();
        // strip unit suffixes
        for(const char* sf:{" sec"," secs"," s"," m/s"," mps"," m"," cm/s"," cm"," ms"}){
            std::string suf(sf);
            if(s.size()>=suf.size()&&s.substr(s.size()-suf.size())==suf){
                s=s.substr(0,s.size()-suf.size()); break;
            }
        }
        try{ return std::stod(s); } catch(...){}
    }
    return {};
}
static std::optional<double> attrVal(const json& a){
    for(const char* k:{"modified","modifiedValue","modified_value","final","finalValue",
        "final_value","valueModified","value_modified","value"}){
        if(const json* v = findCanonKey(a, k)){
            auto r = coerce(*v);
            if(r) return r;
        }
    }
    std::string desc = objStr(a, {"description","Description"}, "");
    if(!desc.empty()){
        static const std::regex numRe(R"(([-+]?\d+(\.\d+)?))");
        std::smatch m;
        if(std::regex_search(desc,m,numRe)){
            try { return std::stod(m[1].str()); } catch(...) {}
        }
    }
    return {};
}
static bool hasFinal(const json& a){
    for(const char* k:{"modified","modifiedValue","modified_value","final","finalValue",
        "final_value","valueModified","value_modified"}){
        if(const json* v = findCanonKey(a, k)){
            if(!v->is_null()) return true;
        }
    }
    return false;
}
static std::vector<json> collectAttrs(const json& d){
    std::vector<json> out;
    for(auto& p: std::vector<std::vector<std::string>>{
        {"e_weapon_type_link","e_weapon_type_0","attributes"},
        {"e_weapon_type_link","e_weapon_type_0","Attributes"},
        {"e_weapon_type_link","e_weapon_type_0","e_weapon_attributes"},
        {"attributes"},
        {"Attributes"},
        {"e_weapon_attributes"}})
    {
        json v=deepGet(d,p);
        if(v.is_array()) for(auto& x:v.get_array()) if(x.is_object()) out.push_back(x);
    }
    return out;
}
static double projFuseDelaySec(const json& proj){
    for(const char* k:{"f_fuse_delay","fFuseDelay","fuse_delay","fFuseTime","FuseDelay"}){
        if(const json* v = findCanonKey(proj, k)){
            if(!v->is_null()) return jDbl(*v);
        }
    }
    if(proj.is_object()){
        for(const auto& [k,v] : proj.get_object()){
            std::string kl;
            kl.reserve(k.size());
            for(char c: k) if(c!='_') kl += char(std::tolower((unsigned char)c));
            if(kl.find("fuse")!=std::string::npos &&
               (kl.find("delay")!=std::string::npos || kl.find("time")!=std::string::npos))
                return jDbl(v);
        }
    }
    return 0.0;
}
static double projArmingTimerSec(const json& proj){
    for(const char* k:{"f_arming_timer","fArmingTimer","arming_timer"}){
        if(const json* v = findCanonKey(proj, k)){
            if(!v->is_null()) return jDbl(*v);
        }
    }
    return 0.0;
}
static double projSpeedCmps(const json& proj){
    for(const char* k:{"f_firing_speed","fFiringSpeed","firing_speed","Speed","speed","ProjectileSpeed","velocity"}){
        if(const json* v = findCanonKey(proj, k)){
            if(!v->is_null()) return interpSpeed(jDbl(*v));
        }
    }
    if(proj.is_object()){
        for(const auto& [k,v] : proj.get_object()){
            std::string kl;
            kl.reserve(k.size());
            for(char c: k) if(c!='_') kl += char(std::tolower((unsigned char)c));
            if(kl.find("speed")!=std::string::npos || kl.find("velocity")!=std::string::npos)
                return interpSpeed(jDbl(v));
        }
    }
    return 0.0;
}

static std::string trimCopy(const std::string& s){
    size_t a = 0, b = s.size();
    while(a < b && std::isspace((unsigned char)s[a])) ++a;
    while(b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}
static std::string lowerCopy(std::string s){
    for(char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
static std::string readTextAnyLocal(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    if(!f) return {};
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    if(raw.size() >= 2){
        auto b0 = uint8_t(raw[0]), b1 = uint8_t(raw[1]);
        if(b0 == 0xFF && b1 == 0xFE){
            std::string out;
            for(size_t i = 2; i + 1 < raw.size(); i += 2){
                uint16_t cp = uint8_t(raw[i]) | (uint16_t(uint8_t(raw[i+1])) << 8);
                if(cp < 0x80) out += char(cp);
                else if(cp < 0x800){
                    out += char(0xC0 | (cp >> 6));
                    out += char(0x80 | (cp & 0x3F));
                }else{
                    out += char(0xE0 | (cp >> 12));
                    out += char(0x80 | ((cp >> 6) & 0x3F));
                    out += char(0x80 | (cp & 0x3F));
                }
            }
            return out;
        }
        if(b0 == 0xEF && b1 == 0xBB && raw.size() >= 3 && uint8_t(raw[2]) == 0xBF)
            return raw.substr(3);
    }
    return raw;
}
static std::optional<double> firstNumber(const std::string& s){
    static const std::regex numRe(R"(([-+]?\d+(?:\.\d+)?))");
    std::smatch m;
    if(std::regex_search(s, m, numRe)){
        try { return std::stod(m[1].str()); } catch(...) {}
    }
    return {};
}
static bool parseNumberTimes(const std::string& s, double& value, int& times){
    static const std::regex timesRe(R"(^\s*([-+]?\d+(?:\.\d+)?)\s*x\s*(\d+)\s*$)", std::regex::icase);
    std::smatch m;
    if(std::regex_match(s, m, timesRe)){
        try{
            value = std::stod(m[1].str());
            times = std::max(1, std::stoi(m[2].str()));
            return true;
        } catch(...) {}
    }
    auto n = firstNumber(s);
    if(!n) return false;
    value = *n;
    times = 1;
    return true;
}
static std::vector<std::string> splitBySep(const std::string& s, const std::string& sep){
    std::vector<std::string> out;
    size_t pos = 0;
    while(true){
        size_t next = s.find(sep, pos);
        if(next == std::string::npos){
            out.push_back(s.substr(pos));
            break;
        }
        out.push_back(s.substr(pos, next - pos));
        pos = next + sep.size();
    }
    return out;
}
static std::string stripColorTags(const std::string& s){
    static const std::regex colorTagRe(R"(<\s*Color\s*:[^>]*>)", std::regex::icase);
    return std::regex_replace(s, colorTagRe, "");
}
static bool parseStatsCacheLine(const std::string& body, Stats& s){
    const std::string plain = stripColorTags(body);
    std::map<std::string,std::string> fields;
    for(const std::string& partRaw : splitBySep(plain, kWeaponSep)){
        const std::string part = trimCopy(partRaw);
        const size_t colon = part.find(':');
        if(colon == std::string::npos) continue;
        fields[lowerCopy(trimCopy(part.substr(0, colon)))] = trimCopy(part.substr(colon + 1));
    }
    if(fields.empty()) return false;

    auto has = [&](const char* key){ return fields.find(key) != fields.end(); };
    auto value = [&](const char* key)->std::string{
        auto it = fields.find(key);
        return it == fields.end() ? std::string{} : it->second;
    };
    auto assign = [&](double& dst, const char* key)->bool{
        auto n = firstNumber(value(key));
        if(!n) return false;
        dst = *n;
        return true;
    };
    auto assignInt = [&](int& dst, const char* key)->bool{
        auto n = firstNumber(value(key));
        if(!n) return false;
        dst = int(std::round(*n));
        return true;
    };
    auto assignDamage = [&](double& dst, int& times, const char* key)->bool{
        return parseNumberTimes(value(key), dst, times);
    };

    Stats parsed;
    if(has("explosion radius")){
        parsed.isThrownGrenade = true;
        assign(parsed.explosiveMaxHealthDamage, "max health damage");
        assign(parsed.explosiveStaminaDamage, "max stamina damage");
        assign(parsed.explosiveHardDamage, "max hard damage");
        assign(parsed.explosiveFRadius, "explosion radius");
        assign(parsed.explosiveFGroundZeroRadius, "max damage radius");
        assign(parsed.fuseDelay, "fuse delay");
        if(assign(parsed.firingSpeed, "speed")) parsed.firingSpeed *= 100.0;
    }else if(has("air burst distance")){
        parsed.isRl = true;
        assign(parsed.explosiveTtk, "time to kill");
        assignInt(parsed.explosiveStk, "shots to kill");
        assign(parsed.explosiveMaxHealthDamage, "max health damage");
        assign(parsed.explosiveStaminaDamage, "max stamina damage");
        assign(parsed.explosiveHardDamage, "max hard damage");
        assign(parsed.windUpTime, "wind up time");
        assign(parsed.reloadTime, "reload time");
        assign(parsed.equipTime, "equip time");
        assign(parsed.explosiveAirBurst, "air burst distance");
    }else if(has("max health damage") && has("time to stun")){
        parsed.isGlLtl = true;
        assign(parsed.explosiveTts, "time to stun");
        assignInt(parsed.explosiveSts, "shots to stun");
        assign(parsed.explosiveMaxHealthDamage, "max health damage");
        assign(parsed.explosiveStaminaDamage, "max stamina damage");
        assign(parsed.explosiveHardDamage, "max hard damage");
        assign(parsed.windUpTime, "wind up time");
        assign(parsed.fireInterval, "fire interval");
        assign(parsed.reloadTime, "reload time");
        assign(parsed.equipTime, "equip time");
    }else if(has("max health damage") && has("time to kill")){
        parsed.isGl = true;
        assign(parsed.explosiveTtk, "time to kill");
        assignInt(parsed.explosiveStk, "shots to kill");
        assign(parsed.explosiveMaxHealthDamage, "max health damage");
        assign(parsed.explosiveStaminaDamage, "max stamina damage");
        assign(parsed.explosiveHardDamage, "max hard damage");
        assign(parsed.windUpTime, "wind up time");
        assign(parsed.fireInterval, "fire interval");
        assign(parsed.reloadTime, "reload time");
        assign(parsed.equipTime, "equip time");
    }else if(has("time to stun")){
        parsed.isLtlAmmoWeapon = true;
        assign(parsed.tts, "time to stun");
        assignInt(parsed.sts, "shots to stun");
        assign(parsed.healthDamage, "health damage");
        assign(parsed.staminaDamage, "stamina damage");
        assign(parsed.hardDamage, "hard damage");
        assign(parsed.ltlRange, "effective range");
        assign(parsed.fireInterval, "fire interval");
        assign(parsed.reloadTime, "reload time");
        assign(parsed.equipTime, "equip time");
    }else if(has("burst interval")){
        int burstShots = 1;
        assign(parsed.burstTtk, "time to kill");
        assignInt(parsed.stk, "shots to kill");
        assignDamage(parsed.healthDamage, burstShots, "health damage");
        int burstShots2 = burstShots;
        assignDamage(parsed.staminaDamage, burstShots2, "stamina damage");
        assignDamage(parsed.hardDamage, burstShots2, "hard damage");
        parsed.burstShots = std::max(2, burstShots);
        assign(parsed.rampDistanceInM, "effective range");
        assign(parsed.burstInterval, "burst interval");
        assign(parsed.reloadTime, "reload time");
        assign(parsed.equipTime, "equip time");
    }else{
        int count = 1;
        assign(parsed.ttk, "time to kill");
        assignInt(parsed.stk, "shots to kill");
        assignDamage(parsed.healthDamage, count, "health damage");
        int count2 = count;
        assignDamage(parsed.staminaDamage, count2, "stamina damage");
        assignDamage(parsed.hardDamage, count2, "hard damage");
        parsed.nPellets = std::max(1, count);
        assign(parsed.rampDistanceInM, "effective range");
        assign(parsed.fireInterval, "fire interval");
        assign(parsed.reloadTime, "reload time");
        assign(parsed.equipTime, "equip time");
    }

    s = parsed;
    return true;
}
static std::optional<fs::path> findWeaponStatsCachePath(){
    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    if(ec) return {};
    for(int i = 0; i < 8; ++i){
        fs::path cand = cur / "PremadeConfigsEXAMPLES" / "writch" / "WeaponItemTypes.GER";
        if(fs::exists(cand, ec) && !ec) return cand;
        if(!cur.has_parent_path()) break;
        cur = cur.parent_path();
    }
    return {};
}
static std::map<std::string, Stats> buildWeaponStatsCache(){
    std::map<std::string, Stats> cache;
    auto path = findWeaponStatsCachePath();
    if(!path) return cache;
    const std::string text = readTextAnyLocal(path->string());
    if(text.empty()) return cache;

    std::istringstream ss(text);
    std::string line;
    while(std::getline(ss, line)){
        const std::string prefix = "WeaponItemTypes_";
        const std::string suffix = "_Description=";
        if(line.rfind(prefix, 0) != 0) continue;
        const size_t pos = line.find(suffix, prefix.size());
        if(pos == std::string::npos) continue;
        const std::string key = line.substr(prefix.size(), pos - prefix.size());
        Stats parsed;
        if(parseStatsCacheLine(line.substr(pos + suffix.size()), parsed))
            cache.emplace(key, parsed);
    }
    return cache;
}
static const std::map<std::string, Stats>& weaponStatsCache(){
    static std::once_flag once;
    static std::map<std::string, Stats> cache;
    std::call_once(once, [](){ cache = buildWeaponStatsCache(); });
    return cache;
}
static std::string stripRegex(const std::string& value, const std::regex& re){
    return std::regex_replace(value, re, "");
}
static std::vector<std::string> weaponKeyCandidates(const std::string& key){
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& cand){
        if(cand.empty()) return;
        if(seen.insert(cand).second) out.push_back(cand);
    };

    static const std::regex slotTailRe(R"((_Slot\d+)_([A-Za-z0-9]+)$)", std::regex::icase);
    static const std::regex slotRe(R"(_Slot\d+$)", std::regex::icase);
    static const std::regex armasPresetRe(R"(_Armas_Preset_FN\d+[A-Za-z0-9_]*$)", std::regex::icase);
    static const std::regex promoRe(R"((?:_[Pp][Rr]\d+[A-Za-z]?)(?:_Armas(?:_RUS)?)?$)");
    static const std::regex suffixRe(R"(_(?:Armas|Joker|NoTrade|Perm|Lease|Leased|5Day|7Day|30Day|GunGame|FC|JB|RUS|Common)$)", std::regex::icase);
    static const std::regex dashPromoRe(R"(-[A-Za-z0-9]+_[Pp][Rr]\d+[A-Za-z]?(?:_Armas(?:_RUS)?)?$)");

    add(key);
    std::string cur = key;
    add(stripRegex(cur, armasPresetRe));
    add(stripRegex(cur, dashPromoRe));
    add(stripRegex(cur, promoRe));
    add(stripRegex(cur, suffixRe));

    std::string slotBase = std::regex_replace(cur, slotTailRe, "$1");
    add(slotBase);
    add(stripRegex(slotBase, suffixRe));
    add(stripRegex(slotBase, slotRe));

    std::string noPromo = stripRegex(cur, promoRe);
    add(noPromo);
    add(std::regex_replace(noPromo, slotTailRe, "$1"));
    add(stripRegex(std::regex_replace(noPromo, slotTailRe, "$1"), slotRe));

    return out;
}
static StatsResult statsFromCache(const std::string& extracted){
    const auto& cache = weaponStatsCache();
    if(cache.empty()) return {};

    auto tryExactOrPrefix = [&](const std::string& cand)->std::optional<Stats>{
        auto it = cache.find(cand);
        if(it != cache.end()) return it->second;

        const Stats* best = nullptr;
        size_t bestLen = SIZE_MAX;
        const std::string prefix = cand + "_";
        for(const auto& [key, stats] : cache){
            if(key.rfind(prefix, 0) == 0 && key.size() < bestLen){
                best = &stats;
                bestLen = key.size();
            }
        }
        if(best) return *best;
        return {};
    };

    for(const std::string& cand : weaponKeyCandidates(extracted)){
        if(auto stats = tryExactOrPrefix(cand))
            return {"", *stats, true};
    }
    return {};
}
static std::string urlEncodePathSegment(const std::string& s){
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for(unsigned char c : s){
        if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~'){
            out += char(c);
        }else{
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

StatsResult statsFromApbdb(const std::string& extracted, int timeoutSec){
    std::string url="https://api.apbdb.com/beacon/items/"+urlEncodePathSegment(extracted);
    auto resp=httpGet(url, timeoutSec*1000);
    if(!resp.ok()) return statsFromCache(extracted);
    bool ok=false;
    json root=json::parse(resp.body,ok);
    if(!ok||!root.is_object()) return statsFromCache(extracted);

    std::string infra = root.value("infracategory","");
    auto& detV = root["detail"];
    if(!detV.is_object()) return statsFromCache(extracted);
    const json& d = detV;

    auto gd=[&](std::vector<std::string> p){ return deepGet(d,p); };

    json w0   = gd({"e_weapon_type_link","e_weapon_type_0"});
    json rwt  = gd({"e_weapon_type_link","e_weapon_type_0","e_ranged_weapon_type"});
    json curve= gd({"e_weapon_type_link","e_weapon_type_0","e_weapon_curve"});
    json proj = gd({"e_weapon_type_link","e_weapon_type_0","e_weapon_projectile"});
    if(!proj.is_object()) proj = gd({"e_weapon_type_link","e_weapon_type_0","e_projectile"});
    json expl = deepGet(proj,{"e_explosion"});
    if(!expl.is_object()) expl = gd({"e_weapon_type_link","e_weapon_type_0","e_projectile","eExplosion"});

    Stats s;
    s.healthDamage      = objDbl(w0, {"f_health_damage"});
    s.staminaDamage     = objDbl(w0, {"f_stamina_damage"});
    double hm           = objDbl(w0, {"f_hard_damage_modifier"});
    s.fireInterval      = objDbl(w0, {"f_fire_interval"});
    s.reloadTime        = objDbl(w0, {"f_reload_time"});
    s.equipTime         = objDbl(w0, {"f_equip_time"});
    s.windUpTime        = objDbl(w0, {"f_wind_up_time"});
    s.nMagazineCapacity = objInt(w0, {"n_magazine_capacity"});
    s.nPellets          = std::max(1, objInt(rwt, {"n_rays_per_shot"}, 1));
    double rampDistance = objDbl(rwt, {"f_ramp_distance"});
    double curveStart   = jDbl(deepGet(curve,{"sEffectiveRange","start_range"}));
    s.burstShots        = std::max(1, objInt(w0, {"n_burst_shots"}, 1));
    s.burstInterval     = objDbl(w0, {"f_burst_interval"});

    // Explosives - try both snake_case and camelCase keys
    auto exGet=[&](const json& src, std::initializer_list<const char*> keys)->double{
        return objDbl(src, keys, 0.0);
    };
    s.explosiveMaxHealthDamage = exGet(expl,{"n_damage","nDamage"});
    s.explosiveStaminaDamage   = exGet(expl,{"n_stun_damage","nStunDamage"});
    s.explosiveFRadius         = exGet(expl,{"f_explosion_radius","fExplosionRadius"});
    s.explosiveFGroundZeroRadius=exGet(expl,{"f_ground_zero_radius","fGroundZeroRadius"});
    double hardModExplBase     = exGet(expl,{"f_hard_damage_modifier","fHardDamageModifier"});
    double hardModExpl         = hardModExplBase;

    s.fuseDelay   = projFuseDelaySec(proj);
    s.armingTimer = projArmingTimerSec(proj);
    s.firingSpeed = projSpeedCmps(proj);

    // Attributes
    std::vector<json> attrs = collectAttrs(d);

    double dropoffRangeM = 0.0;
    for(auto& av:attrs){
        if(!av.is_object()) continue;
        if(normLbl(objStr(av, {"name"}, "")) != "dropoffrange") continue;
        auto val = attrVal(av);
        if(val){ dropoffRangeM = *val; break; }
    }

    if(s.firingSpeed <= 0.0){
        json gwSpd = deepGet(w0,{"grenade_weapon_type","f_firing_speed"});
        if(!gwSpd.is_null()) s.firingSpeed = interpSpeed(jDbl(gwSpd));
    }
    if(s.firingSpeed <= 0.0){
        for(auto& av:attrs){
            if(!av.is_object()) continue;
            std::string canonKey = normLbl(objStr(av, {"name"}, ""));
            if(canonKey=="speed" || canonKey=="projectilespeed" || canonKey=="velocity" || canonKey=="muzzlevelocity"){
                auto val = attrVal(av);
                if(val){ s.firingSpeed = *val * 100.0; break; }
            }
        }
    }
    if(s.fuseDelay <= 0.0){
        for(auto& av:attrs){
            if(!av.is_object()) continue;
            std::string canonKey = normLbl(objStr(av, {"name"}, ""));
            if(canonKey=="fusedelay" || canonKey=="fusetime" || canonKey=="fuse"){
                auto val = attrVal(av);
                if(val){ s.fuseDelay = *val; break; }
            }
        }
    }
    if(s.armingTimer <= 0.0){
        for(auto& av:attrs){
            if(!av.is_object()) continue;
            if(normLbl(objStr(av, {"name"}, ""))=="armingtimer"){
                auto val = attrVal(av);
                if(val){ s.armingTimer = *val; break; }
            }
        }
    }

    s.rampDistanceInM = dropoffRangeM>0.0 ? dropoffRangeM : (rampDistance>0.0 ? rampDistance : curveStart);
    s.ltlRange = s.rampDistanceInM;

    std::vector<std::string> modTargets={"fire interval","reload time","equip time","health damage","dropoff range"};
    std::vector<std::string> modModifiers={"hard damage modifier (explosion)"};
    bool attrsHaveFinals=false;
    for(auto& av:attrs){
        if(!av.is_object()) continue;
        std::string nm=objStr(av, {"name"}, ""); std::string nml=nm;
        for(auto& c:nml) c=char(std::tolower((unsigned char)c));
        // trim
        while(!nml.empty()&&nml.front()==' ') nml.erase(nml.begin());
        while(!nml.empty()&&nml.back()==' ') nml.pop_back();
        for(auto& t:modTargets) if(nml==t&&hasFinal(av)){attrsHaveFinals=true;break;}
        for(auto& t:modModifiers) if(nml==t&&hasFinal(av)){attrsHaveFinals=true;break;}
        if(attrsHaveFinals) break;
    }

    std::vector<ModEffect> mods;
    for(const char* sl:{"eFnMod_0","eFnMod_1","eFnMod_2"}){
        json arr=deepGet(d,{sl,"eModifierItem","aModifierEffects"});
        if(!arr.is_array()) continue;
        for(auto& mv:arr.get_array()){
            if(!mv.is_object()) continue;
            mods.push_back({jInt(mv["eEffectType"]),jDbl(mv["fAddToResult"]),
                mv.contains("fEffectMultiplier")?jDbl(mv["fEffectMultiplier"]):1.0});
        }
    }

    double modScratch = 0.0;
    if(!attrsHaveFinals&&!mods.empty()) applyMods(s,mods,hm,modScratch);

    static const std::set<std::string> mut={"fireinterval","reloadtime","equiptime","healthdamage","effectiverange"};
    for(auto& av:attrs){
        if(!av.is_object()) continue;
        std::string nm=objStr(av, {"name"}, "");
        auto it=N2C.find(normLbl(nm));
        if(it==N2C.end()) continue;
        const std::string& canon=it->second;
        auto val=attrVal(av); if(!val) continue;
        double v=*val;
        if(!attrsHaveFinals&&!mods.empty()&&mut.count(canon)) continue;
        if     (canon=="fireinterval")     s.fireInterval=v;
        else if(canon=="reloadtime")       s.reloadTime=v;
        else if(canon=="equiptime")        s.equipTime=v;
        else if(canon=="winduptime")       s.windUpTime=v;
        else if(canon=="healthdamage")     s.healthDamage=v;
        else if(canon=="staminadamage")    s.staminaDamage=v;
        else if(canon=="pellets")          s.nPellets=int(std::round(v));
        else if(canon=="magazinecapacity") s.nMagazineCapacity=int(std::round(v));
        else if(canon=="burstinterval")    s.burstInterval=v;
        else if(canon=="burstshots")       s.burstShots=int(std::round(v));
        else if(canon=="effectiverange")   {s.rampDistanceInM=v;s.ltlRange=v;}
        else if(canon=="explosionradius")  s.explosiveFRadius=v;
        else if(canon=="maxdamageradius")  s.explosiveFGroundZeroRadius=v;
        else if(canon=="fusedelay")        s.fuseDelay=v;
        else if(canon=="armingtimer")      s.armingTimer=v;
        else if(canon=="firingspeed")      s.firingSpeed=v*100.0;
        else if(canon=="explosivedamage")  s.explosiveMaxHealthDamage=v;
        else if(canon=="explosivestun")    s.explosiveStaminaDamage=v;
        else if(canon=="explosivehardmod") hardModExpl=v;
    }

    if(s.rampDistanceInM>200){s.rampDistanceInM/=100.0;s.ltlRange=s.rampDistanceInM;}
    s.hardDamage=std::round(std::max(0.0,hm)*s.healthDamage*1000.0)/1000.0;
    double hardModExplFinal = hardModExplBase!=0.0 ? hardModExplBase : hardModExpl;
    if(s.explosiveMaxHealthDamage>0&&hardModExplFinal>0)
        s.explosiveHardDamage=std::round(s.explosiveMaxHealthDamage*hardModExplFinal*1000.0)/1000.0;

    std::string keyL=extracted; for(auto& c:keyL) c=char(std::tolower((unsigned char)c));
    std::string sApb=objStr(w0, {"s_apbdb","sAPBDB"}, "");
    s.isThrownGrenade = keyL.rfind("weapon_grenade_",0)==0
                     || infra=="WeaponGrenade"||infra=="WeaponGrenadeChristmas";
    s.isRl    =(infra=="WeaponPrimaryRocketLauncher");
    s.isGl    =(infra=="WeaponPrimaryGrenadeLaunchers"||infra=="WeaponSecondaryFlaregun"||infra=="WeaponPrimaryChristmas");
    s.isGlLtl =(infra=="WeaponPrimaryLessThanLethal"&&sApb=="LTL_GrenadeLauncher");
    s.isLtlAmmoWeapon=(infra=="WeaponPrimaryLessThanLethal"&&!s.isGlLtl)||sApb=="LTL_DartGun"||sApb=="LTL_Tazer";

    if(s.burstShots>1){s.stk=calculateStk(perTriggerHP(s),HEALTH_POOL);s.burstTtk=burstCalculateTtk(s);}
    else{s.stk=calculateStk(perTriggerHP(s),HEALTH_POOL);s.ttk=calculateTtkWithWindup(s,HEALTH_POOL);}
    if(s.isLtlAmmoWeapon&&!(s.isGl||s.isGlLtl||s.isRl)){
        s.sts=calculateSts(perTriggerST(s),STAMINA_POOL);
        s.tts=calculateTtkLtlWithStats(s,STAMINA_POOL);
    }
    if(s.explosiveMaxHealthDamage>0) s.explosiveStk=explosiveCalculateStk(s.explosiveMaxHealthDamage,HEALTH_POOL);
    if(s.explosiveStaminaDamage>0)   s.explosiveSts=explosiveCalculateSts(s.explosiveStaminaDamage,STAMINA_POOL);
    if(s.isGl&&!s.isGlLtl) s.windUpTime=0.0;
    if(s.isGlLtl) s.explosiveTts=explosiveCalculateTtkRust(s,true);
    if(s.isGl||s.isRl) s.explosiveTtk=explosiveCalculateTtkRust(s,false);
    if(s.fuseDelay>0&&s.firingSpeed>0)
        s.explosiveAirBurst=calculateAirBurstRustParity(s.firingSpeed,s.fuseDelay);

    return {infra,s,true};
}

} // namespace apb
