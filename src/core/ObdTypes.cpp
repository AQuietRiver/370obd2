#include "core/ObdTypes.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

namespace obd {

std::string byteToHex(uint32_t value, int width) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << value;
    return out.str();
}

std::string ObdCommand::toElmCommand() const {
    // Modes 03 (stored DTCs), 04 (clear DTCs), 07 (pending DTCs), and 0A (permanent DTCs)
    // take no PID parameter.
    static const std::array<uint8_t, 4> noParamModes = {0x03, 0x04, 0x07, 0x0A};
    if (std::find(noParamModes.begin(), noParamModes.end(), mode) != noParamModes.end()) {
        return byteToHex(mode);
    }
    return byteToHex(mode) + byteToHex(pid);
}

} // namespace obd
