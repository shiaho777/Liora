<script lang="ts">
  import Icon from './Icon.svelte';
  import { call } from './bridge';
  import { t } from './i18n.svelte';

  interface Props {
    kind: 'bookmarks' | 'history';
    onnavigate?: (url: string) => void;
  }

  let { kind, onnavigate }: Props = $props();

  interface BookmarkNode {
    id: number;
    type: 'folder' | 'url';
    title: string;
    url?: string;
    children?: BookmarkNode[];
  }

  interface HistoryItem {
    url: string;
    title: string;
    visitCount: number;
    lastVisit: number;
  }

  let tree = $state<BookmarkNode | null>(null);
  let history = $state<HistoryItem[]>([]);
  let query = $state('');
  let loading = $state(true);

  async function load(): Promise<void> {
    loading = true;
    try {
      if (kind === 'bookmarks') {
        const roots = await call<Record<string, BookmarkNode>>('bookmarks.tree');
        tree = roots.other ?? roots.bar ?? null;
      } else {
        history = await call<HistoryItem[]>('history.search', { query, limit: 200 });
      }
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    void kind;
    void load();
  });

  let bookmarkCount = $derived((function count(node: BookmarkNode | null): number {
    if (!node) return 0;
    if (node.type === 'url') return 1;
    return (node.children ?? []).reduce((sum, child) => sum + count(child), 0);
  })(tree));

  function flatten(node: BookmarkNode | null, out: BookmarkNode[] = []): BookmarkNode[] {
    if (!node) return out;
    if (node.type === 'url') out.push(node);
    for (const child of node.children ?? []) flatten(child, out);
    return out;
  }

  let items = $derived(flatten(tree));

  function when(ms: number): string {
    if (!ms) return '';
    const diff = Date.now() - ms;
    const day = 86400000;
    if (diff < day) return new Date(ms).toLocaleTimeString();
    if (diff < day * 7) return new Date(ms).toLocaleDateString(undefined, { weekday: 'short' });
    return new Date(ms).toLocaleDateString();
  }

  function host(url: string): string {
    try {
      return new URL(url).hostname.replace(/^www\./, '');
    } catch {
      return url;
    }
  }
</script>

<div class="library">
  <header class="library__header">
    <h1>{kind === 'bookmarks' ? t('nav.bookmarks') : t('nav.history')}</h1>
    <div class="library__count">
      {#if kind === 'bookmarks'}
        {bookmarkCount} {t('data.count')}
      {:else}
        {history.length} {t('data.count')}
      {/if}
    </div>
    <div class="library__actions">
      {#if kind === 'history'}
        <input
          class="input library__search"
          bind:value={query}
          oninput={() => load()}
          placeholder={t('address.placeholder')}
        />
      {/if}
      <button class="icon-button" title={t('nav.reload')} onclick={() => load()}>
        <Icon name="refresh" />
      </button>
    </div>
  </header>

  <div class="library__body">
    {#if loading}
      <div class="empty">…</div>
    {:else if kind === 'bookmarks'}
      {#if items.length === 0}
        <div class="empty">{t('rules.empty')}</div>
      {:else}
        {#each items as item (item.id)}
          <button class="entry" onclick={() => item.url && onnavigate?.(item.url)}>
            <span class="entry__icon"><Icon name="star" size={12} /></span>
            <span class="entry__main">
              <span class="entry__title">{item.title || item.url}</span>
              <span class="entry__url mono">{item.url}</span>
            </span>
          </button>
        {/each}
      {/if}
    {:else if history.length === 0}
      <div class="empty">{t('data.backup.empty')}</div>
    {:else}
      {#each history as item (item.url)}
        <button class="entry" onclick={() => onnavigate?.(item.url)}>
          <span class="entry__icon"><Icon name="globe" size={12} /></span>
          <span class="entry__main">
            <span class="entry__title">{item.title || host(item.url)}</span>
            <span class="entry__url mono">{item.url}</span>
          </span>
          <span class="entry__time">{when(item.lastVisit)}</span>
        </button>
      {/each}
    {/if}
  </div>
</div>

<style>
  .library {
    height: 100%;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .library__header {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 18px 22px 12px;
    border-bottom: 1px solid var(--border);
  }

  .library__header h1 {
    margin: 0;
    font-size: 17px;
    font-weight: 650;
    letter-spacing: -0.01em;
  }

  .library__count {
    font-size: 12px;
    color: var(--text-muted);
  }

  .library__actions {
    margin-left: auto;
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .library__search {
    width: 220px;
    height: 30px;
  }

  .library__body {
    flex: 1;
    overflow: auto;
    padding: 8px 14px 28px;
  }

  .entry {
    display: flex;
    align-items: center;
    gap: 10px;
    width: 100%;
    padding: 9px 10px;
    border-radius: var(--radius-sm);
    text-align: left;
  }

  .entry:hover {
    background: var(--bg-hover);
  }

  .entry__icon {
    width: 26px;
    height: 26px;
    flex: none;
    border-radius: 8px;
    display: grid;
    place-items: center;
    background: var(--bg-sunken);
    color: var(--text-muted);
  }

  .entry__main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
  }

  .entry__title {
    font-size: 13px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .entry__url {
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .entry__time {
    flex: none;
    font-size: 11.5px;
    color: var(--text-muted);
  }
</style>
