#pragma once

#include "core/ObdTransport.hpp"

#include <map>

namespace obd {

class SimulatorTransport : public ObdTransport {
public:
    bool connect(std::string* error) override;
    void disconnect() override;
    ObdResponse send(const ObdCommand& command) override;

private:
    bool connected_ = false;
    std::map<std::string, std::vector<uint8_t>> responses_;
};

} // namespace obd
