// Markdown grammar for tree-sitter.
//
// Derived from https://github.com/tree-sitter-grammars/tree-sitter-markdown
// (the `split_parser` branch, `tree-sitter-markdown` block grammar + `common/common.js`),
// extended with structured inline parsing so consumers can query `(emphasis)`,
// `(inline_link)`, `(full_reference_link)`, `(collapsed_reference_link)`, `(shortcut_link)`, `(word_token)`, etc. directly under `(inline)`.
//
// Covers CommonMark Spec (https://spec.commonmark.org/0.30/) block structure
// plus the following extensions always enabled:
//  - YAML front matter, TOML front matter
//  - GFM pipe tables
//  - GFM task lists (promoted to task_list_item)
//  - GFM alerts promoted to callout
//  - GFM strikethrough
//  - Pandoc display math ($$), Pandoc inline math ($)
//  - Generic container directives (:::)
//  - Footnote definitions and references
//  - Image blocks (paragraph with a single image)
//  - MDX JSX blocks and inline elements (shallow)
//
// Inline content emitted under paragraph/heading/footnote bodies is
// structured into the nodes required by mehen's Required Markdown AST Model:
// text_span, word_token, numeric_token, identifier_like_token, path_like_token,
// terminator, separator, bracket, operator_like, inline_code, emphasis,
// strong, strikethrough, inline_link, full_reference_link, collapsed_reference_link, shortcut_link, image, autolink, html_inline, mdx_jsx_inline,
// math_inline, footnote_reference.
//
// Known simplifications vs full CommonMark: emphasis/strong use simple paired
// delimiters rather than the full left/right-flanking algorithm; MDX JSX is
// shallow; pipe-table cells are not reclassified into inline tokens; inline
// code supports only 1- or 2-backtick runs.

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

import nodeFs from 'node:fs';
import nodeUrl from 'node:url';

const PUNCTUATION_CHARACTERS_REGEX = '!-/:-@\\[-`\\{-~';
const PUNCTUATION_CHARACTERS_ARRAY = [
  '!', '"', '#', '$', '%', '&', '\'', '(', ')', '*', '+', ',', '-', '.', '/', ':', ';', '<',
  '=', '>', '?', '@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~',
];
const PRECEDENCE_LEVEL_LINK = 10;
const PRECEDENCE_LEVEL_FOOTNOTE = 11;
const PRECEDENCE_LEVEL_IMAGE = 11;
const PRECEDENCE_LEVEL_MATH_BLOCK = 12;
const PRECEDENCE_LEVEL_DIRECTIVE_BLOCK = 12;
const PRECEDENCE_LEVEL_IMAGE_BLOCK = 12;

