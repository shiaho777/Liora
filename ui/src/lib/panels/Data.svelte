<script lang="ts">
  import Icon from '../Icon.svelte';
  import Toggle from '../Toggle.svelte';
  import { call, notify, onNotification } from '../bridge';
  import { app, toast } from '../state.svelte';
  import { t } from '../i18n.svelte';

  interface BackupFile {
    path: string;
    name: string;
  }

  interface SectionInfo {
    name: string;
    entries?: number;
    count?: number;
  }

  interface Permission {
    origin: string;
    name: string;
    value: string;
    modified: number;
  }

  let backups = $state<BackupFile[]>([]);
  let permissions = $state<Permission[]>([]);
  let loading = $state(true);
  let sections = $state<string[]>(['settings', 'bookmarks', 'history', 'permissions']);
  let merge = $state(true);
  let pendingRestore = $state<{ path: string; sections: SectionInfo[] } | null>(null);

  const allSections = ['settings', 'bookmarks', 'history', 'permissions'];

  async function refresh(): Promise<void> {
    loading = true;
    try {
      backups = await call<BackupFile[]>('backup.list');
      permissions = await call<Permission[]>('permissions.list', {});
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      loading = false;
    }
  }

  async function resetPermission(entry: Permission): Promise<void> {
    await call('permissions.set', { origin: entry.origin, name: entry.name, value: '' });
    await refresh();
  }

  $effect(() => {
    void refresh();
  });

  async function backupNow(): Promise<void> {
    try {
      const result = await call<{ path: string }>('backup.now');
      toast(`${t('data.backup.now')} · ${result.path}`);
      await refresh();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  function toggleSection(name: string): void {
    sections = sections.includes(name) ? sections.filter((item) => item !== name) : [...sections, name];
  }

  async function inspect(path: string): Promise<void> {
    try {
      const info = await call<{ valid: boolean; error?: string; sections: SectionInfo[] }>('backup.inspect', { path });
      if (!info.valid) {
        toast(info.error ?? 'invalid backup', 'error');
        return;
      }
      pendingRestore = { path, sections: info.sections };
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  async function restore(path: string): Promise<void> {
    try {
      await call('backup.restore', { path, merge });
      pendingRestore = null;
      toast(t('settings.changed'));
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  function exportBackup(): void {
    if (app.transport === 'mock') {
      toast(t('data.export'));
      return;
    }
    notify('ui.pickSaveFile', { purpose: 'backup', sections, suggested: 'liora-backup.liora' });
  }

  function importBackup(): void {
    if (app.transport === 'mock') {
      toast(t('data.import'));
      return;
    }
    notify('ui.pickFile', { purpose: 'backup' });
  }

  function backupDate(name: string): string {
    const match = /(\d{8})-(\d{6})/.exec(name);
    if (!match) return '';
    const [, day, clock] = match;
    const date = new Date(
      Number(day.slice(0, 4)),
      Number(day.slice(4, 6)) - 1,
      Number(day.slice(6, 8)),
      Number(clock.slice(0, 2)),
      Number(clock.slice(2, 4)),
      Number(clock.slice(4, 6)),
    );
    if (Number.isNaN(date.getTime())) return '';
    return date.toLocaleString();
  }

  onNotification((message) => {
    if (message.method === 'ui.filePicked' && message.params) {
      const params = message.params as { path?: string };
      if (params.path) void inspect(params.path);
    }
  });
</script>

<div class="section">
  <header class="settings__header">
    <h1 class="settings__title">{t('data.title')}</h1>
    <p class="settings__lede">{t('data.lede')}</p>
  </header>

  <div class="card">
    <div class="card__header">
      <div class="row__main">
        <div class="card__title">{t('data.backup.now')}</div>
        <div class="card__subtitle">{t('data.backup.sections')}</div>
      </div>
      <button class="button button--primary" onclick={backupNow}>{t('data.backup.now')}</button>
    </div>
    <div class="card__body">
      <div class="cluster">
        {#each allSections as name (name)}
          <button class="chip" class:chip--active={sections.includes(name)} onclick={() => toggleSection(name)}>
            {t(`data.${name}`)}
          </button>
        {/each}
      </div>

      <div class="cluster" style:margin-top="12px">
        <button class="button button--ghost" onclick={exportBackup}>
          <Icon name="download" size={13} />
          {t('data.export')}
        </button>
        <button class="button button--ghost" onclick={importBackup}>
          <Icon name="external" size={13} />
          {t('data.import')}
        </button>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card__header">
      <div class="row__main">
        <div class="card__title">{t('data.backup.list')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="setting-line">
        <div class="setting-line__main">
          <div class="row__title">{t('data.restoreMode')}</div>
          <div class="row__desc">{merge ? t('data.mergeDesc') : t('data.replaceDesc')}</div>
        </div>
        <Toggle checked={merge} label={t('data.restoreMode')} onchange={(next) => (merge = next)} />
      </div>

      <div class="divider"></div>

      {#if loading}
        <div class="backup-loading" aria-busy="true">
          <span class="skeleton-row"></span>
          <span class="skeleton-row"></span>
          <span class="skeleton-row"></span>
        </div>
      {:else if backups.length === 0}
        <div class="empty-state empty-state--compact">
          <span class="empty-state__icon"><Icon name="folder" size={18} /></span>
          <div class="empty-state__title">{t('data.backup.empty')}</div>
          <p class="empty-state__body">{t('data.backup.emptyHint')}</p>
        </div>
      {:else}
        <div class="backup-list">
          {#each backups as file (file.path)}
            <div class="backup-row" title={file.path}>
              <span class="backup-row__icon"><Icon name="folder" size={14} /></span>
              <div class="backup-row__main">
                <div class="backup-row__title">{backupDate(file.name) || file.name}</div>
                <div class="backup-row__name mono">{file.name}</div>
              </div>
              <button class="button button--sm" onclick={() => inspect(file.path)}>{t('data.restore')}</button>
            </div>
          {/each}
        </div>
      {/if}

      {#if pendingRestore}
        <div class="restore">
          <span class="restore__icon"><Icon name="info" size={15} /></span>
          <div class="restore__main">
            <div class="restore__title">{t('data.restoreConfirm')}</div>
            <div class="restore__file mono">{pendingRestore.path.split('/').pop()}</div>
            <div class="cluster">
              {#each pendingRestore.sections as section (section.name)}
                <span class="badge">
                  {t(`data.${section.name}`)}
                  {#if section.entries !== undefined}· {section.entries}{/if}
                  {#if section.count !== undefined}· {section.count}{/if}
                </span>
              {/each}
            </div>
            <div class="cluster">
              <button class="button button--primary button--sm" onclick={() => restore(pendingRestore!.path)}>
                {merge ? t('data.merge') : t('data.replace')}
              </button>
              <button class="button button--ghost button--sm" onclick={() => (pendingRestore = null)}>
                {t('kernels.cancel')}
              </button>
            </div>
          </div>
        </div>
      {/if}
    </div>
  </div>

  <div class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('permissions.title')}</div>
        <div class="card__subtitle">{t('permissions.lede')}</div>
      </div>
    </div>
    <div class="card__body">
      {#if permissions.length === 0}
        <div class="empty-state empty-state--compact">
          <span class="empty-state__icon"><Icon name="shield" size={18} /></span>
          <div class="empty-state__title">{t('permissions.empty')}</div>
          <p class="empty-state__body">{t('permissions.emptyHint')}</p>
        </div>
      {:else}
        {#each permissions as entry (`${entry.origin}/${entry.name}`)}
          <div class="row">
            <div class="row__main">
              <div class="row__title mono">{entry.origin}</div>
              <div class="row__desc">{entry.name}</div>
            </div>
            <div class="row__control">
              <span class="badge" class:badge--success={entry.value === 'allow'} class:badge--danger={entry.value !== 'allow'}>
                {entry.value === 'allow' ? t('permissions.allow') : t('permissions.block')}
              </span>
              <button class="button button--sm" onclick={() => resetPermission(entry)}>{t('permissions.reset')}</button>
            </div>
          </div>
        {/each}
      {/if}
    </div>
  </div>
</div>


<style>
  .setting-line {
    display: flex;
    align-items: center;
    gap: 16px;
    padding: 2px 0 12px;
  }

  .setting-line__main {
    flex: 1;
    min-width: 0;
  }

  .backup-list {
    display: flex;
    flex-direction: column;
  }

  .backup-row {
    display: flex;
    align-items: center;
    gap: 11px;
    padding: 10px 0;
  }

  .backup-row + .backup-row {
    border-top: 1px solid var(--border);
  }

  .backup-row__icon {
    width: 30px;
    height: 30px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 9px;
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .backup-row__main {
    flex: 1;
    min-width: 0;
  }

  .backup-row__title {
    font-size: 12.5px;
    font-weight: 550;
  }

  .backup-row__name {
    margin-top: 1px;
    font-size: 10.5px;
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .restore {
    display: flex;
    gap: 10px;
    margin-top: 14px;
    padding: 12px;
    border-radius: var(--radius-md);
    background: var(--accent-soft);
    color: var(--accent-text);
  }

  .restore__icon {
    flex: none;
    margin-top: 1px;
  }

  .restore__main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 7px;
  }

  .restore__title {
    font-size: 12.5px;
    font-weight: 600;
  }

  .restore__file {
    font-size: 10.5px;
    opacity: 0.75;
  }

  .backup-loading {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 6px 0;
  }

  .skeleton-row {
    height: 38px;
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
</style>
