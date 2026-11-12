<script lang="ts">
  import Icon from './Icon.svelte';
  import PageHeader from './PageHeader.svelte';
  import { call, notify, onNotification } from './bridge';
  import { toast } from './state.svelte';
  import { t } from './i18n.svelte';

  interface Props {
    onnavigate?: (url: string) => void;
  }

  let { onnavigate }: Props = $props();

  interface Watch {
    id: string;
    url: string;
    title: string;
    keyword: string;
    intervalMinutes: number;
    createdAt: number;
    lastCheckedAt: number;
    lastChangedAt: number;
    lastSeenAt: number;
    status: 'watching' | 'changed' | 'error';
    lastError: string;
    lastSummary: string;
  }

  let watches = $state<Watch[]>([]);
  let checking = $state<string[]>([]);
  let loading = $state(true);

  async function load(): Promise<void> {
    loading = true;
    try {
      const list = await call<Watch[]>('watch.list');
      watches = Array.isArray(list) ? list : [];
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    void load();
  });

  $effect(() => {
    return onNotification((message) => {
      if (message.method === 'browser.watchChecked') {
        const params = (message.params ?? {}) as { id?: string; error?: string };
        checking = checking.filter((id) => id !== params.id);
        if (params.error) toast(params.error, 'error');
        void load();
      } else if (message.method === 'browser.watchChanged') {
        void load();
      }
    });
  });

  function host(url: string): string {
    try {
      return new URL(url).hostname.replace(/^www\./, '');
    } catch {
      return url;
    }
  }

  function intervalLabel(minutes: number): string {
    if (minutes >= 1440) return t('watch.everyDay');
    if (minutes >= 60) return t('watch.everyHours', { count: Math.round(minutes / 60) });
    return t('watch.everyMinutes', { count: minutes });
  }

  function checkedLabel(ms: number): string {
    if (!ms) return t('watch.neverChecked');
    const diff = Date.now() - ms;
    if (diff < 60_000) return t('watch.justNow');
    if (diff < 3_600_000) return t('watch.minutesAgo', { count: Math.floor(diff / 60_000) });
    if (diff < 86_400_000) return t('watch.hoursAgo', { count: Math.floor(diff / 3_600_000) });
    return new Date(ms).toLocaleDateString();
  }

  async function check(watch: Watch): Promise<void> {
    if (checking.includes(watch.id)) return;
    checking = [...checking, watch.id];
    notify('ui.checkWatch', { id: watch.id });
    toast(t('watch.checking', { title: watch.title }));
  }

  function checkAll(): void {
    if (watches.length === 0) return;
    checking = watches.map((watch) => watch.id);
    notify('ui.checkAllWatches', {});
    toast(t('watch.checkingAll'));
  }

  async function markSeen(watch: Watch): Promise<void> {
    try {
      await call('watch.seen', { id: watch.id });
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  async function remove(watch: Watch): Promise<void> {
    try {
      await call('watch.remove', { id: watch.id });
      watches = watches.filter((item) => item.id !== watch.id);
      toast(t('watch.removed'));
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }
</script>

<div class="section">
  <PageHeader icon="eye" title={t('watch.pageTitle')} subtitle={t('watch.pageLede')}>
    {#snippet actions()}
      <button
        class="button button--sm"
        disabled={watches.length === 0 || checking.length > 0}
        onclick={checkAll}
      >
        <Icon name="refresh" size={13} />
        {t('watch.checkAll')}
      </button>
    {/snippet}
  </PageHeader>

  <div class="card">
    <div class="card__header">
      <div class="card__main">
        <div class="card__title">{t('watch.list')}</div>
        <div class="card__subtitle">{t('watch.count', { count: watches.length })}</div>
      </div>
    </div>
    <div class="card__body">
      {#if loading}
        <div class="watch-loading" aria-busy="true">
          {#each Array(3) as _, index (index)}
            <span class="skeleton-row"></span>
          {/each}
        </div>
      {:else if watches.length === 0}
        <div class="empty-state">
          <span class="empty-state__icon"><Icon name="eye" size={19} /></span>
          <div class="empty-state__title">{t('watch.empty')}</div>
          <p class="empty-state__body">{t('watch.emptyHint')}</p>
        </div>
      {:else}
        <div class="watch-list">
          {#each watches as watch (watch.id)}
            <div class="watch-row">
              <span
                class="watch-row__icon"
                class:watch-row__icon--changed={watch.status === 'changed'}
                class:watch-row__icon--error={watch.status === 'error'}
              >
                <Icon name={watch.status === 'error' ? 'warning' : 'eye'} size={15} />
              </span>
              <div class="watch-row__main">
                <div class="watch-row__title">
                  <button class="watch-row__name" title={watch.url} onclick={() => onnavigate?.(watch.url)}>
                    {watch.title || host(watch.url)}
                  </button>
                  {#if watch.status === 'changed'}
                    <span class="badge badge--accent">{t('watch.changed')}</span>
                  {:else if watch.status === 'error'}
                    <span class="badge badge--danger">{t('watch.failed')}</span>
                  {:else}
                    <span class="badge badge--success">{t('watch.watching')}</span>
                  {/if}
                </div>
                <div class="watch-row__url mono">{watch.url}</div>
                <div class="watch-row__meta">
                  <span>{intervalLabel(watch.intervalMinutes)}</span>
                  <span>·</span>
                  <span>{checkedLabel(watch.lastCheckedAt)}</span>
                  {#if watch.keyword}
                    <span class="badge">{t('watch.keywordBadge', { keyword: watch.keyword })}</span>
                  {/if}
                </div>
                {#if watch.lastError}
                  <div class="watch-row__summary watch-row__summary--error">{watch.lastError}</div>
                {:else if watch.status === 'changed' && watch.lastSummary}
                  <div class="watch-row__summary">{watch.lastSummary}</div>
                {/if}
              </div>
              <div class="watch-row__control">
                {#if watch.status === 'changed'}
                  <button class="button button--primary button--sm" onclick={() => markSeen(watch)}>
                    {t('watch.markSeen')}
                  </button>
                {/if}
                <button
                  class="icon-button"
                  title={t('watch.checkNow')}
                  aria-label={t('watch.checkNow')}
                  disabled={checking.includes(watch.id)}
                  onclick={() => check(watch)}
                >
                  <Icon name="refresh" size={14} />
                </button>
                <button
                  class="icon-button"
                  title={t('downloads.open')}
                  aria-label={t('downloads.open')}
                  onclick={() => onnavigate?.(watch.url)}
                >
                  <Icon name="external" size={14} />
                </button>
                <button
                  class="icon-button"
                  title={t('rules.delete')}
                  aria-label={t('rules.delete')}
                  onclick={() => remove(watch)}
                >
                  <Icon name="trash" size={14} />
                </button>
              </div>
            </div>
          {/each}
        </div>
      {/if}
    </div>
  </div>
</div>

<style>
  .card__main {
    flex: 1;
    min-width: 0;
  }

  .watch-list {
    display: flex;
    flex-direction: column;
  }

  .watch-row {
    display: flex;
    align-items: flex-start;
    gap: 12px;
    padding: 13px 0;
  }

  .watch-row + .watch-row {
    border-top: 1px solid var(--border);
  }

  .watch-row__icon {
    width: 34px;
    height: 34px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 10px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .watch-row__icon--changed {
    color: var(--warning);
    background: var(--warning-soft);
  }

  .watch-row__icon--error {
    color: var(--danger);
    background: var(--danger-soft);
  }

  .watch-row__main {
    flex: 1;
    min-width: 0;
  }

  .watch-row__title {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 7px;
  }

  .watch-row__name {
    max-width: 100%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-size: 13px;
    font-weight: 550;
    color: var(--text);
    text-align: left;
  }

  .watch-row__name:hover {
    color: var(--accent-text);
  }

  .watch-row__url {
    margin-top: 1px;
    font-size: 10.5px;
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .watch-row__meta {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 5px;
    margin-top: 5px;
    font-size: 10.5px;
    color: var(--text-muted);
  }

  .watch-row__summary {
    margin-top: 7px;
    padding: 7px 9px;
    border-radius: var(--radius-xs);
    background: var(--bg-sunken);
    font-size: 11px;
    line-height: 1.45;
    color: var(--text-secondary);
    overflow: hidden;
    display: -webkit-box;
    -webkit-line-clamp: 2;
    line-clamp: 2;
    -webkit-box-orient: vertical;
  }

  .watch-row__summary--error {
    background: var(--danger-soft);
    color: var(--danger);
  }

  .watch-row__control {
    flex: none;
    display: flex;
    align-items: center;
    gap: 3px;
    padding-top: 1px;
  }

  .watch-loading {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .skeleton-row {
    height: 62px;
    border-radius: var(--radius-sm);
    background: linear-gradient(90deg, var(--bg-sunken), var(--bg-hover), var(--bg-sunken));
    background-size: 200% 100%;
    animation: shimmer 1.4s linear infinite;
  }

  @keyframes shimmer {
    from {
      background-position: 200% 0;
    }
    to {
      background-position: -200% 0;
    }
  }

  @media (max-width: 700px) {
    .watch-row {
      flex-wrap: wrap;
    }

    .watch-row__control {
      width: 100%;
      justify-content: flex-end;
    }
  }
</style>
