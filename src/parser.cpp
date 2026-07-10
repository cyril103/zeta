#include "parser.hpp"

#include "diagnostic.hpp"

#include <charconv>
#include <cmath>
#include <limits>

const Token& Parser::peek() const { return tokens_[current_]; }
const Token& Parser::previous() const { return tokens_[current_ - 1]; }
bool Parser::check(TokenKind kind) const { return peek().kind == kind; }
bool Parser::match(TokenKind kind) {
    if (!check(kind)) return false;
    ++current_;
    return true;
}

const Token& Parser::consume(TokenKind kind, const std::string& message) {
    if (check(kind)) return tokens_[current_++];
    throw CompileError(peek().location, message + ", reçu " + tokenName(peek().kind));
}

ValueType Parser::consumeType(const std::string& message) {
    if (match(TokenKind::IntType)) return ValueType::Int;
    if (match(TokenKind::ByteType)) return ValueType::Byte;
    if (match(TokenKind::DoubleType)) return ValueType::Double;
    if (match(TokenKind::BoolType)) return ValueType::Bool;
    throw CompileError(peek().location, message + ", reçu " + tokenName(peek().kind));
}

void Parser::skipSeparators() {
    while (match(TokenKind::Separator)) {}
}

void Parser::expressionContinuation() {
    if (!check(TokenKind::Separator)) return;
    if (blockDepth_ == 0) {
        throw CompileError(peek().location,
                           "une expression sur plusieurs lignes doit être entre accolades");
    }
    skipSeparators();
}

Program Parser::parse() {
    Program program;
    skipSeparators();
    while (!check(TokenKind::End)) {
        program.statements.push_back(statement());
        if (!check(TokenKind::End) && !match(TokenKind::Separator)) {
            throw CompileError(peek().location, "fin d'instruction attendue");
        }
        skipSeparators();
    }
    return program;
}

Statement Parser::statement() {
    if (match(TokenKind::Val)) return declaration(BindingKind::Val);
    if (match(TokenKind::Var)) return declaration(BindingKind::Var);
    if (match(TokenKind::Def)) return declaration(BindingKind::Def);
    if (check(TokenKind::Identifier)) return assignment();
    throw CompileError(peek().location,
                       "instruction attendue ('val', 'var', 'def' ou affectation)");
}

