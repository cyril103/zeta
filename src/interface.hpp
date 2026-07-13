#pragma once

#include "module.hpp"

#include <string>
#include <vector>

struct PersistedInterface {
    ModuleInterface interface;
    std::string fingerprint;
    std::vector<std::string> imports;
    std::vector<Token> genericTokens;
};

class InterfaceCodec {
public:
    static std::string serialize(const ModuleInterface& interface,
                                 const std::string& fingerprint,
                                 const std::vector<std::string>& imports = {},
                                 const std::vector<Token>& genericTokens = {});
    static PersistedInterface deserialize(const std::string& contents);
};
