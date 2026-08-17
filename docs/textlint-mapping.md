# textlint TxtNode ↔ tree-sitter-markdown-text mapping

This grammar's node types are close to — but not byte-identical with — the textlint [TxtNode](https://github.com/textlint/textlint/blob/master/docs/txtnode.md) AST. The mapping below uses textlint's official remark/mdast → TxtNode table from [`markdown-syntax-map.ts`](https://github.com/textlint/textlint/blob/master/packages/%40textlint/markdown-to-ast/src/mapping/markdown-syntax-map.ts) as the source of truth.

## Block-level nodes

| textlint TxtNode | mdast key | `tree-sitter-markdown-text` node | Notes |
|---|---|---|---|
| `Document` | `root` | `document` | Root node. |
| `Paragraph` | `paragraph` | `paragraph` | Children are a single `inline` wrapper with structured inline nodes inside. |
| `BlockQuote` | `blockquote` | `block_quote` / `callout` | Plain block quotes stay as `block_quote`. Block quotes whose first paragraph begins with `[!WORD]` are surfaced as `callout` with a `callout_type` field. |
| `List` | `list` | `list` | Ordered vs unordered distinguishable via the `list_marker_{dot,minus,plus,star,parenthesis}` child of each item. |
| `ListItem` | `listItem` | `list_item` / `task_list_item` | Task list items are a distinct `task_list_item` node carrying `task_list_marker_checked` / `task_list_marker_unchecked`. Plain items stay as `list_item`. |
| `Header` (+ `depth`) | `heading` | `atx_heading` / `setext_heading` | Both expose the marker/underline via a `level` field (`atx_h1_marker`..`atx_h6_marker`, `setext_h1_underline`/`setext_h2_underline`). |
| `CodeBlock` (+ `lang`) | `code` | `fenced_code_block` / `indented_code_block` | Language from `fenced_code_block → info_string → language`. |
| `HtmlBlock` | `HtmlBlock` | `html_block` | HTML comment blocks are additionally aliased to `html_comment_block`. |
| *(no direct counterpart)* | *(no direct counterpart)* | `mdx_jsx_block` | MDX-style JSX element on its own line (`<Component …>`, `<Component/>`, `</Component>`). Shallow; full JSX semantics are out of scope. |
| `HorizontalRule` | `thematicBreak` | `thematic_break` | |
| `Yaml` | `yaml` | `minus_metadata` | `---` fenced front matter. |
| `Toml` | `toml` | `plus_metadata` | `+++` fenced front matter. |
| `Table` | `table` | `pipe_table` | GFM pipe tables only. |
| `TableRow` | `tableRow` | `pipe_table_row` / `pipe_table_header` | First row surfaced as a separate `pipe_table_header`. |
| `TableCell` | `tableCell` | `pipe_table_cell` | Cell content is raw text (not split into classified inline tokens). |
| `Definition` / `ReferenceDef` | `definition` | `link_reference_definition` | With `link_label` / `link_destination` / `link_title` children. |
| `FootnoteDefinition` | `footnoteDefinition` | `footnote_definition` | With `footnote_label` child and an `inline` content node. |
| *(no direct counterpart)* | *(no direct counterpart)* | `math_block` | Pandoc / GitLab / KaTeX display math `$$ … $$`. |
| *(no direct counterpart)* | *(no direct counterpart)* | `directive_block` | Generic container directives `:::name … :::` (remark-directive / MyST / Pandoc fenced divs). |
| `Image` when block-level | `image` at root scope | `image_block` | A paragraph whose entire content is a single image `![alt](dest)` is surfaced as `image_block`. Inline images embedded in prose become `image` children of the `inline` wrapper. |

## Inline-level nodes (children of the `inline` wrapper)

| textlint TxtNode | mdast key | `tree-sitter-markdown-text` node | Notes |
|---|---|---|---|
| `Str` | `text` | `word_token` / `numeric_token` / `identifier_like_token` / `path_like_token`, grouped by `text_span` | Text content is classified into tokens. `word_token` is Unicode alphabetic, `numeric_token` handles integers, decimals and version strings, `identifier_like_token` catches camelCase, PascalCase and snake_case identifiers, `path_like_token` catches slash-paths and dotted identifiers. A `text_span` groups consecutive classified tokens with no intervening structural node. |
| *(no direct counterpart)* | *(no direct counterpart)* | `terminator` / `separator` / `bracket` / `operator_like` | Every punctuation lexeme is emitted as a classified node per §3.3 of the research doc. |
| `Emphasis` | `emphasis` | `emphasis` | `*…*` or `_…_`. Simple paired delimiters (not full CommonMark flanking). |
| `Strong` | `strong` | `strong` | `**…**` or `__…__`. |
| `Delete` | `delete` | `strikethrough` | GFM `~~…~~`. |
| `Code` (`inlineCode`) | `inlineCode` | `inline_code` | Single or double backtick runs. |
| `Link` | `link` | `inline_link` / `full_reference_link` / `collapsed_reference_link` / `shortcut_link` | Inline, full-reference, collapsed-reference, and shortcut-reference forms, exposed as four distinct node kinds. |
| `LinkReference` | `linkReference` | `full_reference_link` / `collapsed_reference_link` / `shortcut_link` | Reference forms are distinct kinds; whether a label resolves against a `link_reference_definition` is decided by consumers, not the grammar. |
| `Image` | `image` | `image` | Inline images inside prose (distinct from `image_block`). |
| `ImageReference` | `imageReference` | `image` | |
| *(autolink)* | *(autolink)* | `autolink` | `<…>` with `uri` or `email` child. |
| `Html` | `html` | `html_inline` | Raw HTML open/close/self-closing tags, comments, CDATA, declarations, processing instructions. |
| *(no direct counterpart)* | *(no direct counterpart)* | `mdx_jsx_inline` | MDX JSX tags and `{expression}` spans. |
| *(no direct counterpart)* | *(no direct counterpart)* | `math_inline` | `$…$` single-dollar inline math (distinct from `math_block`). |
| `FootnoteReference` | `footnoteReference` | `footnote_reference` | `[^id]` inside prose. |
| `Break` | `break` | — | Hard line breaks are not split out. Two-space-newline and backslash-newline are absorbed into the surrounding `inline`. |

## Known simplifications

This grammar is a single-pass markdown parser and does not reproduce every corner of the CommonMark / GFM spec. The following simplifications are intentional:

- **Emphasis flanking.** CommonMark's left/right-flanking and intraword-emphasis rules are not fully implemented. `*foo*` and `**foo**` are recognised as paired delimiters; unbalanced or intraword `_` may be classified as `operator_like` instead of forming emphasis.
- **Link precedence over emphasis.** CommonMark says links are processed before emphasis. This grammar follows the same priority at parse time but does not simulate the full delimiter-stack algorithm.
- **Inline code backtick runs.** Backtick runs longer than two (```` ``` ````, etc.) are not matched as inline code delimiters at the inline level. Fenced code blocks handle longer runs at the block level.
- **Pipe-table cell content** is not reclassified into inline tokens; cell text remains raw.
- **MDX JSX** is matched shallowly. Attribute expressions, nested JSX, and JS expressions inside braces are captured as opaque text inside the surrounding `mdx_jsx_open_tag` / `mdx_jsx_close_tag` / `mdx_jsx_expression` node. A full MDX parser is out of scope.
- **HTML entities** inside inline content are handled via the existing `entity_reference` / `numeric_character_reference` rules but are not always reclassified as distinct inline nodes in every context.

## Extra nodes (no textlint counterpart)

- `section` — wrapper that groups an ATX heading with the blocks that follow it up to the next heading of equal/higher rank. Useful for document outline queries; harmless to ignore.
- `blank_line` — empty line, exposed publicly so file-level metrics can count BLANK LOC directly.
- `block_continuation` — zero-width marker used by the external scanner when a container block continues on a new line. Ignore for AST purposes.
- `html_comment_block` — alias around CommonMark HTML block type 2 inside `html_block`. Handy for Markdown-CLOC metrics.
- `text_span` — prose-token wrapper so consumers can target uninterrupted runs of classified tokens with a single query.
