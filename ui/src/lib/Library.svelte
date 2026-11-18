<script lang="ts">
  import EmptyState from './EmptyState.svelte';
  import Icon from './Icon.svelte';
  import LoadingState from './LoadingState.svelte';
  import Segmented from './Segmented.svelte';
  import { call, onNotification } from './bridge';
  import { toast } from './state.svelte';
  import { t } from './i18n.svelte';
  import { countBookmarks, filterTree, folderOptions, type BookmarkNode } from './bookmark-tree';

  interface Props {
    kind: 'bookmarks' | 'history';
    onnavigate?: (url: string) => void;
    onopen?: (page: string) => void;
  }

  let { kind, onnavigate, onopen }: Props = $props();

  interface HistoryItem {
    url: string;
    title: string;
    visitCount: number;
    lastVisit: number;
  }

  let roots = $state<BookmarkNode[]>([]);
  let history = $state<HistoryItem[]>([]);
  let query = $state('');
  let loading = $state(true);
  let collapsed = $state<Set<number>>(new Set());
  let editing = $state<{ id: number; title: string; url?: string } | null>(null);
  let moving = $state<{ id: number; parent: number } | null>(null);
  let creating = $state<{ parent: number; title: string } | null>(null);

  async function load(): Promise<void> {
    loading = true;
    try {
      if (kind === 'bookmarks') {
        const tree = await call<Record<string, BookmarkNode>>('bookmarks.tree');
        roots = [tree.bar, tree.other, tree.mobile].filter(Boolean);
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

  onNotification((message) => {
    if (message.method === 'bookmarks.changed' && kind === 'bookmarks') void load();
  });

  const bookmarkCount = $derived(countBookmarks(roots));
  const visibleRoots = $derived(filterTree(roots, query));

  function toggleFolder(id: number): void {
    const next = new Set(collapsed);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    collapsed = next;
  }

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

  async function saveEdit(): Promise<void> {
    if (!editing) return;
    const title = editing.title.trim();
    if (!title) return;
    try {
      await call('bookmarks.update', {
        id: editing.id,
        title,
        ...(editing.url !== undefined ? { url: editing.url.trim() } : {}),
      });
      editing = null;
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  async function saveMove(): Promise<void> {
    if (!moving) return;
    try {
      await call('bookmarks.move', { id: moving.id, parent: moving.parent });
      moving = null;
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  async function saveFolder(): Promise<void> {
    if (!creating) return;
    const title = creating.title.trim();
    if (!title) return;
    try {
      await call('bookmarks.addFolder', { parent: creating.parent, title });
      creating = null;
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
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

{#snippet bookmarkNodes(nodes: BookmarkNode[], depth: number)}
  {#each nodes as node (node.id)}
    {#if node.type === 'folder'}
      <div class="folder" style:--depth={depth}>
        <div
          class="folder__row"
          role="button"
          tabindex="0"
          aria-expanded={!collapsed.has(node.id) || Boolean(query.trim())}
          onclick={() => toggleFolder(node.id)}
          onkeydown={(event) => {
            if (event.key === 'Enter' || event.key === ' ') {
              event.preventDefault();
              toggleFolder(node.id);
            }
          }}
        >
          <span class="folder__chevron" class:folder__chevron--open={!collapsed.has(node.id) || Boolean(query.trim())}>
            <Icon name="chevron" size={12} />
          </span>
          <span class="folder__icon"><Icon name="folder" size={13} /></span>
          <span class="folder__title">{node.title}</span>
          <span class="folder__count">{countBookmarks(node.children ?? [])}</span>
          <span class="folder__actions">
            <button
              class="icon-button"
              title={t('bookmarks.newFolderHere')}
              aria-label={t('bookmarks.newFolderHere')}
              onclick={(event) => {
                event.stopPropagation();
                creating = { parent: node.id, title: '' };
              }}
            >
              <Icon name="plus" size={13} />
            </button>
            {#if node.id > 3}
              <button
                class="icon-button"
                title={t('bookmarks.rename')}
                aria-label={t('bookmarks.rename')}
                onclick={(event) => {
                  event.stopPropagation();
                  editing = { id: node.id, title: node.title };
                }}
              >
                <Icon name="pencil" size={13} />
              </button>
              <button
                class="icon-button"
                title={t('bookmarks.moveTo')}
                aria-label={t('bookmarks.moveTo')}
                onclick={(event) => {
                  event.stopPropagation();
                  moving = { id: node.id, parent: node.id };
                }}
              >
                <Icon name="external" size={13} />
              </button>
              <button
                class="icon-button"
                title={t('bookmarks.remove')}
                aria-label={t('bookmarks.remove')}
                onclick={(event) => {
                  event.stopPropagation();
                  void removeBookmark(node.id);
                }}
              >
                <Icon name="trash" size={13} />
              </button>
            {/if}
          </span>
        </div>
        {#if !collapsed.has(node.id) || query.trim()}
          {@render bookmarkNodes(node.children ?? [], depth + 1)}
        {/if}
      </div>
    {:else}
      <div
        class="entry entry--bookmark"
        style:--depth={depth}
        role="button"
        tabindex="0"
        onclick={() => node.url && onnavigate?.(node.url)}
        onkeydown={(event) => {
          if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            node.url && onnavigate?.(node.url);
          }
        }}
      >
        <span class="entry__icon entry__icon--star"><Icon name="star" size={12} /></span>
        <span class="entry__main">
          <span class="entry__title">{node.title || node.url}</span>
          <span class="entry__url mono">{node.url}</span>
        </span>
        <span class="entry__actions">
          <button
            class="icon-button"
            title={t('bookmarks.rename')}
            aria-label={t('bookmarks.rename')}
            onclick={(event) => {
              event.stopPropagation();
              editing = { id: node.id, title: node.title, url: node.url };
            }}
          >
            <Icon name="pencil" size={13} />
          </button>
          <button
            class="icon-button"
            title={t('bookmarks.moveTo')}
            aria-label={t('bookmarks.moveTo')}
            onclick={(event) => {
              event.stopPropagation();
              moving = { id: node.id, parent: node.id };
            }}
          >
            <Icon name="external" size={13} />
          </button>
          <button
            class="icon-button"
            title={t('bookmarks.remove')}
            aria-label={t('bookmarks.remove')}
            onclick={(event) => {
              event.stopPropagation();
              void removeBookmark(node.id);
            }}
          >
            <Icon name="trash" size={13} />
          </button>
        </span>
      </div>
    {/if}
  {/each}
{/snippet}

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
      {#if kind === 'bookmarks'}
        <input
          class="input library__search"
          bind:value={query}
          placeholder={t('bookmarks.search')}
          aria-label={t('bookmarks.search')}
        />
        <button
          class="button button--sm"
          onclick={() => (creating = { parent: 2, title: '' })}
        >
          <Icon name="plus" size={13} />
          {t('bookmarks.newFolder')}
        </button>
      {:else}
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
      {#if bookmarkCount === 0}
        <EmptyState
          icon="star"
          title={t('bookmarks.empty')}
          body={t('bookmarks.emptyHint')}
          actionLabel={t('bookmarks.emptyAction')}
          onaction={() => onopen?.('newtab')}
        />
      {:else if visibleRoots.length === 0}
        <EmptyState icon="search" title={t('bookmarks.noResults')} body={t('bookmarks.noResultsHint')} />
      {:else}
        <div class="tree">
          {@render bookmarkNodes(visibleRoots, 0)}
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

  {#if editing}
    <div class="dialog-backdrop" role="presentation" onclick={() => (editing = null)}>
      <div
        class="dialog"
        role="dialog"
        tabindex="-1"
        aria-modal="true"
        aria-label={t('bookmarks.rename')}
        onclick={(event) => event.stopPropagation()}
        onkeydown={(event) => {
          if (event.key === 'Escape') editing = null;
          if (event.key === 'Enter') void saveEdit();
        }}
      >
        <div class="dialog__title">{t('bookmarks.rename')}</div>
        <label class="dialog__field">
          <span class="dialog__label">{t('bookmarks.titleLabel')}</span>
          <input class="input" bind:value={editing.title} />
        </label>
        {#if editing.url !== undefined}
          <label class="dialog__field">
            <span class="dialog__label">{t('bookmarks.urlLabel')}</span>
            <input class="input input--mono" bind:value={editing.url} />
          </label>
        {/if}
        <div class="dialog__actions">
          <button class="button button--ghost button--sm" onclick={() => (editing = null)}>
            {t('kernels.cancel')}
          </button>
          <button class="button button--primary button--sm" onclick={() => void saveEdit()}>
            {t('kernels.kitesurfEndpointSave')}
          </button>
        </div>
      </div>
    </div>
  {/if}

  {#if moving}
    <div class="dialog-backdrop" role="presentation" onclick={() => (moving = null)}>
      <div
        class="dialog"
        role="dialog"
        tabindex="-1"
        aria-modal="true"
        aria-label={t('bookmarks.moveTo')}
        onclick={(event) => event.stopPropagation()}
        onkeydown={(event) => {
          if (event.key === 'Escape') moving = null;
        }}
      >
        <div class="dialog__title">{t('bookmarks.moveTo')}</div>
        <div class="dialog__folders" role="radiogroup" aria-label={t('bookmarks.moveTo')}>
          {#each folderOptions(roots, moving.id) as folder (folder.id)}
            <button
              class="dialog__folder"
              class:dialog__folder--active={moving.parent === folder.id}
              role="radio"
              aria-checked={moving.parent === folder.id}
              style:padding-left={`${10 + folder.depth * 16}px`}
              onclick={() => (moving!.parent = folder.id)}
            >
              <Icon name="folder" size={13} />
              {folder.title}
            </button>
          {/each}
        </div>
        <div class="dialog__actions">
          <button class="button button--ghost button--sm" onclick={() => (moving = null)}>
            {t('kernels.cancel')}
          </button>
          <button class="button button--primary button--sm" onclick={() => void saveMove()}>
            {t('bookmarks.moveAction')}
          </button>
        </div>
      </div>
    </div>
  {/if}

  {#if creating}
    <div class="dialog-backdrop" role="presentation" onclick={() => (creating = null)}>
      <div
        class="dialog"
        role="dialog"
        tabindex="-1"
        aria-modal="true"
        aria-label={t('bookmarks.newFolder')}
        onclick={(event) => event.stopPropagation()}
        onkeydown={(event) => {
          if (event.key === 'Escape') creating = null;
          if (event.key === 'Enter') void saveFolder();
        }}
      >
        <div class="dialog__title">{t('bookmarks.newFolder')}</div>
        <label class="dialog__field">
          <span class="dialog__label">{t('bookmarks.titleLabel')}</span>
          <input class="input" bind:value={creating.title} placeholder={t('bookmarks.folderPlaceholder')} />
        </label>
        <div class="dialog__actions">
          <button class="button button--ghost button--sm" onclick={() => (creating = null)}>
            {t('kernels.cancel')}
          </button>
          <button class="button button--primary button--sm" onclick={() => void saveFolder()}>
            {t('bookmarks.createAction')}
          </button>
        </div>
      </div>
    </div>
  {/if}
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

  .tree {
    max-width: 860px;
    margin: 0 auto;
    border: 1px solid var(--border);
    border-radius: var(--radius-md);
    background: var(--surface);
    box-shadow: var(--shadow-xs);
    padding: 6px;
  }

  .folder__row {
    display: flex;
    align-items: center;
    gap: 8px;
    min-height: 38px;
    padding: 4px 8px;
    padding-left: calc(8px + var(--depth, 0) * 18px);
    border-radius: var(--radius-sm);
    cursor: pointer;
    transition: background var(--fast) var(--ease);
  }

  .folder__row:hover {
    background: var(--bg-hover);
  }

  .folder__chevron {
    flex: none;
    display: grid;
    place-items: center;
    color: var(--text-muted);
    transition: transform var(--fast) var(--ease);
  }

  .folder__chevron--open {
    transform: rotate(90deg);
  }

  .folder__icon {
    flex: none;
    display: grid;
    place-items: center;
    color: var(--accent);
  }

  .folder__title {
    flex: 1;
    min-width: 0;
    font-size: var(--text-sm);
    font-weight: var(--weight-semibold);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .folder__count {
    flex: none;
    font-size: var(--text-2xs);
    color: var(--text-muted);
  }

  .folder__actions,
  .entry__actions {
    flex: none;
    display: flex;
    gap: 2px;
    opacity: 0;
    transition: opacity var(--fast) var(--ease);
  }

  .folder__row:hover .folder__actions,
  .entry:hover .entry__actions {
    opacity: 1;
  }

  .entry--bookmark {
    padding-left: calc(12px + var(--depth, 0) * 18px);
    border-radius: var(--radius-sm);
  }

  .entry--bookmark + .entry--bookmark {
    border-top: 0;
  }

  .dialog-backdrop {
    position: fixed;
    inset: 0;
    z-index: 80;
    display: grid;
    place-items: center;
    background: rgba(8, 10, 14, 0.46);
    backdrop-filter: blur(5px);
    animation: fade-in var(--fast) var(--ease-out);
  }

  .dialog {
    width: min(420px, calc(100vw - 48px));
    background: var(--surface-strong);
    border: 1px solid var(--border-strong);
    border-radius: var(--radius-lg);
    box-shadow: var(--shadow-lg);
    padding: 18px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    animation: dialog-in var(--normal) var(--ease-out);
  }

  @keyframes fade-in {
    from { opacity: 0; }
    to { opacity: 1; }
  }

  @keyframes dialog-in {
    from { opacity: 0; transform: translateY(6px) scale(0.98); }
    to { opacity: 1; transform: none; }
  }

  .dialog__title {
    font-size: var(--text-md);
    font-weight: var(--weight-semibold);
  }

  .dialog__field {
    display: flex;
    flex-direction: column;
    gap: 5px;
  }

  .dialog__label {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .dialog__folders {
    max-height: 260px;
    overflow: auto;
    display: flex;
    flex-direction: column;
    gap: 2px;
    border: 1px solid var(--border);
    border-radius: var(--radius-md);
    padding: 5px;
  }

  .dialog__folder {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 7px 10px;
    border: 0;
    border-radius: var(--radius-sm);
    background: transparent;
    color: var(--text);
    font-size: var(--text-sm);
    text-align: left;
    cursor: pointer;
  }

  .dialog__folder:hover {
    background: var(--bg-hover);
  }

  .dialog__folder--active {
    background: var(--accent-soft);
    color: var(--accent-text);
  }

  .dialog__actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
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
