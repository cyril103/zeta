#include "semantic.hpp"

#include "diagnostic.hpp"
#include "type_rules.hpp"

#include <type_traits>
#include <unordered_map>

namespace {
[[noreturn]] void mismatch(SourceLocation location, ValueType expected, ValueType actual) {
    throw CompileError(location, "type " + typeName(expected) + " attendu, reçu " +
                                     typeName(actual));
}
std::optional<std::int64_t> constantInteger(const Expression& expression) {
    if (const auto* integer = std::get_if<IntegerExpr>(&expression.value)) return integer->value;
    if (const auto* unary = std::get_if<UnaryExpr>(&expression.value)) {
        const auto operand = constantInteger(*unary->operand);
        if (operand && unary->op == "-") return -*operand;
        if (operand && unary->op == "+") return *operand;
    }
    return std::nullopt;
}
ValueType substituteType(
    const ValueType& type,
    const std::unordered_map<std::string, ValueType>& substitutions) {
    if (type.kind == ValueType::Kind::TypeParameter) return substitutions.at(type.typeParameter);
    if (type.kind == ValueType::Kind::Array)
        return ValueType(std::make_shared<ValueType>(substituteType(*type.element, substitutions)),
                         type.length);
    if (type.kind == ValueType::Kind::Reference)
        return ValueType(std::make_shared<ValueType>(substituteType(*type.element, substitutions)),
                         type.mutableReference);
    if (type.kind == ValueType::Kind::Slice)
        return ValueType(ValueType::Kind::Slice,
            std::make_shared<ValueType>(substituteType(*type.element, substitutions)),
            type.mutableReference);
    if (type.kind == ValueType::Kind::Box)
        return ValueType(ValueType::Kind::Box,
            std::make_shared<ValueType>(substituteType(*type.element, substitutions)));
    return type;
}

void collectExpressionNames(const Expression& expression,
                            std::unordered_map<std::string, std::size_t>& uses);

void collectStatementNames(const Statement& statement,
                           std::unordered_map<std::string, std::size_t>& uses) {
    std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, Declaration>) {
            collectExpressionNames(*node.initializer, uses);
        } else if constexpr (std::is_same_v<T, Assignment>) {
            collectExpressionNames(*node.value, uses);
        } else if constexpr (std::is_same_v<T, IndexAssignment>) {
            ++uses[node.name];
            for (const ExprPtr& index : node.indexes) collectExpressionNames(*index, uses);
            collectExpressionNames(*node.value, uses);
        } else if constexpr (std::is_same_v<T, DereferenceAssignment>) {
            collectExpressionNames(*node.reference, uses);
            collectExpressionNames(*node.value, uses);
        } else if constexpr (std::is_same_v<T, WhileStatement>) {
            collectExpressionNames(*node.condition, uses);
            for (const StatementPtr& item : node.body) collectStatementNames(*item, uses);
        } else if constexpr (std::is_same_v<T, ExpressionStatement>) {
            collectExpressionNames(*node.expression, uses);
        } else if constexpr (std::is_same_v<T, ReturnStatement>) {
            collectExpressionNames(*node.value, uses);
        }
    }, statement.value);
}

void collectExpressionNames(const Expression& expression,
                            std::unordered_map<std::string, std::size_t>& uses) {
    std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ArrayExpr>) {
            for (const ExprPtr& element : node.elements) collectExpressionNames(*element, uses);
        } else if constexpr (std::is_same_v<T, IndexExpr>) {
            collectExpressionNames(*node.array, uses);
            collectExpressionNames(*node.index, uses);
        } else if constexpr (std::is_same_v<T, AddressExpr> ||
                             std::is_same_v<T, DereferenceExpr> ||
                             std::is_same_v<T, ConversionExpr> ||
                             std::is_same_v<T, UnaryExpr>) {
            collectExpressionNames(*node.operand, uses);
        } else if constexpr (std::is_same_v<T, NameExpr>) {
            ++uses[node.name];
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            for (const ExprPtr& argument : node.arguments) collectExpressionNames(*argument, uses);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            collectExpressionNames(*node.left, uses);
            collectExpressionNames(*node.right, uses);
        } else if constexpr (std::is_same_v<T, BlockExpr>) {
            for (const StatementPtr& statement : node.statements)
                collectStatementNames(*statement, uses);
            if (node.result) collectExpressionNames(*node.result, uses);
        } else if constexpr (std::is_same_v<T, IfExpr>) {
            collectExpressionNames(*node.condition, uses);
            collectExpressionNames(*node.thenBranch, uses);
            collectExpressionNames(*node.elseBranch, uses);
        }
    }, expression.value);
}
}

