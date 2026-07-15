#include "generic_tokens.hpp"

#include <deque>
#include <stdexcept>
#include <unordered_map>

namespace {
struct TopLevelRange {
    std::size_t begin;
    std::size_t end;
    bool import;
    std::string name;
    std::unordered_set<std::string> identifiers;
};

std::string declarationName(const std::vector<Token>& tokens,
                            std::size_t begin, std::size_t end) {
    std::size_t cursor = begin;
    while (cursor < end &&
           (tokens[cursor].kind == TokenKind::Pub ||
            tokens[cursor].kind == TokenKind::Native ||
            tokens[cursor].kind == TokenKind::Extend)) ++cursor;
    if (cursor >= end) return {};
    const TokenKind declaration = tokens[cursor++].kind;
    if (declaration != TokenKind::Val && declaration != TokenKind::Var &&
        declaration != TokenKind::Def && declaration != TokenKind::Struct &&
        declaration != TokenKind::Enum && declaration != TokenKind::Trait) return {};
    if (cursor >= end || (tokens[cursor].kind != TokenKind::Identifier &&
                          tokens[cursor].kind != TokenKind::VecType)) return {};
    std::string name = tokens[cursor++].text;
    if (cursor + 1U < end && tokens[cursor].kind == TokenKind::Dot &&
        tokens[cursor + 1U].kind == TokenKind::Identifier)
        name += "." + tokens[cursor + 1U].text;
    return name;
}

TopLevelRange makeRange(const std::vector<Token>& tokens,
                        std::size_t begin, std::size_t end) {
    TopLevelRange range{begin, end, tokens[begin].kind == TokenKind::Import, {}, {}};
    if (!range.import) range.name = declarationName(tokens, begin, end);
    for (std::size_t i = begin; i < end; ++i)
        if (tokens[i].kind == TokenKind::Identifier)
            range.identifiers.insert(tokens[i].text);
    return range;
}

std::vector<TopLevelRange> splitTopLevel(const std::vector<Token>& tokens) {
    std::vector<TopLevelRange> ranges;
    std::size_t cursor = 0;
    while (cursor < tokens.size()) {
        while (cursor < tokens.size() && tokens[cursor].kind == TokenKind::Separator)
            ++cursor;
        if (cursor >= tokens.size() || tokens[cursor].kind == TokenKind::End) break;
        const std::size_t begin = cursor;
        int parentheses = 0;
        int brackets = 0;
        int braces = 0;
        while (cursor < tokens.size()) {
            const TokenKind kind = tokens[cursor].kind;
            if (kind == TokenKind::LeftParen) ++parentheses;
            else if (kind == TokenKind::RightParen) --parentheses;
            else if (kind == TokenKind::LeftBracket) ++brackets;
            else if (kind == TokenKind::RightBracket) --brackets;
            else if (kind == TokenKind::LeftBrace) ++braces;
            else if (kind == TokenKind::RightBrace) --braces;
            if (parentheses < 0 || brackets < 0 || braces < 0)
                throw std::runtime_error("délimiteur global déséquilibré dans les tokens génériques");
            if (kind == TokenKind::End) break;
            ++cursor;
            if (parentheses == 0 && brackets == 0 && braces == 0 &&
                (kind == TokenKind::Separator || kind == TokenKind::Semicolon)) break;
        }
        std::size_t end = cursor;
        if (end > begin && tokens[end - 1U].kind == TokenKind::Separator) --end;
        if (end > begin) ranges.push_back(makeRange(tokens, begin, end));
        if (cursor < tokens.size() && tokens[cursor].kind == TokenKind::End) break;
    }
    return ranges;
}
}

std::vector<Token> reduceGenericTokens(
    const std::vector<Token>& tokens,
    const std::unordered_set<std::string>& genericExports) {
    if (genericExports.empty()) return {};
    const std::vector<TopLevelRange> ranges = splitTopLevel(tokens);
    std::unordered_map<std::string, std::size_t> declarations;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        if (ranges[i].name.empty()) continue;
        if (!declarations.emplace(ranges[i].name, i).second)
            throw std::runtime_error("déclaration globale dupliquée dans les tokens génériques : " +
                                     ranges[i].name);
    }

    std::unordered_set<std::size_t> selected;
    std::deque<std::size_t> pending;
    for (const std::string& root : genericExports) {
        const auto found = declarations.find(root);
        if (found == declarations.end())
            throw std::runtime_error("export générique absent des tokens du module : " + root);
        if (selected.insert(found->second).second) pending.push_back(found->second);
    }
    while (!pending.empty()) {
        const std::size_t current = pending.front();
        pending.pop_front();
        for (const std::string& identifier : ranges[current].identifiers) {
            const auto dependency = declarations.find(identifier);
            if (dependency != declarations.end() &&
                selected.insert(dependency->second).second)
                pending.push_back(dependency->second);
        }
    }

    std::vector<Token> reduced;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        if (!ranges[i].import && !selected.contains(i)) continue;
        if (!reduced.empty() && reduced.back().kind != TokenKind::Separator &&
            reduced.back().kind != TokenKind::Semicolon)
            reduced.push_back(Token{TokenKind::Separator, "\n",
                                    tokens[ranges[i].begin].location});
        reduced.insert(reduced.end(), tokens.begin() +
            static_cast<std::ptrdiff_t>(ranges[i].begin), tokens.begin() +
            static_cast<std::ptrdiff_t>(ranges[i].end));
    }
    const SourceLocation endLocation = tokens.empty()
        ? SourceLocation{} : tokens.back().location;
    if (!reduced.empty() && reduced.back().kind != TokenKind::Separator &&
        reduced.back().kind != TokenKind::Semicolon)
        reduced.push_back(Token{TokenKind::Separator, "\n", endLocation});
    reduced.push_back(Token{TokenKind::End, "", endLocation});
    return reduced;
}
