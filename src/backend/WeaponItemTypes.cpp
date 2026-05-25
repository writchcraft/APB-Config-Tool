#include "backend/WeaponItemTypes.h"
#include "backend/AppDirs.h"
#include "backend/StatCalc.h"
#include "backend/HttpClient.h"
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
#include <ctime>
#include <cstring>

namespace fs = std::filesystem;
namespace apb {

// ── Date helper ───────────────────────────────────────────────────────────────
static std::string todayStr(){
    time_t t=time(nullptr); struct tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&t);
#else
    localtime_r(&t,&tm);
#endif
    char buf[12]; snprintf(buf,sizeof(buf),"%04d-%02d-%02d",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday);
    return buf;
}

// ── APB.DB version ───────────────────────────────────────────────────────────
std::string fetchApbdbVersion(int timeoutSec){
    auto resp=httpGet("https://api.apbdb.com/beacon/version", timeoutSec*1000);
    if(!resp.ok()) return "unknown";
    bool ok=false;
    json root=json::parse(resp.body,ok);
    if(!ok||!root.is_object()) return "unknown";
    if(root.contains("db")&&root["db"].is_object()){
        std::string v=root["db"].value("version","");
        if(!v.empty()) return v;
    }
    return root.value("version","unknown");
}

std::string buildWeaponHeader(const std::string& dbVersion){
    return
        "               _ _       _     \n"
        "              (_) |     | |    \n"
        "__      ___ __ _| |_ ___| |__  \n"
        "\\ \\ /\\ / / '__| | __/ __| '_ \\ \n"
        " \\ V  V /| |  | | || (__| | | |\n"
        "  \\_/\\_/ |_|  |_|\\__\\___|_| |_|\n"
        ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n"
        "; This is an automatically generated file\n"
        "; DO NOT modify this file, modify the SDD table: WeaponItemType\n"
        "; Generated on: " + todayStr() + "\n"
        "; Database Version: " + dbVersion + "\n"
        ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n\n"
        "[WeaponItemTypes]\n";
}

// ── File reading (handles UTF-16 LE/BE BOM and UTF-8) ────────────────────────
static std::string readTextAny(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open: "+path);
    std::string raw((std::istreambuf_iterator<char>(f)),{});
    if(raw.size()>=2){
        auto b0=uint8_t(raw[0]), b1=uint8_t(raw[1]);
        if(b0==0xFF&&b1==0xFE){ // UTF-16 LE
            std::string out;
            for(size_t i=2;i+1<raw.size();i+=2){
                uint16_t cp=uint8_t(raw[i])|(uint16_t(uint8_t(raw[i+1]))<<8);
                if(cp<0x80) out+=char(cp);
                else if(cp<0x800){out+=char(0xC0|(cp>>6));out+=char(0x80|(cp&0x3F));}
                else{out+=char(0xE0|(cp>>12));out+=char(0x80|((cp>>6)&0x3F));out+=char(0x80|(cp&0x3F));}
            }
            return out;
        }
        if(b0==0xEF&&b1==0xBB&&raw.size()>=3&&uint8_t(raw[2])==0xBF)
            return raw.substr(3); // UTF-8 BOM
    }
    return raw;
}

// ── Key extraction ────────────────────────────────────────────────────────────
std::vector<std::string> extractKeysFromInt(const std::string& text){
    // Process line-by-line (avoids std::regex multiline which MSVC doesn't support in C++17)
    std::regex re("WeaponItemTypes_([^_]+(?:_[^_]+)*)_Description=",
        std::regex_constants::icase);
    std::vector<std::string> keys;
    std::set<std::string> seen;
    std::istringstream ss(text);
    std::string line;
    while(std::getline(ss,line)){
        std::smatch m;
        if(std::regex_search(line,m,re)){
            std::string k=m[1].str();
            if(!seen.count(k)){seen.insert(k);keys.push_back(k);}
        }
    }
    return keys;
}

// ── File writing (UTF-16 LE with BOM) ────────────────────────────────────────
static void writeUtf16LE(const std::string& path, const std::string& utf8){
    // Create parent dirs
    fs::path p(path);
    if(p.has_parent_path()) fs::create_directories(p.parent_path());
    std::ofstream f(path, std::ios::binary|std::ios::trunc);
    if(!f) throw std::runtime_error("Cannot write: "+path);
    // BOM
    uint8_t bom[2]={0xFF,0xFE}; f.write((char*)bom,2);
    // Convert UTF-8 → UTF-16 LE
    for(size_t i=0;i<utf8.size();){
        uint8_t c=uint8_t(utf8[i]);
        uint32_t cp;
        if(c<0x80){cp=c;i++;}
        else if(c<0xE0){cp=(c&0x1F)<<6|(uint8_t(utf8[i+1])&0x3F);i+=2;}
        else{cp=(c&0x0F)<<12|(uint8_t(utf8[i+1])&0x3F)<<6|(uint8_t(utf8[i+2])&0x3F);i+=3;}
        uint8_t lo=cp&0xFF, hi=(cp>>8)&0xFF;
        f.put(lo); f.put(hi);
    }
}

// ── Parallel worker ───────────────────────────────────────────────────────────
bool generateWeaponDescriptionsFile(
    const std::string& inputIntPath, const std::string& outputPath,
    Scheme scheme, RGB single, RGB gradStart, RGB gradEnd,
    int timeoutSec, int maxWorkers,
    std::function<void(int,int,const std::string&)> onProgress,
    const std::atomic<bool>* cancelFlag)
{
    std::string src=readTextAny(inputIntPath);
    auto keys=extractKeysFromInt(src);

    std::string header=buildWeaponHeader(fetchApbdbVersion(timeoutSec));
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
                auto sr=statsFromApbdb(k,timeoutSec);
                std::string line;
                bool ok=sr.ok;
                if(!ok) line="; Skipped "+k+": could not fetch or parse APB.DB\n";
                else    line=formatFromInfra(k,sr.stats,scheme,single,gradStart,gradEnd);
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
