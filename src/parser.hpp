#pragma once

#include "ast.hpp"
#include "token.hpp"

#include <vector>
#include <unordered_set>
#include <unordered_map>

class Parser {
public:
    using ImportedStructures =
        std::unordered_map<std::string, std::shared_ptr<const StructType>>;
    using ImportedEnumerations =
        std::unordered_map<std::string, std::shared_ptr<const EnumType>>;

    explicit Parser(std::vector<Token> tokens,
                    ImportedStructures importedStructures = {},
                    ImportedEnumerations importedEnumerations = {},
                    std::string moduleName = {});
    Program parse();

private:
    const Token& peek() const;
    const Token& previous() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    bool checkSeparator() const;
    bool matchSeparator();
    bool startsAssignment() const;
    const Token& consume(TokenKind kind, const std::string& message);
    ValueType consumeType(const std::string& message);
    std::shared_ptr<StructType> structure(bool publicType = false);
    std::shared_ptr<EnumType> enumeration(bool publicType = false);
    TraitDeclaration trait(bool publicTrait = false);
    TraitImplementation traitImplementation();
    std::string traitName();
    std::shared_ptr<const EnumType> instantiateEnumeration(
        const std::shared_ptr<const EnumType>& enumeration, std::vector<ValueType> arguments,
        SourceLocation location);
    ExprPtr enumExpression(SourceLocation location, const std::string& displayName,
                           const std::shared_ptr<const EnumType>& definition);
    std::shared_ptr<const StructType> instantiateStructure(
        const std::shared_ptr<const StructType>& structure, std::vector<ValueType> arguments,
        SourceLocation location);
    ExprPtr structureExpression(SourceLocation location, const std::string& displayName,
                                const std::shared_ptr<const StructType>& definition);
    void skipSeparators();
    void expressionContinuation();
    Statement statement();
    Declaration declaration(BindingKind kind);
    std::string qualifiedName();
    Statement assignment();
    Statement dereferenceAssignment();
    WhileStatement whileStatement();
    std::vector<StatementPtr> loopBody();
    ExprPtr expression();
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr addition();
    ExprPtr multiplication();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();
    ExprPtr blockExpression(SourceLocation location);
    ExprPtr ifExpression(SourceLocation location);
    ExprPtr matchExpression(SourceLocation location);

    std::vector<Token> tokens_;
    std::size_t current_{0};
    std::size_t blockDepth_{0};
    bool publicDeclaration_{false};
    bool nativeDeclaration_{false};
    bool extensionDeclaration_{false};
    std::unordered_set<std::string> activeTypeParameters_;
    std::unordered_map<std::string, std::shared_ptr<StructType>> structures_;
    ImportedStructures importedStructures_;
    ImportedEnumerations importedEnumerations_;
    std::unordered_map<std::string, std::shared_ptr<EnumType>> enumerations_;
    std::unordered_set<std::string> importedModules_;
    bool optionShadowed_{false};
    std::string moduleName_;
};
