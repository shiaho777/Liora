<script lang="ts">
  import Icon from './Icon.svelte';
  import Segmented from './Segmented.svelte';
  import { app, toast, updateSetting } from './state.svelte';
  import { loadSettings } from './settings-store.svelte';
  import { setLocale, t } from './i18n.svelte';
  import { networkPending } from './network-status.svelte';

  import Appearance from './panels/Appearance.svelte';
  import SearchPanel from './panels/Search.svelte';
  import Dns from './panels/Dns.svelte';
  import Kernels from './panels/Kernels.svelte';
  import Data from './panels/Data.svelte';
  import Migration from './panels/Migration.svelte';
  import Diagnostics from './panels/Diagnostics.svelte';
  import About from './panels/About.svelte';

  interface NavEntry {
    id: string;
    label: string;
    icon: string;
    group: string;
    advanced?: boolean;
    keywords?: string[];
  }

  const sections: NavEntry[] = [
    { id: 'appearance', label: 'nav.appearance', icon: 'sliders', group: 'browsing' },
    { id: 'search', label: 'nav.search', icon: 'search', group: 'browsing' },
    {
      id: 'dns',
      label: 'nav.dns',
      icon: 'shield',
      group: 'network',
      keywords: ['doh', 'dot', 'doq', 'ech', 'dns', 'direct', '直连'],
    },
    {
      id: 'kernels',
      label: 'nav.kernels',
      icon: 'puzzle',
      group: 'network',
      advanced: true,
      keywords: ['cef', 'chromium', 'webview', 'component', '组件', '内核'],
    },
    { id: 'data', label: 'nav.data', icon: 'download', group: 'data' },
    { id: 'diagnostics', label: 'nav.diagnostics', icon: 'info', group: 'data' },
    { id: 'migration', label: 'nav.migration', icon: 'external', group: 'data' },
    { id: 'about', label: 'nav.about', icon: 'info', group: 'system' },
  ];

  const modes = $derived([
    { value: 'simple', label: t('appearance.mode.simple') },
    { value: 'standard', label: t('appearance.mode.standard') },
    { value: 'advanced', label: t('appearance.mode.advanced') },
  ]);

  let loaded = $state(false);
  let error = $state('');
  let navQuery = $state('');

  $effect(() => {
    if (loaded) return;
    loadSettings()
      .then(() => {
        loaded = true;
        setLocale(app.language);
      })
      .catch((reason) => {
        error = String(reason?.message ?? reason);
        toast(error, 'error');
      });
  });

  const visibleSections = $derived.by(() => {
    const query = navQuery.trim().toLowerCase();
    return sections.filter((section) => {
      if (section.advanced && app.mode !== 'advanced') return false;
      if (!query) return true;
      const haystack = [t(section.label), section.id, ...(section.keywords ?? [])]
        .join(' ')
        .toLowerCase();
      return haystack.includes(query);
    });
  });

  const groups = $derived.by(() => {
    const result: { id: string; label: string; items: NavEntry[] }[] = [];
    for (const section of visibleSections) {
      let group = result.find((item) => item.id === section.group);
      if (!group) {
        group = {
          id: section.group,
          label: t(`settings.group.${section.group}`),
          items: [],
        };
        result.push(group);
      }
      group.items.push(section);
    }
    return result;
  });

  const activeSection = $derived(
    visibleSections.some((section) => section.id === app.settingsSection)
      ? app.settingsSection
      : visibleSections[0]?.id ?? app.settingsSection,
  );

  function chooseFirst(): void {
    const first = visibleSections[0];
    if (first) app.settingsSection = first.id;
  }
</script>

