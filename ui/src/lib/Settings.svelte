<script lang="ts">
  import Icon from './Icon.svelte';
  import { app, toast, updateSetting } from './state.svelte';
  import { loadSettings } from './settings-store.svelte';
  import { setLocale, t } from './i18n.svelte';

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
    advanced?: boolean;
  }

  const sections: NavEntry[] = [
    { id: 'appearance', label: 'nav.appearance', icon: 'sliders' },
    { id: 'search', label: 'nav.search', icon: 'search' },
    { id: 'dns', label: 'nav.dns', icon: 'shield' },
    { id: 'kernels', label: 'nav.kernels', icon: 'puzzle', advanced: true },
    { id: 'data', label: 'nav.data', icon: 'download' },
    { id: 'diagnostics', label: 'nav.diagnostics', icon: 'info' },
    { id: 'migration', label: 'nav.migration', icon: 'external' },
    { id: 'about', label: 'nav.about', icon: 'info' },
  ];

  let loaded = $state(false);
  let error = $state('');

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

  const visibleSections = $derived(
    sections.filter((section) => !section.advanced || app.mode === 'advanced'),
  );

  const activeSection = $derived(
    visibleSections.some((section) => section.id === app.settingsSection)
      ? app.settingsSection
      : visibleSections[0]?.id ?? 'appearance',
  );
</script>

<div class="settings">
  <nav class="settings__sidebar" aria-label={t('settings.title')}>
    <div class="settings__brand">
      <span class="settings__brand-mark">L</span>
      <span style:font-weight="600" style:font-size="13px">{t('app.name')}</span>
    </div>

    {#each visibleSections as section (section.id)}
      <button
        class="nav-item"
        class:nav-item--active={activeSection === section.id}
        onclick={() => (app.settingsSection = section.id)}
      >
        <Icon name={section.icon} size={14} />
        {t(section.label)}
      </button>
    {/each}

    <div style:flex="1"></div>

    <div class="mode-hint">
      <span class="muted" style:font-size="11px">{t('appearance.mode')}</span>
      <div class="cluster" style:gap="4px; margin-top:6px">
        {#each ['simple', 'standard', 'advanced'] as mode (mode)}
          <button
            class="chip"
            class:chip--active={app.mode === mode}
            style:height="22px"
            style:font-size="11px"
            onclick={() => updateSetting('ui.mode', mode)}
          >
            {t(`appearance.mode.${mode}`)}
          </button>
        {/each}
      </div>
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
      <div class="empty">…</div>
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
  .mode-hint {
    padding: 10px 8px 4px;
    border-top: 1px solid var(--border);
  }
</style>