Declaration Parser::declaration(BindingKind kind) {
    const Token start = previous();
    const Token& name = consume(TokenKind::Identifier,
                                "identifiant attendu après '" + start.text + "'");
    bool callable = false;
    std::vector<Parameter> parameters;
    if (kind == BindingKind::Def && match(TokenKind::LeftParen)) {
        callable = true;
        if (!check(TokenKind::RightParen)) {
            do {
                const Token& parameterName = consume(TokenKind::Identifier,
                                                     "nom de paramètre attendu");
                consume(TokenKind::Colon, "':' attendu après le paramètre");
                const ValueType parameterType = consumeType(
                    "type 'Int', 'Byte', 'Double' ou 'Bool' attendu pour le paramètre");
                parameters.push_back(Parameter{parameterName.location,
                                               parameterName.text,
                                               parameterType});
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RightParen, "')' attendue après les paramètres");
    }
    consume(TokenKind::Colon, "':' attendu après l'identifiant");
    const ValueType type = consumeType("type 'Int', 'Byte', 'Double' ou 'Bool' attendu");
    consume(TokenKind::Equal, "'=' attendu après le type");
    return Declaration{start.location, name.text, type, kind, callable,
                       std::move(parameters), expression()};
}

Assignment Parser::assignment() {
    const Token& name = consume(TokenKind::Identifier, "identifiant attendu");
    consume(TokenKind::Equal, "'=' attendu après l'identifiant");
    return Assignment{name.location, name.text, expression()};
}

ExprPtr Parser::expression() { return logicalOr(); }

ExprPtr Parser::logicalOr() {
    ExprPtr expr = logicalAnd();
    while (match(TokenKind::OrOr)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = logicalAnd();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::logicalAnd() {
    ExprPtr expr = equality();
    while (match(TokenKind::AndAnd)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = equality();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::equality() {
    ExprPtr expr = comparison();
    while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = comparison();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::comparison() {
    ExprPtr expr = addition();
    while (match(TokenKind::Less) || match(TokenKind::LessEqual) ||
           match(TokenKind::Greater) || match(TokenKind::GreaterEqual)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = addition();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::addition() {
    ExprPtr expr = multiplication();
    while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = multiplication();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::multiplication() {
    ExprPtr expr = unary();
    while (match(TokenKind::Star) || match(TokenKind::Slash)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = unary();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::unary() {
    if (match(TokenKind::Minus) || match(TokenKind::Plus) || match(TokenKind::Bang)) {
        const Token op = previous();
        expressionContinuation();
        return std::make_unique<Expression>(Expression{
            op.location, UnaryExpr{op.text, unary()}});
    }
    return primary();
}

ExprPtr Parser::primary() {
    if (match(TokenKind::LeftBrace)) {
        return blockExpression(previous().location);
    }
    if (match(TokenKind::Integer)) {
        const Token token = previous();
        std::int64_t value{};
        const auto result = std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
        if (result.ec != std::errc{} || value > std::numeric_limits<std::int32_t>::max()) {
            throw CompileError(token.location, "entier hors de l'intervalle Int32");
        }
        return std::make_unique<Expression>(Expression{token.location, IntegerExpr{static_cast<std::int32_t>(value)}});
    }
    if (match(TokenKind::Floating)) {
        const Token token = previous();
        double value{};
        const auto result = std::from_chars(token.text.data(),
                                            token.text.data() + token.text.size(), value);
        if (result.ec != std::errc{} || result.ptr != token.text.data() + token.text.size() ||
            !std::isfinite(value)) {
            throw CompileError(token.location, "nombre Double invalide ou hors limites");
        }
        return std::make_unique<Expression>(Expression{token.location, DoubleExpr{value}});
    }
    if (match(TokenKind::True) || match(TokenKind::False)) {
        const Token token = previous();
        return std::make_unique<Expression>(Expression{
            token.location, BoolExpr{token.kind == TokenKind::True}});
    }
    if (match(TokenKind::Identifier)) {
        const Token token = previous();
        if (match(TokenKind::LeftParen)) {
            std::vector<ExprPtr> arguments;
            expressionContinuation();
            if (!check(TokenKind::RightParen)) {
                do {
                    arguments.push_back(expression());
                    if (match(TokenKind::Comma)) {
                        expressionContinuation();
                    } else {
                        break;
                    }
                } while (true);
            }
            expressionContinuation();
            consume(TokenKind::RightParen, "')' attendue après les arguments");
            return std::make_unique<Expression>(Expression{
                token.location, CallExpr{token.text, std::move(arguments)}});
        }
        return std::make_unique<Expression>(Expression{token.location, NameExpr{token.text}});
    }
    if (match(TokenKind::LeftParen)) {
        expressionContinuation();
        ExprPtr expr = expression();
        expressionContinuation();
        consume(TokenKind::RightParen, "')' attendue après l'expression");
        return expr;
    }
    throw CompileError(peek().location, "expression attendue");
}

ExprPtr Parser::blockExpression(SourceLocation location) {
    ++blockDepth_;
    std::vector<StatementPtr> statements;
    skipSeparators();

    while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
        const bool startsDeclaration = check(TokenKind::Val) || check(TokenKind::Var) ||
                                       check(TokenKind::Def);
        const bool startsAssignment = check(TokenKind::Identifier) &&
                                      current_ + 1 < tokens_.size() &&
                                      tokens_[current_ + 1].kind == TokenKind::Equal;
        if (!startsDeclaration && !startsAssignment) break;

        statements.push_back(std::make_unique<Statement>(statement()));
        if (!match(TokenKind::Separator)) {
            throw CompileError(peek().location,
                               "fin de ligne ou ';' attendue après l'instruction du bloc");
        }
        skipSeparators();
    }

    if (check(TokenKind::RightBrace)) {
        throw CompileError(peek().location,
                           "un bloc doit se terminer par une expression");
    }
    ExprPtr result = expression();
    skipSeparators();
    consume(TokenKind::RightBrace, "'}' attendue à la fin du bloc");
    --blockDepth_;
    return std::make_unique<Expression>(Expression{
        location, BlockExpr{std::move(statements), std::move(result)}});
}
