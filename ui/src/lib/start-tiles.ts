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