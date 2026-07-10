#include "lexer.hpp"

#include "diagnostic.hpp"

#include <cctype>

bool Lexer::atEnd() const { return current_ >= source_.size(); }

char Lexer::peek(std::size_t offset) const {
    return current_ + offset < source_.size() ? source_[current_ + offset] : '\0';
}

char Lexer::advance() {
    const char value = source_[current_++];
    ++location_.column;
    return value;
}

void Lexer::add(TokenKind kind, std::string text, SourceLocation location) {
    tokens_.push_back(Token{kind, std::move(text), location});
}

std::vector<Token> Lexer::scan() {
    while (!atEnd()) {
        const SourceLocation start = location_;
        const char c = advance();

        if (c == ' ' || c == '\t' || c == '\r') continue;
        if (c == '\n') {
            add(TokenKind::Separator, "\\n", start);
            ++location_.line;
            location_.column = 1;
            continue;
        }
        if (c == '/' && peek() == '/') {
            while (!atEnd() && peek() != '\n') advance();
            continue;
        }

        switch (c) {
        case ':': add(TokenKind::Colon, ":", start); continue;
        case '=': add(TokenKind::Equal, "=", start); continue;
        case '+': add(TokenKind::Plus, "+", start); continue;
        case '-': add(TokenKind::Minus, "-", start); continue;
        case '*': add(TokenKind::Star, "*", start); continue;
        case '/': add(TokenKind::Slash, "/", start); continue;
        case '(': add(TokenKind::LeftParen, "(", start); continue;
        case ')': add(TokenKind::RightParen, ")", start); continue;
        case ';': add(TokenKind::Separator, ";", start); continue;
        default: break;
        }

        const auto uc = static_cast<unsigned char>(c);
        if (std::isdigit(uc)) {
            const std::size_t begin = current_ - 1;
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') {
                throw CompileError(start, "un identifiant ne peut pas commencer par un chiffre");
            }
            add(TokenKind::Integer, std::string(source_.substr(begin, current_ - begin)), start);
            continue;
        }
        if (std::isalpha(uc) || c == '_') {
            const std::size_t begin = current_ - 1;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
            std::string text(source_.substr(begin, current_ - begin));
            TokenKind kind = TokenKind::Identifier;
            if (text == "val") kind = TokenKind::Val;
            if (text == "var") kind = TokenKind::Var;
            if (text == "Int") kind = TokenKind::IntType;
            add(kind, std::move(text), start);
            continue;
        }
        throw CompileError(start, std::string("caractère inconnu '") + c + "'");
    }
    add(TokenKind::End, "", location_);
    return tokens_;
}
