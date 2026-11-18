// Parses the Tampermonkey/Greasemonkey/Violentmonkey metadata block so users
// can import the scripts they already have, and exports Liora scripts back to
// the same format. Pure functions, unit-tested.
//
// Supported headers: @name, @match, @include, @exclude, @run-at, @description,
// @version, @grant (recognized but not enforced — Liora runs scripts with full
// page access, which we surface honestly in the UI rather than pretending to
// sandbox them).

export type RunAt = 'start' | 'end' | 'idle';

export interface ParsedScript {
  name: string;
  matches: string[];
  excludes: string[];
  runAt: RunAt;
  code: string;
  description: string;
  version: string;
  /** Headers we saw but do not act on, shown to the user for transparency. */
  unsupported: string[];
}

const META_BLOCK = /\/\/\s*==UserScript==([\s\S]*?)\/\/\s*==\/UserScript==/;
const HEADER_LINE = /\/\/\s*@(\S+)\s*(.*)/g;

/**
 * Converts a Tampermonkey @match/@include pattern to Liora's domain syntax.
 * Returns null when the pattern cannot be represented (e.g. path-specific),
 * so the caller can warn instead of silently broadening the scope.
 */
export function patternToLiora(pattern: string): string | null {
  let p = pattern.trim();
  if (p === '*' || p === '<all_urls>') return '*';

  // Strip scheme.
  p = p.replace(/^[a-z*]+:\/\//i, '');
  // A path beyond "/" means the script targets specific pages; Liora matches
  // by host only, so we cannot faithfully represent it.
  const slash = p.indexOf('/');
  if (slash >= 0) {
    const path = p.slice(slash + 1);
    if (path && path !== '*') return null;
    p = p.slice(0, slash);
  }
  // Strip port.
  p = p.replace(/:\d+$/, '');
  if (!p) return null;

  // *.example.com -> subdomains only; example.com -> domain + subdomains.
  if (p.startsWith('*.')) {
    const host = p.slice(2).toLowerCase();
    // Any further wildcard (e.g. *.foo.*.com) cannot be represented.
    return host && !host.includes('*') ? `*.${host}` : null;
  }
  if (p.includes('*')) return null; // mid-host wildcards are not representable
  return p.toLowerCase();
}

function runAtFromHeader(value: string): RunAt {
  const v = value.trim().toLowerCase();
  if (v === 'document-start' || v === 'start') return 'start';
  if (v === 'document-idle' || v === 'idle') return 'idle';
  // document-end, document-body and anything else map to end.
  return 'end';
}

/** Parses a full .user.js source. Returns null when there is no metadata block. */
export function parseUserScript(source: string): ParsedScript | null {
  const block = META_BLOCK.exec(source);
  if (!block) return null;

  const headers: Array<[string, string]> = [];
  let match: RegExpExecArray | null;
  HEADER_LINE.lastIndex = 0;
  while ((match = HEADER_LINE.exec(block[1])) !== null) {
    headers.push([match[1].toLowerCase(), match[2].trim()]);
  }

  const name = headers.find(([key]) => key === 'name')?.[1] ?? '';
  const description = headers.find(([key]) => key === 'description')?.[1] ?? '';
  const version = headers.find(([key]) => key === 'version')?.[1] ?? '';
  const runAt = runAtFromHeader(headers.find(([key]) => key === 'run-at')?.[1] ?? 'end');

  const matches: string[] = [];
  const excludes: string[] = [];
  const unsupported: string[] = [];
  for (const [key, value] of headers) {
    if (key === 'match' || key === 'include') {
      const converted = patternToLiora(value);
      if (converted) matches.push(converted);
      else unsupported.push(`@${key} ${value}`);
    } else if (key === 'exclude' || key === 'exclude-match') {
      const converted = patternToLiora(value);
      if (converted) excludes.push(converted);
    } else if (key === 'grant' && value && value !== 'none') {
      unsupported.push(`@grant ${value}`);
    } else if (key === 'require' || key === 'resource') {
      unsupported.push(`@${key} ${value}`);
    }
  }

  // Deduplicate while preserving order.
  const unique = (list: string[]) => [...new Set(list)];

  // The code is everything after the metadata block.
  const code = source.slice(block.index + block[0].length).replace(/^\s*\n/, '');

  return {
    name: name || 'Imported script',
    matches: unique(matches),
    excludes: unique(excludes),
    runAt,
    code,
    description,
    version,
    unsupported: unique(unsupported),
  };
}

/** Renders a Liora script back to the standard .user.js format. */
export function exportUserScript(script: {
  name: string;
  matches: string[];
  runAt: RunAt;
  code: string;
}): string {
  const runAtHeader =
    script.runAt === 'start' ? 'document-start' : script.runAt === 'idle' ? 'document-idle' : 'document-end';
  const lines = [
    '// ==UserScript==',
    `// @name         ${script.name}`,
    '// @namespace    liora',
    '// @version      1.0',
    `// @run-at       ${runAtHeader}`,
    '// @grant        none',
  ];
  for (const m of script.matches) {
    if (m === '*') lines.push('// @match        *://*/*');
    else if (m.startsWith('*.')) lines.push(`// @match        *://${m}/*`);
    else if (m.startsWith('=')) lines.push(`// @match        *://${m.slice(1)}/*`);
    else lines.push(`// @match        *://${m}/*`, `// @match        *://*.${m}/*`);
  }
  lines.push('// ==/UserScript==', '', script.code);
  return lines.join('\n');
}