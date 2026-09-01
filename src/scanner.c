#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Portable compile-time assertion. C11's `_Static_assert` would be cleaner,
// but `binding.gyp` can't pass `/std:c11` to MSVC without colliding with the
// `/std:c++20` that node-addon-api forces on the shared C/C++ target, so
// fall back to the negative-sized-array trick that every C dialect accepts.
#define TS_MD_STATIC_ASSERT(cond, tag) \
    typedef char ts_md_static_assert_##tag[(cond) ? 1 : -1]

enum {
    // Tab stop used when counting columns for indentation.
    TAB_STOP = 4,
    // Minimum indentation (spaces) that makes a line an indented code block.
    INDENTED_CODE_INDENT = 4,
    // Maximum indentation before a line starts being a list-item continuation.
    MAX_NON_CODE_INDENT = 3,
    // Minimum fence length for a fenced code block.
    FENCED_CODE_MIN_FENCE = 3,
    // Minimum run of `*`/`_`/`-` that forms a thematic break.
    THEMATIC_BREAK_MIN = 3,
    // Number of ATX heading levels (H1..H6).
    ATX_HEADING_LEVELS = 6,
    // Maximum number of digits allowed in an ordered-list marker.
    ORDERED_LIST_MAX_DIGITS = 9,
    // Metadata fences (`---` / `+++`) are exactly three characters wide.
    METADATA_FENCE_WIDTH = 3,
    // Directive block delimiters are exactly three colons (`:::`).
    DIRECTIVE_DELIMITER_LENGTH = 3,
    // Initial capacity for the open-blocks stack.
    OPEN_BLOCKS_INITIAL_CAPACITY = 8,
    // Length of the CDATA prefix (`<![CDATA[`).
    CDATA_PREFIX_LEN = 9,
    // Maximum number of characters captured for HTML tag-name matching.
    HTML_TAG_NAME_MAX = 10,
    // Buffer holds up to HTML_TAG_NAME_MAX plus a null terminator.
    HTML_TAG_NAME_BUFFER = 11,
    // Sentinel written into name_length when a tag name overflows the buffer.
    HTML_TAG_NAME_TOO_LONG = 12,
    // Serialized integers are little-endian.
    SERIALIZED_BYTE_BITS = 8,
    SERIALIZED_U32_BYTE_1_SHIFT = 8,
    SERIALIZED_U32_BYTE_2_SHIFT = 16,
    SERIALIZED_U32_BYTE_3_SHIFT = 24,
    // Fixed header bytes that `serialize`/`deserialize` write/read before the
    // trailing block-byte payload: state, matched, indentation, column,
    // fenced_code_block_delimiter_length.
    SERIALIZED_HEADER_SIZE = 10,
    SERIALIZED_BLOCK_SIZE = 1,
};

// For explanation of the tokens see grammar.js
typedef enum {
    LINE_ENDING,
    SOFT_LINE_ENDING,
    BLOCK_CLOSE,
    BLOCK_CONTINUATION,
    BLOCK_QUOTE_START,
    INDENTED_CHUNK_START,
    ATX_H1_MARKER,
    ATX_H2_MARKER,
    ATX_H3_MARKER,
    ATX_H4_MARKER,
    ATX_H5_MARKER,
    ATX_H6_MARKER,
    SETEXT_H1_UNDERLINE,
    SETEXT_H2_UNDERLINE,
    THEMATIC_BREAK,
    LIST_MARKER_MINUS,
    LIST_MARKER_PLUS,
    LIST_MARKER_STAR,
    LIST_MARKER_PARENTHESIS,
    LIST_MARKER_DOT,
    LIST_MARKER_MINUS_DONT_INTERRUPT,
    LIST_MARKER_PLUS_DONT_INTERRUPT,
    LIST_MARKER_STAR_DONT_INTERRUPT,
    LIST_MARKER_PARENTHESIS_DONT_INTERRUPT,
    LIST_MARKER_DOT_DONT_INTERRUPT,
    TASK_LIST_MARKER_CHECKED,
    TASK_LIST_MARKER_UNCHECKED,
    MATH_INLINE_OPEN_DELIMITER,
    MATH_INLINE_CLOSE_DELIMITER,
    MATH_BLOCK_OPEN_DELIMITER,
    MATH_BLOCK_CLOSE_DELIMITER,
    DIRECTIVE_BLOCK_OPEN_DELIMITER,
    DIRECTIVE_BLOCK_CLOSE_DELIMITER,
    FENCED_CODE_BLOCK_START_BACKTICK,
    FENCED_CODE_BLOCK_START_TILDE,
    BLANK_LINE_START,
    FENCED_CODE_BLOCK_END_BACKTICK,
    FENCED_CODE_BLOCK_END_TILDE,
    HTML_BLOCK_1_START,
    HTML_BLOCK_1_END,
    HTML_BLOCK_2_START,
    HTML_BLOCK_3_START,
    HTML_BLOCK_4_START,
    HTML_BLOCK_5_START,
    HTML_BLOCK_6_START,
    HTML_BLOCK_7_START,
    CLOSE_BLOCK,
    NO_INDENTED_CHUNK,
    ERROR,
    TRIGGER_ERROR,
    TOKEN_EOF,
    MINUS_METADATA,
    PLUS_METADATA,
    PIPE_TABLE_START,
    PIPE_TABLE_LINE_ENDING,
    EMPHASIS_STAR_OPEN,
    EMPHASIS_STAR_CLOSE,
    EMPHASIS_UNDERSCORE_OPEN,
    EMPHASIS_UNDERSCORE_CLOSE,
    STRONG_STAR_OPEN,
    STRONG_STAR_CLOSE,
    STRONG_UNDERSCORE_OPEN,
    STRONG_UNDERSCORE_CLOSE,
    STRIKETHROUGH_OPEN,
    STRIKETHROUGH_CLOSE,
    AUTOLINK_OPEN,
    FOOTNOTE_REF_OPEN,
    INLINE_CODE_BACKTICK_1_OPEN,
    INLINE_CODE_BACKTICK_1_CLOSE,
    INLINE_CODE_BACKTICK_2_OPEN,
    INLINE_CODE_BACKTICK_2_CLOSE,
    SCANNER_TOKEN_TYPE_COUNT,
} TokenType;

// Description of a block on the block stack.
//
// LIST_ITEM is a list item with minimal indentation (content begins at indent
// level 2) while LIST_ITEM_MAX_INDENTATION represents a list item with maximal
// indentation without being considered a indented code block.
//
// ANONYMOUS represents any block that whose close is not handled by the
// external s.
typedef enum {
    BLOCK_QUOTE,
    INDENTED_CODE_BLOCK,
    LIST_ITEM,
    LIST_ITEM_1_INDENTATION,
    LIST_ITEM_2_INDENTATION,
    LIST_ITEM_3_INDENTATION,
    LIST_ITEM_4_INDENTATION,
    LIST_ITEM_5_INDENTATION,
    LIST_ITEM_6_INDENTATION,
    LIST_ITEM_7_INDENTATION,
    LIST_ITEM_8_INDENTATION,
    LIST_ITEM_9_INDENTATION,
    LIST_ITEM_10_INDENTATION,
    LIST_ITEM_11_INDENTATION,
    LIST_ITEM_12_INDENTATION,
    LIST_ITEM_13_INDENTATION,
    LIST_ITEM_14_INDENTATION,
    LIST_ITEM_MAX_INDENTATION,
    FENCED_CODE_BLOCK,
    ANONYMOUS,
} Block;

// Determines if a character is punctuation as defined by the markdown spec.
// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool is_punctuation(char chr) {
    return (chr >= '!' && chr <= '/') || (chr >= ':' && chr <= '@') ||
           (chr >= '[' && chr <= '`') || (chr >= '{' && chr <= '~');
}

static bool is_ascii_digit(int32_t codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}

static bool is_ascii_alpha(int32_t codepoint) {
    return (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z');
}

static bool is_ascii_alnum(int32_t codepoint) {
    return is_ascii_alpha(codepoint) || is_ascii_digit(codepoint);
}

static char ascii_tolower(int32_t codepoint) {
    if (codepoint >= 'A' && codepoint <= 'Z') {
        return (char)(codepoint - 'A' + 'a');
    }
    return (char)codepoint;
}

static size_t max_serialized_blocks(void) {
    return ((size_t)TREE_SITTER_SERIALIZATION_BUFFER_SIZE -
            (size_t)SERIALIZED_HEADER_SIZE) /
           (size_t)SERIALIZED_BLOCK_SIZE;
}

// Returns the indentation level which lines of a list item should have at
// minimum. Should only be called with blocks for which `is_list_item` returns
// true.
static uint16_t list_item_indentation(Block block) {
    return (uint16_t)(block - LIST_ITEM + 2);
}

enum {
    NUM_HTML_TAG_NAMES_RULE_1 = 3,
    NUM_HTML_TAG_NAMES_RULE_7 = 62,
};

static const char *const HTML_TAG_NAMES_RULE_1[NUM_HTML_TAG_NAMES_RULE_1] = {
    "pre", "script", "style"};

static const char *const HTML_TAG_NAMES_RULE_7[NUM_HTML_TAG_NAMES_RULE_7] = {
    "address",  "article",    "aside",  "base",     "basefont", "blockquote",
    "body",     "caption",    "center", "col",      "colgroup", "dd",
    "details",  "dialog",     "dir",    "div",      "dl",       "dt",
    "fieldset", "figcaption", "figure", "footer",   "form",     "frame",
    "frameset", "h1",         "h2",     "h3",       "h4",       "h5",
    "h6",       "head",       "header", "hr",       "html",     "iframe",
    "legend",   "li",         "link",   "main",     "menu",     "menuitem",
    "nav",      "noframes",   "ol",     "optgroup", "option",   "p",
    "param",    "section",    "source", "summary",  "table",    "tbody",
    "td",       "tfoot",      "th",     "thead",    "title",    "tr",
    "track",    "ul"};

// For explanation of the tokens see grammar.js. Designated initializers keep
// this table tied to TokenType names rather than enum positions.
static const bool paragraph_interrupt_symbols[SCANNER_TOKEN_TYPE_COUNT] = {
    [BLOCK_QUOTE_START] = true,
    [ATX_H1_MARKER] = true,
    [ATX_H2_MARKER] = true,
    [ATX_H3_MARKER] = true,
    [ATX_H4_MARKER] = true,
    [ATX_H5_MARKER] = true,
    [ATX_H6_MARKER] = true,
    [SETEXT_H1_UNDERLINE] = true,
    [SETEXT_H2_UNDERLINE] = true,
    [THEMATIC_BREAK] = true,
    [LIST_MARKER_MINUS] = true,
    [LIST_MARKER_PLUS] = true,
    [LIST_MARKER_STAR] = true,
    [LIST_MARKER_PARENTHESIS] = true,
    [LIST_MARKER_DOT] = true,
    [FENCED_CODE_BLOCK_START_BACKTICK] = true,
    [FENCED_CODE_BLOCK_START_TILDE] = true,
    [BLANK_LINE_START] = true,
    [HTML_BLOCK_1_START] = true,
    [HTML_BLOCK_2_START] = true,
    [HTML_BLOCK_3_START] = true,
    [HTML_BLOCK_4_START] = true,
    [HTML_BLOCK_5_START] = true,
    [HTML_BLOCK_6_START] = true,
    [PIPE_TABLE_START] = true,
};

// State bitflags used with `Scanner.state`.
enum {
    STATE_MATCHING = 1U << 0U,            // Currently matching at the beginning of a line.
    STATE_WAS_SOFT_LINE_BREAK = 1U << 1U, // Last line break was inside a paragraph.
    STATE_CLOSE_BLOCK = 1U << 4U,         // Block should be closed after next line break.
    STATE_ALL = STATE_MATCHING | STATE_WAS_SOFT_LINE_BREAK | STATE_CLOSE_BLOCK,
};
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)

TS_MD_STATIC_ASSERT(ATX_H6_MARKER == ATX_H1_MARKER + (ATX_HEADING_LEVELS - 1),
                    atx_markers_contiguous);
TS_MD_STATIC_ASSERT(SCANNER_TOKEN_TYPE_COUNT == INLINE_CODE_BACKTICK_2_CLOSE + 1,
                    token_type_count_trails_enum);
TS_MD_STATIC_ASSERT(ANONYMOUS <= UINT8_MAX,
                    block_fits_in_one_byte);
TS_MD_STATIC_ASSERT(
    SERIALIZED_HEADER_SIZE <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE,
    serialized_header_fits_in_buffer);

