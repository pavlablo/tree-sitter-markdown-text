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

## Cross-block (worst severity) — ELIMINATED (2026-09-14)

The only cross-block `ERROR` class (B1) is now **contained** via scanner guards: it degrades to
literal text instead of swallowing the document. **No cross-block issues remain.**

### B1 — `***a** b*` / `___a__ b_` (delimiter run-splitting) — CONTAINED
- **Symptom (was):** `***a** b*` (and `___a__ b_`) produced an `ERROR` that grew to cover
  **the entire remainder of the document** — the parser never re-synced to a clean block.
- **Severity (was):** **cross-block** — swallowed headings, paragraphs, lists, fences.
- **Status:** ✅ **Contained.** Scanner guards (`parse_star` + `parse_underscore`, via the shared
  `run_lookahead` helper) refuse a phantom strong-open for the mixed-run shapes, so they degrade
  to literal text with clean following blocks — 0 `ERROR`. Commits `7062b98` (star) +
  `47a44ec` (underscore); regression guard SAFE-TO-TAG; corpus 161/161; `node-types.json`
  unchanged; ABI 14. Tracked in [issue #4](https://github.com/pavlablo/tree-sitter-markdown-text/issues/4)
  (ERROR class closed).
- **Remaining (spec-correct, NOT contained):** the CommonMark-correct tree (`***foo** bar*` →
  `em(strong(foo) bar)`; `***both***` → `em(strong(both))`) requires delimiter run-splitting,
  blocked by the forward-only `mark_end` constraint in the external scanner. Tracked in
  [issue #7](https://github.com/pavlablo/tree-sitter-markdown-text/issues/7).
- **Frequency note:** 0 occurrences observed in the 1395-file `rea-skills` corpus (2026-09-13),
  but the trigger is ordinary prose, so this was a latent landmine rather than a present-in-the-wild hit.

## Fixed (2026-09-14, branch `fix/emphasis-block-boundary`)

### D1 — single-line display math `$$math$$` — FIXED
- **Symptom (was):** `$$math$$` → `ERROR` on the opening `$$`. Contained.
- **Fix:** `math_block` gained a single-line alternative (`$$ content $$` on one line);
  multi-line form, `$$$`-exclusion and mid-paragraph `$$`→`operator_like` unchanged.
- **Status:** ✅ **Fixed.** [issue #1](https://github.com/pavlablo/tree-sitter-markdown-text/issues/1) closed.
- **Frequency note:** 0 occurrences in the 1395-file corpus (2026-09-13).

### D3 — multi-line inline code span (backtick across newline) — FIXED
- **Symptom (was):** `` `a\nb` `` → `ERROR`. CommonMark-valid input (§6.3). Contained.
- **Fix:** `inline_code` content now accepts `_soft_line_break` (line ending normalized, per
  §6.3); a blank line inside the span still correctly prevents it from forming.
- **Status:** ✅ **Fixed.** [issue #3](https://github.com/pavlablo/tree-sitter-markdown-text/issues/3) closed.
- **Frequency note:** 0 occurrences in the 1395-file corpus (2026-09-13).

### D2 — unescaped `|` inside link/footnote labels in tables — FIXED
- **Symptom (was):** `| [x | y] | z |` → wrong structure (2 table cells merged instead of 3),
  valid nodes, no `ERROR`.
- **Fix:** table-aware `_pipe_table_*` label rules (grammar-level) exclude unescaped `|` from
  label content, so an unescaped `|` inside `[...]`/`![...]`/`[^...]` now splits the cell per
  GFM §4.10 (3 cells, no fake link across the pipe); escaped `\|` stays literal content.
- **Status:** ✅ **Fixed.** Closes the grammar-level follow-up of
  [issue #2](https://github.com/pavlablo/tree-sitter-markdown-text/issues/2).

### Embedded backtick-run ≥3 inside fenced code — FIXED (was mischaracterized)
- **Symptom (actual):** a fenced code block whose content contains a backtick/tilde run ≥ the
  opening fence length at the **end of a content line** (or with 4+ columns of indent) was
  wrongly treated as a closing fence (premature close; phantom unclosed fence swallowed
  following content). Not an `ERROR` — silent wrong-structure. The previously-documented
  `ERROR` example (`sub("^```")`) already parsed cleanly before this fix.
- **Fix (scanner):** `fence_end` in `parse_backtick`/`parse_tilde` now requires the closing
  run to be at line start (`lexer->get_column` ≤ `MAX_NON_CODE_INDENT`), per CommonMark §4.5.
- **Status:** ✅ **Fixed.** Inherited-from-upstream defect (attribution in the fix commit).
- **Frequency note:** the ~10 of 1395 `rea-skills` files (2026-09-13) that hit this class now
  parse correctly.

## Remaining

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
