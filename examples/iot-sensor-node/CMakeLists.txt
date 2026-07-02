set(example_deps
    microfsm
    microres
    microconf
    microlog
    microsh
    microcbor
    micoring
    microtimer
    microbus
)

set(example_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
)

set(example_include_dirs)

foreach(dep IN LISTS example_deps)
    string(TOUPPER "${dep}" dep_upper)
    set(dep_dir "${MICRO_TOOLKIT_${dep_upper}_DIR}")
    if(NOT EXISTS "${dep_dir}/include")
        message(FATAL_ERROR
            "Missing ${dep} include directory. Set MICRO_TOOLKIT_${dep_upper}_DIR to a valid checkout.")
    endif()
    if(NOT EXISTS "${dep_dir}/src")
        message(FATAL_ERROR
            "Missing ${dep} source directory. Set MICRO_TOOLKIT_${dep_upper}_DIR to a valid checkout.")
    endif()

    list(APPEND example_include_dirs "${dep_dir}/include")

    if(dep STREQUAL "microfsm")
        list(APPEND example_sources "${dep_dir}/src/mfsm.c")
    elseif(dep STREQUAL "microres")
        list(APPEND example_sources "${dep_dir}/src/mres.c")
    elseif(dep STREQUAL "microconf")
        list(APPEND example_sources "${dep_dir}/src/mconf.c")
    elseif(dep STREQUAL "microlog")
        list(APPEND example_sources "${dep_dir}/src/mlog.c")
    elseif(dep STREQUAL "microsh")
        list(APPEND example_sources "${dep_dir}/src/msh.c")
    elseif(dep STREQUAL "microcbor")
        list(APPEND example_sources "${dep_dir}/src/mcbor.c")
    elseif(dep STREQUAL "micoring")
        list(APPEND example_sources "${dep_dir}/src/mring.c")
    elseif(dep STREQUAL "microtimer")
        list(APPEND example_sources "${dep_dir}/src/mtimer.c")
    elseif(dep STREQUAL "microbus")
        list(APPEND example_sources "${dep_dir}/src/mbus.c")
    endif()
endforeach()

option(MICRO_TOOLKIT_EXAMPLE_MLOG_COLOR "Enable microlog ANSI color for the example build" ON)
option(MICRO_TOOLKIT_EXAMPLE_MLOG_TIMESTAMP "Enable microlog timestamps for the example build" ON)
option(MICRO_TOOLKIT_EXAMPLE_MSH_HISTORY "Enable microsh history for the example build" ON)
option(MICRO_TOOLKIT_EXAMPLE_MSH_COMPLETE "Enable microsh completion for the example build" ON)
option(MICRO_TOOLKIT_EXAMPLE_MCBOR_FLOAT32 "Enable microcbor float32 support for the example build" ON)

add_executable(micro_toolkit_iot_sensor_node ${example_sources})

target_include_directories(micro_toolkit_iot_sensor_node PRIVATE ${example_include_dirs})

target_compile_features(micro_toolkit_iot_sensor_node PRIVATE c_std_99)

target_compile_definitions(micro_toolkit_iot_sensor_node PRIVATE
    $<$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MLOG_COLOR}>:MLOG_ENABLE_COLOR=1>
    $<$<NOT:$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MLOG_COLOR}>>:MLOG_ENABLE_COLOR=0>
    $<$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MLOG_TIMESTAMP}>:MLOG_ENABLE_TIMESTAMP=1>
    $<$<NOT:$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MLOG_TIMESTAMP}>>:MLOG_ENABLE_TIMESTAMP=0>
    $<$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MSH_HISTORY}>:MSH_ENABLE_HISTORY=1>
    $<$<NOT:$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MSH_HISTORY}>>:MSH_ENABLE_HISTORY=0>
    $<$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MSH_COMPLETE}>:MSH_ENABLE_COMPLETE=1>
    $<$<NOT:$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MSH_COMPLETE}>>:MSH_ENABLE_COMPLETE=0>
    $<$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MCBOR_FLOAT32}>:MCBOR_ENABLE_FLOAT32=1>
    $<$<NOT:$<BOOL:${MICRO_TOOLKIT_EXAMPLE_MCBOR_FLOAT32}>>:MCBOR_ENABLE_FLOAT32=0>
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(micro_toolkit_iot_sensor_node PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(
    NAME iot_sensor_node_smoke
    COMMAND ${CMAKE_COMMAND}
        -DTEST_EXE_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke_test.cmake
)
