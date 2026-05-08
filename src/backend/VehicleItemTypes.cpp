#include "backend/VehicleItemTypes.h"
#include "backend/AppDirs.h"
#include "backend/HttpClient.h"
#include "backend/WeaponItemTypes.h"  // fetchApbdbVersion
#include "nlohmann/json.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <thread>
#include <mutex>
#include "backend/Semaphore.h"
#include <atomic>
#include <vector>
#include <set>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>

namespace fs = std::filesystem;
namespace apb {

// ── Format helpers ────────────────────────────────────────────────────────────
static std::string fmtNum(double x){
    char buf[32]; snprintf(buf,sizeof(buf),"%.2f",x);
    std::string s=buf;
    while(s.size()>1&&s.back()=='0') s.pop_back();
    if(!s.empty()&&s.back()=='.') s.pop_back();
    return s;
}
static std::string fmtMps(double x){ return fmtNum(x)+" m/s"; }
static std::string fmtCm(double x) { return fmtNum(x)+" cm";  }

// ── Vehicle weight lookup ─────────────────────────────────────────────────────
static const std::vector<std::pair<std::string,double>> WEIGHT_MAP = {
    {"A_2DrCoupe",1.6},{"A_2DrVan",1.0},{"A_ClassicMuscle",4.0},
    {"estatevan",2.0},{"A_ExecSaloon",1.6},{"A_ExoticMuscle",2.5},
    {"A_Hatchback",1.0},{"A_KingCab",3.0},{"A_LowRider",5.5},
    {"A_PerformanceSaloon",2.4},{"A_Pickup",2.5},{"A_Roadster",2.4},
    {"A_Saloon",2.4},{"A_SportsSUV",1.5},{"A_SUV",3.1},
    {"A_Taxi",2.75},{"truckcurtain",100.0},{"A_Utility1Estate",2.0},
    {"vanambulance",2.1},{"vanarmoured",8.0},{"A_VanStandard",2.5},
    {"C_Carrying",5.5},{"C_Compact",1.0},{"C_Perf",2.5},
    {"E_Carrying",5.5},{"E_Compact",1.0},{"E_Perf",2.5},
};
static double lookupWeight(const std::string& key){
    std::string kl=key; for(auto& c:kl) c=char(std::tolower((unsigned char)c));
    for(auto& [pat,val]:WEIGHT_MAP){
        std::string pl=pat; for(auto& c:pl) c=char(std::tolower((unsigned char)c));
        if(kl.find(pl)!=std::string::npos) return val;
    }
    return 0.0;
}

// ── JSON helpers ──────────────────────────────────────────────────────────────
static double jDbl(const json& v, double d=0.0){
    if(v.is_number()) return v.get_double();
    if(v.is_string()){ try{return std::stod(v.get_string());}catch(...){} }
    return d;
}
static const json* deepGetPtr(const json& node, const std::vector<std::string>& path){
    const json* cur=&node;
    for(auto& k:path){
        if(!cur->is_object()) return nullptr;
        const json* next=cur->find_ci(k);
        if(!next) return nullptr;
        cur=next;
    }
    return cur;
}
static json deepGet(const json& node, const std::vector<std::string>& path){
    auto* p=deepGetPtr(node,path); return p?*p:json{};
}
static double deepFuzzy(const json& src, const std::vector<std::string>& names, double def=0.0){
    if(src.is_null()||!src.is_object()&&!src.is_array()) return def;
    if(src.is_object()){
        for(auto& n:names){
            auto* v=src.find_ci(n);
            if(v&&v->is_number()) return v->get_double();
        }
        for(auto& [k,v]:src.get_object()){
            double r=deepFuzzy(v,names,std::numeric_limits<double>::infinity());
            if(!std::isinf(r)) return r;
        }
    }
    if(src.is_array()){
        for(auto& v:src.get_array()){
            double r=deepFuzzy(v,names,std::numeric_limits<double>::infinity());
            if(!std::isinf(r)) return r;
        }
    }
    return def;
}

// ── APB.DB fetch ───────────────────────────────────────────────────────────────
VStatsResult vehicleStatsFromApbdb(const std::string& key, int timeoutSec){
    std::string url="https://api.apbdb.com/beacon/items/"+key;
    auto resp=httpGet(url, timeoutSec*1000);
    if(!resp.ok()) return {};
    bool ok=false;
    json root=json::parse(resp.body,ok);
    if(!ok||!root.is_object()) return {};
    auto& detV=root["detail"];
    if(!detV.is_object()) return {};
    const json& d=detV;

    // Find vehicle sub-object
    json veh;
    for(const char* k:{"eVehicle","e_vehicle","vehicle"}){
        auto* v=d.find_ci(k);
        if(v&&v->is_object()){veh=*v;break;}
    }
    if(veh.empty()){
        for(auto& [k,v]:d.get_object()){
            std::string kl=k; for(auto& c:kl) c=char(std::tolower((unsigned char)c));
            if(kl.find("vehicle")!=std::string::npos&&v.is_object()){veh=v;break;}
        }
    }
    const json& src=veh.empty()?d:veh;

    // Explosion sub-object
    json destruct;
    for(const char* k:{"eExplosionType","e_explosion","explosion"}){
        auto* v=src.find_ci(k); if(!v) v=d.find_ci(k);
        if(v&&v->is_object()){destruct=*v;break;}
    }

    auto gfuzzy=[&](const json& s, std::initializer_list<const char*> ns)->double{
        for(auto n:ns){ auto* v=s.find_ci(n); if(v&&v->is_number()) return v->get_double(); }
        return 0.0;
    };

    double maxHealth = gfuzzy(src,{"nMaxHealth","f_max_health","fMaxHealth","maxHealth","health","hp"});
    if(maxHealth<=0) maxHealth=deepFuzzy(d,{"nMaxHealth","maxHealth","health","hp"});

    double maxSpeed = gfuzzy(src,{"fMaxSpeed","f_max_speed_mps","maxSpeed","speed","top_speed_mps"});
    if(maxSpeed<=0) maxSpeed=deepFuzzy(d,{"fMaxSpeed","maxSpeed","speed","topSpeed"});

    double revSpeed = gfuzzy(src,{"fMaxReverseSpeed","f_max_reverse_speed_mps","maxReverseSpeed","reverseSpeed"});
    if(revSpeed<=0) revSpeed=deepFuzzy(d,{"fMaxReverseSpeed","maxReverseSpeed","reverseSpeed"});

    double cargo = gfuzzy(src,{"nMainCargoPipCapacity","i_cargo_capacity","cargoCapacity","cargo"});
    if(cargo<=0) cargo=deepFuzzy(d,{"nMainCargoPipCapacity","cargoCapacity","cargo"});

    double expDmg = gfuzzy(destruct,{"nDamage","maxDamage","explosionDamage","fExplosionDamage"});
    if(expDmg<=0) expDmg=deepFuzzy(d,{"nDamage","maxDamage","explosionDamage"});

    double expRadius = gfuzzy(destruct,{"fExplosionRadius","fExplosionRadiusCM","explosionRadius","radius"});
    if(expRadius<=0) expRadius=deepFuzzy(d,{"fExplosionRadius","explosionRadius","radius"});

    auto normSpeed=[](double v)->double{
        if(v<=0) return 0;
        if(v>1000) return v/100.0;
        if(v>=100) return v/3.6;
        return v;
    };
    maxSpeed=normSpeed(maxSpeed); revSpeed=normSpeed(revSpeed);
    if(expRadius>0&&expRadius<50) expRadius*=100.0;

    VStats vs;
    vs.maxHealth=maxHealth; vs.maxSpeedMps=maxSpeed; vs.maxReverseSpeedMps=revSpeed;
    vs.cargoCapacity=cargo; vs.vehicleWeight=lookupWeight(key);
    vs.explosionMaxDamage=expDmg; vs.explosionRadiusCm=expRadius;
    return {vs,true};
}

// ── Header ────────────────────────────────────────────────────────────────────
std::string buildVehicleHeader(const std::string& dbVersion){
    time_t t=time(nullptr); struct tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&t);
#else
    localtime_r(&t,&tm);
#endif
    char now[12]; snprintf(now,sizeof(now),"%04d-%02d-%02d",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday);
    return
        "               _ _       _     \n"
        "              (_) |     | |    \n"
        "__      ___ __ _| |_ ___| |__  \n"
        "\\ \\ /\\ / / '__| | __/ __| '_ \\ \n"
        " \\ V  V /| |  | | || (__| | | |\n"
        "  \\_/\\_/ |_|  |_|\\__\\___|_| |_|\n"
        ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n"
        "; This is an automatically generated file\n"
        "; DO NOT modify this file, modify the SDD table: VehicleItemType\n"
        "; Generated on: " + std::string(now) + "\n"
        "; Database Version: " + dbVersion + "\n"
        ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n\n"
        "[VehicleItemTypes]\n";
}

