#include "ast.hpp"

#include <algorithm>
#include <stdexcept>

namespace {
ValueType substitute(
    const ValueType& type,
    const std::vector<std::string>& parameters,
    const std::vector<ValueType>& arguments) {
    if (type.kind == ValueType::Kind::TypeParameter) {
        const auto parameter = std::find(parameters.begin(), parameters.end(), type.typeParameter);
        if (parameter == parameters.end()) return type;
        return arguments[static_cast<std::size_t>(parameter - parameters.begin())];
    }
    if (type.kind == ValueType::Kind::Array)
        return ValueType(std::make_shared<ValueType>(substitute(
            *type.element, parameters, arguments)), type.length);
    if (type.kind == ValueType::Kind::Reference)
        return ValueType(std::make_shared<ValueType>(substitute(
            *type.element, parameters, arguments)), type.mutableReference);
    if (type.kind == ValueType::Kind::Slice)
        return ValueType(ValueType::Kind::Slice,
            std::make_shared<ValueType>(substitute(*type.element, parameters, arguments)),
            type.mutableReference);
    if (type.kind == ValueType::Kind::Box)
        return ValueType(ValueType::Kind::Box,
            std::make_shared<ValueType>(substitute(*type.element, parameters, arguments)));
    if (type.kind == ValueType::Kind::Vec)
        return ValueType(ValueType::Kind::Vec,
            std::make_shared<ValueType>(substitute(*type.element, parameters, arguments)));
    return type;
}

bool symbolic(const ValueType& type) {
    if (type.kind == ValueType::Kind::TypeParameter) return true;
    if (type.kind == ValueType::Kind::Array || type.kind == ValueType::Kind::Reference ||
        type.kind == ValueType::Kind::Slice || type.kind == ValueType::Kind::Box ||
        type.kind == ValueType::Kind::Vec)
        return symbolic(*type.element);
    return false;
}
}

std::shared_ptr<const StructType> instantiateStructType(
    const std::shared_ptr<const StructType>& structure,
    std::vector<ValueType> arguments,
    SourceLocation location) {
    const auto definition = structure->genericDefinition
        ? structure->genericDefinition : structure;
    if (arguments.size() != definition->typeParameters.size())
        throw std::runtime_error("arité invalide pour la structure " + definition->name);
    if (arguments.empty()) return definition;

    std::string key = definition->name;
    for (const ValueType& argument : arguments) key += "[" + typeName(argument) + "]";
    if (const auto cached = definition->instances.find(key);
        cached != definition->instances.end())
        if (const auto instance = cached->second.lock()) return instance;

    auto instance = std::make_shared<StructType>();
    instance->location = location;
    instance->name = definition->name;
    instance->publicType = definition->publicType;
    instance->typeArguments = std::move(arguments);
    instance->genericDefinition = definition;
    for (const StructField& sourceField : definition->fields) {
        const ValueType type = substitute(sourceField.type, definition->typeParameters,
                                          instance->typeArguments);
        const std::size_t alignment = valueTypeAlignment(type);
        const std::size_t offset = (instance->size + alignment - 1U) /
                                   alignment * alignment;
        instance->fields.push_back(StructField{
            sourceField.location, sourceField.name, type, offset});
        instance->size = offset + valueTypeSize(type);
        instance->alignment = std::max(instance->alignment, alignment);
    }
    instance->size = (instance->size + instance->alignment - 1U) /
                     instance->alignment * instance->alignment;
    definition->instances.insert_or_assign(std::move(key), instance);
    return instance;
}

std::shared_ptr<const EnumType> instantiateEnumType(
    const std::shared_ptr<const EnumType>& enumeration,
    std::vector<ValueType> arguments,
    SourceLocation location) {
    const auto definition = enumeration->genericDefinition
        ? enumeration->genericDefinition : enumeration;
    if (arguments.size() != definition->typeParameters.size())
        throw std::runtime_error("arité invalide pour l'énumération " + definition->name);
    if (arguments.empty()) return definition;

    std::string key = definition->name;
    for (const ValueType& argument : arguments) key += "[" + typeName(argument) + "]";
    if (const auto cached = definition->instances.find(key);
        cached != definition->instances.end())
        if (const auto instance = cached->second.lock()) return instance;

    auto instance = std::make_shared<EnumType>();
    instance->location = location;
    instance->name = definition->name;
    instance->publicType = definition->publicType;
    instance->typeArguments = std::move(arguments);
    instance->genericDefinition = definition;
    std::size_t maximumPayloadSize = 0;
    std::size_t maximumPayloadAlignment = 1;
    for (const EnumVariant& sourceVariant : definition->variants) {
        EnumVariant variant{sourceVariant.location, sourceVariant.name, {}};
        for (const StructField& sourceField : sourceVariant.fields) {
            const ValueType type = substitute(sourceField.type, definition->typeParameters,
                                              instance->typeArguments);
            const std::size_t alignment = symbolic(type) ? 1U : valueTypeAlignment(type);
            const std::size_t offset =
                (variant.payloadSize + alignment - 1U) / alignment * alignment;
            variant.fields.push_back(StructField{sourceField.location, sourceField.name,
                                                 type, offset});
            variant.payloadSize = offset + (symbolic(type) ? 0U : valueTypeSize(type));
            variant.payloadAlignment = std::max(variant.payloadAlignment, alignment);
        }
        variant.payloadSize = (variant.payloadSize + variant.payloadAlignment - 1U) /
                              variant.payloadAlignment * variant.payloadAlignment;
        maximumPayloadSize = std::max(maximumPayloadSize, variant.payloadSize);
        maximumPayloadAlignment = std::max(maximumPayloadAlignment, variant.payloadAlignment);
        instance->variants.push_back(std::move(variant));
    }
    instance->alignment = std::max<std::size_t>(4U, maximumPayloadAlignment);
    instance->payloadOffset = (4U + maximumPayloadAlignment - 1U) /
                              maximumPayloadAlignment * maximumPayloadAlignment;
    instance->size = (instance->payloadOffset + maximumPayloadSize + instance->alignment - 1U) /
                     instance->alignment * instance->alignment;
    definition->instances.insert_or_assign(std::move(key), instance);
    return instance;
}
