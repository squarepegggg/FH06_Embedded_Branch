cmake_minimum_required(VERSION 3.13.1)

if(NOT TARGET app)
     message(FATAL_ERROR "Please create a target named 'app' (ex: add_executable(app)) before adding this file")
endif()

# Model bundle under ei-v2/ does not vendor edge-impulse-sdk; reuse the same SDK tree as ei-v6.
set(EI_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../ei-v6/edge-impulse-sdk")
if(NOT EXISTS "${EI_SDK_ROOT}/cmake/utils.cmake")
    message(FATAL_ERROR "Edge Impulse SDK missing at ${EI_SDK_ROOT}")
endif()
include("${EI_SDK_ROOT}/cmake/utils.cmake")
add_subdirectory("${EI_SDK_ROOT}/cmake/zephyr" "${CMAKE_BINARY_DIR}/ei-v2-edge-impulse-zephyr")

RECURSIVE_FIND_FILE_APPEND(MODEL_SOURCE "tflite-model" "*.cpp")
# Headers use "edge-impulse-sdk/..." (repo layout: <exporter-root>/edge-impulse-sdk/...).
target_include_directories(app PRIVATE . "${EI_SDK_ROOT}/..")

# add all sources to the project
target_sources(app PRIVATE ${MODEL_SOURCE})
