cmake_minimum_required(VERSION 3.5)

# Find Git and if available set the version variable
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --tags
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(PROJECT_VER "unknown")
endif()

# Configure version.h to place in the build directory
configure_file(${CMAKE_SOURCE_DIR}/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lizard)