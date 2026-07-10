#include "core/EnhancedPidProfile.hpp"
#include "core/PosixElm327Transport.hpp"
#include "core/ScanSession.hpp"
#include "core/SimulatorTransport.hpp"

#include <iostream>

static void printDtcs(const char* title, const std::vector<obd::DiagnosticTroubleCode>& codes) {
    std::cout << "\n" << title << "\n";
    if (codes.empty()) {
        std::cout << "  none\n";
        return;
    }
    for (const auto& dtc : codes) {
        std::cout << "  " << dtc.module << " " << dtc.code << " (" << dtc.status << ")\n";
    }
}

static void printValues(const char* title, const std::vector<obd::SensorValue>& values) {
    std::cout << "\n" << title << "\n";
    for (const auto& value : values) {
        std::cout << "  " << value.module << " "
                  << obd::byteToHex(value.mode) << obd::byteToHex(value.pid, value.pid > 0xFF ? 4 : 2)
                  << " " << value.name << ": ";
        if (value.numericValue) {
            std::cout << *value.numericValue << " " << value.unit;
        } else {
            std::cout << value.status << " [" << value.raw << "]";
        }
        std::cout << "\n";
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> args;
    bool clearDtcsRequested = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--clear-dtcs") {
            clearDtcsRequested = true;
        } else {
            args.push_back(argv[i]);
        }
    }

    std::string profilePath = !args.empty() ? args[0] : "data/sample_nissan_z34_enhanced_pids.csv";
    std::string serialPath;
    if (args.size() > 1 && args[0] == "--serial") {
        serialPath = args[1];
        if (args.size() > 2) {
            profilePath = args[2];
        }
    }

    obd::EnhancedPidProfile profile;
    std::string error;
    if (!profile.loadCsv(profilePath, &error)) {
        std::cerr << "Enhanced profile not loaded: " << error << "\n";
    }

    obd::SimulatorTransport simulator;
    obd::PosixElm327Transport serial(serialPath);
    obd::ObdTransport& transport = serialPath.empty()
        ? static_cast<obd::ObdTransport&>(simulator)
        : static_cast<obd::ObdTransport&>(serial);

    if (!transport.connect(&error)) {
        std::cerr << "Connection failed: " << error << "\n";
        return 1;
    }

    obd::ScanSession session(transport, &profile);
    obd::ScanOptions options;

    if (clearDtcsRequested) {
        std::string clearError;
        if (session.clearDtcs(options.modules, &clearError)) {
            std::cout << "Cleared stored/pending DTCs.\n";
        } else {
            std::cerr << "Clear DTCs failed: " << clearError << "\n";
        }
    }

    obd::CoverageReport report = session.run(options);
    obd::DtcReport dtcReport = session.readDtcs(options.modules);

    std::cout << "370 OBD-II simulated coverage report\n";
    std::cout << "Supported standard PID count: " << report.supportedStandardPids.size() << "\n";
    std::cout << "Decoded: " << report.decodedSensors.size()
              << ", raw-only: " << report.rawOnlySensors.size()
              << ", unavailable: " << report.unavailableSensors.size() << "\n";

    printValues("Decoded sensors", report.decodedSensors);
    printValues("Raw-only sensors", report.rawOnlySensors);
    printValues("Unavailable sensors", report.unavailableSensors);
    printDtcs("Stored DTCs (logged, MIL-on)", dtcReport.stored);
    printDtcs("Pending DTCs (active, unconfirmed)", dtcReport.pending);
    printDtcs("Permanent DTCs (not clearable by scan tool)", dtcReport.permanent);
    return 0;
}
