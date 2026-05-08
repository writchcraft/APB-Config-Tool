#pragma once
// AppDirs.h – single source of truth for all folder paths
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <cstdlib>
#include <string>
#include <filesystem>

namespace apb {

inline std::string AppDocDir(){
    char buf[MAX_PATH]={};
    if(SUCCEEDED(SHGetFolderPathA(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,buf)))
        return std::string(buf)+"\\APBConfigTool";
    const char* up=getenv("USERPROFILE");
    if(up) return std::string(up)+"\\Documents\\APBConfigTool";
    return ".\\APBConfigTool";
}
inline std::string ThemesDir()  { return AppDocDir()+"\\Themes";  }
inline std::string PresetsDir() { return AppDocDir()+"\\Presets"; }
inline std::string AssetsDir()  { return AppDocDir()+"\\Assets";  }
inline std::string WeaponImgDir(){ return AssetsDir()+"\\Images_WeaponItemTypes"; }
inline std::string VehicleImgDir(){ return AssetsDir()+"\\Images_VehicleItemTypes"; }
inline std::string DownloadsDir(){
    PWSTR wsz = nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &wsz)) && wsz){
        int n = WideCharToMultiByte(CP_UTF8, 0, wsz, -1, nullptr, 0, nullptr, nullptr);
        std::string out;
        if(n > 0){
            out.resize((size_t)n);
            WideCharToMultiByte(CP_UTF8, 0, wsz, -1, out.data(), n, nullptr, nullptr);
            if(!out.empty() && out.back()=='\0') out.pop_back();
        }
        CoTaskMemFree(wsz);
        if(!out.empty()) return out;
    }
    const char* up = std::getenv("USERPROFILE");
    if(up) return std::string(up)+"\\Downloads";
    return ".";
}

} // namespace apb
