<script lang="ts">
  import Icon from '../Icon.svelte';
  import PageHeader from '../PageHeader.svelte';
  import Segmented from '../Segmented.svelte';
  import Toggle from '../Toggle.svelte';
  import { value, set } from '../settings-store.svelte';
  import { t } from '../i18n.svelte';

  // These controls are backed by real WebKit behaviour (see SystemKernel
  // setContentControls). Ad blocking and extensions have no engine yet, so
  // they are deliberately not offered here — no fake switches.

  const autoplayOptions = $derived([
    { value: 'allow', label: t('content.autoplay.allow') },
    { value: 'block-audible', label: t('content.autoplay.audible') },
    { value: 'block', label: t('content.autoplay.block') },
  ]);
</script>

<div class="section">
  <PageHeader icon="shield" title={t('content.title')} subtitle={t('content.lede')} />

  <div class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('content.defaults')}</div>
        <div class="card__subtitle">{t('content.defaultsDesc')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('content.javascript')}</div>
          <div class="row__desc">{t('content.javascript.desc')}</div>
        </div>
        <div class="row__control">
          <Toggle
            checked={value<boolean>('content.javascript') !== false}
            label={t('content.javascript')}
            onchange={(next) => set('content.javascript', next)}
          />
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('content.images')}</div>
          <div class="row__desc">{t('content.images.desc')}</div>
        </div>
        <div class="row__control">
          <Toggle
            checked={value<boolean>('content.images') !== false}
            label={t('content.images')}
            onchange={(next) => set('content.images', next)}
          />
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('content.autoplay')}</div>
          <div class="row__desc">{t('content.autoplay.desc')}</div>
        </div>
        <div class="row__control">
          <Segmented
            value={String(value<string>('content.autoplay') ?? 'block-audible')}
            options={autoplayOptions}
            ariaLabel={t('content.autoplay')}
            onchange={(next) => set('content.autoplay', next)}
          />
        </div>
      </div>
    </div>
  </div>

  <div class="callout">
    <span class="callout__icon"><Icon name="info" size={14} /></span>
    <span>{t('content.scopeHint')}</span>
  </div>
</div>