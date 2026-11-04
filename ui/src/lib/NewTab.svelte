<script lang="ts">
  import Icon from './Icon.svelte';
  import { call } from './bridge';
  import { t } from './i18n.svelte';

  interface Props {
    onnavigate?: (url: string) => void;
    onopen?: (page: string) => void;
  }

  let { onnavigate, onopen }: Props = $props();

  interface Site {
    url: string;
    title: string;
    visitCount: number;
  }

  let query = $state('');
  let sites = $state<Site[]>([]);

  $effect(() => {
    call<Site[]>('history.top', { limit: 8 })
      .then((items) => (sites = items))
      .catch(() => (sites = []));
  });

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
</script>

<div class="newtab">
  <div class="newtab__mark">L</div>

  <form class="newtab__search" onsubmit={submit}>
    <Icon name="search" size={17} />
    <input
      bind:value={query}
      placeholder={t('newtab.hint')}
      spellcheck="false"
      autocomplete="off"
      aria-label={t('address.placeholder')}
    />
    {#if query}
      <kbd class="kbd">↵</kbd>
    {/if}
  </form>

  {#if sites.length > 0}
    <div class="tiles">
      {#each sites as site (site.url)}
        <button class="tile" onclick={() => onnavigate?.(site.url)} title={site.url}>
          <span class="tile__icon">{letter(site.title || host(site.url))}</span>
          <span class="tile__label">{site.title || host(site.url)}</span>
        </button>
      {/each}
    </div>
  {/if}

  <div class="quick">
    <button class="quick__item" onclick={() => onopen?.('settings')}>
      <Icon name="sliders" size={14} />
      {t('nav.settings')}
    </button>
    <button class="quick__item" onclick={() => onopen?.('bookmarks')}>
      <Icon name="star" size={14} />
      {t('nav.bookmarks')}
    </button>
    <button class="quick__item" onclick={() => onopen?.('history')}>
      <Icon name="history" size={14} />
      {t('nav.history')}
    </button>
  </div>
</div>

<style>
  .kbd {
    font-family: var(--font-mono);
    font-size: 11px;
    color: var(--text-muted);
    padding: 2px 6px;
    border-radius: var(--radius-xs);
    border: 1px solid var(--border);
  }

  .quick {
    display: flex;
    gap: 6px;
    flex-wrap: wrap;
    justify-content: center;
  }

  .quick__item {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    height: 30px;
    padding: 0 11px;
    border-radius: var(--radius-pill);
    font-size: 12px;
    color: var(--text-secondary);
    background: var(--surface);
    border: 1px solid var(--border);
    transition: background var(--fast) var(--ease), color var(--fast) var(--ease);
  }

  .quick__item:hover {
    background: var(--bg-hover);
    color: var(--text);
  }
</style>
