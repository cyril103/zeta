#include "semantic.hpp"

#include "diagnostic.hpp"
#include "type_rules.hpp"

#include <type_traits>

namespace {
[[noreturn]] void mismatch(SourceLocation location, ValueType expected, ValueType actual) {
    throw CompileError(location, "type " + typeName(expected) + " attendu, reçu " +
                                     typeName(actual));
}
}

TypedProgram SemanticAnalyzer::analyze(
    Program& program,
    const std::unordered_map<std::string, ModuleInterface>* interfaces,
    bool requireMain) {
    symbols_ = SymbolTable{};
    if (interfaces != nullptr) {
        for (const Program::Import& import : program.imports) {
            const auto module = interfaces->find(import.module);
            if (module == interfaces->end())
                throw CompileError(import.location, "module inconnu '" + import.module + "'");
            for (const auto& [name, exported] : module->second.exports) {
                symbols_.define(import.module + "." + name,
                    SemanticSymbol{exported.type, exported.kind, exported.callable,
                                   nullptr, false, exported.parameterTypes});
            }
        }
    }
    for (Statement& statement : program.statements) checkStatement(statement, true);

    if (!requireMain) return TypedProgram(program);
    const SemanticSymbol* main = symbols_.lookup("main");
    if (main == nullptr) {
        throw CompileError(SourceLocation{1, 1},
                           "point d'entrée manquant : 'def main () : Int' est obligatoire");
    }
    const Declaration& declaration = *main->declaration;
    if (declaration.kind != BindingKind::Def || !declaration.callable ||
        !declaration.parameters.empty() || declaration.type != ValueType::Int) {
        throw CompileError(declaration.location,
                           "le point d'entrée doit avoir la signature 'def main () : Int'");
    }
    return TypedProgram(program);
}

void SemanticAnalyzer::checkStatement(Statement& statement, bool global) {
    std::visit([&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, Declaration>) checkDeclaration(node, global);
        else if constexpr (std::is_same_v<T, Assignment>) checkAssignment(node);
        else if constexpr (std::is_same_v<T, WhileStatement>) checkLoop(node);
        else if constexpr (std::is_same_v<T, ExpressionStatement>) {
            if (global) {
                throw CompileError(node.location,
                                   "une expression seule n'est pas autorisée au niveau global");
            }
            checkExpression(*node.expression, inferType(*node.expression));
        } else if constexpr (std::is_same_v<T, ReturnStatement>) {
            if (!returnType_)
                throw CompileError(node.location, "'return' est autorisé uniquement dans une fonction");
            checkExpression(*node.value, *returnType_);
        } else {
            if (loopDepth_ == 0)
                throw CompileError(node.location,
                    std::is_same_v<T, BreakStatement>
                        ? "'break' est autorisé uniquement dans une boucle"
                        : "'continue' est autorisé uniquement dans une boucle");
        }
    }, statement.value);
}

void SemanticAnalyzer::checkDeclaration(Declaration& declaration, bool allowRecursion) {
    const SemanticSymbol declarationSymbol{declaration.type, declaration.kind,
                                            declaration.callable, &declaration, false, {}};
    if (declaration.callable && allowRecursion &&
        !symbols_.define(declaration.name, declarationSymbol)) {
        throw CompileError(declaration.location,
                           "l'identifiant '" + declaration.name + "' est déjà défini");
    }
    symbols_.pushScope();
    for (const Parameter& parameter : declaration.parameters) {
        if (!symbols_.defineParameter(parameter.name,
                SemanticSymbol{parameter.type, BindingKind::Val, false, nullptr, true, {}})) {
            throw CompileError(parameter.location,
                               "paramètre '" + parameter.name + "' déclaré plusieurs fois");
        }
    }
    const auto previousReturnType = returnType_;
    if (declaration.callable) returnType_ = declaration.type;
    checkExpression(*declaration.initializer, declaration.type);
    returnType_ = previousReturnType;
    symbols_.popScope();

    if ((!declaration.callable || !allowRecursion) &&
        !symbols_.define(declaration.name, declarationSymbol)) {
        throw CompileError(declaration.location,
                           "l'identifiant '" + declaration.name + "' est déjà défini");
    }
}

void SemanticAnalyzer::checkAssignment(Assignment& assignment) {
    const SemanticSymbol* target = symbols_.lookup(assignment.name);
    if (target == nullptr) {
        throw CompileError(assignment.location,
                           "identifiant inconnu '" + assignment.name + "'");
    }
    if (target->parameter) {
        throw CompileError(assignment.location,
                           "le paramètre '" + assignment.name + "' est immuable");
    }
    if (target->kind != BindingKind::Var) {
        const std::string subject = target->kind == BindingKind::Def ? "la définition '" : "la val '";
        throw CompileError(assignment.location, subject + assignment.name + "' est immuable");
    }
    checkExpression(*assignment.value, target->type);
}

void SemanticAnalyzer::checkStatements(std::vector<StatementPtr>& statements) {
    for (StatementPtr& statement : statements) checkStatement(*statement, false);
}

void SemanticAnalyzer::checkLoop(WhileStatement& loop) {
    checkExpression(*loop.condition, ValueType::Bool);
    symbols_.pushScope();
    ++loopDepth_;
    checkStatements(loop.body);
    --loopDepth_;
    symbols_.popScope();
}

