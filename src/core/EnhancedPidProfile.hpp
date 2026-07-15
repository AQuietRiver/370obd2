#pragma once

#include "core/ObdTypes.hpp"

#include <vector>

namespace obd {

struct EnhancedPidDefinition {
    std::string module;
    uint8_t mode = 0x22;
    uint16_t pid = 0;
    std::string name;
    std::string unit;
    double scale = 1.0;
    double offset = 0.0;
    size_t valueBytes = 1;
    std::string description;
};

class EnhancedPidProfile {
public:
    [[nodiscard]] bool loadCsv(const std::string& path, std::string* error);
    const std::vector<EnhancedPidDefinition>& definitions() const { return definitions_; }
    SensorValue decode(const EnhancedPidDefinition& definition, const ObdResponse& response) const;

private:
    std::vector<EnhancedPidDefinition> definitions_;
};

} // namespace obd
