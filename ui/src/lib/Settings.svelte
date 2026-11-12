<script lang="ts">
  import Icon from './Icon.svelte';
  import Toggle from './Toggle.svelte';
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

  interface Props {
    onclose?: () => void;
  }

  let { onclose }: Props = $props();

  interface NavEntry {
    id: string;
    label: string;
    icon: string;
    group: string;
    advanced?: boolean;
  }

  const sections: NavEntry[] = [
    { id: 'appearance', label: 'nav.appearance', icon: 'sliders', group: 'browsing' },
    { id: 'search', label: 'nav.search', icon: 'search', group: 'browsing' },
    { id: 'dns', label: 'nav.dns', icon: 'shield', group: 'network' },
    { id: 'data', label: 'nav.data', icon: 'download', group: 'data' },
    { id: 'migration', label: 'nav.migration', icon: 'external', group: 'data' },
    { id: 'diagnostics', label: 'nav.diagnostics', icon: 'info', group: 'help' },
    { id: 'about', label: 'nav.about', icon: 'info', group: 'help' },
    { id: 'kernels', label: 'nav.kernels', icon: 'puzzle', group: 'advanced', advanced: true },
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

  const advanced = $derived(app.mode === 'advanced');

  const visibleSections = $derived(
    sections.filter((section) => !section.advanced || advanced),
  );

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
      : visibleSections[0]?.id ?? 'appearance',
  );

  function toggleAdvanced(next: boolean): void {
    void updateSetting('ui.mode', next ? 'advanced' : 'simple');
  }
</script>

<div class="settings">
  <nav class="settings__sidebar" aria-label={t('settings.title')}>
    <div class="settings__brand">
      <span class="settings__brand-mark">L</span>
      <span class="settings__brand-name">{t('app.name')}</span>
      <button class="settings__done" onclick={() => onclose?.()}>
        <Icon name="check" size={12} />
        {t('settings.done')}
      </button>
    </div>

    <div class="settings__nav">
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
    </div>

    <div class="settings__footer">
      <div class="advanced-row">
        <div class="advanced-row__main">
          <div class="advanced-row__title">{t('settings.advanced')}</div>
          <div class="advanced-row__desc">{t('settings.advancedDesc')}</div>
        </div>
        <Toggle checked={advanced} label={t('settings.advanced')} onchange={toggleAdvanced} />
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
  .settings__sidebar {
    overflow: hidden;
    padding: 14px 10px 12px;
    background: var(--surface);
  }

  .settings__brand {
    padding: 4px 6px 16px;
  }

  .settings__brand-name {
    flex: 1;
    font-size: 13px;
    font-weight: 650;
    letter-spacing: 0.01em;
  }

  .settings__done {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    height: 24px;
    padding: 0 8px;
    border-radius: var(--radius-pill);
    font-size: 11px;
    font-weight: 550;
    color: var(--text-secondary);
    background: var(--bg-hover);
    transition:
      background var(--fast) var(--ease),
      color var(--fast) var(--ease);
  }

  .settings__done:hover {
    background: var(--accent-soft);
    color: var(--accent-text);
  }

  .settings__nav {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 15px;
    padding-bottom: 12px;
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

  .settings__footer {
    flex: none;
    padding: 12px 6px 2px;
    border-top: 1px solid var(--border);
  }

  .advanced-row {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .advanced-row__main {
    flex: 1;
    min-width: 0;
  }

  .advanced-row__title {
    font-size: 11.5px;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .advanced-row__desc {
    margin-top: 1px;
    font-size: 10.5px;
    line-height: 1.4;
    color: var(--text-muted);
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
