cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The Lizard version for core.info(): the git describe ESP-IDF uses for PROJECT_VER, so that both agree
# in a plain build, with its reconfigure dependency on HEAD and the current ref, also when PROJECT_VER is given
git_describe(GIT_VERSION "${CMAKE_CURRENT_LIST_DIR}")
if(NOT GIT_VERSION)
    set(GIT_VERSION "unknown")
    if(NOT DEFINED PROJECT_VER)
        set(PROJECT_VER "unknown")
    endif()
endif()

# Configure version.h to place in the build directory
configure_file(${CMAKE_SOURCE_DIR}/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h)

# Integrators building this repo as a submodule can override the project name (e.g.
# idf.py -DLIZARD_PROJECT_NAME=lizard-zz build); it ends up in esp_app_desc_t.project_name,
# is reported by core.version(), and names the build artifacts (<name>.bin, <name>.elf).
# Like every -D option it is cached in the build directory until `idf.py fullclean`.
set(LIZARD_PROJECT_NAME lizard CACHE STRING "Project name (esp_app_desc_t.project_name, .bin/.elf names)")
string(LENGTH "${LIZARD_PROJECT_NAME}" LIZARD_PROJECT_NAME_LENGTH)
if(LIZARD_PROJECT_NAME_LENGTH GREATER 31)
    message(FATAL_ERROR "LIZARD_PROJECT_NAME '${LIZARD_PROJECT_NAME}' exceeds 31 characters; "
                        "ESP-IDF would truncate it in esp_app_desc_t.project_name.")
endif()

project(${LIZARD_PROJECT_NAME})
