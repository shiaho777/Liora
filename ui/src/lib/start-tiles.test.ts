import { describe, expect, it } from 'vitest';
import {
  addTile,
  isPinned,
  mergeTiles,
  parseTiles,
  removeTile,
  renameTile,
  type Tile,
  type TopSite,
} from './start-tiles';

const top: TopSite[] = [
  { url: 'https://a.example', title: 'A', visitCount: 10 },
  { url: 'https://b.example', title: 'B', visitCount: 5 },
  { url: 'https://c.example', title: 'C', visitCount: 1 },
];

describe('parseTiles', () => {
  it('returns an empty list for non-arrays', () => {
    expect(parseTiles(null)).toEqual([]);
    expect(parseTiles('x')).toEqual([]);
  });

  it('keeps valid tiles and drops malformed or duplicate ones', () => {
    const raw = [
      { url: 'https://a.example', title: 'A' },
      { url: 'https://a.example', title: 'dup' },
      { url: '  ', title: 'empty' },
      { title: 'no url' },
      { url: 'https://b.example' },
    ];
    const parsed = parseTiles(raw);
    expect(parsed).toHaveLength(2);
    expect(parsed[1]).toEqual({ url: 'https://b.example', title: '' });
  });
});

describe('mergeTiles', () => {
  it('returns top sites when nothing is pinned', () => {
    const merged = mergeTiles([], top);
    expect(merged.map((tile) => tile.url)).toEqual([
      'https://a.example',
      'https://b.example',
      'https://c.example',
    ]);
  });

  it('puts pinned tiles first and skips pinned urls in top sites', () => {
    const pinned: Tile[] = [{ url: 'https://b.example', title: 'B pinned' }];
    const merged = mergeTiles(pinned, top);
    expect(merged[0]).toEqual({ url: 'https://b.example', title: 'B pinned' });
    expect(merged.map((tile) => tile.url)).toEqual([
      'https://b.example',
      'https://a.example',
      'https://c.example',
    ]);
  });

  it('respects the limit', () => {
    const pinned: Tile[] = [
      { url: 'https://p1.example', title: 'P1' },
      { url: 'https://p2.example', title: 'P2' },
    ];
    const merged = mergeTiles(pinned, top, 3);
    expect(merged).toHaveLength(3);
    expect(merged[2].url).toBe('https://a.example');
  });

  it('deduplicates pinned tiles', () => {
    const pinned: Tile[] = [
      { url: 'https://x.example', title: 'X' },
      { url: 'https://x.example', title: 'X again' },
    ];
    expect(mergeTiles(pinned, [])).toHaveLength(1);
  });
});

describe('addTile / removeTile / renameTile / isPinned', () => {
  it('adds a tile and ignores duplicates', () => {
    const added = addTile([], 'https://x.example', 'X');
    expect(added).toHaveLength(1);
    expect(addTile(added, 'https://x.example', 'again')).toHaveLength(1);
    expect(addTile(added, '   ', 'empty')).toHaveLength(1);
  });

  it('removes a tile by url', () => {
    const pinned: Tile[] = [{ url: 'https://x.example', title: 'X' }];
    expect(removeTile(pinned, 'https://x.example')).toHaveLength(0);
    expect(removeTile(pinned, 'https://other.example')).toHaveLength(1);
  });

  it('renames a tile by url', () => {
    const pinned: Tile[] = [{ url: 'https://x.example', title: 'X' }];
    expect(renameTile(pinned, 'https://x.example', 'New')[0].title).toBe('New');
    expect(renameTile(pinned, 'https://other.example', 'New')[0].title).toBe('X');
  });

  it('reports pinned status', () => {
    const pinned: Tile[] = [{ url: 'https://x.example', title: 'X' }];
    expect(isPinned(pinned, 'https://x.example')).toBe(true);
    expect(isPinned(pinned, 'https://y.example')).toBe(false);
  });
});