#pragma once

#include "module.hpp"

#include <string>
#include <stdexcept>
#include <vector>

class InterfaceError : public std::runtime_error {
public:
    InterfaceError(std::string code, std::string detail)
        : std::runtime_error("[" + code + "] " + detail),
          code_(std::move(code)), detail_(std::move(detail)) {}

    const std::string& code() const noexcept { return code_; }
    const std::string& detail() const noexcept { return detail_; }

private:
    std::string code_;
    std::string detail_;
};

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
