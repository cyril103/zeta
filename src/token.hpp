#pragma once

#include "diagnostic.hpp"

#include <string>

enum class TokenKind {
    Val,
    Var,
    Def,
    Import,
    Pub,
    Native,
    IntType,
    ByteType,
    DoubleType,
    BoolType,
    CharType,
    StringType,
    Identifier,
    Integer,
    Floating,
    Character,
    String,
    True,
    False,
    If,
    Else,
    While,
    Do,
    Return,
    Break,
    Continue,
    Colon,
    Comma,
    Dot,
    Semicolon,
    Equal,
    EqualEqual,
    Bang,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AndAnd,
    OrOr,
    Plus,
    Minus,
    Star,
    Slash,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
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
    case TokenKind::Import: return "'import'";
    case TokenKind::Pub: return "'pub'";
    case TokenKind::Native: return "'native'";
    case TokenKind::IntType: return "'Int'";
    case TokenKind::ByteType: return "'Byte'";
    case TokenKind::DoubleType: return "'Double'";
    case TokenKind::BoolType: return "'Bool'";
    case TokenKind::CharType: return "'Char'";
    case TokenKind::StringType: return "'String'";
    case TokenKind::Identifier: return "un identifiant";
    case TokenKind::Integer: return "un entier";
    case TokenKind::Floating: return "un nombre à virgule flottante";
    case TokenKind::Character: return "un caractère";
    case TokenKind::String: return "une chaîne de caractères";
    case TokenKind::True: return "'true'";
    case TokenKind::False: return "'false'";
    case TokenKind::If: return "'if'";
    case TokenKind::Else: return "'else'";
    case TokenKind::While: return "'while'";
    case TokenKind::Do: return "'do'";
    case TokenKind::Return: return "'return'";
    case TokenKind::Break: return "'break'";
    case TokenKind::Continue: return "'continue'";
    case TokenKind::Colon: return "':'";
    case TokenKind::Comma: return "','";
    case TokenKind::Dot: return "'.'";
    case TokenKind::Semicolon: return "';'";
    case TokenKind::Equal: return "'='";
    case TokenKind::EqualEqual: return "'=='";
    case TokenKind::Bang: return "'!'";
    case TokenKind::BangEqual: return "'!='";
    case TokenKind::Less: return "'<'";
    case TokenKind::LessEqual: return "'<='";
    case TokenKind::Greater: return "'>'";
    case TokenKind::GreaterEqual: return "'>='";
    case TokenKind::AndAnd: return "'&&'";
    case TokenKind::OrOr: return "'||'";
    case TokenKind::Plus: return "'+'";
    case TokenKind::Minus: return "'-'";
    case TokenKind::Star: return "'*'";
    case TokenKind::Slash: return "'/'";
    case TokenKind::LeftParen: return "'('";
    case TokenKind::RightParen: return "')'";
    case TokenKind::LeftBrace: return "'{'";
    case TokenKind::RightBrace: return "'}'";
    case TokenKind::LeftBracket: return "'['";
    case TokenKind::RightBracket: return "']'";
    case TokenKind::Separator: return "une fin de ligne ou ';'";
    case TokenKind::End: return "la fin du fichier";
    }
    return "un token";
}
