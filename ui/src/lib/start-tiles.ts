// Pure helpers for the start page's pinned tiles. Kept free of Svelte so the
// merge/edit rules can be unit-tested.

export interface Tile {
  url: string;
  title: string;
}

export interface TopSite {
  url: string;
  title: string;
  visitCount?: number;
}

/** Parses the stored setting value into a typed tile list, tolerating bad data. */
export function parseTiles(raw: unknown): Tile[] {
  if (!Array.isArray(raw)) return [];
  const out: Tile[] = [];
  const seen = new Set<string>();
  for (const item of raw) {
    if (!item || typeof item !== 'object') continue;
    const record = item as Record<string, unknown>;
    if (typeof record.url !== 'string' || !record.url.trim()) continue;
    const url = record.url.trim();
    if (seen.has(url)) continue;
    seen.add(url);
    out.push({ url, title: typeof record.title === 'string' ? record.title : '' });
  }
  return out;
}

/**
 * Builds the visible grid: pinned tiles first (in order), then top sites that
 * are not already pinned, up to `limit`. When nothing is pinned this is just
 * the top sites, preserving the previous behaviour.
 */
export function mergeTiles(pinned: Tile[], top: TopSite[], limit = 8): Tile[] {
  const out: Tile[] = [];
  const seen = new Set<string>();
  for (const tile of pinned) {
    if (seen.has(tile.url)) continue;
    seen.add(tile.url);
    out.push({ url: tile.url, title: tile.title });
    if (out.length >= limit) return out;
  }
  for (const site of top) {
    if (!site.url || seen.has(site.url)) continue;
    seen.add(site.url);
    out.push({ url: site.url, title: site.title });
    if (out.length >= limit) return out;
  }
  return out;
}

/** Adds a tile, ignoring duplicates by URL. Returns the new list. */
export function addTile(pinned: Tile[], url: string, title: string): Tile[] {
  const trimmed = url.trim();
  if (!trimmed) return pinned;
  if (pinned.some((tile) => tile.url === trimmed)) return pinned;
  return [...pinned, { url: trimmed, title: title.trim() }];
}

/** Removes a tile by URL. Returns the new list. */
export function removeTile(pinned: Tile[], url: string): Tile[] {
  return pinned.filter((tile) => tile.url !== url);
}

/** Renames a tile by URL. Returns the new list. */
export function renameTile(pinned: Tile[], url: string, title: string): Tile[] {
  return pinned.map((tile) => (tile.url === url ? { ...tile, title: title.trim() } : tile));
}

/** True when the URL is one of the pinned tiles. */
export function isPinned(pinned: Tile[], url: string): boolean {
  return pinned.some((tile) => tile.url === url);
}

/**
 * First-run shortcuts. A brand-new profile has no history, and an empty start
 * page reads as a demo; these well-known sites give the grid something real
 * to show. They are display defaults only — never written to settings — and
 * disappear as soon as the user pins tiles or builds history.
 */
export const DEFAULT_TILES: Tile[] = [
  { url: 'https://www.wikipedia.org', title: 'Wikipedia' },
  { url: 'https://github.com', title: 'GitHub' },
  { url: 'https://www.bilibili.com', title: '哔哩哔哩' },
  { url: 'https://www.zhihu.com', title: '知乎' },
  { url: 'https://www.youtube.com', title: 'YouTube' },
  { url: 'https://news.ycombinator.com', title: 'Hacker News' },
  { url: 'https://www.reddit.com', title: 'Reddit' },
  { url: 'https://x.com', title: 'X' },
];

/**
 * The visible grid including first-run defaults: pinned tiles and top sites
 * win; defaults only fill the remaining slots when both are empty-ish.
 */
export function gridTiles(pinned: Tile[], top: TopSite[], limit = 8): Tile[] {
  const merged = mergeTiles(pinned, top, limit);
  if (merged.length >= limit) return merged;
  const seen = new Set(merged.map((tile) => tile.url));
  const out = [...merged];
  for (const tile of DEFAULT_TILES) {
    if (out.length >= limit) break;
    if (seen.has(tile.url)) continue;
    seen.add(tile.url);
    out.push(tile);
  }
  return out;
}