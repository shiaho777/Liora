<script lang="ts">
  import PageHeader from '../PageHeader.svelte';
  import Segmented from '../Segmented.svelte';
  import Toggle from '../Toggle.svelte';
  import { app, updateSetting } from '../state.svelte';
  import { set, value } from '../settings-store.svelte';
  import { t } from '../i18n.svelte';

  const themes = $derived([
    { value: 'system', label: t('appearance.theme.system') },
    { value: 'light', label: t('appearance.theme.light') },
    { value: 'dark', label: t('appearance.theme.dark') },
  ]);

  const densities = $derived([
    { value: 'comfortable', label: t('appearance.density.comfortable') },
    { value: 'compact', label: t('appearance.density.compact') },
  ]);

  const languages = [
    { value: 'auto', label: t('appearance.language.auto') },
    { value: 'zh', label: '简体中文' },
    { value: 'en', label: 'English' },
  ];
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
        <div class="preview__main">
          <div class="preview__chrome">
            <div class="preview__tabs">
              <span class="preview__tab preview__tab--active">
                <span class="preview__dot"></span>Liora
              </span>
              <span class="preview__tab">{t('tab.new')}</span>
              <span class="preview__plus">+</span>
            </div>
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
    font-size: 9.5px;
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
    font-size: 12px;
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
    background: linear-gradient(145deg, var(--accent), #7aa2ff);
    color: #fff;
    font-size: 13px;
    font-weight: 700;
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
</style>
