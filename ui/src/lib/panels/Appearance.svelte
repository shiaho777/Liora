<script lang="ts">
  import PageHeader from '../PageHeader.svelte';
  import Segmented from '../Segmented.svelte';
  import Toggle from '../Toggle.svelte';
  import { app, updateSetting } from '../state.svelte';
  import { set, value } from '../settings-store.svelte';
  import { t } from '../i18n.svelte';
  import { accentPresets } from '../palette';
  import { BACKGROUND_PRESETS, backgroundStyle, DEFAULT_CUSTOM_COLOR } from '../newtab-background';

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
  ]);

  const languages = [
    { value: 'auto', label: t('appearance.language.auto') },
    { value: 'zh', label: '简体中文' },
    { value: 'en', label: 'English' },
  ];

  function pickAccent(color: string): void {
    void updateSetting('ui.accent', color);
  }
</script>

<div class="section">
  <PageHeader icon="sliders" title={t('appearance.title')} subtitle={t('appearance.lede')} />

  <div class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('appearance.interface')}</div>
        <div class="card__subtitle">{t('appearance.interfaceDesc')}</div>
      </div>
    </div>
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
          <div class="row__title">{t('appearance.accent')}</div>
          <div class="row__desc">{t('appearance.accentDesc')}</div>
        </div>
        <div class="row__control">
          <div class="accents" role="radiogroup" aria-label={t('appearance.accent')}>
            {#each accentPresets as preset (preset.value)}
              <button
                class="accent-dot"
                class:accent-dot--active={app.accent.toLowerCase() === preset.value}
                style:background={preset.value}
                role="radio"
                aria-checked={app.accent.toLowerCase() === preset.value}
                aria-label={t(preset.name)}
                title={t(preset.name)}
                onclick={() => pickAccent(preset.value)}
              ></button>
            {/each}
            <label class="accent-custom" title={t('appearance.accentCustom')}>
              <input
                type="color"
                value={app.accent}
                aria-label={t('appearance.accentCustom')}
                onchange={(event) => pickAccent((event.target as HTMLInputElement).value)}
              />
              <span class="accent-custom__ring" style:background={app.accent}></span>
            </label>
          </div>
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
            onchange={(event) =>
              updateSetting('ui.language', (event.target as HTMLSelectElement).value)}
          >
            {#each languages as language (language.value)}
              <option value={language.value}>{language.label}</option>
            {/each}
          </select>
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('appearance.startup')}</div>
          <div class="row__desc">{t('appearance.startupDesc')}</div>
        </div>
        <div class="row__control">
          <Toggle
            checked={value<boolean>('startup.restore') !== false}
            label={t('appearance.startup')}
            onchange={(next) => set('startup.restore', next)}
          />
        </div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('appearance.background')}</div>
        <div class="card__subtitle">{t('appearance.backgroundDesc')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('appearance.backgroundStyle')}</div>
        </div>
        <div class="row__control">
          <div class="bgs" role="radiogroup" aria-label={t('appearance.backgroundStyle')}>
            {#each BACKGROUND_PRESETS as preset (preset.id)}
              <button
                class="bg-swatch"
                class:bg-swatch--active={value<string>('start.background') === preset.id}
                style:background={backgroundStyle(preset.id, '')}
                role="radio"
                aria-checked={value<string>('start.background') === preset.id}
                aria-label={t(`appearance.bg.${preset.name}`)}
                title={t(`appearance.bg.${preset.name}`)}
                onclick={() => set('start.background', preset.id)}
              ></button>
            {/each}
            <button
              class="bg-swatch bg-swatch--custom"
              class:bg-swatch--active={value<string>('start.background') === 'custom'}
              style:background={backgroundStyle('custom', String(value<string>('start.backgroundCustom') ?? DEFAULT_CUSTOM_COLOR))}
              role="radio"
              aria-checked={value<string>('start.background') === 'custom'}
              aria-label={t('appearance.bg.custom')}
              title={t('appearance.bg.custom')}
              onclick={() => set('start.background', 'custom')}
            ></button>
          </div>
        </div>
      </div>

      {#if value<string>('start.background') === 'custom'}
        <div class="row">
          <div class="row__main">
            <div class="row__title">{t('appearance.bg.customColor')}</div>
            <div class="row__desc">{t('appearance.bg.customColorDesc')}</div>
          </div>
          <div class="row__control">
            <label class="accent-custom" title={t('appearance.bg.customColor')}>
              <input
                type="color"
                value={String(value<string>('start.backgroundCustom') ?? DEFAULT_CUSTOM_COLOR)}
                aria-label={t('appearance.bg.customColor')}
                onchange={(event) => set('start.backgroundCustom', (event.target as HTMLInputElement).value)}
              />
              <span
                class="accent-custom__ring"
                style:background={String(value<string>('start.backgroundCustom') ?? DEFAULT_CUSTOM_COLOR)}
              ></span>
            </label>
          </div>
        </div>
      {/if}
    </div>
  </div>

  <div class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('appearance.preview')}</div>
        <div class="card__subtitle">{t('appearance.previewDesc')}</div>
      </div>
    </div>
    <div class="card__body">
      <div
        class="preview"
        class:preview--compact={app.density === 'compact'}
        aria-hidden="true"
      >
        {#if app.tabsPosition === 'vertical'}
          <div class="preview__rail">
            <span class="preview__tab preview__tab--active">
              <span class="preview__dot"></span>Liora
            </span>
            <span class="preview__tab">{t('tab.new')}</span>
            <span class="preview__tab">Example</span>
          </div>
        {/if}
        <div class="preview__main">
          <div class="preview__chrome">
            {#if app.tabsPosition !== 'vertical'}
              <div class="preview__tabs">
                <span class="preview__tab preview__tab--active">
                  <span class="preview__dot"></span>Liora
                </span>
                <span class="preview__tab">{t('tab.new')}</span>
                <span class="preview__plus">+</span>
              </div>
            {/if}
            <div class="preview__toolbar">
              <span class="preview__nav"></span>
              <span class="preview__address">example.com</span>
              <span class="preview__action"></span>
            </div>
          </div>
          <div class="preview__page">
            <span class="preview__mark">L</span>
            <span class="preview__line preview__line--title"></span>
            <span class="preview__line"></span>
            <span class="preview__line preview__line--short"></span>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>


<style>
  .accents {
    display: flex;
    align-items: center;
    gap: 7px;
    flex-wrap: wrap;
    justify-content: flex-end;
  }

  .accent-dot {
    width: 22px;
    height: 22px;
    border-radius: 50%;
    border: 2px solid transparent;
    box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.12);
    cursor: pointer;
    padding: 0;
    transition: transform 0.12s ease, box-shadow 0.12s ease;
  }

  .accent-dot:hover {
    transform: scale(1.12);
  }

  .accent-dot--active {
    box-shadow:
      0 0 0 2px var(--bg),
      0 0 0 4px var(--accent);
  }

  .accent-custom {
    position: relative;
    width: 22px;
    height: 22px;
    cursor: pointer;
    display: inline-block;
  }

  .accent-custom input {
    position: absolute;
    inset: 0;
    opacity: 0;
    cursor: pointer;
    width: 100%;
    height: 100%;
  }

  .accent-custom__ring {
    display: block;
    width: 22px;
    height: 22px;
    border-radius: 50%;
    /* quality-audit-ignore: decorative rainbow ring signalling "any colour" */
    background: conic-gradient(#e64980, #e8590c, #2f9e44, #0f9d8f, #3b6cf6, #7c5cf0, #e64980);
    box-shadow: inset 0 0 0 2px var(--bg);
    pointer-events: none;
  }

  .bgs {
    display: flex;
    align-items: center;
    gap: 7px;
    flex-wrap: wrap;
    justify-content: flex-end;
  }

  .bg-swatch {
    width: 40px;
    height: 26px;
    border-radius: 7px;
    border: 1px solid var(--border);
    cursor: pointer;
    padding: 0;
    transition: transform 0.12s ease, box-shadow 0.12s ease;
  }

  .bg-swatch:hover {
    transform: scale(1.06);
  }

  .bg-swatch--active {
    box-shadow:
      0 0 0 2px var(--bg),
      0 0 0 4px var(--accent);
  }

  .preview {
    display: flex;
    height: 208px;
    border-radius: 13px;
    border: 1px solid var(--border);
    background: var(--bg);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
    overflow: hidden;
  }

  .preview__main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
  }

  .preview__chrome {
    flex: none;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
  }

  .preview__tabs {
    display: flex;
    align-items: flex-end;
    gap: 2px;
    padding: 7px 10px 0;
  }

  .preview__tab {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    height: 23px;
    padding: 0 9px;
    border-radius: 7px 7px 0 0;
    font-size: var(--text-2xs);
    color: var(--text-muted);
    white-space: nowrap;
  }

  .preview__tab--active {
    background: var(--bg);
    border: 1px solid var(--border);
    border-bottom: none;
    color: var(--text);
  }

  .preview__dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: var(--accent);
  }

  .preview__plus {
    margin-left: auto;
    padding: 0 4px;
    font-size: var(--text-sm);
    color: var(--text-muted);
  }

  .preview__toolbar {
    display: flex;
    align-items: center;
    gap: 7px;
    height: 34px;
    padding: 0 10px;
    border-top: 1px solid var(--border);
  }

  .preview__nav {
    width: 26px;
    height: 10px;
    border-radius: 3px;
    background: var(--bg-hover);
  }

  .preview__address {
    flex: 1;
    height: 19px;
    border-radius: var(--radius-pill);
    background: var(--bg-sunken);
    color: var(--text-muted);
    font-family: var(--font-mono);
    font-size: 8px;
    line-height: 19px;
    padding: 0 8px;
    overflow: hidden;
    white-space: nowrap;
  }

  .preview__action {
    width: 14px;
    height: 14px;
    border-radius: 4px;
    background: var(--bg-hover);
  }

  .preview__page {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 7px;
    padding: 16px;
    background:
      radial-gradient(280px 160px at 72% -10%, var(--accent-soft), transparent 65%),
      var(--bg);
  }

  .preview__mark {
    width: 28px;
    height: 28px;
    display: grid;
    place-items: center;
    margin-bottom: 2px;
    border-radius: 9px;
    background: linear-gradient(145deg, var(--accent), var(--accent-highlight));
    color: #fff;
    font-size: var(--text-md);
    font-weight: var(--weight-bold);
  }

  .preview__line {
    display: block;
    width: 45%;
    height: 5px;
    border-radius: 3px;
    background: var(--bg-hover);
  }

  .preview__line--title {
    width: 30%;
    height: 7px;
    background: var(--border-strong);
  }

  .preview__line--short {
    width: 26%;
  }

  .preview--compact .preview__tab {
    height: 20px;
  }

  .preview--compact .preview__toolbar {
    height: 30px;
  }

  .preview--compact .preview__page {
    gap: 5px;
  }

  .preview__rail {
    width: 112px;
    flex: none;
    display: flex;
    flex-direction: column;
    gap: 3px;
    padding: 9px 7px;
    background: var(--surface);
    border-right: 1px solid var(--border);
  }

  .preview__rail .preview__tab {
    height: 23px;
    border-radius: var(--radius-xs);
    padding: 0 8px;
  }

  .preview__rail .preview__tab--active {
    background: var(--accent-soft);
    color: var(--accent-text);
    border: none;
  }

</style>