// ── Key extraction ────────────────────────────────────────────────────────────
static std::vector<std::string> extractVehicleKeys(const std::string& text){
    std::regex re("VehicleItemTypes_([^=]+?)_Description\\s*=",
        std::regex_constants::icase);
    std::vector<std::string> keys;
    std::set<std::string> seen;
    std::istringstream ss(text);
    std::string line;
    while(std::getline(ss,line)){
        std::smatch m;
        if(std::regex_search(line,m,re)){
            std::string k=m[1].str();
            while(!k.empty()&&k.back()==' ') k.pop_back();
            while(!k.empty()&&k.front()==' ') k.erase(k.begin());
            if(!seen.count(k)){seen.insert(k);keys.push_back(k);}
        }
    }
    return keys;
}

static std::string readTextAny(const std::string& path){
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open: "+path);
    std::string raw((std::istreambuf_iterator<char>(f)),{});
    if(raw.size()>=2){
        auto b0=uint8_t(raw[0]),b1=uint8_t(raw[1]);
        if(b0==0xFF&&b1==0xFE){
            std::string out;
            for(size_t i=2;i+1<raw.size();i+=2){
                uint16_t cp=uint8_t(raw[i])|(uint16_t(uint8_t(raw[i+1]))<<8);
                if(cp<0x80) out+=char(cp);
                else if(cp<0x800){out+=char(0xC0|(cp>>6));out+=char(0x80|(cp&0x3F));}
                else{out+=char(0xE0|(cp>>12));out+=char(0x80|((cp>>6)&0x3F));out+=char(0x80|(cp&0x3F));}
            }
            return out;
        }
        if(b0==0xEF&&b1==0xBB&&raw.size()>=3&&uint8_t(raw[2])==0xBF) return raw.substr(3);
    }
    return raw;
}

