#pragma once

#include "core/ObdTypes.hpp"

namespace obd {

class ObdTransport {
public:
    virtual ~ObdTransport() = default;
    virtual bool connect(std::string* error) = 0;
    virtual void disconnect() = 0;
    virtual ObdResponse send(const ObdCommand& command) = 0;
};

} // namespace obd
