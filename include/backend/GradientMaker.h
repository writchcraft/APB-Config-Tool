#pragma once
#include "Colors.h"
#include <vector>
#include <string>

namespace apb {
std::vector<RGB> writchGradient6();
std::vector<RGB> spellboundGradient6();
RGB writchSmoothStart(); RGB witchSmoothEnd();
RGB spellboundStart();   RGB spellboundEnd();
std::vector<RGB> steppedPresetByIndex(int presetIdx);
void smoothPresetByIndex(int presetIdx, RGB& start, RGB& end);
std::string hardGradientString (const std::string& text,const std::vector<RGB>& pal,bool skipSpaces=true);
std::string smoothGradientString(const std::string& text,const RGB& s,const RGB& e,bool skip=true);
std::string tripleGradientString(const std::string& text,const RGB& a,const RGB& b,const RGB& c,bool skip=true);
} // namespace apb
