<script lang="ts">
  import EmptyState from './EmptyState.svelte';
  import Icon from './Icon.svelte';
  import LoadingState from './LoadingState.svelte';
  import Segmented from './Segmented.svelte';
  import { call } from './bridge';
  import { t } from './i18n.svelte';

  interface Props {
    kind: 'bookmarks' | 'history';
    onnavigate?: (url: string) => void;
    onopen?: (page: string) => void;
  }

  let { kind, onnavigate, onopen }: Props = $props();

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
        // Show every bookmark, not just one root folder.
        const children = [roots.bar, roots.other, roots.mobile]
          .filter(Boolean)
          .flatMap((root) => root.children ?? []);
        tree = { id: 0, type: 'folder', title: '', children };
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

  let bookmarkCount = $derived(
    (function count(node: BookmarkNode | null): number {
      if (!node) return 0;
      if (node.type === 'url') return 1;
      return (node.children ?? []).reduce((sum, child) => sum + count(child), 0);
    })(tree),
  );

  function flatten(node: BookmarkNode | null, out: BookmarkNode[] = []): BookmarkNode[] {
    if (!node) return out;
    if (node.type === 'url') out.push(node);
    for (const child of node.children ?? []) flatten(child, out);
    return out;
  }

  let items = $derived(flatten(tree));

  async function removeHistory(url: string): Promise<void> {
    await call('history.remove', { url });
    history = history.filter((item) => item.url !== url);
  }

  async function clearHistory(): Promise<void> {
    await call('history.clear', {});
    history = [];
  }

  async function removeBookmark(id: number): Promise<void> {
    await call('bookmarks.remove', { id });
    await load();
  }

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

  const tabs = $derived([
    { value: 'bookmarks', label: t('nav.bookmarks') },
    { value: 'history', label: t('nav.history') },
  ]);

  const count = $derived(kind === 'bookmarks' ? bookmarkCount : history.length);
</script>

