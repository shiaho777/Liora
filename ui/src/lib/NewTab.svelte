<script lang="ts">
  import { onMount } from 'svelte';
  import Icon from './Icon.svelte';
  import { call } from './bridge';
  import { t, i18n } from './i18n.svelte';
  import { app, toast } from './state.svelte';
  import { loadSettings, settingsStore, value, set } from './settings-store.svelte';
  import {
    addTile,
    isPinned,
    mergeTiles,
    parseTiles,
    removeTile,
    renameTile,
    type Tile,
  } from './start-tiles';

  interface Props {
    /** Host → data: URL, cached locally by the shell. Never a third party. */
    favicons?: Record<string, string>;
    onnavigate?: (url: string) => void;
    onopen?: (page: string) => void;
    onpanel?: (section: string) => void;
  }

  let { favicons = {}, onnavigate, onopen, onpanel }: Props = $props();

  interface Site {
    url: string;
    title: string;
    visitCount: number;
  }

  let query = $state('');
  let sites = $state<Site[]>([]);
  let suggestions = $state<Site[]>([]);
  let suggestEnabled = $state(true);
  let now = $state(new Date());
  let searchEl = $state<HTMLInputElement | null>(null);

  // Pinned tiles are user-managed; top sites fill the remaining slots.
  const pinned = $derived(parseTiles(value<unknown>('start.tiles')));
  const tiles = $derived(mergeTiles(pinned, sites, 8));
  let editingTiles = $state(false);
  let tileDialog = $state<{ mode: 'add' | 'edit'; url: string; title: string } | null>(null);

  async function saveTiles(next: Tile[]): Promise<void> {
    await set('start.tiles', next);
  }

  async function saveTileDialog(): Promise<void> {
    if (!tileDialog) return;
    const url = tileDialog.url.trim();
    if (!url) {
      toast(t('start.tiles.errUrl'), 'error');
      return;
    }
    const normalized = /^[a-z][a-z0-9+.-]*:\/\//i.test(url) ? url : `https://${url}`;
    if (tileDialog.mode === 'add') {
      await saveTiles(addTile(pinned, normalized, tileDialog.title));
    } else {
      // Editing an existing tile: rename it (URL stays the identity).
      await saveTiles(renameTile(pinned, tileDialog.url, tileDialog.title));
    }
    tileDialog = null;
  }

  // Only the minute matters for the clock; a 1s timer would wake the page 60x
  // more often than needed on a start page that can stay open for hours.
  onMount(() => {
    searchEl?.focus();
  });

  $effect(() => {
    const timer = setInterval(() => (now = new Date()), 30_000);
    return () => clearInterval(timer);
  });

  $effect(() => {
    call<Site[]>('history.top', { limit: 8 })
      .then((items) => (sites = Array.isArray(items) ? items.filter((item) => item.url) : []))
      .catch(() => (sites = []));
  });

  $effect(() => {
    const apply = (): void => {
      suggestEnabled = value<boolean>('search.suggest') !== false;
    };
    if (settingsStore.loaded) {
      apply();
      return;
    }
    loadSettings().then(apply).catch(() => {
      /* local suggestions stay enabled when settings cannot be read */
    });
  });

  $effect(() => {
    const text = query.trim();
    if (!text || !suggestEnabled) {
      suggestions = [];
      return;
    }
    let current = true;
    const timer = setTimeout(() => {
      call<Site[]>('history.suggest', { input: text, limit: 6 })
        .then((items) => {
          if (current) suggestions = Array.isArray(items) ? items : [];
        })
        .catch(() => {
          if (current) suggestions = [];
        });
    }, 80);
    return () => {
      current = false;
      clearTimeout(timer);
    };
  });

  function focusSearch(): void {
    searchEl?.focus();
  }

  function submit(event: Event): void {
    event.preventDefault();
    if (!query.trim()) return;
    onnavigate?.(query);
    query = '';
  }

  function host(url: string): string {
    try {
      return new URL(url).hostname.replace(/^www\./, '');
    } catch {
      return url;
    }
  }

  function letter(value: string): string {
    return value.trim().charAt(0).toUpperCase() || '·';
  }

  function faviconFor(url: string): string {
    try {
      return favicons[new URL(url).hostname] ?? '';
    } catch {
      return '';
    }
  }

  // A stable hue per host keeps the grid colourful without loading favicons
  // from a third party (which would leak the user's top sites).
  function hue(url: string): number {
    let hash = 0;
    for (const char of host(url)) hash = (hash * 31 + char.charCodeAt(0)) & 0xffff;
    return 20 + (hash % 300);
  }

  const greeting = $derived.by(() => {
    const hour = now.getHours();
    if (hour < 5) return t('start.greeting.night');
    if (hour < 12) return t('start.greeting.morning');
    if (hour < 18) return t('start.greeting.afternoon');
    return t('start.greeting.evening');
  });

  const time = $derived(
    now.toLocaleTimeString(i18n.locale === 'zh' ? 'zh-CN' : 'en-US', {
      hour: '2-digit',
      minute: '2-digit',
      hour12: false,
    }),
  );

  const date = $derived(
    new Intl.DateTimeFormat(i18n.locale === 'zh' ? 'zh-CN' : 'en-US', {
      month: 'long',
      day: 'numeric',
      weekday: 'long',
    }).format(now),
  );
