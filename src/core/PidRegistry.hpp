#pragma once

#include "core/ObdTypes.hpp"

#include <map>

namespace obd {

struct StandardPidDefinition {
    uint8_t pid = 0;
    std::string name;
    std::string unit;
};

class PidRegistry {
public:
    static const std::map<uint8_t, StandardPidDefinition>& standardMode01();
    static std::string nameFor(uint8_t pid);
    static std::string unitFor(uint8_t pid);
};

} // namespace obd
