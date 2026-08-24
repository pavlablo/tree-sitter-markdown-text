import { test } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { resolveFiles, normalizeLabel } from '../resolver.mjs';

const FIXTURE = path.join(path.dirname(fileURLToPath(import.meta.url)), 'edge-cases.md');

const results = resolveFiles([FIXTURE]);

function byKindAndLabel(kind, label) {
  return results.find((r) => r.kind === kind && r.label === label);
}

function expectFound(kind, label) {
  const r = byKindAndLabel(kind, label);
  assert.ok(r, `expected result kind=${kind} label=${label}, got none`);
  return r;
}

test('normalizeLabel: case + whitespace insensitive', () => {
  assert.equal(normalizeLabel('[Foo Bar]'), 'FOO BAR');
  assert.equal(normalizeLabel('[foo   bar]'), 'FOO BAR');
  assert.equal(normalizeLabel('[Foo Bar]'), normalizeLabel('[foo   bar]'));
});

test('sanity: every link label includes brackets (our grammar won, not builtin markdown)', () => {
  assert.ok(results.length > 0, 'resolver produced no results at all');
  for (const r of results) {
    assert.ok(
      r.label.startsWith('[') && r.label.endsWith(']'),
      `label '${r.label}' lacks brackets — builtin grammar likely shadowed ours`,
    );
  }
});

test('full reference resolved', () => {
  const r = expectFound('fullref', '[ref-target]');
  assert.equal(r.resolved, true);
  assert.equal(r.url, 'https://full.example.com');
  assert.equal(r.title, 'Full title');
  assert.equal(r.normalizedLabel, 'REF-TARGET');
});

test('full reference NOT resolved', () => {
  const r = expectFound('fullref', '[missing-ref]');
  assert.equal(r.resolved, false);
});

test('collapsed resolved', () => {
  const r = expectFound('collapsed', '[collapsed]');
  assert.equal(r.resolved, true);
  assert.equal(r.url, 'https://collapsed.example.com');
  assert.equal(r.title, 'Collapsed title');
});

test('collapsed NOT resolved', () => {
  const r = expectFound('collapsed', '[collapsed-nope]');
  assert.equal(r.resolved, false);
});

test('shortcut resolved', () => {
  const r = expectFound('shortcut', '[shortcut-hit]');
  assert.equal(r.resolved, true);
  assert.equal(r.url, 'https://shortcut.example.com');
});

test('shortcut NOT resolved', () => {
  const r = expectFound('shortcut', '[shortcut-miss]');
  assert.equal(r.resolved, false);
});

test('reference image resolved', () => {
  const r = expectFound('image', '[img-ref]');
  assert.equal(r.kind, 'image');
  assert.equal(r.resolved, true);
  assert.equal(r.url, 'https://img.example.com');
  assert.equal(r.title, 'Img title');
});

test('reference image NOT resolved', () => {
  const r = expectFound('image', '[img-miss]');
  assert.equal(r.resolved, false);
});

test('forward reference: definition after use', () => {
  const r = expectFound('fullref', '[fwd]');
  assert.equal(r.resolved, true);
  assert.equal(r.url, 'https://forward.example.com');
  assert.equal(r.title, 'Fwd title');
});

test('case/whitespace-insensitive matching', () => {
  const r = expectFound('shortcut', '[Foo Bar]');
  assert.equal(r.resolved, true);
  assert.equal(r.url, 'https://case.example.com');
  assert.equal(r.title, 'Case title');
  assert.equal(r.normalizedLabel, 'FOO BAR');
});

test('duplicate definition: first wins', () => {
  const r = expectFound('shortcut', '[dup]');
  assert.equal(r.resolved, true);
  assert.equal(r.url, 'https://first.example.com');
  assert.equal(r.title, 'First');
});

test('inline link is not a reference', () => {
  assert.equal(byKindAndLabel('shortcut', '[inline only]'), undefined);
  assert.equal(results.some((r) => r.text === '[inline only](https://inline.example.com)'), false);
});

test('footnote reference is not a shortcut link', () => {
  assert.equal(byKindAndLabel('shortcut', '[^1]'), undefined);
  assert.equal(results.some((r) => r.label === '[^1]'), false);
});
