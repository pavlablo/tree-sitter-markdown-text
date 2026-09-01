# Inline complexity fixture

This fixture exercises the structured-inline edge cases that the corpus
tests cover individually, as one real-content document. The `parse-fixtures`
CI job requires every file under `test/fixtures/` to parse with **zero**
`ERROR` / `MISSING` nodes, so this file is a regression gate for the inline
fixes (multiline emphasis, delimiter run-splitting, nested label brackets,
table cell inline content, and block-boundary stopping).

## Multiline emphasis

*This emphasis spans
multiple soft-wrapped lines and stays one node.*

## Delimiter run-splitting

**a*** and **a**** and **a**b** all stay safe.

## Triple-star degrades safely

***both*** never produces an ERROR; it degrades to strong plus literal stars.

## Nested brackets inside a link label

A link with [nested brackets] in the label resolves to a single link:
[link with [nested] text](https://example.com).

## Inline links and emphasis inside table cells

| Name | Value |
|------|-------|
| [docs](https://example.com) | **bold** |
| plain cell | *italic* |

## Autolink mid-line

See https://example.com and <https://example.com> inline.

## Setext underline and ordered list after unclosed delimiters

*an unclosed star

1. ordered item
2. another

=== a setext underline

A final paragraph with a trailing `code` span.
