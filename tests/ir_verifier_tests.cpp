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

    return failures == 0 ? 0 : 1;
}
