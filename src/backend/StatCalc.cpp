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

namespace apb {

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

StatsResult statsFromApbdb(const std::string& extracted, int timeoutSec){
    std::string url="https://api.apbdb.com/beacon/items/"+extracted;
    auto resp=httpGet(url, timeoutSec*1000);
    if(!resp.ok()) return {};
    bool ok=false;
    json root=json::parse(resp.body,ok);
    if(!ok||!root.is_object()) return {};

    std::string infra = root.value("infracategory","");
    auto& detV = root["detail"];
    if(!detV.is_object()) return {};
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
