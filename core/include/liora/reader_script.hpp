#pragma once

namespace liora {

// Extracts an article-like document from the current page. The script is
// deliberately self-contained: WebKit evaluates it and returns the JSON string,
// while CEF runs it and reports the same JSON through console.log. It returns
// only text blocks and absolute image URLs, so the reader view never renders
// arbitrary page HTML.
inline constexpr const char* kReaderScript = R"LIORAJS(
(() => {
  const clean = (value) => (value || '').replace(/\s+/g, ' ').trim();
  const absolute = (value) => {
    try {
      const url = new URL(value, document.baseURI);
      return (url.protocol === 'http:' || url.protocol === 'https:') ? url.href : '';
    } catch (_) {
      return '';
    }
  };
  const meta = (selector) => {
    const node = document.querySelector(selector);
    return node ? clean(node.getAttribute('content') || node.textContent || '') : '';
  };

  const junk = [
    'script', 'style', 'noscript', 'iframe', 'svg', 'canvas', 'video', 'audio',
    'nav', 'header', 'footer', 'aside', 'form', 'button', 'select', 'textarea',
    '[aria-hidden="true"]', '[hidden]',
    '.ad', '.ads', '.advert', '.advertisement', '.banner', '.promo', '.sponsor',
    '.share', '.sharing', '.social', '.comments', '.comment', '.related',
    '.sidebar', '.newsletter', '.subscribe', '.breadcrumb', '.menu', '.nav',
    '.footer', '.header', '.cookie', '.popup', '.modal', '.toolbar', '.meta',
  ].join(',');

  const score = (node) => {
    if (!node) return 0;
    const text = clean(node.textContent).length;
    const paragraphs = node.querySelectorAll('p').length;
    const links = node.querySelectorAll('a').length;
    return text + paragraphs * 90 - links * 6;
  };

  const selectors = [
    'article', '[itemprop="articleBody"]', '[role="main"]', 'main',
    '.post-content', '.article-content', '.entry-content', '.article-body',
    '.post-body', '.story-body', '#article', '#content',
  ];
  let candidate = null;
  let best = 0;
  for (const selector of selectors) {
    for (const node of document.querySelectorAll(selector)) {
      const value = score(node);
      if (value > best) {
        best = value;
        candidate = node;
      }
    }
  }
  if (!candidate || best < 260) candidate = document.body;

  const clone = candidate.cloneNode(true);
  clone.querySelectorAll(junk).forEach((node) => node.remove());

  const blocks = [];
  const push = (block) => {
    if (block && block.text !== undefined && !block.text) return;
    if (blocks.length < 240) blocks.push(block);
  };

  const walk = (node) => {
    if (!node) return;
    if (node.nodeType === Node.TEXT_NODE) return;
    if (node.nodeType !== Node.ELEMENT_NODE) return;
    const tag = node.tagName.toLowerCase();
    if (tag === 'h1' || tag === 'h2' || tag === 'h3') {
      const text = clean(node.textContent);
      if (text.length >= 2) push({ type: 'h', level: Number(tag[1]), text });
      return;
    }
    if (tag === 'p') {
      const text = clean(node.textContent);
      if (text.length >= 20) push({ type: 'p', text });
      return;
    }
    if (tag === 'blockquote') {
      const text = clean(node.textContent);
      if (text.length >= 20) push({ type: 'quote', text });
      return;
    }
    if (tag === 'pre') {
      const text = (node.textContent || '').replace(/\s+$/g, '');
      if (text.length >= 4) push({ type: 'pre', text: text.slice(0, 8000) });
      return;
    }
    if (tag === 'li') {
      const text = clean(node.textContent);
      if (text.length >= 8) push({ type: 'li', text });
      return;
    }
    if (tag === 'img') {
      const src = absolute(node.getAttribute('src') || node.getAttribute('data-src') || '');
      if (src) push({ type: 'img', src, alt: clean(node.getAttribute('alt') || '') });
      return;
    }
    for (const child of node.childNodes) walk(child);
  };

  walk(clone);

  // Some sites wrap every paragraph in divs instead of p tags.
  if (blocks.length < 4) {
    const texts = [];
    clone.querySelectorAll('div, section').forEach((node) => {
      if (node.querySelector('p, h1, h2, h3, li, blockquote, img, pre')) return;
      const text = clean(node.textContent);
      if (text.length >= 80) texts.push(text);
    });
    const seen = new Set(blocks.map((block) => block.text));
    for (const text of texts) {
      if (!seen.has(text)) {
        seen.add(text);
        push({ type: 'p', text });
      }
    }
  }

  const textLength = blocks.reduce((sum, block) => sum + (block.text || '').length, 0);
  if (textLength < 260 || blocks.length < 2) {
    return JSON.stringify({ error: 'no-article' });
  }

  const payload = {
    version: 1,
    url: location.href,
    title: meta('meta[property="og:title"]') || clean(document.title) || clean(document.querySelector('h1')?.textContent) || location.hostname,
    byline: meta('meta[name="author"]') || clean(document.querySelector('[rel="author"], .byline, .author, [class*="byline"]')?.textContent),
    site: meta('meta[property="og:site_name"]') || location.hostname.replace(/^www\./, ''),
    excerpt: meta('meta[name="description"]') || meta('meta[property="og:description"]'),
    lang: document.documentElement.lang || '',
    length: textLength,
    blocks,
  };
  const json = JSON.stringify(payload);
  try { console.log('__LIORA_READER__' + json); } catch (_) {}
  return json;
})()
)LIORAJS";

}  // namespace liora
