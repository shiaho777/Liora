<script lang="ts">
  import Icon from '../Icon.svelte';
  import { call, notify } from '../bridge';
  import { app } from '../state.svelte';
  import { t } from '../i18n.svelte';

  interface Info {
    version: string;
    profile: string;
    platform: string;
    arch: string;
    netNeedsNet: boolean;
    netSummary: string;
  }

  let info = $state<Info | null>(null);

  $effect(() => {
    call<Info>('core.info')
      .then((value) => (info = value))
      .catch(() => (info = null));
  });
</script>

<div class="section">
  <header class="settings__header">
    <h1 class="settings__title">{t('about.title')}</h1>
    <p class="settings__lede">{t('about.lede')}</p>
  </header>

  <div class="card">
    <div class="card__body">
      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('about.version')}</div>
          <div class="row__desc mono">Liora {info?.version ?? app.version}</div>
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('about.platform')}</div>
          <div class="row__desc mono">{info?.platform ?? app.platform} · {info?.arch ?? app.arch}</div>
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('about.profile')}</div>
          <div class="row__desc mono">{info?.profile ?? app.profile}</div>
        </div>
        <div class="row__control">
          <button class="button button--sm" onclick={() => notify('ui.openPath', { path: info?.profile ?? app.profile })}>
            {t('about.openProfile')}
          </button>
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('dns.current')}</div>
          <div class="row__desc">{info?.netSummary ?? '—'}</div>
        </div>
        <div class="row__control">
          {#if info?.netNeedsNet}
            <span class="badge badge--warning">{t('dns.status.needsNet')}</span>
          {/if}
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('about.privacy')}</div>
          <div class="row__desc">{t('about.privacy.desc')}</div>
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('about.license')}</div>
          <div class="row__desc">
            Apache-2.0 ·
            <a href="https://www.apache.org/licenses/LICENSE-2.0" target="_blank" rel="noreferrer">
              apache.org/licenses/LICENSE-2.0
              <Icon name="external" size={11} />
            </a>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>
