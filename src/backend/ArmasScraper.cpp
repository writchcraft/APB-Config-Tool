#include "backend/ArmasScraper.h"
#include "backend/AppDirs.h"
#include "backend/HttpClient.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <algorithm>
#include <regex>
#include <cstdio>
#include <filesystem>
#include <chrono>
#include <random>
#include <cctype>
#include <windows.h>

namespace apb {

static std::string exeDir(){
    char buf[MAX_PATH]={};
    GetModuleFileNameA(nullptr,buf,MAX_PATH);
    std::string s=buf;
    auto pos=s.find_last_of("/\\");
    return pos==std::string::npos?".":s.substr(0,pos);
}

static std::string trim(std::string s){
    auto isWs=[](unsigned char c){ return std::isspace(c)!=0; };
    while(!s.empty() && isWs((unsigned char)s.front())) s.erase(s.begin());
    while(!s.empty() && isWs((unsigned char)s.back())) s.pop_back();
    return s;
}

static std::string caseFold(const std::string& s){
    std::string o=s;
    std::transform(o.begin(),o.end(),o.begin(),[](unsigned char c){ return (char)std::tolower(c); });
    return o;
}

ArmasScraper::ArmasScraper(const ScrapeConfig& cfg):m_cfg(cfg){
    m_outPath=cfg.outPath.empty()
        ? (std::filesystem::path(DownloadsDir())/"armas_known_ids.txt").string()
        : cfg.outPath;
}

void ArmasScraper::stop(){ m_stop=true; }

bool ArmasScraper::isBackoffCode(int code) const {
    return std::find(m_cfg.backoffCodes.begin(), m_cfg.backoffCodes.end(), code) != m_cfg.backoffCodes.end();
}

ArmasScraper::ProbeResult ArmasScraper::probe(int pid){
    std::string url=m_cfg.baseUrl+std::to_string(pid);
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> jitter(
        std::min(m_cfg.jitterMinSec,m_cfg.jitterMaxSec),
        std::max(m_cfg.jitterMinSec,m_cfg.jitterMaxSec));

    for(int tries=1; tries<=m_cfg.maxRetries+1 && !m_stop.load(); ++tries){
        std::this_thread::sleep_for(std::chrono::duration<double>(jitter(rng)));
        auto r=httpGet(url,int(m_cfg.timeout*1000),m_cfg.allowRedirects);
        int status=r.statusCode;

        if(status==0){
            std::this_thread::sleep_for(std::chrono::duration<double>(std::min(1.25*tries,4.0)));
            continue;
        }

        if(isBackoffCode(status)){
            std::this_thread::sleep_for(std::chrono::duration<double>(std::min(1.5*tries,6.0)));
            continue;
        }

        // Legacy rule: any non-302 is a hit.
        return {pid,status!=302,status};
    }
    return {pid,false,0};
}

std::string ArmasScraper::fetchTitle(const std::string& url){
    auto r=httpGet(url,int(m_cfg.titleTimeout*1000));
    if(!r.ok()) return "";
    std::regex rx("<title[^>]*>([\\s\\S]*?)</title>",std::regex::icase);
    std::smatch m;
    if(std::regex_search(r.body,m,rx)){
        std::string t=trim(std::regex_replace(m[1].str(),std::regex("\\s+")," "));
        t=std::regex_replace(t,std::regex("\\s*\\|\\s*GamersFirst.*$",std::regex::icase),"");
        t=trim(t);
        return t;
    }
    return "";
}

void ArmasScraper::loadExistingFile(){
    std::ifstream f(m_outPath);
    if(!f) return;
    std::string line;
    std::regex legacyPidRx("^\\s*(\\d+)\\s+(.+)$");
    while(std::getline(f,line)){
        if(!line.empty() && line.back()=='\r') line.pop_back();
        line=trim(line);
        if(line.empty() || line[0]=='#') continue;

        std::string title, url;
        size_t sep=line.rfind(" - ");
        if(sep!=std::string::npos){
            title=trim(line.substr(0,sep));
            url=trim(line.substr(sep+3));
        } else if(line.rfind("http://",0)==0 || line.rfind("https://",0)==0){
            url=line;
        } else {
            std::smatch m;
            if(std::regex_match(line,m,legacyPidRx)){
                int pid=std::stoi(m[1].str());
                title=trim(m[2].str());
                url=m_cfg.baseUrl+std::to_string(pid);
            }
        }

        if(url.empty()) continue;
        m_knownUrls.insert(url);
        if(!title.empty()){
            std::string tk=caseFold(title);
            if(m_knownByTitle.find(tk)==m_knownByTitle.end()){
                m_knownByTitle[tk]={title,url};
            }
        }
    }
}

void ArmasScraper::mergeAndSaveSorted(){
    auto merged=m_knownByTitle;
    auto urlsSeen=m_knownUrls;

    for(auto& [_,entry]:m_newHits){
        std::string titleKey=caseFold(trim(entry.title));
        if((!titleKey.empty() && merged.find(titleKey)!=merged.end()) || urlsSeen.find(entry.url)!=urlsSeen.end()){
            continue;
        }
        if(!titleKey.empty()) merged[titleKey]=entry;
        else merged[caseFold(entry.url)] = entry;
        urlsSeen.insert(entry.url);
    }

    std::vector<std::pair<std::string,std::string>> items;
    items.reserve(merged.size());
    for(auto& [_,entry]:merged){
        std::string display = entry.title.empty() ? entry.url : entry.title;
        items.push_back({display,entry.url});
    }
    std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){
        return caseFold(a.first) < caseFold(b.first);
    });

    std::filesystem::path outP(m_outPath);
    if(!outP.parent_path().empty()){
        std::error_code ec;
        std::filesystem::create_directories(outP.parent_path(), ec);
    }

    std::ofstream f(m_outPath);
    if(!f) return;
    f<<"# Valid / known product items (sorted by title)\n";
    for(auto& [display,url]:items){
        if(display==url) f<<url<<"\n";
        else f<<display<<" - "<<url<<"\n";
    }
}

