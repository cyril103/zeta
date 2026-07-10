cmake_minimum_required(VERSION 3.20)
project(zeta VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(zeta
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
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

    foreach(invalid_case IN ITEMS redefinition assignment invalid_identifier unknown_assignment)
        add_test(
            NAME reject_${invalid_case}
            COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/tests/${invalid_case}.zeta
                    -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-${invalid_case}
        )
        set_tests_properties(reject_${invalid_case} PROPERTIES WILL_FAIL TRUE)
    endforeach()
endif()
