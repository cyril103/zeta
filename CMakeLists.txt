cmake_minimum_required(VERSION 3.20)
project(zeta VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(zeta
    src/ast.cpp
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/module.cpp
    src/interface.cpp
    src/semantic.cpp
    src/ir.cpp
    src/codegen.cpp
)
target_include_directories(zeta PRIVATE src)
target_compile_definitions(zeta PRIVATE
    ZETA_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
    ZETA_RUNTIME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_compile_options(zeta PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME compile_vec_empty COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_empty.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-empty)
    add_test(NAME run_vec_empty COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-empty)
    set_tests_properties(run_vec_empty PROPERTIES DEPENDS compile_vec_empty)
    add_test(NAME reject_vec_type_mismatch COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_type_mismatch.zeta)
    set_tests_properties(reject_vec_type_mismatch PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_use_after_move COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_use_after_move.zeta)
    set_tests_properties(reject_vec_use_after_move PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_vec_reserve COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_reserve.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-reserve)
    add_test(NAME run_vec_reserve COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-reserve)
    set_tests_properties(run_vec_reserve PROPERTIES DEPENDS compile_vec_reserve)
    add_test(NAME reject_vec_reserve_immutable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_reserve_immutable.zeta)
    set_tests_properties(reject_vec_reserve_immutable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_vec_reserve_negative COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_reserve_negative.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-reserve-negative)
    add_test(NAME run_vec_reserve_negative COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exit_code.sh
        ${CMAKE_CURRENT_BINARY_DIR}/vec-reserve-negative 105)
    set_tests_properties(run_vec_reserve_negative PROPERTIES
        DEPENDS compile_vec_reserve_negative)
    add_test(NAME compile_vec_push_clear COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_push_clear.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-push-clear)
    add_test(NAME run_vec_push_clear COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-push-clear)
    set_tests_properties(run_vec_push_clear PROPERTIES DEPENDS compile_vec_push_clear)
    add_test(NAME compile_vec_owned_elements COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_owned_elements.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-owned-elements)
    add_test(NAME run_vec_owned_elements COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-owned-elements)
    set_tests_properties(run_vec_owned_elements PROPERTIES DEPENDS compile_vec_owned_elements)
    add_test(NAME reject_vec_push_use_after_move COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_push_use_after_move.zeta)
    set_tests_properties(reject_vec_push_use_after_move PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_push_wrong_type COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_push_wrong_type.zeta)
    set_tests_properties(reject_vec_push_wrong_type PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_vec_slices COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_slices.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-slices)
    add_test(NAME run_vec_slices COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-slices)
    set_tests_properties(run_vec_slices PROPERTIES DEPENDS compile_vec_slices)
    add_test(NAME reject_vec_slice_blocks_growth COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_slice_blocks_growth.zeta)
    set_tests_properties(reject_vec_slice_blocks_growth PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_slice_mut_blocks_access COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_slice_mut_blocks_access.zeta)
    set_tests_properties(reject_vec_slice_mut_blocks_access PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_slice_mut_immutable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_slice_mut_immutable.zeta)
    set_tests_properties(reject_vec_slice_mut_immutable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_enum_declaration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_declaration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/enum-declaration)
    add_test(NAME run_enum_declaration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/enum-declaration)
    set_tests_properties(run_enum_declaration PROPERTIES DEPENDS compile_enum_declaration)
    add_test(NAME reject_enum_duplicate_variant COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_duplicate_variant.zeta)
    set_tests_properties(reject_enum_duplicate_variant PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_duplicate_field COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_duplicate_field.zeta)
    set_tests_properties(reject_enum_duplicate_field PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_direct_recursion COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_direct_recursion.zeta)
    set_tests_properties(reject_enum_direct_recursion PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_empty COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_empty.zeta)
    set_tests_properties(reject_enum_empty PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_enum_construction COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_construction.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/enum-construction)
    add_test(NAME run_enum_construction COMMAND ${CMAKE_CURRENT_BINARY_DIR}/enum-construction)
    set_tests_properties(run_enum_construction PROPERTIES DEPENDS compile_enum_construction)
    add_test(NAME reject_enum_unknown_variant COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_unknown_variant.zeta)
    set_tests_properties(reject_enum_unknown_variant PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_missing_field COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_missing_field.zeta)
    set_tests_properties(reject_enum_missing_field PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_field_type_mismatch COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_field_type_mismatch.zeta)
    set_tests_properties(reject_enum_field_type_mismatch PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_enum_match COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_match.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/enum-match)
    add_test(NAME run_enum_match COMMAND ${CMAKE_CURRENT_BINARY_DIR}/enum-match)
    set_tests_properties(run_enum_match PROPERTIES DEPENDS compile_enum_match)
    add_test(NAME reject_enum_match_incomplete COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_match_incomplete.zeta)
    set_tests_properties(reject_enum_match_incomplete PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_match_duplicate COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_match_duplicate.zeta)
    set_tests_properties(reject_enum_match_duplicate PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_match_wrong_operand COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_match_wrong_operand.zeta)
    set_tests_properties(reject_enum_match_wrong_operand PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_match_wrong_arity COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_match_wrong_arity.zeta)
    set_tests_properties(reject_enum_match_wrong_arity PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_enum_owned_drop COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_owned_drop.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/enum-owned-drop)
    add_test(NAME run_enum_owned_drop COMMAND ${CMAKE_CURRENT_BINARY_DIR}/enum-owned-drop)
    set_tests_properties(run_enum_owned_drop PROPERTIES DEPENDS compile_enum_owned_drop)
    add_test(NAME reject_enum_use_after_move COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_use_after_move.zeta)
    set_tests_properties(reject_enum_use_after_move PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_payload_use_after_move COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_payload_use_after_move.zeta)
    set_tests_properties(reject_enum_payload_use_after_move PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_enum_equality COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_equality.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/enum-equality)
    add_test(NAME run_enum_equality COMMAND ${CMAKE_CURRENT_BINARY_DIR}/enum-equality)
    set_tests_properties(run_enum_equality PROPERTIES DEPENDS compile_enum_equality)
    add_test(NAME reject_enum_not_equatable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_not_equatable.zeta)
    set_tests_properties(reject_enum_not_equatable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_not_copy COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_not_copy.zeta)
    set_tests_properties(reject_enum_not_copy PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_enum_ordering COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/enum_ordering.zeta)
    set_tests_properties(reject_enum_ordering PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_generic_enums COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_enums.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/generic-enums)
    add_test(NAME run_generic_enums COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generic-enums)
    set_tests_properties(run_generic_enums PROPERTIES DEPENDS compile_generic_enums)
    add_test(NAME reject_generic_enum_arity COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_enum_arity.zeta)
    set_tests_properties(reject_generic_enum_arity PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_generic_enum_missing_argument COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_enum_missing_argument.zeta)
    set_tests_properties(reject_generic_enum_missing_argument PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_generic_enum_duplicate_parameter COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_enum_duplicate_parameter.zeta)
    set_tests_properties(reject_generic_enum_duplicate_parameter PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_generic_enum_pattern_mismatch COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_enum_pattern_mismatch.zeta)
    set_tests_properties(reject_generic_enum_pattern_mismatch PROPERTIES WILL_FAIL TRUE)
    add_test(NAME precompiled_standard_library
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/precompiled_stdlib.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/stdlib
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_output.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/collections_module.zeta)
    add_test(NAME persisted_module_interfaces
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/persisted_interfaces.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_integration
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/collections_module.zeta)
    add_test(NAME separate_compilation_cache
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/separate_compilation_cache.sh
            $<TARGET_FILE:zeta> ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_integration)
    add_test(NAME compile_string_properties COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_properties.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-properties)
    add_test(NAME run_string_properties COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-properties)
    set_tests_properties(run_string_properties PROPERTIES DEPENDS compile_string_properties)
    add_test(NAME reject_string_unknown_property COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_unknown_property.zeta)
    set_tests_properties(reject_string_unknown_property PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_string_utf8_decode COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_utf8_decode.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-utf8-decode)
    add_test(NAME run_string_utf8_decode COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-utf8-decode)
    set_tests_properties(run_string_utf8_decode PROPERTIES DEPENDS compile_string_utf8_decode)
    add_test(NAME compile_string_char_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_char_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-char-iteration)
    add_test(NAME run_string_char_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-char-iteration)
    set_tests_properties(run_string_char_iteration PROPERTIES DEPENDS compile_string_char_iteration)
    add_test(NAME compile_string_views COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_views.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-views)
    add_test(NAME run_string_views COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-views)
    set_tests_properties(run_string_views PROPERTIES DEPENDS compile_string_views)
    add_test(NAME reject_string_view_return COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_view_return.zeta)
    set_tests_properties(reject_string_view_return PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_string_view_search COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_view_search.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-view-search)
    add_test(NAME run_string_view_search COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-view-search)
    set_tests_properties(run_string_view_search PROPERTIES DEPENDS compile_string_view_search)
    add_test(NAME compile_string_double_conversion COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_double_conversion.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-double-conversion)
    add_test(NAME run_string_double_conversion
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-double-conversion)
    set_tests_properties(run_string_double_conversion PROPERTIES
        DEPENDS compile_string_double_conversion
        PASS_REGULAR_EXPRESSION "18.5 / -0.25 / 0")
    add_test(NAME compile_string_primitive_conversions COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_primitive_conversions.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-primitive-conversions)
    add_test(NAME run_string_primitive_conversions
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-primitive-conversions)
    set_tests_properties(run_string_primitive_conversions PROPERTIES
        DEPENDS compile_string_primitive_conversions
        PASS_REGULAR_EXPRESSION "age = 18, byte = 7, actif = true, symbole = 🚀")
    add_test(NAME compile_string_concat COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_concat.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-concat)
    add_test(NAME run_string_concat COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-concat)
    set_tests_properties(run_string_concat PROPERTIES
        DEPENDS compile_string_concat
        PASS_REGULAR_EXPRESSION "Bonjour Zeta")
    add_test(NAME compile_collections_module COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/collections_module.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/collections-module)
    add_test(NAME run_collections_module COMMAND ${CMAKE_CURRENT_BINARY_DIR}/collections-module)
    set_tests_properties(run_collections_module PROPERTIES DEPENDS compile_collections_module)

    add_test(NAME compile_empty_slice_option COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/array_zero_length.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/empty-slice-option)
    add_test(NAME run_empty_slice_option COMMAND ${CMAKE_CURRENT_BINARY_DIR}/empty-slice-option)
    set_tests_properties(run_empty_slice_option PROPERTIES DEPENDS compile_empty_slice_option)
    add_test(NAME compile_generic_structs COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_structs.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/generic-structs)
    add_test(NAME run_generic_structs COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generic-structs)
    set_tests_properties(run_generic_structs PROPERTIES DEPENDS compile_generic_structs)
    add_test(
        NAME compile_struct_types
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/struct_types.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/struct-types
    )
    add_test(NAME run_struct_types COMMAND ${CMAKE_CURRENT_BINARY_DIR}/struct-types)
    set_tests_properties(run_struct_types PROPERTIES DEPENDS compile_struct_types)
    add_test(
        NAME compile_example
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/examples/basic.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/basic
    )
    set_tests_properties(compile_example PROPERTIES
        PASS_REGULAR_EXPRESSION "Executable créé"
    )
    add_test(NAME run_example COMMAND ${CMAKE_CURRENT_BINARY_DIR}/basic)
    set_tests_properties(run_example PROPERTIES DEPENDS compile_example)

    add_test(
        NAME compile_mutable_assignment
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/mutable_assignment.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/mutable-assignment
    )
    add_test(NAME run_mutable_assignment COMMAND ${CMAKE_CURRENT_BINARY_DIR}/mutable-assignment)
    set_tests_properties(run_mutable_assignment PROPERTIES DEPENDS compile_mutable_assignment)

    add_test(
        NAME compile_lazy_definition
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/lazy_definition.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/lazy-definition
    )
    add_test(NAME run_lazy_definition COMMAND ${CMAKE_CURRENT_BINARY_DIR}/lazy-definition)
    set_tests_properties(run_lazy_definition PROPERTIES DEPENDS compile_lazy_definition)

    add_test(
        NAME compile_functions
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/functions.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/functions
    )
    add_test(NAME run_functions COMMAND ${CMAKE_CURRENT_BINARY_DIR}/functions)
    set_tests_properties(run_functions PROPERTIES DEPENDS compile_functions)

    add_test(
        NAME compile_block_expressions
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/block_expressions.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/block-expressions
    )
    add_test(NAME run_block_expressions COMMAND ${CMAKE_CURRENT_BINARY_DIR}/block-expressions)
    set_tests_properties(run_block_expressions PROPERTIES DEPENDS compile_block_expressions)

    add_test(
        NAME compile_main_exit_code
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/main_exit_code.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/main-exit-code
    )
    add_test(NAME run_main_exit_code COMMAND ${CMAKE_CURRENT_BINARY_DIR}/main-exit-code)
    set_tests_properties(run_main_exit_code PROPERTIES
        DEPENDS compile_main_exit_code
        WILL_FAIL TRUE
    )

    add_test(
        NAME compile_byte_type
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/byte_type.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/byte-type
    )
    add_test(NAME run_byte_type COMMAND ${CMAKE_CURRENT_BINARY_DIR}/byte-type)
    set_tests_properties(run_byte_type PROPERTIES DEPENDS compile_byte_type)

    add_test(
        NAME compile_double_type
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/double_type.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/double-type
    )
    add_test(NAME run_double_type COMMAND ${CMAKE_CURRENT_BINARY_DIR}/double-type)
    set_tests_properties(run_double_type PROPERTIES DEPENDS compile_double_type)

    add_test(
        NAME compile_bool_type
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/bool_type.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/bool-type
    )
    add_test(NAME run_bool_type COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bool-type)
    set_tests_properties(run_bool_type PROPERTIES DEPENDS compile_bool_type)

    add_test(
        NAME compile_boolean_operators
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/boolean_operators.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/boolean-operators
    )
    add_test(NAME run_boolean_operators COMMAND ${CMAKE_CURRENT_BINARY_DIR}/boolean-operators)
    set_tests_properties(run_boolean_operators PROPERTIES DEPENDS compile_boolean_operators)

    add_test(
        NAME compile_conditional_expressions
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/conditional_expressions.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/conditional-expressions
    )
    add_test(NAME run_conditional_expressions COMMAND ${CMAKE_CURRENT_BINARY_DIR}/conditional-expressions)
    set_tests_properties(run_conditional_expressions PROPERTIES DEPENDS compile_conditional_expressions)

    add_test(
        NAME compile_while_loops
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/while_loops.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/while-loops
    )
    add_test(NAME run_while_loops COMMAND ${CMAKE_CURRENT_BINARY_DIR}/while-loops)
    set_tests_properties(run_while_loops PROPERTIES DEPENDS compile_while_loops)

    add_test(
        NAME compile_explicit_conversions
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/explicit_conversions.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/explicit-conversions
    )
    add_test(NAME run_explicit_conversions COMMAND ${CMAKE_CURRENT_BINARY_DIR}/explicit-conversions)
    set_tests_properties(run_explicit_conversions PROPERTIES DEPENDS compile_explicit_conversions)

    add_test(
        NAME compile_recursive_functions
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/recursive_functions.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/recursive-functions
    )
    add_test(NAME run_recursive_functions COMMAND ${CMAKE_CURRENT_BINARY_DIR}/recursive-functions)
    set_tests_properties(run_recursive_functions PROPERTIES DEPENDS compile_recursive_functions)

    add_test(
        NAME compile_tail_recursive_functions
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/tail_recursive_functions.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/tail-recursive-functions
    )
    add_test(NAME run_tail_recursive_functions COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tail-recursive-functions)
    set_tests_properties(run_tail_recursive_functions PROPERTIES DEPENDS compile_tail_recursive_functions)

    add_test(NAME compile_return_statements COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/return_statements.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/return-statements)
    add_test(NAME run_return_statements COMMAND ${CMAKE_CURRENT_BINARY_DIR}/return-statements)
    set_tests_properties(run_return_statements PROPERTIES DEPENDS compile_return_statements)

    add_test(NAME compile_break_continue COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/break_continue.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/break-continue)
    add_test(NAME run_break_continue COMMAND ${CMAKE_CURRENT_BINARY_DIR}/break-continue)
    set_tests_properties(run_break_continue PROPERTIES DEPENDS compile_break_continue)

    add_test(NAME compile_char_type COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/char_type.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/char-type)
    add_test(NAME run_char_type COMMAND ${CMAKE_CURRENT_BINARY_DIR}/char-type)
    set_tests_properties(run_char_type PROPERTIES DEPENDS compile_char_type)

    add_test(NAME compile_string_type COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/string_type.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/string-type)
    add_test(NAME run_string_type COMMAND ${CMAKE_CURRENT_BINARY_DIR}/string-type)
    set_tests_properties(run_string_type PROPERTIES DEPENDS compile_string_type)

    add_test(NAME compile_public_declaration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_declaration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/public-declaration)
    add_test(NAME run_public_declaration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/public-declaration)
    set_tests_properties(run_public_declaration PROPERTIES DEPENDS compile_public_declaration)

    add_test(NAME compile_module_loading COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_loading/main.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/module-loading)
    add_test(NAME run_module_loading COMMAND ${CMAKE_CURRENT_BINARY_DIR}/module-loading)
    set_tests_properties(run_module_loading PROPERTIES DEPENDS compile_module_loading)

    add_test(NAME reject_module_duplicate_export COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_duplicate_export/main.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-module-duplicate-export)
    set_tests_properties(reject_module_duplicate_export PROPERTIES WILL_FAIL TRUE)

    foreach(module_error IN ITEMS private wrong_arity)
        add_test(NAME reject_module_${module_error} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_${module_error}/main.zeta
            -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-module-${module_error})
        set_tests_properties(reject_module_${module_error} PROPERTIES WILL_FAIL TRUE)
    endforeach()

    add_test(NAME reject_module_cycle COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_cycle/main.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-module-cycle)
    set_tests_properties(reject_module_cycle PROPERTIES WILL_FAIL TRUE)

    add_test(NAME compile_modules_integration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_integration/main.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/modules-integration)
    add_test(NAME run_modules_integration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/modules-integration)
    set_tests_properties(run_modules_integration PROPERTIES DEPENDS compile_modules_integration)

    add_test(NAME compile_native_declaration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_native_declaration/main.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/native-declaration)
    add_test(NAME run_native_declaration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/native-declaration)
    set_tests_properties(run_native_declaration PROPERTIES DEPENDS compile_native_declaration)

    add_test(NAME compile_stdlib_lookup COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib_lookup.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/stdlib-lookup
        --stdlib ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/stdlib)
    add_test(NAME run_stdlib_lookup COMMAND ${CMAKE_CURRENT_BINARY_DIR}/stdlib-lookup)
    set_tests_properties(run_stdlib_lookup PROPERTIES DEPENDS compile_stdlib_lookup)

    add_test(NAME compile_io_interface COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_interface.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/io-interface)
    add_test(NAME run_io_interface COMMAND ${CMAKE_CURRENT_BINARY_DIR}/io-interface)
    set_tests_properties(run_io_interface PROPERTIES
        DEPENDS compile_io_interface
        PASS_REGULAR_EXPRESSION "Zeta")

    add_test(NAME assemble_runtime_io COMMAND fasm
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/io.asm
        ${CMAKE_CURRENT_BINARY_DIR}/runtime-io.o)

    add_test(NAME compile_io_output COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_output.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/io-output)
    add_test(NAME run_io_output COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=${CMAKE_CURRENT_BINARY_DIR}/io-output
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_io_output.cmake)
    set_tests_properties(run_io_output PROPERTIES DEPENDS compile_io_output)

    add_test(NAME compile_io_char COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_char.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/io-char)
    add_test(NAME run_io_char COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=${CMAKE_CURRENT_BINARY_DIR}/io-char
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_io_char.cmake)
    set_tests_properties(run_io_char PROPERTIES DEPENDS compile_io_char)

    add_test(NAME compile_io_int COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_int.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/io-int)
    add_test(NAME run_io_int COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=${CMAKE_CURRENT_BINARY_DIR}/io-int
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_io_int.cmake)
    set_tests_properties(run_io_int PROPERTIES DEPENDS compile_io_int)

    foreach(io_case IN ITEMS byte_bool double)
        add_test(NAME compile_io_${io_case} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_${io_case}.zeta
            -o ${CMAKE_CURRENT_BINARY_DIR}/io-${io_case})
        add_test(NAME run_io_${io_case} COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=${CMAKE_CURRENT_BINARY_DIR}/io-${io_case}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_io_${io_case}.cmake)
        set_tests_properties(run_io_${io_case} PROPERTIES
            DEPENDS compile_io_${io_case})
    endforeach()

    add_test(NAME compile_io_bytes COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_bytes.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/io-bytes)
    add_test(NAME run_io_bytes COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=${CMAKE_CURRENT_BINARY_DIR}/io-bytes
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_io_bytes.cmake)
    set_tests_properties(run_io_bytes PROPERTIES DEPENDS compile_io_bytes)

    add_test(NAME compile_box_type_syntax COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/box_type_syntax.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/box-type-syntax)
    add_test(NAME run_box_type_syntax COMMAND ${CMAKE_CURRENT_BINARY_DIR}/box-type-syntax)
    set_tests_properties(run_box_type_syntax PROPERTIES DEPENDS compile_box_type_syntax)

    add_test(NAME compile_box_move_declaration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/box_move_declaration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/box-move-declaration)
    add_test(NAME run_box_move_declaration
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/box-move-declaration)
    set_tests_properties(run_box_move_declaration PROPERTIES
        DEPENDS compile_box_move_declaration)

    add_test(NAME compile_box_move_call COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/box_move_call.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/box-move-call)
    add_test(NAME run_box_move_call COMMAND ${CMAKE_CURRENT_BINARY_DIR}/box-move-call)
    set_tests_properties(run_box_move_call PROPERTIES DEPENDS compile_box_move_call)
    add_test(NAME check_box_parameter_drop COMMAND ${CMAKE_COMMAND}
        -DIR_FILE=${CMAKE_CURRENT_BINARY_DIR}/box-move-call.ir
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_box_drop.cmake)
    set_tests_properties(check_box_parameter_drop PROPERTIES DEPENDS compile_box_move_call)

    foreach(box_case IN ITEMS construct_dereference nested_construct)
        add_test(NAME compile_box_${box_case} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/box_${box_case}.zeta
            -o ${CMAKE_CURRENT_BINARY_DIR}/box-${box_case})
        add_test(NAME run_box_${box_case} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/box-${box_case})
        set_tests_properties(run_box_${box_case} PROPERTIES DEPENDS compile_box_${box_case})
    endforeach()

    add_test(NAME check_box_lexical_drop COMMAND ${CMAKE_COMMAND}
        -DIR_FILE=${CMAKE_CURRENT_BINARY_DIR}/box-construct_dereference.ir
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_box_drop.cmake)
    set_tests_properties(check_box_lexical_drop PROPERTIES
        DEPENDS compile_box_construct_dereference)

    add_test(NAME compile_box_early_drop COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/box_early_drop.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/box-early-drop)
    add_test(NAME run_box_early_drop COMMAND ${CMAKE_CURRENT_BINARY_DIR}/box-early-drop)
    set_tests_properties(run_box_early_drop PROPERTIES DEPENDS compile_box_early_drop)
    add_test(NAME check_box_early_drop COMMAND ${CMAKE_COMMAND}
        -DIR_FILE=${CMAKE_CURRENT_BINARY_DIR}/box-early-drop.ir
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_box_early_drop.cmake)
    set_tests_properties(check_box_early_drop PROPERTIES DEPENDS compile_box_early_drop)

    foreach(box_case IN ITEMS allocator_stress aggregate_content)
        add_test(NAME compile_box_${box_case} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/box_${box_case}.zeta
            -o ${CMAKE_CURRENT_BINARY_DIR}/box-${box_case})
        add_test(NAME run_box_${box_case} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/box-${box_case})
        set_tests_properties(run_box_${box_case} PROPERTIES DEPENDS compile_box_${box_case})
    endforeach()

    add_test(NAME compile_generic_function_syntax COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_function_syntax.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/generic-function-syntax)
    add_test(NAME run_generic_function_syntax
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generic-function-syntax)
    set_tests_properties(run_generic_function_syntax PROPERTIES
        DEPENDS compile_generic_function_syntax)

    add_test(NAME compile_generic_explicit_instantiation_syntax COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_explicit_instantiation_syntax.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/generic-explicit-instantiation-syntax)
    add_test(NAME run_generic_explicit_instantiation_syntax
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generic-explicit-instantiation-syntax)
    set_tests_properties(run_generic_explicit_instantiation_syntax PROPERTIES
        DEPENDS compile_generic_explicit_instantiation_syntax)

    add_test(NAME compile_generic_monomorphization COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_monomorphization.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/generic-monomorphization)
    add_test(NAME run_generic_monomorphization
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generic-monomorphization)
    set_tests_properties(run_generic_monomorphization PROPERTIES
        DEPENDS compile_generic_monomorphization)
    add_test(NAME check_generic_instances COMMAND ${CMAKE_COMMAND}
        -DIR_FILE=${CMAKE_CURRENT_BINARY_DIR}/generic-monomorphization.ir
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_generic_instances.cmake)
    set_tests_properties(check_generic_instances PROPERTIES
        DEPENDS compile_generic_monomorphization)

    add_test(NAME compile_generic_type_inference COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_type_inference.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/generic-type-inference)
    add_test(NAME run_generic_type_inference
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generic-type-inference)
    set_tests_properties(run_generic_type_inference PROPERTIES
        DEPENDS compile_generic_type_inference)

    add_test(NAME compile_generic_constraints COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_constraints.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/generic-constraints)
    add_test(NAME run_generic_constraints COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generic-constraints)
    set_tests_properties(run_generic_constraints PROPERTIES DEPENDS compile_generic_constraints)

    add_test(NAME compile_box_content_borrow COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/box_content_borrow.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/box-content-borrow)
    add_test(NAME run_box_content_borrow COMMAND ${CMAKE_CURRENT_BINARY_DIR}/box-content-borrow)
    set_tests_properties(run_box_content_borrow PROPERTIES DEPENDS compile_box_content_borrow)

    add_test(NAME compile_array_copy COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/array_copy.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/array-copy)
    add_test(NAME run_array_copy COMMAND ${CMAKE_CURRENT_BINARY_DIR}/array-copy)
    set_tests_properties(run_array_copy PROPERTIES DEPENDS compile_array_copy)

    add_test(NAME compile_array_index COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/array_index.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/array-index)
    add_test(NAME run_array_index COMMAND ${CMAKE_CURRENT_BINARY_DIR}/array-index)
    set_tests_properties(run_array_index PROPERTIES DEPENDS compile_array_index)

    add_test(NAME compile_array_dynamic_bounds COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/array_dynamic_bounds.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/array-dynamic-bounds)
    add_test(NAME run_array_dynamic_bounds COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=${CMAKE_CURRENT_BINARY_DIR}/array-dynamic-bounds
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exit_101.cmake)
    set_tests_properties(run_array_dynamic_bounds PROPERTIES DEPENDS compile_array_dynamic_bounds)

    add_test(NAME compile_array_mutation COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/array_mutation.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/array-mutation)
    add_test(NAME run_array_mutation COMMAND ${CMAKE_CURRENT_BINARY_DIR}/array-mutation)
    set_tests_properties(run_array_mutation PROPERTIES DEPENDS compile_array_mutation)

    add_test(NAME compile_nested_arrays COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested_arrays.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/nested-arrays)
    add_test(NAME run_nested_arrays COMMAND ${CMAKE_CURRENT_BINARY_DIR}/nested-arrays)
    set_tests_properties(run_nested_arrays PROPERTIES DEPENDS compile_nested_arrays)

    add_test(NAME compile_array_types COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/array_types.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/array-types)
    add_test(NAME run_array_types COMMAND ${CMAKE_CURRENT_BINARY_DIR}/array-types)
    set_tests_properties(run_array_types PROPERTIES DEPENDS compile_array_types)

    add_test(NAME compile_reference_read COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_read.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-read)
    add_test(NAME run_reference_read COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-read)
    set_tests_properties(run_reference_read PROPERTIES DEPENDS compile_reference_read)

    add_test(NAME compile_reference_write COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_write.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-write)
    add_test(NAME run_reference_write COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-write)
    set_tests_properties(run_reference_write PROPERTIES DEPENDS compile_reference_write)

    add_test(NAME compile_reference_borrows COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_borrows.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-borrows)
    add_test(NAME run_reference_borrows COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-borrows)
    set_tests_properties(run_reference_borrows PROPERTIES DEPENDS compile_reference_borrows)

    add_test(NAME compile_reference_parameters COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_parameters.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-parameters)
    add_test(NAME run_reference_parameters COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-parameters)
    set_tests_properties(run_reference_parameters PROPERTIES DEPENDS compile_reference_parameters)

    add_test(NAME compile_reference_array COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_array.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-array)
    add_test(NAME run_reference_array COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-array)
    set_tests_properties(run_reference_array PROPERTIES DEPENDS compile_reference_array)

    add_test(NAME compile_reference_array_mutation COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_array_mutation.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-array-mutation)
    add_test(NAME run_reference_array_mutation
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-array-mutation)
    set_tests_properties(run_reference_array_mutation PROPERTIES
        DEPENDS compile_reference_array_mutation)

    add_test(NAME compile_reference_array_mutation_bounds COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_array_mutation_bounds.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-array-mutation-bounds)
    add_test(NAME run_reference_array_mutation_bounds COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=${CMAKE_CURRENT_BINARY_DIR}/reference-array-mutation-bounds
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exit_101.cmake)
    set_tests_properties(run_reference_array_mutation_bounds PROPERTIES
        DEPENDS compile_reference_array_mutation_bounds)

    foreach(reference_case IN ITEMS string char nested_array recursive_calls)
        add_test(NAME compile_reference_${reference_case} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_${reference_case}.zeta
            -o ${CMAKE_CURRENT_BINARY_DIR}/reference-${reference_case})
        add_test(NAME run_reference_${reference_case}
            COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-${reference_case})
        set_tests_properties(run_reference_${reference_case} PROPERTIES
            DEPENDS compile_reference_${reference_case})
    endforeach()

    add_test(NAME compile_reference_last_use COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_last_use.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-last-use)
    add_test(NAME run_reference_last_use
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-last-use)
    set_tests_properties(run_reference_last_use PROPERTIES
        DEPENDS compile_reference_last_use)

    add_test(NAME compile_reference_shared_alias_last_use COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_shared_alias_last_use.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/reference-shared-alias-last-use)
    add_test(NAME run_reference_shared_alias_last_use
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/reference-shared-alias-last-use)
    set_tests_properties(run_reference_shared_alias_last_use PROPERTIES
        DEPENDS compile_reference_shared_alias_last_use)

    add_test(NAME compile_slice_type_syntax COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/slice_type_syntax.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/slice-type-syntax)
    add_test(NAME run_slice_type_syntax
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/slice-type-syntax)
    set_tests_properties(run_slice_type_syntax PROPERTIES
        DEPENDS compile_slice_type_syntax)

    add_test(NAME compile_slice_creation COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/slice_creation.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/slice-creation)
    add_test(NAME run_slice_creation
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/slice-creation)
    set_tests_properties(run_slice_creation PROPERTIES
        DEPENDS compile_slice_creation)

    foreach(slice_case IN ITEMS index_abi nested_index)
        add_test(NAME compile_slice_${slice_case} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/slice_${slice_case}.zeta
            -o ${CMAKE_CURRENT_BINARY_DIR}/slice-${slice_case})
        add_test(NAME run_slice_${slice_case}
            COMMAND ${CMAKE_CURRENT_BINARY_DIR}/slice-${slice_case})
        set_tests_properties(run_slice_${slice_case} PROPERTIES
            DEPENDS compile_slice_${slice_case})
    endforeach()

    add_test(NAME compile_slice_dynamic_bounds COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/slice_dynamic_bounds.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/slice-dynamic-bounds)
    add_test(NAME run_slice_dynamic_bounds COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=${CMAKE_CURRENT_BINARY_DIR}/slice-dynamic-bounds
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exit_101.cmake)
    set_tests_properties(run_slice_dynamic_bounds PROPERTIES
        DEPENDS compile_slice_dynamic_bounds)

    foreach(invalid_case IN ITEMS
            redefinition
            assignment
            invalid_identifier
            unknown_assignment
            definition_assignment
            definition_unknown_reference
            function_duplicate_parameter
            function_wrong_arity
            call_non_function
            function_without_call
            block_missing_result
            block_scope
            multiline_without_braces
            missing_main
            main_parameters
            byte_out_of_range
            byte_int_mismatch
            byte_argument_mismatch
            double_int_mismatch
            double_to_int_mismatch
            double_invalid_exponent
            bool_numeric_literal
            bool_arithmetic
            bool_int_mismatch
            bool_argument_mismatch
            comparison_type_mismatch
            bool_ordering
            logical_numeric_operand
            incomplete_logical_operator
            if_non_bool_predicate
            if_branch_type_mismatch
            if_missing_else
            else_without_if
            while_non_bool_predicate
            while_missing_do
            while_scope
            while_as_expression
            bool_conversion_source
            bool_conversion_target
            return_outside_function
            return_type_mismatch
            break_outside_loop
            continue_outside_loop
            char_multiple_codepoints
            char_arithmetic
            char_int_mismatch
            char_empty
            char_invalid_escape
            char_surrogate_literal
            string_invalid_escape
            string_arithmetic
            string_ordering
            string_type_mismatch
            module_missing
            native_main
            array_type_syntax
            array_literal_syntax
            array_length_mismatch
            array_element_mismatch
            array_index_too_large
            array_index_negative
            array_immutable_mutation
            array_function_signature
            array_comparison
            reference_mutable_val
            reference_temporary
            reference_global
            reference_return
            reference_shared_write
            reference_borrow_conflict
            reference_double_mut
            reference_borrowed_assignment
            reference_call_alias
            reference_shared_array_mutation
            reference_mutable_alias
            reference_mutable_direct_read
            slice_type_missing_element
            slice_type_missing_bracket
            slice_element_mismatch
            slice_mut_requires_mut_borrow
            slice_mut_immutable_source
            slice_global
            slice_borrowed_mutation
            slice_mut_borrowed_read
            slice_mut_copy
            slice_shared_mutation
            box_type_missing_element
            box_type_missing_bracket
            box_use_after_move
            box_conditional_move
            box_use_after_call
            box_duplicate_call_argument
            box_content_immutable
            box_constructor_mismatch
            box_content_borrow_conflict
            box_content_mut_borrow_val
            box_owner_reassignment
            box_asymmetric_move
            generic_duplicate_type_parameter
            generic_call_without_instantiation
            generic_type_arity
            generic_arguments_on_regular_function
            generic_conflicting_inference
            generic_copy_box
            generic_numeric_string
            generic_unknown_constraint
            generic_unconstrained_operator
            struct_duplicate_field
            struct_missing_field
            struct_immutable_field
            generic_struct_arity)
        add_test(
            NAME reject_${invalid_case}
            COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/${invalid_case}.zeta
                    -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-${invalid_case}
        )
        set_tests_properties(reject_${invalid_case} PROPERTIES WILL_FAIL TRUE)
    endforeach()
endif()
