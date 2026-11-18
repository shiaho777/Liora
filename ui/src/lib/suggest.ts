// Pure helpers for the address-bar suggestion dropdown. Kept free of Svelte
// so the ranking and dedupe rules can be unit-tested.

export interface SuggestEntry {
  url: string;
  title: string;
}

export type SuggestionKind = 'history' | 'bookmark' | 'search' | 'url';

export interface Suggestion {
  kind: SuggestionKind;
  /** What the address bar navigates to: a URL, or raw text for a search. */
  value: string;
  title: string;
  detail: string;
}

export function hostOf(url: string): string {
  try {
    return new URL(url).hostname.replace(/^www\./, '');
  } catch {
    return url;
  }
}

/**
 * Merge history and bookmark matches into one ranked list:
 * bookmarks first (the user asked to keep them), then history, deduped by
 * URL. The raw input always leads as a search row; a URL-looking input also
 * gets a direct "open" row.
 */
export function buildSuggestions(
  input: string,
  history: SuggestEntry[],
  bookmarks: SuggestEntry[],
  searchEngineName: string,
  looksLikeUrl: (value: string) => boolean,
  limit = 8,
): Suggestion[] {
  const text = input.trim();
  if (!text) return [];

  const out: Suggestion[] = [];
  const seen = new Set<string>();
  const push = (item: Suggestion): void => {
    const key = item.kind === 'search' ? `search:${item.value}` : item.value;
    if (seen.has(key)) return;
    seen.add(key);
    out.push(item);
  };

  push({
    kind: 'search',
    value: text,
    title: text,
    detail: searchEngineName,
  });

  if (looksLikeUrl(text)) {
    const url = /^[a-z][a-z0-9+.-]*:/i.test(text) ? text : `https://${text}`;
    push({ kind: 'url', value: url, title: url.replace(/^https?:\/\//, ''), detail: '' });
  }

  for (const entry of bookmarks) {
    if (!entry.url || seen.has(entry.url)) continue;
    push({
      kind: 'bookmark',
      value: entry.url,
      title: entry.title || hostOf(entry.url),
      detail: hostOf(entry.url),
    });
  }
  for (const entry of history) {
    if (!entry.url || seen.has(entry.url)) continue;
    push({
      kind: 'history',
      value: entry.url,
      title: entry.title || hostOf(entry.url),
      detail: hostOf(entry.url),
    });
  }

  return out.slice(0, limit);
}

/** Arrow-key navigation wraps around the list, like every address bar. */
export function moveSelection(current: number, delta: number, count: number): number {
  if (count <= 0) return -1;
  if (current < 0) return delta > 0 ? 0 : count - 1;
  return (current + delta + count) % count;
}