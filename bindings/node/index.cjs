"use strict";

const { readFileSync } = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..", "..");

const binding = typeof process.versions.bun === "string"
  ? require(path.join(root, "prebuilds", `${process.platform}-${process.arch}`, "tree-sitter-markdown-text.node"))
  : require("node-gyp-build")(root);

try {
  binding.nodeTypeInfo = require(path.join(root, "src", "node-types.json"));
} catch { }

const queries = [
  ["HIGHLIGHTS_QUERY", "queries/highlights.scm"],
  ["INJECTIONS_QUERY", "queries/injections.scm"],
  ["LOCALS_QUERY", "queries/locals.scm"],
  ["TAGS_QUERY", "queries/tags.scm"],
];

for (const [prop, rel] of queries) {
  Object.defineProperty(binding, prop, {
    configurable: true,
    enumerable: true,
    get() {
      delete binding[prop];
      try {
        binding[prop] = readFileSync(path.join(root, rel), "utf8");
      } catch { }
      return binding[prop];
    }
  });
}

module.exports = binding;
