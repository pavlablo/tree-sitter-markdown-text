# tree-sitter-markdown-text

Markdown grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter), shaped so that its AST lines up with the [textlint `TxtNode`](https://github.com/textlint/textlint/blob/master/docs/txtnode.md) model.

Parses `.md` (and `.markdown`, `.mdown`, `.mkd`, `.mkdn`) files into a concrete syntax tree covering the full CommonMark block structure plus common extensions (GFM pipe tables, task lists, GFM alerts, YAML/TOML front matter, Pandoc math and directive blocks, footnotes, MDX JSX). Inline content is surfaced as structured children of the `inline` wrapper: classified tokens (`word_token`, `numeric_token`, `identifier_like_token`, `path_like_token`) and punctuation-class nodes (`terminator`, `separator`, `bracket`, `operator_like`), plus inline structural nodes (`emphasis`, `strong`, `strikethrough`, `link`, `image`, `autolink`, `inline_code`, `html_inline`, `math_inline`, `mdx_jsx_inline`, `footnote_reference`).

## Why another Markdown grammar?

Existing tree-sitter Markdown grammars are optimized for syntax highlighting and editor tooling: the `inline` wrapper is opaque, every word is one big text run, and prose-level structure is something the consumer is expected to reconstruct with regex. That works for colorizing a file in a code editor, but it falls apart the moment you want to *reason about prose*.

The driver was [mehen](https://github.com/ophidiarium/mehen) &mdash; a CLI that computes documentation-aware metrics on Markdown files in software repositories. mehen needs to:

- count operators and operands per Markdown construct for [Halstead-style metrics](https://github.com/ophidiarium/mehen/blob/main/docs/mehen_markdown_metrics_research_foundation.md#9-markdown-halstead-metrics) (heading markers, list markers, link/image syntax, code-fence delimiters, math delimiters, emphasis &mdash; all distinguishable),
- distinguish `path_like_token` from `identifier_like_token` from plain `word_token` so it can score [repository grounding](https://github.com/ophidiarium/mehen/blob/main/docs/mehen_markdown_metrics_research_foundation.md#15-repository-grounding-score) (does the doc actually anchor to repo files / APIs / versions?),
- classify every link as inline / reference / autolink and resolve fragments &mdash; so it can compute [link debt](https://github.com/ophidiarium/mehen/blob/main/docs/mehen_markdown_metrics_research_foundation.md#11-link-and-reference-metrics) without re-tokenizing,
- detect [diagrams](https://github.com/ophidiarium/mehen/blob/main/docs/mehen_markdown_metrics_research_foundation.md#12-visual-and-diagram-metrics), [embedded code](https://github.com/ophidiarium/mehen/blob/main/docs/mehen_markdown_metrics_research_foundation.md#14-embedded-code-config-logs-and-math), [pipe tables](https://github.com/ophidiarium/mehen/blob/main/docs/mehen_markdown_metrics_research_foundation.md#13-table-metrics), front-matter, raw HTML / MDX as first-class nodes (for [artifact-debt scoring](https://github.com/ophidiarium/mehen/blob/main/docs/mehen_markdown_metrics_research_foundation.md#19-artifact-debt-score), not for highlighting),
- run a [language-aware prose layer](https://github.com/ophidiarium/mehen/blob/main/docs/mehen_markdown_metrics_research_foundation.md#29-language-aware-prose-metric-layer) on top &mdash; readability formulas, lexical diversity, JTF style rules &mdash; which means it has to know which characters are prose vs. code vs. link target vs. front-matter vs. table delimiter.

A grammar that surfaces those distinctions natively is faster, more accurate, and easier to maintain than bolting them onto a syntax-highlighting grammar with post-processing. Other use cases that benefit from the same shape:

- doc-quality linters (extending `textlint` / `vale` / `proselint` with deeper AST queries),
- prose stylometry / readability tooling that needs sentence-level segmentation outside of code fences,
- AI-assisted documentation review &mdash; structural signals (filler density, evidence coverage, repository grounding) work better than authorship classifiers,
- any tool that needs to walk Markdown the way a compiler walks source code, not the way an editor highlights it.

## Fork status and patches on top of upstream

This grammar is a **fork** of [tree-sitter-grammars/tree-sitter-markdown](https://github.com/tree-sitter-grammars/tree-sitter-markdown), specifically derived from the **`split_parser`** branch's block grammar (see [Credits](#credits-and-references)). The block-level parsing, the `inline` wrapper, and the textlint `TxtNode` alignment all come from upstream. On top of that upstream base, this fork adds:

- **Four explicit link kinds.** The single `link` node is split into `inline_link`, `full_reference_link`, `collapsed_reference_link`, and `shortcut_link`, so a consumer can tell link syntax apart without re-tokenizing the source.
- **Structural content inside link labels.** `link_label` children are parsed into structured inline nodes (code spans, emphasis/strong, autolinks, etc.) instead of opaque text.
- **All image reference forms.** Images support the same four forms as links &mdash; `![alt](dest)` inline, `![alt][label]` full reference, `![alt][]` collapsed reference, `![alt]` shortcut reference &mdash; plus a block-level `image_block` for a standalone image paragraph.
- **A reference-style link resolver layer** in [`tools/link-resolver`](tools/link-resolver/) that consumes the parse tree (via ast-grep custom-language scanning) and resolves reference links against `link_reference_definition`s per CommonMark normalization rules. It is a post-processing tool, not part of the grammar.

These additions are surfaced as new kinds in `src/node-types.json`; see the [Node kind reference](#node-kind-reference) below.

## Features

### Block nodes

- **Document structure** &mdash; `document`, nested `section` wrappers around ATX headings, `paragraph`, `blank_line` (as a first-class node).
- **Headings** &mdash; ATX (`#`..`######`) and setext (`===`/`---`) with the heading level exposed as a `level` field on both `atx_heading` and `setext_heading`.
- **Code blocks** &mdash; indented code blocks and fenced code blocks (backtick and tilde), with `info_string`/`language` children for the GFM language tag.
- **Math blocks** &mdash; Pandoc/GitLab/KaTeX display math (`$$…$$`) as a dedicated `math_block` with `math_block_delimiter`/`math_block_content` children.
- **Lists** &mdash; unordered (`+`/`-`/`*`) and ordered (`1.`/`1)`) list markers. GFM task list items are promoted to `task_list_item` (distinct from `list_item`), with `task_list_marker_checked`/`task_list_marker_unchecked` markers.
- **Block quotes and callouts** &mdash; nested quotes and lazy continuations. A block quote whose first paragraph begins with `[!NOTE]` / `[!TIP]` / `[!IMPORTANT]` / `[!WARNING]` / `[!CAUTION]` (or any uppercase-only label) is surfaced as `callout` with a `callout_type` field.
- **Thematic breaks** &mdash; `---`, `***`, `___`.
- **HTML blocks** &mdash; all 7 CommonMark HTML block types; block-level HTML comments are aliased to `html_comment_block` for easy metric extraction.
- **MDX JSX blocks** &mdash; shallow `mdx_jsx_block` for lines that start with an MDX-style JSX element (`<Component ...>`, `<Component/>`, `</Component>`). Component-style mixed-case names disambiguate from all-caps HTML blocks such as `<DIV>`.
- **Pipe tables** &mdash; `pipe_table` with `pipe_table_header`, `pipe_table_delimiter_row`, `pipe_table_row`, `pipe_table_cell`, `pipe_table_align_left`/`pipe_table_align_right`.
- **Link reference definitions** &mdash; `link_reference_definition` with `link_label`/`link_destination`/`link_title` children.
- **Footnote definitions** &mdash; `footnote_definition` (`[^id]: …`) with a `footnote_label` child.
- **Directive blocks** &mdash; generic container directives (`:::name … :::`, per remark-directive / MyST / Pandoc fenced divs) as `directive_block` with `directive_block_delimiter`/`directive_name`/`directive_block_content` children.
- **Image blocks** &mdash; a paragraph consisting of a single block-level image (`![alt](dest)` on its own line) is surfaced as `image_block` with `link_label`/`link_destination` children.
- **Front matter** &mdash; YAML (`---` fenced) as `minus_metadata`, TOML (`+++` fenced) as `plus_metadata`.

### Inline nodes (children of the `inline` wrapper)

- **Classified text tokens** &mdash; `text_span` wraps runs of classified tokens: `word_token` (Unicode alphabetic), `numeric_token` (integers, decimals, versions), `identifier_like_token` (camelCase / PascalCase / snake_case), `path_like_token` (paths with `/` separators or dotted identifiers).
- **Punctuation classes** &mdash; every punctuation lexeme is classified: `terminator` (`.`, `?`, `!`, `。`, `…`), `separator` (`,`, `;`, `:`), `bracket` (`(`, `)`, `[`, `]`, `{`, `}`, `<`, `>`), `operator_like` (`::`, `->`, `=>`, `=`, `+`, `-`, `*`, `/`, `|`, `&`, and other punctuation).
- **Emphasis / strong / strikethrough** &mdash; `emphasis` (`*…*` or `_…_`), `strong` (`**…**` or `__…__`), `strikethrough` (`~~…~~`), each with a `_delimiter`/`_content`/`_delimiter` sub-tree.
- **Code spans** &mdash; `inline_code` with matched backtick-run delimiters (1 or 2 backticks).
- **Links and images** &mdash; `link` (inline, full-reference, collapsed-reference, shortcut-reference forms) and `image` (`![alt](dest)` or `![alt][ref]`). Both expose `link_label`/`link_destination`/`link_title` children. See the [node kind reference](#node-kind-reference) for the exact per-form kinds.
- **Autolinks** &mdash; `autolink` with `uri` or `email` children for `<https://…>` and `<user@example.com>`.
- **Raw HTML inline** &mdash; `html_inline` with `html_open_tag`/`html_close_tag`/`html_comment`/`html_cdata`/`html_declaration`/`html_processing_instruction` children.
- **MDX JSX inline** &mdash; shallow `mdx_jsx_inline` with `mdx_jsx_open_tag`/`mdx_jsx_close_tag`/`mdx_jsx_expression` children.
- **Inline math** &mdash; `math_inline` (`$…$`) with `math_inline_delimiter`/`math_inline_content` children. Disambiguated from `math_block` (`$$…$$`).
- **Footnote references** &mdash; `footnote_reference` (`[^id]` inside prose) with a `footnote_reference_label` child.

- **Injections query** &mdash; ships a `queries/injections.scm` that injects into fenced-code-block info strings, HTML blocks, and front matter.

## Example

```markdown
# Heading

A paragraph with inline content.

- one
- two

```go
func main() {}
```
```

Parsed tree (abbreviated):

```
(document
  (section
    (atx_heading level: (atx_h1_marker) heading_content: (inline))
    (blank_line)
    (paragraph (inline))
    (blank_line)
    (list
      (list_item (list_marker_minus) (paragraph (inline)))
      (list_item (list_marker_minus) (paragraph (inline))))
    (blank_line)
    (fenced_code_block
      (fenced_code_block_delimiter)
      (info_string (language))
      (code_fence_content)
      (fenced_code_block_delimiter))))
```

## Relationship to textlint

The grammar is structurally close to the textlint AST. Every block-level `TxtNode` type has a direct counterpart here; inline `TxtNode` types (`Str`, `Emphasis`, `Strong`, `Link`, `Image`, `Code`, `Html`, `Delete`, `FootnoteReference`) also have direct counterparts as children of the `inline` wrapper. Names stay snake_case per the tree-sitter convention; consumers map names themselves. See [docs/textlint-mapping.md](docs/textlint-mapping.md) for the full table.

## Installation

### Installing as a git dependency (recommended for consumer projects)

The most common way to consume this grammar from another project is as an npm
git dependency. Point at the fork and **pin a specific commit SHA or tag**:

```sh
npm install git+https://github.com/pavlablo/tree-sitter-markdown-text.git#v1.0.0
```

or, equivalently, pin by full commit SHA:

```sh
npm install git+https://github.com/pavlablo/tree-sitter-markdown-text.git#5fbc3b567e1631f629b4f8c61cac90eeec22315a
```

**Why pin a SHA or tag instead of `#main`?**

- **Reproducible builds.** A floating ref like `#main` resolves to whatever is
  on the branch the day the dependency is (re)installed. Two developers &mdash;
  or one CI run and a local machine &mdash; can silently end up with different
  parser versions locked in different `package-lock.json` snapshots, and a fresh
  `npm install` on a clean checkout can pull a newer, behaviorally different
  grammar than the one the code was written against.
- **Protection from future upstream changes.** This is a fork that will keep
  evolving. Pinning a SHA or a tag makes the grammar a *fixed contract*: the AST
  kinds and fields your rules depend on will not change under you. `#main` gives
  you none of that guarantee.
- **A tag is as immutable as a SHA but readable.** `v1.0.0` is easy to grep in
  lockfiles and to reason about in PRs, yet it still resolves to one exact
  commit. Treat tags published here as "do not move" contracts. (`git tag -f`
  can re-point a tag, so a tag is only as stable as its maintainer's discipline
  &mdash; a full commit SHA is the strongest possible guarantee.)

The `install` script of the package compiles the native Node binding via
`node-gyp-build`, and the npm tarball includes `src/` (the C parser source), so
a working C toolchain is all that is required beyond npm itself.

### npm

```sh
npm install tree-sitter-markdown-text
```

### Cargo

```sh
cargo add tree-sitter-markdown-text
```

### PyPI

```sh
pip install tree-sitter-markdown-text
```

### Go

```go
import tree_sitter_markdown_text "github.com/ophidiarium/tree-sitter-markdown-text/bindings/go"
```

The root package also exports the bundled queries via `go:embed`:

```go
import markdown "github.com/ophidiarium/tree-sitter-markdown-text"

lang := markdown.GetLanguage()
query, _ := markdown.GetHighlightsQuery()
```

> The `Go` and registry (npm/Cargo/PyPI) examples above are the *published
> bindings* of the upstream package. If you consume the fork directly, prefer
> the git-dependency form from the previous section and build the shared
> library yourself as described below.

## Building the shared library (`markdown-text.so`) from source

Any consumer that loads the grammar as a dynamically-linked library (notably
[ast-grep custom languages](#using-with-ast-grep)) needs a `.so`/`.dylib`/`.dll`
built from the parser sources. The npm git dependency ships `src/parser.c`,
`src/scanner.c`, and the `src/tree_sitter/` headers, so the only external
requirement is a C compiler.

This repository ships ready-made build scripts. Two options:

**Option 1 — the repository's own `Makefile`** (works on Linux/macOS):

```sh
make            # produces libtree-sitter-markdown-text.so (and .a, .pc)
make clean
```

**Option 2 — the tree-sitter CLI** (also produces the grammar's language symbol):

```sh
tree-sitter build --output markdown-text.so
```

**Minimal universal fallback** (no Makefile, no tree-sitter CLI &mdash; just a
C compiler, e.g. `gcc`/`cc`/`clang`):

```sh
cc -shared -fPIC -O2 -Isrc -o markdown-text.so src/parser.c src/scanner.c
```

All three routes export the grammar's language entry point as
`tree_sitter_markdown` (the grammar name is `markdown`, per `tree-sitter.json`).

## Using with ast-grep

### Registering as a custom language in `sgconfig.yml`

Once the `.so` is built (see above), register it in the `customLanguages`
section of any ast-grep project's `sgconfig.yml`. Minimal working example:

```yaml
# sgconfig.yml
ruleDirs:
  - ./rules
customLanguages:
  markdown:
    libraryPath: ./vendor/markdown-text.so   # relative to sgconfig.yml, or absolute
    extensions: [md, markdown, mdown, mkd, mkdn]
    expandoChar: _                           # optional: replaces '$' in patterns
```

The custom-language key (`markdown` above) is used to look up the loader symbol.
By default ast-grep loads `tree_sitter_<name>`, so `markdown` matches the
`tree_sitter_markdown` symbol this grammar exports. If you prefer a different
key (e.g. to avoid clashing with ast-grep's built-in Markdown), set
`languageSymbol` explicitly:

```yaml
customLanguages:
  markdownText:
    libraryPath: ./vendor/markdown-text.so
    extensions: [md]
    languageSymbol: tree_sitter_markdown
```

Then rules can target the grammar's kinds, e.g.:

```yaml
id: no-broken-shortcut-links
language: markdown
severity: warning
rule:
  kind: shortcut_link
```

See the official [ast-grep custom-language
guide](https://ast-grep.github.io/advanced/custom-language.html) for the full
option list (`outlineRules`, per-platform `libraryPath` maps, etc.).

### Node kind reference

Exact kinds this grammar produces, taken verbatim from `src/node-types.json`.
Use them as `kind:` selectors in ast-grep rules (or as node names in any
tree-sitter consumer). Anonymous punctuation/lexemes (`*`, `**`, `` ` ``, `~~`,
`[`, `]`, …) are omitted here; the full list lives in `src/node-types.json`.

**Links** — the four explicit reference kinds (this fork's addition), plus
autolinks and reference definitions:

| kind | meaning | children |
|---|---|---|
| `inline_link` | `[text](dest "title")` | `link_label`, `link_destination`, `link_title` |
| `full_reference_link` | `[text][label]` | `link_label` |
| `collapsed_reference_link` | `[text][]` | `link_label` |
| `shortcut_link` | `[label]` (bare) | `link_label` |
| `autolink` | `<https://…>` / `<user@…>` | `uri`, `email` |
| `link_reference_definition` | `[label]: dest "title"` | `link_label`, `link_destination`, `link_title` |

**Images** — all four reference forms collapse into one `image` node, plus the
block-level form:

| kind | meaning | children |
|---|---|---|
| `image` | `![alt](dest)`, `![alt][label]`, `![alt][]`, `![alt]` | `link_label`, `link_destination`, `link_title` |
| `image_block` | standalone block-level image paragraph | `link_label`, `link_destination` |

**Code blocks / code spans:**

| kind | meaning | children |
|---|---|---|
| `fenced_code_block` | ```` ```lang … ``` ```` / `~~~` | `fenced_code_block_delimiter`, `info_string`, `code_fence_content`, `block_continuation` |
| `indented_code_block` | 4-space-indented block | `blank_line`, `block_continuation` |
| `inline_code` | `` `code` `` / ` ``code`` ` | `inline_code_delimiter`, `inline_code_content` |
| `info_string` / `language` | fence language tag | &mdash; |

**Shared link part nodes:**

| kind | meaning |
|---|---|
| `link_label` | the `[...]` label text; **children are parsed structurally** (this fork's addition): `text_span`, `emphasis`, `strong`, `strikethrough`, `inline_code`, `autolink`, `html_inline`, `math_inline`, `mdx_jsx_inline`, `backslash_escape`, `entity_reference`, `numeric_character_reference` |
| `link_destination` | the destination (`(dest)` or `label` target) |
| `link_title` | the optional `"title"` |

**Other kinds** (block & inline) you may want as selectors: `document`,
`section`, `paragraph`, `blank_line`, `atx_heading`/`setext_heading` (with
`level:` field), `block_quote`, `callout` (+ `callout_type`), `list`,
`list_item`, `task_list_item`, `thematic_break`, `pipe_table` (+
`pipe_table_header`/`pipe_table_row`/`pipe_table_cell`), `html_block`,
`html_comment_block`, `mdx_jsx_block`, `directive_block`, `footnote_definition`,
`footnote_reference`, `minus_metadata`/`plus_metadata`, `math_block`/
`math_inline`, `inline` (the structured wrapper: `text_span`, `word_token`,
`numeric_token`, `identifier_like_token`, `path_like_token`, `terminator`,
`separator`, `bracket`, `operator_like`, `emphasis`, `strong`,
`strikethrough`, `inline_code`, `html_inline`, `mdx_jsx_inline`,
`footnote_reference`).

## Usage

### Node.js

```javascript
import Parser from "tree-sitter";
import Markdown from "tree-sitter-markdown-text";

const parser = new Parser();
parser.setLanguage(Markdown);

const tree = parser.parse("# hello\n");
console.log(tree.rootNode.toString());
```

### Rust

```rust
let mut parser = tree_sitter::Parser::new();
let language = tree_sitter_markdown_text::LANGUAGE;
parser.set_language(&language.into()).unwrap();

let tree = parser.parse("# hello\n", None).unwrap();
println!("{}", tree.root_node().to_sexp());
```

### Python

```python
from tree_sitter import Language, Parser
import tree_sitter_markdown_text

parser = Parser(Language(tree_sitter_markdown_text.language()))
tree = parser.parse(b"# hello\n")
print(tree.root_node.sexp())
```

## Credits and references

- [tree-sitter-grammars/tree-sitter-markdown](https://github.com/tree-sitter-grammars/tree-sitter-markdown) &mdash; upstream grammar, specifically the `split_parser` branch's block grammar, which this grammar is derived from.
- [textlint TxtNode](https://github.com/textlint/textlint/blob/master/docs/txtnode.md) &mdash; the AST shape this grammar targets for compatibility.
- [CommonMark Spec](https://spec.commonmark.org/) &mdash; the block structure this grammar implements.
- [Github Flavored Markdown](https://github.github.com/gfm/) &mdash; for the pipe-table and task-list extensions.

## License

[MIT](LICENSE)
