export type ReaderBlockType = 'h' | 'p' | 'quote' | 'pre' | 'li' | 'img';

export interface ReaderBlock {
  type: ReaderBlockType;
  level?: number;
  text?: string;
  src?: string;
  alt?: string;
}

export interface ReaderData {
  version: number;
  url: string;
  title: string;
  byline: string;
  site: string;
  excerpt: string;
  lang: string;
  length: number;
  blocks: ReaderBlock[];
}

/**
 * Validates the JSON produced by liora/reader_script.hpp. Reader content is
 * rendered as text blocks and direct image URLs only, so a page cannot inject
 * markup into the browser interface.
 */
export function parseReaderData(raw: string): ReaderData | null {
  let value: unknown;
  try {
    value = JSON.parse(raw);
  } catch {
    return null;
  }
  if (!value || typeof value !== 'object') return null;
  const record = value as Record<string, unknown>;
  if (record.error) return null;
  const blocks: ReaderBlock[] = [];
  if (Array.isArray(record.blocks)) {
    for (const entry of record.blocks) {
      if (!entry || typeof entry !== 'object') continue;
      const block = entry as Record<string, unknown>;
      const type = block.type;
      if (type === 'img') {
        const src = typeof block.src === 'string' ? block.src : '';
        if (!/^https?:\/\//i.test(src)) continue;
        blocks.push({
          type: 'img',
          src,
          alt: typeof block.alt === 'string' ? block.alt : '',
        });
        continue;
      }
      if (type !== 'h' && type !== 'p' && type !== 'quote' && type !== 'pre' && type !== 'li') {
        continue;
      }
      const text = typeof block.text === 'string' ? block.text.trim() : '';
      if (!text) continue;
      blocks.push({
        type,
        level: type === 'h' ? Math.min(3, Math.max(1, Number(block.level) || 2)) : undefined,
        text: text.slice(0, 8000),
      });
    }
  }
  if (blocks.length === 0) return null;
  const title = typeof record.title === 'string' ? record.title.trim() : '';
  if (!title) return null;
  return {
    version: 1,
    url: typeof record.url === 'string' ? record.url : '',
    title: title.slice(0, 300),
    byline: typeof record.byline === 'string' ? record.byline.trim().slice(0, 200) : '',
    site: typeof record.site === 'string' ? record.site.trim().slice(0, 200) : '',
    excerpt: typeof record.excerpt === 'string' ? record.excerpt.trim().slice(0, 500) : '',
    lang: typeof record.lang === 'string' ? record.lang.slice(0, 20) : '',
    length: Number(record.length) || blocks.reduce((sum, block) => sum + (block.text ?? '').length, 0),
    blocks,
  };
}

export function readingMinutes(data: ReaderData): number {
  return Math.max(1, Math.round(data.length / 240));
}
