#include "core/ScanSession.hpp"

#include "core/ObdParser.hpp"

#include <algorithm>

namespace obd {

ScanSession::ScanSession(ObdTransport& transport, const EnhancedPidProfile* enhancedProfile)
    : transport_(transport), enhancedProfile_(enhancedProfile) {}

std::vector<uint8_t> ScanSession::discoverStandardPids(const std::string& module) {
    std::vector<uint8_t> supported;
    for (uint8_t base : {0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0}) {
        ObdCommand command{module, 0x01, base};
        ObdResponse response = transport_.send(command);
        const auto block = ObdParser::decodeSupportedPids(base, response.bytes);
        supported.insert(supported.end(), block.begin(), block.end());
        if (std::find(block.begin(), block.end(), static_cast<uint8_t>(base + 0x20)) == block.end()) {
            break;
        }
    }
    return supported;
}

CoverageReport ScanSession::run(const ScanOptions& options) {
    CoverageReport report;
    report.modules = options.modules;

    for (const auto& module : options.modules) {
        if (options.includeStandardPids) {
            const auto pids = discoverStandardPids(module);
            report.supportedStandardPids.insert(report.supportedStandardPids.end(), pids.begin(), pids.end());

            for (uint8_t pid : pids) {
                if (pid == 0x00 || pid == 0x20 || pid == 0x40 || pid == 0x60 || pid == 0x80 || pid == 0xA0 || pid == 0xC0) {
                    continue;
                }
                ObdResponse response = transport_.send(ObdCommand{module, 0x01, pid});
                SensorValue value = ObdParser::decodeMode01(response);
                if (!response.ok) {
                    report.unavailableSensors.push_back(value);
                } else if (value.decoded) {
                    report.decodedSensors.push_back(value);
                } else {
                    report.rawOnlySensors.push_back(value);
                }
            }
        }

        if (options.includeEnhancedPids && enhancedProfile_ != nullptr) {
            for (const auto& def : enhancedProfile_->definitions()) {
                if (!def.module.empty() && def.module != module) {
                    continue;
                }
                ObdResponse response = transport_.send(ObdCommand{module, def.mode, def.pid});
                SensorValue value = enhancedProfile_->decode(def, response);
                if (!response.ok) {
                    report.unavailableSensors.push_back(value);
                } else if (value.decoded) {
                    report.decodedSensors.push_back(value);
                } else {
                    report.rawOnlySensors.push_back(value);
                }
            }
        }
    }

    std::sort(report.supportedStandardPids.begin(), report.supportedStandardPids.end());
    report.supportedStandardPids.erase(
        std::unique(report.supportedStandardPids.begin(), report.supportedStandardPids.end()),
        report.supportedStandardPids.end());
    return report;
}

DtcReport ScanSession::readDtcs(const std::vector<std::string>& modules) {
    DtcReport report;
    for (const auto& module : modules) {
        ObdResponse pendingResponse = transport_.send(ObdCommand{module, 0x07, 0});
        const auto pending = ObdParser::decodeDtcResponse(pendingResponse, "pending");
        report.pending.insert(report.pending.end(), pending.begin(), pending.end());

        ObdResponse storedResponse = transport_.send(ObdCommand{module, 0x03, 0});
        const auto stored = ObdParser::decodeDtcResponse(storedResponse, "stored");
        report.stored.insert(report.stored.end(), stored.begin(), stored.end());

        ObdResponse permanentResponse = transport_.send(ObdCommand{module, 0x0A, 0});
        const auto permanent = ObdParser::decodeDtcResponse(permanentResponse, "permanent");
        report.permanent.insert(report.permanent.end(), permanent.begin(), permanent.end());
    }
    return report;
}

bool ScanSession::clearDtcs(const std::vector<std::string>& modules, std::string* error) {
    for (const auto& module : modules) {
        ObdResponse response = transport_.send(ObdCommand{module, 0x04, 0});
        if (!response.ok) {
            if (error) {
                *error = "clear DTCs failed on module " + module + ": " + response.error;
            }
            return false;
        }
    }
    return true;
}

} // namespace obd
