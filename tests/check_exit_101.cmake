execute_process(COMMAND "${PROGRAM}" RESULT_VARIABLE result)
if(NOT result EQUAL 101)
    message(FATAL_ERROR "code 101 attendu, reçu ${result}")
endif()
