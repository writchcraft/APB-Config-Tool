#pragma once
// ThemeLibrary.h — loads gradient themes from Themes/*.json next to the .exe
// Each theme has a name, up to 8 stepped colours, and a smooth start/end pair.
// Uses our bundled nlohmann/json.hpp — zero extra dependencies.

#include "Colors.h"
#include "AppDirs.h"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shlobj.h>
#  pragma comment(lib, "shell32.lib")
#endif

namespace apb {

struct GradientTheme {
    std::string      name;
    std::vector<RGB> stepped;     // 2–8 colours for hard/stepped mode
    RGB              smoothStart;
    RGB              smoothEnd;
    RGB              tripleMiddle;
};

// ── ThemeLibrary ─────────────────────────────────────────────────────────
struct ThemeLibrary {
    std::vector<GradientTheme> themes;
    std::string                error;   // last scan error, if any

    // Returns Documents\APBConfigTool\Themes (via AppDirs.h)
    static std::string themesDir(){ return apb::ThemesDir(); }

    // Creates the themes directory if it doesn't exist (called by reload)
    static void ensureDir(){
        std::filesystem::create_directories(themesDir());
    }

    // Scan Themes/ folder and reload all .json files
    void reload(){
        themes.clear();
        error.clear();
        std::string dir = themesDir();
        namespace fs = std::filesystem;

        // Always ensure the folder exists — create it on first run
        fs::create_directories(dir);

        if(!fs::exists(dir)){
            error = "Could not create Themes folder: " + dir;
            return;
        }

        for(auto& entry : fs::directory_iterator(dir)){
            if(entry.path().extension() != ".json") continue;
            GradientTheme t;
            if(!loadFile(entry.path().string(), t)){
                error += "Bad theme: " + entry.path().filename().string() + "\n";
                continue;
            }
            themes.push_back(std::move(t));
        }

        std::sort(themes.begin(), themes.end(),
            [](const GradientTheme& a, const GradientTheme& b){ return a.name < b.name; });
    }

    int count() const { return (int)themes.size(); }

    // Build a null-terminated list of names for ImGui::Combo
    // Appends "Custom" at the end. Caller must keep the returned strings alive.
    std::vector<std::string> nameList(bool includeCustom = true) const {
        std::vector<std::string> out;
        for(auto& t : themes) out.push_back(t.name);
        if(includeCustom) out.push_back("Custom");
        return out;
    }

private:
    static bool loadFile(const std::string& path, GradientTheme& out){
        std::ifstream f(path);
        if(!f) return false;
        std::ostringstream ss; ss << f.rdbuf();
        bool ok = false;
        json j = json::parse(ss.str(), ok);
        if(!ok || !j.is_object()) return false;

        out.name = j.value("name", "Unnamed");

        // stepped colours — array of [r,g,b] arrays
        if(j.contains("stepped") && j["stepped"].is_array()){
            for(auto& c : j["stepped"].get_array()){
                if(c.is_array() && c.size() >= 3)
                    out.stepped.push_back({
                        c[0].get<double>(),
                        c[1].get<double>(),
                        c[2].get<double>()
                    });
            }
        }

        // smooth start / end
        auto readRGB = [&](const std::string& key, RGB& rgb) -> bool {
            if(!j.contains(key)) return false;
            auto& arr = j[key];
            if(!arr.is_array() || arr.size() < 3) return false;
            rgb = { arr[0].get<double>(), arr[1].get<double>(), arr[2].get<double>() };
            return true;
        };

        // Fall back to first/last stepped colour if smooth keys missing
        if(!readRGB("smooth_start", out.smoothStart) && !out.stepped.empty())
            out.smoothStart = out.stepped.front();
        if(!readRGB("smooth_end",   out.smoothEnd)   && !out.stepped.empty())
            out.smoothEnd   = out.stepped.back();
        if(!readRGB("triple_middle", out.tripleMiddle)){
            if(out.stepped.size() >= 3){
                out.tripleMiddle = out.stepped[out.stepped.size() / 2];
            } else {
                out.tripleMiddle = lerpRGB(out.smoothStart, out.smoothEnd, 0.5);
            }
        }

        return !out.name.empty();
    }
};

// Singleton — call ThemeLib() anywhere
inline ThemeLibrary& ThemeLib(){
    static ThemeLibrary lib;
    static bool loaded = false;
    if(!loaded){ lib.reload(); loaded = true; }
    return lib;
}

} // namespace apb