<div class="settings">
  <nav class="settings__sidebar" aria-label={t('settings.title')}>
    <div class="settings__brand">
      <span class="settings__brand-mark">L</span>
      <span class="settings__brand-name">{t('app.name')}</span>
    </div>

    <div class="settings__search">
      <Icon name="search" size={13} />
      <input
        bind:value={navQuery}
        placeholder={t('settings.search')}
        aria-label={t('settings.search')}
        spellcheck="false"
        onkeydown={(event) => {
          if (event.key === 'Enter') {
            event.preventDefault();
            chooseFirst();
          } else if (event.key === 'Escape') {
            navQuery = '';
          }
        }}
      />
      {#if navQuery}
        <button
          class="settings__search-clear"
          title={t('settings.clearSearch')}
          aria-label={t('settings.clearSearch')}
          onclick={() => (navQuery = '')}
        >
          <Icon name="close" size={10} />
        </button>
      {/if}
    </div>

    <div class="settings__nav">
      {#if groups.length === 0}
        <div class="settings__nav-empty">{t('settings.noResults')}</div>
      {:else}
        {#each groups as group (group.id)}
          <div class="settings__group">
            <div class="settings__group-label">{group.label}</div>
            {#each group.items as section (section.id)}
              <button
                class="nav-item"
                class:nav-item--active={activeSection === section.id}
                onclick={() => (app.settingsSection = section.id)}
              >
                <Icon name={section.icon} size={14} />
                <span class="nav-item__label">{t(section.label)}</span>
                {#if section.id === 'dns' && networkPending()}
                  <span class="nav-badge">{t('network.navBadge')}</span>
                {/if}
              </button>
            {/each}
          </div>
        {/each}
      {/if}
    </div>

    <div class="mode-hint">
      <div class="mode-hint__label">{t('appearance.mode')}</div>
      <Segmented
        value={app.mode}
        options={modes}
        ariaLabel={t('appearance.mode')}
        onchange={(mode) => updateSetting('ui.mode', mode)}
      />
      <div class="mode-hint__desc">{t('appearance.mode.desc')}</div>
    </div>
  </nav>

  <main class="settings__main">
    {#if error}
      <div class="callout callout--danger" style:margin-bottom="16px">
        <span class="callout__icon"><Icon name="warning" size={14} /></span>
        <span>{error}</span>
      </div>
    {/if}

    {#if !loaded}
      <div class="settings-loading" aria-busy="true">
        <span class="skeleton skeleton--title"></span>
        <span class="skeleton skeleton--line"></span>
        <span class="skeleton skeleton--line skeleton--short"></span>
      </div>
    {:else if activeSection === 'appearance'}
      <Appearance />
    {:else if activeSection === 'search'}
      <SearchPanel />
    {:else if activeSection === 'dns'}
      <Dns />
    {:else if activeSection === 'kernels'}
      <Kernels />
    {:else if activeSection === 'data'}
      <Data />
    {:else if activeSection === 'diagnostics'}
      <Diagnostics />
    {:else if activeSection === 'migration'}
      <Migration />
    {:else}
      <About />
    {/if}
  </main>
</div>

<style>
  .settings__brand-name {
    font-size: 13px;
    font-weight: 600;
  }

  .settings__search {
    display: flex;
    align-items: center;
    gap: 6px;
    height: 30px;
    margin: 0 4px 10px;
    padding: 0 7px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
    border: 1px solid transparent;
    color: var(--text-muted);
    transition:
      background var(--fast) var(--ease),
      border-color var(--fast) var(--ease),
      box-shadow var(--fast) var(--ease);
  }

  .settings__search:focus-within {
    background: var(--surface);
    border-color: var(--accent);
    box-shadow: 0 0 0 3px var(--accent-soft);
  }

  .settings__search input {
    flex: 1;
    min-width: 0;
    border: none;
    outline: none;
    background: transparent;
    color: var(--text);
    font-size: 12px;
  }

  .settings__search input::placeholder {
    color: var(--text-muted);
  }

  .settings__search-clear {
    width: 18px;
    height: 18px;
    display: grid;
    place-items: center;
    border-radius: var(--radius-xs);
    color: var(--text-muted);
  }

  .settings__search-clear:hover {
    background: var(--bg-hover);
    color: var(--text);
  }

  .settings__nav {
    display: flex;
    flex-direction: column;
    gap: 14px;
    padding: 0 0 12px;
  }

  .settings__group {
    display: flex;
    flex-direction: column;
    gap: 1px;
  }

  .settings__group-label {
    padding: 0 9px 5px;
    font-size: 10px;
    font-weight: 650;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .settings__nav-empty {
    padding: 22px 10px;
    text-align: center;
    font-size: 11.5px;
    color: var(--text-muted);
  }

  .nav-item__label {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nav-badge {
    margin-left: auto;
    padding: 1px 6px;
    border-radius: var(--radius-pill);
    background: var(--warning-soft);
    color: var(--warning);
    font-size: 10px;
    font-weight: 600;
  }

  .mode-hint {
    display: flex;
    flex-direction: column;
    gap: 7px;
    padding: 12px 8px 4px;
    border-top: 1px solid var(--border);
  }

  .mode-hint__label {
    font-size: 10px;
    font-weight: 650;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .mode-hint__desc {
    font-size: 10.5px;
    line-height: 1.45;
    color: var(--text-muted);
  }

  .mode-hint :global(.segmented) {
    width: 100%;
  }

  .mode-hint :global(.segmented__item) {
    flex: 1;
    padding: 0 4px;
    font-size: 11px;
  }

  .settings-loading {
    display: flex;
    flex-direction: column;
    gap: 11px;
    max-width: 800px;
    margin: 0 auto;
    padding: 12px 0;
  }

  .skeleton {
    display: block;
    height: 12px;
    border-radius: 6px;
    background: linear-gradient(90deg, var(--bg-sunken), var(--bg-hover), var(--bg-sunken));
    background-size: 200% 100%;
    animation: shimmer 1.4s linear infinite;
  }

  .skeleton--title {
    width: 140px;
    height: 20px;
  }

  .skeleton--line {
    width: 100%;
  }

  .skeleton--short {
    width: 58%;
  }

  @keyframes shimmer {
    from {
      background-position: 200% 0;
    }
    to {
      background-position: -200% 0;
    }
  }
</style>
