#pragma once

#include "ast.hpp"
#include "symbol_table.hpp"

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
    TypedProgram analyze(Program& program);

private:
    ValueType checkExpression(Expression& expression, ValueType expected);
    ValueType inferType(const Expression& expression) const;
    void checkStatement(Statement& statement, bool global);
    void checkDeclaration(Declaration& declaration);
    void checkAssignment(Assignment& assignment);
    void checkLoop(WhileStatement& loop);
    void checkStatements(std::vector<StatementPtr>& statements);

    SymbolTable symbols_;
};