TypedProgram SemanticAnalyzer::analyze(
    Program& program,
    const std::unordered_map<std::string, ModuleInterface>* interfaces,
    bool requireMain) {
    symbols_ = SymbolTable{};
    borrows_.clear();
    referenceBorrows_.clear();
    movedBoxes_.clear();
    borrowScopes_ = {{}};
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
        declaration.nativeSymbol || !declaration.parameters.empty() ||
        declaration.type != ValueType::Int) {
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
        else if constexpr (std::is_same_v<T, IndexAssignment>) checkIndexAssignment(node);
        else if constexpr (std::is_same_v<T, DereferenceAssignment>)
            checkDereferenceAssignment(node);
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
    if (!declaration.typeParameters.empty() && declaration.publicSymbol)
        throw CompileError(declaration.location,
                           "l'export des fonctions génériques attend la monomorphisation");
    if (declaration.type.kind == ValueType::Kind::Reference) {
        if (declaration.callable)
            throw CompileError(declaration.location,
                               "le retour de références n'est pas encore autorisé");
        if (allowRecursion)
            throw CompileError(declaration.location,
                               "une référence ne peut pas être stockée globalement");
        if (declaration.kind != BindingKind::Val)
            throw CompileError(declaration.location,
                               "une référence locale doit être déclarée avec 'val'");
        if (const auto* address = std::get_if<AddressExpr>(&declaration.initializer->value)) {
            const auto* borrowedName = std::get_if<NameExpr>(&address->operand->value);
            if (borrowedName == nullptr) {
                if (const auto* dereference =
                        std::get_if<DereferenceExpr>(&address->operand->value))
                    borrowedName = std::get_if<NameExpr>(&dereference->operand->value);
            }
            if (borrowedName == nullptr) {
                throw CompileError(declaration.location,
                                   "une référence doit emprunter un identifiant");
            }
            const auto& name = borrowedName->name;
            BorrowState& state = borrows_[name];
            if (address->mutableBorrow) {
                if (state.mutableBorrow || state.shared != 0)
                    throw CompileError(declaration.location,
                                       "emprunt mutable exclusif impossible pour '" + name + "'");
                state.mutableBorrow = true;
            } else {
                if (state.mutableBorrow)
                    throw CompileError(declaration.location,
                                       "'" + name + "' possède déjà un emprunt mutable");
                ++state.shared;
            }
            referenceBorrows_.insert_or_assign(declaration.name,
                ReferenceBorrow{name, address->mutableBorrow});
            borrowScopes_.back().push_back(declaration.name);
        } else if (const auto* source = std::get_if<NameExpr>(&declaration.initializer->value)) {
            if (declaration.type.mutableReference)
                throw CompileError(declaration.location,
                                   "une référence '&mut' ne peut pas être copiée");
            if (const auto original = referenceBorrows_.find(source->name);
                original != referenceBorrows_.end()) {
                ++borrows_[original->second.target].shared;
                referenceBorrows_.insert_or_assign(declaration.name,
                    ReferenceBorrow{original->second.target, false});
                borrowScopes_.back().push_back(declaration.name);
            }
        }
    }
    if (declaration.type.kind == ValueType::Kind::Slice && !declaration.nativeSymbol) {
        if (declaration.callable)
            throw CompileError(declaration.location,
                               "le retour de slices n'est pas encore autorisé");
        if (allowRecursion)
            throw CompileError(declaration.location,
                               "une slice ne peut pas être stockée globalement");
        if (declaration.kind != BindingKind::Val)
            throw CompileError(declaration.location,
                               "une slice locale doit être déclarée avec 'val'");
        const auto* source = std::get_if<NameExpr>(&declaration.initializer->value);
        if (source != nullptr) {
            if (declaration.type.mutableReference)
                throw CompileError(declaration.location,
                                   "une 'SliceMut' ne peut pas être copiée");
            if (const auto original = referenceBorrows_.find(source->name);
                original != referenceBorrows_.end()) {
                ++borrows_[original->second.target].shared;
                referenceBorrows_.insert_or_assign(declaration.name,
                    ReferenceBorrow{original->second.target, false});
                borrowScopes_.back().push_back(declaration.name);
            }
        }
        const auto* conversion = std::get_if<ConversionExpr>(&declaration.initializer->value);
        const auto* address = conversion == nullptr ? nullptr
            : std::get_if<AddressExpr>(&conversion->operand->value);
        const auto* borrowedName = address == nullptr ? nullptr
            : std::get_if<NameExpr>(&address->operand->value);
        if (source == nullptr &&
            (conversion == nullptr || conversion->target.kind != ValueType::Kind::Slice ||
             borrowedName == nullptr))
            throw CompileError(declaration.location,
                               "une slice doit être créée depuis un tableau emprunté");
        if (borrowedName != nullptr) {
            BorrowState& state = borrows_[borrowedName->name];
            if (conversion->target.mutableReference) {
                if (state.mutableBorrow || state.shared != 0)
                    throw CompileError(declaration.location,
                                       "emprunt mutable exclusif impossible pour '" +
                                       borrowedName->name + "'");
                state.mutableBorrow = true;
            } else {
                if (state.mutableBorrow)
                    throw CompileError(declaration.location,
                                       "'" + borrowedName->name +
                                       "' possède déjà un emprunt mutable");
                ++state.shared;
            }
            referenceBorrows_.insert_or_assign(declaration.name,
                ReferenceBorrow{borrowedName->name, conversion->target.mutableReference});
            borrowScopes_.back().push_back(declaration.name);
        }
    }
    if (declaration.callable && declaration.type.kind == ValueType::Kind::Array)
        throw CompileError(declaration.location,
                           "le retour d'un tableau par valeur n'est pas encore pris en charge");
    for (const Parameter& parameter : declaration.parameters)
        if (parameter.type.kind == ValueType::Kind::Array)
            throw CompileError(parameter.location,
                               "les paramètres tableaux ne sont pas encore pris en charge");
    const SemanticSymbol declarationSymbol{declaration.type, declaration.kind,
                                            declaration.callable, &declaration, false, {}};
    if (declaration.callable && allowRecursion &&
        !symbols_.define(declaration.name, declarationSymbol)) {
        throw CompileError(declaration.location,
                           "l'identifiant '" + declaration.name + "' est déjà défini");
    }
    if (declaration.nativeSymbol) return;
    symbols_.pushScope();
    for (const Parameter& parameter : declaration.parameters) {
        if (!symbols_.defineParameter(parameter.name,
                SemanticSymbol{parameter.type, BindingKind::Val, false, nullptr, true, {}})) {
            throw CompileError(parameter.location,
                               "paramètre '" + parameter.name + "' déclaré plusieurs fois");
        }
    }
    const auto previousReturnType = returnType_;
    const bool previousGenericDeclaration = insideGenericDeclaration_;
    insideGenericDeclaration_ = !declaration.typeParameters.empty();
    const auto previousMovedBoxes = movedBoxes_;
    if (declaration.callable)
        for (const Parameter& parameter : declaration.parameters)
            if (parameter.type.kind == ValueType::Kind::Box)
                movedBoxes_.erase(parameter.name);
    if (declaration.callable) returnType_ = declaration.type;
    checkExpression(*declaration.initializer, declaration.type);
    if (!declaration.callable && declaration.type.kind == ValueType::Kind::Box) {
        if (const auto* source = std::get_if<NameExpr>(&declaration.initializer->value))
            movedBoxes_.insert(source->name);
    }
    returnType_ = previousReturnType;
    insideGenericDeclaration_ = previousGenericDeclaration;
    if (declaration.callable) movedBoxes_ = previousMovedBoxes;
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
    if (target->type.kind == ValueType::Kind::Box)
        throw CompileError(assignment.location,
                           "la réaffectation d'un propriétaire Box n'est pas encore autorisée ; modifiez son contenu avec '*box = valeur'");
    if (const auto borrowed = borrows_.find(assignment.name);
        borrowed != borrows_.end() &&
        (borrowed->second.mutableBorrow || borrowed->second.shared != 0))
        throw CompileError(assignment.location,
                           "la variable '" + assignment.name + "' est empruntée");
    checkExpression(*assignment.value, target->type);
}

