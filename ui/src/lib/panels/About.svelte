<script lang="ts">
  import Icon from '../Icon.svelte';
  import PageHeader from '../PageHeader.svelte';
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

  function netSummaryLabel(summary: string): string {
    if (!summary) return '—';
    if (summary === 'system resolver') return t('dns.systemResolver');
    if (summary === 'automatic') return t('dns.mode.auto');
    if (summary.startsWith('automatic (selected server unavailable)')) {
      return t('dns.autoUnavailable');
    }
    return summary;
  }

  $effect(() => {
    call<Info>('core.info')
      .then((value) => (info = value))
      .catch(() => (info = null));
  });
</script>

<div class="section">
  <PageHeader icon="info" title={t('about.title')} subtitle={t('about.subtitle')} />

  <div class="card hero-card">
    <span class="hero-card__mark">L</span>
    <div class="hero-card__main">
      <div class="hero-card__name">
        Liora
        <span class="badge mono">v{info?.version ?? app.version}</span>
      </div>
      <div class="hero-card__tagline">{t('about.lede')}</div>
    </div>
    <span class="badge mono">Apache-2.0</span>
  </div>

  <div class="card">
    <div class="card__body">
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
          <div class="row__desc">{netSummaryLabel(info?.netSummary ?? '')}</div>
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

  <div class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('shortcut.title')}</div>
        <div class="card__subtitle">{t('shortcut.subtitle')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="shortcuts">
        <div class="shortcut"><kbd>⌘ L</kbd><span>{t('shortcut.address')}</span></div>
        <div class="shortcut"><kbd>⌘ T</kbd><span>{t('shortcut.newTab')}</span></div>
        <div class="shortcut"><kbd>⌘ W</kbd><span>{t('shortcut.closeTab')}</span></div>
        <div class="shortcut"><kbd>⌘ F</kbd><span>{t('shortcut.find')}</span></div>
        <div class="shortcut"><kbd>⌘ D</kbd><span>{t('shortcut.bookmark')}</span></div>
        <div class="shortcut"><kbd>⌘ 1–9</kbd><span>{t('shortcut.switchTab')}</span></div>
      </div>
    </div>
  </div>
</div>


<style>
  .hero-card {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 18px;
  }

  .hero-card__mark {
    width: 48px;
    height: 48px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 14px;
    background: linear-gradient(145deg, var(--accent), var(--accent-highlight));
    color: #fff;
    font-size: var(--text-2xl);
    font-weight: var(--weight-bold);
    letter-spacing: -0.02em;
    box-shadow: 0 10px 28px var(--accent-soft);
  }

  .hero-card__main {
    flex: 1;
    min-width: 0;
  }

  .hero-card__name {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: var(--text-xl);
    font-weight: var(--weight-bold);
    letter-spacing: -0.02em;
  }

  .hero-card__tagline {
    margin-top: 2px;
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  @media (max-width: 640px) {
    .hero-card {
      flex-wrap: wrap;
    }
  }

  .shortcuts {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 8px;
  }

  .shortcut {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 8px 10px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
    font-size: var(--text-sm);
    color: var(--text-secondary);
  }

  .shortcut kbd {
    flex: none;
    min-width: 54px;
    padding: 2px 6px;
    border-radius: 5px;
    border: 1px solid var(--border);
    background: var(--surface);
    font-family: var(--font-mono);
    font-size: var(--text-2xs);
    text-align: center;
    color: var(--text);
  }

  @media (max-width: 640px) {
    .shortcuts {
      grid-template-columns: 1fr;
    }
  }

</style>
