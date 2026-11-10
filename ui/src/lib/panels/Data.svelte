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
      <div class="row__control">
        <Toggle checked={merge} label={t('data.merge')} onchange={(next) => (merge = next)} />
        <span class="muted" style:font-size="11.5px">{merge ? t('data.merge') : t('data.replace')}</span>
      </div>
    </div>
    <div class="card__body">
      {#if loading}
        <div class="empty">…</div>
      {:else if backups.length === 0}
        <div class="empty">{t('data.backup.empty')}</div>
      {:else}
        {#each backups as file (file.path)}
          <div class="row">
            <div class="row__main">
              <div class="row__title mono">{file.name}</div>
              <div class="row__desc mono">{file.path}</div>
            </div>
            <div class="row__control">
              <button class="button button--sm" onclick={() => inspect(file.path)}>{t('data.restore')}</button>
            </div>
          </div>
        {/each}
      {/if}

      {#if pendingRestore}
        <div class="callout callout--accent" style:margin-top="12px">
          <span class="callout__icon"><Icon name="info" size={14} /></span>
          <div style:flex="1">
            <div>{t('data.restore')}: <span class="mono">{pendingRestore.path}</span></div>
            <div class="cluster" style:margin-top="8px">
              {#each pendingRestore.sections as section (section.name)}
                <span class="badge">
                  {t(`data.${section.name}`)}
                  {#if section.entries !== undefined}· {section.entries}{/if}
                  {#if section.count !== undefined}· {section.count}{/if}
                </span>
              {/each}
            </div>
            <div class="cluster" style:margin-top="10px">
              <button class="button button--primary button--sm" onclick={() => restore(pendingRestore!.path)}>
                {merge ? t('data.merge') : t('data.replace')}
              </button>
              <button class="button button--ghost button--sm" onclick={() => (pendingRestore = null)}>✕</button>
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
        <div class="empty">{t('permissions.empty')}</div>
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