export default grammar({
  name: 'markdown',

  rules: {
    document: ($) => seq(
      optional(choice($.minus_metadata, $.plus_metadata)),
      alias(prec.right(repeat($._block_not_section)), $.section),
      repeat($.section),
    ),

    // Common building blocks formerly in common/common.js.

    backslash_escape: ($) => $._backslash_escape,
    _backslash_escape: ($) => new RegExp('\\\\[' + PUNCTUATION_CHARACTERS_REGEX + ']'),

    entity_reference: ($) => html_entity_regex(),
    numeric_character_reference: ($) => /&#([0-9]{1,7}|[xX][0-9a-fA-F]{1,6});/,

    // Link text between `[` and `]`. Parsed with the same structured inline
    // elements as ordinary paragraph text (code spans, emphasis, strong,
    // strikethrough, token classifiers) so labels like [`path/to/file`],
    // [**bold text**], [InnovAIte] form links. Deliberately narrower than
    // paragraph text:
    //  - links (inline/full/collapsed/shortcut) and images are excluded:
    //    CommonMark §6.3 — "Links may not contain other links, at any level of
    //    nesting" — so `[a [nested] link](url)` does not become a link.
    //  - footnote_reference is excluded: `[^1]` must stay a footnote reference
    //    (the `[^` token outranks the bare `[` at the lexer).
    //  - `[` and `]` are excluded (see `_label_bracket`), so the label always
    //    closes at the first unescaped `]`.
    link_label: ($) => seq('[', repeat1(choice(
      $._inline_label_element,
      $._soft_line_break,
    )), ']'),

    // One inline element allowed inside a link label.
    _inline_label_element: ($) => choice(
      $._whitespace,
      $.inline_code,
      $.autolink,
      $.html_inline,
      $.mdx_jsx_inline,
      $.math_inline,
      $.strong,
      $.emphasis,
      $.strikethrough,
      $.backslash_escape,
      $.entity_reference,
      $.numeric_character_reference,
      alias($._label_text, $.text_span),
    ),

    // Runs of plain text tokens inside a label. Mirrors paragraph `text_span`
    // (the same §3.2/§3.3 classifiers) but with square brackets excluded so a
    // nested `[...]` cannot be swallowed by the label.
    _label_text: ($) => prec.right(repeat1(choice(
      $.numeric_token,
      $.path_like_token,
      $.identifier_like_token,
      $.word_token,
      $.terminator,
      $.separator,
      $._label_bracket,
      $.operator_like,
    ))),
    // Bracket punctuation allowed in a label: everything except `[`/`]`.
    _label_bracket: ($) => alias(choice('(', ')', '{', '}', '<', '>'), $.bracket),

    link_destination: ($) => prec.dynamic(PRECEDENCE_LEVEL_LINK, choice(
      seq('<', repeat(choice($._text_no_angle, $.backslash_escape, $.entity_reference, $.numeric_character_reference)), '>'),
      seq(
        choice(
          $._word,
          punctuation_without($, ['<', '(', ')']),
          $.backslash_escape,
          $.entity_reference,
          $.numeric_character_reference,
          $._link_destination_parenthesis,
        ),
        repeat(choice(
          $._word,
          punctuation_without($, ['(', ')']),
          $.backslash_escape,
          $.entity_reference,
          $.numeric_character_reference,
          $._link_destination_parenthesis,
        )),
      ),
    )),
    _link_destination_parenthesis: ($) => seq('(', repeat(choice(
      $._word,
      punctuation_without($, ['(', ')']),
      $.backslash_escape,
      $.entity_reference,
      $.numeric_character_reference,
      $._link_destination_parenthesis,
    )), ')'),
    _text_no_angle: ($) => choice($._word, punctuation_without($, ['<', '>']), $._whitespace),
    link_title: ($) => choice(
      seq('"', repeat(choice(
        $._word,
        punctuation_without($, ['"']),
        $._whitespace,
        $.backslash_escape,
        $.entity_reference,
        $.numeric_character_reference,
        seq($._soft_line_break, optional(seq($._soft_line_break, $._trigger_error))),
      )), '"'),
      seq('\'', repeat(choice(
        $._word,
        punctuation_without($, ['\'']),
        $._whitespace,
        $.backslash_escape,
        $.entity_reference,
        $.numeric_character_reference,
        seq($._soft_line_break, optional(seq($._soft_line_break, $._trigger_error))),
      )), '\''),
      seq('(', repeat(choice(
        $._word,
        punctuation_without($, ['(', ')']),
        $._whitespace,
        $.backslash_escape,
        $.entity_reference,
        $.numeric_character_reference,
        seq($._soft_line_break, optional(seq($._soft_line_break, $._trigger_error))),
      )), ')'),
    ),

    _newline_token: ($) => /\n|\r\n?/,

    // Needed for compatibility with upstream common rules that referenced it
    // from the inline grammar.
    _last_token_punctuation: ($) => choice(),

    // BLOCK STRUCTURE

    // All blocks. Every block contains a trailing newline.
    _block: ($) => choice(
      $._block_not_section,
      $.section,
    ),
    _block_not_section: ($) => choice(
      alias($._setext_heading1, $.setext_heading),
      alias($._setext_heading2, $.setext_heading),
      $.paragraph,
      $.indented_code_block,
      $.block_quote,
      $.thematic_break,
      $.list,
      $.fenced_code_block,
      $.blank_line,
      $.html_block,
      $.mdx_jsx_block,
      $.link_reference_definition,
      $.footnote_definition,
      $.math_block,
      $.directive_block,
      $.image_block,
      $.pipe_table,
    ),
    section: ($) => choice($._section1, $._section2, $._section3, $._section4, $._section5, $._section6),
    _section1: ($) => prec.right(seq(
      alias($._atx_heading1, $.atx_heading),
      repeat(choice(
        alias(choice($._section6, $._section5, $._section4, $._section3, $._section2), $.section),
        $._block_not_section,
      )),
    )),
    _section2: ($) => prec.right(seq(
      alias($._atx_heading2, $.atx_heading),
      repeat(choice(
        alias(choice($._section6, $._section5, $._section4, $._section3), $.section),
        $._block_not_section,
      )),
    )),
    _section3: ($) => prec.right(seq(
      alias($._atx_heading3, $.atx_heading),
      repeat(choice(
        alias(choice($._section6, $._section5, $._section4), $.section),
        $._block_not_section,
      )),
    )),
    _section4: ($) => prec.right(seq(
      alias($._atx_heading4, $.atx_heading),
      repeat(choice(
        alias(choice($._section6, $._section5), $.section),
        $._block_not_section,
      )),
    )),
    _section5: ($) => prec.right(seq(
      alias($._atx_heading5, $.atx_heading),
      repeat(choice(
        alias($._section6, $.section),
        $._block_not_section,
      )),
    )),
    _section6: ($) => prec.right(seq(
      alias($._atx_heading6, $.atx_heading),
      repeat($._block_not_section),
    )),

    // LEAF BLOCKS

    // A thematic break. This is currently handled by the external scanner but maybe could be
    // parsed using normal tree-sitter rules.
    //
    // https://github.github.com/gfm/#thematic-breaks
    thematic_break: ($) => seq($._thematic_break, choice($._newline, $._eof)),

    // An ATX heading. The heading level (h1..h6) is exposed as a `level` field
    // that binds the `atx_h{N}_marker` child. A textlint-shaped consumer can
    // read it directly via `(atx_heading level: _ @lvl)`.
    //
    // https://github.github.com/gfm/#atx-headings
    _atx_heading1: ($) => prec(1, seq(
      field('level', $.atx_h1_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading2: ($) => prec(1, seq(
      field('level', $.atx_h2_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading3: ($) => prec(1, seq(
      field('level', $.atx_h3_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading4: ($) => prec(1, seq(
      field('level', $.atx_h4_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading5: ($) => prec(1, seq(
      field('level', $.atx_h5_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading6: ($) => prec(1, seq(
      field('level', $.atx_h6_marker),
      optional($._atx_heading_content),
      $._newline,
    )),
    _atx_heading_content: ($) => prec(1, seq(
      optional($._whitespace),
      field('heading_content', alias($._inline_content_line, $.inline)),
    )),

    // A setext heading. The heading level is the underline kind, exposed as a
    // `level` field for parity with ATX headings.
    //
    // https://github.github.com/gfm/#setext-headings
    _setext_heading1: ($) => seq(
      field('heading_content', $.paragraph),
      field('level', $.setext_h1_underline),
      choice($._newline, $._eof),
    ),
    _setext_heading2: ($) => seq(
      field('heading_content', $.paragraph),
      field('level', $.setext_h2_underline),
      choice($._newline, $._eof),
    ),

    // An indented code block. An indented code block is made up of indented chunks and blank
    // lines. The indented chunks are handled by the external scanner.
    //
    // https://github.github.com/gfm/#indented-code-blocks
    indented_code_block: ($) => prec.right(seq($._indented_chunk, repeat(choice($._indented_chunk, $.blank_line)))),
    _indented_chunk: ($) => seq($._indented_chunk_start, repeat(choice($._line, $._newline)), $._block_close, optional($.block_continuation)),

    // A fenced code block. Fenced code blocks are mainly handled by the external scanner. In
    // case of backtick code blocks the external scanner also checks that the info string is
    // proper.
    //
    // https://github.github.com/gfm/#fenced-code-blocks
    fenced_code_block: ($) => prec.right(choice(
      seq(
        alias($._fenced_code_block_start_backtick, $.fenced_code_block_delimiter),
        optional($._whitespace),
        optional($.info_string),
        $._newline,
        optional($.code_fence_content),
        optional(seq(alias($._fenced_code_block_end_backtick, $.fenced_code_block_delimiter), $._close_block, $._newline)),
        $._block_close,
      ),
      seq(
        alias($._fenced_code_block_start_tilde, $.fenced_code_block_delimiter),
        optional($._whitespace),
        optional($.info_string),
        $._newline,
        optional($.code_fence_content),
        optional(seq(alias($._fenced_code_block_end_tilde, $.fenced_code_block_delimiter), $._close_block, $._newline)),
        $._block_close,
      ),
    )),
    code_fence_content: ($) => repeat1(choice($._newline, $._line)),
    info_string: ($) => choice(
      seq($.language, repeat(choice($._line, $.backslash_escape, $.entity_reference, $.numeric_character_reference))),
      seq(
        repeat1(choice('{', '}')),
        optional(choice(
          seq($.language, repeat(choice($._line, $.backslash_escape, $.entity_reference, $.numeric_character_reference))),
          seq($._whitespace, repeat(choice($._line, $.backslash_escape, $.entity_reference, $.numeric_character_reference))),
        )),
      ),
    ),
    language: ($) => prec.right(repeat1(choice($._word, punctuation_without($, ['{', '}', ',']), $.backslash_escape, $.entity_reference, $.numeric_character_reference))),

    // An HTML block. Type 2 (`<!-- ... -->`) is aliased to `html_comment_block`
    // so consumers computing markdown-CLOC can target comments directly. Other
    // types remain as `html_block`; injections/HTML parsers should do the rest.
    //
    // https://github.github.com/gfm/#html-blocks
    html_block: ($) => prec(1, seq(optional($._whitespace), choice(
      $._html_block_1,
      alias($._html_block_2, $.html_comment_block),
      $._html_block_3,
      $._html_block_4,
      $._html_block_5,
      $._html_block_6,
      $._html_block_7,
    ))),
    _html_block_1: ($) => build_html_block($, $._html_block_1_start, $._html_block_1_end),
    _html_block_2: ($) => build_html_block($, $._html_block_2_start, '-->'),
    _html_block_3: ($) => build_html_block($, $._html_block_3_start, '?>'),
    _html_block_4: ($) => build_html_block($, $._html_block_4_start, '>'),
    _html_block_5: ($) => build_html_block($, $._html_block_5_start, ']]>'),
    _html_block_6: ($) => build_html_block($, $._html_block_6_start, seq($._newline, $.blank_line)),
    _html_block_7: ($) => build_html_block($, $._html_block_7_start, seq($._newline, $.blank_line)),

    // A footnote definition. Modeled after `link_reference_definition`.
    // Syntax: `[^label]: definition text`. The `^` immediately after `[`
    // disambiguates it from a regular link reference.
    //
    // https://github.github.com/gfm/#footnotes (not in the base spec, widely
    // supported extension).
    footnote_definition: ($) => choice(
      prec.dynamic(PRECEDENCE_LEVEL_FOOTNOTE + 1, seq(
        $._footnote_definition_start,
        $._newline,
        $._footnote_definition_continuation,
      )),
      prec.dynamic(PRECEDENCE_LEVEL_FOOTNOTE, seq(
        $._footnote_definition_start,
        choice($._newline, $._eof),
      )),
    ),
    _footnote_definition_start: ($) => seq(
      optional($._whitespace),
      $.footnote_label,
      ':',
      optional($._whitespace),
      optional(alias($._inline_content_line, $.inline)),
    ),
    _footnote_definition_continuation: ($) => choice(
      prec.dynamic(1, seq(
        $._whitespace,
        alias($._inline_content_line, $.inline),
        $._newline,
        $._footnote_definition_continuation,
      )),
      seq(
        $._whitespace,
        alias($._inline_content_line, $.inline),
        choice($._newline, $._eof),
      ),
    ),
    footnote_label: ($) => seq(alias($._footnote_ref_open, $.footnote_label_open), repeat1(choice(
      $._word,
      $.backslash_escape,
      punctuation_without($, ['[', ']', '^']),
    )), ']'),

    // A math block (Pandoc / GitLab / KaTeX display math).
    // Syntax:
    //   $$
    //   formula
    //   $$
    // Pure grammar implementation: the opening `$$` and closing `$$` are each
    // required to sit alone on their own line. The literal `$$` token (two
    // adjacent `$` characters) beats the single `$` punctuation that would
    // otherwise be consumed by `_line` because tree-sitter prefers the longer
    // token at a given lex position.
    math_block: ($) => prec.dynamic(PRECEDENCE_LEVEL_MATH_BLOCK, seq(
      alias($._math_block_delimiter, $.math_block_delimiter),
      $._newline,
      optional($.math_block_content),
      alias($._math_block_delimiter, $.math_block_delimiter),
      choice($._newline, $._eof),
    )),
    _math_block_delimiter: ($) => token(prec(4, '$$')),
    math_block_content: ($) => prec.right(repeat1(choice($._line, $._newline))),

    // A directive block (`:::name ... :::`). Implemented following the syntax
    // used by remark-directive / MyST / Pandoc fenced divs: a line opening
    // with three or more colons followed by an optional name, content lines,
    // then a matching closing line of colons. For simplicity the opening and
    // closing use a fixed `:::` literal; longer runs fall back to the
    // paragraph rule.
    directive_block: ($) => prec.dynamic(PRECEDENCE_LEVEL_DIRECTIVE_BLOCK, seq(
      alias($._directive_block_delimiter, $.directive_block_delimiter),
      optional($._whitespace),
      optional($.directive_name),
      $._newline,
      optional($.directive_block_content),
      alias($._directive_block_delimiter, $.directive_block_delimiter),
      choice($._newline, $._eof),
    )),
    _directive_block_delimiter: ($) => ':::',
    directive_name: ($) => prec.right(repeat1(choice(
      $._word,
      punctuation_without($, [':']),
    ))),
    directive_block_content: ($) => prec.right(repeat1(choice($._line, $._newline))),

    // A block-level image (paragraph consisting of a single image).
    // Syntax: `![alt](destination)` on its own line.
    // Kept intentionally minimal: title strings are omitted to keep the rule
    // unambiguous with `link_reference_definition`. If a title is present the
    // paragraph falls back to ordinary `paragraph` with inline content.
    image_block: ($) => prec.dynamic(PRECEDENCE_LEVEL_IMAGE_BLOCK, seq(
      optional($._whitespace),
      '!',
      $.link_label,
      '(',
      optional($._whitespace),
      optional($.link_destination),
      optional($._whitespace),
      ')',
      optional($._whitespace),
      choice($._newline, $._eof),
    )),

    // MDX JSX block. Shallow block-level recognizer: a line that begins
    // (after optional whitespace) with an MDX-style JSX tag (<Name ...>,
    // <Name ... />, or </Name>) is surfaced as `mdx_jsx_block`.
    // Paired matching of open/close tags is not required at the grammar
    // level; consumers can validate. Full JSX/expression semantics are out
    // of scope — see docs/textlint-mapping.md.
    mdx_jsx_block: ($) => prec(3, seq(
      optional($._whitespace),
      choice(
        alias($._mdx_jsx_open_block_tag, $.mdx_jsx_open_tag),
        alias($._mdx_jsx_close_block_tag, $.mdx_jsx_close_tag),
      ),
      optional($._whitespace),
      choice($._newline, $._eof),
    )),
    _mdx_jsx_open_block_tag: ($) => token(prec(5, new RegExp(
      '<[A-Z][A-Za-z0-9.]*(\\s+[a-zA-Z_:][a-zA-Z0-9_.:-]*(\\s*=\\s*("[^"]*"|\'[^\']*\'|\\{([^{}]|\\{[^{}]*\\})*\\}|[^\\s"\'=<>`{}]+))?)*\\s*/?>',
    ))),
    _mdx_jsx_close_block_tag: ($) => token(prec(5, /<\/[A-Z][A-Za-z0-9.]*\s*>/)),

    // A link reference definition.
    //
    // https://github.github.com/gfm/#link-reference-definitions
    link_reference_definition: ($) => prec.dynamic(PRECEDENCE_LEVEL_LINK, seq(
      optional($._whitespace),
      $.link_label,
      ':',
      optional(seq(optional($._whitespace), optional(seq($._soft_line_break, optional($._whitespace))))),
      $.link_destination,
      optional(prec.dynamic(2 * PRECEDENCE_LEVEL_LINK, seq(
        choice(
          seq($._whitespace, optional(seq($._soft_line_break, optional($._whitespace)))),
          seq($._soft_line_break, optional($._whitespace)),
        ),
        optional($._no_indented_chunk),
        $.link_title,
      ))),
      choice($._newline, $._soft_line_break, $._eof),
    )),
    _text_inline_no_link: ($) => choice($._word, $._whitespace, punctuation_without($, ['[', ']'])),

    // A paragraph.
    //
    // https://github.github.com/gfm/#paragraphs
    paragraph: ($) => seq(alias($._inline_content, $.inline), choice($._newline, $._eof)),

    // A blank line including the following newline. Publicly named so metrics
    // like mehen's BLANK can target it directly.
    //
    // https://github.github.com/gfm/#blank-lines
    blank_line: ($) => seq($._blank_line_start, choice($._newline, $._eof)),

    // CONTAINER BLOCKS

    // A block quote. If its first paragraph starts with `[!NOTE]`,
    // `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`, or `[!CAUTION]` (GFM alerts),
    // the whole node is aliased to `callout` with a `callout_type` child.
    block_quote: ($) => choice(
      alias($._callout, $.callout),
      $._plain_block_quote,
    ),
    _plain_block_quote: ($) => seq(
      alias($._block_quote_start, $.block_quote_marker),
      optional($.block_continuation),
      repeat($._block),
      $._block_close,
      optional($.block_continuation),
    ),
    _callout: ($) => prec.dynamic(1, seq(
      alias($._block_quote_start, $.block_quote_marker),
      optional($.block_continuation),
      $._callout_header_paragraph,
      repeat($._block),
      $._block_close,
      optional($.block_continuation),
    )),
    _callout_header_paragraph: ($) => seq(
      alias(seq(
        alias($._callout_marker_open, $.callout_marker_open),
        field('callout_type', alias($._callout_type, $.callout_type)),
        alias($._callout_marker_close, $.callout_marker_close),
        optional(alias($._inline_content_line, $.inline)),
      ), $.paragraph),
      choice($._newline, $._eof),
    ),
    _callout_marker_open: ($) => seq('[', '!'),
    _callout_marker_close: ($) => ']',
    _callout_type: ($) => choice('NOTE', 'TIP', 'IMPORTANT', 'WARNING', 'CAUTION'),

    list: ($) => prec.right(choice(
      $._list_plus,
      $._list_minus,
      $._list_star,
      $._list_dot,
      $._list_parenthesis,
    )),
    _list_plus: ($) => prec.right(repeat1(choice(
      alias($._task_list_item_plus, $.task_list_item),
      alias($._list_item_plus, $.list_item),
    ))),
    _list_minus: ($) => prec.right(repeat1(choice(
      alias($._task_list_item_minus, $.task_list_item),
      alias($._list_item_minus, $.list_item),
    ))),
    _list_star: ($) => prec.right(repeat1(choice(
      alias($._task_list_item_star, $.task_list_item),
      alias($._list_item_star, $.list_item),
    ))),
    _list_dot: ($) => prec.right(repeat1(choice(
      alias($._task_list_item_dot, $.task_list_item),
      alias($._list_item_dot, $.list_item),
    ))),
    _list_parenthesis: ($) => prec.right(repeat1(choice(
      alias($._task_list_item_parenthesis, $.task_list_item),
      alias($._list_item_parenthesis, $.list_item),
    ))),
    list_marker_plus: ($) => choice($._list_marker_plus, $._list_marker_plus_dont_interrupt),
    list_marker_minus: ($) => choice($._list_marker_minus, $._list_marker_minus_dont_interrupt),
    list_marker_star: ($) => choice($._list_marker_star, $._list_marker_star_dont_interrupt),
    list_marker_dot: ($) => choice($._list_marker_dot, $._list_marker_dot_dont_interrupt),
    list_marker_parenthesis: ($) => choice($._list_marker_parenthesis, $._list_marker_parenthesis_dont_interrupt),
    _list_item_plus: ($) => seq(
      $.list_marker_plus,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_minus: ($) => seq(
      $.list_marker_minus,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_star: ($) => seq(
      $.list_marker_star,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_dot: ($) => seq(
      $.list_marker_dot,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_parenthesis: ($) => seq(
      $.list_marker_parenthesis,
      optional($.block_continuation),
      $._list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _task_list_item_plus: ($) => seq(
      $.list_marker_plus,
      optional($.block_continuation),
      $._task_list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _task_list_item_minus: ($) => seq(
      $.list_marker_minus,
      optional($.block_continuation),
      $._task_list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _task_list_item_star: ($) => seq(
      $.list_marker_star,
      optional($.block_continuation),
      $._task_list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _task_list_item_dot: ($) => seq(
      $.list_marker_dot,
      optional($.block_continuation),
      $._task_list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _task_list_item_parenthesis: ($) => seq(
      $.list_marker_parenthesis,
      optional($.block_continuation),
      $._task_list_item_content,
      $._block_close,
      optional($.block_continuation),
    ),
    _list_item_content: ($) => choice(
      prec(1, seq(
        $.blank_line,
        $.blank_line,
        $._close_block,
        optional($.block_continuation),
      )),
      repeat1($._block),
    ),
    _task_list_item_content: ($) => prec(1, seq(
      choice($.task_list_marker_checked, $.task_list_marker_unchecked),
      choice(
        seq($._whitespace, optional($.paragraph), repeat($._block)),
        seq($.blank_line, repeat($._block)),
      ),
    )),

    _newline: ($) => seq(
      $._line_ending,
      optional($.block_continuation),
    ),
    _soft_line_break: ($) => seq(
      $._soft_line_ending,
      optional($.block_continuation),
    ),
    _line: ($) => prec.right(repeat1(choice($._word, $._whitespace, punctuation_without($, [])))),
    _word: ($) => choice(
      new RegExp('[^' + PUNCTUATION_CHARACTERS_REGEX + ' \\t\\n\\r]+'),
      /\[[xX]\]/,
      /\[[ \t]\]/,
    ),
    _whitespace: ($) => /[ \t]+/,

    // ---------------------------------------------------------------------
    // INLINE CONTENT
    //
    // `_inline_content` is the rule used wherever block rules previously
    // emitted `alias(_line, $.inline)` (paragraph, ATX heading content,
    // footnote definition body). It emits a sequence of classified tokens
    // (word_token / numeric_token / identifier_like_token / path_like_token
    // and punctuation-class nodes) plus inline structural nodes
    // (inline_code, emphasis, strong, strikethrough, inline_link, full_reference_link, collapsed_reference_link, shortcut_link, image, autolink,
    // html_inline, mdx_jsx_inline, math_inline, footnote_reference).
    //
    // The outer `inline` wrapper is kept so existing queries that match
    // `(paragraph (inline))` continue to work. Consumers targeting
    // `(inline (emphasis))` etc. now get structured children.
    // ---------------------------------------------------------------------

    _inline_content: ($) => prec.right(repeat1(choice(
      $._inline_element,
      $._soft_line_break,
    ))),
    // Single-line inline content (no soft line breaks) — used for contexts
    // like ATX headings, footnote definition bodies, and similar where the
    // content must fit on one logical line.
    _inline_content_line: ($) => prec.right(repeat1($._inline_element)),
    _inline_element: ($) => choice(
      $._whitespace,
      $.inline_code,
      $.autolink,
      $.html_inline,
      $.mdx_jsx_inline,
      $.math_inline,
      $.image,
      $.footnote_reference,
      $.inline_link,
      $.full_reference_link,
      $.collapsed_reference_link,
      $.shortcut_link,
      $.strong,
      $.emphasis,
      $.strikethrough,
      $.text_span,
    ),

    // A text_span groups a run of classified tokens and punctuation-class
    // nodes with no intervening structural inline nodes. This gives
    // consumers a single node to query for prose chunks while still
    // preserving the inner token classification required by §3.2/§3.3.
    text_span: ($) => prec.right(repeat1(choice(
      $.numeric_token,
      $.path_like_token,
      $.identifier_like_token,
      $.word_token,
      $.terminator,
      $.separator,
      $.bracket,
      $.operator_like,
    ))),

    // --- §3.2 token classifiers -----------------------------------------

    // Pure alphabetic run (letters only). Simple word in prose.
    word_token: ($) => new RustRegex('\\p{L}+'),

    // Numeric tokens: integers, decimals, multi-dot versions (1, 1.0, 1.2.3).
    numeric_token: ($) => /[0-9]+(\.[0-9]+)*/,

    // Identifier-like tokens: alnum runs with at least one underscore or at
    // least one digit adjacent to letters (camelCase/snake_case). Requires
    // anchoring to distinguish from plain word_token / numeric_token.
    identifier_like_token: ($) => token(prec(1, choice(
      // snake_case / underscored
      /[A-Za-z][A-Za-z0-9]*(_[A-Za-z0-9]+)+/,
      /_[A-Za-z0-9]+([_][A-Za-z0-9]+)*/,
      // camelCase / PascalCase
      /[a-z]+[A-Z][A-Za-z0-9]*/,
      /[A-Z][a-z]+[A-Z][A-Za-z0-9]*/,
      // alnum mix (letter then digit or digit then letter)
      /[A-Za-z]+[0-9]+[A-Za-z0-9]*/,
      /[0-9]+[A-Za-z][A-Za-z0-9]*/,
    ))),

    // Path-like tokens: runs containing at least one slash between alnum
    // segments, or dotted path with 2+ dots (e.g. a.b.c).
    path_like_token: ($) => token(prec(2, choice(
      // slashed path: foo/bar/baz, ./foo, ../foo, /foo
      /(\.{1,2}\/|\/)?[A-Za-z0-9_.-]+(\/[A-Za-z0-9_.-]+)+/,
      // dotted path with 3+ segments (a.b.c, com.example.Foo)
      /[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*){2,}/,
    ))),

    // --- §3.3 punctuation classes ---------------------------------------

    // Sentence terminators. Includes the specified Unicode codepoints.
    terminator: ($) => choice('.', '?', '!', '\u3002', '\u2026'),

    // Clause separators.
    separator: ($) => choice(',', ';', ':'),

    // Paired brackets (individual chars; pairing is structural in link/image/html rules).
    bracket: ($) => choice('(', ')', '[', ']', '{', '}', '<', '>'),

    // Operator-like punctuation. Covers spec-listed operators plus the
    // remaining punctuation chars so every punctuation lexeme has a class.
    // Non-ASCII symbol runes (emoji, currency marks, copyright signs, etc.)
    // also land here so ordinary prose never falls into ERROR.
    operator_like: ($) => choice(
      // Multi-character operators (longer tokens first so lexer prefers them)
      '::', '->', '=>',
      // `$$` appearing inline (outside a math_block context) classifies as operator_like.
      alias($._dollar_dollar_inline, '$$'),
      $._unicode_symbol_run,
      $._unicode_punctuation_run,
      '=', '+', '-', '*', '/', '|', '&',
      // Remaining ASCII punctuation not covered by other classes.
      '"', '#', '$', '%', '\'', '@', '\\', '^', '_', '`', '~',
    ),
    _dollar_dollar_inline: ($) => token(prec(3, '$$')),
    _unicode_symbol_run: ($) => new RustRegex('[\\p{S}&&[^\\x00-\\x7F]]+'),
    _unicode_punctuation_run: ($) => new RustRegex('[\\p{P}&&[^\\x00-\\x7F]]+'),

    // --- §3.2 structural inline nodes -----------------------------------

    // Inline code. Supports delimiter runs of 1 or 2 backticks. The content
    // is everything up to the matching run of the same length. A full
    // CommonMark implementation handles arbitrarily long runs; two cases
    // cover the overwhelming majority of real documents.
    inline_code: ($) => choice(
      seq(
        alias($._backtick_1, $.inline_code_delimiter),
        optional(alias(/[^`\n\r]+/, $.inline_code_content)),
        alias($._backtick_1, $.inline_code_delimiter),
      ),
      seq(
        alias($._backtick_2, $.inline_code_delimiter),
        optional(alias(/([^`\n\r]|`[^`\n\r])+/, $.inline_code_content)),
        alias($._backtick_2, $.inline_code_delimiter),
      ),
    ),
    _backtick_1: ($) => token(prec(1, '`')),
    _backtick_2: ($) => token(prec(2, '``')),

    // Autolinks: <scheme:rest> and <email@host>.
    autolink: ($) => choice(
      seq('<', alias(/[A-Za-z][A-Za-z0-9+.\-]{1,31}:[^<> \t\n\r]+/, $.uri), '>'),
      seq('<', alias(/[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+(\.[A-Za-z0-9\-]+)+/, $.email), '>'),
    ),

    // Raw HTML spans: opening tag, closing tag, self-closing, comment,
    // CDATA, declaration, processing instruction. Attribute parsing is
    // intentionally permissive.
    html_inline: ($) => choice(
      // Comment
      alias(/<!--([^-]|-[^-]|--[^>])*-->/, $.html_comment),
      // CDATA
      alias(/<!\[CDATA\[([^\]]|\][^\]]|\]\][^>])*\]\]>/, $.html_cdata),
      // Processing instruction
      alias(/<\?([^?]|\?[^>])*\?>/, $.html_processing_instruction),
      // Declaration
      alias(/<![A-Z][^>]*>/, $.html_declaration),
      // Open or self-closing tag. Lowercase-starting tags and all-caps tags
      // are HTML; mixed-case uppercase-starting names fall through to MDX JSX.
      alias(/<(?:[a-z][A-Za-z0-9-]*|[A-Z][A-Z0-9-]*)(\s+[a-zA-Z_:][a-zA-Z0-9_.:-]*(\s*=\s*("[^"]*"|'[^']*'|[^\s"'=<>`]+))?)*\s*\/?>/, $.html_open_tag),
      // Closing tag
      alias(/<\/(?:[a-z][A-Za-z0-9-]*|[A-Z][A-Z0-9-]*)\s*>/, $.html_close_tag),
    ),

    // MDX JSX inline. Shallow recognizer: matches <Name ...>, </Name>,
    // <Name/>, and {expression} as opaque spans. Mixed-case names that start
    // with an uppercase letter distinguish MDX JSX from all-caps HTML tags.
    // Full MDX parsing is out of scope; see docs/textlint-mapping.md.
    mdx_jsx_inline: ($) => choice(
      alias(new RegExp([
        '<[A-Z][A-Za-z0-9.]*[a-z][A-Za-z0-9.]*',
        '(\\s+[a-zA-Z_:][a-zA-Z0-9_.:-]*',
        '(\\s*=\\s*("[^"]*"|\'[^\']*\'|\\{([^{}]|\\{[^{}]*\\})*\\}|[^\\s"\'=<>`{}]+))?)*\\s*/?>',
      ].join('')), $.mdx_jsx_open_tag),
      alias(/<\/[A-Z][A-Za-z0-9.]*[a-z][A-Za-z0-9.]*\s*>/, $.mdx_jsx_close_tag),
      alias(/\{[^{}\n]*\}/, $.mdx_jsx_expression),
    ),

    // Inline math ($...$). Excludes $$ (which is math_block).
    math_inline: ($) => prec.dynamic(3, seq(
      alias($._math_inline_open_delimiter, $.math_inline_delimiter),
      alias($._math_inline_content, $.math_inline_content),
      alias($._math_inline_close_delimiter, $.math_inline_delimiter),
    )),
    _math_inline_content: ($) => new RustRegex('[^$\\s\\n\\r](?:[^$\\n\\r]*[^$\\s\\n\\r])?'),

    // Footnote reference [^id]. Distinct from footnote_definition which has
    // `:` immediately after the label. Uses a combined `[^` token to beat
    // the `[` bracket in the inline-content lexer context.
    footnote_reference: ($) => seq(
      alias($._footnote_ref_open, $.footnote_reference_open),
      alias(repeat1(choice(
        $._word,
        punctuation_without($, ['[', ']', '^']),
      )), $.footnote_reference_label),
      ']',
    ),
    _footnote_ref_open: ($) => token(prec(2, '[^')),

    // Inline images. Reuse link_label and link_destination for the internals.
    // A single `image` kind covers all four forms; the trailing reference part
    // is optional so a bare `![alt]` degrades to the shortcut form instead of
    // an ERROR. Resolution of reference labels against
    // link_reference_definitions is a consumer concern — the grammar has no
    // way to know whether a label resolves.
    //
    //   image  ![alt](url "title")   inline/direct
    //          ![alt][label]         full reference (adjacent brackets)
    //          ![alt][]              collapsed reference
    //          ![alt]                shortcut reference
    //
    // The full-reference and collapsed arms get a higher static precedence
    // than the shortcut fallback so that `![alt][label]` collapses into ONE
    // image node instead of a shortcut image followed by a separate link.
    image: ($) => prec.dynamic(PRECEDENCE_LEVEL_IMAGE, prec.right(seq(
      '!',
      $.link_label,
      optional(choice(
        // inline destination
        seq('(', optional($._whitespace), optional($.link_destination),
          optional(seq($._whitespace, $.link_title)),
          optional($._whitespace), ')'),
        // full reference: ![alt][label]
        prec(1, $.link_label),
        // collapsed reference: ![alt][]
        prec(1, seq('[', ']')),
      )),
    ))),

    // Inline links, split into explicit kinds so consumers can distinguish
    // inline, full-reference, collapsed-reference and shortcut forms directly
    // (mirrors upstream split_parser, which exposes them as separate node
    // kinds). Resolution of reference forms against link_reference_definitions
    // is a consumer concern — the grammar has no way to know whether a label
    // resolves — so a bare `[text]` always parses as shortcut_link.
    //
    //   inline_link              [text](url "title")
    //   full_reference_link      [text][label]   (adjacent brackets, no space)
    //   collapsed_reference_link [text][]
    //   shortcut_link            [text]
    //
    // Static precedence on the reference arms (prec(1) vs prec(0)) ensures two
    // adjacent link_labels collapse into ONE full_reference_link instead of two
    // separate shortcut_links.
    inline_link: ($) => prec.dynamic(PRECEDENCE_LEVEL_LINK, seq(
      $.link_label,
      '(', optional($._whitespace), optional($.link_destination),
      optional(seq($._whitespace, $.link_title)),
      optional($._whitespace), ')')),
    full_reference_link: ($) => prec.dynamic(PRECEDENCE_LEVEL_LINK, prec(1, seq(
      $.link_label, $.link_label))),
    collapsed_reference_link: ($) => prec.dynamic(PRECEDENCE_LEVEL_LINK, seq(
      $.link_label, '[', ']')),
    shortcut_link: ($) => prec.dynamic(PRECEDENCE_LEVEL_LINK, prec(0, $.link_label)),

    // Strikethrough (GFM): ~~text~~
    strikethrough: ($) => prec.dynamic(1, seq(
      alias($._strikethrough_delimiter, $.strikethrough_delimiter),
      alias(repeat1($._inline_no_strikethrough), $.strikethrough_content),
      alias($._strikethrough_delimiter, $.strikethrough_delimiter),
    )),
    _strikethrough_delimiter: ($) => token(prec(2, '~~')),
    _inline_no_strikethrough: ($) => choice(
      $._whitespace,
      $.inline_code,
      $.autolink,
      $.html_inline,
      $.mdx_jsx_inline,
      $.math_inline,
      $.image,
      $.footnote_reference,
      $.inline_link,
      $.full_reference_link,
      $.collapsed_reference_link,
      $.shortcut_link,
      $.strong,
      $.emphasis,
      $.numeric_token,
      $.path_like_token,
      $.identifier_like_token,
      $.word_token,
      $.terminator,
      $.separator,
      $.bracket,
      // omit strikethrough itself (no same-delimiter nesting)
      $.operator_like,
    ),

    // Emphasis: *text* or _text_. Strong: **text** or __text__.
    // Implemented as simple paired delimiters. CommonMark's full
    // left/right-flanking rules are not reproduced; intraword `_` and
    // ambiguous cases may parse differently from a spec-strict parser.
    strong: ($) => prec.dynamic(2, choice(
      seq(alias($._strong_star_delim, $.strong_delimiter),
        alias(repeat1($._inline_no_strong), $.strong_content),
        alias($._strong_star_delim, $.strong_delimiter)),
      seq(alias($._strong_under_delim, $.strong_delimiter),
        alias(repeat1($._inline_no_strong), $.strong_content),
        alias($._strong_under_delim, $.strong_delimiter)),
    )),
    _strong_star_delim: ($) => token(prec(3, '**')),
    _strong_under_delim: ($) => token(prec(3, '__')),
    _inline_no_strong: ($) => choice(
      $._whitespace,
      $.inline_code,
      $.autolink,
      $.html_inline,
      $.mdx_jsx_inline,
      $.math_inline,
      $.image,
      $.footnote_reference,
      $.inline_link,
      $.full_reference_link,
      $.collapsed_reference_link,
      $.shortcut_link,
      $.emphasis,
      $.strikethrough,
      $.numeric_token,
      $.path_like_token,
      $.identifier_like_token,
      $.word_token,
      $.terminator,
      $.separator,
      $.bracket,
      $.operator_like,
    ),

    emphasis: ($) => prec.dynamic(1, choice(
      seq(alias($._emphasis_star_delim, $.emphasis_delimiter),
        alias(repeat1($._inline_no_emphasis), $.emphasis_content),
        alias($._emphasis_star_delim, $.emphasis_delimiter)),
      seq(alias($._emphasis_under_delim, $.emphasis_delimiter),
        alias(repeat1($._inline_no_emphasis), $.emphasis_content),
        alias($._emphasis_under_delim, $.emphasis_delimiter)),
    )),
    _emphasis_star_delim: ($) => token(prec(1, '*')),
    _emphasis_under_delim: ($) => token(prec(1, '_')),
    _inline_no_emphasis: ($) => choice(
      $._whitespace,
      $.inline_code,
      $.autolink,
      $.html_inline,
      $.mdx_jsx_inline,
      $.math_inline,
      $.image,
      $.footnote_reference,
      $.inline_link,
      $.full_reference_link,
      $.collapsed_reference_link,
      $.shortcut_link,
      $.strong,
      $.strikethrough,
      $.numeric_token,
      $.path_like_token,
      $.identifier_like_token,
      $.word_token,
      $.terminator,
      $.separator,
      $.bracket,
      $.operator_like,
    ),

    pipe_table: ($) => prec.right(seq(
      $._pipe_table_start,
      alias($.pipe_table_row, $.pipe_table_header),
      $._newline,
      $.pipe_table_delimiter_row,
      repeat(seq($._pipe_table_newline, optional($.pipe_table_row))),
      choice($._newline, $._eof),
    )),

    _pipe_table_newline: ($) => seq(
      $._pipe_table_line_ending,
      optional($.block_continuation),
    ),

    pipe_table_delimiter_row: ($) => seq(
      optional(seq(
        optional($._whitespace),
        '|',
      )),
      repeat1(prec.right(seq(
        optional($._whitespace),
        $.pipe_table_delimiter_cell,
        optional($._whitespace),
        '|',
      ))),
      optional($._whitespace),
      optional(seq(
        $.pipe_table_delimiter_cell,
        optional($._whitespace),
      )),
    ),

    pipe_table_delimiter_cell: ($) => seq(
      optional(alias(':', $.pipe_table_align_left)),
      repeat1('-'),
      optional(alias(':', $.pipe_table_align_right)),
    ),

    pipe_table_row: ($) => seq(
      optional(seq(
        optional($._whitespace),
        '|',
      )),
      choice(
        seq(
          repeat1(prec.right(seq(
            choice(
              seq(
                optional($._whitespace),
                $.pipe_table_cell,
                optional($._whitespace),
              ),
              alias($._whitespace, $.pipe_table_cell),
            ),
            '|',
          ))),
          optional($._whitespace),
          optional(seq(
            $.pipe_table_cell,
            optional($._whitespace),
          )),
        ),
        seq(
          optional($._whitespace),
          $.pipe_table_cell,
          optional($._whitespace),
        ),
      ),
    ),

    pipe_table_cell: ($) => prec.right(seq(
      choice(
        $._word,
        $._backslash_escape,
        punctuation_without($, ['|']),
      ),
      repeat(choice(
        $._word,
        $._whitespace,
        $._backslash_escape,
        punctuation_without($, ['|']),
      )),
    )),
  },

  externals: ($) => [
    $._line_ending,
    $._soft_line_ending,
    $._block_close,
    $.block_continuation,

    $._block_quote_start,
    $._indented_chunk_start,
    $.atx_h1_marker,
    $.atx_h2_marker,
    $.atx_h3_marker,
    $.atx_h4_marker,
    $.atx_h5_marker,
    $.atx_h6_marker,
    $.setext_h1_underline,
    $.setext_h2_underline,
    $._thematic_break,
    $._list_marker_minus,
    $._list_marker_plus,
    $._list_marker_star,
    $._list_marker_parenthesis,
    $._list_marker_dot,
    $._list_marker_minus_dont_interrupt,
    $._list_marker_plus_dont_interrupt,
    $._list_marker_star_dont_interrupt,
    $._list_marker_parenthesis_dont_interrupt,
    $._list_marker_dot_dont_interrupt,
    $.task_list_marker_checked,
    $.task_list_marker_unchecked,
    $._math_inline_open_delimiter,
    $._math_inline_close_delimiter,
    $._fenced_code_block_start_backtick,
    $._fenced_code_block_start_tilde,
    $._blank_line_start,

    $._fenced_code_block_end_backtick,
    $._fenced_code_block_end_tilde,

    $._html_block_1_start,
    $._html_block_1_end,
    $._html_block_2_start,
    $._html_block_3_start,
    $._html_block_4_start,
    $._html_block_5_start,
    $._html_block_6_start,
    $._html_block_7_start,

    $._close_block,
    $._no_indented_chunk,

    $._error,
    $._trigger_error,
    $._eof,

    $.minus_metadata,
    $.plus_metadata,

    $._pipe_table_start,
    $._pipe_table_line_ending,
  ],
  precedences: ($) => [
    [$._setext_heading1, $._block],
    [$._setext_heading2, $._block],
    [$.indented_code_block, $._block],
  ],
  conflicts: ($) => [
    [$.link_reference_definition],
    [$.link_label, $._line],
    [$.link_label, $.footnote_label, $._line],
    [$.footnote_definition],
    [$._footnote_definition_start, $._inline_element],
    [$._footnote_definition_continuation],
    [$.footnote_definition, $._line],
    [$.footnote_definition, $.link_reference_definition, $._line],
    [$.image_block, $._line],
    [$.footnote_label, $._text_inline_no_link],
    // Inline-content conflicts.
    [$.link_label, $.bracket],
    [$.link_label, $._callout_header_paragraph, $.bracket],
    [$.link_label, $._callout_marker_open, $.bracket],
    [$.footnote_label, $.bracket],
    [$.footnote_reference, $.bracket],
    [$.image, $.bracket],
    [$.autolink, $.bracket],
    [$.html_inline, $.bracket],
    [$.mdx_jsx_inline, $.bracket],
    [$.inline_code, $.operator_like],
    [$.math_inline, $.operator_like],
    [$.strong, $.emphasis, $.operator_like],
    [$.strong, $.operator_like],
    [$.emphasis, $.operator_like],
    [$.strikethrough, $.operator_like],
    [$.image_block, $.terminator, $.image],
    [$.footnote_definition, $.link_reference_definition, $._inline_element],
    [$.image_block, $._inline_element],
    [$.terminator, $.image],
    [$.footnote_label, $.footnote_reference],
    [$.footnote_definition, $._inline_element],
    [$.image_block, $.image],
    [$.link_destination, $.link_title],
    [$._link_destination_parenthesis, $.link_title],
    [$.link_reference_definition, $.shortcut_link],
    [$.inline_link, $.shortcut_link],
    [$.link_label, $.footnote_label, $.bracket, $.footnote_reference],
    [$.link_label, $.bracket, $.footnote_reference],
    [$.footnote_label, $._text_inline_no_link, $.footnote_reference],
    [$._text_inline_no_link, $.footnote_reference],
    [$.footnote_reference, $.text_span],
    [$.footnote_reference, $.bracket],
    [$.link_reference_definition, $._inline_element],
  ],
  extras: ($) => [],
});

// Returns a rule that matches all characters that count as punctuation inside
// markdown, besides a list of excluded punctuation characters. Calling this
// function with an empty list as the second argument returns a rule that
// matches all punctuation.
/**
 * @param {GrammarSymbols<string>} $
 * @param {string[]} chars
 */
function punctuation_without($, chars) {
  return seq(choice(...PUNCTUATION_CHARACTERS_ARRAY.filter((c) => !chars.includes(c))), optional($._last_token_punctuation));
}

// Constructs a regex that matches all html entity references. Source list is
// html_entities.json at repo root (kept in sync with
// https://html.spec.whatwg.org/multipage/entities.json).
/**
 * @returns {RegExp}
 */
function html_entity_regex() {
  const entitiesPath = nodeUrl.fileURLToPath(new URL('./html_entities.json', import.meta.url));
  const html_entities = JSON.parse(nodeFs.readFileSync(entitiesPath, 'utf8'));
  let s = '&(';
  s += Object.keys(html_entities).map((name) => name.substring(1, name.length - 1)).join('|');
  s += ');';
  return new RegExp(s);
}

// General-purpose structure for html blocks. Different kinds have different
// opening and closing conditions; the scanner takes care of the distinction
// via the start/end tokens.
/**
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} open
 * @param {RuleOrLiteral} close
 */
function build_html_block($, open, close) {
  return seq(
    open,
    repeat(choice(
      $._line,
      $._newline,
      seq(close, $._close_block),
    )),
    $._block_close,
    optional($.block_continuation),
  );
}
