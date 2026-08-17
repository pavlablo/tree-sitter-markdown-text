import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const RESOLVER_DIR = path.dirname(fileURLToPath(import.meta.url));
const CONFIG_FILE = path.join(RESOLVER_DIR, 'sgconfig.yml');

// Link kinds produced by our rules (ruleId -> kind). Definitions are handled
// separately before links.
const LINK_RULES = {
  shortcut: 'shortcut',
  fullref: 'fullref',
  collapsed: 'collapsed',
  image: 'image',
};
const DEFINITION_RULES = new Set(['defs', 'defs-titled']);

// ---------------------------------------------------------------------------
// Normalization (CommonMark 0.30 §6.2 / commonmark.js normalizeReference):
//   1. strip surrounding brackets
//   2. trim
//   3. collapse space/tab/CR/LF into single space (NOT \s — \s also eats \v\f)
//   4. Unicode case fold approximation: toLowerCase().toUpperCase()
//   Empty result => invalid label.
// ---------------------------------------------------------------------------
export function normalizeLabel(text) {
  let s = String(text);
  if (s.startsWith('[') && s.endsWith(']')) {
    s = s.slice(1, s.length - 1);
  }
  s = s.trim();
  s = s.replace(/[ \t\r\n]+/g, ' ');
  s = s.toLowerCase().toUpperCase();
  return s;
}

// Parse the text of a `link_reference_definition` node:
//   [label]: <destination> ["title"|'title'|(title)]
// The node text may include a trailing newline. Returns {label, url, title?}
// or null if the text is not a well-formed definition.
function parseDefinition(text) {
  const t = String(text).replace(/\r?\n/g, ' ').trim();
  const labelMatch = /^(\[[^\]]*\])\s*:\s*(.*)$/s.exec(t);
  if (!labelMatch) return null;
  const label = labelMatch[1];
  let rest = labelMatch[2].trim();

  let url = '';
  if (rest.startsWith('<')) {
    const end = rest.indexOf('>');
    if (end === -1) return null;
    url = rest.slice(1, end);
    rest = rest.slice(end + 1).trim();
  } else if (rest.startsWith('(')) {
    const end = rest.indexOf(')');
    if (end === -1) return null;
    url = rest.slice(1, end);
    rest = rest.slice(end + 1).trim();
  } else {
    const bare = /^\S+/.exec(rest);
    if (!bare) return null;
    url = bare[0];
    rest = rest.slice(url.length).trim();
  }

  let title;
  if (rest) {
    const tm = /^(?:"([^"]*)"|'([^']*)'|\(([^)]*)\))$/.exec(rest);
    if (tm) title = tm[1] ?? tm[2] ?? tm[3];
  }

  return { label, url, title };
}

// Run one `sg scan --json` over all files using OUR grammar (sgconfig.yml in
// this dir registers the `markdowntext` custom language; .md files are
// dispatched to it by extension). Returns the parsed match array.
function runScan(files) {
  const args = ['scan', '--config', CONFIG_FILE, '--json=compact', ...files];
  try {
    const stdout = execFileSync('sg', args, {
      encoding: 'utf8',
      maxBuffer: 64 * 1024 * 1024,
    });
    const start = stdout.indexOf('[');
    if (start === -1) throw new Error(`unexpected sg stdout: ${stdout.slice(0, 200)}`);
    return JSON.parse(stdout.slice(start));
  } catch (e) {
    const stderr = e.stderr ? `\n${String(e.stderr)}` : '';
    const err = new Error(`sg scan failed (${args.join(' ')}): ${e.message}${stderr}`);
    err.cause = e;
    throw err;
  }
}

function resolveFile(matches) {
  // (a) Collect definitions, deduping by (file, byteOffset.start) and
  //     preferring the `defs-titled` match for the same start.
  const defsByStart = new Map();
  for (const m of matches) {
    if (!DEFINITION_RULES.has(m.ruleId)) continue;
    const key = m.range.byteOffset.start;
    const existing = defsByStart.get(key);
    if (!existing || (m.ruleId === 'defs-titled' && existing.ruleId !== 'defs-titled')) {
      defsByStart.set(key, m);
    }
  }

  // (b) Map normalized label -> {url,title}; FIRST definition in document
  //     order wins.
  const labelMap = new Map();
  for (const m of defsByStart.values()) {
    const parsed = parseDefinition(m.text);
    if (!parsed) continue;
    const norm = normalizeLabel(parsed.label);
    if (!norm) continue;
    if (!labelMap.has(norm)) {
      labelMap.set(norm, { url: parsed.url, title: parsed.title });
    }
  }

  // (c+d) Resolve each link match.
  const results = [];
  for (const m of matches) {
    const kind = LINK_RULES[m.ruleId];
    if (!kind) continue;

    // With kind-selector rules the matched node is already the reference
    // link_label (brackets included). For `image` the selector
    // `image > link_label:nth-child(2)` never matches inline images (their 2nd
    // child is a link_destination); the starts-with-`[` check below is a
    // defensive guard for that invariant.
    let label = m.text;
    if (kind === 'image' && !label.startsWith('[')) continue;

    const normalizedLabel = normalizeLabel(label);
    if (!normalizedLabel) continue;

    const def = labelMap.get(normalizedLabel);
    results.push({
      file: m.file,
      kind,
      label,
      normalizedLabel,
      resolved: Boolean(def),
      url: def?.url,
      title: def?.title,
      start: { line: m.range.start.line, column: m.range.start.column },
      end: { line: m.range.end.line, column: m.range.end.column },
      text: m.text,
    });
  }
  // ast-grep returns matches rule-by-rule; sort into document order for
  // deterministic output.
  results.sort((a, b) => a.start.line - b.start.line || a.start.column - b.start.column);
  return results;
}

export function resolveFiles(files) {
  const matches = runScan(files);
  const results = [];
  for (const file of files) {
    const fileMatches = matches.filter((m) => m.file === file);
    results.push(...resolveFile(fileMatches));
  }
  return results;
}

function main() {
  const files = process.argv.slice(2);
  if (files.length === 0) {
    process.stderr.write('usage: node tools/link-resolver/resolver.mjs <file.md>...\n');
    process.exit(2);
  }
  const results = resolveFiles(files);
  process.stdout.write(`${JSON.stringify(results, null, 2)}\n`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main();
}
