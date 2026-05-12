#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace apb {

struct PremadeConfigEditableValue {
    std::string kind;
    std::string value;
    std::string replacementHint;
    int occurrences = 0;
    int fileCount = 0;
    std::vector<std::string> samples;
};

enum class GradientType : uint8_t { Smooth, Triple, Stepped };

struct PremadeConfigEditableGradient {
    GradientType type = GradientType::Smooth;
    std::string kind;
    std::string sequence;   // pipe-separated normalised step values (first|...|last)
    std::string midValue;   // normalised mid-step colour for Triple gradients
    std::string preview;
    std::string sampleText;
    std::string replacementHint;
    int steps = 0;
    int uniqueSteps = 0;
    int occurrences = 0;
    int fileCount = 0;
};

struct PremadeConfigEditableFile {
    std::string relativePath;
    int namedColourTags = 0;
    int rgbColourTags = 0;
    int gradientRuns = 0;
};

struct PremadeConfigSummary {
    std::string name;
    int fileCount = 0;
    int recolourableFiles = 0;
    int colourTagCount = 0;
    int namedColourTagCount = 0;
    int rgbColourTagCount = 0;
    int gradientRunCount = 0;
    std::vector<PremadeConfigEditableValue> editableValues;
    std::vector<PremadeConfigEditableGradient> editableGradients;
    std::vector<PremadeConfigEditableFile> editableFiles;
};

struct PremadeConfigExportOptions {
    std::string templateName;
    std::string outputDir;
    // Optional colour substitutions: normalised-old-value → normalised-new-value.
    // Applied to every RGB colour tag in every exported text file.
    std::map<std::string,std::string> colourSubstitutions;
};

struct PremadeConfigExportResult {
    std::string outputDir;
    int filesWritten = 0;
    bool cancelled = false;
};

const std::string& premadeConfigLocation();
void setPremadeConfigLocation(const std::string& path);
void refreshPremadeConfigSummaries();
std::vector<PremadeConfigSummary> premadeConfigSummaries();
bool isPremadeCacheBuilding();
void premadeCacheBuildProgress(int& done, int& total);

bool exportPremadeConfig(
    const PremadeConfigExportOptions& options,
    PremadeConfigExportResult& result,
    std::function<void(const std::string&)> log = nullptr,
    const std::atomic<bool>* cancelFlag = nullptr);

} // namespace apb
