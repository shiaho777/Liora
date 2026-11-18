import { describe, expect, it } from 'vitest';
import {
  BUILTIN_ENGINES,
  buildSearchUrl,
  engineName,
  engineTemplate,
  newEngineId,
  parseCustomEngines,
  validateEngine,
  type CustomEngine,
} from './search-engines';

const custom: CustomEngine[] = [
  { id: 'custom-a', name: 'My Search', template: 'https://search.example/find?q=%s' },
];

describe('engineName / engineTemplate', () => {
  it('resolves built-in engines', () => {
    expect(engineName('google', [])).toBe('Google');
    expect(engineTemplate('baidu', [])).toContain('baidu.com');
  });

  it('resolves custom engines', () => {
    expect(engineName('custom-a', custom)).toBe('My Search');
    expect(engineTemplate('custom-a', custom)).toBe('https://search.example/find?q=%s');
  });

  it('falls back to DuckDuckGo for an unknown engine', () => {
    expect(engineTemplate('missing', custom)).toBe(BUILTIN_ENGINES[0].template);
    expect(engineName('missing', custom)).toBe('missing');
  });
});

describe('buildSearchUrl', () => {
  it('encodes the query into the built-in template', () => {
    expect(buildSearchUrl('duckduckgo', [], 'hello world')).toBe(
      'https://duckduckgo.com/?q=hello%20world',
    );
  });

  it('uses a custom engine template', () => {
    expect(buildSearchUrl('custom-a', custom, 'a&b')).toBe(
      'https://search.example/find?q=a%26b',
    );
  });

  it('replaces every %s placeholder', () => {
    const multi: CustomEngine[] = [
      { id: 'm', name: 'Multi', template: 'https://x.example/?q=%s&ref=%s' },
    ];
    expect(buildSearchUrl('m', multi, 'q')).toBe('https://x.example/?q=q&ref=q');
  });

  it('encodes special characters', () => {
    expect(buildSearchUrl('google', [], 'a+b/c?d')).toBe(
      'https://www.google.com/search?q=a%2Bb%2Fc%3Fd',
    );
  });
});

describe('validateEngine', () => {
  it('accepts a valid name and template', () => {
    expect(validateEngine('My', 'https://x.example/?q=%s')).toEqual({ ok: true });
  });

  it('rejects an empty name', () => {
    expect(validateEngine('  ', 'https://x.example/?q=%s').error).toBe('name');
  });

  it('rejects an empty template', () => {
    expect(validateEngine('My', '   ').error).toBe('template');
  });

  it('rejects a template without %s', () => {
    expect(validateEngine('My', 'https://x.example/?q=test').error).toBe('placeholder');
  });

  it('rejects a non-http scheme', () => {
    expect(validateEngine('My', 'ftp://x.example/?q=%s').error).toBe('scheme');
    expect(validateEngine('My', 'javascript:alert(%s)').error).toBe('scheme');
  });
});

describe('newEngineId', () => {
  it('produces an id not already present', () => {
    const id = newEngineId(custom);
    expect(custom.some((engine) => engine.id === id)).toBe(false);
    expect(id.startsWith('custom-')).toBe(true);
  });
});

describe('parseCustomEngines', () => {
  it('returns an empty list for non-arrays', () => {
    expect(parseCustomEngines(null)).toEqual([]);
    expect(parseCustomEngines('x')).toEqual([]);
    expect(parseCustomEngines({})).toEqual([]);
  });

  it('keeps valid entries and drops malformed ones', () => {
    const raw = [
      { id: 'a', name: 'A', template: 'https://a.example/?q=%s' },
      { id: 'b', name: '', template: 'https://b.example/?q=%s' },
      { id: 'c', name: 'C', template: 'https://c.example/?q=none' },
      { id: 'd', name: 'D' },
      'not-an-object',
    ];
    const parsed = parseCustomEngines(raw);
    expect(parsed).toHaveLength(1);
    expect(parsed[0].id).toBe('a');
  });
});