std::string ArmasScraper::run(
    std::function<void(int,int,const std::string&)> onProgress,
    std::function<void(int,const std::string&,const std::string&)> onHit)
{
    loadExistingFile();
    int total=m_cfg.endId-m_cfg.startId+1;
    std::atomic<int> done{0}, nextId{m_cfg.startId};
    std::vector<std::thread> workers;

    for(int w=0;w<m_cfg.threads;++w){
        workers.emplace_back([&](){
            while(!m_stop){
                int pid=nextId.fetch_add(1);
                if(pid>m_cfg.endId) break;
                auto pr=probe(pid);
                int d=++done;
                std::ostringstream msg;
                msg<<"ID "<<pid<<" -> "<<pr.code;
                if(pr.isHit){
                    std::string url=m_cfg.baseUrl+std::to_string(pid);
                    std::string title;
                    if(m_cfg.includeTitles) title=fetchTitle(url);

                    std::string urlKey=trim(url);
                    std::string titleKey=caseFold(trim(title));

                    {
                        std::lock_guard<std::mutex> lk(m_lock);
                        bool knownTitle = !titleKey.empty() && (m_knownByTitle.find(titleKey)!=m_knownByTitle.end());
                        bool knownUrl = m_knownUrls.find(urlKey)!=m_knownUrls.end();
                        bool newUrlAlready = false;
                        for(const auto& [_,e] : m_newHits){
                            if(e.url == urlKey){ newUrlAlready = true; break; }
                        }
                        if(!knownTitle && !knownUrl && !newUrlAlready){
                            m_newHits[titleKey.empty()?caseFold(urlKey):titleKey] = {title,urlKey};
                        }
                    }
                    msg<<"  HIT";
                    if(onHit) onHit(pid,url,title.empty()?("Product "+std::to_string(pid)):title);
                }
                if(onProgress) onProgress(d,total,msg.str());
            }
        });
    }
    for(auto& t:workers) t.join();
    mergeAndSaveSorted();
    return m_outPath;
}

} // namespace apb
