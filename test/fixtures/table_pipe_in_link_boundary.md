# Pipe-table link/footnote delimiter boundaries must not be crossed by `|`

An unescaped `|` is a cell separator even inside link/image/footnote labels
(GFM §4.10 tables). A `]`-closer search (footnote reference `[^...]`) must not
cross an unescaped `|` inside a pipe table, otherwise a `[^` opener is emitted
that cannot close within its cell and produces an ERROR. The repro cases below
must all parse with zero ERROR/MISSING.

## Shortcut link label containing a pipe

| a | b |
|---|---|
| [x | y] | z |

## Inline link label containing a pipe

| a | b |
|---|---|
| [x | y](/u) | z |

## Image label containing a pipe

| a | b |
|---|---|
| ![alt | x](/u) | z |

## Footnote reference label containing a pipe (the scanner `]`-guard)

| a | b |
|---|---|
| [^x | y] | z |

## Pipe inside a link label in a paragraph stays a single link

[a|b](/u)
