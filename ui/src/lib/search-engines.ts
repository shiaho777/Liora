// Pure helpers for search engines (built-in + user-defined). Kept free of
// Svelte so validation and URL building can be unit-tested.

export interface CustomEngine {
  id: string;
  name: string;
  /** Search URL with a %s placeholder for the query. */
  template: string;
}

export const BUILTIN_ENGINES: Array<{ id: string; name: string; template: string }> = [
  { id: 'duckduckgo', name: 'DuckDuckGo', template: 'https://duckduckgo.com/?q=%s' },
  { id: 'google', name: 'Google', template: 'https://www.google.com/search?q=%s' },
  { id: 'bing', name: 'Bing', template: 'https://www.bing.com/search?q=%s' },
  { id: 'brave', name: 'Brave Search', template: 'https://search.brave.com/search?q=%s' },
  { id: 'startpage', name: 'Startpage', template: 'https://www.startpage.com/do/search?q=%s' },
  { id: 'baidu', name: '百度', template: 'https://www.baidu.com/s?wd=%s' },
  { id: 'sogou', name: '搜狗', template: 'https://www.sogou.com/web?query=%s' },
];

const BUILTIN_BY_ID = new Map(BUILTIN_ENGINES.map((engine) => [engine.id, engine]));

/** Resolves the active engine's display name, falling back to the id. */
export function engineName(engineId: string, custom: CustomEngine[]): string {
  const builtin = BUILTIN_BY_ID.get(engineId);
  if (builtin) return builtin.name;
  const found = custom.find((engine) => engine.id === engineId);
  return found ? found.name : engineId;
}

/** Resolves the active engine's URL template, falling back to DuckDuckGo. */
export function engineTemplate(engineId: string, custom: CustomEngine[]): string {
  const builtin = BUILTIN_BY_ID.get(engineId);
  if (builtin) return builtin.template;
  const found = custom.find((engine) => engine.id === engineId);
  return found ? found.template : BUILTIN_ENGINES[0].template;
}

/** Builds the search URL for a query using the active engine. */
export function buildSearchUrl(engineId: string, custom: CustomEngine[], query: string): string {
  const template = engineTemplate(engineId, custom);
  const encoded = encodeURIComponent(query);
  // Replace every %s so templates with multiple placeholders still work.
  return template.split('%s').join(encoded);
}

export interface EngineValidation {
  ok: boolean;
  error?: 'name' | 'template' | 'placeholder' | 'scheme';
}

/** Validates a custom engine's name and template before saving. */
export function validateEngine(name: string, template: string): EngineValidation {
  if (!name.trim()) return { ok: false, error: 'name' };
  const trimmed = template.trim();
  if (!trimmed) return { ok: false, error: 'template' };
  if (!trimmed.includes('%s')) return { ok: false, error: 'placeholder' };
  if (!/^https?:\/\//i.test(trimmed)) return { ok: false, error: 'scheme' };
  return { ok: true };
}

/** Generates a stable id for a new custom engine. */
export function newEngineId(existing: CustomEngine[]): string {
  let id = '';
  do {
    id = `custom-${Date.now().toString(36)}-${Math.floor(Math.random() * 1e6).toString(36)}`;
  } while (existing.some((engine) => engine.id === id));
  return id;
}

/** Parses the stored setting value into a typed list, tolerating bad data. */
export function parseCustomEngines(raw: unknown): CustomEngine[] {
  if (!Array.isArray(raw)) return [];
  const out: CustomEngine[] = [];
  for (const item of raw) {
    if (!item || typeof item !== 'object') continue;
    const record = item as Record<string, unknown>;
    if (typeof record.id !== 'string' || typeof record.name !== 'string' || typeof record.template !== 'string') {
      continue;
    }
    if (!record.name.trim() || !record.template.includes('%s')) continue;
    out.push({ id: record.id, name: record.name, template: record.template });
  }
  return out;
}