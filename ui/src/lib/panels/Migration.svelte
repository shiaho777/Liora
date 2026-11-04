<script lang="ts">
  import Icon from '../Icon.svelte';
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
    void detect();
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
  <header class="settings__header">
    <h1 class="settings__title">{t('migration.title')}</h1>
    <p class="settings__lede">{t('migration.lede')}</p>
  </header>

  <div class="card">
    <div class="card__body">
      <div class="cluster" style:margin-bottom="12px">
        <span class="muted" style:font-size="12px">{t('data.backup.sections')}</span>
        <button class="chip" class:chip--active={includeBookmarks} onclick={() => (includeBookmarks = !includeBookmarks)}>
          {t('data.bookmarks')}
        </button>
        <button class="chip" class:chip--active={includeHistory} onclick={() => (includeHistory = !includeHistory)}>
          {t('data.history')}
        </button>
        <button class="icon-button" title={t('migration.detect')} onclick={detect}>
          <Icon name="refresh" size={14} />
        </button>
      </div>

      {#if loading}
        <div class="empty">…</div>
      {:else if sources.length === 0}
        <div class="empty">{t('migration.empty')}</div>
      {:else}
        <div class="stack">
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
                <div class="row__desc mono">{source.path}</div>
              </div>
              <div class="row__control">
                {#if source.needsAccess}
                  <button class="button button--sm" onclick={() => notify('ui.openSystemSettings', { pane: 'privacy' })}>
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
