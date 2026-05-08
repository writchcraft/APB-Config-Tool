#pragma once
#include <string>
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

namespace apb {
struct ScrapeConfig {
    int startId=0,endId=25000,threads=32,maxRetries=3;
    double timeout=12.0,titleTimeout=12.0;
    std::string outPath;
    std::string baseUrl="https://www.gamersfirst.com/marketplace/ingame/"
        "product_details.php?storetype=g1c&gameID=20&catID=62&subcatID=258&productId=";
    bool allowRedirects=false;
    double jitterMinSec=0.05, jitterMaxSec=0.20;
    std::vector<int> backoffCodes{429,503,520};
    bool includeTitles=true;
};
struct ScrapeHit { int pid=0; std::string url,title; };
class ArmasScraper {
public:
    explicit ArmasScraper(const ScrapeConfig& cfg);
    void stop();
    std::string run(
        std::function<void(int,int,const std::string&)> onProgress=nullptr,
        std::function<void(int,const std::string&,const std::string&)> onHit=nullptr);
private:
    struct ProbeResult{ int pid; bool isHit; int code; };
    struct KnownEntry { std::string title, url; };

    ProbeResult probe(int pid);
    std::string fetchTitle(const std::string& url);
    void loadExistingFile();
    void mergeAndSaveSorted();
    bool isBackoffCode(int code) const;

    ScrapeConfig m_cfg;
    std::atomic<bool> m_stop{false};
    std::string m_outPath;
    std::map<std::string,KnownEntry> m_knownByTitle;
    std::set<std::string> m_knownUrls;
    std::map<std::string,KnownEntry> m_newHits;
    std::mutex m_lock;
};
} // namespace apb
