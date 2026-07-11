cmake_minimum_required(VERSION 3.20)
project(zeta VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(zeta
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/semantic.cpp
    src/ir.cpp
    src/codegen.cpp
)
target_include_directories(zeta PRIVATE src)
target_compile_options(zeta PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

include(CTest)
if(BUILD_TESTING)
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
            continue_outside_loop)
        add_test(
            NAME reject_${invalid_case}
            COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/${invalid_case}.zeta
                    -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-${invalid_case}
        )
        set_tests_properties(reject_${invalid_case} PROPERTIES WILL_FAIL TRUE)
    endforeach()
endif()
