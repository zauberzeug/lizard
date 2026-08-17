#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t owl_parse_heap_requirement(size_t line_length);
size_t owl_parse_block_requirement(size_t line_length);

#ifdef __cplusplus
}
#endif
