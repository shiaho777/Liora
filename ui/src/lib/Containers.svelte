<script lang="ts">
  import Icon from './Icon.svelte';
  import PageHeader from './PageHeader.svelte';
  import { t } from './i18n.svelte';
  import { value } from './settings-store.svelte';
  import { identityPalette } from './palette';

  export interface ContainerItem {
    id: string;
    name: string;
    color: string;
  }

  interface Props {
    containers: ContainerItem[];
    defaultCount: number;
    counts: Record<string, number>;
    oncreate: (name: string, color: string) => ContainerItem;
    onrename: (id: string, name: string) => void;
    onrecolor: (id: string, color: string) => void;
    ondelete: (id: string) => void;
    onopen: (id?: string) => void;
  }

  let {
    containers,
    defaultCount,
    counts,
    oncreate,
    onrename,
    onrecolor,
    ondelete,
    onopen,
  }: Props = $props();

  const palette = identityPalette;
  let name = $state('');
  let color = $state(palette[0]);
  let renaming = $state('');
  let draft = $state('');
  const compatCef = $derived(
    String(value('kernel.default') ?? 'system') === 'cef' &&
      value<boolean>('kernel.cef.singleProcess') === true,
  );

  function create(): void {
    if (!name.trim()) return;
    const container = oncreate(name, color);
    name = '';
    color = palette[containers.length % palette.length];
    onopen(container.id);
  }

  function beginRename(container: ContainerItem): void {
    renaming = container.id;
    draft = container.name;
  }

  function commitRename(): void {
    const value = draft.trim();
    if (renaming && value) onrename(renaming, value);
    renaming = '';
  }
</script>

<div class="section">
  <PageHeader icon="shield" title={t('containers.title')} subtitle={t('containers.pageLede')} />

  {#if compatCef}
    <div class="callout callout--warning">
      <span class="callout__icon"><Icon name="warning" size={14} /></span>
      <span>{t('containers.cefCompatWarning')}</span>
    </div>
  {/if}

  <div class="card">
    <div class="card__header">
      <div class="card__main">
        <div class="card__title">{t('containers.list')}</div>
        <div class="card__subtitle">{t('containers.listHint')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="container-list">
        <div class="container-row">
          <span class="container-row__dot container-row__dot--default"></span>
          <div class="container-row__main">
            <div class="container-row__title">
              {t('containers.default')}
              <span class="badge">{defaultCount}</span>
            </div>
            <div class="container-row__desc">{t('containers.defaultDesc')}</div>
          </div>
          <button class="button button--sm" onclick={() => onopen(undefined)}>
            <Icon name="plus" size={12} />
            {t('containers.newTab')}
          </button>
        </div>

        {#each containers as container (container.id)}
          <div class="container-row">
            <button
              class="container-row__dot"
              style:background={container.color}
              title={t('containers.color')}
              aria-label={t('containers.color')}
              onclick={() => {
                const index = palette.indexOf(container.color);
                onrecolor(container.id, palette[(index + 1) % palette.length]);
              }}
            ></button>
            <div class="container-row__main">
              {#if renaming === container.id}
                <input
                  class="container-row__rename"
                  bind:value={draft}
                  onblur={commitRename}
                  onkeydown={(event) => {
                    if (event.key === 'Enter') {
                      event.preventDefault();
                      commitRename();
                    } else if (event.key === 'Escape') {
                      renaming = '';
                    }
                  }}
                />
              {:else}
                <div class="container-row__title">
                  {container.name}
                  <span class="badge">{counts[container.id] ?? 0}</span>
                </div>
                <div class="container-row__desc">{t('containers.isolatedDesc')}</div>
              {/if}
            </div>
            <div class="container-row__control">
              <button
                class="icon-button"
                title={t('containers.rename')}
                aria-label={t('containers.rename')}
                onclick={() => beginRename(container)}
              >
                <Icon name="pencil" size={13} />
              </button>
              <button
                class="icon-button"
                title={t('containers.deleteHint')}
                aria-label={t('containers.deleteHint')}
                onclick={() => ondelete(container.id)}
              >
                <Icon name="trash" size={14} />
              </button>
              <button class="button button--sm" onclick={() => onopen(container.id)}>
                <Icon name="plus" size={12} />
                {t('containers.newTab')}
              </button>
            </div>
          </div>
        {/each}
      </div>

      {#if containers.length === 0}
        <div class="containers-empty">{t('containers.empty')}</div>
      {/if}

      <div class="container-create">
        <div class="container-create__title">{t('containers.create')}</div>
        <div class="container-create__row">
          <input
            class="input"
            bind:value={name}
            placeholder={t('containers.namePlaceholder')}
            onkeydown={(event) => event.key === 'Enter' && create()}
          />
          <button class="button button--primary button--sm" onclick={create}>
            <Icon name="plus" size={12} />
            {t('containers.createOpen')}
          </button>
        </div>
        <div class="container-colors">
          {#each palette as item (item)}
            <button
              class="container-color"
              class:container-color--active={color === item}
              style:background={item}
              title={item}
              aria-label={item}
              onclick={() => (color = item)}
            >
              {#if color === item}<Icon name="check" size={9} />{/if}
            </button>
          {/each}
        </div>
      </div>
    </div>
  </div>
</div>

<style>
  .card__main {
    flex: 1;
    min-width: 0;
  }

  .container-list {
    display: flex;
    flex-direction: column;
  }

  .container-row {
    display: flex;
    align-items: center;
    gap: 11px;
    padding: 12px 0;
  }

  .container-row + .container-row {
    border-top: 1px solid var(--border);
  }

  .container-row__dot {
    width: 12px;
    height: 12px;
    flex: none;
    border-radius: 4px;
  }

  .container-row__dot--default {
    background: var(--border-strong);
  }

  .container-row__main {
    flex: 1;
    min-width: 0;
  }

  .container-row__title {
    display: flex;
    align-items: center;
    gap: 7px;
    font-size: var(--text-md);
    font-weight: var(--weight-medium);
  }

  .container-row__desc {
    margin-top: 1px;
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .container-row__rename {
    width: 100%;
    max-width: 260px;
    height: 24px;
    padding: 0 6px;
    border: 1px solid var(--accent);
    border-radius: var(--radius-xs);
    outline: none;
    background: var(--surface);
    color: var(--text);
    font-size: var(--text-sm);
  }

  .container-row__control {
    flex: none;
    display: flex;
    align-items: center;
    gap: 4px;
  }

  .containers-empty {
    padding: 14px 0 3px;
    text-align: center;
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .container-create {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin-top: 12px;
    padding-top: 14px;
    border-top: 1px solid var(--border);
  }

  .container-create__title {
    font-size: var(--text-xs);
    font-weight: var(--weight-semibold);
    color: var(--text-secondary);
  }

  .container-create__row {
    display: flex;
    gap: 6px;
  }

  .container-colors {
    display: flex;
    gap: 7px;
  }

  .container-color {
    width: 20px;
    height: 20px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    color: #fff;
    box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.12);
  }

  .container-color--active {
    box-shadow:
      0 0 0 2px var(--surface),
      0 0 0 4px var(--accent);
  }

  @media (max-width: 700px) {
    .container-row {
      flex-wrap: wrap;
    }

    .container-row__control {
      width: 100%;
      justify-content: flex-end;
    }
  }
</style>
