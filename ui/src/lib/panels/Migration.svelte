<script lang="ts">
  import Icon from '../Icon.svelte';
  import LoadingState from '../LoadingState.svelte';
  import PageHeader from '../PageHeader.svelte';
  import Toggle from '../Toggle.svelte';
  import { call, notify, onNotification } from '../bridge';
  import { app, toast } from '../state.svelte';
  import { t } from '../i18n.svelte';

  interface Source {
    id: string;
    browser: string;
    name: string;
    profile: string;
    kind: string;
    path: string;
    bookmarks: boolean;
    history: boolean;
    needsAccess: boolean;
  }

  interface Result {
    ok: boolean;
    error: string;
    message: string;
    bookmarks: number;
    history: number;
    warnings: Array<{ item: string; message: string }>;
  }

  let sources = $state<Source[]>([]);
  let loading = $state(true);
  let busy = $state('');
  let includeBookmarks = $state(true);
  let includeHistory = $state(true);
  let lastResult = $state<Result | null>(null);

  async function detect(): Promise<void> {
    loading = true;
    try {
      sources = await call<Source[]>('migrate.detect');
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    // Scanning browser profiles can block the shell's main thread for a
    // moment, so let the page paint its skeleton first.
    const timer = setTimeout(() => void detect(), 50);
    return () => clearTimeout(timer);
  });

  async function run(source: Source): Promise<void> {
    busy = source.id;
    lastResult = null;
    try {
      const result = await call<Result>('migrate.run', {
        id: source.id,
        bookmarks: includeBookmarks,
        history: includeHistory,
      });
      if (!result.ok) {
        toast(result.error === 'permission' ? t('migration.needsAccess') : result.error, 'error');
      } else {
        lastResult = result;
        toast(t('migration.result', { bookmarks: result.bookmarks, history: result.history }));
      }
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      busy = '';
    }
  }

  function pickFile(): void {
    if (app.transport === 'mock') {
      toast(t('migration.fromFile'));
      return;
    }
    notify('ui.pickFile', { purpose: 'migration' });
  }

  function exportHtml(): void {
    if (app.transport === 'mock') {
      toast(t('migration.exportHtml'));
      return;
    }
    notify('ui.pickSaveFile', { purpose: 'bookmarksHtml', suggested: 'bookmarks.html' });
  }

  onNotification((message) => {
    if (message.method !== 'ui.filePicked' || !message.params) return;
    const params = message.params as { path?: string; purpose?: string };
    if (!params.path || params.purpose !== 'migration') return;
    call<Result>('migrate.file', { path: params.path, bookmarks: true, history: true })
      .then((result) => {
        if (result.ok) {
          toast(t('migration.result', { bookmarks: result.bookmarks, history: result.history }));
        } else {
          toast(result.error, 'error');
        }
      })
      .catch((error) => toast(String(error?.message ?? error), 'error'));
  });

  function initial(source: Source): string {
    return (source.name || '?').slice(0, 1).toUpperCase();
  }

  function subtitle(source: Source): string {
    return [source.profile, source.kind].filter(Boolean).join(' · ');
  }
</script>

<div class="section">
  <PageHeader icon="external" title={t('migration.title')} subtitle={t('migration.lede')} />

  <div class="card">
    <div class="card__header">
      <div class="card__main">
        <div class="card__title">{t('migration.detected')}</div>
        <div class="card__subtitle">{t('migration.detectedDesc')}</div>
      </div>
      <button class="icon-button" title={t('migration.detect')} onclick={detect}>
        <Icon name="refresh" size={14} />
      </button>
    </div>
    <div class="card__body">
      <div class="migration-options">
        <span class="migration-options__label">{t('migration.include')}</span>
        <button
          class="chip"
          class:chip--active={includeBookmarks}
          onclick={() => (includeBookmarks = !includeBookmarks)}
        >
          {t('data.bookmarks')}
        </button>
        <button
          class="chip"
          class:chip--active={includeHistory}
          onclick={() => (includeHistory = !includeHistory)}
        >
          {t('data.history')}
        </button>
      </div>

      {#if loading}
        <LoadingState rows={3} />
      {:else if sources.length === 0}
        <div class="empty-state empty-state--compact">
          <span class="empty-state__icon"><Icon name="external" size={18} /></span>
          <div class="empty-state__title">{t('migration.empty')}</div>
          <p class="empty-state__body">{t('migration.emptyHint')}</p>
          <button class="button button--sm" onclick={pickFile}>
            <Icon name="folder" size={13} />
            {t('migration.chooseFile')}
          </button>
        </div>
      {:else}
        <div class="source-list">
          {#each sources as source (source.id)}
            <div class="source">
              <div class="source__avatar">{initial(source)}</div>
              <div class="row__main">
                <div class="row__title">
                  {source.name}
                  {#if source.needsAccess}
                    <span class="badge badge--warning">{t('migration.needsAccess')}</span>
                  {/if}
                </div>
                <div class="row__desc">{subtitle(source)}</div>
                <div class="row__desc mono source__path" title={source.path}>{source.path}</div>
              </div>
              <div class="row__control">
                {#if source.needsAccess}
                  <button
                    class="button button--sm"
                    onclick={() => notify('ui.openSystemSettings', { pane: 'privacy' })}
                  >
                    {t('migration.openSettings')}
                  </button>
                {:else}
                  <button
                    class="button button--primary button--sm"
                    disabled={busy === source.id || (!includeBookmarks && !includeHistory)}
                    onclick={() => run(source)}
                  >
                    {busy === source.id ? t('migration.importing') : t('migration.import')}
                  </button>
                {/if}
              </div>
            </div>
          {/each}
        </div>
      {/if}

      {#if lastResult?.warnings.length}
        <div class="callout callout--warning" style:margin-top="12px">
          <span class="callout__icon"><Icon name="warning" size={14} /></span>
          <div>
            {#each lastResult.warnings as warning (warning.item + warning.message)}
              <div>{warning.item}: {warning.message}</div>
            {/each}
          </div>
        </div>
      {/if}
    </div>
  </div>

  <div class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('migration.fromFile')}</div>
        <div class="card__subtitle">{t('migration.fromFile.desc')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="cluster">
        <button class="button" onclick={pickFile}>
          <Icon name="folder" size={13} />
          {t('migration.chooseFile')}
        </button>
        <button class="button button--ghost" onclick={exportHtml}>
          <Icon name="download" size={13} />
          {t('migration.exportHtml')}
        </button>
      </div>
      <div class="row__desc" style:margin-top="8px">{t('migration.exportEdges')}</div>
    </div>
  </div>
</div>

<style>

  .card__main {
    flex: 1;
    min-width: 0;
  }

  .migration-options {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 6px;
    margin-bottom: 12px;
  }

  .migration-options__label {
    margin-right: 3px;
    font-size: var(--text-xs);
    font-weight: var(--weight-medium);
    color: var(--text-muted);
  }

  .source-list {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .source__path {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-size: var(--text-2xs);
  }

</style>