<div class="library">
  <header class="library__header">
    <div class="library__heading">
      <span class="library__icon">
        <Icon name={kind === 'bookmarks' ? 'star' : 'history'} size={17} />
      </span>
      <div class="library__title-wrap">
        <h1 class="library__title">
          {kind === 'bookmarks' ? t('nav.bookmarks') : t('nav.history')}
        </h1>
        <div class="library__subtitle">{count} {t('data.count')}</div>
      </div>
    </div>

    <Segmented
      value={kind}
      options={tabs}
      ariaLabel={t('library.tabs')}
      onchange={(next) => onopen?.(next)}
    />

    <div class="library__actions">
      {#if kind === 'history'}
        <input
          class="input library__search"
          bind:value={query}
          oninput={() => load()}
          placeholder={t('history.search')}
          aria-label={t('history.search')}
        />
        {#if history.length > 0}
          <button class="button button--sm" onclick={clearHistory}>{t('history.clear')}</button>
        {/if}
      {/if}
      <button class="icon-button" title={t('nav.reload')} onclick={() => load()}>
        <Icon name="refresh" />
      </button>
    </div>
  </header>

  <div class="library__body">
    {#if loading}
      <LoadingState rows={4} />
    {:else if kind === 'bookmarks'}
      {#if items.length === 0}
        <EmptyState
          icon="star"
          title={t('bookmarks.empty')}
          body={t('bookmarks.emptyHint')}
          actionLabel={t('bookmarks.emptyAction')}
          onaction={() => onopen?.('newtab')}
        />
      {:else}
        <div class="entries">
          {#each items as item (item.id)}
            <div
              class="entry"
              role="button"
              tabindex="0"
              onclick={() => item.url && onnavigate?.(item.url)}
              onkeydown={(event) => {
                if (event.key === 'Enter' || event.key === ' ') {
                  event.preventDefault();
                  item.url && onnavigate?.(item.url);
                }
              }}
            >
              <span class="entry__icon entry__icon--star"><Icon name="star" size={12} /></span>
              <span class="entry__main">
                <span class="entry__title">{item.title || item.url}</span>
                <span class="entry__url mono">{item.url}</span>
              </span>
              <button
                class="icon-button entry__remove"
                title={t('bookmarks.remove')}
                aria-label={t('bookmarks.remove')}
                onclick={(event) => {
                  event.stopPropagation();
                  void removeBookmark(item.id);
                }}
              >
                <Icon name="trash" size={14} />
              </button>
            </div>
          {/each}
        </div>
      {/if}
    {:else if history.length === 0}
      <EmptyState
        icon="history"
        title={query.trim() ? t('history.noResults') : t('history.empty')}
        body={query.trim() ? t('history.noResultsHint') : t('history.emptyHint')}
      />
    {:else}
      <div class="entries">
        {#each history as item (item.url)}
          <div
            class="entry"
            role="button"
            tabindex="0"
            onclick={() => onnavigate?.(item.url)}
            onkeydown={(event) => {
              if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                onnavigate?.(item.url);
              }
            }}
          >
            <span class="entry__icon"><Icon name="globe" size={12} /></span>
            <span class="entry__main">
              <span class="entry__title">{item.title || host(item.url)}</span>
              <span class="entry__url mono">{item.url}</span>
            </span>
            <span class="entry__time">{when(item.lastVisit)}</span>
            <button
              class="icon-button entry__remove"
              title={t('rules.delete')}
              aria-label={t('rules.delete')}
              onclick={(event) => {
                event.stopPropagation();
                void removeHistory(item.url);
              }}
            >
              <Icon name="trash" size={14} />
            </button>
          </div>
        {/each}
      </div>
    {/if}
  </div>
</div>

<style>
  .library {
    height: 100%;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    background: var(--bg);
  }

  .library__header {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 18px 24px;
    border-bottom: 1px solid var(--border);
    background: var(--surface);
  }

  .library__heading {
    display: flex;
    align-items: center;
    gap: 10px;
    flex: 1;
    min-width: 0;
  }

  .library__icon {
    width: 36px;
    height: 36px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 10px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .library__title {
    margin: 0;
    font-size: var(--text-lg);
    font-weight: var(--weight-semibold);
    letter-spacing: -0.01em;
  }

  .library__subtitle {
    margin-top: 1px;
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .library__actions {
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .library__search {
    width: 210px;
    height: 30px;
  }

  .library__body {
    flex: 1;
    min-height: 0;
    overflow: auto;
    padding: 18px 24px 40px;
  }

  .entries {
    max-width: 860px;
    margin: 0 auto;
    border: 1px solid var(--border);
    border-radius: var(--radius-md);
    overflow: hidden;
    background: var(--surface);
    box-shadow: var(--shadow-xs);
  }

  .entry {
    display: flex;
    align-items: center;
    gap: 11px;
    min-height: 50px;
    padding: 8px 12px;
    cursor: pointer;
    transition: background var(--fast) var(--ease);
  }

  .entry + .entry {
    border-top: 1px solid var(--border);
  }

  .entry:hover {
    background: var(--bg-hover);
  }

  .entry__icon {
    width: 28px;
    height: 28px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: var(--radius-sm);
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .entry__icon--star {
    color: var(--accent);
    background: var(--accent-soft);
  }

  .entry__main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 1px;
  }

  .entry__title {
    font-size: var(--text-sm);
    font-weight: var(--weight-medium);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .entry__url {
    font-size: var(--text-2xs);
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .entry__time {
    flex: none;
    font-size: var(--text-2xs);
    color: var(--text-muted);
  }

  .entry__remove {
    opacity: 0;
    transition: opacity var(--fast) var(--ease);
  }

  .entry:hover .entry__remove {
    opacity: 1;
  }

  @media (max-width: 760px) {
    .library__header {
      flex-wrap: wrap;
      padding: 14px 16px;
    }

    .library__heading {
      width: 100%;
    }

    .library__actions {
      width: 100%;
    }

    .library__search {
      flex: 1;
      width: auto;
    }

    .library__body {
      padding: 16px;
    }

    .entry__remove {
      opacity: 1;
    }
  }
</style>
