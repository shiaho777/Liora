<script lang="ts">
  import Segmented from '../Segmented.svelte';
  import { app, updateSetting } from '../state.svelte';
  import { t } from '../i18n.svelte';

  const modes = $derived([
    { value: 'simple', label: t('appearance.mode.simple') },
    { value: 'standard', label: t('appearance.mode.standard') },
    { value: 'advanced', label: t('appearance.mode.advanced') },
  ]);

  const themes = $derived([
    { value: 'system', label: t('appearance.theme.system') },
    { value: 'light', label: t('appearance.theme.light') },
    { value: 'dark', label: t('appearance.theme.dark') },
  ]);

  const densities = $derived([
    { value: 'comfortable', label: t('appearance.density.comfortable') },
    { value: 'compact', label: t('appearance.density.compact') },
  ]);

  const tabPositions = $derived([
    { value: 'strip', label: t('appearance.tabs.strip') },
    { value: 'vertical', label: t('appearance.tabs.vertical') },
    { value: 'hidden', label: t('appearance.tabs.hidden') },
  ]);

  const languages = [
    { value: 'auto', label: t('appearance.language.auto') },
    { value: 'zh', label: '简体中文' },
    { value: 'en', label: 'English' },
  ];
</script>

<div class="section">
  <header class="settings__header">
    <h1 class="settings__title">{t('appearance.title')}</h1>
    <p class="settings__lede">{t('appearance.lede')}</p>
  </header>

  <div class="card">
    <div class="card__body">
      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('appearance.mode')}</div>
          <div class="row__desc">{t('appearance.mode.desc')}</div>
        </div>
        <div class="row__control">
          <Segmented
            value={app.mode}
            options={modes}
            ariaLabel={t('appearance.mode')}
            onchange={(value) => updateSetting('ui.mode', value)}
          />
        </div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card__body">
      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('appearance.theme')}</div>
        </div>
        <div class="row__control">
          <Segmented
            value={app.theme}
            options={themes}
            ariaLabel={t('appearance.theme')}
            onchange={(value) => updateSetting('ui.theme', value)}
          />
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('appearance.density')}</div>
        </div>
        <div class="row__control">
          <Segmented
            value={app.density}
            options={densities}
            ariaLabel={t('appearance.density')}
            onchange={(value) => updateSetting('ui.density', value)}
          />
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('appearance.tabs')}</div>
        </div>
        <div class="row__control">
          <Segmented
            value={app.tabsPosition}
            options={tabPositions}
            ariaLabel={t('appearance.tabs')}
            onchange={(value) => updateSetting('ui.tabs', value)}
          />
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('appearance.language')}</div>
        </div>
        <div class="row__control">
          <select
            class="select"
            value={app.language}
            onchange={(event) => updateSetting('ui.language', (event.target as HTMLSelectElement).value)}
          >
            {#each languages as language (language.value)}
              <option value={language.value}>{language.label}</option>
            {/each}
          </select>
        </div>
      </div>
    </div>
  </div>
</div>
