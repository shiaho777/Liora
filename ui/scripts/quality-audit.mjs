#!/usr/bin/env node
// Liora interface quality audit.
//
// A repeatable, objective check that drives each improvement pass: it parses
// every Svelte component and reports concrete anti-patterns instead of relying
// on guesswork. Run with `node scripts/quality-audit.mjs` (or `pnpm audit:ui`).
// Exit code is non-zero when blocking issues remain, so it can gate CI.
//
// Checks:
//   1. icon-button / button with only an icon must carry aria-label or title
//   2. raw px font-size in components (design tokens required; em/% allowed)
//   3. hardcoded hex colours outside the token sheet (app.css)
//   4. console.log left in production code
//   5. empty catch blocks that swallow errors silently
//   6. images without alt text

import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parse } from 'svelte/compiler';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const SRC = join(ROOT, 'src');

function walk(dir, out = []) {
  for (const name of readdirSync(dir)) {
    const full = join(dir, name);
    if (statSync(full).isDirectory()) walk(full, out);
    else if (name.endsWith('.svelte')) out.push(full);
  }
  return out;
}

const issues = [];
function report(file, line, rule, message) {
  issues.push({ file: relative(ROOT, file), line, rule, message });
}

// Walk the parsed template AST, collecting element nodes with their source.
function elements(node, source, visit) {
  if (!node) return;
  if (node.type === 'RegularElement' || node.type === 'Component' || node.type === 'SvelteElement') {
    visit(node, source);
  }
  for (const child of node.children ?? []) elements(child, source, visit);
  // Svelte 5 snippets / blocks carry fragments.
  if (node.fragment) elements(node.fragment, source, visit);
  for (const branch of node.branches ?? []) elements(branch, source, visit);
  if (node.body) elements(node.body, source, visit);
  if (node.consequent) elements(node.consequent, source, visit);
  if (node.alternate) elements(node.alternate, source, visit);
}

function attrValue(el, name) {
  const attr = (el.attributes ?? []).find(
    (a) => a.type === 'Attribute' && a.name === name,
  );
  if (!attr) return undefined;
  if (attr.value === true) return '';
  if (Array.isArray(attr.value)) {
    return attr.value.map((v) => (v.type === 'Text' ? v.data : '')).join('');
  }
  return String(attr.value ?? '');
}

function hasAttr(el, name) {
  return (el.attributes ?? []).some(
    (a) => (a.type === 'Attribute' || a.type === 'SpreadAttribute') && a.name === name,
  );
}

function lineOf(source, offset) {
  return source.slice(0, offset).split('\n').length;
}

const files = walk(SRC);
for (const file of files) {
  const source = readFileSync(file, 'utf8');
  let ast;
  try {
    ast = parse(source, { filename: file });
  } catch (error) {
    report(file, 1, 'parse', `could not parse: ${error.message}`);
    continue;
  }

  const template = ast.fragment ?? ast.html;
  elements(template, source, (el) => {
    const cls = attrValue(el, 'class') ?? '';
    const isButton = el.name === 'button';
    const isIconish = /icon-button|tab__close|tabrow__close|tabgroup__action/.test(cls);

    // 1. Accessible name for icon-only controls.
    if (isButton || isIconish) {
      const hasLabel = hasAttr(el, 'aria-label') || hasAttr(el, 'title');
      // A button with visible text content has an accessible name already.
      const text = (el.children ?? [])
        .filter((c) => c.type === 'Text')
        .map((c) => c.data.trim())
        .join('');
      const hasExpressionChild = (el.children ?? []).some(
        (c) => c.type === 'ExpressionTag' || c.type === 'Component' || c.type === 'RegularElement',
      );
      if (!hasLabel && !text && isIconish) {
        report(file, lineOf(source, el.start), 'a11y-label', 'icon control lacks aria-label/title');
      } else if (!hasLabel && !text && !hasExpressionChild && isButton) {
        report(file, lineOf(source, el.start), 'a11y-label', 'button lacks accessible name');
      }
    }

    // 6. Images need alt.
    if (el.name === 'img' && !hasAttr(el, 'alt')) {
      report(file, lineOf(source, el.start), 'img-alt', 'img without alt attribute');
    }

    // 7. Modal dialogs must trap focus so Tab cannot escape behind them.
    if (attrValue(el, 'aria-modal') === 'true' && !/trapFocus\(/.test(source)) {
      report(file, lineOf(source, el.start), 'focus-trap', 'aria-modal dialog without trapFocus');
    }
  });

  // 2/3/4/5 operate on the raw source (style + script), line by line.
  const lines = source.split('\n');
  const inStyle = { value: false };
  lines.forEach((text, i) => {
    if (/<style/.test(text)) inStyle.value = true;
    if (/<\/style>/.test(text)) inStyle.value = false;

    // An explicit `quality-audit-ignore` comment on this line or the one above
    // marks a deliberate exception (e.g. a decorative multi-colour gradient).
    if (/quality-audit-ignore/.test(text) || /quality-audit-ignore/.test(lines[i - 1] ?? '')) return;

    // 2. raw px font-size (em, %, rem, var() are fine). The Appearance panel
    // renders a miniature browser mock-up whose tiny labels are deliberately
    // sub-token; that one file is exempt.
    const fs = /font-size:\s*([0-9.]+)px/.exec(text);
    if (fs && !file.endsWith('Appearance.svelte')) {
      report(file, i + 1, 'font-token', `raw font-size ${fs[1]}px; use a --text-* token`);
    }

    // 3. hardcoded hex colour in components. The token sheet (app.css) and the
    // shared palette data module (palette.ts) are the legitimate homes for raw
    // colour values, so they are exempt; everywhere else should use a token.
    if (!file.endsWith('app.css') && !file.endsWith('palette.ts')) {
      const hex = /#[0-9a-fA-F]{6}\b/.exec(text);
      // Allow hex inside comments and lines that already reference a token.
      if (hex && !/^\s*(\/\/|\/\*|\*)/.test(text) && !/var\(--/.test(text)) {
        report(file, i + 1, 'color-token', `hardcoded colour ${hex[0]}; use a token`);
      }
    }

    // 4. console.log in production.
    if (/console\.log\(/.test(text) && !/test/.test(file)) {
      report(file, i + 1, 'console', 'console.log left in production code');
    }

    // 5. empty catch.
    if (/catch\s*(\([^)]*\))?\s*\{\s*\}/.test(text)) {
      report(file, i + 1, 'empty-catch', 'empty catch swallows errors');
    }
  });
}

// Group and print.
const byRule = {};
for (const issue of issues) (byRule[issue.rule] ??= []).push(issue);

console.log(`Liora interface quality audit — ${files.length} components scanned`);
console.log('='.repeat(60));
if (issues.length === 0) {
  console.log('No issues found. Interface meets the quality bar.');
} else {
  for (const [rule, list] of Object.entries(byRule)) {
    console.log(`\n[${rule}] ${list.length} issue(s)`);
    for (const item of list.slice(0, 40)) {
      console.log(`  ${item.file}:${item.line}  ${item.message}`);
    }
    if (list.length > 40) console.log(`  … and ${list.length - 40} more`);
  }
  console.log(`\nTotal: ${issues.length} issue(s) across ${Object.keys(byRule).length} rule(s).`);
}

// Blocking rules fail the run; advisory ones are reported but tolerated.
const blocking = issues.filter((i) => ['a11y-label', 'img-alt', 'console', 'empty-catch', 'parse', 'focus-trap'].includes(i.rule));
process.exit(blocking.length > 0 ? 1 : 0);