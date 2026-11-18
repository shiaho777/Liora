<script lang="ts">
  import EmptyState from '../EmptyState.svelte';
  import Icon from '../Icon.svelte';
  import PageHeader from '../PageHeader.svelte';
  import Toggle from '../Toggle.svelte';
  import { call } from '../bridge';
  import { toast } from '../state.svelte';
  import { t } from '../i18n.svelte';
  import { exportUserScript, parseUserScript } from '../userscript-meta';

  interface UserScript {
    id: string;
    name: string;
    enabled: boolean;
    matches: string[];
    runAt: 'start' | 'end' | 'idle';
    code: string;
    created: number;
    modified: number;
  }

  let scripts = $state<UserScript[]>([]);
  let loading = $state(true);
  let editing = $state<UserScript | null>(null);
  let isNew = $state(false);
  let saving = $state(false);
  let fileInput = $state<HTMLInputElement | null>(null);

  // Editor fields.
  let name = $state('');
  let matchesText = $state('');
  let runAt = $state<'start' | 'end' | 'idle'>('end');
  let code = $state('');

  async function load(): Promise<void> {
    loading = true;
    try {
      const list = await call<UserScript[]>('userscript.list');
      scripts = Array.isArray(list) ? list : [];
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    void load();
  });

  function openNew(): void {
    isNew = true;
    editing = { id: '', name: '', enabled: true, matches: [], runAt: 'end', code: '', created: 0, modified: 0 };
    name = '';
    matchesText = '';
    runAt = 'end';
    code = '';
  }

  function openEdit(script: UserScript): void {
    isNew = false;
    editing = script;
    name = script.name;
    matchesText = script.matches.join('\n');
    runAt = script.runAt;
    code = script.code;
  }

  function closeEditor(): void {
    editing = null;
    isNew = false;
  }

  async function save(): Promise<void> {
    if (!editing || saving) return;
    const matches = matchesText
      .split('\n')
      .map((line) => line.trim())
      .filter(Boolean);
    if (!name.trim()) {
      toast(t('userscripts.needName'), 'error');
      return;
    }
    if (matches.length === 0) {
      toast(t('userscripts.needMatch'), 'error');
      return;
    }
    if (!code.trim()) {
      toast(t('userscripts.needCode'), 'error');
      return;
    }
    saving = true;
    try {
      const payload = { name: name.trim(), matches, runAt, code, enabled: editing.enabled };
      if (isNew) {
        await call('userscript.add', payload);
      } else {
        await call('userscript.update', { ...payload, id: editing.id });
      }
      toast(t('userscripts.saved'));
      closeEditor();
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      saving = false;
    }
  }

  async function toggle(script: UserScript, enabled: boolean): Promise<void> {
    try {
      await call('userscript.setEnabled', { id: script.id, enabled });
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  async function remove(script: UserScript): Promise<void> {
    try {
      await call('userscript.remove', { id: script.id });
      toast(t('userscripts.removed'));
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  function runAtLabel(value: string): string {
    if (value === 'start') return t('userscripts.runAt.start');
    if (value === 'idle') return t('userscripts.runAt.idle');
    return t('userscripts.runAt.end');
  }

  function triggerImport(): void {
    fileInput?.click();
  }

  async function onImportFile(event: Event): Promise<void> {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file) return;
    try {
      const text = await file.text();
      const parsed = parseUserScript(text);
      if (!parsed) {
        toast(t('userscripts.importNoMeta'), 'error');
        return;
      }
      if (parsed.matches.length === 0) {
        toast(t('userscripts.importNoMatch'), 'error');
        return;
      }
      await call('userscript.add', {
        name: parsed.name,
        matches: parsed.matches,
        runAt: parsed.runAt,
        code: parsed.code,
        enabled: true,
      });
      toast(t('userscripts.imported'));
      if (parsed.unsupported.length > 0) {
        // Be honest about what we could not carry over (grants, requires,
        // path-specific matches) instead of silently dropping them.
        toast(`${t('userscripts.importPartial')}: ${parsed.unsupported.join(', ')}`, 'error');
      }
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  function downloadSource(source: string, filename: string): void {
    const blob = new Blob([source], { type: 'text/javascript' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = filename;
    anchor.click();
    URL.revokeObjectURL(url);
  }

  function exportScript(script: UserScript): void {
    const safe = script.name.replace(/[^\w.-]+/g, '_') || 'script';
    downloadSource(exportUserScript(script), `${safe}.user.js`);
  }

  function exportAll(): void {
    if (scripts.length === 0) return;
    const source = scripts.map((script) => exportUserScript(script)).join('\n\n');
    downloadSource(source, 'liora-userscripts.user.js');
  }
</script>

<div class="section">
  <PageHeader icon="puzzle" title={t('userscripts.title')} subtitle={t('userscripts.lede')} />

  {#if editing}
    <div class="card">
      <div class="card__header">
        <div>
          <div class="card__title">{isNew ? t('userscripts.new') : t('userscripts.edit')}</div>
          <div class="card__subtitle">{t('userscripts.editorHint')}</div>
        </div>
      </div>
      <div class="card__body editor">
        <label class="field">
          <span class="field__label">{t('userscripts.name')}</span>
          <input class="input" type="text" bind:value={name} placeholder={t('userscripts.namePlaceholder')} />
        </label>

        <label class="field">
          <span class="field__label">{t('userscripts.matches')}</span>
          <textarea
            class="input input--mono"
            rows="3"
            bind:value={matchesText}
            placeholder={'x.com\n*.example.org\n*'}
            spellcheck="false"
          ></textarea>
          <span class="field__hint">{t('userscripts.matchesHint')}</span>
        </label>

        <label class="field">
          <span class="field__label">{t('userscripts.runAt')}</span>
          <select class="select" bind:value={runAt}>
            <option value="start">{t('userscripts.runAt.start')}</option>
            <option value="end">{t('userscripts.runAt.end')}</option>
            <option value="idle">{t('userscripts.runAt.idle')}</option>
          </select>
        </label>

        <label class="field">
          <span class="field__label">{t('userscripts.code')}</span>
          <textarea
            class="input input--mono code"
            rows="10"
            bind:value={code}
            placeholder={'// ' + t('userscripts.codePlaceholder')}
            spellcheck="false"
          ></textarea>
        </label>

        <div class="editor__actions">
          <button class="button button--ghost" onclick={closeEditor}>{t('userscripts.cancel')}</button>
          <button class="button button--primary" disabled={saving} onclick={save}>
            <Icon name="check" size={13} />
            {t('userscripts.save')}
          </button>
        </div>
      </div>
    </div>
  {:else}
    <div class="card">
      <div class="card__header">
        <div>
          <div class="card__title">{t('userscripts.installed')}</div>
          <div class="card__subtitle">{t('userscripts.installedDesc')}</div>
        </div>
        <div class="header-actions">
          <button class="button button--ghost button--sm" onclick={triggerImport}>
            <Icon name="upload" size={13} />
            {t('userscripts.import')}
          </button>
          {#if scripts.length > 0}
            <button class="button button--ghost button--sm" onclick={exportAll}>
              <Icon name="download" size={13} />
              {t('userscripts.exportAll')}
            </button>
          {/if}
          <button class="button button--primary button--sm" onclick={openNew}>
            <Icon name="plus" size={13} />
            {t('userscripts.new')}
          </button>
        </div>
      </div>
      <div class="card__body">
        {#if loading}
          <div class="empty">{t('userscripts.loading')}</div>
        {:else if scripts.length === 0}
          <EmptyState
            icon="puzzle"
            title={t('userscripts.emptyTitle')}
            body={t('userscripts.empty')}
            actionLabel={t('userscripts.new')}
            onaction={openNew}
          />
        {:else}
          {#each scripts as script (script.id)}
            <div class="script-row">
              <Toggle
                checked={script.enabled}
                label={script.name}
                onchange={(next) => toggle(script, next)}
              />
              <button class="script-row__main" onclick={() => openEdit(script)}>
                <span class="script-row__name">{script.name}</span>
                <span class="script-row__meta mono">
                  {script.matches.join(', ')} · {runAtLabel(script.runAt)}
                </span>
              </button>
              <button class="icon-button" title={t('userscripts.export')} onclick={() => exportScript(script)}>
                <Icon name="download" size={14} />
              </button>
              <button class="icon-button" title={t('userscripts.remove')} onclick={() => remove(script)}>
                <Icon name="trash" size={14} />
              </button>
            </div>
          {/each}
        {/if}
      </div>
    </div>

    <input
      bind:this={fileInput}
      type="file"
      accept=".js,.user.js,text/javascript"
      class="file-hidden"
      onchange={onImportFile}
    />

    <div class="callout">
      <span class="callout__icon"><Icon name="info" size={14} /></span>
      <span>{t('userscripts.safetyHint')}</span>
    </div>
  {/if}
</div>

<style>
  .header-actions {
    display: flex;
    align-items: center;
    gap: 6px;
    flex-wrap: wrap;
    justify-content: flex-end;
  }

  .file-hidden {
    display: none;
  }

  .editor {
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: 5px;
  }

  .field__label {
    font-size: var(--text-sm);
    font-weight: var(--weight-medium);
    color: var(--text-secondary);
  }

  .field__hint {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .code {
    min-height: 180px;
    resize: vertical;
    line-height: 1.5;
  }

  .editor__actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
  }

  .empty {
    padding: 18px 0;
    font-size: var(--text-sm);
    color: var(--text-muted);
    text-align: center;
  }

  .script-row {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 9px 0;
    border-bottom: 1px solid var(--border);
  }

  .script-row:last-child {
    border-bottom: none;
  }

  .script-row__main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
    text-align: left;
    cursor: pointer;
  }

  .script-row__name {
    font-size: var(--text-sm);
    font-weight: var(--weight-medium);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .script-row__meta {
    font-size: var(--text-xs);
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
</style>