</script>

<div class="start">
  <div class="start__shell">
    <header class="start__head">
      <div class="brand">
        <span class="brand__mark">L</span>
        <span class="brand__name">{t('app.name')}</span>
      </div>
      <div class="clock" aria-label={`${time} ${date}`}>
        <span class="clock__time">{time}</span>
        <span class="clock__date">{date}</span>
      </div>
    </header>

    <section class="hero">
      <h1 class="hero__greeting">{greeting}</h1>
      <p class="hero__subtitle">{t('start.subtitle')}</p>

      <form class="search" onsubmit={submit}>
        <span class="search__icon"><Icon name="search" size={17} /></span>
        <input
          bind:this={searchEl}
          bind:value={query}
          placeholder={t('newtab.hint')}
          spellcheck="false"
          autocomplete="off"
          aria-label={t('address.placeholder')}
        />
        {#if query}
          <kbd class="search__key">↵</kbd>
        {:else}
          <span class="search__hint">{t('start.enterHint')}</span>
        {/if}
      </form>

      {#if query.trim() && suggestions.length > 0}
        <div class="suggestions">
          {#each suggestions as item (item.url)}
            <button class="suggestion" onclick={() => onnavigate?.(item.url)} title={item.url}>
              <span class="suggestion__icon"><Icon name="history" size={13} /></span>
              <span class="suggestion__main">
                <span class="suggestion__title">{item.title || host(item.url)}</span>
                <span class="suggestion__url">{item.url}</span>
              </span>
              <span class="suggestion__arrow"><Icon name="chevron" size={12} /></span>
            </button>
          {/each}
          <div class="suggestions__foot">{t('start.localSuggestions')}</div>
        </div>
      {/if}
    </section>

    {#if !query.trim() && (tiles.length > 0 || editingTiles)}
      <section class="block">
        <div class="block__head">
          <h2 class="block__title">{t('start.frequent')}</h2>
          <div class="block__actions">
            <button
              class="text-button"
              class:text-button--active={editingTiles}
              onclick={() => (editingTiles = !editingTiles)}
            >
              <Icon name="pencil" size={12} />
              {editingTiles ? t('start.tiles.done') : t('start.tiles.edit')}
            </button>
            <button class="text-button" onclick={() => onopen?.('history')}>
              {t('start.allHistory')}
              <Icon name="chevron" size={12} />
            </button>
          </div>
        </div>
        <div class="sites">
          {#each tiles as tile (tile.url)}
            <div class="site-wrap">
              <button
                class="site"
                style:--site-hue={hue(tile.url)}
                onclick={() => !editingTiles && onnavigate?.(tile.url)}
                title={tile.url}
              >
                <span class="site__mark" class:site__mark--icon={Boolean(faviconFor(tile.url))}>
                  {#if faviconFor(tile.url)}
                    <img src={faviconFor(tile.url)} alt="" />
                  {:else}
                    {letter(tile.title || host(tile.url))}
                  {/if}
                </span>
                <span class="site__text">
                  <span class="site__title">{tile.title || host(tile.url)}</span>
                  <span class="site__host">{host(tile.url)}</span>
                </span>
                {#if isPinned(pinned, tile.url)}
                  <span class="site__pin" title={t('start.tiles.pinned')}><Icon name="star" size={11} /></span>
                {:else}
                  <span class="site__arrow"><Icon name="chevron" size={12} /></span>
                {/if}
              </button>
              {#if editingTiles}
                <span class="site__edit">
                  {#if isPinned(pinned, tile.url)}
                    <button
                      class="icon-button"
                      title={t('start.tiles.rename')}
                      aria-label={t('start.tiles.rename')}
                      onclick={() => (tileDialog = { mode: 'edit', url: tile.url, title: tile.title })}
                    >
                      <Icon name="pencil" size={12} />
                    </button>
                    <button
                      class="icon-button"
                      title={t('start.tiles.unpin')}
                      aria-label={t('start.tiles.unpin')}
                      onclick={() => void saveTiles(removeTile(pinned, tile.url))}
                    >
                      <Icon name="close" size={12} />
                    </button>
                  {:else}
                    <button
                      class="icon-button"
                      title={t('start.tiles.pin')}
                      aria-label={t('start.tiles.pin')}
                      onclick={() => void saveTiles(addTile(pinned, tile.url, tile.title))}
                    >
                      <Icon name="star" size={12} />
                    </button>
                  {/if}
                </span>
              {/if}
            </div>
          {/each}
          {#if editingTiles}
            <button
              class="site site--add"
              onclick={() => (tileDialog = { mode: 'add', url: '', title: '' })}
            >
              <span class="site__mark site__mark--add"><Icon name="plus" size={15} /></span>
              <span class="site__text">
                <span class="site__title">{t('start.tiles.add')}</span>
                <span class="site__host">{t('start.tiles.addHint')}</span>
              </span>
            </button>
          {/if}
        </div>
      </section>
    {:else if !query.trim()}
      <section class="welcome">
        <span class="welcome__glow" aria-hidden="true"></span>
        <span class="welcome__mark">L</span>
        <div class="welcome__main">
          <div class="welcome__eyebrow">{t('start.welcome.eyebrow')}</div>
          <h2 class="welcome__title">{t('start.welcome.title')}</h2>
          <p class="welcome__body">{t('start.welcome.body')}</p>
          <div class="welcome__actions">
            <button class="button button--primary" onclick={() => onpanel?.('migration')}>
              <Icon name="external" size={14} />
              {t('start.welcome.import')}
            </button>
            <button class="button button--ghost" onclick={focusSearch}>
              {t('start.welcome.start')}
            </button>
          </div>
          <div class="welcome__privacy">
            <Icon name="shield" size={12} />
            {t('start.welcome.privacy')}
          </div>
        </div>
      </section>

      <section class="block block--starter">
        <div class="block__head">
          <h2 class="block__title">{t('start.next')}</h2>
          <span class="block__hint">{t('start.nextHint')}</span>
        </div>
        <div class="starters">
          <button class="starter" onclick={() => onpanel?.('dns')}>
            <span class="starter__icon"><Icon name="shield" size={17} /></span>
            <span class="starter__title">{t('start.starter.dns')}</span>
            <span class="starter__desc">{t('start.starter.dnsDesc')}</span>
            <span class="starter__go"><Icon name="chevron" size={12} /></span>
          </button>
          <button class="starter" onclick={() => onopen?.('bookmarks')}>
            <span class="starter__icon"><Icon name="star" size={17} /></span>
            <span class="starter__title">{t('start.starter.library')}</span>
            <span class="starter__desc">{t('start.starter.libraryDesc')}</span>
            <span class="starter__go"><Icon name="chevron" size={12} /></span>
          </button>
          {#if app.mode === 'advanced'}
            <button class="starter" onclick={() => onpanel?.('kernels')}>
              <span class="starter__icon"><Icon name="puzzle" size={17} /></span>
              <span class="starter__title">{t('start.starter.kernel')}</span>
              <span class="starter__desc">{t('start.starter.kernelDesc')}</span>
              <span class="starter__go"><Icon name="chevron" size={12} /></span>
            </button>
          {/if}
        </div>
      </section>
    {/if}

    <footer class="start__foot">
      <span class="privacy">
        <Icon name="shield" size={12} />
        {t('start.privacy')}
      </span>
      <div class="foot-links">
        <button class="foot-link" onclick={() => onopen?.('bookmarks')}>
          <Icon name="star" size={12} />
          {t('nav.bookmarks')}
        </button>
        <button class="foot-link" onclick={() => onopen?.('history')}>
          <Icon name="history" size={12} />
          {t('nav.history')}
        </button>
        <button class="foot-link" onclick={() => onopen?.('watches')}>
          <Icon name="eye" size={12} />
          {t('nav.watches')}
        </button>
        <button class="foot-link" onclick={() => onpanel?.('appearance')}>
          <Icon name="sliders" size={12} />
          {t('nav.settings')}
        </button>
        <span class="version">v{app.version}</span>
      </div>
    </footer>
  </div>

  {#if tileDialog}
    <div class="tile-backdrop" role="presentation" onclick={() => (tileDialog = null)}>
      <div
        class="tile-dialog"
        role="dialog"
        tabindex="-1"
        aria-modal="true"
        aria-label={tileDialog.mode === 'add' ? t('start.tiles.add') : t('start.tiles.rename')}
        onclick={(event) => event.stopPropagation()}
        onkeydown={(event) => {
          if (event.key === 'Escape') tileDialog = null;
          if (event.key === 'Enter') void saveTileDialog();
        }}
      >
        <div class="tile-dialog__title">
          {tileDialog.mode === 'add' ? t('start.tiles.add') : t('start.tiles.rename')}
        </div>
        <label class="tile-dialog__field">
          <span class="tile-dialog__label">{t('start.tiles.url')}</span>
          <input
            class="input input--mono"
            bind:value={tileDialog.url}
            placeholder="https://example.com"
            spellcheck="false"
            disabled={tileDialog.mode === 'edit'}
          />
        </label>
        <label class="tile-dialog__field">
          <span class="tile-dialog__label">{t('start.tiles.name')}</span>
          <input class="input" bind:value={tileDialog.title} placeholder={t('start.tiles.namePlaceholder')} />
        </label>
        <div class="tile-dialog__actions">
          <button class="button button--ghost button--sm" onclick={() => (tileDialog = null)}>
            {t('kernels.cancel')}
          </button>
          <button class="button button--primary button--sm" onclick={() => void saveTileDialog()}>
            {t('kernels.kitesurfEndpointSave')}
          </button>
        </div>
      </div>
    </div>
  {/if}
</div>

<style>
  .start {
    height: 100%;
    overflow: auto;
    background:
      radial-gradient(900px 520px at 82% -18%, var(--accent-soft), transparent 62%),
      radial-gradient(760px 480px at -12% 112%, var(--success-soft), transparent 60%),
      var(--bg);
  }

  .start__shell {
    width: min(860px, 100%);
    min-height: 100%;
    margin: 0 auto;
    padding: 32px 34px 22px;
    display: flex;
    flex-direction: column;
    gap: 34px;
  }

  .start__head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
  }

  .brand {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .brand__mark {
    width: 26px;
    height: 26px;
    border-radius: 8px;
    display: grid;
    place-items: center;
    background: linear-gradient(145deg, var(--accent), var(--accent-highlight));
    color: #fff;
    font-size: var(--text-md);
    font-weight: var(--weight-bold);
    box-shadow: 0 4px 12px var(--accent-soft);
  }

  .brand__name {
    font-size: var(--text-md);
    font-weight: var(--weight-semibold);
    letter-spacing: 0.01em;
  }

  .clock {
    display: flex;
    align-items: baseline;
    gap: 9px;
    white-space: nowrap;
  }

  .clock__time {
    font-size: var(--text-md);
    font-weight: var(--weight-semibold);
    font-variant-numeric: tabular-nums;
    color: var(--text-secondary);
  }

  .clock__date {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .hero {
    padding-top: 24px;
  }

  .hero__greeting {
    margin: 0;
    font-size: var(--text-3xl);
    line-height: 1.12;
    font-weight: var(--weight-bold);
    letter-spacing: -0.03em;
  }

  .hero__subtitle {
    margin: 7px 0 0;
    font-size: var(--text-md);
    color: var(--text-muted);
  }

  .search {
    display: flex;
    align-items: center;
    gap: 11px;
    height: 54px;
    margin-top: 22px;
    padding: 0 12px 0 10px;
    border-radius: var(--radius-lg);
    background: var(--surface);
    border: 1px solid var(--border);
    box-shadow:
      0 1px 2px rgba(16, 24, 40, 0.04),
      0 8px 24px rgba(16, 24, 40, 0.05);
    transition:
      border-color var(--normal) var(--ease),
      box-shadow var(--normal) var(--ease);
  }

  .search:focus-within {
    border-color: var(--accent);
    box-shadow:
      0 0 0 4px var(--accent-soft),
      0 10px 28px rgba(16, 24, 40, 0.08);
  }

  .search__icon {
    width: 34px;
    height: 34px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 10px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .search input {
    flex: 1;
    min-width: 0;
    border: none;
    outline: none;
    background: transparent;
    font-size: var(--text-lg);
  }

  .search input::placeholder {
    color: var(--text-muted);
  }

  .search__key {
    flex: none;
    font-family: var(--font-mono);
    font-size: var(--text-xs);
    line-height: 18px;
    padding: 0 6px;
    border-radius: 5px;
    border: 1px solid var(--border);
    color: var(--text-muted);
  }

  .search__hint {
    flex: none;
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .block {
    display: flex;
    flex-direction: column;
    gap: 11px;
  }

  .block--starter {
    gap: 12px;
  }

  .block__head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }

  .block__title {
    margin: 0;
    font-size: var(--text-sm);
    font-weight: var(--weight-semibold);
    letter-spacing: 0.02em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .block__hint {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .text-button {
    display: inline-flex;
    align-items: center;
    gap: 3px;
    height: 24px;
    padding: 0 6px;
    margin-right: -6px;
    border-radius: var(--radius-xs);
    font-size: var(--text-xs);
    color: var(--text-muted);
    transition:
      color var(--fast) var(--ease),
      background var(--fast) var(--ease);
  }

  .text-button:hover {
    color: var(--text);
    background: var(--bg-hover);
  }

  .sites {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 8px;
  }

  .site {
    position: relative;
    min-width: 0;
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 10px 11px;
    border-radius: var(--radius-md);
    border: 1px solid transparent;
    background: var(--surface);
    text-align: left;
    box-shadow: var(--shadow-sm);
    transition:
      transform var(--fast) var(--ease),
      border-color var(--fast) var(--ease),
      box-shadow var(--fast) var(--ease);
  }

  .site:hover {
    transform: translateY(-1px);
    border-color: var(--border-strong);
    box-shadow: var(--shadow-md);
  }

  .site__mark {
    width: 32px;
    height: 32px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 9px;
    font-size: var(--text-md);
    font-weight: var(--weight-bold);
    color: hsl(var(--site-hue) 52% 34%);
    background: hsl(var(--site-hue) 62% 92%);
    overflow: hidden;
  }

  .site__mark--icon {
    background: var(--bg-sunken);
  }

  .site__mark img {
    width: 20px;
    height: 20px;
    object-fit: contain;
  }

  .site__text {
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 1px;
  }

  .site__title {
    font-size: var(--text-sm);
    font-weight: var(--weight-medium);
    color: var(--text);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .site__host {
    font-size: var(--text-2xs);
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .site__arrow {
    flex: none;
    margin-left: auto;
    color: var(--text-muted);
    opacity: 0;
    transform: translateX(-2px);
    transition:
      opacity var(--fast) var(--ease),
      transform var(--fast) var(--ease);
  }

  .site:hover .site__arrow {
    opacity: 1;
    transform: none;
  }

  .block__actions {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .text-button--active {
    color: var(--accent);
  }

  .site-wrap {
    position: relative;
    min-width: 0;
  }

  .site-wrap .site {
    width: 100%;
  }

  .site__pin {
    flex: none;
    margin-left: auto;
    color: var(--accent);
    display: grid;
    place-items: center;
  }

  .site__edit {
    position: absolute;
    top: 4px;
    right: 4px;
    display: flex;
    gap: 2px;
    padding: 2px;
    border-radius: var(--radius-sm);
    background: var(--surface-strong);
    border: 1px solid var(--border);
    box-shadow: var(--shadow-xs);
  }

  .site__edit .icon-button {
    width: 22px;
    height: 22px;
  }

  .site--add {
    border: 1px dashed var(--border-strong);
    background: transparent;
    cursor: pointer;
  }

  .site--add:hover {
    border-color: var(--accent);
    background: var(--accent-soft);
  }

  .site__mark--add {
    color: var(--accent);
    background: var(--accent-soft);
  }

  .tile-backdrop {
    position: fixed;
    inset: 0;
    z-index: 80;
    display: grid;
    place-items: center;
    background: rgba(8, 10, 14, 0.46);
    backdrop-filter: blur(5px);
    animation: tile-fade var(--fast) var(--ease-out);
  }

  .tile-dialog {
    width: min(420px, calc(100vw - 48px));
    background: var(--surface-strong);
    border: 1px solid var(--border-strong);
    border-radius: var(--radius-lg);
    box-shadow: var(--shadow-lg);
    padding: 18px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    animation: tile-in var(--normal) var(--ease-out);
  }

  @keyframes tile-fade {
    from { opacity: 0; }
    to { opacity: 1; }
  }

  @keyframes tile-in {
    from { opacity: 0; transform: translateY(6px) scale(0.98); }
    to { opacity: 1; transform: none; }
  }

  .tile-dialog__title {
    font-size: var(--text-md);
    font-weight: var(--weight-semibold);
  }

  .tile-dialog__field {
    display: flex;
    flex-direction: column;
    gap: 5px;
  }

  .tile-dialog__label {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .tile-dialog__actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
  }

  .starters {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 10px;
  }

  .starter {
    position: relative;
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 4px;
    padding: 14px;
    border-radius: var(--radius-md);
    border: 1px solid var(--border);
    background: var(--surface);
    box-shadow: var(--shadow-sm);
    text-align: left;
    transition:
      transform var(--fast) var(--ease),
      border-color var(--fast) var(--ease),
      box-shadow var(--fast) var(--ease);
  }

  .starter:hover {
    transform: translateY(-1px);
    border-color: var(--border-strong);
    box-shadow: var(--shadow-md);
  }

  .starter__icon {
    width: 34px;
    height: 34px;
    display: grid;
    place-items: center;
    margin-bottom: 4px;
    border-radius: 10px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .starter__title {
    font-size: var(--text-sm);
    font-weight: var(--weight-semibold);
    color: var(--text);
  }

  .starter__desc {
    font-size: var(--text-xs);
    line-height: 1.45;
    color: var(--text-muted);
  }

  .starter__go {
    position: absolute;
    top: 13px;
    right: 12px;
    color: var(--text-muted);
    opacity: 0;
    transition: opacity var(--fast) var(--ease);
  }

  .starter:hover .starter__go {
    opacity: 1;
  }

  .start__foot {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    margin-top: auto;
    padding-top: 14px;
    border-top: 1px solid var(--border);
  }

  .privacy {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .foot-links {
    display: flex;
    align-items: center;
    gap: 2px;
  }

  .foot-link {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    height: 24px;
    padding: 0 7px;
    border-radius: var(--radius-xs);
    font-size: var(--text-xs);
    color: var(--text-muted);
    transition:
      color var(--fast) var(--ease),
      background var(--fast) var(--ease);
  }

  .foot-link:hover {
    color: var(--text);
    background: var(--bg-hover);
  }

  .version {
    margin-left: 6px;
    font-family: var(--font-mono);
    font-size: var(--text-2xs);
    color: var(--text-muted);
    opacity: 0.75;
  }

  @media (max-width: 760px) {
    .start__shell {
      padding: 24px 20px 18px;
      gap: 28px;
    }

    .sites {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .starters {
      grid-template-columns: 1fr;
    }

    .start__foot {
      flex-direction: column;
      align-items: flex-start;
      gap: 8px;
    }
  }

  @media (prefers-color-scheme: dark) {
    :global(:root:not([data-theme='light'])) .site__mark {
      color: hsl(var(--site-hue) 62% 78%);
      background: hsl(var(--site-hue) 34% 24%);
    }

    :global(:root:not([data-theme='light'])) .site__mark--icon {
      background: var(--bg-sunken);
    }

    .search {
      box-shadow:
        0 1px 2px rgba(0, 0, 0, 0.3),
        0 10px 30px rgba(0, 0, 0, 0.24);
    }
  }

  :global(:root[data-theme='dark']) .site__mark {
    color: hsl(var(--site-hue) 62% 78%);
    background: hsl(var(--site-hue) 34% 24%);
  }

  :global(:root[data-theme='dark']) .site__mark--icon {
    background: var(--bg-sunken);
  }

  .suggestions {
    margin-top: 8px;
    border-radius: var(--radius-md);
    border: 1px solid var(--border);
    background: var(--surface);
    box-shadow: var(--shadow-md);
    overflow: hidden;
  }

  .suggestion {
    display: flex;
    align-items: center;
    gap: 10px;
    width: 100%;
    padding: 9px 12px;
    text-align: left;
    transition: background var(--fast) var(--ease);
  }

  .suggestion + .suggestion {
    border-top: 1px solid var(--border);
  }

  .suggestion:hover {
    background: var(--bg-hover);
  }

  .suggestion__icon {
    width: 26px;
    height: 26px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 7px;
    color: var(--text-muted);
    background: var(--bg-sunken);
  }

  .suggestion__main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 1px;
  }

  .suggestion__title {
    font-size: var(--text-sm);
    font-weight: var(--weight-medium);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .suggestion__url {
    font-size: var(--text-2xs);
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .suggestion__arrow {
    flex: none;
    color: var(--text-muted);
    opacity: 0;
    transition: opacity var(--fast) var(--ease);
  }

  .suggestion:hover .suggestion__arrow {
    opacity: 1;
  }

  .suggestions__foot {
    padding: 6px 12px;
    border-top: 1px solid var(--border);
    background: var(--bg-sunken);
    font-size: var(--text-2xs);
    color: var(--text-muted);
  }



  .welcome {
    position: relative;
    display: flex;
    align-items: flex-start;
    gap: 16px;
    padding: 22px;
    border-radius: var(--radius-lg);
    border: 1px solid var(--border);
    background:
      linear-gradient(135deg, var(--accent-soft), transparent 46%),
      var(--surface);
    box-shadow: var(--shadow-sm);
    overflow: hidden;
  }

  .welcome__glow {
    position: absolute;
    width: 260px;
    height: 260px;
    right: -90px;
    top: -130px;
    border-radius: 50%;
    background: var(--accent-soft);
    filter: blur(10px);
    pointer-events: none;
  }

  .welcome__mark {
    position: relative;
    width: 42px;
    height: 42px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 13px;
    background: linear-gradient(145deg, var(--accent), var(--accent-highlight));
    color: #fff;
    font-size: var(--text-xl);
    font-weight: var(--weight-bold);
    box-shadow: 0 8px 22px var(--accent-soft);
  }

  .welcome__main {
    position: relative;
    flex: 1;
    min-width: 0;
  }

  .welcome__eyebrow {
    font-size: var(--text-2xs);
    font-weight: var(--weight-semibold);
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--accent-text);
  }

  .welcome__title {
    margin: 3px 0 0;
    font-size: var(--text-xl);
    font-weight: var(--weight-bold);
    letter-spacing: -0.02em;
  }

  .welcome__body {
    margin: 5px 0 0;
    max-width: 58ch;
    font-size: var(--text-sm);
    line-height: 1.55;
    color: var(--text-secondary);
  }

  .welcome__actions {
    display: flex;
    flex-wrap: wrap;
    gap: 7px;
    margin-top: 14px;
  }

  .welcome__privacy {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    margin-top: 12px;
    font-size: var(--text-2xs);
    color: var(--text-muted);
  }


</style>