void SemanticAnalyzer::checkIndexAssignment(IndexAssignment& assignment) {
    const SemanticSymbol* target = symbols_.lookup(assignment.name);
    if (target == nullptr)
        throw CompileError(assignment.location, "identifiant inconnu '" + assignment.name + "'");
    const bool throughReference = target->type.kind == ValueType::Kind::Reference;
    const bool throughSlice = target->type.kind == ValueType::Kind::Slice;
    if (throughSlice && !target->type.mutableReference)
        throw CompileError(assignment.location,
                           "l'affectation indexée exige une 'SliceMut'");
    if (throughReference) {
        if (!target->type.mutableReference || target->type.element->kind != ValueType::Kind::Array)
            throw CompileError(assignment.location,
                               "l'affectation indexée exige un tableau mutable ou une référence '&mut' vers un tableau");
    } else if (!throughSlice) {
        if (target->parameter || target->kind != BindingKind::Var)
            throw CompileError(assignment.location,
                               "le tableau '" + assignment.name + "' est immuable");
        if (const auto borrowed = borrows_.find(assignment.name);
            borrowed != borrows_.end() &&
            (borrowed->second.mutableBorrow || borrowed->second.shared != 0))
            throw CompileError(assignment.location,
                               "le tableau '" + assignment.name + "' est emprunté");
    }
    if (!throughReference && !throughSlice && target->type.kind != ValueType::Kind::Array)
        throw CompileError(assignment.location,
                           "la cible d'une affectation indexée doit être un tableau");
    ValueType indexedType = throughReference ? *target->type.element : target->type;
    for (const ExprPtr& indexExpression : assignment.indexes) {
        if (indexedType.kind != ValueType::Kind::Array &&
            indexedType.kind != ValueType::Kind::Slice)
            throw CompileError(indexExpression->location, "trop d'index pour " + typeName(target->type));
        checkExpression(*indexExpression, ValueType::Int);
        if (const auto index = constantInteger(*indexExpression);
            indexedType.kind == ValueType::Kind::Array && index &&
            (*index < 0 || static_cast<std::uint64_t>(*index) >= indexedType.length))
            throw CompileError(indexExpression->location, "index " + std::to_string(*index) +
                               " hors limites pour " + typeName(indexedType));
        indexedType = *indexedType.element;
    }
    checkExpression(*assignment.value, indexedType);
}