// ── Line builder ───────────────────────────────────────────────────────────────
static const std::string SEP="\xe2\x86\xb5"; // ↵ U+21B5

static std::string buildVehicleLine(const std::string& key, const VStats& v,
    Scheme scheme, RGB single, RGB gs, RGB ge)
{
    auto lerp=[](double a,double b,double t){return a+(b-a)*t;};
    auto ctag=[](double r,double g,double b,const std::string& t)->std::string{
        char buf[256];
        snprintf(buf,sizeof(buf),"<Color:R=%.3f G=%.3f B=%.3f>%s<Color:/>",r,g,b,t.c_str());
        return buf;
    };
    auto grad=[&](const std::string& lbl, RGB c1, RGB c2)->std::string{
        if(lbl.empty()) return {};
        int n=std::max(1,(int)lbl.size()-1);
        std::string out;
        for(int i=0;i<(int)lbl.size();++i){
            double t=n==0?0.0:double(i)/n;
            out+=ctag(lerp(c1.r,c2.r,t),lerp(c1.g,c2.g,t),lerp(c1.b,c2.b,t),std::string(1,lbl[i]));
        }
        return out;
    };
    auto triple=[&](const std::string& lbl, RGB c1, RGB c2, RGB c3)->std::string{
        if(lbl.empty()) return {};
        int n=std::max(1,(int)lbl.size()-1);
        std::string out;
        for(int i=0;i<(int)lbl.size();++i){
            double t=n==0?0.0:double(i)/n;
            RGB c = (t <= 0.5)
                ? RGB{lerp(c1.r,c2.r,t*2.0),lerp(c1.g,c2.g,t*2.0),lerp(c1.b,c2.b,t*2.0)}
                : RGB{lerp(c2.r,c3.r,(t-0.5)*2.0),lerp(c2.g,c3.g,(t-0.5)*2.0),lerp(c2.b,c3.b,(t-0.5)*2.0)};
            out+=ctag(c.r,c.g,c.b,std::string(1,lbl[i]));
        }
        return out;
    };
    auto solid=[&](const std::string& lbl, RGB c)->std::string{
        return ctag(c.r,c.g,c.b,lbl);
    };
    auto label=[&](const std::string& txt)->std::string{
        if(scheme==Scheme::WRITCH)     return grad(txt,WRITCH_START(),WRITCH_END());
        if(scheme==Scheme::SPELLBOUND) return grad(txt,SPELL_START(),SPELL_END());
        if(scheme==Scheme::GRADIENT)   return grad(txt,gs,ge);
        if(scheme==Scheme::TRIPLE)     return triple(txt,gs,single,ge);
        if(scheme==Scheme::SINGLE)     return solid(txt,single);
        return txt;
    };

    std::vector<std::string> parts;
    if(scheme==Scheme::CLEAR){
        parts.push_back("Max Health: "+std::to_string(int(std::round(v.maxHealth))));
        parts.push_back("Max Speed: "+fmtMps(v.maxSpeedMps));
        parts.push_back("Max Reverse Speed: "+fmtMps(v.maxReverseSpeedMps));
        parts.push_back("Cargo Capacity: "+std::to_string(int(std::round(v.cargoCapacity))));
        parts.push_back("Vehicle Weight: "+fmtNum(v.vehicleWeight));
        parts.push_back("Explosion Max Damage: "+std::to_string(int(std::round(v.explosionMaxDamage))));
        parts.push_back("Explosion Radius: "+fmtCm(v.explosionRadiusCm));
    } else {
        parts.push_back(label("Max Health:")+" "+std::to_string(int(std::round(v.maxHealth))));
        parts.push_back(label("Max Speed:")+" "+fmtMps(v.maxSpeedMps));
        parts.push_back(label("Max Reverse Speed:")+" "+fmtMps(v.maxReverseSpeedMps));
        parts.push_back(label("Cargo Capacity:")+" "+std::to_string(int(std::round(v.cargoCapacity))));
        parts.push_back(label("Vehicle Weight:")+" "+fmtNum(v.vehicleWeight));
        parts.push_back(label("Explosion Max Damage:")+" "+std::to_string(int(std::round(v.explosionMaxDamage))));
        parts.push_back(label("Explosion Radius:")+" "+fmtCm(v.explosionRadiusCm));
    }
    std::string joined;
    for(size_t i=0;i<parts.size();++i){ if(i) joined+=SEP; joined+=parts[i]; }
    return "VehicleItemTypes_"+key+"_Description="+joined+"\n";
}

