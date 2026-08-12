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

# Integrators building this repo as a submodule can override the project name (e.g.
# idf.py -DLIZARD_PROJECT_NAME=lizard-zz build); it ends up in esp_app_desc_t.project_name,
# is reported by core.version(), and names the build artifacts (<name>.bin, <name>.elf).
if(NOT DEFINED LIZARD_PROJECT_NAME)
    set(LIZARD_PROJECT_NAME lizard)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${LIZARD_PROJECT_NAME})