ValueType SemanticAnalyzer::inferType(const Expression& expression) const {
    return std::visit([&](const auto& node) -> ValueType {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IntegerExpr>) return ValueType::Int;
        else if constexpr (std::is_same_v<T, DoubleExpr>) return ValueType::Double;
        else if constexpr (std::is_same_v<T, BoolExpr>) return ValueType::Bool;
        else if constexpr (std::is_same_v<T, CharacterExpr>) return ValueType::Char;
        else if constexpr (std::is_same_v<T, StringExpr>) return ValueType::String;
        else if constexpr (std::is_same_v<T, NameExpr> || std::is_same_v<T, CallExpr>) {
            const SemanticSymbol* symbol = symbols_.lookup(node.name);
            return symbol == nullptr ? ValueType::Int : symbol->type;
        } else if constexpr (std::is_same_v<T, ConversionExpr>) {
            return node.target;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            return node.op == "!" ? ValueType::Bool : inferType(*node.operand);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            if (TypeRules::isLogical(node.op) || TypeRules::isComparison(node.op))
                return ValueType::Bool;
            return TypeRules::commonOperandType(inferType(*node.left), inferType(*node.right));
        } else if constexpr (std::is_same_v<T, BlockExpr>) {
            return node.result ? inferType(*node.result) : ValueType::Int;
        } else {
            return TypeRules::commonOperandType(inferType(*node.thenBranch),
                                                inferType(*node.elseBranch));
        }
    }, expression.value);
}

ValueType SemanticAnalyzer::checkExpression(Expression& expression, ValueType expected) {
    const ValueType actual = std::visit([&](auto& node) -> ValueType {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IntegerExpr>) {
            if (expected == ValueType::Bool)
                throw CompileError(expression.location,
                                   "un Bool doit être écrit avec 'true' ou 'false'");
            if (expected == ValueType::Byte && node.value > 255)
                throw CompileError(expression.location, "le littéral " +
                    std::to_string(node.value) + " dépasse l'intervalle Byte (0..255)");
            if (expected == ValueType::Char || expected == ValueType::String)
                return ValueType::Int;
            return expected;
        } else if constexpr (std::is_same_v<T, DoubleExpr>) return ValueType::Double;
        else if constexpr (std::is_same_v<T, BoolExpr>) return ValueType::Bool;
        else if constexpr (std::is_same_v<T, CharacterExpr>) return ValueType::Char;
        else if constexpr (std::is_same_v<T, StringExpr>) return ValueType::String;
        else if constexpr (std::is_same_v<T, NameExpr>) {
            const SemanticSymbol* symbol = symbols_.lookup(node.name);
            if (symbol == nullptr)
                throw CompileError(expression.location, "identifiant inconnu '" + node.name + "'");
            if (symbol->kind == BindingKind::Def && symbol->callable)
                throw CompileError(expression.location,
                                   "la fonction '" + node.name + "' doit être appelée");
            return symbol->type;
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            const SemanticSymbol* symbol = symbols_.lookup(node.name);
            if (symbol == nullptr)
                throw CompileError(expression.location, "fonction inconnue '" + node.name + "'");
            if (symbol->kind != BindingKind::Def || !symbol->callable)
                throw CompileError(expression.location, "'" + node.name + "' n'est pas une fonction");
            const std::size_t parameterCount = symbol->declaration != nullptr
                ? symbol->declaration->parameters.size() : symbol->parameterTypes.size();
            if (node.arguments.size() != parameterCount)
                throw CompileError(expression.location, "la fonction '" + node.name + "' attend " +
                    std::to_string(parameterCount) + " argument(s), reçu " +
                    std::to_string(node.arguments.size()));
            for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                const ValueType parameterType = symbol->declaration != nullptr
                    ? symbol->declaration->parameters[i].type : symbol->parameterTypes[i];
                checkExpression(*node.arguments[i], parameterType);
            }
            return symbol->type;
        } else if constexpr (std::is_same_v<T, ConversionExpr>) {
            const ValueType source = inferType(*node.operand);
            if (!TypeRules::canExplicitlyConvert(source, node.target)) {
                throw CompileError(expression.location,
                    "conversion explicite de " + typeName(source) + " vers " +
                    typeName(node.target) + " interdite");
            }
            checkExpression(*node.operand, source);
            return node.target;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            if (node.op == "!") {
                checkExpression(*node.operand, ValueType::Bool);
                return ValueType::Bool;
            }
            if (!TypeRules::isNumeric(expected))
                throw CompileError(expression.location,
                                   "les opérateurs arithmétiques ne s'appliquent pas à Bool");
            checkExpression(*node.operand, expected);
            return expected;
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            if (TypeRules::isLogical(node.op)) {
                checkExpression(*node.left, ValueType::Bool);
                checkExpression(*node.right, ValueType::Bool);
                return ValueType::Bool;
            }
            if (TypeRules::isComparison(node.op)) {
                const ValueType operands = TypeRules::commonOperandType(
                    inferType(*node.left), inferType(*node.right));
                if (TypeRules::isOrdering(node.op) &&
                    (operands == ValueType::Bool || operands == ValueType::String))
                    throw CompileError(expression.location,
                                       "seuls '==' et '!=' sont autorisés sur " +
                                           typeName(operands));
                checkExpression(*node.left, operands);
                checkExpression(*node.right, operands);
                return ValueType::Bool;
            }
            if (!TypeRules::isNumeric(expected))
                throw CompileError(expression.location,
                                   "les opérateurs arithmétiques ne s'appliquent pas à Bool");
            checkExpression(*node.left, expected);
            checkExpression(*node.right, expected);
            return expected;
        } else if constexpr (std::is_same_v<T, BlockExpr>) {
            symbols_.pushScope();
            checkStatements(node.statements);
            if (node.result) checkExpression(*node.result, expected);
            symbols_.popScope();
            return expected;
        } else {
            checkExpression(*node.condition, ValueType::Bool);
            checkExpression(*node.thenBranch, expected);
            checkExpression(*node.elseBranch, expected);
            return expected;
        }
    }, expression.value);

    if (actual != expected) mismatch(expression.location, expected, actual);
    expression.inferredType = actual;
    expression.typed = true;
    return actual;
}
