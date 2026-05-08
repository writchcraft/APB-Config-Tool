#pragma once
#include "StatCalc.h"
#include "Colors.h"
#include <string>

namespace apb {
enum class Scheme { CLEAR, WRITCH, SPELLBOUND, GRADIENT, SINGLE, TRIPLE };
inline Scheme presetSchemeByIndex(int presetIdx){
    return (presetIdx == 1) ? Scheme::SPELLBOUND : Scheme::WRITCH;
}
std::string formatFromInfra(const std::string& extracted,const Stats& s,
    Scheme scheme=Scheme::CLEAR,RGB single={},RGB gradStart={},RGB gradEnd={});
} // namespace apb
