<script lang="ts">
  import Icon from './lib/Icon.svelte';
  import NewTab from './lib/NewTab.svelte';
  import Settings from './lib/Settings.svelte';
  import Library from './lib/Library.svelte';
  import { app, boot, internalPageFor, openPage, toast } from './lib/state.svelte';
  import { describeSettings, notify } from './lib/bridge';
  import { t } from './lib/i18n.svelte';

  interface Tab {
    id: number;
    title: string;
    url: string;
    page: 'newtab' | 'settings' | 'bookmarks' | 'history' | 'web';
  }

  const firstTab: Tab = { id: 1, title: t('tab.new'), url: 'liora://newtab', page: 'newtab' };
  let tabSeq = 2;
  let tabs = $state<Tab[]>([firstTab]);
  let activeId = $state(firstTab.id);
  let addressValue = $state('');
  let addressFocused = $state(false);
  let contentEl = $state<HTMLDivElement | null>(null);

  const active = $derived(tabs.find((tab) => tab.id === activeId) ?? tabs[0]);
  const showTabs = $derived(app.tabsPosition !== 'hidden');

  const searchTemplates: Record<string, string> = {
    duckduckgo: 'https://duckduckgo.com/?q=',
    google: 'https://www.google.com/search?q=',
    bing: 'https://www.bing.com/search?q=',
    brave: 'https://search.brave.com/search?q=',
    startpage: 'https://www.startpage.com/do/search?q=',
    baidu: 'https://www.baidu.com/s?wd=',
    sogou: 'https://www.sogou.com/web?query=',
  };
  let searchEngine = $state('duckduckgo');

  boot()
    .then(async () => {
      const description = await describeSettings();
      const engine = description.items.find((item) => item.key === 'search.engine');
      if (engine && typeof engine.value === 'string') searchEngine = engine.value;
    })
    .catch((error) => toast(String(error?.message ?? error), 'error'));

  $effect(() => {
    if (active) addressValue = active.page === 'web' ? active.url : `liora://${active.page}`;
  });

  // Used by the shell for automated checks and deep links.
  $effect(() => {
    (window as unknown as { __lioraOpenUrl?: (url: string) => void }).__lioraOpenUrl = (url: string) =>
      navigate(url);
  });

  // Tell the shell where the kernel view goes. It is repositioned on resize.
  $effect(() => {
    if (!contentEl) return;
    const observer = new ResizeObserver(() => {
      const rect = contentEl!.getBoundingClientRect();
      notify('ui.contentRect', {
        x: Math.round(rect.x),
        y: Math.round(rect.y),
        width: Math.round(rect.width),
        height: Math.round(rect.height),
        visible: active?.page === 'web',
      });
    });
    observer.observe(contentEl);
    return () => observer.disconnect();
  });

  function updateTab(id: number, patch: Partial<Tab>): void {
    const index = tabs.findIndex((tab) => tab.id === id);
    if (index >= 0) tabs[index] = { ...tabs[index], ...patch };
  }

  function newTab(url = 'liora://newtab'): void {
    const tab: Tab = { id: tabSeq++, title: t('tab.new'), url, page: 'newtab' };
    tabs = [...tabs, tab];
    activeId = tab.id;
    if (url !== 'liora://newtab') navigate(url);
  }

  function closeTab(id: number): void {
    if (tabs.length === 1) {
      updateTab(id, { title: t('tab.new'), url: 'liora://newtab', page: 'newtab' });
      return;
    }
    const index = tabs.findIndex((tab) => tab.id === id);
    tabs = tabs.filter((tab) => tab.id !== id);
    if (activeId === id) activeId = tabs[Math.max(0, index - 1)].id;
  }

  function looksLikeUrl(input: string): boolean {
    if (/^[a-z][a-z0-9+.-]*:\/\//i.test(input)) return true;
    if (input.startsWith('liora:')) return true;
    if (input.startsWith('localhost')) return true;
    return /^[^\s/]+\.[a-z]{2,}([/:?#].*)?$/i.test(input);
  }

  function resolveInput(input: string): string {
    const value = input.trim();
    if (!value) return 'liora://newtab';
    if (value.startsWith('liora://')) return value;
    if (looksLikeUrl(value)) return /^[a-z][a-z0-9+.-]*:/i.test(value) ? value : `https://${value}`;
    const template = searchTemplates[searchEngine] ?? searchTemplates.duckduckgo;
    return template + encodeURIComponent(value);
  }

  function navigate(raw: string): void {
    const url = resolveInput(raw);
    const internal = internalPageFor(url);

    if (internal) {
      updateTab(activeId, { url, page: internal as Tab['page'], title: t('tab.new') });
      if (internal === 'settings') app.settingsSection = app.settingsSection || 'appearance';
      notify('ui.setPage', { page: internal, url });
      return;
    }

    updateTab(activeId, { url, page: 'web', title: url.replace(/^https?:\/\//, '').split('/')[0] });
    notify('ui.navigate', { url, tab: activeId });
  }

  function submitAddress(event: Event): void {
    event.preventDefault();
    navigate(addressValue);
    (event.target as HTMLFormElement).querySelector('input')?.blur();
  }

  function openSettings(section?: string): void {
    app.settingsSection = section ?? 'appearance';
    updateTab(activeId, { url: 'liora://settings', page: 'settings', title: t('settings.title') });
    notify('ui.setPage', { page: 'settings' });
  }

  function onAddressFocus(): void {
    addressFocused = true;
  }

  function onAddressBlur(): void {
    addressFocused = false;
    addressValue = active.page === 'web' ? active.url : `liora://${active.page}`;
  }
</script>

{#if !app.ready}
  <div class="boot">
    <div class="newtab__mark">L</div>
  </div>
{:else}
  <div class="app">
    {#if app.platform === 'macos'}
      <div class="titlebar titlebar--mac">
        <span class="titlebar__title">{t('app.name')}</span>
      </div>
    {/if}

    {#if showTabs}
      <div class="tabstrip">
        <div class="tabstrip__rail">
          {#each tabs as tab (tab.id)}
            <div
              class="tab"
              class:tab--active={tab.id === activeId}
              role="tab"
              tabindex="0"
              aria-selected={tab.id === activeId}
              onclick={() => (activeId = tab.id)}
              onkeydown={(event) => {
                if (event.key === 'Enter' || event.key === ' ') {
                  event.preventDefault();
                  activeId = tab.id;
                }
              }}
              ondblclick={() => newTab()}
              title={tab.title}
            >
              <span class="tab__favicon">
                {#if tab.page === 'settings'}
                  <Icon name="settings" size={10} />
                {:else if tab.page === 'newtab'}
                  L
                {:else}
                  <Icon name="globe" size={10} />
                {/if}
              </span>
              <span class="tab__title">{tab.title}</span>
              <button
                class="tab__close"
                title={t('tab.close')}
                aria-label={t('tab.close')}
                onclick={(event) => {
                  event.stopPropagation();
                  closeTab(tab.id);
                }}
              >
                <Icon name="close" size={10} />
              </button>
            </div>
          {/each}
        </div>
        <button class="icon-button" title={t('tab.new')} onclick={() => newTab()}>
          <Icon name="plus" />
        </button>
      </div>
    {/if}

    <div class="toolbar">
      <div class="toolbar__group">
        <button class="icon-button" title={t('nav.back')} disabled={active.page !== 'web'}>
          <Icon name="back" />
        </button>
        <button class="icon-button" title={t('nav.forward')} disabled={active.page !== 'web'}>
          <Icon name="forward" />
        </button>
        <button class="icon-button" title={t('nav.reload')} onclick={() => navigate(active.url)}>
          <Icon name="reload" />
        </button>
      </div>

      <form class="address" onsubmit={submitAddress}>
        <span class="address__scheme">
          <Icon name={active.page === 'web' ? 'globe' : 'shield'} size={12} />
        </span>
        <input
          class="address__input"
          value={addressValue}
          oninput={(event) => (addressValue = (event.target as HTMLInputElement).value)}
          onfocus={onAddressFocus}
          onblur={onAddressBlur}
          placeholder={t('address.placeholder')}
          spellcheck="false"
          autocomplete="off"
        />
        {#if !addressFocused && active.page === 'web'}
          <span class="address__hint">H3 · ECH</span>
        {/if}
      </form>

      <div class="toolbar__group">
        <button class="icon-button" title={t('nav.settings')} onclick={() => openSettings()}>
          <Icon name="settings" />
        </button>
      </div>
    </div>

    <div class="bookmarksbar">
      <button class="bookmark-chip" onclick={() => openPage('bookmarks')}>
        <Icon name="star" size={12} />
        {t('bookmarks.bar')}
      </button>
      <button class="bookmark-chip" onclick={() => openPage('history')}>
        <Icon name="history" size={12} />
        {t('nav.history')}
      </button>
    </div>

    <div class="content" class:content--native={active.page === 'web'} bind:this={contentEl}>
      {#if active.page === 'newtab'}
        <NewTab onnavigate={(url) => navigate(url)} onopen={(page) => openPage(page as never)} />
      {:else if active.page === 'settings'}
        <Settings />
      {:else if active.page === 'bookmarks' || active.page === 'history'}
        <Library kind={active.page} onnavigate={(url) => navigate(url)} />
      {:else if active.page === 'web'}
        {#if app.transport === 'mock'}
          <div class="webplaceholder">
            <div class="webplaceholder__bar">
              <Icon name="globe" size={14} />
              <span class="mono">{active.url}</span>
            </div>
            <div class="empty">
              页面由原生内核渲染在这里。<br />
              In the real app the kernel view is placed at this rectangle.
            </div>
          </div>
        {/if}
      {/if}
    </div>

    <div class="toasts">
      {#each app.toasts as item (item.id)}
        <div class="toast" class:toast--error={item.kind === 'error'}>{item.text}</div>
      {/each}
    </div>
  </div>
{/if}

<style>
  .boot {
    height: 100%;
    display: grid;
    place-items: center;
    background: var(--bg);
    animation: fade 240ms var(--ease);
  }

  @keyframes fade {
    from {
      opacity: 0;
    }
    to {
      opacity: 1;
    }
  }

  .webplaceholder {
    height: 100%;
    display: flex;
    flex-direction: column;
    background: var(--bg-elevated);
  }

  .webplaceholder__bar {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 14px;
    border-bottom: 1px solid var(--border);
    color: var(--text-muted);
  }
</style>
