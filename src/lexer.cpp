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
        if (c == '\'') {
            const std::size_t begin = current_ - 1;
            bool escaped = false;
            while (!atEnd() && peek() != '\n') {
                const char value = advance();
                if (value == '\'' && !escaped) {
                    add(TokenKind::Character,
                        std::string(source_.substr(begin, current_ - begin)), start);
                    break;
                }
                escaped = value == '\\' && !escaped;
                if (value != '\\') escaped = false;
            }
            if (tokens_.empty() || tokens_.back().location.line != start.line ||
                tokens_.back().kind != TokenKind::Character)
                throw CompileError(start, "littéral Char non terminé");
            continue;
        }
        if (c == '"') {
            const std::size_t begin = current_ - 1;
            bool escaped = false;
            while (!atEnd() && peek() != '\n') {
                const char value = advance();
                if (value == '"' && !escaped) {
                    add(TokenKind::String,
                        std::string(source_.substr(begin, current_ - begin)), start);
                    break;
                }
                escaped = value == '\\' && !escaped;
                if (value != '\\') escaped = false;
            }
            if (tokens_.empty() || tokens_.back().location.line != start.line ||
                tokens_.back().kind != TokenKind::String)
                throw CompileError(start, "littéral String non terminé");
            continue;
        }

        if (c == '=' && peek() == '=') {
            advance(); add(TokenKind::EqualEqual, "==", start); continue;
        }
        if (c == '!' && peek() == '=') {
            advance(); add(TokenKind::BangEqual, "!=", start); continue;
        }
        if (c == '<' && peek() == '=') {
            advance(); add(TokenKind::LessEqual, "<=", start); continue;
        }
        if (c == '>' && peek() == '=') {
            advance(); add(TokenKind::GreaterEqual, ">=", start); continue;
        }
        if (c == '&' && peek() == '&') {
            advance(); add(TokenKind::AndAnd, "&&", start); continue;
        }
        if (c == '|' && peek() == '|') {
            advance(); add(TokenKind::OrOr, "||", start); continue;
        }

        switch (c) {
        case ':': add(TokenKind::Colon, ":", start); continue;
        case ',': add(TokenKind::Comma, ",", start); continue;
        case '.': add(TokenKind::Dot, ".", start); continue;
        case '=': add(TokenKind::Equal, "=", start); continue;
        case '!': add(TokenKind::Bang, "!", start); continue;
        case '<': add(TokenKind::Less, "<", start); continue;
        case '>': add(TokenKind::Greater, ">", start); continue;
        case '+': add(TokenKind::Plus, "+", start); continue;
        case '-': add(TokenKind::Minus, "-", start); continue;
        case '*': add(TokenKind::Star, "*", start); continue;
        case '/': add(TokenKind::Slash, "/", start); continue;
        case '(': add(TokenKind::LeftParen, "(", start); continue;
        case ')': add(TokenKind::RightParen, ")", start); continue;
        case '{': add(TokenKind::LeftBrace, "{", start); continue;
        case '}': add(TokenKind::RightBrace, "}", start); continue;
        case ';': add(TokenKind::Semicolon, ";", start); continue;
        case '[': add(TokenKind::LeftBracket, "[", start); continue;
        case ']': add(TokenKind::RightBracket, "]", start); continue;
        default: break;
        }
        if (c == '&' || c == '|') {
            throw CompileError(start, std::string("opérateur incomplet '") + c + "'");
        }

        const auto uc = static_cast<unsigned char>(c);
        if (std::isdigit(uc)) {
            const std::size_t begin = current_ - 1;
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            bool floating = false;
            if (peek() == '.') {
                floating = true;
                advance();
                while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            }
            if (peek() == 'e' || peek() == 'E') {
                floating = true;
                advance();
                if (peek() == '+' || peek() == '-') advance();
                if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                    throw CompileError(start, "exposant scientifique incomplet");
                }
                while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            }
            if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') {
                throw CompileError(start, "un identifiant ne peut pas commencer par un chiffre");
            }
            add(floating ? TokenKind::Floating : TokenKind::Integer,
                std::string(source_.substr(begin, current_ - begin)), start);
            continue;
        }
        if (std::isalpha(uc) || c == '_') {
            const std::size_t begin = current_ - 1;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
            std::string text(source_.substr(begin, current_ - begin));
            TokenKind kind = TokenKind::Identifier;
            if (text == "val") kind = TokenKind::Val;
            if (text == "var") kind = TokenKind::Var;
            if (text == "def") kind = TokenKind::Def;
            if (text == "import") kind = TokenKind::Import;
            if (text == "pub") kind = TokenKind::Pub;
            if (text == "native") kind = TokenKind::Native;
            if (text == "Int") kind = TokenKind::IntType;
            if (text == "Byte") kind = TokenKind::ByteType;
            if (text == "Double") kind = TokenKind::DoubleType;
            if (text == "Bool") kind = TokenKind::BoolType;
            if (text == "Char") kind = TokenKind::CharType;
            if (text == "String") kind = TokenKind::StringType;
            if (text == "true") kind = TokenKind::True;
            if (text == "false") kind = TokenKind::False;
            if (text == "if") kind = TokenKind::If;
            if (text == "else") kind = TokenKind::Else;
            if (text == "while") kind = TokenKind::While;
            if (text == "do") kind = TokenKind::Do;
            if (text == "return") kind = TokenKind::Return;
            if (text == "break") kind = TokenKind::Break;
            if (text == "continue") kind = TokenKind::Continue;
            add(kind, std::move(text), start);
            continue;
        }
        throw CompileError(start, std::string("caractère inconnu '") + c + "'");
    }
    add(TokenKind::End, "", location_);
    return tokens_;
}