// ── File writing ───────────────────────────────────────────────────────────────
static void writeUtf16LE(const std::string& path, const std::string& utf8){
    fs::path p(path);
    if(p.has_parent_path()) fs::create_directories(p.parent_path());
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    if(!f) throw std::runtime_error("Cannot write: "+path);
    uint8_t bom[2]={0xFF,0xFE}; f.write((char*)bom,2);
    for(size_t i=0;i<utf8.size();){
        uint8_t c=uint8_t(utf8[i]);
        uint32_t cp;
        if(c<0x80){cp=c;i++;}
        else if(c<0xE0){cp=(c&0x1F)<<6|(uint8_t(utf8[i+1])&0x3F);i+=2;}
        else{cp=(c&0x0F)<<12|(uint8_t(utf8[i+1])&0x3F)<<6|(uint8_t(utf8[i+2])&0x3F);i+=3;}
        uint8_t lo=cp&0xFF, hi=(cp>>8)&0xFF;
        f.put(char(lo)); f.put(char(hi));
    }
}

// ── Main generator ────────────────────────────────────────────────────────────
bool generateVehicleDescriptionsFile(
    const std::string& inputIntPath, const std::string& outputPath,
    Scheme scheme, RGB single, RGB gradStart, RGB gradEnd,
    int timeoutSec, int maxWorkers,
    std::function<void(int,int,const std::string&)> onProgress,
    const std::atomic<bool>* cancelFlag)
{
    std::string src=readTextAny(inputIntPath);
    auto keys=extractVehicleKeys(src);

    std::string header=buildVehicleHeader(fetchApbdbVersion(timeoutSec));
    int total=int(keys.size());
    std::vector<std::string> results(total);
    std::mutex mu;
    std::atomic<int> done{0};
    std::atomic<int> next{0};

    std::vector<std::thread> threads;
    threads.reserve(std::max(1, maxWorkers));
    for(int worker=0; worker<std::max(1, maxWorkers); ++worker){
        threads.emplace_back([&](){
            while(true){
                if(cancelFlag && cancelFlag->load()) break;

                int i=next.fetch_add(1);
                if(i>=total) break;

                const std::string& k=keys[i];
                auto sr=vehicleStatsFromApbdb(k,timeoutSec);
                std::string line;
                bool ok=sr.ok;
                if(!ok) line="; Skipped "+k+": could not fetch or parse APB.DB\n";
                else    line=buildVehicleLine(k,sr.stats,scheme,single,gradStart,gradEnd);
                {std::lock_guard<std::mutex> lk(mu); results[i]=line;}
                int d=++done;
                if(onProgress) onProgress(d,total,ok?k:"[skip] "+k);
            }
        });
    }
    for(auto& t:threads) t.join();

    if(cancelFlag && cancelFlag->load()) return false;

    std::string body=header;
    for(auto& l:results) body+=l;

    std::string outPath=outputPath;
    if(outPath.empty()){
        fs::path p(inputIntPath);
        fs::path outDir(DownloadsDir());
        fs::create_directories(outDir);
        outPath=(outDir/(p.stem().string()+"_Generated.GER")).string();
    }
    writeUtf16LE(outPath,body);
    return true;
}

} // namespace apb
