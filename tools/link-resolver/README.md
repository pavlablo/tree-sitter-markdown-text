# link-resolver

A reference-style link RESOLVER layer **on top of** the `tree-sitter-markdown-text`
parse tree. It does not modify the grammar; it consumes the tree via
[ast-grep](https://ast-grep.github.io/) custom-language scanning and resolves
reference-style links against `link_reference_definition`s per
[CommonMark 0.30 §6.2](https://spec.commonmark.org/0.30/#link-reference-definitions)
normalization rules.

## Why Variant 2 (pure-YAML can't do it)

Pure ast-grep YAML has no cross-node text-equality constraint (`constraints:`
does not exist in 0.45), so matching a link label to its definition cannot be
expressed declaratively. Instead we scan for definitions and links with simple
kind-selector rules, then do the label normalization / lookup in a small Node
postprocessor (`resolver.mjs`).

## Implementation note (ast-grep 0.45 quirk)

ast-grep's `pattern:` field compiles by running the target grammar over the
pattern string. For a document grammar like this one, `(shortcut_link $LABEL)`
is just inline markdown text, so `pattern:` produces a literal-text query and
never matches nodes. The rules therefore use `kind:` (ESQuery) selectors
instead — e.g. `full_reference_link > link_label:nth-child(2)` — which match
real nodes and expose their text (a `link_label` node text includes the
brackets, `[label]`). There are no `metaVariables` in this mode; the resolver
reads label/url/title from the matched node text.

## Build (once)

Build the shared library from the repo `src/` into this directory:

```bash
bash tools/link-resolver/build.sh
```

Produces `tools/link-resolver/libtree-sitter-markdown.so` (git-ignored).

## Usage

```bash
node tools/link-resolver/resolver.mjs <file.md>...
```

Prints a JSON array of per-link results to stdout:
`{ file, kind, label, normalizedLabel, resolved, url?, title?, start, end, text }`.

## Tests

```bash
node --test tools/link-resolver/test/resolver.test.mjs
```
