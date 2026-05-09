#include "backend/PremadeConfigs.h"

#include "backend/AppDirs.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace apb {
namespace {

enum class EmbeddedEncoding {
    Utf8,
    Utf8Bom,
    Utf16LeBom
};

struct TextFile {
    std::wstring text;
    EmbeddedEncoding encoding = EmbeddedEncoding::Utf8;
};

struct PremadeSourceFile {
    std::string templateName;
    std::string relativePath;
    std::string absolutePath;
};

struct EditableValueAccumulator {
    PremadeConfigEditableValue value;
    std::set<std::string> files;
};

struct PremadeCache {
    std::vector<PremadeConfigSummary> summaries;
    std::map<std::string, std::vector<PremadeSourceFile>> filesByTemplate;
    bool dirty = true;
};

static std::mutex g_premadeMutex;
static std::string g_premadeLocation = PremadeConfigsDir();
static PremadeCache g_premadeCache;

static std::string normalisePathString(const std::string& path){
    if(path.empty())
        return PremadeConfigsDir();
    return std::filesystem::path(path).lexically_normal().string();
}

static bool loadFileBytes(const std::string& path, std::vector<unsigned char>& out){
    std::ifstream in(path, std::ios::binary);
    if(!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return in.good() || in.eof();
}

static std::wstring utf8ToWide(const std::string& text){
    if(text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if(needed <= 0) return {};
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), needed);
    return out;
}

static std::string wideToUtf8(const std::wstring& text){
    if(text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if(needed <= 0) return {};
    std::string out((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), needed, nullptr, nullptr);
    return out;
}

static bool decodeTextFile(const std::vector<unsigned char>& bytes, TextFile& out){
    if(bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE){
        out.encoding = EmbeddedEncoding::Utf16LeBom;
        const wchar_t* data = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
        const size_t wcharCount = (bytes.size() - 2) / sizeof(wchar_t);
        out.text.assign(data, data + wcharCount);
        return true;
    }

    if(bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF){
        out.encoding = EmbeddedEncoding::Utf8Bom;
        out.text = utf8ToWide(std::string((const char*)bytes.data() + 3, (const char*)bytes.data() + bytes.size()));
        return true;
    }

    out.encoding = EmbeddedEncoding::Utf8;
    out.text = utf8ToWide(std::string((const char*)bytes.data(), (const char*)bytes.data() + bytes.size()));
    return true;
}

static std::string trimCopy(const std::string& text){
    const size_t first = text.find_first_not_of(" \t\r\n");
    if(first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

static std::string collapseWhitespace(const std::string& text){
    std::string out;
    out.reserve(text.size());

    bool inSpace = false;
    for(char ch : text){
        const bool isSpace = ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        if(isSpace){
            if(!out.empty() && !inSpace)
                out.push_back(' ');
            inSpace = true;
            continue;
        }

        out.push_back(ch);
        inSpace = false;
    }

    if(!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

static std::string normaliseTagValue(const std::string& value){
    return collapseWhitespace(trimCopy(value));
}

static void inspectEditableColourTags(
    const std::string& utf8Text,
    const std::string& relativePath,
    PremadeConfigSummary& summary,
    std::map<std::string, EditableValueAccumulator>& editableValues)
{
    static const std::regex openRgbTag(R"(<\s*Color\s*:(?!\s*/)\s*([^>]*)>)", std::regex::icase);
    static const std::regex openNamedTag(R"(<\s*col\s*:\s*([^>]*)>)", std::regex::icase);

    PremadeConfigEditableFile editableFile;
    editableFile.relativePath = relativePath;

    for(std::sregex_iterator it(utf8Text.begin(), utf8Text.end(), openRgbTag), end; it != end; ++it){
        const std::string value = normaliseTagValue((*it)[1].str());
        const std::string key = "rgb:" + value;
        auto& acc = editableValues[key];
        if(acc.value.kind.empty()){
            acc.value.kind = "RGB";
            acc.value.value = value.empty() ? "(empty)" : value;
            acc.value.replacementHint = "<Color:R={r} G={g} B={b}>";
        }
        acc.value.occurrences++;
        acc.files.insert(relativePath);
        editableFile.rgbColourTags++;
    }

    for(std::sregex_iterator it(utf8Text.begin(), utf8Text.end(), openNamedTag), end; it != end; ++it){
        const std::string value = normaliseTagValue((*it)[1].str());
        const std::string key = "named:" + value;
        auto& acc = editableValues[key];
        if(acc.value.kind.empty()){
            acc.value.kind = "Named";
            acc.value.value = value.empty() ? "(empty)" : value;
            acc.value.replacementHint = "<col:{color}>";
        }
        acc.value.occurrences++;
        acc.files.insert(relativePath);
        editableFile.namedColourTags++;
    }

    if(editableFile.namedColourTags == 0 && editableFile.rgbColourTags == 0)
        return;

    summary.recolourableFiles++;
    summary.namedColourTagCount += editableFile.namedColourTags;
    summary.rgbColourTagCount += editableFile.rgbColourTags;
    summary.colourTagCount += editableFile.namedColourTags + editableFile.rgbColourTags;
    summary.editableFiles.push_back(std::move(editableFile));
}

static std::string makeTimestampFolderName(){
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
    localtime_s(&localTm, &nowTime);

    std::ostringstream out;
    out << "APBCT_" << std::put_time(&localTm, "%Y%m%d_%H%M%S");
    return out.str();
}

static std::string makeOutputRoot(const PremadeConfigExportOptions& options){
    std::filesystem::path root(options.outputDir);
    root /= makeTimestampFolderName();
    return root.string();
}

static std::vector<PremadeSourceFile> scanPremadeSourceFiles(const std::string& rootPath){
    namespace fs = std::filesystem;

    std::vector<PremadeSourceFile> files;
    std::error_code ec;
    const fs::path root(rootPath);
    if(!fs::exists(root, ec) || !fs::is_directory(root, ec))
        return files;

    for(const auto& templateEntry : fs::directory_iterator(root, ec)){
        if(ec) break;
        if(!templateEntry.is_directory(ec)) continue;

        const std::string templateName = templateEntry.path().filename().string();
        const fs::path templateRoot = templateEntry.path();
        for(const auto& entry : fs::recursive_directory_iterator(templateRoot, ec)){
            if(ec) break;
            if(!entry.is_regular_file(ec)) continue;

            fs::path rel = fs::relative(entry.path(), templateRoot, ec);
            if(ec) rel = entry.path().filename();

            files.push_back({
                templateName,
                rel.generic_string(),
                entry.path().string()
            });
        }
    }

    std::sort(files.begin(), files.end(), [](const PremadeSourceFile& a, const PremadeSourceFile& b){
        if(a.templateName != b.templateName) return a.templateName < b.templateName;
        return a.relativePath < b.relativePath;
    });
    return files;
}

static void rebuildPremadeCacheLocked(){
    g_premadeCache = {};

    std::map<std::string, PremadeConfigSummary> byName;
    std::map<std::string, std::map<std::string, EditableValueAccumulator>> editableByTemplate;

    for(const auto& file : scanPremadeSourceFiles(g_premadeLocation)){
        g_premadeCache.filesByTemplate[file.templateName].push_back(file);

        auto& summary = byName[file.templateName];
        summary.name = file.templateName;
        summary.fileCount++;

        std::vector<unsigned char> bytes;
        if(!loadFileBytes(file.absolutePath, bytes))
            continue;

        TextFile text;
        if(!decodeTextFile(bytes, text))
            continue;

        inspectEditableColourTags(wideToUtf8(text.text), file.relativePath, summary,
            editableByTemplate[file.templateName]);
    }

    g_premadeCache.summaries.reserve(byName.size());
    for(auto& [templateName, summary] : byName){
        auto& editableValues = editableByTemplate[templateName];
        summary.editableValues.reserve(editableValues.size());
        for(auto& [key, acc] : editableValues){
            (void)key;
            acc.value.fileCount = (int)acc.files.size();
            summary.editableValues.push_back(std::move(acc.value));
        }

        std::sort(summary.editableValues.begin(), summary.editableValues.end(),
            [](const PremadeConfigEditableValue& a, const PremadeConfigEditableValue& b){
                if(a.kind != b.kind) return a.kind < b.kind;
                if(a.occurrences != b.occurrences) return a.occurrences > b.occurrences;
                return a.value < b.value;
            });
        std::sort(summary.editableFiles.begin(), summary.editableFiles.end(),
            [](const PremadeConfigEditableFile& a, const PremadeConfigEditableFile& b){
                return a.relativePath < b.relativePath;
            });

        g_premadeCache.summaries.push_back(std::move(summary));
    }

    std::sort(g_premadeCache.summaries.begin(), g_premadeCache.summaries.end(),
        [](const PremadeConfigSummary& a, const PremadeConfigSummary& b){
            return a.name < b.name;
        });
    g_premadeCache.dirty = false;
}

static void ensurePremadeCacheLocked(){
    if(g_premadeCache.dirty)
        rebuildPremadeCacheLocked();
}

} // namespace

const std::string& premadeConfigLocation(){
    return g_premadeLocation;
}

void setPremadeConfigLocation(const std::string& path){
    std::lock_guard<std::mutex> lock(g_premadeMutex);
    const std::string normalised = normalisePathString(path);
    if(normalised == g_premadeLocation)
        return;
    g_premadeLocation = normalised;
    g_premadeCache.dirty = true;
}

void refreshPremadeConfigSummaries(){
    std::lock_guard<std::mutex> lock(g_premadeMutex);
    g_premadeCache.dirty = true;
}

const std::vector<PremadeConfigSummary>& premadeConfigSummaries(){
    std::lock_guard<std::mutex> lock(g_premadeMutex);
    ensurePremadeCacheLocked();
    return g_premadeCache.summaries;
}

bool exportPremadeConfig(
    const PremadeConfigExportOptions& options,
    PremadeConfigExportResult& result,
    std::function<void(const std::string&)> log,
    const std::atomic<bool>* cancelFlag)
{
    result = {};

    if(options.templateName.empty())
        throw std::runtime_error("No premade config template selected.");
    if(options.outputDir.empty())
        throw std::runtime_error("No output folder selected.");

    std::vector<PremadeSourceFile> selectedFiles;
    {
        std::lock_guard<std::mutex> lock(g_premadeMutex);
        ensurePremadeCacheLocked();
        auto it = g_premadeCache.filesByTemplate.find(options.templateName);
        if(it != g_premadeCache.filesByTemplate.end())
            selectedFiles = it->second;
    }

    if(selectedFiles.empty())
        throw std::runtime_error("The selected premade config has no files in the configured premade folder.");

    result.outputDir = makeOutputRoot(options);
    std::filesystem::create_directories(result.outputDir);

    for(const PremadeSourceFile& file : selectedFiles){
        if(cancelFlag && cancelFlag->load()){
            result.cancelled = true;
            if(log) log("Export cancelled.");
            return true;
        }

        std::vector<unsigned char> bytes;
        if(!loadFileBytes(file.absolutePath, bytes))
            throw std::runtime_error("Failed to load premade file: " + file.absolutePath);

        const std::filesystem::path outPath = std::filesystem::path(result.outputDir) / file.relativePath;
        std::filesystem::create_directories(outPath.parent_path());

        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if(!out) throw std::runtime_error("Failed to write file: " + outPath.string());
        out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
        if(!out) throw std::runtime_error("Failed to write file: " + outPath.string());

        result.filesWritten++;
        if(log) log("Wrote " + file.relativePath);
    }

    if(log){
        std::ostringstream done;
        done << "Finished exporting " << result.filesWritten << " files to " << result.outputDir;
        log(done.str());
    }
    return true;
}

} // namespace apb
