#pragma once

#include "token.hpp"

#include <string>
#include <unordered_set>
#include <vector>

std::vector<Token> reduceGenericTokens(
    const std::vector<Token>& tokens,
    const std::unordered_set<std::string>& genericExports);
