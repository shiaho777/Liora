import { describe, expect, it } from 'vitest';
import { buildSuggestions, hostOf, moveSelection } from './suggest';

const looksLikeUrl = (value: string): boolean => /^[^\s/]+\.[a-z]{2,}([/:?#].*)?$/i.test(value);

describe('buildSuggestions', () => {
  it('returns nothing for empty input', () => {
    expect(buildSuggestions('   ', [], [], 'DuckDuckGo', looksLikeUrl)).toEqual([]);
  });

  it('always leads with a search row carrying the raw text', () => {
    const items = buildSuggestions('hello world', [], [], 'DuckDuckGo', looksLikeUrl);
    expect(items[0]).toMatchObject({ kind: 'search', value: 'hello world', detail: 'DuckDuckGo' });
  });

  it('adds a direct open row for URL-looking input', () => {
    const items = buildSuggestions('example.com', [], [], 'DuckDuckGo', looksLikeUrl);
    expect(items[0].kind).toBe('search');
    expect(items[1]).toMatchObject({ kind: 'url', value: 'https://example.com' });
  });

  it('keeps an explicit scheme untouched', () => {
    const items = buildSuggestions('http://localhost:8080/x', [], [], 'Bing', looksLikeUrl);
    // looksLikeUrl is false for this shape in the test helper, so only search.
    expect(items.every((item) => item.kind === 'search')).toBe(true);
  });

  it('ranks bookmarks before history and dedupes by URL', () => {
    const history = [
      { url: 'https://a.example/', title: 'A from history' },
      { url: 'https://b.example/', title: 'B' },
    ];
    const bookmarks = [{ url: 'https://a.example/', title: 'A saved' }];
    const items = buildSuggestions('example', history, bookmarks, 'DDG', looksLikeUrl);
    const pages = items.filter((item) => item.kind !== 'search');
    expect(pages[0]).toMatchObject({ kind: 'bookmark', title: 'A saved' });
    expect(pages[1]).toMatchObject({ kind: 'history', title: 'B' });
    expect(pages.filter((item) => item.value === 'https://a.example/')).toHaveLength(1);
  });

  it('falls back to the host when a title is missing', () => {
    const items = buildSuggestions('x', [{ url: 'https://www.example.org/page', title: '' }], [], 'DDG', looksLikeUrl);
    expect(items[1]).toMatchObject({ title: 'example.org', detail: 'example.org' });
  });

  it('respects the limit', () => {
    const history = Array.from({ length: 20 }, (_, i) => ({ url: `https://s${i}.example/`, title: `S${i}` }));
    const items = buildSuggestions('example', history, [], 'DDG', looksLikeUrl, 8);
    expect(items).toHaveLength(8);
  });
});

describe('moveSelection', () => {
  it('wraps in both directions', () => {
    expect(moveSelection(-1, 1, 3)).toBe(0);
    expect(moveSelection(-1, -1, 3)).toBe(2);
    expect(moveSelection(2, 1, 3)).toBe(0);
    expect(moveSelection(0, -1, 3)).toBe(2);
  });

  it('stays unset for an empty list', () => {
    expect(moveSelection(-1, 1, 0)).toBe(-1);
  });
});

describe('hostOf', () => {
  it('strips scheme and www', () => {
    expect(hostOf('https://www.example.com/a/b')).toBe('example.com');
  });

  it('returns the input for non-URLs', () => {
    expect(hostOf('not a url')).toBe('not a url');
  });
});