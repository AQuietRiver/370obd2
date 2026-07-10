#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace obd {

struct ObdCommand {
    std::string module;
    uint8_t mode = 0;
    uint16_t pid = 0;

    std::string toElmCommand() const;
};

struct ObdResponse {
    std::string module;
    ObdCommand command;
    std::vector<uint8_t> bytes;
    std::string raw;
    bool ok = true;
    std::string error;
};

struct SensorValue {
    std::string module;
    uint8_t mode = 0;
    uint16_t pid = 0;
    std::string name;
    std::string unit;
    std::optional<double> numericValue;
    std::string textValue;
    std::string raw;
    bool decoded = false;
    std::string status;
};

struct CoverageReport {
    std::vector<uint8_t> supportedStandardPids;
    std::vector<std::string> modules;
    std::vector<SensorValue> decodedSensors;
    std::vector<SensorValue> rawOnlySensors;
    std::vector<SensorValue> unavailableSensors;
};

struct DiagnosticTroubleCode {
    std::string module;
    std::string code;   // e.g. "P0301"
    std::string status; // "pending" (mode 07), "stored" (mode 03), or "permanent" (mode 0A)
    std::string raw;
};

struct DtcReport {
    std::vector<DiagnosticTroubleCode> pending;   // mode 07: detected this/last drive cycle, not yet confirmed
    std::vector<DiagnosticTroubleCode> stored;    // mode 03: confirmed, logged in ECU memory (MIL-on)
    std::vector<DiagnosticTroubleCode> permanent; // mode 0A: can't be cleared by a scan tool
};

std::string byteToHex(uint32_t value, int width = 2);

} // namespace obd
