<script lang="ts">
  import Icon from '../Icon.svelte';
  import PageHeader from '../PageHeader.svelte';
  import Toggle from '../Toggle.svelte';
  import { value, set } from '../settings-store.svelte';
  import { toast } from '../state.svelte';
  import { t } from '../i18n.svelte';
  import {
    BUILTIN_ENGINES,
    newEngineId,
    parseCustomEngines,
    validateEngine,
    type CustomEngine,
  } from '../search-engines';

  const customEngines = $derived(parseCustomEngines(value<unknown>('search.customEngines')));
  const engineOptions = $derived([
    ...BUILTIN_ENGINES.map((engine) => ({ id: engine.id, name: engine.name })),
    ...customEngines.map((engine) => ({ id: engine.id, name: engine.name })),
  ]);

  let adding = $state(false);
  let newName = $state('');
  let newTemplate = $state('');
  let formError = $state('');

  function openAdd(): void {
    adding = true;
    newName = '';
    newTemplate = '';
    formError = '';
  }

  async function saveEngine(): Promise<void> {
    const check = validateEngine(newName, newTemplate);
    if (!check.ok) {
      formError = t(`search.custom.err.${check.error}`);
      return;
    }
    const engine: CustomEngine = {
      id: newEngineId(customEngines),
      name: newName.trim(),
      template: newTemplate.trim(),
    };
    await set('search.customEngines', [...customEngines, engine]);
    adding = false;
    toast(t('search.custom.added'));
  }

  async function removeEngine(id: string): Promise<void> {
    const next = customEngines.filter((engine) => engine.id !== id);
    await set('search.customEngines', next);
    // If the removed engine was active, fall back to the default.
    if (value<string>('search.engine') === id) await set('search.engine', 'duckduckgo');
  }
</script>

<div class="section">
  <PageHeader icon="search" title={t('search.title')} subtitle={t('search.lede')} />

  <div class="card">
    <div class="card__body">
      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('search.engine')}</div>
          <div class="row__desc">{t('search.engine.desc')}</div>
        </div>
        <div class="row__control">
          <select
            class="select"
            value={value<string>('search.engine')}
            onchange={(event) => set('search.engine', (event.target as HTMLSelectElement).value)}
          >
            {#each engineOptions as engine (engine.id)}
              <option value={engine.id}>{engine.name}</option>
            {/each}
          </select>
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('search.suggest')}</div>
          <div class="row__desc">{t('search.suggest.desc')}</div>
        </div>
        <div class="row__control">
          <Toggle
            checked={value<boolean>('search.suggest')}
            label={t('search.suggest')}
            onchange={(next) => set('search.suggest', next)}
          />
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('search.homepage')}</div>
          <div class="row__desc">{t('search.homepage.desc')}</div>
        </div>
        <div class="row__control">
          <input
            class="input input--mono home-input"
            type="text"
            value={String(value<string>('home.page') ?? 'liora://newtab')}
            spellcheck="false"
            aria-label={t('search.homepage')}
            onchange={(event) => set('home.page', (event.target as HTMLInputElement).value.trim() || 'liora://newtab')}
          />
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('search.homeButton')}</div>
          <div class="row__desc">{t('search.homeButton.desc')}</div>
        </div>
        <div class="row__control">
          <Toggle
            checked={value<boolean>('ui.showHomeButton') === true}
            label={t('search.homeButton')}
            onchange={(next) => set('ui.showHomeButton', next)}
          />
        </div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card__header">
      <div class="row__main">
        <div class="card__title">{t('search.custom.title')}</div>
        <div class="card__subtitle">{t('search.custom.desc')}</div>
      </div>
      {#if !adding}
        <button class="button button--sm" onclick={openAdd}>
          <Icon name="plus" size={13} />
          {t('search.custom.add')}
        </button>
      {/if}
    </div>
    <div class="card__body">
      {#if customEngines.length === 0 && !adding}
        <div class="empty-state empty-state--compact">
          <span class="empty-state__icon"><Icon name="search" size={18} /></span>
          <div class="empty-state__title">{t('search.custom.empty')}</div>
          <p class="empty-state__body">{t('search.custom.emptyHint')}</p>
        </div>
      {:else}
        {#each customEngines as engine (engine.id)}
          <div class="row">
            <div class="row__main">
              <div class="row__title">{engine.name}</div>
              <div class="row__desc mono engine-template">{engine.template}</div>
            </div>
            <div class="row__control">
              <button
                class="icon-button"
                title={t('search.custom.remove')}
                aria-label={t('search.custom.remove')}
                onclick={() => void removeEngine(engine.id)}
              >
                <Icon name="trash" size={14} />
              </button>
            </div>
          </div>
        {/each}
      {/if}

      {#if adding}
        <div class="engine-form">
          <label class="engine-form__field">
            <span class="engine-form__label">{t('search.custom.name')}</span>
            <input class="input" bind:value={newName} placeholder={t('search.custom.namePlaceholder')} />
          </label>
          <label class="engine-form__field">
            <span class="engine-form__label">{t('search.custom.template')}</span>
            <input
              class="input input--mono"
              bind:value={newTemplate}
              placeholder="https://search.example/?q=%s"
              spellcheck="false"
            />
          </label>
          <p class="engine-form__hint">{t('search.custom.templateHint')}</p>
          {#if formError}
            <p class="engine-form__error">{formError}</p>
          {/if}
          <div class="engine-form__actions">
            <button class="button button--ghost button--sm" onclick={() => (adding = false)}>
              {t('kernels.cancel')}
            </button>
            <button class="button button--primary button--sm" onclick={() => void saveEngine()}>
              {t('search.custom.save')}
            </button>
          </div>
        </div>
      {/if}
    </div>
  </div>
</div>

<style>
  .home-input {
    width: 220px;
    max-width: 100%;
  }

  .engine-template {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    max-width: 320px;
  }

  .engine-form {
    display: flex;
    flex-direction: column;
    gap: 10px;
    margin-top: 12px;
    padding: 14px;
    border: 1px solid var(--border);
    border-radius: var(--radius-md);
    background: var(--bg-sunken);
  }

  .engine-form__field {
    display: flex;
    flex-direction: column;
    gap: 5px;
  }

  .engine-form__label {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .engine-form__hint {
    margin: 0;
    font-size: var(--text-2xs);
    color: var(--text-muted);
  }

  .engine-form__error {
    margin: 0;
    font-size: var(--text-xs);
    color: var(--danger);
  }

  .engine-form__actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
  }
</style>