void SemanticAnalyzer::checkDereferenceAssignment(DereferenceAssignment& assignment) {
    const ValueType reference = inferType(*assignment.reference);
    if (reference.kind == ValueType::Kind::Box) {
        if (const auto* name = std::get_if<NameExpr>(&assignment.reference->value)) {
            const SemanticSymbol* symbol = symbols_.lookup(name->name);
            if (symbol == nullptr || symbol->kind != BindingKind::Var || symbol->parameter)
                throw CompileError(assignment.location,
                                   "la mutation du contenu d'une Box exige une variable 'var'");
        }
        checkExpression(*assignment.reference, reference);
        checkExpression(*assignment.value, *reference.element);
        return;
    }
    if (reference.kind != ValueType::Kind::Reference || !reference.mutableReference)
        throw CompileError(assignment.location,
                           "l'affectation déréférencée exige une référence '&mut'");
    checkExpression(*assignment.reference, reference);
    checkExpression(*assignment.value, *reference.element);
}

void SemanticAnalyzer::checkStatements(std::vector<StatementPtr>& statements,
                                       const Expression* trailingExpression) {
    std::unordered_map<std::string, std::size_t> remainingUses;
    for (const StatementPtr& statement : statements)
        collectStatementNames(*statement, remainingUses);
    if (trailingExpression != nullptr)
        collectExpressionNames(*trailingExpression, remainingUses);

    for (StatementPtr& statement : statements) {
        if (const auto* declaration = std::get_if<Declaration>(&statement->value);
            declaration != nullptr && declaration->callable) {
            std::unordered_map<std::string, std::size_t> capturedNames;
            collectExpressionNames(*declaration->initializer, capturedNames);
            for (const auto& [name, count] : capturedNames) {
                (void)count;
                if (auto found = referenceBorrows_.find(name);
                    found != referenceBorrows_.end())
                    found->second.captured = true;
            }
        }
        checkStatement(*statement, false);
        std::unordered_map<std::string, std::size_t> statementUses;
        collectStatementNames(*statement, statementUses);
        for (const auto& [name, count] : statementUses) remainingUses[name] -= count;
        for (const std::string& referenceName : borrowScopes_.back()) {
            const auto borrow = referenceBorrows_.find(referenceName);
            if (borrow != referenceBorrows_.end() && borrow->second.active &&
                !borrow->second.captured && remainingUses[referenceName] == 0)
                releaseBorrow(referenceName);
        }
    }
}

