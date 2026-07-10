#include "core/SimulatorTransport.hpp"

namespace obd {

static std::string keyFor(const ObdCommand& command) {
    return command.module + ":" + command.toElmCommand();
}

bool SimulatorTransport::connect(std::string* error) {
    (void)error;
    connected_ = true;
    responses_.clear();

    responses_["7E0:0100"] = {0x41, 0x00, 0x18, 0x1B, 0x80, 0x13};
    responses_["7E0:0120"] = {0x41, 0x20, 0x80, 0x01, 0x80, 0x01};
    responses_["7E0:0140"] = {0x41, 0x40, 0x40, 0x00, 0x00, 0x00};

    responses_["7E0:0104"] = {0x41, 0x04, 0x52};
    responses_["7E0:0105"] = {0x41, 0x05, 0x5F};
    responses_["7E0:010C"] = {0x41, 0x0C, 0x1F, 0x40};
    responses_["7E0:010D"] = {0x41, 0x0D, 0x00};
    responses_["7E0:010F"] = {0x41, 0x0F, 0x50};
    responses_["7E0:0110"] = {0x41, 0x10, 0x03, 0x20};
    responses_["7E0:0111"] = {0x41, 0x11, 0x2E};
    responses_["7E0:0121"] = {0x41, 0x21, 0x00, 0x00};
    responses_["7E0:012F"] = {0x41, 0x2F, 0x99};
    responses_["7E0:0142"] = {0x41, 0x42, 0x36, 0xB0};
    responses_["7E0:0146"] = {0x41, 0x46, 0x4D};

    responses_["7E0:221001"] = {0x62, 0x10, 0x01, 0x00, 0xA5};

    // Stored (logged) DTCs: P0133, P0171.
    responses_["7E0:03"] = {0x43, 0x01, 0x33, 0x01, 0x71};
    // Pending (active, unconfirmed) DTCs: P0300.
    responses_["7E0:07"] = {0x47, 0x03, 0x00};
    // Permanent DTCs: P0420. Not removed by a mode 04 clear.
    responses_["7E0:0A"] = {0x4A, 0x04, 0x20};
    return true;
}

void SimulatorTransport::disconnect() {
    connected_ = false;
}

ObdResponse SimulatorTransport::send(const ObdCommand& command) {
    ObdResponse response;
    response.module = command.module;
    response.command = command;

    if (!connected_) {
        response.ok = false;
        response.error = "not connected";
        return response;
    }

    if (command.mode == 0x04) {
        // Clearing DTCs resets stored/pending codes and the MIL, but permanent
        // (mode 0A) codes require real drive-cycle monitors to clear.
        responses_.erase(command.module + ":03");
        responses_.erase(command.module + ":07");
        response.bytes = {0x44};
        response.raw = "44";
        return response;
    }

    const auto found = responses_.find(keyFor(command));
    if (found == responses_.end()) {
        response.ok = false;
        response.error = "no response";
        response.raw = "NO DATA";
        return response;
    }

    response.bytes = found->second;
    for (uint8_t byte : response.bytes) {
        if (!response.raw.empty()) {
            response.raw += ' ';
        }
        response.raw += byteToHex(byte);
    }
    return response;
}

} // namespace obd
