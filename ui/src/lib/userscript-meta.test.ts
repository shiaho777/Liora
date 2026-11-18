import { describe, expect, it } from 'vitest';
import { exportUserScript, parseUserScript, patternToLiora } from './userscript-meta';

describe('patternToLiora', () => {
  it('maps wildcard and all_urls to *', () => {
    expect(patternToLiora('*')).toBe('*');
    expect(patternToLiora('<all_urls>')).toBe('*');
  });

  it('strips scheme and trailing path', () => {
    expect(patternToLiora('https://example.com/*')).toBe('example.com');
    expect(patternToLiora('*://example.com/')).toBe('example.com');
    expect(patternToLiora('http://example.com')).toBe('example.com');
  });

  it('keeps subdomain-only patterns', () => {
    expect(patternToLiora('*://*.example.com/*')).toBe('*.example.com');
  });

  it('strips ports', () => {
    expect(patternToLiora('https://example.com:8080/*')).toBe('example.com');
  });

  it('refuses path-specific patterns rather than broadening them', () => {
    expect(patternToLiora('https://example.com/admin/*')).toBeNull();
    expect(patternToLiora('https://example.com/page')).toBeNull();
  });

  it('refuses mid-host wildcards', () => {
    expect(patternToLiora('https://*.foo.*.com/*')).toBeNull();
  });
});

describe('parseUserScript', () => {
  const sample = `// ==UserScript==
// @name         Hide sidebar
// @namespace    http://tampermonkey.net/
// @version      1.2
// @description  Removes the sidebar
// @match        https://example.com/*
// @match        *://*.test.org/*
// @exclude      https://example.com/keep/*
// @run-at       document-start
// @grant        GM_addStyle
// ==/UserScript==

document.querySelector('.sidebar')?.remove();
`;

  it('returns null without a metadata block', () => {
    expect(parseUserScript('console.log(1);')).toBeNull();
  });

  it('extracts name, description and version', () => {
    const parsed = parseUserScript(sample)!;
    expect(parsed.name).toBe('Hide sidebar');
    expect(parsed.description).toBe('Removes the sidebar');
    expect(parsed.version).toBe('1.2');
  });

  it('converts matches and excludes', () => {
    const parsed = parseUserScript(sample)!;
    expect(parsed.matches).toEqual(['example.com', '*.test.org']);
    // The exclude has a path, so it cannot be represented and is dropped.
    expect(parsed.excludes).toEqual([]);
  });

  it('maps run-at', () => {
    expect(parseUserScript(sample)!.runAt).toBe('start');
    const idle = sample.replace('document-start', 'document-idle');
    expect(parseUserScript(idle)!.runAt).toBe('idle');
    const end = sample.replace('document-start', 'document-end');
    expect(parseUserScript(end)!.runAt).toBe('end');
  });

  it('keeps the code after the block', () => {
    const parsed = parseUserScript(sample)!;
    expect(parsed.code.trim()).toBe("document.querySelector('.sidebar')?.remove();");
  });

  it('reports unsupported grants transparently', () => {
    const parsed = parseUserScript(sample)!;
    expect(parsed.unsupported).toContain('@grant GM_addStyle');
  });

  it('defaults name and runAt when headers are missing', () => {
    const minimal = `// ==UserScript==
// @match *://example.com/*
// ==/UserScript==
alert(1);`;
    const parsed = parseUserScript(minimal)!;
    expect(parsed.name).toBe('Imported script');
    expect(parsed.runAt).toBe('end');
  });
});

describe('exportUserScript', () => {
  it('round-trips a domain script', () => {
    const exported = exportUserScript({
      name: 'Demo',
      matches: ['example.com'],
      runAt: 'end',
      code: 'alert(1);',
    });
    expect(exported).toContain('// @name         Demo');
    expect(exported).toContain('// @match        *://example.com/*');
    expect(exported).toContain('// @match        *://*.example.com/*');
    const reparsed = parseUserScript(exported)!;
    expect(reparsed.name).toBe('Demo');
    expect(reparsed.matches).toContain('example.com');
    expect(reparsed.code.trim()).toBe('alert(1);');
  });

  it('exports subdomain-only and exact patterns distinctly', () => {
    const sub = exportUserScript({ name: 's', matches: ['*.x.com'], runAt: 'start', code: 'x' });
    expect(sub).toContain('// @match        *://*.x.com/*');
    expect(sub).not.toContain('*://x.com/*');
    const exact = exportUserScript({ name: 'e', matches: ['=x.com'], runAt: 'end', code: 'x' });
    expect(exact).toContain('// @match        *://x.com/*');
    expect(exact).not.toContain('*.x.com');
  });

  it('exports wildcard as all urls', () => {
    const all = exportUserScript({ name: 'a', matches: ['*'], runAt: 'idle', code: 'x' });
    expect(all).toContain('// @match        *://*/*');
    expect(all).toContain('document-idle');
  });
});