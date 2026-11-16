foreach(_argument EXPORT_TOOL EXPORT_INPUT EXPORT_OUTPUT)
    if(NOT DEFINED ${_argument})
        message(FATAL_ERROR "Missing ${_argument} for PSP export generation.")
    endif()
endforeach()

execute_process(
    COMMAND "${EXPORT_TOOL}" -b "${EXPORT_INPUT}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _source
    ERROR_VARIABLE _error)
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "psp-build-exports failed (${_result}): ${_error}")
endif()
file(WRITE "${EXPORT_OUTPUT}" "${_source}")
