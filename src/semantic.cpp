#include "semantic.hpp"

#include "diagnostic.hpp"
#include "type_rules.hpp"

#include <algorithm>
#include <type_traits>
#include <unordered_map>

namespace {
bool blockEndsWithTerminator(const BlockExpr& block) {
    if (block.statements.empty()) return false;
    const Statement& statement = *block.statements.back();
    return std::holds_alternative<ReturnStatement>(statement.value) ||
        std::holds_alternative<BreakStatement>(statement.value) ||
        std::holds_alternative<ContinueStatement>(statement.value);
}

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
const StructType* methodOwner(const ValueType& receiver) {
    if (receiver.kind != ValueType::Kind::Reference || receiver.element == nullptr ||
        receiver.element->kind != ValueType::Kind::Struct)
        return nullptr;
    return receiver.element->structure->genericDefinition
        ? receiver.element->structure->genericDefinition.get()
        : receiver.element->structure.get();
}
std::optional<std::pair<std::string, std::string>> methodParts(const std::string& name) {
    const std::size_t separator = name.find('.');
    if (separator == std::string::npos || separator == 0U ||
        separator + 1U == name.size() || name.find('.', separator + 1U) != std::string::npos)
        return std::nullopt;
    return std::pair{name.substr(0, separator), name.substr(separator + 1U)};
}
bool builtinVecMethod(const std::string& name) {
    static const std::unordered_set<std::string> names{
        "push", "pop", "get", "set", "reserve", "clear", "asSlice", "asSliceMut"};
    return names.contains(name);
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
    if (type.kind == ValueType::Kind::Vec)
        return ValueType(ValueType::Kind::Vec,
            std::make_shared<ValueType>(substituteType(*type.element, substitutions)));
    if (type.kind == ValueType::Kind::Enum &&
        !type.enumeration->typeArguments.empty()) {
        std::vector<ValueType> arguments;
        for (const ValueType& argument : type.enumeration->typeArguments)
            arguments.push_back(substituteType(argument, substitutions));
        return ValueType(instantiateEnumType(type.enumeration, std::move(arguments),
                                             type.enumeration->location));
    }
    return type;
}
bool inferTypeArguments(
    const ValueType& pattern, const ValueType& actual,
    std::unordered_map<std::string, ValueType>& substitutions) {
    if (pattern.kind == ValueType::Kind::TypeParameter) {
        const auto found = substitutions.find(pattern.typeParameter);
        if (found == substitutions.end()) {
            substitutions.emplace(pattern.typeParameter, actual);
            return true;
        }
        return found->second == actual;
    }
    if (pattern.kind != actual.kind) return false;
    if (pattern.kind == ValueType::Kind::Array && pattern.length != actual.length) return false;
    if ((pattern.kind == ValueType::Kind::Reference ||
         pattern.kind == ValueType::Kind::Slice) &&
        pattern.mutableReference != actual.mutableReference) return false;
    if (pattern.kind == ValueType::Kind::Array ||
        pattern.kind == ValueType::Kind::Reference ||
        pattern.kind == ValueType::Kind::Slice ||
        pattern.kind == ValueType::Kind::Box ||
        pattern.kind == ValueType::Kind::Vec)
        return inferTypeArguments(*pattern.element, *actual.element, substitutions);
    if (pattern.kind == ValueType::Kind::Enum &&
        pattern.enumeration->genericDefinition && actual.enumeration->genericDefinition ==
            pattern.enumeration->genericDefinition) {
        for (std::size_t i = 0; i < pattern.enumeration->typeArguments.size(); ++i)
            if (!inferTypeArguments(pattern.enumeration->typeArguments[i],
                                    actual.enumeration->typeArguments[i], substitutions))
                return false;
        return true;
    }
    return pattern == actual;
}
bool containsTypeParameter(const ValueType& type) {
    using Kind = ValueType::Kind;
    if (type.kind == Kind::TypeParameter) return true;
    if (type.kind == Kind::Array || type.kind == Kind::Reference ||
        type.kind == Kind::Slice || type.kind == Kind::Box || type.kind == Kind::Vec)
        return containsTypeParameter(*type.element);
    if (type.kind == Kind::Struct)
        return std::any_of(type.structure->typeArguments.begin(),
                           type.structure->typeArguments.end(), containsTypeParameter);
    if (type.kind == Kind::Enum)
        return std::any_of(type.enumeration->typeArguments.begin(),
                           type.enumeration->typeArguments.end(), containsTypeParameter);
    return false;
}
bool isCopyType(const ValueType& type) {
    return isCopyValueType(type);
}
bool satisfiesConstraint(const ValueType& type, const std::string& constraint) {
    if (constraint.empty()) return true;
    if (constraint == "Copy") return isCopyType(type);
    if (constraint == "Numeric") return TypeRules::isNumeric(type);
    if (constraint == "Ordered")
        return TypeRules::isNumeric(type) || type == ValueType::Char;
    if (constraint == "Equatable")
        return isEquatableValueType(type);
    return false;
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
        } else if constexpr (std::is_same_v<T, FieldAssignment>) {
            ++uses[node.name];
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
        } else if constexpr (std::is_same_v<T, StructExpr> ||
                             std::is_same_v<T, EnumExpr>) {
            for (const ExprPtr& field : node.fields) collectExpressionNames(*field, uses);
        } else if constexpr (std::is_same_v<T, FieldExpr>) {
            collectExpressionNames(*node.object, uses);
        } else if constexpr (std::is_same_v<T, MethodCallExpr>) {
            collectExpressionNames(*node.object, uses);
            for (const ExprPtr& argument : node.arguments)
                collectExpressionNames(*argument, uses);
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
            if (node.elseBranch) collectExpressionNames(*node.elseBranch, uses);
        } else if constexpr (std::is_same_v<T, MatchExpr>) {
            collectExpressionNames(*node.operand, uses);
            for (const MatchBranch& branch : node.branches)
                collectExpressionNames(*branch.result, uses);
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
    insideGenericDeclaration_ = false;
    activeTypeConstraints_.clear();
    methods_.clear();
    vecMethods_.clear();
    localMethodOwners_.clear();
    for (const std::shared_ptr<const StructType>& structure : program.structures)
        localMethodOwners_.insert(structure->genericDefinition
            ? structure->genericDefinition.get() : structure.get());
    borrowScopes_ = {{}};
    if (interfaces != nullptr) {
        for (const Program::Import& import : program.imports) {
            const auto module = interfaces->find(import.module);
            if (module == interfaces->end())
                throw CompileError(import.location, "module inconnu '" + import.module + "'");
            for (const auto& [name, exported] : module->second.exports) {
                const std::string qualified = import.module + "." + name;
                const SemanticSymbol symbol{exported.type, exported.kind, exported.callable,
                                            exported.declaration, false,
                                            exported.parameterTypes};
                symbols_.define(qualified, symbol);
                const auto parts = methodParts(name);
                if (!parts || exported.parameterTypes.empty()) continue;
                if (exported.extensionMethod && parts->first == "Vec") {
                    if (builtinVecMethod(parts->second))
                        throw CompileError(import.location, "une extension ne peut pas remplacer "
                                           "la méthode Vec builtin '" + parts->second + "'");
                    if (!vecMethods_.emplace(parts->second,
                            MethodSymbol{qualified, symbol}).second)
                        throw CompileError(import.location, "extension Vec dupliquée '" +
                                           parts->second + "'");
                    continue;
                }
                const StructType* owner = methodOwner(exported.parameterTypes.front());
                if (owner == nullptr || owner->name != parts->first) continue;
                methods_[owner].emplace(parts->second,
                    MethodSymbol{qualified, symbol});
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
        else if constexpr (std::is_same_v<T, FieldAssignment>) checkFieldAssignment(node);
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
    static const std::unordered_set<std::string> knownConstraints{
        "", "Copy", "Numeric", "Ordered", "Equatable"};
    for (std::size_t i = 0; i < declaration.typeConstraints.size(); ++i)
        if (!knownConstraints.contains(declaration.typeConstraints[i]))
            throw CompileError(declaration.location,
                               "contrainte générique inconnue '" +
                               declaration.typeConstraints[i] + "'");
    if (declaration.inferTypeFromInitializer) {
        if (allowRecursion || declaration.callable || declaration.publicSymbol ||
            declaration.nativeSymbol || declaration.initializer == nullptr)
            throw CompileError(declaration.location,
                               "l'inférence de type est réservée aux variables locales");
        if (const auto* array = std::get_if<ArrayExpr>(&declaration.initializer->value);
            array != nullptr && array->elements.empty())
            throw CompileError(declaration.location,
                               "un tableau vide exige une annotation de type");
        declaration.type = inferType(*declaration.initializer);
        if (declaration.type == ValueType::Never)
            throw CompileError(declaration.location,
                               "une liaison ne peut pas avoir le type interne Never");
        if (containsTypeParameter(declaration.type))
            throw CompileError(declaration.location,
                               "l'initialiseur ne permet pas d'inférer un type concret");
    }
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
        const auto* method = std::get_if<MethodCallExpr>(&declaration.initializer->value);
        const auto* vectorName = method == nullptr ? nullptr
            : std::get_if<NameExpr>(&method->object->value);
        const bool vectorView = method != nullptr && vectorName != nullptr &&
            (method->method == "asSlice" || method->method == "asSliceMut");
        if (source == nullptr &&
            (conversion == nullptr || conversion->target.kind != ValueType::Kind::Slice ||
             borrowedName == nullptr) && !vectorView)
            throw CompileError(declaration.location,
                               "une slice doit être créée depuis une collection empruntée");
        const std::string* borrowedOwner = borrowedName != nullptr ? &borrowedName->name
            : vectorView ? &vectorName->name : nullptr;
        if (borrowedOwner != nullptr) {
            const bool mutableView = borrowedName != nullptr
                ? conversion->target.mutableReference : method->method == "asSliceMut";
            BorrowState& state = borrows_[*borrowedOwner];
            if (mutableView) {
                if (state.mutableBorrow || state.shared != 0)
                    throw CompileError(declaration.location,
                                       "emprunt mutable exclusif impossible pour '" +
                                       *borrowedOwner + "'");
                state.mutableBorrow = true;
            } else {
                if (state.mutableBorrow)
                    throw CompileError(declaration.location,
                                       "'" + *borrowedOwner +
                                       "' possède déjà un emprunt mutable");
                ++state.shared;
            }
            referenceBorrows_.insert_or_assign(declaration.name,
                ReferenceBorrow{*borrowedOwner, mutableView});
            borrowScopes_.back().push_back(declaration.name);
        }
    }
    if (declaration.type == ValueType::StringView && !declaration.nativeSymbol) {
        if (declaration.callable)
            throw CompileError(declaration.location,
                               "le retour de StringView n'est pas encore autorisé");
        if (allowRecursion)
            throw CompileError(declaration.location,
                               "une StringView ne peut pas être stockée globalement");
        if (declaration.kind != BindingKind::Val)
            throw CompileError(declaration.location,
                               "une StringView locale doit être déclarée avec 'val'");
    }
    if (declaration.callable && declaration.type.kind == ValueType::Kind::Array)
        throw CompileError(declaration.location,
                           "le retour d'un tableau par valeur n'est pas encore pris en charge");
    if (declaration.callable &&
        (declaration.type.kind == ValueType::Kind::Struct ||
         declaration.type.kind == ValueType::Kind::Enum) &&
        valueTypeSize(declaration.type) > 16U)
        throw CompileError(declaration.location,
                           "le retour ABI d'un agrégat est limité à 16 octets");
    for (const Parameter& parameter : declaration.parameters)
        if (parameter.type.kind == ValueType::Kind::Array)
            throw CompileError(parameter.location,
                               "les paramètres tableaux ne sont pas encore pris en charge");
    const auto method = methodParts(declaration.name);
    if (method) {
        if (!allowRecursion || declaration.kind != BindingKind::Def ||
            !declaration.callable || declaration.nativeSymbol)
            throw CompileError(declaration.location,
                               "une méthode doit être une fonction globale définie en Zeta");
        if (declaration.parameters.empty() || declaration.parameters.front().name != "self")
            throw CompileError(declaration.location,
                               "le premier paramètre d'une méthode doit être 'self'");
        const ValueType& receiver = declaration.parameters.front().type;
        if (declaration.extensionMethod) {
            if (method->first != "Vec" || receiver.kind != ValueType::Kind::Reference ||
                receiver.element == nullptr || receiver.element->kind != ValueType::Kind::Vec)
                throw CompileError(declaration.parameters.front().location,
                    "une extension Vec exige un receveur '&Vec[T]' ou '&mut Vec[T]'");
            if (builtinVecMethod(method->second))
                throw CompileError(declaration.location,
                    "une extension ne peut pas remplacer la méthode Vec builtin '" +
                    method->second + "'");
        } else {
            const StructType* owner = methodOwner(receiver);
            if (owner == nullptr || owner->name != method->first)
                throw CompileError(declaration.parameters.front().location,
                                   "le receveur doit être '&" + method->first +
                                   "' ou '&mut " + method->first + "'");
            if (!localMethodOwners_.contains(owner))
                throw CompileError(declaration.location,
                                   "une méthode doit être déclarée dans le module de son type");
            if (!owner->typeParameters.empty())
                throw CompileError(declaration.location,
                                   "les méthodes de structures génériques sont reportées");
            if (!declaration.typeParameters.empty())
                throw CompileError(declaration.location,
                                   "les méthodes génériques sont réservées aux extensions");
        }
    } else if (declaration.name.find('.') != std::string::npos) {
        throw CompileError(declaration.location, "nom de méthode invalide '" +
                           declaration.name + "'");
    }
    const SemanticSymbol declarationSymbol{declaration.type, declaration.kind,
                                            declaration.callable, &declaration, false, {}};
    if (declaration.callable && allowRecursion &&
        !symbols_.define(declaration.name, declarationSymbol)) {
        throw CompileError(declaration.location,
                           "l'identifiant '" + declaration.name + "' est déjà défini");
    }
    if (method) {
        if (declaration.extensionMethod) {
            if (!vecMethods_.emplace(method->second,
                    MethodSymbol{declaration.name, declarationSymbol}).second)
                throw CompileError(declaration.location,
                                   "extension Vec dupliquée '" + method->second + "'");
        } else {
            const StructType* owner = methodOwner(declaration.parameters.front().type);
            if (!methods_[owner].emplace(method->second,
                    MethodSymbol{declaration.name, declarationSymbol}).second)
                throw CompileError(declaration.location,
                                   "méthode '" + method->second + "' déjà définie pour " +
                                   method->first);
        }
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
    const auto previousTypeConstraints = activeTypeConstraints_;
    insideGenericDeclaration_ = !declaration.typeParameters.empty();
    activeTypeConstraints_.clear();
    for (std::size_t i = 0; i < declaration.typeParameters.size(); ++i)
        activeTypeConstraints_.emplace(declaration.typeParameters[i],
                                       declaration.typeConstraints[i]);
    const auto previousMovedBoxes = movedBoxes_;
    if (declaration.callable)
        for (const Parameter& parameter : declaration.parameters)
            if (isMoveOnlyValueType(parameter.type))
                movedBoxes_.erase(parameter.name);
    if (declaration.callable) returnType_ = declaration.type;
    checkExpression(*declaration.initializer, declaration.type);
    if (!declaration.callable && isMoveOnlyValueType(declaration.type)) {
        if (const auto* source = std::get_if<NameExpr>(&declaration.initializer->value))
            movedBoxes_.insert(source->name);
    }
    returnType_ = previousReturnType;
    insideGenericDeclaration_ = previousGenericDeclaration;
    activeTypeConstraints_ = previousTypeConstraints;
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
    if (isMoveOnlyValueType(target->type))
        throw CompileError(assignment.location,
                           "la réaffectation d'un propriétaire non Copy n'est pas encore autorisée");
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

void SemanticAnalyzer::checkFieldAssignment(FieldAssignment& assignment) {
    const SemanticSymbol* target = symbols_.lookup(assignment.name);
    if (target == nullptr)
        throw CompileError(assignment.location, "identifiant inconnu '" + assignment.name + "'");
    if (target->parameter || target->kind != BindingKind::Var)
        throw CompileError(assignment.location, "la structure '" + assignment.name + "' est immuable");
    if (target->type.kind != ValueType::Kind::Struct)
        throw CompileError(assignment.location, "l'affectation de champ exige une structure");
    for (const StructField& field : target->type.structure->fields)
        if (field.name == assignment.field) {
            checkExpression(*assignment.value, field.type);
            return;
        }
    throw CompileError(assignment.location, "champ inconnu '" + assignment.field + "'");
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
        else if constexpr (std::is_same_v<T, VecExpr>)
            return ValueType(ValueType::Kind::Vec,
                             std::make_shared<ValueType>(node.elementType));
        else if constexpr (std::is_same_v<T, StructExpr>) return ValueType(node.type);
        else if constexpr (std::is_same_v<T, EnumExpr>) return ValueType(node.type);
        else if constexpr (std::is_same_v<T, FieldExpr>) {
            const ValueType object = inferType(*node.object);
            if ((object == ValueType::String || object == ValueType::StringView) &&
                node.field == "lengthBytes")
                return ValueType::Int;
            if ((object == ValueType::String || object == ValueType::StringView) &&
                node.field == "isEmpty")
                return ValueType::Bool;
            if (object.kind == ValueType::Kind::Slice && node.field == "length")
                return ValueType::Int;
            if (object.kind == ValueType::Kind::Vec &&
                (node.field == "length" || node.field == "capacity"))
                return ValueType::Int;
            if (object.kind == ValueType::Kind::Vec && node.field == "isEmpty")
                return ValueType::Bool;
            if (object.kind != ValueType::Kind::Struct) return ValueType::Int;
            for (const StructField& field : object.structure->fields)
                if (field.name == node.field) return field.type;
            return ValueType::Int;
        }
        else if constexpr (std::is_same_v<T, MethodCallExpr>) {
            const ValueType object = inferType(*node.object);
            const ValueType* ownerType = &object;
            if (object.kind == ValueType::Kind::Reference && object.element != nullptr)
                ownerType = object.element.get();
            if (ownerType->kind == ValueType::Kind::Struct) {
                const StructType* owner = ownerType->structure->genericDefinition
                    ? ownerType->structure->genericDefinition.get()
                    : ownerType->structure.get();
                if (const auto methods = methods_.find(owner); methods != methods_.end())
                    if (const auto method = methods->second.find(node.method);
                        method != methods->second.end())
                        return method->second.symbol.type;
            }
            if (ownerType->kind == ValueType::Kind::Vec) {
                if (const auto method = vecMethods_.find(node.method);
                    method != vecMethods_.end()) {
                    const Declaration* declaration = method->second.symbol.declaration;
                    if (declaration == nullptr || declaration->typeParameters.empty())
                        return method->second.symbol.type;
                    std::unordered_map<std::string, ValueType> substitutions;
                    const ValueType actualReceiver(
                        std::make_shared<ValueType>(*ownerType),
                        declaration->parameters.front().type.mutableReference);
                    if (inferTypeArguments(declaration->parameters.front().type,
                                           actualReceiver, substitutions))
                        return substituteType(declaration->type, substitutions);
                }
            }
            if (object.kind == ValueType::Kind::Vec &&
                (node.method == "asSlice" || node.method == "asSliceMut"))
                return ValueType(ValueType::Kind::Slice,
                    std::make_shared<ValueType>(*object.element),
                    node.method == "asSliceMut");
            if (object.kind == ValueType::Kind::Vec &&
                (node.method == "get" || node.method == "pop") &&
                node.optionDefinition != nullptr)
                return ValueType(instantiateEnumType(node.optionDefinition,
                    {*object.element}, expression.location));
            return ValueType::Int;
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
        else if constexpr (std::is_same_v<T, NameExpr>) {
            const SemanticSymbol* symbol = symbols_.lookup(node.name);
            return symbol == nullptr ? ValueType::Int : symbol->type;
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            const SemanticSymbol* symbol = symbols_.lookup(node.name);
            if (symbol == nullptr) return ValueType::Int;
            const Declaration* declaration = symbol->declaration;
            if (declaration == nullptr || declaration->typeParameters.empty()) return symbol->type;
            std::unordered_map<std::string, ValueType> substitutions;
            if (!node.typeArguments.empty()) {
                for (std::size_t i = 0; i < node.typeArguments.size(); ++i)
                    substitutions.emplace(declaration->typeParameters[i], node.typeArguments[i]);
            } else {
                for (std::size_t i = 0; i < node.arguments.size(); ++i)
                    inferTypeArguments(declaration->parameters[i].type,
                                       inferType(*node.arguments[i]), substitutions);
            }
            return substitutions.size() == declaration->typeParameters.size()
                ? substituteType(declaration->type, substitutions) : symbol->type;
        } else if constexpr (std::is_same_v<T, ConversionExpr>) {
            const ValueType source = inferType(*node.operand);
            if (node.target.kind == ValueType::Kind::Slice &&
                source.kind == ValueType::Kind::Reference &&
                source.element->kind == ValueType::Kind::Array)
                return ValueType(ValueType::Kind::Slice,
                    std::make_shared<ValueType>(*source.element->element),
                    node.target.mutableReference);
            if (node.target.kind == ValueType::Kind::Box)
                return ValueType(ValueType::Kind::Box,
                                 std::make_shared<ValueType>(source));
            return node.target;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            return node.op == "!" ? ValueType::Bool : inferType(*node.operand);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            if (TypeRules::isLogical(node.op) || TypeRules::isComparison(node.op))
                return ValueType::Bool;
            return TypeRules::commonOperandType(inferType(*node.left), inferType(*node.right));
        } else if constexpr (std::is_same_v<T, BlockExpr>) {
            return node.result ? inferType(*node.result) :
                blockEndsWithTerminator(node) ? ValueType::Never : ValueType::Unit;
        } else if constexpr (std::is_same_v<T, MatchExpr>) {
            for (const MatchBranch& branch : node.branches) {
                const ValueType branchType = inferType(*branch.result);
                if (branchType != ValueType::Never) return branchType;
            }
            return ValueType::Never;
        } else {
            if (!node.elseBranch) return ValueType::Unit;
            const ValueType thenType = inferType(*node.thenBranch);
            const ValueType elseType = inferType(*node.elseBranch);
            if (thenType == ValueType::Never) return elseType;
            if (elseType == ValueType::Never) return thenType;
            return TypeRules::commonOperandType(thenType, elseType);
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
            if (expected == ValueType::Int || expected == ValueType::Byte ||
                expected == ValueType::Double)
                return expected;
            return ValueType::Int;
        } else if constexpr (std::is_same_v<T, DoubleExpr>) return ValueType::Double;
        else if constexpr (std::is_same_v<T, BoolExpr>) return ValueType::Bool;
        else if constexpr (std::is_same_v<T, CharacterExpr>) return ValueType::Char;
        else if constexpr (std::is_same_v<T, StringExpr>) return ValueType::String;
        else if constexpr (std::is_same_v<T, ArrayExpr>) {
            if (expected.kind != ValueType::Kind::Array)
                return ValueType(std::make_shared<ValueType>(
                    node.elements.empty() ? ValueType::Int : inferType(*node.elements.front())),
                    node.elements.size());
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
        else if constexpr (std::is_same_v<T, VecExpr>) {
            const ValueType type(ValueType::Kind::Vec,
                                 std::make_shared<ValueType>(node.elementType));
            if (valueTypeSize(node.elementType) == 0U)
                throw CompileError(expression.location,
                                   "Vec ne prend pas encore en charge les éléments de taille nulle");
            if (expected.kind == ValueType::Kind::Vec && expected != type)
                mismatch(expression.location, expected, type);
            return type;
        }
        else if constexpr (std::is_same_v<T, StructExpr>) {
            const ValueType type(node.type);
            for (std::size_t i = 0; i < node.fields.size(); ++i)
                checkExpression(*node.fields[i], node.type->fields[i].type);
            return type;
        }
        else if constexpr (std::is_same_v<T, EnumExpr>) {
            const ValueType type(node.type);
            const EnumVariant& variant = node.type->variants[node.variant];
            for (std::size_t i = 0; i < node.fields.size(); ++i) {
                checkExpression(*node.fields[i], variant.fields[i].type);
                if (isMoveOnlyValueType(variant.fields[i].type))
                    if (const auto* moved = std::get_if<NameExpr>(&node.fields[i]->value))
                        movedBoxes_.insert(moved->name);
            }
            return type;
        }
        else if constexpr (std::is_same_v<T, FieldExpr>) {
            const ValueType object = inferType(*node.object);
            if (object == ValueType::String || object == ValueType::StringView) {
                checkExpression(*node.object, object);
                if (node.field == "lengthBytes") return ValueType::Int;
                if (node.field == "isEmpty") return ValueType::Bool;
                throw CompileError(expression.location,
                                   "propriété " + typeName(object) + " inconnue '" +
                                   node.field + "'");
            }
            if (object.kind == ValueType::Kind::Slice && node.field == "length") {
                checkExpression(*node.object, object);
                return ValueType::Int;
            }
            if (object.kind == ValueType::Kind::Vec) {
                checkExpression(*node.object, object);
                if (node.field == "length" || node.field == "capacity")
                    return ValueType::Int;
                if (node.field == "isEmpty") return ValueType::Bool;
                throw CompileError(expression.location,
                                   "propriété Vec inconnue '" + node.field + "'");
            }
            if (object.kind != ValueType::Kind::Struct)
                throw CompileError(node.object->location, "l'accès à un champ exige une structure");
            checkExpression(*node.object, object);
            for (const StructField& field : object.structure->fields)
                if (field.name == node.field) return field.type;
            throw CompileError(expression.location, "champ inconnu '" + node.field +
                "' pour " + typeName(object));
        }
        else if constexpr (std::is_same_v<T, MethodCallExpr>) {
            const ValueType userReceiver = inferType(*node.object);
            const ValueType* userOwnerType = &userReceiver;
            if (userReceiver.kind == ValueType::Kind::Reference &&
                userReceiver.element != nullptr)
                userOwnerType = userReceiver.element.get();
            const MethodSymbol* userMethod = nullptr;
            if (userOwnerType->kind == ValueType::Kind::Struct) {
                const StructType* owner = userOwnerType->structure->genericDefinition
                    ? userOwnerType->structure->genericDefinition.get()
                    : userOwnerType->structure.get();
                const auto ownerMethods = methods_.find(owner);
                if (ownerMethods != methods_.end()) {
                    const auto found = ownerMethods->second.find(node.method);
                    if (found != ownerMethods->second.end()) userMethod = &found->second;
                }
                if (userMethod == nullptr)
                    throw CompileError(expression.location, "méthode inconnue '" +
                                       node.method + "' pour " + typeName(*userOwnerType));
            } else if (userOwnerType->kind == ValueType::Kind::Vec) {
                const auto found = vecMethods_.find(node.method);
                if (found != vecMethods_.end()) userMethod = &found->second;
            }
            if (userMethod != nullptr) {
                const SemanticSymbol& methodSymbol = userMethod->symbol;
                const std::vector<ValueType>* persistedParameters =
                    methodSymbol.declaration == nullptr ? &methodSymbol.parameterTypes : nullptr;
                const std::size_t parameterCount = methodSymbol.declaration != nullptr
                    ? methodSymbol.declaration->parameters.size()
                    : persistedParameters->size();
                if (node.arguments.size() + 1U != parameterCount)
                    throw CompileError(expression.location, "'" + node.method + "' attend " +
                        std::to_string(parameterCount - 1U) + " argument(s), reçu " +
                        std::to_string(node.arguments.size()));
                const Declaration* generic = methodSymbol.declaration != nullptr &&
                    !methodSymbol.declaration->typeParameters.empty()
                    ? methodSymbol.declaration : nullptr;
                std::unordered_map<std::string, ValueType> substitutions;
                const ValueType declaredReceiver = methodSymbol.declaration != nullptr
                    ? methodSymbol.declaration->parameters.front().type
                    : persistedParameters->front();
                if (generic != nullptr) {
                    node.typeArguments.clear();
                    const ValueType actualReceiver = userReceiver.kind == ValueType::Kind::Reference
                        ? userReceiver
                        : ValueType(std::make_shared<ValueType>(*userOwnerType),
                                    declaredReceiver.mutableReference);
                    if (!inferTypeArguments(declaredReceiver, actualReceiver, substitutions))
                        throw CompileError(node.object->location,
                                           "receveur incompatible pour '" + node.method + "'");
                    for (std::size_t i = 0; i < node.arguments.size(); ++i)
                        if (!inferTypeArguments(generic->parameters[i + 1U].type,
                                                inferType(*node.arguments[i]), substitutions))
                            throw CompileError(node.arguments[i]->location,
                                "arguments incompatibles pour l'extension '" +
                                node.method + "'");
                    for (std::size_t i = 0; i < generic->typeParameters.size(); ++i) {
                        const std::string& parameter = generic->typeParameters[i];
                        const auto inferred = substitutions.find(parameter);
                        if (inferred == substitutions.end())
                            throw CompileError(expression.location,
                                "impossible de déduire le paramètre de type '" + parameter + "'");
                        if (!satisfiesConstraint(inferred->second,
                                                 generic->typeConstraints[i]))
                            throw CompileError(expression.location, "le type " +
                                typeName(inferred->second) + " ne satisfait pas la contrainte " +
                                generic->typeConstraints[i]);
                        node.typeArguments.push_back(inferred->second);
                    }
                }
                const ValueType receiverParameter = generic == nullptr
                    ? declaredReceiver : substituteType(declaredReceiver, substitutions);
                if (userReceiver.kind == ValueType::Kind::Reference) {
                    if (userReceiver != receiverParameter)
                        throw CompileError(node.object->location,
                            "receveur " + typeName(receiverParameter) + " attendu, reçu " +
                            typeName(userReceiver));
                    checkExpression(*node.object, receiverParameter);
                } else {
                    const auto* receiverName = std::get_if<NameExpr>(&node.object->value);
                    if (receiverName == nullptr)
                        throw CompileError(node.object->location,
                                           "le receveur d'une méthode doit être un identifiant");
                    const SemanticSymbol* receiverSymbol = symbols_.lookup(receiverName->name);
                    if (receiverSymbol == nullptr)
                        throw CompileError(node.object->location, "receveur inconnu '" +
                                           receiverName->name + "'");
                    if (movedBoxes_.contains(receiverName->name))
                        throw CompileError(node.object->location, "utilisation de '" +
                                           receiverName->name + "' après son déplacement");
                    const bool mutableReceiver = receiverParameter.mutableReference;
                    if (mutableReceiver &&
                        (receiverSymbol->parameter || receiverSymbol->kind != BindingKind::Var))
                        throw CompileError(node.object->location,
                                           "la méthode '" + node.method +
                                           "' exige un receveur déclaré avec 'var'");
                    if (const auto borrowed = borrows_.find(receiverName->name);
                        borrowed != borrows_.end() &&
                        (borrowed->second.mutableBorrow ||
                         (mutableReceiver && borrowed->second.shared != 0)))
                        throw CompileError(node.object->location, "le receveur '" +
                                           receiverName->name + "' est déjà emprunté");
                    ExprPtr receiver = std::move(node.object);
                    node.object = std::make_unique<Expression>(Expression{
                        receiver->location,
                        AddressExpr{mutableReceiver, std::move(receiver)}});
                    checkExpression(*node.object, receiverParameter);
                }
                for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                    const ValueType parameterType = methodSymbol.declaration != nullptr
                        ? methodSymbol.declaration->parameters[i + 1U].type
                        : (*persistedParameters)[i + 1U];
                    const ValueType concreteParameter = generic == nullptr
                        ? parameterType : substituteType(parameterType, substitutions);
                    checkExpression(*node.arguments[i], concreteParameter);
                    if (isMoveOnlyValueType(concreteParameter))
                        if (const auto* moved =
                                std::get_if<NameExpr>(&node.arguments[i]->value))
                            movedBoxes_.insert(moved->name);
                }
                node.resolvedFunction = userMethod->functionName;
                return generic == nullptr ? methodSymbol.type
                                          : substituteType(methodSymbol.type, substitutions);
            }
            const auto* name = std::get_if<NameExpr>(&node.object->value);
            const auto* field = std::get_if<FieldExpr>(&node.object->value);
            const auto* owner = field == nullptr ? nullptr
                : std::get_if<NameExpr>(&field->object->value);
            const SemanticSymbol* symbol = name != nullptr ? symbols_.lookup(name->name)
                : owner != nullptr ? symbols_.lookup(owner->name) : nullptr;
            const ValueType receiverType = symbol == nullptr ? ValueType::Int : symbol->type;
            const bool referenceReceiver = receiverType.kind == ValueType::Kind::Reference &&
                receiverType.element != nullptr &&
                receiverType.element->kind == ValueType::Kind::Vec;
            ValueType vectorType = referenceReceiver ? *receiverType.element : receiverType;
            const bool fieldReceiver = field != nullptr;
            if (fieldReceiver && symbol != nullptr &&
                symbol->type.kind == ValueType::Kind::Struct) {
                const auto found = std::find_if(symbol->type.structure->fields.begin(),
                    symbol->type.structure->fields.end(), [&](const StructField& candidate) {
                        return candidate.name == field->field;
                    });
                if (found != symbol->type.structure->fields.end()) vectorType = found->type;
            }
            if (symbol == nullptr || vectorType.kind != ValueType::Kind::Vec)
                throw CompileError(node.object->location,
                                   "la méthode '" + node.method + "' exige un Vec");
            const std::string& receiverName = name != nullptr ? name->name : owner->name;
            const bool mutationMethod = node.method == "reserve" || node.method == "push" ||
                node.method == "clear" || node.method == "set";
            const bool viewMethod = node.method == "asSlice" || node.method == "asSliceMut";
            if ((fieldReceiver && !mutationMethod) ||
                (referenceReceiver && !mutationMethod && !viewMethod))
                throw CompileError(node.object->location,
                    "la méthode '" + node.method +
                    "' n'est pas encore disponible sur ce receveur Vec");
            if (referenceReceiver && !receiverType.mutableReference &&
                node.method != "asSlice")
                throw CompileError(node.object->location,
                                   "la mutation exige une référence '&mut Vec'");
            if (fieldReceiver)
                checkExpression(*node.object, vectorType);
            else {
                node.object->inferredType = receiverType;
                node.object->typed = true;
            }
            const bool mutableReceiver =
                (referenceReceiver && receiverType.mutableReference) ||
                (!symbol->parameter && symbol->kind == BindingKind::Var);
            if (node.method == "asSlice" || node.method == "asSliceMut") {
                if (!node.arguments.empty())
                    throw CompileError(expression.location, "'" + node.method +
                        "' n'attend aucun argument");
                if (node.method == "asSliceMut" && !mutableReceiver)
                    throw CompileError(node.object->location,
                                       "'asSliceMut' exige un Vec déclaré avec 'var'");
                if (movedBoxes_.contains(receiverName))
                    throw CompileError(node.object->location,
                                       "utilisation de '" + receiverName +
                                       "' après son déplacement");
                return ValueType(ValueType::Kind::Slice,
                    std::make_shared<ValueType>(*vectorType.element),
                    node.method == "asSliceMut");
            }
            if (node.method == "get") {
                if (node.arguments.size() != 1U)
                    throw CompileError(expression.location,
                                       "'get' attend 1 argument");
                if (!isCopyValueType(*vectorType.element))
                    throw CompileError(expression.location,
                                       "'get' exige un type d'élément Copy");
                if (movedBoxes_.contains(receiverName))
                    throw CompileError(node.object->location,
                                       "utilisation de '" + receiverName +
                                       "' après son déplacement");
                if (const auto borrowed = borrows_.find(receiverName);
                    borrowed != borrows_.end() && borrowed->second.mutableBorrow)
                    throw CompileError(node.object->location,
                                       "le Vec '" + receiverName + "' possède un emprunt mutable");
                checkExpression(*node.arguments.front(), ValueType::Int);
                return ValueType(instantiateEnumType(node.optionDefinition,
                    {*vectorType.element}, expression.location));
            }
            if (node.method == "pop" || node.method == "set") {
                if (!mutableReceiver)
                    throw CompileError(node.object->location,
                                       "'" + node.method +
                                       "' exige un Vec déclaré avec 'var'");
                if (const auto borrowed = borrows_.find(receiverName);
                    borrowed != borrows_.end() &&
                    (borrowed->second.mutableBorrow || borrowed->second.shared != 0))
                    throw CompileError(node.object->location,
                                       "le Vec '" + receiverName + "' est emprunté");
                if (movedBoxes_.contains(receiverName))
                    throw CompileError(node.object->location,
                                       "utilisation de '" + receiverName +
                                       "' après son déplacement");
                const std::size_t expectedArguments = node.method == "pop" ? 0U : 2U;
                if (node.arguments.size() != expectedArguments)
                    throw CompileError(expression.location, "'" + node.method + "' attend " +
                        std::to_string(expectedArguments) + " argument(s), reçu " +
                        std::to_string(node.arguments.size()));
                if (node.method == "set") {
                    checkExpression(*node.arguments[0], ValueType::Int);
                    checkExpression(*node.arguments[1], *vectorType.element);
                    if (isMoveOnlyValueType(*vectorType.element))
                        if (const auto* moved =
                                std::get_if<NameExpr>(&node.arguments[1]->value))
                            movedBoxes_.insert(moved->name);
                }
                if (node.method == "set") return ValueType::Int;
                return ValueType(instantiateEnumType(node.optionDefinition,
                    {*vectorType.element}, expression.location));
            }
            if (node.method != "reserve" && node.method != "push" &&
                node.method != "clear")
                throw CompileError(expression.location,
                                   "méthode Vec inconnue '" + node.method + "'");
            if (!mutableReceiver)
                throw CompileError(node.object->location,
                                   "'" + node.method + "' exige un Vec déclaré avec 'var'");
            if (const auto borrowed = borrows_.find(receiverName);
                borrowed != borrows_.end() &&
                (borrowed->second.mutableBorrow || borrowed->second.shared != 0))
                throw CompileError(node.object->location,
                                   "le Vec '" + receiverName + "' est emprunté");
            if (movedBoxes_.contains(receiverName))
                throw CompileError(node.object->location,
                                   "utilisation de '" + receiverName +
                                   "' après son déplacement");
            const std::size_t expectedArguments = node.method == "clear" ? 0U : 1U;
            if (node.arguments.size() != expectedArguments)
                throw CompileError(expression.location, "'" + node.method + "' attend " +
                    std::to_string(expectedArguments) + " argument(s), reçu " +
                    std::to_string(node.arguments.size()));
            if (node.method == "reserve")
                checkExpression(*node.arguments.front(), ValueType::Int);
            else if (node.method == "push") {
                checkExpression(*node.arguments.front(), *vectorType.element);
                if (isMoveOnlyValueType(*vectorType.element))
                    if (const auto* moved =
                            std::get_if<NameExpr>(&node.arguments.front()->value))
                        movedBoxes_.insert(moved->name);
            }
            return ValueType::Int;
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
            if (isMoveOnlyValueType(symbol->type) && movedBoxes_.contains(node.name))
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
            const std::size_t parameterCount = symbol->declaration != nullptr
                ? symbol->declaration->parameters.size() : symbol->parameterTypes.size();
            if (node.arguments.size() != parameterCount)
                throw CompileError(expression.location, "la fonction '" + node.name + "' attend " +
                    std::to_string(parameterCount) + " argument(s), reçu " +
                    std::to_string(node.arguments.size()));
            std::unordered_map<std::string, ValueType> substitutions;
            if (generic != nullptr) {
                if (node.typeArguments.empty()) {
                    for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                        if (!inferTypeArguments(generic->parameters[i].type,
                                                inferType(*node.arguments[i]), substitutions))
                            throw CompileError(node.arguments[i]->location,
                                               "arguments incompatibles pour l'inférence générique de '" +
                                               node.name + "'");
                    }
                    for (const std::string& parameter : generic->typeParameters) {
                        const auto inferred = substitutions.find(parameter);
                        if (inferred == substitutions.end())
                            throw CompileError(expression.location,
                                               "impossible de déduire le paramètre de type '" +
                                               parameter + "'");
                        node.typeArguments.push_back(inferred->second);
                    }
                } else if (node.typeArguments.size() != generic->typeParameters.size())
                    throw CompileError(expression.location, "la fonction générique '" + node.name +
                        "' attend " + std::to_string(generic->typeParameters.size()) +
                        " argument(s) de type, reçu " + std::to_string(node.typeArguments.size()));
                if (substitutions.empty())
                    for (std::size_t i = 0; i < node.typeArguments.size(); ++i)
                        substitutions.emplace(generic->typeParameters[i], node.typeArguments[i]);
                for (std::size_t i = 0; i < generic->typeParameters.size(); ++i) {
                    const ValueType& type = substitutions.at(generic->typeParameters[i]);
                    const std::string& constraint = generic->typeConstraints[i];
                    const auto activeConstraint = type.kind == ValueType::Kind::TypeParameter
                        ? activeTypeConstraints_.find(type.typeParameter)
                        : activeTypeConstraints_.end();
                    const bool propagatedConstraint = activeConstraint !=
                        activeTypeConstraints_.end() && activeConstraint->second == constraint;
                    if (!satisfiesConstraint(type, constraint) && !propagatedConstraint)
                        throw CompileError(expression.location,
                                           "le type " + typeName(type) +
                                           " ne satisfait pas la contrainte " + constraint);
                }
            }
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
                if (isMoveOnlyValueType(parameterType)) {
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
            const bool genericNumeric = expected.kind == ValueType::Kind::TypeParameter &&
                activeTypeConstraints_[expected.typeParameter] == "Numeric";
            if (!TypeRules::isNumeric(expected) && !genericNumeric)
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
                if (operands.kind == ValueType::Kind::TypeParameter) {
                    const std::string& constraint = activeTypeConstraints_[operands.typeParameter];
                    const bool allowed = TypeRules::isOrdering(node.op)
                        ? constraint == "Ordered"
                        : constraint == "Equatable" || constraint == "Ordered" ||
                          constraint == "Numeric";
                    if (!allowed)
                        throw CompileError(expression.location,
                                           "l'opérateur '" + node.op +
                                           "' exige une contrainte générique adaptée");
                }
                if (operands.kind == ValueType::Kind::Array ||
                    operands.kind == ValueType::Kind::Slice)
                    throw CompileError(expression.location,
                                       "les comparaisons de tableaux et slices ne sont pas encore disponibles");
                if (operands.kind == ValueType::Kind::Reference ||
                    operands.kind == ValueType::Kind::Box)
                    throw CompileError(expression.location,
                                       "les comparaisons de références et Box ne sont pas disponibles");
                if (TypeRules::isEquality(node.op) &&
                    !isEquatableValueType(operands) &&
                    operands.kind != ValueType::Kind::TypeParameter)
                    throw CompileError(expression.location,
                                       "le type " + typeName(operands) +
                                       " ne satisfait pas Equatable");
                if (TypeRules::isOrdering(node.op) &&
                    !TypeRules::isNumeric(operands) && operands != ValueType::Char &&
                    operands.kind != ValueType::Kind::TypeParameter)
                    throw CompileError(expression.location,
                                       "seuls '==' et '!=' sont autorisés sur " +
                                           typeName(operands));
                checkExpression(*node.left, operands);
                checkExpression(*node.right, operands);
                return ValueType::Bool;
            }
            const bool genericNumeric = expected.kind == ValueType::Kind::TypeParameter &&
                activeTypeConstraints_[expected.typeParameter] == "Numeric";
            if (expected == ValueType::String && node.op == "+") {
                checkExpression(*node.left, ValueType::String);
                checkExpression(*node.right, ValueType::String);
                return ValueType::String;
            }
            if (!TypeRules::isNumeric(expected) && !genericNumeric)
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
            else if (expected != ValueType::Unit && !blockEndsWithTerminator(node))
                mismatch(expression.location, expected, ValueType::Unit);
            popBorrowScope();
            symbols_.popScope();
            return node.result ? expected :
                blockEndsWithTerminator(node) ? ValueType::Never : ValueType::Unit;
        } else if constexpr (std::is_same_v<T, IfExpr>) {
            checkExpression(*node.condition, ValueType::Bool);
            const auto beforeBranches = movedBoxes_;
            if (!node.elseBranch) {
                if (expected != ValueType::Unit)
                    mismatch(expression.location, expected, ValueType::Unit);
                checkExpression(*node.thenBranch, ValueType::Unit);
                if (movedBoxes_ != beforeBranches)
                    throw CompileError(expression.location,
                        "un déplacement conditionnel doit avoir lieu dans toutes les branches");
                return ValueType::Unit;
            }
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
        } else {
            checkExpression(*node.operand, ValueType(node.type));
            if (isMoveOnlyValueType(ValueType(node.type)))
                if (const auto* moved = std::get_if<NameExpr>(&node.operand->value))
                    movedBoxes_.insert(moved->name);
            const auto beforeBranches = movedBoxes_;
            std::optional<std::unordered_set<std::string>> mergedMoves;
            for (MatchBranch& branch : node.branches) {
                movedBoxes_ = beforeBranches;
                symbols_.pushScope();
                const EnumVariant& variant = node.type->variants[branch.variant];
                for (std::size_t i = 0; i < branch.bindings.size(); ++i) {
                    if (!branch.bindings[i]) continue;
                    if (!symbols_.define(*branch.bindings[i], SemanticSymbol{
                            variant.fields[i].type, BindingKind::Val, false, nullptr, false, {}}))
                        throw CompileError(branch.location, "la liaison '" +
                            *branch.bindings[i] + "' masque un identifiant existant");
                }
                checkExpression(*branch.result, expected);
                symbols_.popScope();
                if (!mergedMoves) mergedMoves = movedBoxes_;
                else if (*mergedMoves != movedBoxes_)
                    throw CompileError(expression.location,
                        "un déplacement non Copy dans un match doit avoir lieu dans toutes les branches");
            }
            movedBoxes_ = mergedMoves.value_or(beforeBranches);
            return expected;
        }
    }, expression.value);

    if (actual != expected && actual != ValueType::Never)
        mismatch(expression.location, expected, actual);
    expression.inferredType = actual;
    expression.typed = true;
    return actual;
}
