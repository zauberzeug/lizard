file(GLOB SRC_FILES
     "*.cpp"
     "*.h"
     "**/*.cpp"
     "**/*.h"
     "parser.c"
)

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "." "compilation" "modules" "utils" "${CMAKE_BINARY_DIR}/generated"
)

component_compile_options(-std=gnu++17 -Wsuggest-override)
set_source_files_properties(parser.c PROPERTIES COMPILE_FLAGS -Wno-missing-field-initializers)
