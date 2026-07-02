cmake_minimum_required(VERSION 3.20)

project(micro_toolkit LANGUAGES C)

option(MICRO_TOOLKIT_BUILD_EXAMPLES "Build integration examples" ON)

set(MICRO_TOOLKIT_DEP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib" CACHE PATH
    "Default root directory for micro-toolkit dependency source trees")

foreach(dep IN ITEMS microfsm microres microconf microlog microsh microcbor micoring microtimer microbus)
    string(TOUPPER "${dep}" dep_upper)
    set("MICRO_TOOLKIT_${dep_upper}_DIR" "${MICRO_TOOLKIT_DEP_ROOT}/${dep}" CACHE PATH
        "Path to the ${dep} dependency source tree")
endforeach()

if(MICRO_TOOLKIT_BUILD_EXAMPLES)
    add_subdirectory(examples/iot-sensor-node)
endif()
