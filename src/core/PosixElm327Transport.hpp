#pragma once

#include "core/ObdTransport.hpp"

#include <string>
#include <termios.h>

namespace obd {

class PosixElm327Transport : public ObdTransport {
public:
    explicit PosixElm327Transport(std::string devicePath, int baudRate = 38400);
    ~PosixElm327Transport() override;

    bool connect(std::string* error) override;
    void disconnect() override;
    ObdResponse send(const ObdCommand& command) override;

private:
    bool writeLine(const std::string& line, std::string* error);
    std::string readUntilPrompt(std::string* error);
    bool configurePort(std::string* error);
    speed_t baudFlag() const;

    std::string devicePath_;
    int baudRate_ = 38400;
    int fd_ = -1;
    std::string activeModule_;
};

} // namespace obd
