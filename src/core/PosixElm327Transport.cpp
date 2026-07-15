#include "core/PosixElm327Transport.hpp"

#include "core/ObdParser.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace obd {

PosixElm327Transport::PosixElm327Transport(std::string devicePath, int baudRate)
    : devicePath_(std::move(devicePath)), baudRate_(baudRate) {}

PosixElm327Transport::~PosixElm327Transport() {
    disconnect();
}

speed_t PosixElm327Transport::baudFlag() const {
    switch (baudRate_) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return B38400;
    }
}

bool PosixElm327Transport::configurePort(std::string* error) {
    termios tty {};
    if (tcgetattr(fd_, &tty) != 0) {
        if (error) {
            *error = std::strerror(errno);
        }
        return false;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, baudFlag());
    cfsetospeed(&tty, baudFlag());
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    // With VMIN=0, VTIME=10 returns from an idle read after one second. The
    // absolute deadline in readUntilPrompt also handles continuous junk data.
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        if (error) {
            *error = std::strerror(errno);
        }
        return false;
    }
    return true;
}

bool PosixElm327Transport::connect(std::string* error) {
    fd_ = open(devicePath_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0) {
        if (error) {
            *error = "could not open " + devicePath_ + ": " + std::strerror(errno);
        }
        return false;
    }

    if (!configurePort(error)) {
        disconnect();
        return false;
    }

    for (const char* init : {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"}) {
        if (!writeLine(init, error)) {
            disconnect();
            return false;
        }
        readUntilPrompt(error);
        if (error && !error->empty()) {
            disconnect();
            return false;
        }
    }
    return true;
}

void PosixElm327Transport::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool PosixElm327Transport::writeLine(const std::string& line, std::string* error) {
    const std::string command = line + "\r";
    const ssize_t written = write(fd_, command.data(), command.size());
    if (written != static_cast<ssize_t>(command.size())) {
        if (error) {
            *error = std::strerror(errno);
        }
        return false;
    }
    return true;
}

std::string PosixElm327Transport::readUntilPrompt(std::string* error) {
    constexpr auto totalTimeout = std::chrono::seconds(5);
    const auto deadline = std::chrono::steady_clock::now() + totalTimeout;
    std::string raw;
    char buffer[128];
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) {
            if (error) {
                *error = "timed out waiting for ELM327 prompt";
            }
            return raw;
        }
        const ssize_t count = read(fd_, buffer, sizeof(buffer));
        if (count < 0) {
            if (error) {
                *error = std::strerror(errno);
            }
            return raw;
        }
        if (count == 0) {
            return raw;
        }
        raw.append(buffer, buffer + count);
        if (raw.find('>') != std::string::npos) {
            return raw;
        }
    }
}

ObdResponse PosixElm327Transport::send(const ObdCommand& command) {
    ObdResponse response;
    response.module = command.module;
    response.command = command;

    if (fd_ < 0) {
        response.ok = false;
        response.error = "not connected";
        return response;
    }

    std::string error;
    if (!command.module.empty() && command.module != activeModule_) {
        if (!writeLine("ATSH" + command.module, &error)) {
            response.ok = false;
            response.error = error;
            return response;
        }
        readUntilPrompt(&error);
        activeModule_ = command.module;
    }

    if (!writeLine(command.toElmCommand(), &error)) {
        response.ok = false;
        response.error = error;
        return response;
    }

    response.raw = readUntilPrompt(&error);
    if (!error.empty()) {
        response.ok = false;
        response.error = error;
        return response;
    }
    if (response.raw.find("NO DATA") != std::string::npos || response.raw.find("?") != std::string::npos) {
        response.ok = false;
        response.error = "no response";
        return response;
    }

    response.bytes = ObdParser::parseHexBytes(response.raw);
    return response;
}

} // namespace obd
