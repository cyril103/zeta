#pragma once

#include "ast.hpp"

#include <string>
#include <unordered_map>
#include <vector>

struct SemanticSymbol {
    ValueType type;
    BindingKind kind;
    bool callable;
    const Declaration* declaration;
    bool parameter{false};
    std::vector<ValueType> parameterTypes;
};

class SymbolTable {
public:
    SymbolTable() { pushScope(); }

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }

    bool define(const std::string& name, SemanticSymbol symbol) {
        if (lookup(name) != nullptr) return false;
        scopes_.back().emplace(name, symbol);
        return true;
    }

    bool defineParameter(const std::string& name, SemanticSymbol symbol) {
        return scopes_.back().emplace(name, symbol).second;
    }

    const SemanticSymbol* lookup(const std::string& name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (const auto found = scope->find(name); found != scope->end())
                return &found->second;
        }
        return nullptr;
    }

private:
    std::vector<std::unordered_map<std::string, SemanticSymbol>> scopes_;
};
