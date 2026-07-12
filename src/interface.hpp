#pragma once

#include "module.hpp"

#include <string>

class InterfaceCodec {
public:
    static std::string serialize(const ModuleInterface& interface,
                                 const std::string& fingerprint);
};
