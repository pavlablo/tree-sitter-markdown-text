#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
REPO="${REPO:-$(cd ../../ && pwd)}"
cc -shared -fPIC -O2 -std=c11 -I "$REPO/src" -o libtree-sitter-markdown.so "$REPO/src/parser.c" "$REPO/src/scanner.c"
nm -D libtree-sitter-markdown.so | grep -q ' tree_sitter_markdown$' || { echo "ERROR: symbol missing"; exit 1; }
echo "built $PWD/libtree-sitter-markdown.so"
