# Known Limitations

This document lists the current, non-exhaustive set of parser limitations. It is the
authoritative companion to the [README "Runaway-emphasis fix" section](README.md#runaway-emphasis-fix),
which intentionally describes only the deliberate, spec-correct *safe-degradation* subset.

Severity terms used here:

- **Cross-block** — the malformed state propagates past the construct's own
  paragraph/block into subsequent, otherwise-valid content (can corrupt the whole file).
- **Contained** — produces an `ERROR`/`MISSING` node (or wrong structure) but does not
  affect sibling or following content; the parser re-syncs cleanly at the next block.

Status terms:

- **Not fixed** — known, reproducible, no fix merged.
- **Tracking pending** — a new issue has not yet been filed for this class.

---

## Cross-block (worst severity)

### B1 — `***a** b*` (delimiter run-splitting) — NOT FIXED
- **Symptom:** `***a** b*` produces an `ERROR` node. With a following blank line + heading,
  the `ERROR` grows to cover **the entire remainder of the document** — the parser never
  re-syncs to a clean block.
- **Severity:** **cross-block** — swallows following content (headings, paragraphs).
- **Status:** ❌ **Not fixed.** Tracked in [issue #4](https://github.com/pavlablo/tree-sitter-markdown-text/issues/4).
  A containment fix (grammar-level error recovery, bounding the `ERROR` to the block instead
  of the whole file) is in progress on branch `fix/grammar-error-recovery`. A full
  CommonMark-correct fix requires delimiter run-splitting.
- **Frequency note:** 0 occurrences observed in the 1395-file `rea-skills` corpus (2026-09-13),
  but the trigger is ordinary prose, so this is a latent landmine rather than a present-in-the-wild hit.

## Contained ERRORs

### D1 — single-line display math `$$math$$` — NOT FIXED
- **Symptom:** `$$math$$` → `ERROR` on the opening `$$`. Contained; a following heading
  parses cleanly.
- **Status:** ❌ **Not fixed.** Tracked in [issue #1](https://github.com/pavlablo/tree-sitter-markdown-text/issues/1).
- **Frequency note:** 0 occurrences in the 1395-file corpus (2026-09-13).

### D3 — multi-line inline code span (backtick across newline) — NOT FIXED
- **Symptom:** `` `a\nb` `` → `ERROR`. CommonMark-valid input (§6.3). Contained.
- **Status:** ❌ **Not fixed.** Tracked in [issue #3](https://github.com/pavlablo/tree-sitter-markdown-text/issues/3).
- **Frequency note:** 0 occurrences in the 1395-file corpus (2026-09-13).

### D2 — unescaped `|` inside link/footnote labels in tables — PARTIALLY FIXED
- **Symptom:** `| [x | y] | z |` → wrong structure (2 table cells merged instead of 3),
  valid nodes, no `ERROR`.
- **Status:** The footnote-`ERROR` case was fixed by the C3 scanner change. The remaining
  link/image wrong-structure (2 cells instead of GFM's 3) is deferred. Tracked in
  [issue #2](https://github.com/pavlablo/tree-sitter-markdown-text/issues/2) (closed; the
  deferred grammar-level follow-up is separate).

### Embedded backtick-run ≥3 inside fenced code — NOT FIXED
- **Symptom:** a fenced code block whose content contains a backtick run ≥3 embedded in a
  line (e.g. a `jq`/`awk`/shell snippet like `sub("^```")`) produces an `ERROR` covering the
  fence region. Contained; the following paragraph parses cleanly.
- **Status:** ❌ **Not fixed.** **Tracking pending** — a new issue has not yet been filed.
- **Frequency note:** hits ~10 of 1395 `rea-skills` files (2026-09-13).

### Wide / machine-generated pipe tables — NOT FIXED
- **Symptom:** long, machine-generated pipe tables (e.g. a `shellcheck` report rendered as a
  markdown table) produce many small, row-local `ERROR` nodes — one per affected row at a
  consistent column. Contained; the parser recovers between rows, so it is not a whole-file
  runaway, but the file is effectively error-flagged.
- **Status:** ❌ **Not fixed.** **Tracking pending** — a new issue has not yet been filed.
- **Frequency note:** e.g. `shellcheck-report.md` in the `rea-skills` corpus → 416 row-local
  `ERROR`s (2026-09-13).

---

## Safe-degradation (deliberate, spec-correct) — not duplicated here

The A1–A7 category degrades safely to literal text and never produces an `ERROR` or swallows
following content. These are documented in the [README "Runaway-emphasis fix"
section](README.md#runaway-emphasis-fix) and are **not** repeated here.

## 4-backtick inline code — safe degradation

An inline code span opened with a 4-backtick delimiter (```` ````lang ```` ````) degrades to
literal text (no `ERROR`). The grammar supports 1- and 2-backtick delimiters. Documented in
[docs/textlint-mapping.md](docs/textlint-mapping.md).
