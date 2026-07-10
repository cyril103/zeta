#pragma once

#include "diagnostic.hpp"

#include <string>

enum class TokenKind {
    Val,
    Var,
    Def,
    IntType,
    ByteType,
    DoubleType,
    BoolType,
    Identifier,
    Integer,
    Floating,
    True,
    False,
    Colon,
    Comma,
    Equal,
    Plus,
    Minus,
    Star,
    Slash,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Separator,
    End,
};

struct Token {
    TokenKind kind;
    std::string text;
    SourceLocation location;
};

inline std::string tokenName(TokenKind kind) {
    switch (kind) {
    case TokenKind::Val: return "'val'";
    case TokenKind::Var: return "'var'";
    case TokenKind::Def: return "'def'";
    case TokenKind::IntType: return "'Int'";
    case TokenKind::ByteType: return "'Byte'";
    case TokenKind::DoubleType: return "'Double'";
    case TokenKind::BoolType: return "'Bool'";
    case TokenKind::Identifier: return "un identifiant";
    case TokenKind::Integer: return "un entier";
    case TokenKind::Floating: return "un nombre à virgule flottante";
    case TokenKind::True: return "'true'";
    case TokenKind::False: return "'false'";
    case TokenKind::Colon: return "':'";
    case TokenKind::Comma: return "','";
    case TokenKind::Equal: return "'='";
    case TokenKind::Plus: return "'+'";
    case TokenKind::Minus: return "'-'";
    case TokenKind::Star: return "'*'";
    case TokenKind::Slash: return "'/'";
    case TokenKind::LeftParen: return "'('";
    case TokenKind::RightParen: return "')'";
    case TokenKind::LeftBrace: return "'{'";
    case TokenKind::RightBrace: return "'}'";
    case TokenKind::Separator: return "une fin de ligne ou ';'";
    case TokenKind::End: return "la fin du fichier";
    }
    return "un token";
}
