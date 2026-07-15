#include "core/EnhancedPidProfile.hpp"
#include "core/ObdParser.hpp"
#include "core/ScanSession.hpp"
#include "core/SimulatorTransport.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>

static bool near(double actual, double expected) {
    return std::fabs(actual - expected) < 0.001;
}

int main() {
    {
        const auto bytes = obd::ObdParser::parseHexBytes("41 0C 1F 40");
        assert(bytes.size() == 4);
        assert(bytes[0] == 0x41);
        assert(bytes[3] == 0x40);
    }

    {
        assert(obd::ObdParser::parseHexBytes("").empty());
        assert(obd::ObdParser::parseHexBytes("not hex").empty());
        const auto odd = obd::ObdParser::parseHexBytes("410C1");
        assert(odd.size() == 2);
        assert(odd[0] == 0x41);
        assert(odd[1] == 0x0C);
    }

    {
        const auto path = std::filesystem::temp_directory_path() / "370obd2-bad-profile.csv";
        {
            std::ofstream output(path);
            output << "module,mode,pid,name,unit,scale,offset,valueBytes,description\n"
                   << "7E0,22,1234,Bad PID,V,not-a-number,0,2,test\n";
        }
        obd::EnhancedPidProfile profile;
        std::string error;
        assert(!profile.loadCsv(path.string(), &error));
        assert(error.find("line 2") != std::string::npos);
        assert(profile.definitions().empty());
        std::filesystem::remove(path);
    }

    {
        const std::vector<uint8_t> response = {0x41, 0x00, 0x18, 0x00, 0x00, 0x00};
        const auto pids = obd::ObdParser::decodeSupportedPids(0x00, response);
        assert(pids.size() == 2);
        assert(pids[0] == 0x04);
        assert(pids[1] == 0x05);
    }

    {
        obd::ObdResponse response;
        response.module = "7E0";
        response.command = obd::ObdCommand{"7E0", 0x01, 0x0C};
        response.bytes = {0x41, 0x0C, 0x1F, 0x40};
        response.raw = "41 0C 1F 40";
        const auto value = obd::ObdParser::decodeMode01(response);
        assert(value.decoded);
        assert(value.numericValue);
        assert(near(*value.numericValue, 2000.0));
    }

    {
        obd::SimulatorTransport transport;
        std::string error;
        assert(transport.connect(&error));
        obd::EnhancedPidProfile profile;
        assert(profile.loadCsv("data/sample_nissan_z34_enhanced_pids.csv", &error));

        obd::ScanSession session(transport, &profile);
        const auto report = session.run(obd::ScanOptions{});
        assert(!report.supportedStandardPids.empty());
        assert(!report.decodedSensors.empty());
        assert(report.decodedSensors.size() >= 5);
    }

    {
        obd::ObdResponse response;
        response.module = "7E0";
        response.command = obd::ObdCommand{"7E0", 0x03, 0};
        response.bytes = {0x43, 0x01, 0x33, 0x01, 0x71};
        response.raw = "43 01 33 01 71";
        const auto codes = obd::ObdParser::decodeDtcResponse(response, "stored");
        assert(codes.size() == 2);
        assert(codes[0].code == "P0133");
        assert(codes[1].code == "P0171");
        assert(codes[0].status == "stored");
    }

    {
        assert(obd::ObdCommand({"7E0", 0x03, 0}).toElmCommand() == "03");
        assert(obd::ObdCommand({"7E0", 0x07, 0}).toElmCommand() == "07");
        assert(obd::ObdCommand({"7E0", 0x01, 0x0C}).toElmCommand() == "010C");
    }

    {
        obd::SimulatorTransport transport;
        std::string error;
        assert(transport.connect(&error));

        obd::ScanSession session(transport, nullptr);
        const auto dtcReport = session.readDtcs({"7E0"});
        assert(dtcReport.stored.size() == 2);
        assert(dtcReport.pending.size() == 1);
        assert(dtcReport.pending[0].code == "P0300");
        assert(dtcReport.permanent.size() == 1);
        assert(dtcReport.permanent[0].code == "P0420");
        assert(dtcReport.permanent[0].status == "permanent");
    }

    {
        obd::SimulatorTransport transport;
        std::string error;
        assert(transport.connect(&error));

        obd::ScanSession session(transport, nullptr);
        assert(session.clearDtcs({"7E0"}, &error));

        const auto dtcReport = session.readDtcs({"7E0"});
        assert(dtcReport.stored.empty());
        assert(dtcReport.pending.empty());
        // Permanent codes require real drive-cycle monitors and are not
        // removed by a mode 04 clear.
        assert(dtcReport.permanent.size() == 1);
    }

    return 0;
}
