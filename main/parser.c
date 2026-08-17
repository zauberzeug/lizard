#define OWL_PARSER_IMPLEMENTATION
#include "parser.h"

#include "parser_heap.h"

// owl keeps one owl_token_run alive per OWL_TOKEN_RUN_LENGTH tokens until the tree is destroyed, and grows the
// varint parse tree by 1.5x, so an old and a new buffer coexist during each realloc. Lizard's grammar measures
// at ~2 bytes of tree per character and ~3 bytes of total peak; both bounds below leave room above that.
#define PARSE_HEAP_PER_CHAR 8
#define PARSE_TREE_PER_CHAR 4

size_t owl_parse_heap_requirement(size_t line_length) {
    const size_t runs = 1 + line_length / OWL_TOKEN_RUN_LENGTH;
    return runs * sizeof(struct owl_token_run) + PARSE_HEAP_PER_CHAR * line_length;
}

size_t owl_parse_block_requirement(size_t line_length) {
    const size_t tree = PARSE_TREE_PER_CHAR * line_length;
    return tree > sizeof(struct owl_token_run) ? tree : sizeof(struct owl_token_run);
}
