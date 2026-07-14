#include "codegen.hpp"
#include "ir_verifier.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;

void expectValid(const std::string& name, const IrProgram& program) {
    try {
        IrVerifier::verify(program, IrVerificationMode::ModuleObject);
    } catch (const std::exception& error) {
        std::cerr << name << " : diagnostic inattendu : " << error.what() << '\n';
        ++failures;
    }
}

void expectCode(const std::string& name, const std::string& code,
                const IrProgram& program) {
    try {
        IrVerifier::verify(program, IrVerificationMode::ModuleObject);
        std::cerr << name << " : diagnostic " << code << " attendu\n";
        ++failures;
    } catch (const IrVerificationError& error) {
        if (error.code() != code) {
            std::cerr << name << " : diagnostic " << error.code()
                      << " reçu au lieu de " << code << '\n';
            ++failures;
        }
    } catch (const std::exception& error) {
        std::cerr << name << " : exception non IRV : " << error.what() << '\n';
        ++failures;
    }
}

template<typename Action>
void expectBoundaryCode(const std::string& name, const std::string& code, Action action) {
    try {
        static_cast<void>(action());
        std::cerr << name << " : aucune erreur\n";
        ++failures;
    } catch (const IrVerificationError& error) {
        if (error.code() != code) {
            std::cerr << name << " : attendu " << code << ", reçu " << error.code() << '\n';
            ++failures;
        }
    }
}
}