typedef struct {
    // A stack of open blocks in the current parse state
    struct {
        size_t size;
        size_t capacity;
        Block *items;
    } open_blocks;

    // Parser state flags
    uint8_t state;
    // Number of blocks that have been matched so far. Only changes during
    // matching and is reset after every line ending.
    size_t matched;
    // Consumed but "unused" indentation. Sometimes a tab needs to be "split" to
    // be used in multiple tokens.
    uint16_t indentation;
    // The current column. Used to decide how many spaces a tab should equal
    uint8_t column;
    // The delimiter length of the currently open fenced code block
    uint32_t fenced_code_block_delimiter_length;

    bool simulate;
} Scanner;

typedef struct {
    size_t open_blocks_size;
    uint8_t state;
    size_t matched;
    uint16_t indentation;
    uint8_t column;
    uint32_t fenced_code_block_delimiter_length;
    bool simulate;
} ScannerSnapshot;

typedef struct {
    TokenType result_symbol;
    uint16_t extra_indentation;
    uint16_t marker_width_adjust;
} ListMarker;

typedef struct {
    char delimiter;
    TokenType result_symbol;
} MetadataFence;

// NOLINTNEXTLINE(readability-identifier-length)
static ScannerSnapshot snapshot_scanner(const Scanner *s) {
    ScannerSnapshot snapshot = {
        .open_blocks_size = s->open_blocks.size,
        .state = s->state,
        .matched = s->matched,
        .indentation = s->indentation,
        .column = s->column,
        .fenced_code_block_delimiter_length =
            s->fenced_code_block_delimiter_length,
        .simulate = s->simulate,
    };
    return snapshot;
}

// NOLINTNEXTLINE(readability-identifier-length)
static void restore_scanner(Scanner *s, ScannerSnapshot snapshot) {
    s->open_blocks.size = snapshot.open_blocks_size;
    s->state = snapshot.state;
    s->matched = snapshot.matched;
    s->indentation = snapshot.indentation;
    s->column = snapshot.column;
    s->fenced_code_block_delimiter_length =
        snapshot.fenced_code_block_delimiter_length;
    s->simulate = snapshot.simulate;
}

// NOLINTNEXTLINE(readability-identifier-length)
static bool restore_scanner_and_return(Scanner *s, ScannerSnapshot snapshot,
                                       bool result) {
    restore_scanner(s, snapshot);
    return result;
}

static void add_indentation(uint16_t *indentation, size_t amount) {
    if ((size_t)UINT16_MAX - (size_t)(*indentation) < amount) {
        *indentation = UINT16_MAX;
    } else {
        *indentation = (uint16_t)(*indentation + amount);
    }
}

// NOLINTNEXTLINE(readability-identifier-length) — `s`/`b` are the scanner/block conventions throughout this file
static bool push_block(Scanner *s, Block b) {
    size_t max_blocks = max_serialized_blocks();
    if (s->open_blocks.size >= max_blocks) {
        return false;
    }
    if (s->open_blocks.size == s->open_blocks.capacity) {
        size_t capacity = s->open_blocks.capacity != 0U
                              ? s->open_blocks.capacity << 1U
                              : (size_t)OPEN_BLOCKS_INITIAL_CAPACITY;
        if (capacity > max_blocks) {
            capacity = max_blocks;
        }
        void *tmp = ts_realloc(s->open_blocks.items,
                               sizeof(Block) * capacity);
        if (tmp == NULL) {
            return false;
        }
        s->open_blocks.items = tmp;
        s->open_blocks.capacity = capacity;
    }

    s->open_blocks.items[s->open_blocks.size++] = b;
    return true;
}

// NOLINTNEXTLINE(readability-identifier-length)
static inline Block pop_block(Scanner *s) {
    return s->open_blocks.items[--s->open_blocks.size];
}

// NOLINTNEXTLINE(readability-identifier-length)
static void write_u16(char *buffer, unsigned *size, uint16_t value) {
    buffer[(*size)++] = (char)(value & UINT8_MAX);
    buffer[(*size)++] = (char)(value >> SERIALIZED_BYTE_BITS);
}

// NOLINTNEXTLINE(readability-identifier-length)
static void write_u32(char *buffer, unsigned *size, uint32_t value) {
    buffer[(*size)++] = (char)(value & UINT8_MAX);
    buffer[(*size)++] =
        (char)((value >> SERIALIZED_U32_BYTE_1_SHIFT) & UINT8_MAX);
    buffer[(*size)++] =
        (char)((value >> SERIALIZED_U32_BYTE_2_SHIFT) & UINT8_MAX);
    buffer[(*size)++] =
        (char)((value >> SERIALIZED_U32_BYTE_3_SHIFT) & UINT8_MAX);
}

// NOLINTNEXTLINE(readability-identifier-length)
static uint16_t read_u16(const char *buffer, unsigned *size) {
    uint16_t byte_0 = (uint8_t)buffer[(*size)++];
    uint16_t byte_1 = (uint8_t)buffer[(*size)++];
    return (uint16_t)(byte_0 | (uint16_t)(byte_1 << SERIALIZED_BYTE_BITS));
}

// NOLINTNEXTLINE(readability-identifier-length)
static uint32_t read_u32(const char *buffer, unsigned *size) {
    uint32_t byte_0 = (uint8_t)buffer[(*size)++];
    uint32_t byte_1 = (uint8_t)buffer[(*size)++];
    uint32_t byte_2 = (uint8_t)buffer[(*size)++];
    uint32_t byte_3 = (uint8_t)buffer[(*size)++];
    return byte_0 | (byte_1 << SERIALIZED_U32_BYTE_1_SHIFT) |
           (byte_2 << SERIALIZED_U32_BYTE_2_SHIFT) |
           (byte_3 << SERIALIZED_U32_BYTE_3_SHIFT);
}

static bool is_valid_block_value(uint8_t value) {
    return value <= (uint8_t)ANONYMOUS;
}

// Write the whole state of a Scanner to a byte buffer.
// NOLINTNEXTLINE(readability-identifier-length)
static unsigned serialize(Scanner *s, char *buffer) {
    unsigned size = 0;
    buffer[size++] = (char)s->state;
    write_u16(buffer, &size, (uint16_t)s->matched);
    write_u16(buffer, &size, s->indentation);
    buffer[size++] = (char)s->column;
    write_u32(buffer, &size, s->fenced_code_block_delimiter_length);
    assert(size == SERIALIZED_HEADER_SIZE);
    size_t max_blocks = max_serialized_blocks();
    size_t blocks_count = s->open_blocks.size < max_blocks
                              ? s->open_blocks.size
                              : max_blocks;
    for (size_t i = 0; i < blocks_count; i++) {
        buffer[size++] = (char)s->open_blocks.items[i];
    }
    return size;
}

// Read the whole state of a Scanner from a byte buffer.
// `serialize` and `deserialize` should be fully symmetric.
// NOLINTNEXTLINE(readability-identifier-length)
static void deserialize(Scanner *s, const char *buffer, unsigned length) {
    s->open_blocks.size = 0;
    s->state = 0;
    s->matched = 0;
    s->indentation = 0;
    s->column = 0;
    s->fenced_code_block_delimiter_length = 0;
    // The serialized form is a fixed header followed by one byte per Block.
    // Validate all fields before applying them so corrupted buffers resume from
    // a clean state instead of a partially restored one.
    if (length < SERIALIZED_HEADER_SIZE) {
        return;
    }
    size_t blocks_count = (size_t)length - (size_t)SERIALIZED_HEADER_SIZE;
    if (blocks_count > max_serialized_blocks()) {
        return;
    }
    unsigned size = 0;
    uint8_t state = (uint8_t)buffer[size++];
    size_t matched = (size_t)read_u16(buffer, &size);
    uint16_t indentation = read_u16(buffer, &size);
    uint8_t column = (uint8_t)buffer[size++];
    uint32_t fenced_code_block_delimiter_length = read_u32(buffer, &size);
    assert(size == SERIALIZED_HEADER_SIZE);
    if ((state & (uint8_t)(~STATE_ALL)) != 0 || column >= TAB_STOP ||
        matched > blocks_count) {
        return;
    }
    size_t block_offset = size;
    for (size_t i = 0; i < blocks_count; i++) {
        if (!is_valid_block_value((uint8_t)buffer[block_offset + i])) {
            return;
        }
    }
    if (blocks_count > 0 && s->open_blocks.capacity < blocks_count) {
        void *tmp = ts_realloc(s->open_blocks.items,
                               sizeof(Block) * blocks_count);
        if (tmp == NULL) {
            return;
        }
        s->open_blocks.items = tmp;
        s->open_blocks.capacity = blocks_count;
    }
    s->state = state;
    s->matched = matched;
    s->indentation = indentation;
    s->column = column;
    s->fenced_code_block_delimiter_length =
        fenced_code_block_delimiter_length;
    for (size_t i = 0; i < blocks_count; i++) {
        s->open_blocks.items[i] = (Block)(uint8_t)buffer[block_offset + i];
    }
    s->open_blocks.size = blocks_count;
}

// NOLINTNEXTLINE(readability-identifier-length)
static void mark_end(Scanner *s, TSLexer *lexer) {
    if (!s->simulate) {
        lexer->mark_end(lexer);
    }
}

// Convenience function to emit the error token. This is done to stop invalid
// parse branches. Specifically:
// 1. When encountering a newline after a line break that ended a paragraph, and
// no new block
//    has been opened.
// 2. When encountering a new block after a soft line break.
// 3. When a `$._trigger_error` token is valid, which is used to stop parse
// branches through
//    normal tree-sitter grammar rules.
//
// See also the `$._soft_line_break` and `$._paragraph_end_newline` tokens in
// grammar.js
static bool error(TSLexer *lexer) {
    lexer->result_symbol = ERROR;
    return true;
}

// Advance the lexer one character
// Also keeps track of the current column, counting tabs as spaces with tab stop
// 4 See https://github.github.com/gfm/#tabs
// NOLINTNEXTLINE(readability-identifier-length)
static size_t advance(Scanner *s, TSLexer *lexer) {
    size_t size = 1;
    if (lexer->lookahead == '\t') {
        size = (size_t)(TAB_STOP - s->column);
        s->column = 0;
    } else {
        s->column = (uint8_t)((s->column + 1) % TAB_STOP);
    }
    lexer->advance(lexer, false);
    return size;
}

// Try to match the given block, i.e. consume all tokens that belong to the
// block. These are
// 1. indentation for list items and indented code blocks
// 2. '>' for block quotes
// Returns true if the block is matched and false otherwise
// NOLINTNEXTLINE(readability-identifier-length,readability-function-cognitive-complexity)
static bool match(Scanner *s, TSLexer *lexer, Block block) {
    switch (block) {
        case INDENTED_CODE_BLOCK:
            while (s->indentation < INDENTED_CODE_INDENT) {
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation, advance(s, lexer));
                } else {
                    break;
                }
            }
            if (s->indentation >= INDENTED_CODE_INDENT &&
                lexer->lookahead != '\n' && lexer->lookahead != '\r') {
                s->indentation -= INDENTED_CODE_INDENT;
                return true;
            }
            break;
        case LIST_ITEM:
        case LIST_ITEM_1_INDENTATION:
        case LIST_ITEM_2_INDENTATION:
        case LIST_ITEM_3_INDENTATION:
        case LIST_ITEM_4_INDENTATION:
        case LIST_ITEM_5_INDENTATION:
        case LIST_ITEM_6_INDENTATION:
        case LIST_ITEM_7_INDENTATION:
        case LIST_ITEM_8_INDENTATION:
        case LIST_ITEM_9_INDENTATION:
        case LIST_ITEM_10_INDENTATION:
        case LIST_ITEM_11_INDENTATION:
        case LIST_ITEM_12_INDENTATION:
        case LIST_ITEM_13_INDENTATION:
        case LIST_ITEM_14_INDENTATION:
        case LIST_ITEM_MAX_INDENTATION:
            while (s->indentation < list_item_indentation(block)) {
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation, advance(s, lexer));
                } else {
                    break;
                }
            }
            if (s->indentation >= list_item_indentation(block)) {
                s->indentation -= list_item_indentation(block);
                return true;
            }
            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                s->indentation = 0;
                return true;
            }
            break;
        case BLOCK_QUOTE:
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                add_indentation(&s->indentation, advance(s, lexer));
            }
            if (lexer->lookahead == '>') {
                advance(s, lexer);
                s->indentation = 0;
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation,
                                    advance(s, lexer) - 1U);
                }
                return true;
            }
            break;
        case FENCED_CODE_BLOCK:
        case ANONYMOUS:
            return true;
        default:
            break;
    }
    return false;
}