void SemanticAnalyzer::checkLoop(WhileStatement& loop) {
    checkExpression(*loop.condition, ValueType::Bool);
    symbols_.pushScope();
    pushBorrowScope();
    ++loopDepth_;
    checkStatements(loop.body);
    --loopDepth_;
    popBorrowScope();
    symbols_.popScope();
}

void SemanticAnalyzer::pushBorrowScope() { borrowScopes_.emplace_back(); }

void SemanticAnalyzer::releaseBorrow(const std::string& referenceName) {
    ReferenceBorrow& reference = referenceBorrows_.at(referenceName);
    if (!reference.active) return;
    BorrowState& state = borrows_.at(reference.target);
    if (reference.mutableBorrow) state.mutableBorrow = false;
    else --state.shared;
    if (!state.mutableBorrow && state.shared == 0) borrows_.erase(reference.target);
    reference.active = false;
}

void SemanticAnalyzer::popBorrowScope() {
    for (const std::string& referenceName : borrowScopes_.back()) {
        releaseBorrow(referenceName);
        referenceBorrows_.erase(referenceName);
    }
    borrowScopes_.pop_back();
}

ValueType SemanticAnalyzer::inferType(const Expression& expression) const {
    return std::visit([&](const auto& node) -> ValueType {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IntegerExpr>) return ValueType::Int;
        else if constexpr (std::is_same_v<T, DoubleExpr>) return ValueType::Double;
        else if constexpr (std::is_same_v<T, BoolExpr>) return ValueType::Bool;
        else if constexpr (std::is_same_v<T, CharacterExpr>) return ValueType::Char;
        else if constexpr (std::is_same_v<T, StringExpr>) return ValueType::String;
        else if constexpr (std::is_same_v<T, ArrayExpr>) {
            if (node.elements.empty())
                return ValueType(std::make_shared<ValueType>(ValueType::Int), std::size_t{0});
            return ValueType(std::make_shared<ValueType>(inferType(*node.elements.front())),
                             node.elements.size());
        }
        else if constexpr (std::is_same_v<T, IndexExpr>) {
            ValueType arrayType = inferType(*node.array);
            if (arrayType.kind == ValueType::Kind::Reference) arrayType = *arrayType.element;
            return arrayType.kind == ValueType::Kind::Array ||
                arrayType.kind == ValueType::Kind::Slice ? *arrayType.element : ValueType::Int;
        }
        else if constexpr (std::is_same_v<T, AddressExpr>)
            return ValueType(std::make_shared<ValueType>(inferType(*node.operand)),
                             node.mutableBorrow);
        else if constexpr (std::is_same_v<T, DereferenceExpr>) {
            const ValueType reference = inferType(*node.operand);
            return reference.kind == ValueType::Kind::Reference ||
                reference.kind == ValueType::Kind::Box ? *reference.element : ValueType::Int;
        }
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
        else if constexpr (std::is_same_v<T, ArrayExpr>) {
            if (expected.kind != ValueType::Kind::Array)
                return ValueType(std::make_shared<ValueType>(
                    node.elements.empty() ? ValueType::Int : inferType(*node.elements.front())),
                    node.elements.size());
            if (expected.length == 0)
                throw CompileError(expression.location,
                                   "la taille d'un tableau doit être supérieure à zéro");
            if (node.elements.size() != expected.length)
                throw CompileError(expression.location, typeName(expected) + " attend " +
                    std::to_string(expected.length) + " élément(s), reçu " +
                    std::to_string(node.elements.size()));
            for (std::size_t i = 0; i < node.elements.size(); ++i) {
                try {
                    checkExpression(*node.elements[i], *expected.element);
                } catch (const CompileError&) {
                    throw CompileError(node.elements[i]->location, "élément " +
                        std::to_string(i) + " : " + typeName(*expected.element) + " attendu");
                }
            }
            return expected;
        }
        else if constexpr (std::is_same_v<T, IndexExpr>) {
            const ValueType operandType = inferType(*node.array);
            ValueType arrayType = operandType;
            if (arrayType.kind == ValueType::Kind::Reference) arrayType = *arrayType.element;
            if (arrayType.kind != ValueType::Kind::Array &&
                arrayType.kind != ValueType::Kind::Slice)
                throw CompileError(node.array->location,
                                   "l'indexation exige une valeur de type tableau");
            checkExpression(*node.array, operandType);
            checkExpression(*node.index, ValueType::Int);
            if (const auto index = constantInteger(*node.index);
                arrayType.kind == ValueType::Kind::Array && index &&
                (*index < 0 || static_cast<std::uint64_t>(*index) >= arrayType.length)) {
                throw CompileError(node.index->location, "index " + std::to_string(*index) +
                    " hors limites pour " + typeName(arrayType));
            }
            return *arrayType.element;
        }
        else if constexpr (std::is_same_v<T, AddressExpr>) {
            const auto* name = std::get_if<NameExpr>(&node.operand->value);
            const auto* dereference = std::get_if<DereferenceExpr>(&node.operand->value);
            const auto* boxName = dereference == nullptr ? nullptr
                : std::get_if<NameExpr>(&dereference->operand->value);
            if (name == nullptr && boxName == nullptr)
                throw CompileError(expression.location,
                                   "seul un identifiant ou le contenu d'une Box peut être emprunté");
            if (boxName != nullptr) {
                const SemanticSymbol* box = symbols_.lookup(boxName->name);
                if (box == nullptr || box->type.kind != ValueType::Kind::Box)
                    throw CompileError(expression.location,
                                       "l'emprunt déréférencé exige une Box");
                if (node.mutableBorrow && (box->kind != BindingKind::Var || box->parameter))
                    throw CompileError(expression.location,
                                       "'&mut *box' exige un propriétaire 'var'");
                if (movedBoxes_.contains(boxName->name))
                    throw CompileError(expression.location,
                                       "utilisation de '" + boxName->name +
                                       "' après son déplacement");
                dereference->operand->inferredType = box->type;
                dereference->operand->typed = true;
                node.operand->inferredType = *box->type.element;
                node.operand->typed = true;
                return ValueType(std::make_shared<ValueType>(*box->type.element),
                                 node.mutableBorrow);
            }
            const SemanticSymbol* symbol = symbols_.lookup(name->name);
            if (symbol == nullptr || (symbol->kind == BindingKind::Def && symbol->callable))
                throw CompileError(expression.location,
                                   "identifiant non empruntable '" + name->name + "'");
            if (node.mutableBorrow && (symbol->kind != BindingKind::Var || symbol->parameter))
                throw CompileError(expression.location,
                                   "'&mut' exige une variable 'var'");
            node.operand->inferredType = symbol->type;
            node.operand->typed = true;
            return ValueType(std::make_shared<ValueType>(symbol->type), node.mutableBorrow);
        }
        else if constexpr (std::is_same_v<T, DereferenceExpr>) {
            const ValueType reference = inferType(*node.operand);
            if (reference.kind != ValueType::Kind::Reference &&
                reference.kind != ValueType::Kind::Box)
                throw CompileError(expression.location,
                                   "le déréférencement exige une référence");
            checkExpression(*node.operand, reference);
            return *reference.element;
        }
        else if constexpr (std::is_same_v<T, NameExpr>) {
            const SemanticSymbol* symbol = symbols_.lookup(node.name);
            if (symbol == nullptr)
                throw CompileError(expression.location, "identifiant inconnu '" + node.name + "'");
            if (symbol->kind == BindingKind::Def && symbol->callable)
                throw CompileError(expression.location,
                                   "la fonction '" + node.name + "' doit être appelée");
            if (symbol->type.kind == ValueType::Kind::Box && movedBoxes_.contains(node.name))
                throw CompileError(expression.location,
                                   "utilisation de '" + node.name + "' après son déplacement");
            if (const auto borrowed = borrows_.find(node.name);
                borrowed != borrows_.end() && borrowed->second.mutableBorrow)
                throw CompileError(expression.location,
                    "accès direct interdit pendant l'emprunt mutable de '" + node.name + "'");
            return symbol->type;
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            const SemanticSymbol* symbol = symbols_.lookup(node.name);
            if (symbol == nullptr)
                throw CompileError(expression.location, "fonction inconnue '" + node.name + "'");
            if (symbol->kind != BindingKind::Def || !symbol->callable)
                throw CompileError(expression.location, "'" + node.name + "' n'est pas une fonction");
            const Declaration* generic = symbol->declaration != nullptr &&
                !symbol->declaration->typeParameters.empty() ? symbol->declaration : nullptr;
            if (generic == nullptr && !node.typeArguments.empty())
                throw CompileError(expression.location,
                                   "la fonction '" + node.name + "' n'est pas générique");
            if (generic != nullptr && node.typeArguments.empty())
                throw CompileError(expression.location,
                                   "l'appel d'une fonction générique exige une instanciation explicite");
            std::unordered_map<std::string, ValueType> substitutions;
            if (generic != nullptr) {
                if (node.typeArguments.size() != generic->typeParameters.size())
                    throw CompileError(expression.location, "la fonction générique '" + node.name +
                        "' attend " + std::to_string(generic->typeParameters.size()) +
                        " argument(s) de type, reçu " + std::to_string(node.typeArguments.size()));
                for (std::size_t i = 0; i < node.typeArguments.size(); ++i)
                    substitutions.emplace(generic->typeParameters[i], node.typeArguments[i]);
            }
            const std::size_t parameterCount = symbol->declaration != nullptr
                ? symbol->declaration->parameters.size() : symbol->parameterTypes.size();
            if (node.arguments.size() != parameterCount)
                throw CompileError(expression.location, "la fonction '" + node.name + "' attend " +
                    std::to_string(parameterCount) + " argument(s), reçu " +
                    std::to_string(node.arguments.size()));
            std::unordered_map<std::string, BorrowState> callBorrows;
            for (const ExprPtr& argument : node.arguments) {
                const auto* address = std::get_if<AddressExpr>(&argument->value);
                if (address == nullptr) continue;
                const auto* name = std::get_if<NameExpr>(&address->operand->value);
                if (name == nullptr) continue;
                BorrowState& temporary = callBorrows[name->name];
                const auto existing = borrows_.find(name->name);
                if (address->mutableBorrow) {
                    if (temporary.mutableBorrow || temporary.shared != 0 ||
                        (existing != borrows_.end() &&
                         (existing->second.mutableBorrow || existing->second.shared != 0)))
                        throw CompileError(argument->location,
                            "emprunts incompatibles de '" + name->name + "' dans l'appel");
                    temporary.mutableBorrow = true;
                } else {
                    if (temporary.mutableBorrow ||
                        (existing != borrows_.end() && existing->second.mutableBorrow))
                        throw CompileError(argument->location,
                            "emprunt partagé incompatible de '" + name->name + "'");
                    ++temporary.shared;
                }
            }
            for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                const ValueType declaredParameterType = symbol->declaration != nullptr
                    ? symbol->declaration->parameters[i].type : symbol->parameterTypes[i];
                const ValueType parameterType = generic == nullptr ? declaredParameterType
                    : substituteType(declaredParameterType, substitutions);
                checkExpression(*node.arguments[i], parameterType);
                if (parameterType.kind == ValueType::Kind::Box) {
                    if (const auto* moved = std::get_if<NameExpr>(&node.arguments[i]->value))
                        movedBoxes_.insert(moved->name);
                }
            }
            return generic == nullptr ? symbol->type
                                      : substituteType(symbol->type, substitutions);
        } else if constexpr (std::is_same_v<T, ConversionExpr>) {
            if (node.target.kind == ValueType::Kind::Slice) {
                if (expected.kind != ValueType::Kind::Slice ||
                    expected.mutableReference != node.target.mutableReference)
                    return node.target;
                node.target = expected;
            }
            if (node.target.kind == ValueType::Kind::Box) {
                if (expected.kind != ValueType::Kind::Box) return node.target;
                node.target = expected;
            }
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
                if (operands.kind == ValueType::Kind::Array ||
                    operands.kind == ValueType::Kind::Slice)
                    throw CompileError(expression.location,
                                       "les comparaisons de tableaux et slices ne sont pas encore disponibles");
                if (operands.kind == ValueType::Kind::Reference ||
                    operands.kind == ValueType::Kind::Box)
                    throw CompileError(expression.location,
                                       "les comparaisons de références et Box ne sont pas disponibles");
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
            pushBorrowScope();
            checkStatements(node.statements, node.result.get());
            if (node.result) checkExpression(*node.result, expected);
            popBorrowScope();
            symbols_.popScope();
            return expected;
        } else {
            checkExpression(*node.condition, ValueType::Bool);
            const auto beforeBranches = movedBoxes_;
            checkExpression(*node.thenBranch, expected);
            const auto afterThen = movedBoxes_;
            movedBoxes_ = beforeBranches;
            checkExpression(*node.elseBranch, expected);
            const auto afterElse = movedBoxes_;
            if (afterThen != afterElse)
                throw CompileError(expression.location,
                                   "un déplacement de Box conditionnel doit avoir lieu dans toutes les branches");
            movedBoxes_.insert(afterThen.begin(), afterThen.end());
            return expected;
        }
    }, expression.value);

    if (actual != expected) mismatch(expression.location, expected, actual);
    expression.inferredType = actual;
    expression.typed = true;
    return actual;
}
