set(_candidate_dirs
    "${TEST_EXE_DIR}/Debug"
    "${TEST_EXE_DIR}/Release"
    "${TEST_EXE_DIR}/RelWithDebInfo"
    "${TEST_EXE_DIR}/MinSizeRel"
    "${TEST_EXE_DIR}"
)

set(_exe_path "")
foreach(_dir IN LISTS _candidate_dirs)
    set(_candidate "${_dir}/micro_toolkit_iot_sensor_node${CMAKE_EXECUTABLE_SUFFIX}")
    if(EXISTS "${_candidate}")
        set(_exe_path "${_candidate}")
        break()
    endif()
endforeach()

if(_exe_path STREQUAL "")
    message(FATAL_ERROR "Smoke test executable not found under ${TEST_EXE_DIR}")
endif()

execute_process(
    COMMAND "${_exe_path}"
    RESULT_VARIABLE _result
)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Smoke test failed with exit code ${_result}")
endif()
