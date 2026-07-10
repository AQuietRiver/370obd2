#include "core/PidRegistry.hpp"

namespace obd {

const std::map<uint8_t, StandardPidDefinition>& PidRegistry::standardMode01() {
    static const std::map<uint8_t, StandardPidDefinition> pids = {
        {0x04, {0x04, "Calculated engine load", "%"}},
        {0x05, {0x05, "Engine coolant temperature", "C"}},
        {0x06, {0x06, "Short term fuel trim bank 1", "%"}},
        {0x07, {0x07, "Long term fuel trim bank 1", "%"}},
        {0x0B, {0x0B, "Intake manifold absolute pressure", "kPa"}},
        {0x0C, {0x0C, "Engine speed", "rpm"}},
        {0x0D, {0x0D, "Vehicle speed", "km/h"}},
        {0x0E, {0x0E, "Timing advance", "deg"}},
        {0x0F, {0x0F, "Intake air temperature", "C"}},
        {0x10, {0x10, "Mass air flow rate", "g/s"}},
        {0x11, {0x11, "Throttle position", "%"}},
        {0x1F, {0x1F, "Run time since engine start", "s"}},
        {0x21, {0x21, "Distance with MIL on", "km"}},
        {0x2F, {0x2F, "Fuel tank level input", "%"}},
        {0x33, {0x33, "Absolute barometric pressure", "kPa"}},
        {0x42, {0x42, "Control module voltage", "V"}},
        {0x46, {0x46, "Ambient air temperature", "C"}},
        {0x5C, {0x5C, "Engine oil temperature", "C"}},
        {0x5E, {0x5E, "Engine fuel rate", "L/h"}},
    };
    return pids;
}

std::string PidRegistry::nameFor(uint8_t pid) {
    const auto& pids = standardMode01();
    const auto found = pids.find(pid);
    return found == pids.end() ? "Standard PID 0x" + byteToHex(pid) : found->second.name;
}

std::string PidRegistry::unitFor(uint8_t pid) {
    const auto& pids = standardMode01();
    const auto found = pids.find(pid);
    return found == pids.end() ? "" : found->second.unit;
}

} // namespace obd
