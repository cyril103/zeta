if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "PROGRAM manquant")
endif()

execute_process(
    COMMAND "${PROGRAM}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
)

set(expected "ASCII é🚀\nfin\n")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "le programme io a retourné ${result}")
endif()
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "sortie inattendue: [${output}]")
endif()
