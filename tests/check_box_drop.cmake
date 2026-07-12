file(READ "${IR_FILE}" ir)
if(NOT ir MATCHES "drop.*Box\\[Int\\]")
    message(FATAL_ERROR "instruction drop manquante dans ${IR_FILE}")
endif()