int main() {
    expectValid("programme objet vide", IrProgram{});

    IrProgram validFunction;
    validFunction.valueTypes.push_back(ValueType::Int);
    validFunction.valueCount = 1;
    validFunction.instructions.push_back(IrFunctionStart{"identity", false, {}});
    validFunction.instructions.push_back(IrParameter{0, 0, 16, ValueType::Int});
    validFunction.instructions.push_back(IrReturn{0, ValueType::Int});
    expectValid("fonction structurellement valide", validFunction);

    IrProgram validUnitFunction;
    validUnitFunction.valueTypes.push_back(ValueType::Unit);
    validUnitFunction.valueCount = 1;
    validUnitFunction.instructions.push_back(IrFunctionStart{"effect", false, {}});
    validUnitFunction.instructions.push_back(IrUnit{0});
    validUnitFunction.instructions.push_back(IrReturn{0, ValueType::Unit});
    expectValid("fonction Unit structurellement valide", validUnitFunction);

    const ValueType intVector(ValueType::Kind::Vec,
        std::make_shared<ValueType>(ValueType::Int));
    auto vecOwnerDefinition = std::make_shared<StructType>();
    vecOwnerDefinition->name = "VecOwner";
    vecOwnerDefinition->fields.push_back(StructField{{}, "values", intVector, 0});
    vecOwnerDefinition->size = 24;
    vecOwnerDefinition->alignment = 8;
    const ValueType vecOwner(vecOwnerDefinition);

    IrProgram validVecField;
    validVecField.slots.push_back(IrSlot{"owner", vecOwner, true});
    validVecField.valueTypes = {ValueType::Int, ValueType::Int};
    validVecField.valueCount = 2;
    validVecField.instructions.push_back(IrConst{0, 4, ValueType::Int});
    validVecField.instructions.push_back(IrVecReserve{
        1, IrVecMutationTarget{0, std::nullopt, 0}, 0, intVector});
    expectValid("mutation Vec sur champ valide", validVecField);

    IrProgram invalidVecField = validVecField;
    invalidVecField.instructions.back() = IrVecReserve{
        1, IrVecMutationTarget{0, std::nullopt, 1}, 0, intVector};
    expectCode("champ Vec hors limites", "IRV031", invalidVecField);

    const ValueType mutableVecReference(
        std::make_shared<ValueType>(intVector), true);
    IrProgram validVecReference;
    validVecReference.valueTypes = {mutableVecReference, ValueType::Int, ValueType::Int};
    validVecReference.valueCount = 3;
    validVecReference.instructions.push_back(
        IrFunctionStart{"mutate_vec", false, {}});
    validVecReference.instructions.push_back(
        IrParameter{0, 0, 16, mutableVecReference});
    validVecReference.instructions.push_back(IrConst{1, 4, ValueType::Int});
    validVecReference.instructions.push_back(IrVecReserve{
        2, IrVecMutationTarget{std::nullopt, 0, std::nullopt}, 1, intVector});
    validVecReference.instructions.push_back(IrReturn{2, ValueType::Int});
    expectValid("mutation Vec par référence mutable", validVecReference);

    IrProgram ambiguousVecReference = validVecReference;
    ambiguousVecReference.slots.push_back(IrSlot{"values", intVector, true});
    ambiguousVecReference.instructions[3] = IrVecReserve{
        2, IrVecMutationTarget{0, 0, std::nullopt}, 1, intVector};
    expectCode("cible Vec ambiguë", "IRV040", ambiguousVecReference);

    IrProgram wrongUnitOutput;
    wrongUnitOutput.valueTypes.push_back(ValueType::Int);
    wrongUnitOutput.valueCount = 1;
    wrongUnitOutput.instructions.push_back(IrUnit{0});
    expectCode("sortie Unit mal typée", "IRV021", wrongUnitOutput);

    IrProgram countMismatch;
    countMismatch.valueCount = 1;
    expectCode("table de valeurs incohérente", "IRV001", countMismatch);

    IrProgram missingElement;
    missingElement.valueTypes.emplace_back(
        std::shared_ptr<const ValueType>{}, std::size_t{2});
    missingElement.valueCount = 1;
    expectCode("élément de tableau absent", "IRV002", missingElement);

    IrProgram missingDefinition;
    missingDefinition.slots.push_back(
        IrSlot{"broken", ValueType(std::shared_ptr<const StructType>{}), false});
    expectCode("définition de structure absente", "IRV002", missingDefinition);

    IrProgram typeParameter;
    typeParameter.valueTypes.emplace_back(ValueType::Kind::TypeParameter, "T");
    typeParameter.valueCount = 1;
    expectCode("paramètre générique direct", "IRV003", typeParameter);

    IrProgram nestedTypeParameter;
    nestedTypeParameter.slots.push_back(IrSlot{
        "items",
        ValueType(ValueType::Kind::Vec,
                  std::make_shared<const ValueType>(ValueType::Kind::TypeParameter, "E")),
        false});
    expectCode("paramètre générique imbriqué", "IRV003", nestedTypeParameter);

    IrProgram mismatchedExitValue;
    mismatchedExitValue.valueTypes = {ValueType::Int, ValueType::Int};
    mismatchedExitValue.valueCount = 2;
    mismatchedExitValue.exitValue = 1;
    mismatchedExitValue.instructions.push_back(IrConst{0, 0, ValueType::Int});
    mismatchedExitValue.instructions.push_back(IrConst{1, 1, ValueType::Int});
    mismatchedExitValue.instructions.push_back(IrExit{0});
    expectCode("métadonnée exitValue incohérente", "IRV004", mismatchedExitValue);

    IrProgram invalidFunction;
    invalidFunction.instructions.push_back(IrFunctionStart{"", false, {}});
    expectCode("nom de fonction vide", "IRV010", invalidFunction);

    IrProgram duplicateFunction;
    duplicateFunction.instructions.push_back(IrFunctionStart{"same", false, {}});
    duplicateFunction.instructions.push_back(IrFunctionStart{"same", false, {}});
    expectCode("fonction dupliquée", "IRV010", duplicateFunction);

    IrProgram outputOutOfBounds;
    outputOutOfBounds.valueTypes.push_back(ValueType::Int);
    outputOutOfBounds.valueCount = 1;
    outputOutOfBounds.instructions.push_back(IrConst{1, 0, ValueType::Int});
    expectCode("sortie hors limites", "IRV020", outputOutOfBounds);

    IrProgram readOutOfBounds;
    readOutOfBounds.valueTypes.push_back(ValueType::Int);
    readOutOfBounds.valueCount = 1;
    readOutOfBounds.instructions.push_back(IrExit{1});
    expectCode("lecture hors limites", "IRV020", readOutOfBounds);

    IrProgram slotOutOfBounds;
    slotOutOfBounds.valueTypes.push_back(ValueType::Int);
    slotOutOfBounds.valueCount = 1;
    slotOutOfBounds.instructions.push_back(IrLoad{0, 0, ValueType::Int});
    expectCode("slot hors limites", "IRV030", slotOutOfBounds);

    IrProgram duplicateLabel;
    duplicateLabel.instructions.push_back(IrLabel{7});
    duplicateLabel.instructions.push_back(IrLabel{7});
    expectCode("label dupliqué", "IRV050", duplicateLabel);

    IrProgram missingLabel;
    missingLabel.instructions.push_back(IrJump{9});
    expectCode("label absent", "IRV050", missingLabel);

    IrProgram crossRegionValue;
    crossRegionValue.valueTypes.push_back(ValueType::Int);
    crossRegionValue.valueCount = 1;
    crossRegionValue.instructions.push_back(IrConst{0, 0, ValueType::Int});
    crossRegionValue.instructions.push_back(IrFunctionStart{"read", false, {}});
    crossRegionValue.instructions.push_back(IrReturn{0, ValueType::Int});
    expectCode("valeur d'une autre région", "IRV012", crossRegionValue);

    IrProgram crossRegionSlot;
    crossRegionSlot.slots.push_back(IrSlot{"local", ValueType::Int, false});
    crossRegionSlot.valueTypes = {ValueType::Int, ValueType::Int};
    crossRegionSlot.valueCount = 2;
    crossRegionSlot.instructions.push_back(IrFunctionStart{"first", false, {}});
    crossRegionSlot.instructions.push_back(IrLoad{0, 0, ValueType::Int});
    crossRegionSlot.instructions.push_back(IrFunctionStart{"second", false, {}});
    crossRegionSlot.instructions.push_back(IrLoad{1, 0, ValueType::Int});
    expectCode("slot dans deux régions", "IRV013", crossRegionSlot);

    IrProgram crossRegionLabel;
    crossRegionLabel.instructions.push_back(IrLabel{3});
    crossRegionLabel.instructions.push_back(IrFunctionStart{"jump", false, {}});
    crossRegionLabel.instructions.push_back(IrJump{3});
    expectCode("label d'une autre région", "IRV050", crossRegionLabel);

    IrProgram parameterOutsideFunction;
    parameterOutsideFunction.valueTypes.push_back(ValueType::Int);
    parameterOutsideFunction.valueCount = 1;
    parameterOutsideFunction.instructions.push_back(
        IrParameter{0, 0, 16, ValueType::Int});
    expectCode("paramètre hors fonction", "IRV011", parameterOutsideFunction);

    IrProgram parameterAfterBody;
    parameterAfterBody.valueTypes = {ValueType::Int, ValueType::Int};
    parameterAfterBody.valueCount = 2;
    parameterAfterBody.instructions.push_back(IrFunctionStart{"late", false, {}});
    parameterAfterBody.instructions.push_back(IrConst{0, 1, ValueType::Int});
    parameterAfterBody.instructions.push_back(IrParameter{1, 0, 16, ValueType::Int});
    expectCode("paramètre après le corps", "IRV011", parameterAfterBody);

    IrProgram wrongParameterIndex;
    wrongParameterIndex.valueTypes.push_back(ValueType::Int);
    wrongParameterIndex.valueCount = 1;
    wrongParameterIndex.instructions.push_back(IrFunctionStart{"index", false, {}});
    wrongParameterIndex.instructions.push_back(IrParameter{0, 1, 16, ValueType::Int});
    expectCode("index de paramètre invalide", "IRV011", wrongParameterIndex);

    IrProgram wrongParameterOffset;
    wrongParameterOffset.valueTypes = {ValueType::Int, ValueType::String};
    wrongParameterOffset.valueCount = 2;
    wrongParameterOffset.instructions.push_back(IrFunctionStart{"offset", false, {}});
    wrongParameterOffset.instructions.push_back(IrParameter{0, 0, 16, ValueType::Int});
    wrongParameterOffset.instructions.push_back(IrParameter{1, 1, 32, ValueType::String});
    expectCode("offset de paramètre invalide", "IRV011", wrongParameterOffset);

    IrProgram undefinedValue;
    undefinedValue.valueTypes.push_back(ValueType::Int);
    undefinedValue.valueCount = 1;
    undefinedValue.instructions.push_back(IrFunctionStart{"undefined", false, {}});
    undefinedValue.instructions.push_back(IrReturn{0, ValueType::Int});
    expectCode("valeur sans producteur", "IRV023", undefinedValue);

    IrProgram useBeforeDefinition;
    useBeforeDefinition.valueTypes.push_back(ValueType::Int);
    useBeforeDefinition.valueCount = 1;
    useBeforeDefinition.instructions.push_back(IrFunctionStart{"before", false, {}});
    useBeforeDefinition.instructions.push_back(IrReturn{0, ValueType::Int});
    useBeforeDefinition.instructions.push_back(IrConst{0, 1, ValueType::Int});
    expectCode("usage avant définition", "IRV023", useBeforeDefinition);

    IrProgram duplicateDefinition;
    duplicateDefinition.valueTypes.push_back(ValueType::Int);
    duplicateDefinition.valueCount = 1;
    duplicateDefinition.instructions.push_back(IrFunctionStart{"duplicate", false, {}});
    duplicateDefinition.instructions.push_back(IrConst{0, 1, ValueType::Int});
    duplicateDefinition.instructions.push_back(IrConst{0, 2, ValueType::Int});
    expectCode("définition non Copy dupliquée", "IRV022", duplicateDefinition);

    IrProgram validPhi;
    validPhi.valueTypes = {
        ValueType::Bool, ValueType::Int, ValueType::Int, ValueType::Int};
    validPhi.valueCount = 4;
    validPhi.instructions.push_back(IrFunctionStart{"phi", false, {}});
    validPhi.instructions.push_back(IrConst{0, 1, ValueType::Bool});
    validPhi.instructions.push_back(IrBranch{0, false, 10});
    validPhi.instructions.push_back(IrConst{1, 11, ValueType::Int});
    validPhi.instructions.push_back(IrCopy{3, 1, ValueType::Int});
    validPhi.instructions.push_back(IrJump{11});
    validPhi.instructions.push_back(IrLabel{10});
    validPhi.instructions.push_back(IrConst{2, 22, ValueType::Int});
    validPhi.instructions.push_back(IrCopy{3, 2, ValueType::Int});
    validPhi.instructions.push_back(IrLabel{11});
    validPhi.instructions.push_back(IrReturn{3, ValueType::Int});
    expectValid("fusion conditionnelle complète", validPhi);

    IrProgram incompletePhi = validPhi;
    incompletePhi.instructions.erase(incompletePhi.instructions.begin() + 8);
    expectCode("fusion conditionnelle incomplète", "IRV023", incompletePhi);

    IrProgram validShortCircuit;
    validShortCircuit.valueTypes = {
        ValueType::Bool, ValueType::Bool, ValueType::Bool};
    validShortCircuit.valueCount = 3;
    validShortCircuit.instructions.push_back(IrFunctionStart{"short", false, {}});
    validShortCircuit.instructions.push_back(IrConst{0, 0, ValueType::Bool});
    validShortCircuit.instructions.push_back(IrCopy{2, 0, ValueType::Bool});
    validShortCircuit.instructions.push_back(IrBranch{0, false, 20});
    validShortCircuit.instructions.push_back(IrConst{1, 1, ValueType::Bool});
    validShortCircuit.instructions.push_back(IrCopy{2, 1, ValueType::Bool});
    validShortCircuit.instructions.push_back(IrLabel{20});
    validShortCircuit.instructions.push_back(IrReturn{2, ValueType::Bool});
    expectValid("court-circuit avec réécriture IrCopy", validShortCircuit);

    IrProgram validLoopPhi;
    validLoopPhi.valueTypes = {
        ValueType::Bool, ValueType::Int, ValueType::Int, ValueType::Int};
    validLoopPhi.valueCount = 4;
    validLoopPhi.instructions.push_back(IrFunctionStart{"loop_phi", false, {}});
    validLoopPhi.instructions.push_back(IrLabel{21});
    validLoopPhi.instructions.push_back(IrConst{0, 0, ValueType::Bool});
    validLoopPhi.instructions.push_back(IrBranch{0, false, 22});
    validLoopPhi.instructions.push_back(IrConst{1, 1, ValueType::Int});
    validLoopPhi.instructions.push_back(IrCopy{3, 1, ValueType::Int});
    validLoopPhi.instructions.push_back(IrJump{21});
    validLoopPhi.instructions.push_back(IrLabel{22});
    validLoopPhi.instructions.push_back(IrConst{2, 2, ValueType::Int});
    validLoopPhi.instructions.push_back(IrCopy{3, 2, ValueType::Int});
    validLoopPhi.instructions.push_back(IrReturn{3, ValueType::Int});
    expectValid("pseudo-phi réécrit après une arête de boucle", validLoopPhi);

    IrProgram nonExclusivePhi;
    nonExclusivePhi.valueTypes = {ValueType::Int, ValueType::Int, ValueType::Int};
    nonExclusivePhi.valueCount = 3;
    nonExclusivePhi.instructions.push_back(IrFunctionStart{"non_exclusive", false, {}});
    nonExclusivePhi.instructions.push_back(IrConst{0, 1, ValueType::Int});
    nonExclusivePhi.instructions.push_back(IrConst{1, 2, ValueType::Int});
    nonExclusivePhi.instructions.push_back(IrCopy{2, 0, ValueType::Int});
    nonExclusivePhi.instructions.push_back(IrCopy{2, 1, ValueType::Int});
    nonExclusivePhi.instructions.push_back(IrReturn{2, ValueType::Int});
    expectCode("pseudo-phi non exclusif", "IRV024", nonExclusivePhi);

    IrProgram wrongOutputType;
    wrongOutputType.valueTypes.push_back(ValueType::Int);
    wrongOutputType.valueCount = 1;
    wrongOutputType.instructions.push_back(IrConst{0, 1, ValueType::Bool});
    expectCode("type de sortie incohérent", "IRV021", wrongOutputType);

    IrProgram wrongSlotType;
    wrongSlotType.slots.push_back(IrSlot{"flag", ValueType::Bool, false});
    wrongSlotType.valueTypes.push_back(ValueType::Int);
    wrongSlotType.valueCount = 1;
    wrongSlotType.instructions.push_back(IrFunctionStart{"slot", false, {}});
    wrongSlotType.instructions.push_back(IrLoad{0, 0, ValueType::Int});
    expectCode("type de slot incohérent", "IRV031", wrongSlotType);

    IrProgram invalidExternalSlot;
    invalidExternalSlot.slots.push_back(
        IrSlot{"external_local", ValueType::Int, false, true});
    expectCode("slot externe non global", "IRV032", invalidExternalSlot);

    IrProgram wrongOperandType;
    wrongOperandType.valueTypes = {ValueType::Int, ValueType::String};
    wrongOperandType.valueCount = 2;
    wrongOperandType.instructions.push_back(IrConst{0, 1, ValueType::Int});
    wrongOperandType.instructions.push_back(IrStringLength{1, 0});
    expectCode("type d'opérande incohérent", "IRV040", wrongOperandType);

    IrProgram unknownOperator;
    unknownOperator.valueTypes = {ValueType::Int, ValueType::Int, ValueType::Int};
    unknownOperator.valueCount = 3;
    unknownOperator.instructions.push_back(IrConst{0, 1, ValueType::Int});
    unknownOperator.instructions.push_back(IrConst{1, 2, ValueType::Int});
    unknownOperator.instructions.push_back(
        IrBinary{2, "%", 0, 1, ValueType::Int, ValueType::Int});
    expectCode("opérateur inconnu", "IRV041", unknownOperator);

    IrProgram invalidCallArity;
    invalidCallArity.valueTypes = {ValueType::Int, ValueType::Int};
    invalidCallArity.valueCount = 2;
    invalidCallArity.instructions.push_back(IrConst{0, 1, ValueType::Int});
    invalidCallArity.instructions.push_back(
        IrCall{1, "external", {0}, {}, ValueType::Int});
    expectCode("arité de types d'appel invalide", "IRV042", invalidCallArity);

    IrProgram invalidInternalCall;
    invalidInternalCall.valueTypes = {
        ValueType::Int, ValueType::Int, ValueType::Bool, ValueType::Int};
    invalidInternalCall.valueCount = 4;
    invalidInternalCall.instructions.push_back(IrConst{0, 1, ValueType::Int});
    invalidInternalCall.instructions.push_back(
        IrCall{1, "target", {0}, {ValueType::Int}, ValueType::Int});
    invalidInternalCall.instructions.push_back(IrFunctionStart{"target", false, {}});
    invalidInternalCall.instructions.push_back(IrParameter{2, 0, 16, ValueType::Bool});
    invalidInternalCall.instructions.push_back(IrConst{3, 0, ValueType::Int});
    invalidInternalCall.instructions.push_back(IrReturn{3, ValueType::Int});
    expectCode("signature d'appel interne invalide", "IRV043", invalidInternalCall);

    IrProgram invalidAggregate;
    const ValueType pairArray(std::make_shared<const ValueType>(ValueType::Int),
                              std::size_t{2});
    invalidAggregate.valueTypes = {ValueType::Int, pairArray};
    invalidAggregate.valueCount = 2;
    invalidAggregate.instructions.push_back(IrConst{0, 1, ValueType::Int});
    invalidAggregate.instructions.push_back(IrArrayConstruct{1, {0}, pairArray});
    expectCode("arité d'agrégat invalide", "IRV044", invalidAggregate);

    IrProgram invalidIndexFlags;
    const ValueType intArray(std::make_shared<const ValueType>(ValueType::Int),
                             std::size_t{1});
    invalidIndexFlags.valueTypes = {
        ValueType::Int, intArray, ValueType::Int, ValueType::Int};
    invalidIndexFlags.valueCount = 4;
    invalidIndexFlags.instructions.push_back(IrConst{0, 7, ValueType::Int});
    invalidIndexFlags.instructions.push_back(IrArrayConstruct{1, {0}, intArray});
    invalidIndexFlags.instructions.push_back(IrConst{2, 0, ValueType::Int});
    invalidIndexFlags.instructions.push_back(
        IrIndexLoad{3, 1, 2, intArray, true, true});
    expectCode("drapeaux d'indexation invalides", "IRV045", invalidIndexFlags);

    IrProgram invalidBranchCondition;
    invalidBranchCondition.valueTypes.push_back(ValueType::Int);
    invalidBranchCondition.valueCount = 1;
    invalidBranchCondition.instructions.push_back(IrConst{0, 1, ValueType::Int});
    invalidBranchCondition.instructions.push_back(IrBranch{0, true, 30});
    invalidBranchCondition.instructions.push_back(IrLabel{30});
    expectCode("condition de branche invalide", "IRV051", invalidBranchCondition);

    IrProgram instructionAfterReturn;
    instructionAfterReturn.valueTypes = {ValueType::Int, ValueType::Int};
    instructionAfterReturn.valueCount = 2;
    instructionAfterReturn.instructions.push_back(IrFunctionStart{"after", false, {}});
    instructionAfterReturn.instructions.push_back(IrConst{0, 0, ValueType::Int});
    instructionAfterReturn.instructions.push_back(IrReturn{0, ValueType::Int});
    instructionAfterReturn.instructions.push_back(IrConst{1, 1, ValueType::Int});
    expectCode("instruction après retour", "IRV052", instructionAfterReturn);

    IrProgram unterminatedFunction;
    unterminatedFunction.valueTypes.push_back(ValueType::Int);
    unterminatedFunction.valueCount = 1;
    unterminatedFunction.instructions.push_back(IrFunctionStart{"open", false, {}});
    unterminatedFunction.instructions.push_back(IrConst{0, 0, ValueType::Int});
    expectCode("fonction sans terminaison", "IRV053", unterminatedFunction);

    IrProgram infiniteFunction;
    infiniteFunction.instructions.push_back(IrFunctionStart{"infinite", false, {}});
    infiniteFunction.instructions.push_back(IrLabel{31});
    infiniteFunction.instructions.push_back(IrJump{31});
    expectCode("fonction sans chemin terminal", "IRV053", infiniteFunction);

    IrProgram exitInModule;
    exitInModule.valueTypes.push_back(ValueType::Int);
    exitInModule.valueCount = 1;
    exitInModule.instructions.push_back(IrConst{0, 0, ValueType::Int});
    exitInModule.instructions.push_back(IrExit{0});
    expectCode("exit dans un objet module", "IRV054", exitInModule);

    IrProgram invalidBoundary;
    invalidBoundary.valueCount = 1;
    expectBoundaryCode("frontière printer", "IRV001", [&] {
        return IrGenerator::print(invalidBoundary, IrVerificationMode::ModuleObject);
    });
    expectBoundaryCode("frontière exécutable", "IRV001", [&] {
        return FasmCodeGenerator::generate(invalidBoundary);
    });
    expectBoundaryCode("frontière objet", "IRV001", [&] {
        return FasmCodeGenerator::generateObject(invalidBoundary, false, "invalid");
    });
    const IrProgram emptyModule;
    const VerifiedIrProgram verifiedModule =
        IrVerifier::verify(emptyModule, IrVerificationMode::ModuleObject);
    expectBoundaryCode("mode validé incompatible", "IRV004", [&] {
        return FasmCodeGenerator::generate(verifiedModule);
    });

    return failures == 0 ? 0 : 1;
}
