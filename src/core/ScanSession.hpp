#pragma once

#include "core/EnhancedPidProfile.hpp"
#include "core/ObdTransport.hpp"

namespace obd {

struct ScanOptions {
    std::vector<std::string> modules = {"7E0"};
    bool includeStandardPids = true;
    bool includeEnhancedPids = true;
};

class ScanSession {
public:
    ScanSession(ObdTransport& transport, const EnhancedPidProfile* enhancedProfile);
    CoverageReport run(const ScanOptions& options);
    DtcReport readDtcs(const std::vector<std::string>& modules);
    bool clearDtcs(const std::vector<std::string>& modules, std::string* error);

private:
    std::vector<uint8_t> discoverStandardPids(const std::string& module);

    ObdTransport& transport_;
    const EnhancedPidProfile* enhancedProfile_;
};

} // namespace obd
