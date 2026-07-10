#include "core/EnhancedPidProfile.hpp"

#include <fstream>
#include <sstream>

namespace obd {

static std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> cells;
    std::string cell;
    bool quoted = false;
    for (char ch : line) {
        if (ch == '"') {
            quoted = !quoted;
        } else if (ch == ',' && !quoted) {
            cells.push_back(cell);
            cell.clear();
        } else {
            cell.push_back(ch);
        }
    }
    cells.push_back(cell);
    return cells;
}

static uint16_t parseHex16(const std::string& text) {
    return static_cast<uint16_t>(std::stoul(text, nullptr, 16));
}

bool EnhancedPidProfile::loadCsv(const std::string& path, std::string* error) {
    definitions_.clear();

    std::ifstream input(path);
    if (!input) {
        if (error) {
            *error = "could not open enhanced PID profile: " + path;
        }
        return false;
    }

    std::string line;
    bool header = true;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (header) {
            header = false;
            continue;
        }

        const auto cells = splitCsvLine(line);
        if (cells.size() < 8) {
            if (error) {
                *error = "bad enhanced PID row at line " + std::to_string(lineNumber);
            }
            return false;
        }

        EnhancedPidDefinition def;
        def.module = cells[0];
        def.mode = static_cast<uint8_t>(parseHex16(cells[1]));
        def.pid = parseHex16(cells[2]);
        def.name = cells[3];
        def.unit = cells[4];
        def.scale = std::stod(cells[5]);
        def.offset = std::stod(cells[6]);
        def.valueBytes = static_cast<size_t>(std::stoul(cells[7]));
        if (cells.size() > 8) {
            def.description = cells[8];
        }
        definitions_.push_back(def);
    }

    return true;
}

SensorValue EnhancedPidProfile::decode(const EnhancedPidDefinition& definition, const ObdResponse& response) const {
    SensorValue value;
    value.module = response.module;
    value.mode = definition.mode;
    value.pid = definition.pid;
    value.name = definition.name;
    value.unit = definition.unit;
    value.raw = response.raw;

    if (!response.ok) {
        value.status = response.error.empty() ? "unavailable" : response.error;
        return value;
    }

    if (response.bytes.size() < 3 + definition.valueBytes) {
        value.status = "raw-only";
        return value;
    }

    uint32_t rawNumber = 0;
    const size_t start = response.bytes.size() - definition.valueBytes;
    for (size_t i = start; i < response.bytes.size(); ++i) {
        rawNumber = (rawNumber << 8) | response.bytes[i];
    }

    value.numericValue = rawNumber * definition.scale + definition.offset;
    value.decoded = true;
    value.status = "decoded";
    return value;
}

} // namespace obd