// NOLINTNEXTLINE(readability-identifier-length)
static bool push_list_item(Scanner *s, uint16_t block_offset) {
    uint16_t max_offset =
        (uint16_t)(LIST_ITEM_MAX_INDENTATION - LIST_ITEM);
    if (block_offset > max_offset) {
        return false;
    }
    return push_block(s, (Block)(LIST_ITEM + block_offset));
}

// NOLINTNEXTLINE(readability-identifier-length)
static bool finish_list_marker(Scanner *s, TSLexer *lexer, ListMarker marker) {
    assert(marker.extra_indentation >= 1);
    marker.extra_indentation--;
    uint16_t block_offset = 0;
    if (marker.extra_indentation <= MAX_NON_CODE_INDENT) {
        add_indentation(&marker.extra_indentation, s->indentation);
        s->indentation = 0;
        block_offset = marker.extra_indentation;
    } else {
        block_offset = s->indentation;
        s->indentation = marker.extra_indentation;
    }
    add_indentation(&block_offset, marker.marker_width_adjust);
    if (!s->simulate && !push_list_item(s, block_offset)) {
        return false;
    }
    lexer->result_symbol = marker.result_symbol;
    return true;
}

// NOLINTNEXTLINE(readability-identifier-length)
static void consume_line_ending(Scanner *s, TSLexer *lexer) {
    if (lexer->lookahead == '\r') {
        advance(s, lexer);
        if (lexer->lookahead == '\n') {
            advance(s, lexer);
        }
    } else {
        advance(s, lexer);
    }
}

// NOLINTNEXTLINE(readability-identifier-length)
static void skip_horizontal_space(Scanner *s, TSLexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        advance(s, lexer);
    }
}

// NOLINTNEXTLINE(readability-identifier-length)
static bool scan_metadata_block(Scanner *s, TSLexer *lexer,
                                MetadataFence fence) {
    for (;;) {
        consume_line_ending(s, lexer);
        size_t delimiter_count = 0;
        while (lexer->lookahead == fence.delimiter) {
            delimiter_count++;
            advance(s, lexer);
        }
        if (delimiter_count == METADATA_FENCE_WIDTH) {
            skip_horizontal_space(s, lexer);
            if (lexer->lookahead == '\r' || lexer->lookahead == '\n') {
                consume_line_ending(s, lexer);
                mark_end(s, lexer);
                lexer->result_symbol = fence.result_symbol;
                return true;
            }
        }
        while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               !lexer->eof(lexer)) {
            advance(s, lexer);
        }
        if (lexer->eof(lexer)) {
            return false;
        }
    }
}


// ---------------------------------------------------------------------------
// looks_like_block_start: heuristic check run at the FIRST character of each
// new line inside has_closing_delimiter.  Returns true when the line looks
// like the start of a block element that would interrupt a paragraph, meaning
// the inline span cannot legally cross this boundary.
//
// Covered interrupting patterns:
//   ATX heading       : 1-6 `#` characters followed by a space or end-of-line.
//   Fenced code fence : 3+ backticks (```) or 3+ tildes (~~~) at line start.
//   Block-quote marker: line starts with `>`.
//   Thematic break    : line starts with `-`, `*`, or `_` — we do a simple
//                       first-character check only (not a full thematic-break
//                       parse) because the scanner cannot advance further
//                       without corrupting the search position.  This catches
//                       the common `---` / `***` / `___` forms; it will also
//                       fire for `- item` (list marker), which is acceptable
//                       because list items are another CommonMark interrupt.
//   Setext underline  : line starts with `=` (setext H1 underline).
//   Ordered list      : line starts with an ASCII digit (`1. ` / `1) `).
//
// NOT covered (known limitations — tracked for future work):
//   - HTML block start tags: complex to detect correctly in raw-text scan.
//   - Indented code blocks: require column-counting, not feasible here.
//   - Underscore-emphasis closer as the ONLY character on a new line
//     (e.g. `_text\n_`): the closing `_` is indistinguishable from the start
//     of a thematic break (`___`) without advancing the lexer, so the search
//     stops there and the emphasis is not formed.  Error direction is safe:
//     emphasis silently degrades to plain text, the document is not damaged.
//
// The lexer is NOT advanced by this function; it reads only lexer->lookahead.
// ---------------------------------------------------------------------------
// NOLINTBEGIN(readability-identifier-length)
static bool looks_like_block_start(TSLexer *lexer) {
    int32_t ch = lexer->lookahead;

    // ATX heading: `#` (1-6 times) followed by space or end-of-line.
    // We only peek at the first character; the heading-level check is done by
    // verifying it IS a `#`.  The space/EOL requirement is not checked here
    // (we cannot advance), but a lone `#` at line-start is overwhelmingly a
    // heading in practice.
    if (ch == '#') {
        return true;
    }

    // Block-quote marker.
    if (ch == '>') {
        return true;
    }

    // Fenced code fence: ``` or ~~~.  First character is enough signal.
    if (ch == '`' || ch == '~') {
        return true;
    }

    // Thematic break / list-marker first character.
    // `-` covers `---` thematic break and `- item` list marker.
    // `*` covers `***` thematic break (and would also be a `* item` list).
    // `_` covers `___` thematic break.
    // These are all valid paragraph-interrupting elements.
    if (ch == '-' || ch == '_') {
        return true;
    }

    // Setext heading underline (`===`).  A line beginning with `=` at line
    // start is overwhelmingly a setext H1 underline in practice; treating it
    // as a block start stops an unclosed inline delimiter from swallowing it.
    if (ch == '=') {
        return true;
    }

    // Ordered list marker (`1. ` / `1) `).  A line beginning with an ASCII
    // digit at line start is overwhelmingly an ordered-list item (or a setext
    // H2-style continuation).  This is a conservative first-character check in
    // the safe direction: a prose line that merely starts with a digit will
    // stop the emphasis search and degrade to text, never to ERROR/runaway.
    if (is_ascii_digit(ch)) {
        return true;
    }

    return false;
}
// NOLINTEND(readability-identifier-length)


