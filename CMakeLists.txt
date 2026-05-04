# # *****************************************************************************
# # This source code is licensed under the MIT license found in the
# # LICENSE file in the root directory of this source tree.
# #
# # Copyright (c) 2024 Georgia Institute of Technology
# # *****************************************************************************

# CMake Requirement
cmake_minimum_required(VERSION 3.22)

# C++ requirement
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Setup project
project(AstraSim)

# Capture repository metadata for serving-mode run metadata output.
set(ASTRA_SIM_GIT_COMMIT "unknown")
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE ASTRA_SIM_GIT_COMMIT_RESULT
    OUTPUT_VARIABLE ASTRA_SIM_GIT_COMMIT_OUTPUT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(ASTRA_SIM_GIT_COMMIT_RESULT EQUAL 0 AND NOT ASTRA_SIM_GIT_COMMIT_OUTPUT STREQUAL "")
    set(ASTRA_SIM_GIT_COMMIT "${ASTRA_SIM_GIT_COMMIT_OUTPUT}")
endif()
set(ASTRA_SIM_VERSION "serving-week2-v1")

# Add libraries.
include(FetchContent)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/extern/helper/fmt")
option(SPDLOG_FMT_EXTERNAL ON) # override default option for spdlog.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/extern/helper/spdlog")
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG a83cd31 # Current as of 2025-10-01
)
FetchContent_MakeAvailable(yaml-cpp)


# Find Protobuf.
# Prefer the config package when it is available because it carries transitive
# dependencies such as Abseil for newer protobuf releases. Fall back to the
# classic module mode for environments that only expose FindProtobuf.cmake.
set(ASTRASIM_USE_PROTOBUF_CONFIG OFF)
find_package(protobuf CONFIG QUIET)
if(protobuf_FOUND)
    set(ASTRASIM_USE_PROTOBUF_CONFIG ON)
else()
    find_package(Protobuf REQUIRED)
endif()

# Files to compile
file(GLOB srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/workload/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/analytical/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/native_collectives/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/native_collectives/logical_topology/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/native_collectives/collective_algorithm/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/astraccl/custom_collectives/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/memory/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/scheduling/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/common/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/src/third_party/utils/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/schema/protobuf/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/src/feeder_v3/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/remote_memory_backend/analytical/*.cc")

# Compile AstraSim Library
add_library(AstraSim STATIC ${srcs})

# Link libraries
target_link_libraries(AstraSim PUBLIC fmt::fmt)
target_link_libraries(AstraSim PUBLIC spdlog::spdlog)
target_link_libraries(AstraSim PUBLIC yaml-cpp)
target_compile_definitions(
    AstraSim
    PUBLIC ASTRA_SIM_GIT_COMMIT="${ASTRA_SIM_GIT_COMMIT}"
           ASTRA_SIM_VERSION="${ASTRA_SIM_VERSION}"
)

# Same as above.
if(ASTRASIM_USE_PROTOBUF_CONFIG)
    target_link_libraries(AstraSim PUBLIC protobuf::libprotobuf)
else()
    target_link_libraries(AstraSim PUBLIC ${Protobuf_LIBRARIES})
endif()


# Include Directories
target_include_directories(AstraSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AstraSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/)
target_include_directories(AstraSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/schema/protobuf)
target_include_directories(AstraSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/graph_frontend/chakra/src/third_party/utils)
target_include_directories(AstraSim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/extern/helper/)
target_include_directories(AstraSim PUBLIC ${Protobuf_INCLUDE_DIR})

# Same as above.
if(ASTRASIM_USE_PROTOBUF_CONFIG)
    target_include_directories(AstraSim PUBLIC ${Protobuf_INCLUDE_DIRS})
endif()

# Properties
set_target_properties(AstraSim PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
set_target_properties(AstraSim
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../bin/
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../lib/
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../lib/
)
