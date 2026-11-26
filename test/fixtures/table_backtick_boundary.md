# Pipe-table inline-code backtick runs must not cross cell boundaries

An unclosed backtick run in one cell must not close against a backtick in a
neighbouring cell across the `|` separator (GFM tables + CommonMark §6.3).

## Single backtick run, opener in one cell, closer in the next

| a | b |
|---|---|
| `code | `more |

## Double backtick run across the boundary

| a | b |
|---|---|
| ``code | ``more |

## Triple backtick run in a cell takes the fenced-code path (not inline)

| a | b |
|---|---|
| ``` ``` | x |

## Backtick run fully inside one cell closes normally

| a | b |
|---|---|
| `code` | x |

## Backtick run adjacent to the `|` character

| a | b |
|---|---|
| `code| | x |

## Escaped pipe inside a code span stays one cell (GFM)

| a | b |
|---|---|
| `code\|code` | x |
