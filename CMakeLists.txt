cmake_minimum_required(VERSION 3.20)

project(micro_toolkit LANGUAGES C)

option(MICRO_TOOLKIT_BUILD_EXAMPLES "Build integration examples" ON)

include(CTest)

set(MICRO_TOOLKIT_DEP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib" CACHE PATH
    "Default root directory for micro-toolkit dependency source trees")

if(DEFINED MICRO_TOOLKIT_MICRORING_DIR AND NOT DEFINED MICRO_TOOLKIT_MICORING_DIR)
    set(MICRO_TOOLKIT_MICORING_DIR "${MICRO_TOOLKIT_MICRORING_DIR}" CACHE PATH
        "Backward-compatible micoring source tree path" FORCE)
endif()

foreach(dep IN ITEMS microfsm microres microconf microlog microsh microcbor micoring microtimer microbus)
    string(TOUPPER "${dep}" dep_upper)
    set("MICRO_TOOLKIT_${dep_upper}_DIR" "${MICRO_TOOLKIT_DEP_ROOT}/${dep}" CACHE PATH
        "Path to the ${dep} dependency source tree")
endforeach()

set(MICRO_TOOLKIT_MICRORING_DIR "${MICRO_TOOLKIT_MICORING_DIR}" CACHE PATH
    "Alias for MICRO_TOOLKIT_MICORING_DIR" FORCE)

if(MICRO_TOOLKIT_BUILD_EXAMPLES)
    add_subdirectory(examples/iot-sensor-node)
endif()
