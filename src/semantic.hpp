#pragma once

#include "ast.hpp"
#include "module.hpp"
#include "symbol_table.hpp"
#include <optional>
#include <unordered_map>
#include <unordered_set>

class TypedProgram {
public:
    const Program& ast() const { return program_; }

private:
    friend class SemanticAnalyzer;
    explicit TypedProgram(const Program& program) : program_(program) {}
    const Program& program_;
};

class SemanticAnalyzer {
public:
    TypedProgram analyze(Program& program,
        const std::unordered_map<std::string, ModuleInterface>* interfaces = nullptr,
        bool requireMain = true);

private:
    ValueType checkExpression(Expression& expression, ValueType expected);
    ValueType inferType(const Expression& expression) const;
    void checkStatement(Statement& statement, bool global);
    void checkDeclaration(Declaration& declaration, bool allowRecursion);
    void checkAssignment(Assignment& assignment);
    void checkIndexAssignment(IndexAssignment& assignment);
    void checkFieldAssignment(FieldAssignment& assignment);
    void checkDereferenceAssignment(DereferenceAssignment& assignment);
    void checkLoop(WhileStatement& loop);
    void checkStatements(std::vector<StatementPtr>& statements,
                         const Expression* trailingExpression = nullptr);
    void pushBorrowScope();
    void popBorrowScope();
    void releaseBorrow(const std::string& referenceName);
    bool satisfiesSingleConstraint(const ValueType& type,
                                   std::string_view constraint) const;
    bool satisfiesConstraint(const ValueType& type,
                             const std::string& constraints) const;

    struct BorrowState { std::size_t shared{0}; bool mutableBorrow{false}; };
    struct ReferenceBorrow {
        std::string target;
        bool mutableBorrow;
        bool active{true};
        bool captured{false};
    };
    struct MethodSymbol {
        std::string functionName;
        SemanticSymbol symbol;
    };

    SymbolTable symbols_;
    std::optional<ValueType> returnType_;
    std::size_t loopDepth_{0};
    std::unordered_map<std::string, BorrowState> borrows_;
    std::unordered_map<std::string, ReferenceBorrow> referenceBorrows_;
    std::vector<std::vector<std::string>> borrowScopes_;
    std::unordered_set<std::string> movedBoxes_;
    bool insideGenericDeclaration_{false};
    std::unordered_map<std::string, std::string> activeTypeConstraints_;
    std::unordered_map<const StructType*,
        std::unordered_map<std::string, MethodSymbol>> methods_;
    std::unordered_map<std::string, MethodSymbol> vecMethods_;
    std::unordered_set<const StructType*> localMethodOwners_;
    std::unordered_set<std::string> knownTraits_;
    std::vector<TraitImplementation> traitImplementations_;
};
