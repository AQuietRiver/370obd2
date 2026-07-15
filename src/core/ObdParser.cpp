#include "core/ObdParser.hpp"

#include "core/PidRegistry.hpp"

#include <cctype>

namespace obd {

namespace {

char hexDigit(uint8_t nibble) {
    return nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('A' + (nibble - 10));
}

int hexValue(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

// Standard 2-byte DTC encoding: byte1 bits[7:6] select the letter, bits[5:4]
// are the first digit (0-3), bits[3:0] are the second digit; byte2 holds the
// third and fourth digits as two hex nibbles.
std::string decodeDtcCode(uint8_t high, uint8_t low) {
    static const char letters[4] = {'P', 'C', 'B', 'U'};
    std::string code;
    code += letters[(high >> 6) & 0x03];
    code += hexDigit((high >> 4) & 0x03);
    code += hexDigit(high & 0x0F);
    code += hexDigit((low >> 4) & 0x0F);
    code += hexDigit(low & 0x0F);
    return code;
}

bool isHexDataLine(const std::string& line) {
    bool sawHexDigit = false;
    for (char ch : line) {
        if (ch == ' ' || ch == '\t') {
            continue;
        }
        if (hexValue(ch) < 0) {
            return false;
        }
        sawHexDigit = true;
    }
    return sawHexDigit;
}

} // namespace

// The ELM327 interleaves protocol-status chatter (e.g. "SEARCHING...",
// "BUS INIT: OK") with real data lines, most often while auto-detecting the
// protocol on the first query after connecting. Several of those words
// contain letters (A, C, E) that are also valid hex digits, so filtering at
// the character level (as parseHexBytes does) would silently misalign the
// decoded bytes. Keep only whole lines that are pure hex/whitespace before
// handing the response to the byte parser.
std::string ObdParser::stripAdapterChatter(const std::string& raw) {
    std::string result;
    std::string line;
    auto flushLine = [&]() {
        if (isHexDataLine(line)) {
            if (!result.empty()) {
                result += ' ';
            }
            result += line;
        }
        line.clear();
    };
    for (char ch : raw) {
        if (ch == '\r' || ch == '\n') {
            flushLine();
        } else if (ch != '>') {
            line += ch;
        }
    }
    flushLine();
    return result;
}

std::vector<uint8_t> ObdParser::parseHexBytes(const std::string& raw) {
    std::vector<uint8_t> bytes;
    bytes.reserve(raw.size() / 3);
    int highNibble = -1;
    for (char ch : raw) {
        const int nibble = hexValue(ch);
        if (nibble < 0) {
            continue;
        }
        if (highNibble < 0) {
            highNibble = nibble;
        } else {
            bytes.push_back(static_cast<uint8_t>((highNibble << 4) | nibble));
            highNibble = -1;
        }
    }
    return bytes;
}

std::vector<uint8_t> ObdParser::decodeSupportedPids(uint8_t basePid, const std::vector<uint8_t>& responseBytes) {
    std::vector<uint8_t> supported;
    if (responseBytes.size() < 6 || responseBytes[0] != 0x41 || responseBytes[1] != basePid) {
        return supported;
    }

    for (int byteIndex = 0; byteIndex < 4; ++byteIndex) {
        const uint8_t mask = responseBytes[2 + byteIndex];
        for (int bit = 0; bit < 8; ++bit) {
            if ((mask & (1u << (7 - bit))) != 0) {
                supported.push_back(static_cast<uint8_t>(basePid + byteIndex * 8 + bit + 1));
            }
        }
    }
    return supported;
}

SensorValue ObdParser::decodeMode01(const ObdResponse& response) {
    SensorValue value;
    value.module = response.module;
    value.mode = response.command.mode;
    value.pid = response.command.pid;
    value.name = PidRegistry::nameFor(static_cast<uint8_t>(response.command.pid));
    value.unit = PidRegistry::unitFor(static_cast<uint8_t>(response.command.pid));
    value.raw = response.raw;

    if (!response.ok) {
        value.status = response.error.empty() ? "unavailable" : response.error;
        return value;
    }

    const auto& b = response.bytes;
    if (b.size() < 3 || b[0] != 0x41 || b[1] != response.command.pid) {
        value.status = "raw-only";
        return value;
    }

    const uint8_t A = b.size() > 2 ? b[2] : 0;
    const uint8_t B = b.size() > 3 ? b[3] : 0;
    switch (response.command.pid) {
    case 0x04:
    case 0x11:
    case 0x2F:
        value.numericValue = A * 100.0 / 255.0;
        break;
    case 0x05:
    case 0x0F:
    case 0x46:
    case 0x5C:
        value.numericValue = A - 40.0;
        break;
    case 0x06:
    case 0x07:
        value.numericValue = A * 100.0 / 128.0 - 100.0;
        break;
    case 0x0B:
    case 0x0D:
    case 0x33:
        value.numericValue = A;
        break;
    case 0x0C:
        value.numericValue = ((A * 256.0) + B) / 4.0;
        break;
    case 0x0E:
        value.numericValue = A / 2.0 - 64.0;
        break;
    case 0x10:
        value.numericValue = ((A * 256.0) + B) / 100.0;
        break;
    case 0x1F:
    case 0x21:
        value.numericValue = A * 256.0 + B;
        break;
    case 0x42:
        value.numericValue = (A * 256.0 + B) / 1000.0;
        break;
    case 0x5E:
        value.numericValue = (A * 256.0 + B) / 20.0;
        break;
    default:
        value.status = "raw-only";
        return value;
    }

    value.decoded = true;
    value.status = "decoded";
    return value;
}

std::vector<DiagnosticTroubleCode> ObdParser::decodeDtcResponse(const ObdResponse& response, const std::string& status) {
    std::vector<DiagnosticTroubleCode> codes;
    if (!response.ok || response.bytes.size() < 3) {
        return codes;
    }

    // bytes[0] is the mode echo (0x43/0x47/0x4A); DTCs follow as 2-byte pairs.
    for (size_t i = 1; i + 1 < response.bytes.size(); i += 2) {
        const uint8_t high = response.bytes[i];
        const uint8_t low = response.bytes[i + 1];
        if (high == 0 && low == 0) {
            continue; // empty slot / padding
        }
        DiagnosticTroubleCode dtc;
        dtc.module = response.module;
        dtc.status = status;
        dtc.code = decodeDtcCode(high, low);
        dtc.raw = response.raw;
        codes.push_back(dtc);
    }
    return codes;
}

} // namespace obd
