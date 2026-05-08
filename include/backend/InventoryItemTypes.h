#pragma once
#include <string>

namespace apb {
struct MergeResult { std::string outputPath,reportText; };
MergeResult mergeInventoryItemTypes(
    const std::string& gerPath,const std::string& intPath,
    const std::string& outputPath={},bool appendRemaining=false);
} // namespace apb