// ---------------------------------------------------------------------------
// Universal lookahead helper: does a closing delimiter exist before the next
// block boundary?
//
// Scans forward from the CURRENT lexer position looking for a run of exactly
// `close_len` consecutive `close_char` characters whose length equals
// `close_len` (not a longer run that merely *starts* with `close_len` chars).
// Returns true iff such a run is found before a block boundary.
//
// Block boundary detection (two independent triggers):
//   1. Empty line: two consecutive line endings with no other character
//      between them (\n\n or \r\n\r\n etc.).
//   2. Single newline followed by a line that looks like a block-level
//      element start (ATX heading, fenced code, blockquote, thematic break /
//      list marker first character) — checked via looks_like_block_start().
//
// NOT covered by the boundary heuristic (see looks_like_block_start comment):
//   setext underlines, ordered list items, HTML block tags, indented code.
//
// Position contract: this function ONLY calls `lexer->advance`; it does NOT
// call `mark_end`.  The caller restores `s->indentation` / `s->column` before
// returning false, because tree-sitter's C runtime resets the raw byte-stream
// position on scanner backtrack but does NOT restore Scanner struct fields.
// ---------------------------------------------------------------------------
// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity)
static bool has_closing_delimiter(TSLexer *lexer,
                                  int32_t close_char,
                                  uint32_t close_len) {
    bool prev_was_newline = false;
    uint32_t run = 0;

    while (!lexer->eof(lexer)) {
        int32_t ch = lexer->lookahead;

        if (ch == '\n' || ch == '\r') {
            if (prev_was_newline) {
                // Empty line — block boundary, stop.
                return false;
            }
            prev_was_newline = true;
            run = 0;
            lexer->advance(lexer, false);
            continue;
        }

        // First non-newline character of a new line: check for block start.
        if (prev_was_newline && looks_like_block_start(lexer)) {
            return false;
        }
        prev_was_newline = false;

        if (ch == close_char) {
            run++;
            lexer->advance(lexer, false);
            // Check whether the run ends exactly here (next char ≠ close_char).
            // We must NOT accept a prefix of a longer run, e.g. when
            // close_len==2 and the text has `***`, only `**` would match, but
            // the third `*` makes this a run of 3, not 2.
            if (run == close_len && lexer->lookahead != close_char) {
                return true;
            }
        } else {
            run = 0;
            lexer->advance(lexer, false);
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity)

// Variant of has_closing_delimiter for STRONG delimiters only: a closing run
// of length >= `close_len` counts as a closer (CommonMark-correct run
// splitting, e.g. `**a***` closes the strong with the first two `*` of the
// trailing `***`).  This is deliberately NOT used for emphasis (close_len 1):
// there an exact run is required so a `**` strong run is never mistaken for a
// single `*` emphasis closer, preserving `*a **b** c*` nesting.  Block-boundary
// semantics are identical to has_closing_delimiter.
// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity)
static bool has_closing_delimiter_ge(TSLexer *lexer,
                                     int32_t close_char,
                                     uint32_t close_len) {
    bool prev_was_newline = false;
    uint32_t run = 0;

    while (!lexer->eof(lexer)) {
        int32_t ch = lexer->lookahead;

        if (ch == '\n' || ch == '\r') {
            if (prev_was_newline) {
                // Empty line — block boundary, stop.
                return false;
            }
            prev_was_newline = true;
            run = 0;
            lexer->advance(lexer, false);
            continue;
        }

        // First non-newline character of a new line: check for block start.
        if (prev_was_newline && looks_like_block_start(lexer)) {
            return false;
        }
        prev_was_newline = false;

        if (ch == close_char) {
            run++;
            lexer->advance(lexer, false);
            // A closing run of length >= close_len (not just ==) counts, so a
            // `***` run can close a `**` strong.
            if (run >= close_len && lexer->lookahead != close_char) {
                return true;
            }
        } else {
            run = 0;
            lexer->advance(lexer, false);
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity)


// NOLINTBEGIN(readability-identifier-length,readability-magic-numbers,readability-function-cognitive-complexity)
static bool parse_math_inline_delimiter(Scanner *s, TSLexer *lexer,
                                        const bool *valid_symbols) {
    if (lexer->lookahead != '$') {
        return false;
    }

    bool block_open = valid_symbols[MATH_BLOCK_OPEN_DELIMITER];
    bool block_close = valid_symbols[MATH_BLOCK_CLOSE_DELIMITER];
    bool inline_open = valid_symbols[MATH_INLINE_OPEN_DELIMITER];
    bool inline_close = valid_symbols[MATH_INLINE_CLOSE_DELIMITER];
    if (!block_open && !block_close && !inline_open && !inline_close) {
        return false;
    }

    uint16_t start_indentation = s->indentation;
    uint8_t start_column = s->column;
    advance(s, lexer);

    // Math-block delimiter: a run of EXACTLY two `$` (`$$`) with no third.
    // The open form additionally requires a matching `$$` closer before the
    // next block boundary (has_closing_delimiter's exact-run semantics).  A
    // single `$` is never a block delimiter and falls through to the inline
    // handling below.
    if ((block_open || block_close) && lexer->lookahead == '$') {
        advance(s, lexer);
        if (lexer->lookahead != '$') {
            // Exactly two dollars: mark_end commits the `$$` boundary so the
            // token is exactly two characters even though has_closing_delimiter
            // scans further ahead.
            mark_end(s, lexer);
            if (block_open && has_closing_delimiter(lexer, '$', 2)) {
                lexer->result_symbol = MATH_BLOCK_OPEN_DELIMITER;
                return true;
            }
            if (block_close) {
                lexer->result_symbol = MATH_BLOCK_CLOSE_DELIMITER;
                return true;
            }
        }
        // A longer run (`$$$`) is not a math-block delimiter, and an unclosed
        // `$$` has no matching closer: neither yields a valid block token here.
        // Inline math cannot consume a `$$` run either, so returning false lets
        // the run degrade to ordinary text instead of a runaway ERROR.
        s->indentation = start_indentation;
        s->column = start_column;
        return false;
    }

    if (inline_open &&
        lexer->lookahead != '$' && lexer->lookahead != ' ' &&
        lexer->lookahead != '\t' && lexer->lookahead != '\n' &&
        lexer->lookahead != '\r' && !lexer->eof(lexer)) {
        uint16_t delimiter_indentation = s->indentation;
        uint8_t delimiter_column = s->column;
        mark_end(s, lexer);

        int32_t previous = 0;
        while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               !lexer->eof(lexer)) {
            if (lexer->lookahead == '$') {
                advance(s, lexer);
                bool previous_is_space = false;
                if (previous == ' ' || previous == '\t') {
                    previous_is_space = true;
                }
                if (!previous_is_space && !is_ascii_digit(lexer->lookahead)) {
                    s->indentation = delimiter_indentation;
                    s->column = delimiter_column;
                    lexer->result_symbol = MATH_INLINE_OPEN_DELIMITER;
                    return true;
                }
                break;
            }
            previous = lexer->lookahead;
            advance(s, lexer);
        }
        s->indentation = start_indentation;
        s->column = start_column;
        return false;
    }

    if (inline_close && !is_ascii_digit(lexer->lookahead)) {
        lexer->result_symbol = MATH_INLINE_CLOSE_DELIMITER;
        return true;
    }

    s->indentation = start_indentation;
    s->column = start_column;
    return false;
}
// NOLINTEND(readability-identifier-length,readability-magic-numbers,readability-function-cognitive-complexity)


// NOLINTBEGIN(readability-identifier-length,readability-magic-numbers,readability-function-cognitive-complexity)
static bool parse_colon(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    bool block_open = valid_symbols[DIRECTIVE_BLOCK_OPEN_DELIMITER];
    bool block_close = valid_symbols[DIRECTIVE_BLOCK_CLOSE_DELIMITER];
    if (!block_open && !block_close) {
        return false;
    }

    uint16_t start_indentation = s->indentation;
    uint8_t start_column = s->column;
    advance(s, lexer);
    advance(s, lexer);
    if (lexer->lookahead != ':') {
        // A run of one or two colons (`:` / `::`) is not a directive
        // delimiter.
        s->indentation = start_indentation;
        s->column = start_column;
        return false;
    }
    advance(s, lexer);
    if (lexer->lookahead == ':') {
        // A run of 4+ colons (`::::`) is not a directive delimiter either; it
        // degrades to ordinary text.
        s->indentation = start_indentation;
        s->column = start_column;
        return false;
    }

    // Exactly three colons (`:::`): the directive delimiter.  mark_end commits
    // the three-char boundary so the token is exactly `:::` even though
    // has_closing_delimiter scans further ahead.
    mark_end(s, lexer);
    if (block_open && has_closing_delimiter(lexer, ':', DIRECTIVE_DELIMITER_LENGTH)) {
        lexer->result_symbol = DIRECTIVE_BLOCK_OPEN_DELIMITER;
        return true;
    }
    if (block_close) {
        lexer->result_symbol = DIRECTIVE_BLOCK_CLOSE_DELIMITER;
        return true;
    }
    // An unclosed `:::` has no matching closer before the next block boundary:
    // return false so it degrades to ordinary paragraph text instead of a
    // runaway ERROR.
    s->indentation = start_indentation;
    s->column = start_column;
    return false;
}
// NOLINTEND(readability-identifier-length,readability-magic-numbers,readability-function-cognitive-complexity)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
// A '[' at a token start could begin a footnote reference (`[^id]`), a task
// list marker (`[x]` / `[ ]`), or a plain bracket (link label / shortcut).
// All three are disambiguated by the character after the '['.  We consume the
// '[' exactly once and branch on the second character; if none of the
// scanner-emitted tokens apply we return false, letting tree-sitter rewind the
// byte position and treat '[' as an ordinary bracket.  (We deliberately do NOT
// chain a second parse_* after a failed one: the lexer byte position only
// rewinds when the whole scan() returns false, not between internal calls.)
static bool parse_bracket_open(Scanner *s, TSLexer *lexer,
                               const bool *valid_symbols) {
    if (lexer->lookahead != '[') {
        return false;
    }
    bool footnote = valid_symbols[FOOTNOTE_REF_OPEN];
    bool task_list = valid_symbols[TASK_LIST_MARKER_CHECKED] ||
                     valid_symbols[TASK_LIST_MARKER_UNCHECKED];
    if (!footnote && !task_list) {
        return false;
    }
    uint16_t start_indentation = s->indentation;
    uint8_t start_column = s->column;
    advance(s, lexer); // consume '['

    // Footnote reference: '[^'.  The open is only emitted when a closing ']'
    // exists before the next block boundary; an unclosed '[^' degrades to
    // ordinary text instead of a runaway ERROR.
    if (footnote && lexer->lookahead == '^') {
        advance(s, lexer); // consume '^'
        mark_end(s, lexer); // token = the two-char `[^`
        if (has_closing_delimiter(lexer, ']', 1)) {
            lexer->result_symbol = FOOTNOTE_REF_OPEN;
            return true;
        }
        s->indentation = start_indentation;
        s->column = start_column;
        return false;
    }

    // Task list marker: '[x]' or '[ ]', followed by whitespace.
    if (task_list) {
        bool checked = false;
        bool unchecked = false;
        if (lexer->lookahead == 'x' || lexer->lookahead == 'X') {
            checked = true;
        } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            unchecked = true;
        }
        if ((!checked || !valid_symbols[TASK_LIST_MARKER_CHECKED]) &&
            (!unchecked || !valid_symbols[TASK_LIST_MARKER_UNCHECKED])) {
            s->indentation = start_indentation;
            s->column = start_column;
            return false;
        }
        advance(s, lexer); // consume 'x' or ' '
        if (lexer->lookahead != ']') {
            s->indentation = start_indentation;
            s->column = start_column;
            return false;
        }
        advance(s, lexer); // consume ']'
        if (lexer->lookahead != ' ' && lexer->lookahead != '\t' &&
            lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
            !lexer->eof(lexer)) {
            s->indentation = start_indentation;
            s->column = start_column;
            return false;
        }
        lexer->result_symbol =
            checked ? TASK_LIST_MARKER_CHECKED : TASK_LIST_MARKER_UNCHECKED;
        return true;
    }

    // Neither a footnote reference nor a task list marker: degrade to a plain
    // '[' bracket (runtime rewinds to the token start).
    s->indentation = start_indentation;
    s->column = start_column;
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_backtick(Scanner *s, TSLexer *lexer,
                           const bool *valid_symbols) {
    bool ic_1_open = valid_symbols[INLINE_CODE_BACKTICK_1_OPEN];
    bool ic_1_close = valid_symbols[INLINE_CODE_BACKTICK_1_CLOSE];
    bool ic_2_open = valid_symbols[INLINE_CODE_BACKTICK_2_OPEN];
    bool ic_2_close = valid_symbols[INLINE_CODE_BACKTICK_2_CLOSE];
    bool inline_valid = ic_1_open || ic_1_close || ic_2_open || ic_2_close;
    bool fence_start = valid_symbols[FENCED_CODE_BLOCK_START_BACKTICK];
    bool fence_end = valid_symbols[FENCED_CODE_BLOCK_END_BACKTICK];
    if (!inline_valid && !fence_start && !fence_end) {
        return false;
    }

    // Count the run of backticks, remembering the single- and double-backtick
    // boundaries so an inline-code delimiter token covers exactly 1 or 2 chars
    // (mark_end commits the boundary even though has_closing_delimiter scans
    // further ahead).
    uint32_t level = 0;
    while (lexer->lookahead == '`') {
        advance(s, lexer);
        level++;
        if (level <= 2) {
            mark_end(s, lexer);
        }
    }

    // Inline code: a run of exactly one or two backticks.  A close emits
    // unconditionally; an open requires a matching closer run of the same
    // length before the next block boundary.
    if (level == 1 && inline_valid) {
        if (ic_1_close) {
            lexer->result_symbol = INLINE_CODE_BACKTICK_1_CLOSE;
            return true;
        }
        if (ic_1_open && !lexer->eof(lexer) &&
            has_closing_delimiter(lexer, '`', 1)) {
            lexer->result_symbol = INLINE_CODE_BACKTICK_1_OPEN;
            return true;
        }
        return false;
    }
    if (level == 2 && inline_valid) {
        if (ic_2_close) {
            lexer->result_symbol = INLINE_CODE_BACKTICK_2_CLOSE;
            return true;
        }
        if (ic_2_open && !lexer->eof(lexer) &&
            has_closing_delimiter(lexer, '`', 2)) {
            lexer->result_symbol = INLINE_CODE_BACKTICK_2_OPEN;
            return true;
        }
        return false;
    }

    // A run of 3+ backticks is a fenced code block, never inline code.
    if (level < FENCED_CODE_MIN_FENCE || s->indentation > MAX_NON_CODE_INDENT) {
        return false;
    }
    mark_end(s, lexer);
    if (fence_end && level >= s->fenced_code_block_delimiter_length) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            s->fenced_code_block_delimiter_length = 0;
            lexer->result_symbol = FENCED_CODE_BLOCK_END_BACKTICK;
            return true;
        }
    }
    if (fence_start && level >= FENCED_CODE_MIN_FENCE) {
        bool info_string_has_backtick = false;
        while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               !lexer->eof(lexer)) {
            if (lexer->lookahead == '`') {
                info_string_has_backtick = true;
                break;
            }
            advance(s, lexer);
        }
        if (!info_string_has_backtick) {
            lexer->result_symbol = FENCED_CODE_BLOCK_START_BACKTICK;
            if (!s->simulate && !push_block(s, FENCED_CODE_BLOCK)) {
                return false;
            }
            s->fenced_code_block_delimiter_length = level;
            s->indentation = 0;
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_star(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    bool block_valid = valid_symbols[LIST_MARKER_STAR] ||
                       valid_symbols[LIST_MARKER_STAR_DONT_INTERRUPT] ||
                       valid_symbols[THEMATIC_BREAK];
    bool strong_open = valid_symbols[STRONG_STAR_OPEN];
    bool strong_close = valid_symbols[STRONG_STAR_CLOSE];
    bool emph_open = valid_symbols[EMPHASIS_STAR_OPEN];
    bool emph_close = valid_symbols[EMPHASIS_STAR_CLOSE];
    bool inline_valid = strong_open || strong_close || emph_open || emph_close;
    if (!block_valid && !inline_valid) {
        return false;
    }
    // Block tokens require the indentation to be within limits; inline
    // emphasis tokens have no such restriction.
    if (block_valid && s->indentation > MAX_NON_CODE_INDENT) {
        if (!inline_valid) {
            return false;
        }
    }
    advance(s, lexer);
    mark_end(s, lexer);

    // Inline emphasis close: a lone single `*` (next char is not `*`).  A
    // multi-star run is handled as strong below; nested `***` runs are limited
    // by has_closing_delimiter's exact-run semantics (see note in parse_star).
    // Checked before the counting loop because the loop advances past
    // additional `*` and spaces, which would corrupt the delimiter length for
    // the inline case.
    if (emph_close && lexer->lookahead != '*') {
        lexer->result_symbol = EMPHASIS_STAR_CLOSE;
        return true;
    }

    // Otherwise count the number of stars permitting whitespaces between them.
    // `consecutive` is the number of stars before any whitespace — the inline
    // delimiter length (`**` strong, `*` emphasis).  `star_count` includes
    // stars after whitespace and is used only for block detection (thematic
    // break `* * *`); mixing the two would let a following `*` (e.g. in
    // `**bold** *em*`) corrupt the inline delimiter length.
    size_t star_count = 1;
    size_t consecutive = 1;
    // Also remember how many stars there are before the first whitespace...
    // ...and how many spaces follow the first star.
    uint16_t extra_indentation = 0;
    bool seen_space = false;
    for (;;) {
        if (lexer->lookahead == '*') {
            if (star_count == 1 && extra_indentation >= 1 &&
                valid_symbols[LIST_MARKER_STAR]) {
                // If we get to this point then the token has to be at least
                // this long. We need to call `mark_end` here in case we decide
                // later that this is a list item.
                mark_end(s, lexer);
            }
            star_count++;
            if (!seen_space) {
                consecutive++;
            }
            advance(s, lexer);
            if (star_count == 2) {
                // Remember the two-star boundary so a strong open/close token
                // covers exactly `**`.
                mark_end(s, lexer);
            }
        } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            if (star_count == 1) {
                add_indentation(&extra_indentation, advance(s, lexer));
            } else {
                advance(s, lexer);
            }
            seen_space = true;
        } else {
            break;
        }
    }
    bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';

    bool dont_interrupt = false;
    if (star_count == 1 && line_end) {
        extra_indentation = 1;
        // line is empty so don't interrupt paragraphs if this is a list marker
        dont_interrupt = s->matched == s->open_blocks.size;
    }
    // If there were at least 3 stars then this could be a thematic break
    bool thematic_break = star_count >= 3 && line_end;
    // If there was a star and at least one space after that star then this
    // could be a list marker.
    bool list_marker_star = star_count >= 1 && extra_indentation >= 1;
    if (valid_symbols[THEMATIC_BREAK] && thematic_break &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        // If a thematic break is valid then it takes precedence
        lexer->result_symbol = THEMATIC_BREAK;
        mark_end(s, lexer);
        s->indentation = 0;
        return true;
    }
    if ((dont_interrupt ? valid_symbols[LIST_MARKER_STAR_DONT_INTERRUPT]
                        : valid_symbols[LIST_MARKER_STAR]) &&
        list_marker_star) {
        // List markers take precedence over emphasis markers
        // If star_count > 1 then we already called mark_end at the right point.
        // Otherwise the token should go until this point.
        if (star_count == 1) {
            mark_end(s, lexer);
        }
        return finish_list_marker(
            s, lexer,
            (ListMarker){
                .result_symbol = dont_interrupt
                                     ? LIST_MARKER_STAR_DONT_INTERRUPT
                                     : LIST_MARKER_STAR,
                .extra_indentation = extra_indentation,
                .marker_width_adjust = 0,
            });
    }

    // Strong close: at least two consecutive stars (no trailing space).  A run
    // of 3 (`***`) closes the strong with its first two `*` (run-splitting);
    // the leftover `*` is re-scanned separately and degrades to text.
    if (strong_close && consecutive >= 2) {
        // mark_end already committed at the two-star boundary.
        lexer->result_symbol = STRONG_STAR_CLOSE;
        return true;
    }
    // Strong open: at least two consecutive stars, no trailing space, not at
    // end of line (left-flanking simplified rule), and a matching `**` closer
    // (run of >= 2) exists before the next block boundary.
    if (strong_open && consecutive >= 2 && !line_end && extra_indentation == 0 &&
        !lexer->eof(lexer)) {
        // mark_end already committed at the two-star boundary, so the token
        // length is correct even though has_closing_delimiter advances further.
        if (has_closing_delimiter_ge(lexer, '*', 2)) {
            lexer->result_symbol = STRONG_STAR_OPEN;
            return true;
        }
    }

    // Inline emphasis open: exactly one `*`, no trailing space, not at end
    // of line (left-flanking simplified rule), and a matching closer exists
    // before the next block boundary.
    if (emph_open && consecutive == 1 && !line_end && extra_indentation == 0 &&
        !lexer->eof(lexer)) {
        // mark_end is already committed after the first `*`, so the token
        // length is correct even though has_closing_delimiter advances further.
        if (has_closing_delimiter(lexer, '*', 1)) {
            lexer->result_symbol = EMPHASIS_STAR_OPEN;
            return true;
        }
    }

    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_underscore(Scanner *s, TSLexer *lexer,
                             const bool *valid_symbols) {
    bool thematic_valid = valid_symbols[THEMATIC_BREAK];
    bool strong_open = valid_symbols[STRONG_UNDERSCORE_OPEN];
    bool strong_close = valid_symbols[STRONG_UNDERSCORE_CLOSE];
    bool emph_open = valid_symbols[EMPHASIS_UNDERSCORE_OPEN];
    bool emph_close = valid_symbols[EMPHASIS_UNDERSCORE_CLOSE];
    bool inline_valid = strong_open || strong_close || emph_open || emph_close;
    if (!thematic_valid && !inline_valid) {
        return false;
    }
    // Block tokens (thematic break) require the indentation to be within
    // limits; inline emphasis tokens have no such restriction.
    if (thematic_valid && s->indentation > MAX_NON_CODE_INDENT) {
        thematic_valid = false;
        if (!inline_valid) {
            return false;
        }
    }
    advance(s, lexer);
    mark_end(s, lexer);

    // Inline emphasis close: a lone single `_` (next char is not `_`).
    // Checked before the scanning loop because the loop advances past
    // additional `_` and spaces, which would corrupt the delimiter length for
    // the inline case.
    if (emph_close && lexer->lookahead != '_') {
        // mark_end is already committed after the single `_`.
        lexer->result_symbol = EMPHASIS_UNDERSCORE_CLOSE;
        return true;
    }

    // Otherwise count the underscores permitting whitespaces between them.
    // `consecutive` is the number of underscores before any whitespace — the
    // inline delimiter length (`__` strong, `_` emphasis).  `underscore_count`
    // includes underscores after whitespace and is used only for block
    // detection (thematic break `_ _ _`).
    size_t underscore_count = 1;
    size_t consecutive = 1;
    // Remember how many spaces follow the first underscore.
    uint16_t extra_indentation = 0;
    bool seen_space = false;
    for (;;) {
        if (lexer->lookahead == '_') {
            underscore_count++;
            if (!seen_space) {
                consecutive++;
            }
            advance(s, lexer);
            if (underscore_count == 2) {
                // Remember the two-underscore boundary so a strong open/close
                // token covers exactly `__`.
                mark_end(s, lexer);
            }
        } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            if (underscore_count == 1) {
                add_indentation(&extra_indentation, advance(s, lexer));
            } else {
                advance(s, lexer);
            }
            seen_space = true;
        } else {
            break;
        }
    }
    bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';

    // If there were at least 3 underscores at end of line this is a thematic
    // break.
    if (thematic_valid && underscore_count >= 3 && line_end &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        lexer->result_symbol = THEMATIC_BREAK;
        mark_end(s, lexer);
        s->indentation = 0;
        return true;
    }

    // Strong close: at least two consecutive underscores (no trailing space).
    // A run of 3 (`___`) closes the strong with its first two `_`
    // (run-splitting); the leftover `_` is re-scanned separately and degrades
    // to text.
    if (strong_close && consecutive >= 2) {
        // mark_end already committed at the two-underscore boundary.
        lexer->result_symbol = STRONG_UNDERSCORE_CLOSE;
        return true;
    }
    // Strong open: at least two consecutive underscores, no trailing space,
    // not at end of line (left-flanking simplified rule), and a matching `__`
    // closer (run of >= 2) exists before the next block boundary.
    if (strong_open && consecutive >= 2 && !line_end && extra_indentation == 0 &&
        !lexer->eof(lexer)) {
        // mark_end already committed at the two-underscore boundary, so the
        // token length is correct even though has_closing_delimiter advances
        // further.
        if (has_closing_delimiter_ge(lexer, '_', 2)) {
            lexer->result_symbol = STRONG_UNDERSCORE_OPEN;
            return true;
        }
    }

    // Inline emphasis open: exactly one `_`, no trailing space, not at end
    // of line (left-flanking simplified rule), and a matching closer exists
    // before the next block boundary.
    if (emph_open && consecutive == 1 && !line_end && extra_indentation == 0 &&
        !lexer->eof(lexer)) {
        // mark_end is already committed after the first `_`, so the token
        // length is correct even though has_closing_delimiter advances further.
        if (has_closing_delimiter(lexer, '_', 1)) {
            lexer->result_symbol = EMPHASIS_UNDERSCORE_OPEN;
            return true;
        }
    }

    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_tilde(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    bool fence_start = valid_symbols[FENCED_CODE_BLOCK_START_TILDE];
    bool fence_end = valid_symbols[FENCED_CODE_BLOCK_END_TILDE];
    bool strike_open = valid_symbols[STRIKETHROUGH_OPEN];
    bool strike_close = valid_symbols[STRIKETHROUGH_CLOSE];
    if (!fence_start && !fence_end && !strike_open && !strike_close) {
        return false;
    }

    // Count the run of tildes, remembering the two-tilde boundary for
    // strikethrough and the full-run boundary for the fenced code block.
    uint32_t level = 0;
    while (lexer->lookahead == '~') {
        advance(s, lexer);
        if (level < UINT32_MAX) {
            level++;
        }
        if (level == 2) {
            // Two-tilde boundary: the strikethrough delimiter `~~`.
            mark_end(s, lexer);
        }
    }

    // Strikethrough: exactly two tildes.
    if (level == 2 && (strike_open || strike_close)) {
        if (strike_close) {
            lexer->result_symbol = STRIKETHROUGH_CLOSE;
            return true;
        }
        // Inline strikethrough open: a matching `~~` closer exists before the
        // next block boundary.  (No left-flanking space rule needed here: `~`
        // is not a word character, so there is no intraword ambiguity.)
        if (strike_open && !lexer->eof(lexer) &&
            has_closing_delimiter(lexer, '~', 2)) {
            lexer->result_symbol = STRIKETHROUGH_OPEN;
            return true;
        }
        return false;
    }

    // Fenced code block (run >= 3).  Inlined from parse_fenced_code_block for
    // the `~` delimiter (the tilde fence has no info-string backtick check).
    if (level < FENCED_CODE_MIN_FENCE ||
        s->indentation > MAX_NON_CODE_INDENT) {
        return false;
    }
    mark_end(s, lexer);
    if (fence_end && level >= s->fenced_code_block_delimiter_length) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            s->fenced_code_block_delimiter_length = 0;
            lexer->result_symbol = FENCED_CODE_BLOCK_END_TILDE;
            return true;
        }
    }
    if (fence_start && level >= FENCED_CODE_MIN_FENCE) {
        lexer->result_symbol = FENCED_CODE_BLOCK_START_TILDE;
        if (!s->simulate && !push_block(s, FENCED_CODE_BLOCK)) {
            return false;
        }
        s->fenced_code_block_delimiter_length = level;
        s->indentation = 0;
        return true;
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_block_quote(Scanner *s, TSLexer *lexer,
                              const bool *valid_symbols) {
    if (valid_symbols[BLOCK_QUOTE_START] &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        advance(s, lexer);
        s->indentation = 0;
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            add_indentation(&s->indentation, advance(s, lexer) - 1U);
        }
        lexer->result_symbol = BLOCK_QUOTE_START;
        if (!s->simulate && !push_block(s, BLOCK_QUOTE)) {
            return false;
        }
        return true;
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_atx_heading(Scanner *s, TSLexer *lexer,
                              const bool *valid_symbols) {
    if (valid_symbols[ATX_H1_MARKER] &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        mark_end(s, lexer);
        uint16_t level = 0;
        while (lexer->lookahead == '#' && level <= ATX_HEADING_LEVELS) {
            advance(s, lexer);
            level++;
        }
        if (level <= ATX_HEADING_LEVELS &&
            (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
             lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
            lexer->result_symbol = ATX_H1_MARKER + (level - 1);
            s->indentation = 0;
            mark_end(s, lexer);
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_setext_underline(Scanner *s, TSLexer *lexer,
                                   const bool *valid_symbols) {
    if (valid_symbols[SETEXT_H1_UNDERLINE] &&
        s->indentation <= MAX_NON_CODE_INDENT &&
        s->matched == s->open_blocks.size) {
        mark_end(s, lexer);
        while (lexer->lookahead == '=') {
            advance(s, lexer);
        }
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            lexer->result_symbol = SETEXT_H1_UNDERLINE;
            mark_end(s, lexer);
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_plus(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    if (s->indentation <= MAX_NON_CODE_INDENT &&
        (valid_symbols[LIST_MARKER_PLUS] ||
         valid_symbols[LIST_MARKER_PLUS_DONT_INTERRUPT] ||
         valid_symbols[PLUS_METADATA])) {
        advance(s, lexer);
        if (valid_symbols[PLUS_METADATA] && lexer->lookahead == '+') {
            advance(s, lexer);
            if (lexer->lookahead != '+') {
                return false;
            }
            advance(s, lexer);
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                advance(s, lexer);
            }
            if (lexer->lookahead != '\n' && lexer->lookahead != '\r') {
                return false;
            }
            ScannerSnapshot metadata_snapshot = snapshot_scanner(s);
            if (scan_metadata_block(
                    s, lexer,
                    (MetadataFence){.delimiter = '+',
                                    .result_symbol = PLUS_METADATA})) {
                return true;
            }
            restore_scanner(s, metadata_snapshot);
            return false;
        } else {
            uint16_t extra_indentation = 0;
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                add_indentation(&extra_indentation, advance(s, lexer));
            }
            bool dont_interrupt = false;
            if (lexer->lookahead == '\r' || lexer->lookahead == '\n') {
                extra_indentation = 1;
                dont_interrupt = true;
            }
            dont_interrupt =
                dont_interrupt && s->matched == s->open_blocks.size;
            if (extra_indentation >= 1 &&
                (dont_interrupt ? valid_symbols[LIST_MARKER_PLUS_DONT_INTERRUPT]
                                : valid_symbols[LIST_MARKER_PLUS])) {
                return finish_list_marker(
                    s, lexer,
                    (ListMarker){
                        .result_symbol = dont_interrupt
                                             ? LIST_MARKER_PLUS_DONT_INTERRUPT
                                             : LIST_MARKER_PLUS,
                        .extra_indentation = extra_indentation,
                        .marker_width_adjust = 0,
                    });
            }
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_ordered_list_marker(Scanner *s, TSLexer *lexer,
                                      const bool *valid_symbols) {
    if (s->indentation <= MAX_NON_CODE_INDENT &&
        (valid_symbols[LIST_MARKER_PARENTHESIS] ||
         valid_symbols[LIST_MARKER_DOT] ||
         valid_symbols[LIST_MARKER_PARENTHESIS_DONT_INTERRUPT] ||
         valid_symbols[LIST_MARKER_DOT_DONT_INTERRUPT])) {
        size_t digits = 0;
        size_t marker_value = 0;
        while (is_ascii_digit(lexer->lookahead)) {
            if (digits < ORDERED_LIST_MAX_DIGITS) {
                marker_value =
                    (marker_value * 10U) + (size_t)(lexer->lookahead - '0');
            }
            digits++;
            advance(s, lexer);
        }
        if (digits >= 1 && digits <= ORDERED_LIST_MAX_DIGITS) {
            bool dont_interrupt = marker_value != 1U;
            bool dot = false;
            bool parenthesis = false;
            if (lexer->lookahead == '.') {
                advance(s, lexer);
                dot = true;
            } else if (lexer->lookahead == ')') {
                advance(s, lexer);
                parenthesis = true;
            }
            if (dot || parenthesis) {
                uint16_t extra_indentation = 0;
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&extra_indentation, advance(s, lexer));
                }
                bool line_end =
                    lexer->lookahead == '\n' || lexer->lookahead == '\r';
                if (line_end) {
                    extra_indentation = 1;
                    dont_interrupt = true;
                }
                dont_interrupt =
                    dont_interrupt && s->matched == s->open_blocks.size;
                TokenType result_symbol =
                    dot ? (dont_interrupt ? LIST_MARKER_DOT_DONT_INTERRUPT
                                          : LIST_MARKER_DOT)
                        : (dont_interrupt
                               ? LIST_MARKER_PARENTHESIS_DONT_INTERRUPT
                               : LIST_MARKER_PARENTHESIS);
                if (extra_indentation >= 1 && valid_symbols[result_symbol]) {
                    return finish_list_marker(
                        s, lexer,
                        (ListMarker){
                            .result_symbol = result_symbol,
                            .extra_indentation = extra_indentation,
                            .marker_width_adjust = (uint16_t)digits,
                        });
                }
            }
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_minus(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    if (s->indentation <= MAX_NON_CODE_INDENT &&
        (valid_symbols[LIST_MARKER_MINUS] ||
         valid_symbols[LIST_MARKER_MINUS_DONT_INTERRUPT] ||
         valid_symbols[SETEXT_H2_UNDERLINE] || valid_symbols[THEMATIC_BREAK] ||
         valid_symbols[MINUS_METADATA])) {
        mark_end(s, lexer);
        bool whitespace_after_minus = false;
        bool minus_after_whitespace = false;
        size_t minus_count = 0;
        uint16_t extra_indentation = 0;

        for (;;) {
            if (lexer->lookahead == '-') {
                if (minus_count == 1 && extra_indentation >= 1) {
                    mark_end(s, lexer);
                }
                minus_count++;
                advance(s, lexer);
                minus_after_whitespace = whitespace_after_minus;
            } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                if (minus_count == 1) {
                    add_indentation(&extra_indentation, advance(s, lexer));
                } else {
                    advance(s, lexer);
                }
                whitespace_after_minus = true;
            } else {
                break;
            }
        }
        bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';
        bool dont_interrupt = false;
        if (minus_count == 1 && line_end) {
            extra_indentation = 1;
            dont_interrupt = true;
        }
        dont_interrupt = dont_interrupt && s->matched == s->open_blocks.size;
        bool thematic_break = minus_count >= 3 && line_end;
        bool underline =
            minus_count >= 1 && !minus_after_whitespace && line_end &&
            s->matched ==
                s->open_blocks
                    .size; // setext heading can not break lazy continuation
        bool list_marker_minus = minus_count >= 1 && extra_indentation >= 1;
        bool success = false;
        if (valid_symbols[SETEXT_H2_UNDERLINE] && underline) {
            lexer->result_symbol = SETEXT_H2_UNDERLINE;
            mark_end(s, lexer);
            s->indentation = 0;
            success = true;
        } else if (valid_symbols[THEMATIC_BREAK] &&
                   thematic_break) { // underline is false if list_marker_minus
                                     // is true
            lexer->result_symbol = THEMATIC_BREAK;
            mark_end(s, lexer);
            s->indentation = 0;
            success = true;
        } else if ((dont_interrupt
                        ? valid_symbols[LIST_MARKER_MINUS_DONT_INTERRUPT]
                        : valid_symbols[LIST_MARKER_MINUS]) &&
                   list_marker_minus) {
            if (minus_count == 1) {
                mark_end(s, lexer);
            }
            return finish_list_marker(
                s, lexer,
                (ListMarker){
                    .result_symbol = dont_interrupt
                                         ? LIST_MARKER_MINUS_DONT_INTERRUPT
                                         : LIST_MARKER_MINUS,
                    .extra_indentation = extra_indentation,
                    .marker_width_adjust = 0,
                });
        }
        if (minus_count == 3 && (!minus_after_whitespace) && line_end &&
            valid_symbols[MINUS_METADATA]) {
            ScannerSnapshot metadata_snapshot = snapshot_scanner(s);
            if (scan_metadata_block(
                    s, lexer,
                    (MetadataFence){.delimiter = '-',
                                    .result_symbol = MINUS_METADATA})) {
                return true;
            }
            restore_scanner(s, metadata_snapshot);
        }
        if (success) {
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_autolink(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    if (!valid_symbols[AUTOLINK_OPEN] || lexer->lookahead != '<') {
        return false;
    }
    uint16_t start_indentation = s->indentation;
    uint8_t start_column = s->column;
    advance(s, lexer); // consume '<'
    // GATE token: the emitted token is ONLY the opening `<`.  mark_end right
    // after the `<` so the parser re-parses the content with ordinary grammar
    // rules (named `uri` / `email` children).  The scan below validates ahead
    // that a well-formed URI/email is closed by `>` on the same line; if so we
    // return with the token already bounded to just the `<`.
    mark_end(s, lexer); // token = the single '<' gate

    // URI candidate: [A-Za-z][A-Za-z0-9+.\-]{1,31} ':' [^<> \t\n\r]+
    bool uri_ok = true;
    uint32_t uri_scheme_len = 0;
    bool uri_seen_colon = false;
    bool uri_has_rest = false;
    // Email candidate: [A-Za-z0-9._%+\-]+ '@' [A-Za-z0-9.\-]+ (\.[A-Za-z0-9\-]+)+
    bool email_ok = true;
    uint32_t email_local_len = 0;
    bool email_seen_at = false;
    uint32_t email_labels = 0;
    bool email_domain_has_char = false;

    // Scan the content run [^<> \t\n\r]+ up to the closing `>`.
    while (lexer->lookahead != '<' && lexer->lookahead != '>' &&
           lexer->lookahead != ' ' && lexer->lookahead != '\t' &&
           lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
           !lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;

        // URI scheme tracking.
        if (!uri_seen_colon && uri_ok) {
            if (uri_scheme_len == 0) {
                if (is_ascii_alpha(c)) {
                    uri_scheme_len = 1;
                } else {
                    uri_ok = false;
                }
            } else if (c == ':') {
                if (uri_scheme_len >= 1 && uri_scheme_len <= 32) {
                    uri_seen_colon = true;
                } else {
                    uri_ok = false;
                }
            } else if (is_ascii_alnum(c) || c == '+' || c == '.' || c == '-') {
                uri_scheme_len++;
            } else {
                uri_ok = false;
            }
        } else if (uri_seen_colon) {
            uri_has_rest = true;
        }

        // Email tracking.
        if (email_ok) {
            if (!email_seen_at) {
                if (c == '@') {
                    if (email_local_len >= 1) {
                        email_seen_at = true;
                        email_labels = 0;
                        email_domain_has_char = false;
                    } else {
                        email_ok = false;
                    }
                } else if (is_ascii_alnum(c) || c == '.' || c == '_' ||
                           c == '%' || c == '+' || c == '-') {
                    email_local_len++;
                } else {
                    email_ok = false;
                }
            } else {
                // domain
                if (c == '.') {
                    if (!email_domain_has_char) {
                        email_ok = false;
                    } else {
                        email_labels++;
                    }
                    email_domain_has_char = false;
                } else if (is_ascii_alnum(c) || c == '-') {
                    email_domain_has_char = true;
                } else {
                    email_ok = false;
                }
            }
        }

        advance(s, lexer);
    }

    // The run must be terminated by `>`.
    if (lexer->lookahead != '>') {
        s->indentation = start_indentation;
        s->column = start_column;
        return false;
    }
    advance(s, lexer); // consume '>' (scan-ahead only; token end stays at '<')

    bool matched = false;
    if (uri_ok && uri_seen_colon && uri_has_rest) {
        matched = true;
    }
    if (!matched && email_ok && email_seen_at) {
        // Count the final domain label and require at least two labels.
        if (email_domain_has_char) {
            email_labels++;
        }
        if (email_labels >= 2) {
            matched = true;
        }
    }

    if (matched) {
        lexer->result_symbol = AUTOLINK_OPEN;
        return true;
    }
    s->indentation = start_indentation;
    s->column = start_column;
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_html_block(Scanner *s, TSLexer *lexer,
                              const bool *valid_symbols) {
    if (!(valid_symbols[HTML_BLOCK_1_START] ||
          valid_symbols[HTML_BLOCK_1_END] ||
          valid_symbols[HTML_BLOCK_2_START] ||
          valid_symbols[HTML_BLOCK_3_START] ||
          valid_symbols[HTML_BLOCK_4_START] ||
          valid_symbols[HTML_BLOCK_5_START] ||
          valid_symbols[HTML_BLOCK_6_START] ||
          valid_symbols[HTML_BLOCK_7_START])) {
        return false;
    }
    advance(s, lexer);
    if (lexer->lookahead == '?' && valid_symbols[HTML_BLOCK_3_START] &&
        s->indentation <= MAX_NON_CODE_INDENT) {
        advance(s, lexer);
        lexer->result_symbol = HTML_BLOCK_3_START;
        if (!s->simulate && !push_block(s, ANONYMOUS)) {
            return false;
        }
        return true;
    }
    if (lexer->lookahead == '!') {
        // could be block 2
        advance(s, lexer);
        if (lexer->lookahead == '-') {
            advance(s, lexer);
            if (lexer->lookahead == '-' && valid_symbols[HTML_BLOCK_2_START] &&
                s->indentation <= MAX_NON_CODE_INDENT) {
                advance(s, lexer);
                lexer->result_symbol = HTML_BLOCK_2_START;
                if (!s->simulate && !push_block(s, ANONYMOUS)) {
                    return false;
                }
                return true;
            }
        } else if ('A' <= lexer->lookahead && lexer->lookahead <= 'Z' &&
                   valid_symbols[HTML_BLOCK_4_START] &&
                   s->indentation <= MAX_NON_CODE_INDENT) {
            advance(s, lexer);
            lexer->result_symbol = HTML_BLOCK_4_START;
            if (!s->simulate && !push_block(s, ANONYMOUS)) {
                return false;
            }
            return true;
        } else if (lexer->lookahead == '[') {
            advance(s, lexer);
            if (lexer->lookahead == 'C') {
                advance(s, lexer);
                if (lexer->lookahead == 'D') {
                    advance(s, lexer);
                    if (lexer->lookahead == 'A') {
                        advance(s, lexer);
                        if (lexer->lookahead == 'T') {
                            advance(s, lexer);
                            if (lexer->lookahead == 'A') {
                                advance(s, lexer);
                                if (lexer->lookahead == '[' &&
                                    valid_symbols[HTML_BLOCK_5_START] &&
                                    s->indentation <= MAX_NON_CODE_INDENT) {
                                    advance(s, lexer);
                                    lexer->result_symbol = HTML_BLOCK_5_START;
                                    if (!s->simulate &&
                                        !push_block(s, ANONYMOUS)) {
                                        return false;
                                    }
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    bool starting_slash = lexer->lookahead == '/';
    if (starting_slash) {
        advance(s, lexer);
    }
    bool starts_with_ascii_uppercase =
        lexer->lookahead >= 'A' && lexer->lookahead <= 'Z';
    char name[HTML_TAG_NAME_BUFFER];
    size_t name_length = 0;
    while (is_ascii_alpha(lexer->lookahead)) {
        if (name_length < HTML_TAG_NAME_MAX) {
            name[name_length++] = ascii_tolower(lexer->lookahead);
        } else {
            name_length = HTML_TAG_NAME_TOO_LONG;
        }
        advance(s, lexer);
    }
    if (name_length == 0) {
        return false;
    }
    bool tag_closed = false;
    if (name_length < HTML_TAG_NAME_BUFFER) {
        name[name_length] = 0;
        bool next_symbol_valid =
            lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
            lexer->lookahead == '\n' || lexer->lookahead == '\r' ||
            lexer->lookahead == '>';
        if (next_symbol_valid) {
            // try block 1 names
            for (size_t i = 0; i < NUM_HTML_TAG_NAMES_RULE_1; i++) {
                if (strcmp(name, HTML_TAG_NAMES_RULE_1[i]) == 0) {
                    if (starting_slash) {
                        if (valid_symbols[HTML_BLOCK_1_END]) {
                            lexer->result_symbol = HTML_BLOCK_1_END;
                            return true;
                        }
                    } else if (valid_symbols[HTML_BLOCK_1_START] &&
                               s->indentation <= MAX_NON_CODE_INDENT) {
                        lexer->result_symbol = HTML_BLOCK_1_START;
                        if (!s->simulate && !push_block(s, ANONYMOUS)) {
                            return false;
                        }
                        return true;
                    }
                }
            }
        }
        if (!next_symbol_valid && lexer->lookahead == '/') {
            advance(s, lexer);
            if (lexer->lookahead == '>') {
                advance(s, lexer);
                tag_closed = true;
            }
        }
        if (next_symbol_valid || tag_closed) {
            // try block 2 names
            for (size_t i = 0; i < NUM_HTML_TAG_NAMES_RULE_7; i++) {
                if (strcmp(name, HTML_TAG_NAMES_RULE_7[i]) == 0 &&
                    valid_symbols[HTML_BLOCK_6_START] &&
                    s->indentation <= MAX_NON_CODE_INDENT) {
                    lexer->result_symbol = HTML_BLOCK_6_START;
                    if (!s->simulate && !push_block(s, ANONYMOUS)) {
                        return false;
                    }
                    return true;
                }
            }
        }
    }

    // Known HTML tags already matched case-insensitively above. Any remaining
    // ASCII-uppercase start tag falls through to MDX JSX instead of becoming a
    // generic CommonMark HTML block type 7.
    if (starts_with_ascii_uppercase) {
        return false;
    }

    if (!valid_symbols[HTML_BLOCK_7_START] ||
        s->indentation > MAX_NON_CODE_INDENT) {
        return false;
    }

    if (!tag_closed) {
        // tag name (continued)
        while (is_ascii_alnum(lexer->lookahead) ||
               lexer->lookahead == '-') {
            advance(s, lexer);
        }
        if (!starting_slash) {
            // attributes
            bool had_whitespace = false;
            for (;;) {
                // whitespace
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    had_whitespace = true;
                    advance(s, lexer);
                }
                if (lexer->lookahead == '/') {
                    advance(s, lexer);
                    break;
                }
                if (lexer->lookahead == '>') {
                    break;
                }
                // attribute name
                if (!had_whitespace) {
                    return false;
                }
                if (!is_ascii_alpha(lexer->lookahead) &&
                    lexer->lookahead != '_' && lexer->lookahead != ':') {
                    return false;
                }
                had_whitespace = false;
                advance(s, lexer);
                while (is_ascii_alnum(lexer->lookahead) ||
                       lexer->lookahead == '_' || lexer->lookahead == '.' ||
                       lexer->lookahead == ':' || lexer->lookahead == '-') {
                    advance(s, lexer);
                }
                // attribute value specification
                // optional whitespace
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    had_whitespace = true;
                    advance(s, lexer);
                }
                // =
                if (lexer->lookahead == '=') {
                    advance(s, lexer);
                    had_whitespace = false;
                    // optional whitespace
                    while (lexer->lookahead == ' ' ||
                           lexer->lookahead == '\t') {
                        advance(s, lexer);
                    }
                    // attribute value
                    if (lexer->lookahead == '\'' || lexer->lookahead == '"') {
                        char delimiter = (char)lexer->lookahead;
                        advance(s, lexer);
                        while (lexer->lookahead != delimiter &&
                               lexer->lookahead != '\n' &&
                               lexer->lookahead != '\r' && !lexer->eof(lexer)) {
                            advance(s, lexer);
                        }
                        if (lexer->lookahead != delimiter) {
                            return false;
                        }
                        advance(s, lexer);
                    } else {
                        // unquoted attribute value
                        bool had_one = false;
                        while (lexer->lookahead != ' ' &&
                               lexer->lookahead != '\t' &&
                               lexer->lookahead != '"' &&
                               lexer->lookahead != '\'' &&
                               lexer->lookahead != '=' &&
                               lexer->lookahead != '<' &&
                               lexer->lookahead != '>' &&
                               lexer->lookahead != '`' &&
                               lexer->lookahead != '\n' &&
                               lexer->lookahead != '\r' && !lexer->eof(lexer)) {
                            advance(s, lexer);
                            had_one = true;
                        }
                        if (!had_one) {
                            return false;
                        }
                    }
                }
            }
        } else {
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                advance(s, lexer);
            }
        }
        if (lexer->lookahead != '>') {
            return false;
        }
        advance(s, lexer);
    }
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        advance(s, lexer);
    }
    if (lexer->lookahead == '\r' || lexer->lookahead == '\n') {
        lexer->result_symbol = HTML_BLOCK_7_START;
        if (!s->simulate && !push_block(s, ANONYMOUS)) {
            return false;
        }
        return true;
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool parse_pipe_table(Scanner *s, TSLexer *lexer,
                             const bool *valid_symbols) {
    if (!valid_symbols[PIPE_TABLE_START]) {
        return false;
    }

    // PIPE_TABLE_START is zero width
    mark_end(s, lexer);
    ScannerSnapshot snapshot = snapshot_scanner(s);
    // count number of cells
    size_t cell_count = 0;
    // also remember if we see starting and ending pipes, as empty headers have
    // to have both
    bool starting_pipe = false;
    bool ending_pipe = false;
    if (lexer->lookahead == '|') {
        starting_pipe = true;
        advance(s, lexer);
    }
    while (lexer->lookahead != '\r' && lexer->lookahead != '\n' &&
           !lexer->eof(lexer)) {
        if (lexer->lookahead == '|') {
            cell_count++;
            ending_pipe = true;
            advance(s, lexer);
        } else {
            if (lexer->lookahead != ' ' && lexer->lookahead != '\t') {
                ending_pipe = false;
            }
            if (lexer->lookahead == '\\') {
                advance(s, lexer);
                if (is_punctuation((char)lexer->lookahead)) {
                    advance(s, lexer);
                }
            } else {
                advance(s, lexer);
            }
        }
    }
    if (cell_count == 0 && !(starting_pipe && ending_pipe)) {
        return restore_scanner_and_return(s, snapshot, false);
    }
    if (!ending_pipe) {
        cell_count++;
    }

    // check the following line for a delimiter row
    // parse a newline
    if (lexer->lookahead == '\n') {
        advance(s, lexer);
    } else if (lexer->lookahead == '\r') {
        advance(s, lexer);
        if (lexer->lookahead == '\n') {
            advance(s, lexer);
        }
    } else {
        return restore_scanner_and_return(s, snapshot, false);
    }
    s->indentation = 0;
    s->column = 0;
    for (;;) {
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            add_indentation(&s->indentation, advance(s, lexer));
        } else {
            break;
        }
    }
    s->simulate = true;
    size_t matched_temp = 0;
    while (matched_temp < s->open_blocks.size) {
        if (match(s, lexer, s->open_blocks.items[matched_temp])) {
            matched_temp++;
        } else {
            return restore_scanner_and_return(s, snapshot, false);
        }
    }

    // check if delimiter row has the same number of cells and at least one pipe
    size_t delimiter_cell_count = 0;
    if (lexer->lookahead == '|') {
        advance(s, lexer);
    }
    for (;;) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '|') {
            delimiter_cell_count++;
            advance(s, lexer);
            continue;
        }
        if (lexer->lookahead == ':') {
            advance(s, lexer);
            if (lexer->lookahead != '-') {
                return restore_scanner_and_return(s, snapshot, false);
            }
        }
        bool had_one_minus = false;
        while (lexer->lookahead == '-') {
            had_one_minus = true;
            advance(s, lexer);
        }
        if (had_one_minus) {
            delimiter_cell_count++;
        }
        if (lexer->lookahead == ':') {
            if (!had_one_minus) {
                return restore_scanner_and_return(s, snapshot, false);
            }
            advance(s, lexer);
        }
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(s, lexer);
        }
        if (lexer->lookahead == '|') {
            if (!had_one_minus) {
                delimiter_cell_count++;
            }
            advance(s, lexer);
            continue;
        }
        if (lexer->lookahead != '\r' && lexer->lookahead != '\n') {
            return restore_scanner_and_return(s, snapshot, false);
        } else {
            break;
        }
    }
    // if the cell counts are not equal then this is not a table
    if (cell_count != delimiter_cell_count) {
        return restore_scanner_and_return(s, snapshot, false);
    }

    lexer->result_symbol = PIPE_TABLE_START;
    return restore_scanner_and_return(s, snapshot, true);
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTNEXTLINE(readability-identifier-length)
static bool any_block_start_valid(const bool *valid_symbols) {
    return valid_symbols[BLOCK_QUOTE_START] ||
           valid_symbols[INDENTED_CHUNK_START] ||
           valid_symbols[ATX_H1_MARKER] || valid_symbols[ATX_H2_MARKER] ||
           valid_symbols[ATX_H3_MARKER] || valid_symbols[ATX_H4_MARKER] ||
           valid_symbols[ATX_H5_MARKER] || valid_symbols[ATX_H6_MARKER] ||
           valid_symbols[SETEXT_H1_UNDERLINE] ||
           valid_symbols[SETEXT_H2_UNDERLINE] ||
           valid_symbols[THEMATIC_BREAK] ||
           valid_symbols[LIST_MARKER_MINUS] || valid_symbols[LIST_MARKER_PLUS] ||
           valid_symbols[LIST_MARKER_STAR] || valid_symbols[LIST_MARKER_PARENTHESIS] ||
           valid_symbols[LIST_MARKER_DOT] ||
           valid_symbols[LIST_MARKER_MINUS_DONT_INTERRUPT] ||
           valid_symbols[LIST_MARKER_PLUS_DONT_INTERRUPT] ||
           valid_symbols[LIST_MARKER_STAR_DONT_INTERRUPT] ||
           valid_symbols[LIST_MARKER_PARENTHESIS_DONT_INTERRUPT] ||
           valid_symbols[LIST_MARKER_DOT_DONT_INTERRUPT] ||
           valid_symbols[TASK_LIST_MARKER_CHECKED] ||
           valid_symbols[TASK_LIST_MARKER_UNCHECKED] ||
           valid_symbols[MATH_BLOCK_OPEN_DELIMITER] ||
           valid_symbols[DIRECTIVE_BLOCK_OPEN_DELIMITER] ||
           valid_symbols[FENCED_CODE_BLOCK_START_BACKTICK] ||
           valid_symbols[FENCED_CODE_BLOCK_START_TILDE] ||
           valid_symbols[BLANK_LINE_START] ||
           valid_symbols[HTML_BLOCK_1_START] || valid_symbols[HTML_BLOCK_2_START] ||
           valid_symbols[HTML_BLOCK_3_START] || valid_symbols[HTML_BLOCK_4_START] ||
           valid_symbols[HTML_BLOCK_5_START] || valid_symbols[HTML_BLOCK_6_START] ||
           valid_symbols[HTML_BLOCK_7_START] ||
           valid_symbols[MINUS_METADATA] || valid_symbols[PLUS_METADATA] ||
           valid_symbols[PIPE_TABLE_START];
}

// NOLINTBEGIN(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)
static bool scan(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
    // A normal tree-sitter rule decided that the current branch is invalid and
    // now "requests" an error to stop the branch
    if (valid_symbols[TRIGGER_ERROR]) {
        return error(lexer);
    }

    // Close the inner most block after the next line break as requested. See
    // `$._close_block` in grammar.js
    if (valid_symbols[CLOSE_BLOCK]) {
        s->state |= STATE_CLOSE_BLOCK;
        lexer->result_symbol = CLOSE_BLOCK;
        return true;
    }

    // if we are at the end of the file and there are still open blocks close
    // them all
    if (lexer->eof(lexer)) {
        if (valid_symbols[TOKEN_EOF]) {
            lexer->result_symbol = TOKEN_EOF;
            return true;
        }
        if (s->open_blocks.size > 0) {
            lexer->result_symbol = BLOCK_CLOSE;
            if (!s->simulate) {
                pop_block(s);

            }
            return true;
        }
        return false;
    }

    if (!(s->state & STATE_MATCHING)) {
        // Parse any preceeding whitespace and remember its length. This makes a
        // lot of parsing quite a bit easier.  Skip this entirely when we are
        // tokenizing inline content (no block-start token is valid): there the
        // whitespace belongs to the surrounding text, and consuming it here
        // would fold it into the start of the next inline external token
        // (e.g. `autolink` would absorb the space before `<`).  In that case
        // scan() returns false on the whitespace and the inline text rule
        // consumes it; the next scan() call then starts at the real token.
        if (any_block_start_valid(valid_symbols)) {
            for (;;) {
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation, advance(s, lexer));
                } else {
                    break;
                }
            }
        }
        // We are not matching. This is where the parsing logic for most
        // "normal" token is. Most importantly parsing logic for the start of
        // new blocks.
        if (valid_symbols[INDENTED_CHUNK_START] &&
            !valid_symbols[NO_INDENTED_CHUNK]) {
            if (s->indentation >= INDENTED_CODE_INDENT &&
                lexer->lookahead != '\n' && lexer->lookahead != '\r') {
                lexer->result_symbol = INDENTED_CHUNK_START;
                if (!s->simulate && !push_block(s, INDENTED_CODE_BLOCK)) {
                    return false;
                }
                s->indentation -= INDENTED_CODE_INDENT;
                return true;
            }
        }
        // Decide which tokens to consider based on the first non-whitespace
        // character
        switch (lexer->lookahead) {
            case '\r':
            case '\n':
                if (valid_symbols[BLANK_LINE_START]) {
                    // A blank line token is actually just 0 width, so do not
                    // consume the characters
                    lexer->result_symbol = BLANK_LINE_START;
                    return true;
                }
                break;
            case '`':
                // A backtick could be a fenced code block (```) or an inline
                // code span (1-2 backticks).  parse_backtick() handles both
                // atomically, emitting an INLINE_CODE_BACKTICK_*_OPEN only
                // when a matching closer run exists before the next block
                // boundary.
                return parse_backtick(s, lexer, valid_symbols);
            case '~':
                // A tilde could be a fenced code block (~~~) or a strikethrough
                // (~~).  parse_tilde() handles both atomically, emitting
                // STRIKETHROUGH_OPEN when the run is exactly two tildes.
                return parse_tilde(s, lexer, valid_symbols);
            case '*':
                // A star could be a list marker, thematic break, or emphasis
                // open/close.  parse_star() handles all cases atomically,
                // including emitting EMPHASIS_STAR_OPEN when the star is not
                // a block-level token.
                return parse_star(s, lexer, valid_symbols);
            case '_':
                // An underscore could be a thematic break or an emphasis
                // open/close.  parse_underscore() handles all cases atomically,
                // including emitting EMPHASIS_UNDERSCORE_OPEN when the
                // underscore is not a block-level token.
                return parse_underscore(s, lexer, valid_symbols);
            case '>':
                // A '>' could mark the beginning of a block quote
                return parse_block_quote(s, lexer, valid_symbols);
            case '#':
                // A '#' could mark a atx heading
                return parse_atx_heading(s, lexer, valid_symbols);
            case '=':
                // A '=' could mark a setext underline
                return parse_setext_underline(s, lexer, valid_symbols);
            case '+':
                // A '+' could be a list marker
                return parse_plus(s, lexer, valid_symbols);
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                // A number could be a list marker (if followed by a dot or a
                // parenthesis)
                return parse_ordered_list_marker(s, lexer, valid_symbols);
            case '-':
                // A minus could mark a list marker, a thematic break or a
                // setext underline
                return parse_minus(s, lexer, valid_symbols);
            case '[':
                // A '[' could start a task list marker (`- [ ]`), a footnote
                // reference (`[^id]`), or a plain bracket (link label).
                // parse_bracket_open() disambiguates by the character after
                // the '[' and degrades to an ordinary bracket when neither
                // applies.
                return parse_bracket_open(s, lexer, valid_symbols);
            case '$':
                return parse_math_inline_delimiter(s, lexer, valid_symbols);
            case ':':
                // A colon could mark a directive block delimiter (`:::`).
                // parse_colon() emits DIRECTIVE_BLOCK_OPEN/CLOSE_DELIMITER for
                // a run of EXACTLY three colons; shorter/longer runs degrade
                // to ordinary text.
                return parse_colon(s, lexer, valid_symbols);
            case '<':
                // A < could be an autolink (inline) or the beginning of a
                // html block.  parse_autolink() validates the whole `<...>`
                // span and requires a closing `>`, emitting only the opening
                // `<` (the AUTOLINK_OPEN gate); it only runs when the
                // AUTOLINK_OPEN token is expected AND no block-HTML token is
                // valid (a block HTML tag takes precedence at line start).
                // Trying autolink first does NOT work: on a non-match it
                // consumes the `<` + content before returning false, and the
                // scanner cannot rewind within one call, so the html block
                // would start at the wrong position.
                if (valid_symbols[AUTOLINK_OPEN] &&
                    !(valid_symbols[HTML_BLOCK_1_START] ||
                      valid_symbols[HTML_BLOCK_2_START] ||
                      valid_symbols[HTML_BLOCK_3_START] ||
                      valid_symbols[HTML_BLOCK_4_START] ||
                      valid_symbols[HTML_BLOCK_5_START] ||
                      valid_symbols[HTML_BLOCK_6_START] ||
                      valid_symbols[HTML_BLOCK_7_START])) {
                    return parse_autolink(s, lexer, valid_symbols);
                }
                return parse_html_block(s, lexer, valid_symbols);
            default:
                break;
        }
        if (lexer->lookahead != '\r' && lexer->lookahead != '\n' &&
            valid_symbols[PIPE_TABLE_START]) {
            return parse_pipe_table(s, lexer, valid_symbols);
        }
    } else { // we are in the state of trying to match all currently open blocks
        bool partial_success = false;
        while (s->matched < s->open_blocks.size) {
            if (s->matched + 1U == s->open_blocks.size &&
                (s->state & STATE_CLOSE_BLOCK)) {
                if (!partial_success) {
                    s->state &= ~STATE_CLOSE_BLOCK;

                }
                break;
            }
            if (match(s, lexer, s->open_blocks.items[s->matched])) {
                partial_success = true;
                s->matched++;
            } else {
                if (s->state & STATE_WAS_SOFT_LINE_BREAK) {
                    s->state &= (~STATE_MATCHING);
                }
                break;
            }
        }
        if (partial_success) {
            if (s->matched == s->open_blocks.size) {
                s->state &= (~STATE_MATCHING);
            }
            lexer->result_symbol = BLOCK_CONTINUATION;
            return true;
        }

        if (!(s->state & STATE_WAS_SOFT_LINE_BREAK)) {
            lexer->result_symbol = BLOCK_CLOSE;
            pop_block(s);
            if (s->matched == s->open_blocks.size) {
                s->state &= (~STATE_MATCHING);
            }
            return true;
        }
    }

    // The parser just encountered a line break. Setup the state correspondingly
    if ((valid_symbols[LINE_ENDING] || valid_symbols[SOFT_LINE_ENDING] ||
         valid_symbols[PIPE_TABLE_LINE_ENDING]) &&
        (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
        consume_line_ending(s, lexer);
        s->indentation = 0;
        s->column = 0;
        if (!(s->state & STATE_CLOSE_BLOCK) &&
            (valid_symbols[SOFT_LINE_ENDING] ||
             valid_symbols[PIPE_TABLE_LINE_ENDING])) {
            lexer->mark_end(lexer);
            for (;;) {
                if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    add_indentation(&s->indentation, advance(s, lexer));
                } else {
                    break;
                }
            }
            s->simulate = true;
            size_t matched_temp = s->matched;
            s->matched = 0;
            bool one_will_be_matched = false;
            while (s->matched < s->open_blocks.size) {
                if (match(s, lexer, s->open_blocks.items[s->matched])) {
                    s->matched++;
                    one_will_be_matched = true;
                } else {
                    break;
                }
            }
            bool all_will_be_matched = s->matched == s->open_blocks.size;
            ScannerSnapshot interrupt_snapshot = snapshot_scanner(s);
            bool paragraph_interrupted =
                !lexer->eof(lexer) && scan(s, lexer, paragraph_interrupt_symbols);
            restore_scanner(s, interrupt_snapshot);
            if (!paragraph_interrupted) {
                s->matched = matched_temp;
                // If the last line break ended a paragraph and no new block
                // opened, the last line break should have been a soft line
                // break Reset the counter for matched blocks
                s->matched = 0;
                s->indentation = 0;
                s->column = 0;
                // If there is at least one open block, we should be in the
                // matching state. Also set the matching flag if a
                // `$._soft_line_break_marker` can be emitted so it does get
                // emitted.
                if (one_will_be_matched) {
                    s->state |= STATE_MATCHING;
                } else {
                    s->state &= (~STATE_MATCHING);
                }
                if (valid_symbols[PIPE_TABLE_LINE_ENDING]) {
                    if (all_will_be_matched) {
                        lexer->result_symbol = PIPE_TABLE_LINE_ENDING;
                        return true;
                    }
                } else {
                    lexer->result_symbol = SOFT_LINE_ENDING;
                    // reset some state variables
                    s->state |= STATE_WAS_SOFT_LINE_BREAK;
                    return true;
                }
            } else {
                s->matched = matched_temp;
            }
            s->indentation = 0;
            s->column = 0;
        }
        if (valid_symbols[LINE_ENDING]) {
            // If the last line break ended a paragraph and no new block opened,
            // the last line break should have been a soft line break Reset the
            // counter for matched blocks
            s->matched = 0;
            // If there is at least one open block, we should be in the matching
            // state. Also set the matching flag if a
            // `$._soft_line_break_marker` can be emitted so it does get
            // emitted.
            if (s->open_blocks.size > 0) {
                s->state |= STATE_MATCHING;
            } else {
                s->state &= (~STATE_MATCHING);
            }
            // reset some state variables
            s->state &= (~STATE_WAS_SOFT_LINE_BREAK);
            lexer->result_symbol = LINE_ENDING;
            return true;
        }
    }
    return false;
}
// NOLINTEND(readability-identifier-length,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-avoid-nested-conditional-operator,readability-else-after-return,readability-redundant-parentheses,readability-magic-numbers,readability-braces-around-statements,bugprone-switch-missing-default-case)


// NOLINTBEGIN(readability-identifier-length)
void *tree_sitter_markdown_external_scanner_create(void) {
    Scanner *s = ts_malloc(sizeof(Scanner));
    if (s == NULL) {
        return NULL;
    }
    s->open_blocks.items = NULL;
    s->open_blocks.capacity = 0;
    deserialize(s, NULL, 0);
    return s;
}
// NOLINTEND(readability-identifier-length)

// NOLINTNEXTLINE(readability-identifier-length)
bool tree_sitter_markdown_external_scanner_scan(void *payload, TSLexer *lexer,
                                                const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    scanner->simulate = false;
    return scan(scanner, lexer, valid_symbols);
}

// NOLINTNEXTLINE(readability-identifier-length)
unsigned tree_sitter_markdown_external_scanner_serialize(void *payload,
                                                         char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    return serialize(scanner, buffer);
}

// NOLINTNEXTLINE(readability-identifier-length)
void tree_sitter_markdown_external_scanner_deserialize(void *payload,
                                                       const char *buffer,
                                                       unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    deserialize(scanner, buffer, length);
}

// NOLINTNEXTLINE(readability-identifier-length)
void tree_sitter_markdown_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    ts_free(scanner->open_blocks.items);
    ts_free(scanner);
}
