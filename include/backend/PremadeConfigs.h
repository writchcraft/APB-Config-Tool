#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace apb {

struct PremadeConfigEditableValue {
    std::string kind;
    std::string value;
    std::string replacementHint;
    int occurrences = 0;
    int fileCount = 0;
};

struct PremadeConfigEditableGradient {
    std::string kind;
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
