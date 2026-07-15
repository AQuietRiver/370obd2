#pragma once

#include "core/ObdTypes.hpp"

#include <string>

namespace obd {

class ObdParser {
public:
    static std::vector<uint8_t> parseHexBytes(const std::string& raw);
    static std::string stripAdapterChatter(const std::string& raw);
    static std::vector<uint8_t> decodeSupportedPids(uint8_t basePid, const std::vector<uint8_t>& responseBytes);
    static SensorValue decodeMode01(const ObdResponse& response);
    static std::vector<DiagnosticTroubleCode> decodeDtcResponse(const ObdResponse& response, const std::string& status);
};

} // namespace obd
