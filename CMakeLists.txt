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
    src/generic_tokens.cpp
    src/interface.cpp
    src/semantic.cpp
    src/ir.cpp
    src/ir_verifier.cpp
    src/codegen.cpp
)
target_include_directories(zeta PRIVATE src)
target_compile_definitions(zeta PRIVATE
    ZETA_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
    ZETA_RUNTIME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_compile_options(zeta PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

include(CTest)
if(BUILD_TESTING)
    add_executable(ir_verifier_tests
        src/ast.cpp
        src/ir.cpp
        src/ir_verifier.cpp
        src/codegen.cpp
        tests/ir_verifier_tests.cpp)
    target_include_directories(ir_verifier_tests PRIVATE src)
    target_compile_options(ir_verifier_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion)
    add_test(NAME ir_verifier_unit COMMAND ir_verifier_tests)
    add_test(NAME emit_llvm_minimal COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_emit_llvm_minimal.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/emit_llvm_minimal.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/emit-llvm-minimal)
    add_test(NAME emit_llvm_scalars COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_emit_llvm_scalars.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/emit_llvm_scalars.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/emit-llvm-scalars)
    add_test(NAME emit_llvm_control COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_emit_llvm_control.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/emit_llvm_control.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/emit-llvm-control)
    add_test(NAME emit_llvm_parameters COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_emit_llvm_parameters.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/emit_llvm_parameters.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/emit-llvm-parameters)
    add_test(NAME compile_clang_backend_parameters COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_parameters.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/emit_llvm_parameters.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-parameters)
    add_test(NAME run_fasm_backend_still_default COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_fasm_backend_still_default.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/emit_llvm_parameters.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/fasm-backend-default)
    add_test(NAME reject_clang_backend_cli_diagnostics COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_cli_diagnostics.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/emit_llvm_parameters.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-cli-diagnostics)
    add_test(NAME compile_clang_backend_build_library COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_build_library.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-build-library)
    add_test(NAME compile_clang_backend_shared_library_cache COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_shared_library_cache.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-shared-library-cache)
    add_test(NAME compile_clang_backend_build_stdlib COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_build_stdlib.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-build-stdlib)
    add_test(NAME compile_clang_backend_global_values COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_global_values.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_values
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-global-values)
    add_test(NAME compile_clang_backend_global_string COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_global_string.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_global_string.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-global-string)
    add_test(NAME compile_clang_backend_global_double COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_global_double.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_global_double.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-global-double)
    add_test(NAME compile_clang_backend_global_byte COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_global_byte.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_global_byte.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-global-byte)
    add_test(NAME compile_clang_backend_global_char COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_global_char.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_global_char.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-global-char)
    add_test(NAME compile_clang_backend_global_struct COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_global_struct.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_global_struct.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-global-struct)
    add_test(NAME reject_clang_backend_unsupported_aggregates COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_unsupported_aggregates.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reject_clang_backend_local_struct.zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reject_clang_backend_local_vec.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-unsupported-aggregates)
    add_test(NAME reject_clang_backend_unsupported_global_aggregates COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_unsupported_global_aggregates.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reject_clang_backend_global_vec.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-unsupported-global-aggregates)
    add_test(NAME compile_clang_backend_string_literal COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_literal.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_literal.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-literal)
    add_test(NAME compile_clang_backend_string_is_empty COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_is_empty.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_is_empty.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-is-empty)
    add_test(NAME compile_clang_backend_local_string COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_local_string.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_local_string.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-local-string)
    add_test(NAME compile_clang_backend_string_concat COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_concat.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_concat.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-concat)
    add_test(NAME compile_clang_backend_string_concat_drop COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_concat_drop.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_concat_drop.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-concat-drop)
    add_test(NAME compile_clang_backend_string_view COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_view.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_view.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-view)
    add_test(NAME compile_clang_backend_string_search COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_search.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_search.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-search)
    add_test(NAME compile_clang_backend_string_utf8_decode COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_utf8_decode.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_utf8_decode.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-utf8-decode)
    add_test(NAME compile_clang_backend_for_string_char_iteration COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_for_string_char_iteration.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_for_string_char_iteration.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-for-string-char-iteration)
    add_test(NAME compile_clang_backend_string_bool_conversion COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_bool_conversion.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_bool_conversion.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-bool-conversion)
    add_test(NAME compile_clang_backend_string_int_conversion COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_int_conversion.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_int_conversion.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-int-conversion)
    add_test(NAME compile_clang_backend_string_byte_conversion COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_byte_conversion.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_byte_conversion.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-byte-conversion)
    add_test(NAME compile_clang_backend_string_char_conversion COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_char_conversion.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_char_conversion.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-char-conversion)
    add_test(NAME compile_clang_backend_string_double_conversion COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_string_double_conversion.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_string_double_conversion.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang-backend-string-double-conversion)
    add_test(NAME compile_clang_backend_io_println_string COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_io_println_string.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_io_println_string.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_io_println_string)
    add_test(NAME compile_clang_backend_io_println_heap_string COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_io_println_heap_string.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_io_println_heap_string.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_io_println_heap_string)

    add_test(NAME compile_clang_backend_io_println_int COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_io_println_int.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_io_println_int.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_io_println_int)

    add_test(NAME compile_clang_backend_io_println_bool COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_io_println_bool.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_io_println_bool.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_io_println_bool)
    add_test(NAME compile_clang_backend_io_println_byte COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_io_println_byte.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_io_println_byte.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_io_println_byte)
    add_test(NAME compile_clang_backend_io_println_char COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_io_println_char.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_io_println_char.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_io_println_char)
    add_test(NAME compile_clang_backend_io_println_double COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_io_println_double.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_io_println_double.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_io_println_double)
    add_test(NAME compile_clang_backend_double_operations COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_double_operations.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_double_operations.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_double_operations)
    add_test(NAME compile_clang_backend_local_struct COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_local_struct.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_local_struct.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_local_struct)
    add_test(NAME compile_clang_backend_local_struct_field_store COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_local_struct_field_store.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_local_struct_field_store.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_local_struct_field_store)
    add_test(NAME compile_clang_backend_struct_function_abi COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_struct_function_abi.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_struct_function_abi.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_struct_function_abi)
    add_test(NAME compile_clang_backend_mixed_struct COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_mixed_struct.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_mixed_struct.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_mixed_struct)
    add_test(NAME compile_clang_backend_nested_struct COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_nested_struct.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_nested_struct.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_nested_struct)
    add_test(NAME compile_clang_backend_nested_struct_field_store COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_nested_struct_field_store.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_nested_struct_field_store.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_nested_struct_field_store)
    add_test(NAME compile_clang_backend_nested_struct_subfield_store COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_nested_struct_subfield_store.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_nested_struct_subfield_store.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_nested_struct_subfield_store)
    add_test(NAME compile_clang_backend_struct_heap_string_drop COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_struct_heap_string_drop.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_struct_heap_string_drop.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_struct_heap_string_drop)
    add_test(NAME compile_clang_backend_struct_heap_string_retain COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_struct_heap_string_retain.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_struct_heap_string_retain.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_struct_heap_string_retain)
    add_test(NAME compile_clang_backend_struct_heap_string_field_replace COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_struct_heap_string_field_replace.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_struct_heap_string_field_replace.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_struct_heap_string_field_replace)
    add_test(NAME compile_clang_backend_struct_heap_string_interprocedural COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_struct_heap_string_interprocedural.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_struct_heap_string_interprocedural.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_struct_heap_string_interprocedural)
    add_test(NAME compile_clang_backend_branch_copy COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_branch_copy.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_branch_copy.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_branch_copy)
    add_test(NAME compile_clang_backend_struct_branch_copy COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_clang_backend_struct_branch_copy.sh
        $<TARGET_FILE:zeta>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_backend_struct_branch_copy.zeta
        ${CMAKE_CURRENT_BINARY_DIR}/clang_backend_struct_branch_copy)

    add_test(NAME compile_public_struct_declaration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_struct_declaration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/public-struct-declaration)
    add_test(NAME run_public_struct_declaration
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/public-struct-declaration)
    set_tests_properties(run_public_struct_declaration PROPERTIES
        DEPENDS compile_public_struct_declaration)
    add_test(NAME reject_public_struct_private_field COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_struct_private_field.zeta)
    set_tests_properties(reject_public_struct_private_field PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_public_enum_declaration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_enum_declaration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/public-enum-declaration)
    add_test(NAME run_public_enum_declaration
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/public-enum-declaration)
    set_tests_properties(run_public_enum_declaration PROPERTIES
        DEPENDS compile_public_enum_declaration)
    add_test(NAME reject_public_enum_private_payload COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_enum_private_payload.zeta)
    set_tests_properties(reject_public_enum_private_payload PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_public_function_private_enum COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_function_private_enum.zeta)
    set_tests_properties(reject_public_function_private_enum PROPERTIES WILL_FAIL TRUE)
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
    add_test(NAME reject_vec_slice_blocks_move COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_slice_blocks_move.zeta)
    set_tests_properties(reject_vec_slice_blocks_move PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_slice_mut_blocks_access COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_slice_mut_blocks_access.zeta)
    set_tests_properties(reject_vec_slice_mut_blocks_access PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_slice_mut_immutable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_slice_mut_immutable.zeta)
    set_tests_properties(reject_vec_slice_mut_immutable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_loop_local_slice_last_use_allows_vec_push COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/loop_local_slice_last_use_allows_vec_push.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/loop-local-slice-last-use-allows-vec-push)
    add_test(NAME run_loop_local_slice_last_use_allows_vec_push
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/loop-local-slice-last-use-allows-vec-push)
    set_tests_properties(run_loop_local_slice_last_use_allows_vec_push PROPERTIES
        DEPENDS compile_loop_local_slice_last_use_allows_vec_push)
    add_test(NAME compile_loop_local_slice_mut_last_use_allows_vec_push COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/loop_local_slice_mut_last_use_allows_vec_push.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/loop-local-slice-mut-last-use-allows-vec-push)
    add_test(NAME run_loop_local_slice_mut_last_use_allows_vec_push
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/loop-local-slice-mut-last-use-allows-vec-push)
    set_tests_properties(run_loop_local_slice_mut_last_use_allows_vec_push PROPERTIES
        DEPENDS compile_loop_local_slice_mut_last_use_allows_vec_push)
    add_test(NAME reject_for_iterable_slice_blocks_vec_push COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reject_for_iterable_slice_blocks_vec_push.zeta)
    set_tests_properties(reject_for_iterable_slice_blocks_vec_push PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_vec_complete_api COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_complete_api.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-complete-api)
    add_test(NAME run_vec_complete_api COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-complete-api)
    set_tests_properties(run_vec_complete_api PROPERTIES DEPENDS compile_vec_complete_api)
    add_test(NAME compile_vec_complete_owned_api COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_complete_owned_api.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-complete-owned-api)
    add_test(NAME run_vec_complete_owned_api
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-complete-owned-api)
    set_tests_properties(run_vec_complete_owned_api PROPERTIES
        DEPENDS compile_vec_complete_owned_api)
    add_test(NAME reject_vec_get_non_copy COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_get_non_copy.zeta)
    set_tests_properties(reject_vec_get_non_copy PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_pop_immutable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_pop_immutable.zeta)
    set_tests_properties(reject_vec_pop_immutable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_vec_set_bounds COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_set_bounds.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-set-bounds)
    add_test(NAME run_vec_set_bounds COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exit_code.sh
        ${CMAKE_CURRENT_BINARY_DIR}/vec-set-bounds 101)
    set_tests_properties(run_vec_set_bounds PROPERTIES DEPENDS compile_vec_set_bounds)
    add_test(NAME compile_vec_primitive_matrix COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_primitive_matrix.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-primitive-matrix)
    add_test(NAME run_vec_primitive_matrix
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-primitive-matrix)
    set_tests_properties(run_vec_primitive_matrix PROPERTIES
        DEPENDS compile_vec_primitive_matrix)
    add_test(NAME compile_vec_struct_matrix COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_struct_matrix.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-struct-matrix)
    add_test(NAME run_vec_struct_matrix COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-struct-matrix)
    set_tests_properties(run_vec_struct_matrix PROPERTIES DEPENDS compile_vec_struct_matrix)
    add_test(NAME compile_vec_nested COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_nested.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-nested)
    add_test(NAME run_vec_nested COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-nested)
    set_tests_properties(run_vec_nested PROPERTIES DEPENDS compile_vec_nested)
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
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/stdlib/vec_generic_fixture.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_output.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/collections_module.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/precompiled_vec_fixture.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_read.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_compare.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_mutate.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_ordered.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_sort_extension.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_bounds.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_aggregate.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_iteration.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_iteration.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/stack.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/queue.zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/option_module.zeta)
    add_test(NAME persisted_module_interfaces
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/persisted_interfaces.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_integration
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/collections_module.zeta)
    add_test(NAME public_struct_interfaces
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_struct_interfaces.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_struct_interfaces)
    add_test(NAME public_enum_interfaces
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_enum_interfaces.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_enum_interfaces)
    add_test(NAME user_method_interfaces
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_method_interfaces.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_method_interfaces)
    add_test(NAME extension_method_interfaces
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/extension_method_interfaces.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/extension_method_interfaces)
    add_test(NAME user_traits_interfaces
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_traits_interfaces.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_traits_interfaces)
    add_test(NAME separate_compilation_cache
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/separate_compilation_cache.sh
            $<TARGET_FILE:zeta> ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules_integration)
    add_test(NAME generic_interface_cache
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_interface_cache.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_interface_cache)
    add_test(NAME deduplicate_source_generic_instances
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_instance_collision.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_instance_dedup)
    add_test(NAME distribute_deduplicated_generic_instances
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_instance_distribution.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_instance_dedup)
    add_test(NAME interface_diagnostics
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/interface_diagnostics.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/interface_diagnostics)
    add_test(NAME explicit_library_build
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/library_build.sh
            $<TARGET_FILE:zeta> ${CMAKE_CURRENT_SOURCE_DIR}/stdlib)
    add_test(NAME shared_library_cache
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_library_cache.sh
            $<TARGET_FILE:zeta>)
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

    add_test(NAME compile_option_module COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/option_module.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/option-module)
    add_test(NAME run_option_module COMMAND ${CMAKE_CURRENT_BINARY_DIR}/option-module)
    set_tests_properties(run_option_module PROPERTIES DEPENDS compile_option_module)
    add_test(NAME reject_option_non_copy COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/option_non_copy.zeta)
    set_tests_properties(reject_option_non_copy PROPERTIES WILL_FAIL TRUE)

    add_test(NAME compile_sequences_read COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_read.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/sequences-read)
    add_test(NAME run_sequences_read COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sequences-read)
    set_tests_properties(run_sequences_read PROPERTIES DEPENDS compile_sequences_read)

    add_test(NAME compile_sequences_compare COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_compare.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/sequences-compare)
    add_test(NAME run_sequences_compare COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sequences-compare)
    set_tests_properties(run_sequences_compare PROPERTIES DEPENDS compile_sequences_compare)

    add_test(NAME compile_sequences_reverse COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_reverse.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/sequences-reverse)
    add_test(NAME run_sequences_reverse COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sequences-reverse)
    set_tests_properties(run_sequences_reverse PROPERTIES DEPENDS compile_sequences_reverse)

    add_test(NAME compile_sequences_mutate COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_mutate.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/sequences-mutate)
    add_test(NAME run_sequences_mutate COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sequences-mutate)
    set_tests_properties(run_sequences_mutate PROPERTIES DEPENDS compile_sequences_mutate)

    add_test(NAME compile_sequences_ordered COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_ordered.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/sequences-ordered)
    add_test(NAME run_sequences_ordered COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sequences-ordered)
    set_tests_properties(run_sequences_ordered PROPERTIES DEPENDS compile_sequences_ordered)

    add_test(NAME compile_vec_sort_extension COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_sort_extension.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-sort-extension)
    add_test(NAME run_vec_sort_extension COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-sort-extension)
    set_tests_properties(run_vec_sort_extension PROPERTIES DEPENDS compile_vec_sort_extension)
    add_test(NAME reject_vec_sort_extension_val COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_sort_extension_val.zeta)
    set_tests_properties(reject_vec_sort_extension_val PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_sort_extension_constraint COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_sort_extension_constraint.zeta)
    set_tests_properties(reject_vec_sort_extension_constraint PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_sort_extension_borrow COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_sort_extension_borrow.zeta)
    set_tests_properties(reject_vec_sort_extension_borrow PROPERTIES WILL_FAIL TRUE)

    add_test(NAME compile_sequences_bounds COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_bounds.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/sequences-bounds)
    add_test(NAME run_sequences_bounds COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sequences-bounds)
    set_tests_properties(run_sequences_bounds PROPERTIES DEPENDS compile_sequences_bounds)

    add_test(NAME compile_sequences_aggregate COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_aggregate.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/sequences-aggregate)
    add_test(NAME run_sequences_aggregate COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sequences-aggregate)
    set_tests_properties(run_sequences_aggregate PROPERTIES DEPENDS compile_sequences_aggregate)

    add_test(NAME compile_sequences_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sequences_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/sequences-iteration)
    add_test(NAME run_sequences_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sequences-iteration)
    set_tests_properties(run_sequences_iteration PROPERTIES DEPENDS compile_sequences_iteration)

    add_test(NAME compile_for_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-iteration)
    add_test(NAME run_for_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-iteration)
    set_tests_properties(run_for_iteration PROPERTIES DEPENDS compile_for_iteration)
    add_test(NAME reject_for_non_iterable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_non_iterable.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-for-non-iterable)
    set_tests_properties(reject_for_non_iterable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_for_non_copy_element COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_non_copy_element.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-for-non-copy-element)
    set_tests_properties(reject_for_non_copy_element PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_for_duplicate_item COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_duplicate_item.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-for-duplicate-item)
    set_tests_properties(reject_for_duplicate_item PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_for_borrow_conflict COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_borrow_conflict.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-borrow-conflict)
    set_tests_properties(reject_for_borrow_conflict PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_for_array_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_array_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-array-iteration)
    add_test(NAME run_for_array_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-array-iteration)
    add_test(NAME reject_for_array_non_copy_element COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_array_non_copy_element.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-array-non-copy-element)
    set_tests_properties(reject_for_array_non_copy_element PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_for_mutable_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_mutable_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-mutable-iteration)
    add_test(NAME run_for_mutable_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-mutable-iteration)
    set_tests_properties(run_for_mutable_iteration PROPERTIES DEPENDS compile_for_mutable_iteration)
    add_test(NAME compile_for_mutable_box_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_mutable_box_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-mutable-box-iteration)
    add_test(NAME run_for_mutable_box_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-mutable-box-iteration)
    set_tests_properties(run_for_mutable_box_iteration PROPERTIES DEPENDS compile_for_mutable_box_iteration)
    add_test(NAME reject_for_mutable_requires_mutable_view COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_mutable_requires_mutable_view.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-mutable-requires-mutable-view)
    set_tests_properties(reject_for_mutable_requires_mutable_view PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_for_vec_consuming_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_vec_consuming_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-vec-consuming-iteration)
    add_test(NAME run_for_vec_consuming_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-vec-consuming-iteration)
    set_tests_properties(run_for_vec_consuming_iteration PROPERTIES DEPENDS compile_for_vec_consuming_iteration)
    add_test(NAME compile_for_vec_consuming_order COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_vec_consuming_order.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-vec-consuming-order)
    add_test(NAME run_for_vec_consuming_order COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-vec-consuming-order)
    set_tests_properties(run_for_vec_consuming_order PROPERTIES DEPENDS compile_for_vec_consuming_order)
    add_test(NAME compile_for_vec_consuming_drop_items COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_vec_consuming_drop_items.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-vec-consuming-drop-items)
    add_test(NAME run_for_vec_consuming_drop_items COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-vec-consuming-drop-items)
    set_tests_properties(run_for_vec_consuming_drop_items PROPERTIES DEPENDS compile_for_vec_consuming_drop_items)
    add_test(NAME reject_for_vec_consuming_use_after COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_vec_consuming_use_after.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-vec-consuming-use-after)
    set_tests_properties(reject_for_vec_consuming_use_after PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_for_string_char_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_string_char_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-string-char-iteration)
    add_test(NAME run_for_string_char_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-string-char-iteration)
    set_tests_properties(run_for_string_char_iteration PROPERTIES DEPENDS compile_for_string_char_iteration)
    add_test(NAME compile_for_string_view_char_iteration COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_string_view_char_iteration.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-string-view-char-iteration)
    add_test(NAME run_for_string_view_char_iteration COMMAND ${CMAKE_CURRENT_BINARY_DIR}/for-string-view-char-iteration)
    set_tests_properties(run_for_string_view_char_iteration PROPERTIES DEPENDS compile_for_string_view_char_iteration)
    add_test(NAME reject_for_string_mut COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/for_string_mut_rejected.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/for-string-mut-rejected)
    set_tests_properties(reject_for_string_mut PROPERTIES WILL_FAIL TRUE)

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
        NAME compile_stdlib_showcase
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/examples/stdlib_showcase.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/stdlib-showcase
    )
    add_test(NAME run_stdlib_showcase COMMAND ${CMAKE_CURRENT_BINARY_DIR}/stdlib-showcase)
    set_tests_properties(run_stdlib_showcase PROPERTIES
        DEPENDS compile_stdlib_showcase
        PASS_REGULAR_EXPRESSION "Démonstration terminée")

    add_test(
        NAME compile_resource_showcase
        COMMAND zeta ${CMAKE_CURRENT_SOURCE_DIR}/examples/resource_showcase/main.zeta
                -o ${CMAKE_CURRENT_BINARY_DIR}/resource-showcase
    )
    add_test(NAME run_resource_showcase COMMAND ${CMAKE_CURRENT_BINARY_DIR}/resource-showcase)
    set_tests_properties(run_resource_showcase PROPERTIES
        DEPENDS compile_resource_showcase
        PASS_REGULAR_EXPRESSION "Démonstration des traits terminée")

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

    add_test(NAME compile_unit_type COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit_type.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/unit-type)
    add_test(NAME run_unit_type COMMAND ${CMAKE_CURRENT_BINARY_DIR}/unit-type)
    set_tests_properties(run_unit_type PROPERTIES DEPENDS compile_unit_type)
    add_test(NAME reject_unit_missing_value COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit_missing_value.zeta)
    set_tests_properties(reject_unit_missing_value PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_guard_conditional_move COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/guard_conditional_move.zeta)
    set_tests_properties(reject_guard_conditional_move PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_never_binding COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/never_binding.zeta)
    set_tests_properties(reject_never_binding PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_vec_struct_field_mutation COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_struct_field_mutation.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-struct-field-mutation)
    add_test(NAME run_vec_struct_field_mutation
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-struct-field-mutation)
    set_tests_properties(run_vec_struct_field_mutation PROPERTIES
        DEPENDS compile_vec_struct_field_mutation)
    add_test(NAME reject_vec_struct_field_immutable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_struct_field_immutable.zeta)
    set_tests_properties(reject_vec_struct_field_immutable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_vec_struct_field_borrowed COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_struct_field_borrowed.zeta)
    set_tests_properties(reject_vec_struct_field_borrowed PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_vec_mut_reference COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_mut_reference.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/vec-mut-reference)
    add_test(NAME run_vec_mut_reference
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vec-mut-reference)
    set_tests_properties(run_vec_mut_reference PROPERTIES
        DEPENDS compile_vec_mut_reference)
    add_test(NAME reject_vec_shared_reference_mutation COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vec_shared_reference_mutation.zeta)
    set_tests_properties(reject_vec_shared_reference_mutation PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_user_methods COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_methods.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/user-methods)
    add_test(NAME run_user_methods COMMAND ${CMAKE_CURRENT_BINARY_DIR}/user-methods)
    set_tests_properties(run_user_methods PROPERTIES DEPENDS compile_user_methods)
    add_test(NAME reject_user_method_mutable_val COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_method_mutable_val.zeta)
    set_tests_properties(reject_user_method_mutable_val PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_user_method_borrow_conflict COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_method_borrow_conflict.zeta)
    set_tests_properties(reject_user_method_borrow_conflict PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_user_method_invalid_receiver COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_method_invalid_receiver.zeta)
    set_tests_properties(reject_user_method_invalid_receiver PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_extension_method_without_import COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/extension_method_without_import.zeta)
    set_tests_properties(reject_extension_method_without_import PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_extension_method_mutable_val COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/extension_method_mutable_val/main.zeta)
    set_tests_properties(reject_extension_method_mutable_val PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_extension_method_collision COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/extension_method_collision/main.zeta)
    set_tests_properties(reject_extension_method_collision PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_extension_method_builtin_override COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/extension_method_builtin_override.zeta)
    set_tests_properties(reject_extension_method_builtin_override PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_stack COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/stack.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/stack)
    add_test(NAME run_stack COMMAND ${CMAKE_CURRENT_BINARY_DIR}/stack)
    set_tests_properties(run_stack PROPERTIES DEPENDS compile_stack)
    add_test(NAME reject_stack_immutable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/stack_immutable.zeta)
    set_tests_properties(reject_stack_immutable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_stack_borrow_conflict COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/stack_borrow_conflict.zeta)
    set_tests_properties(reject_stack_borrow_conflict PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_generic_method_parameters COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_method_parameters.zeta)
    set_tests_properties(reject_generic_method_parameters PROPERTIES WILL_FAIL TRUE)
    add_test(NAME compile_queue COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/queue.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/queue)
    add_test(NAME run_queue COMMAND ${CMAKE_CURRENT_BINARY_DIR}/queue)
    set_tests_properties(run_queue PROPERTIES DEPENDS compile_queue)
    add_test(NAME reject_queue_immutable COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/queue_immutable.zeta)
    set_tests_properties(reject_queue_immutable PROPERTIES WILL_FAIL TRUE)
    add_test(NAME reject_queue_borrow_conflict COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/queue_borrow_conflict.zeta)
    set_tests_properties(reject_queue_borrow_conflict PROPERTIES WILL_FAIL TRUE)

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

    add_test(NAME compile_local_type_inference COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/local_type_inference.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/local-type-inference)
    add_test(NAME run_local_type_inference
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/local-type-inference)
    set_tests_properties(run_local_type_inference PROPERTIES
        DEPENDS compile_local_type_inference)
    add_test(NAME reject_local_type_inference_global COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/local_type_inference_global.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-local-type-inference-global)
    set_tests_properties(reject_local_type_inference_global PROPERTIES WILL_FAIL TRUE)
    foreach(inference_error
            local_type_inference_empty_array
            local_type_inference_unresolved_generic
            local_type_inference_owned_move)
        add_test(NAME reject_${inference_error} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/${inference_error}.zeta
            -o ${CMAKE_CURRENT_BINARY_DIR}/invalid-${inference_error})
        set_tests_properties(reject_${inference_error} PROPERTIES WILL_FAIL TRUE)
    endforeach()

    add_test(NAME compile_generic_constraints COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_constraints.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/generic-constraints)
    add_test(NAME run_generic_constraints COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generic-constraints)
    set_tests_properties(run_generic_constraints PROPERTIES DEPENDS compile_generic_constraints)
    add_test(NAME compile_multiple_constraints COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/multiple_constraints.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/multiple-constraints)
    add_test(NAME run_multiple_constraints
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/multiple-constraints)
    set_tests_properties(run_multiple_constraints PROPERTIES
        DEPENDS compile_multiple_constraints)
    add_test(NAME compile_user_traits COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_traits.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/user-traits)
    add_test(NAME run_user_traits COMMAND ${CMAKE_CURRENT_BINARY_DIR}/user-traits)
    set_tests_properties(run_user_traits PROPERTIES DEPENDS compile_user_traits)
    add_test(NAME compile_user_trait_methods COMMAND zeta
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/user_trait_methods.zeta
        -o ${CMAKE_CURRENT_BINARY_DIR}/user-trait-methods)
    add_test(NAME run_user_trait_methods
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/user-trait-methods)
    set_tests_properties(run_user_trait_methods PROPERTIES
        DEPENDS compile_user_trait_methods)
    foreach(trait_error
            user_trait_missing_impl
            user_trait_duplicate_impl
            user_trait_private_leak
            user_trait_method_missing
            user_trait_method_signature
            user_trait_method_ambiguous
            user_trait_method_unconstrained)
        add_test(NAME reject_${trait_error} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/${trait_error}.zeta)
        set_tests_properties(reject_${trait_error} PROPERTIES WILL_FAIL TRUE)
    endforeach()
    foreach(constraint_error
            multiple_constraints_duplicate
            multiple_constraints_unknown
            multiple_constraints_propagation
            multiple_constraints_failure)
        add_test(NAME reject_${constraint_error} COMMAND zeta
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/${constraint_error}.zeta)
        set_tests_properties(reject_${constraint_error} PROPERTIES WILL_FAIL TRUE)
    endforeach()
    add_test(NAME multiple_constraints_interfaces
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/multiple_constraints_interfaces.sh
            $<TARGET_FILE:zeta>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/multiple_constraints_interfaces)

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
