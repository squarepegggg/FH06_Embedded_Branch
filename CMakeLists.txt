#
# Copyright (c) 2024 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bma400_sample)

add_subdirectory(ei-v2)
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/bma400.c)
target_sources(app PRIVATE src/ei_glue_v2.cpp)


# Add CMSIS-NN include directories
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
zephyr_link_libraries(stdc++)
zephyr_link_libraries(supc++)
