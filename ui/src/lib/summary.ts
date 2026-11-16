import type { ReaderData } from './reader';

const STOP_WORDS = new Set([
  'the', 'and', 'for', 'that', 'this', 'with', 'from', 'have', 'has', 'had',
  'are', 'was', 'were', 'will', 'would', 'can', 'could', 'not', 'but', 'you',
  'your', 'they', 'their', 'them', 'its', 'our', 'out', 'about', 'into', 'than',
  'then', 'when', 'where', 'which', 'while', 'what', 'who', 'why', 'how', 'all',
  'any', 'each', 'more', 'most', 'some', 'such', 'only', 'also', 'just', 'been',
  'being', 'because', 'before', 'after', 'over', 'under', 'between', 'through',
  '的', '了', '和', '是', '在', '我', '有', '就', '不', '人', '都', '一', '一个',
  '上', '也', '很', '到', '说', '要', '去', '你', '会', '着', '没有', '看', '好',
  '自己', '这', '那', '他', '她', '它', '我们', '你们', '他们', '可以', '以及',
  '但是', '因为', '所以', '如果', '而且', '或者', '不是', '这个', '那个', '这些',
]);

function sentencesOf(text: string): string[] {
  const pieces = text.match(/[^。！？!?；;\n]+[。！？!?；;]?/g) ?? [];
  return pieces
    .map((sentence) => sentence.trim())
    .filter((sentence) => sentence.length >= 24 && sentence.length <= 420);
}

function wordCounts(text: string, locale: string): Map<string, number> {
  const counts = new Map<string, number>();
  let words: string[] = [];
  try {
    const segmenter = new Intl.Segmenter(locale, { granularity: 'word' });
    for (const part of segmenter.segment(text.toLowerCase())) {
      if (part.isWordLike) words.push(part.segment);
    }
  } catch {
    words = text.toLowerCase().split(/\s+/);
  }
  for (const word of words) {
    if (word.length < 2 || STOP_WORDS.has(word) || /^\d+$/.test(word)) continue;
    counts.set(word, (counts.get(word) ?? 0) + 1);
  }
  return counts;
}

/**
 * Extractive summary, entirely on this device. It scores sentences by word
 * frequency and position, then returns a small number of sentences in their
 * original order. No model, network request or page HTML is involved.
 */
export function summarize(data: ReaderData, limit = 4): string[] {
  const locale = data.lang || (typeof navigator !== 'undefined' ? navigator.language : 'en');
  const entries: Array<{ text: string; index: number; block: number }> = [];
  let sequence = 0;
  for (let blockIndex = 0; blockIndex < data.blocks.length; blockIndex += 1) {
    const block = data.blocks[blockIndex];
    if (block.type !== 'p' && block.type !== 'quote' && block.type !== 'li') continue;
    for (const text of sentencesOf(block.text ?? '')) {
      entries.push({ text, index: sequence++, block: blockIndex });
    }
  }
  if (entries.length === 0) return [];

  const counts = wordCounts(entries.map((entry) => entry.text).join(' '), locale);
  const maxFrequency = Math.max(1, ...counts.values());
  const scored = entries.map((entry) => {
    const words = wordCounts(entry.text, locale);
    let score = 0;
    for (const [word, count] of words) {
      score += ((counts.get(word) ?? 0) / maxFrequency) * count;
    }
    score /= Math.sqrt(Math.max(8, entry.text.length));
    if (entry.index < 3) score += 0.22 / (entry.index + 1);
    if (/[：:].{12,}/.test(entry.text)) score -= 0.08;
    return { ...entry, score };
  });

  const tokenSets = new Map<string, Set<string>>();
  const tokens = (text: string): Set<string> => {
    const cached = tokenSets.get(text);
    if (cached) return cached;
    const set = new Set<string>();
    try {
      const segmenter = new Intl.Segmenter(locale, { granularity: 'word' });
      for (const part of segmenter.segment(text.toLowerCase())) {
        if (part.isWordLike && part.segment.length > 1) set.add(part.segment);
      }
    } catch {
      for (const word of text.toLowerCase().split(/\s+/)) {
        if (word.length > 1) set.add(word);
      }
    }
    if (set.size === 0) {
      for (let i = 0; i + 2 <= text.length; i += 2) set.add(text.slice(i, i + 2));
    }
    tokenSets.set(text, set);
    return set;
  };
  const similar = (a: string, b: string): boolean => {
    const left = tokens(a);
    const right = tokens(b);
    if (left.size === 0 || right.size === 0) return false;
    let shared = 0;
    for (const token of left) if (right.has(token)) shared += 1;
    return shared / (left.size + right.size - shared) > 0.68;
  };

  const chosen: typeof scored = [];
  const perBlock = new Map<number, number>();
  let total = 0;
  for (const entry of [...scored].sort((a, b) => b.score - a.score)) {
    if (chosen.length >= limit) break;
    if ((perBlock.get(entry.block) ?? 0) >= 2) continue;
    if (chosen.some((existing) => similar(existing.text, entry.text))) continue;
    if (total + entry.text.length > 900) continue;
    chosen.push(entry);
    perBlock.set(entry.block, (perBlock.get(entry.block) ?? 0) + 1);
    total += entry.text.length;
  }
  return chosen.sort((a, b) => a.index - b.index).map((entry) => entry.text);
}
