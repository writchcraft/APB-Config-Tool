#include "backend/InventoryItemTypes.h"
#include "backend/AppDirs.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <stdexcept>
#include <windows.h>

namespace apb {

static std::string readUtf16LE(const std::string& path){
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open: "+path);
    std::string raw((std::istreambuf_iterator<char>(f)),{});
    if(raw.size()<2) return raw;
    bool bom=(unsigned char)raw[0]==0xFF&&(unsigned char)raw[1]==0xFE;
    int off=bom?2:0;
    int wlen=(int)((raw.size()-off)/2);
    std::wstring ws(wlen,0);
    memcpy(ws.data(),raw.data()+off,wlen*2);
    int n=WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,nullptr,0,nullptr,nullptr);
    std::string s(n,0);
    WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,s.data(),n,nullptr,nullptr);
    if(!s.empty()&&s.back()==0)s.pop_back();
    return s;
}

static std::string readLatin1(const std::string& path){
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open: "+path);
    return std::string((std::istreambuf_iterator<char>(f)),{});
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

static std::string dirOf(const std::string& p){auto pos=p.find_last_of("/\\");return pos==std::string::npos?".":p.substr(0,pos);}
static std::string baseNoExt(const std::string& p){
    auto pos=p.find_last_of("/\\"); std::string b=pos==std::string::npos?p:p.substr(pos+1);
    auto dot=b.rfind('.'); return dot==std::string::npos?b:b.substr(0,dot);
}

MergeResult mergeInventoryItemTypes(
    const std::string& gerPath,const std::string& intPath,
    const std::string& outputPath,bool appendRemaining)
{
    std::string gerText=readUtf16LE(gerPath);
    std::string intText=readLatin1(intPath);

    // Parse GER: ordered list of (key, fullLine)
    struct Entry{std::string key,line;};
    std::vector<Entry> gerEntries;
    std::map<std::string,int> gerIndex;
    {
        std::istringstream ss(gerText); std::string ln;
        while(std::getline(ss,ln)){
            if(!ln.empty()&&ln.back()=='\r')ln.pop_back();
            auto eq=ln.find('=');
            if(eq!=std::string::npos){std::string k=ln.substr(0,eq);gerEntries.push_back({k,ln});gerIndex[k]=(int)gerEntries.size()-1;}
            else gerEntries.push_back({"",ln});
        }
    }

    // Parse INT: ordered list
    std::vector<Entry> intEntries;
    std::map<std::string,int> intIndex;
    {
        std::istringstream ss(intText); std::string ln;
        while(std::getline(ss,ln)){
            if(!ln.empty()&&ln.back()=='\r')ln.pop_back();
            auto eq=ln.find('=');
            if(eq!=std::string::npos){std::string k=ln.substr(0,eq);if(!intIndex.count(k)){intEntries.push_back({k,ln});intIndex[k]=(int)intEntries.size()-1;}}
        }
    }

    // Find missing keys
    std::vector<std::string> added;
    std::map<int,std::vector<Entry>> insertAfter; // gerIndex → entries to insert after

    for(int ii=0;ii<(int)intEntries.size();++ii){
        const auto& ie=intEntries[ii];
        if(gerIndex.count(ie.key)) continue; // already present
        // Find best insertion point: look at INT neighbours
        int insertPos=-1;
        for(int back=ii-1;back>=0;--back){
            auto git=gerIndex.find(intEntries[back].key);
            if(git!=gerIndex.end()){insertPos=git->second;break;}
        }
        if(insertPos<0&&appendRemaining) insertPos=(int)gerEntries.size()-1;
        if(insertPos>=0){insertAfter[insertPos].push_back(ie);added.push_back(ie.key);}
    }

    // Build output
    std::string out;
    for(int i=0;i<(int)gerEntries.size();++i){
        out+=gerEntries[i].line+"\n";
        auto it=insertAfter.find(i);
        if(it!=insertAfter.end()) for(auto& e:it->second) out+=e.line+"\n";
    }

    std::string op = outputPath;
    if(op.empty()){
        namespace fs = std::filesystem;
        fs::path outDir(DownloadsDir());
        fs::create_directories(outDir);
        op = (outDir / (baseNoExt(gerPath) + "_MERGED.txt")).string();
    }
    writeUtf16LE(op,out);

    std::string report;
    for(auto& k:added) report+="+ "+k+"\n";
    return {op,report};
}
} // namespace apb
