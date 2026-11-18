<script lang="ts">
  import { onMount } from 'svelte';
  import Icon from './Icon.svelte';
  import { t } from './i18n.svelte';
  import { value } from './settings-store.svelte';
  import { identityPalette } from './palette';
  import { trapFocus } from './focus-trap';

  export interface ContainerItem {
    id: string;
    name: string;
    color: string;
  }

  interface Props {
    containers: ContainerItem[];
    url: string;
    onclose: () => void;
    onopen: (containerId?: string) => void;
    oncreate: (name: string, color: string) => ContainerItem;
  }

  let { containers, url, onclose, onopen, oncreate }: Props = $props();

  const palette = identityPalette;
  let name = $state('');
  let color = $state(palette[0]);
  let nameEl = $state<HTMLInputElement | null>(null);
  let dialogEl = $state<HTMLDivElement | null>(null);

  $effect(() => {
    if (!dialogEl) return;
    return trapFocus(dialogEl);
  });
  const compatCef = $derived(
    String(value('kernel.default') ?? 'system') === 'cef' &&
      value<boolean>('kernel.cef.singleProcess') === true,
  );

  onMount(() => nameEl?.focus());

  function onKeydown(event: KeyboardEvent): void {
    if (event.key === 'Escape') onclose();
  }

  function create(): void {
    const container = oncreate(name, color);
    onopen(container.id);
  }
</script>

<svelte:window onkeydown={onKeydown} />

<!-- svelte-ignore a11y_click_events_have_key_events -->
<div class="container-backdrop" role="presentation" onclick={onclose}>
  <div
    class="container-dialog"
    bind:this={dialogEl}
    role="dialog"
    aria-modal="true"
    aria-label={t('containers.open')}
    tabindex="-1"
    onclick={(event) => event.stopPropagation()}
  >
    <header class="container-dialog__head">
      <span class="container-dialog__icon"><Icon name="shield" size={17} /></span>
      <div class="container-dialog__heading">
        <div class="container-dialog__title">{t('containers.open')}</div>
        <div class="container-dialog__url">{url}</div>
      </div>
      <button class="icon-button" title={t('palette.close')} aria-label={t('palette.close')} onclick={onclose}>
        <Icon name="close" size={13} />
      </button>
    </header>

    <div class="container-dialog__body">
      <div class="container-options">
        <button class="container-option" onclick={() => onopen(undefined)}>
          <span class="container-option__dot container-option__dot--default"></span>
          <span class="container-option__name">{t('containers.default')}</span>
          <span class="container-option__hint">{t('containers.defaultHint')}</span>
          <Icon name="chevron" size={11} />
        </button>
        {#each containers as container (container.id)}
          <button class="container-option" onclick={() => onopen(container.id)}>
            <span class="container-option__dot" style:background={container.color}></span>
            <span class="container-option__name">{container.name}</span>
            <span class="container-option__hint">{t('containers.isolatedHint')}</span>
            <Icon name="chevron" size={11} />
          </button>
        {/each}
      </div>

      <div class="container-create">
        <div class="container-create__title">{t('containers.create')}</div>
        <div class="container-create__row">
          <input
            bind:this={nameEl}
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

      {#if compatCef}
        <div class="callout callout--warning container-warning">
          <span class="callout__icon"><Icon name="warning" size={14} /></span>
          <span>{t('containers.cefCompatWarning')}</span>
        </div>
      {/if}

      <p class="container-note">{t('containers.note')}</p>
    </div>
  </div>
</div>

<style>
  .container-backdrop {
    position: fixed;
    inset: 0;
    z-index: 85;
    display: flex;
    justify-content: center;
    align-items: flex-start;
    padding: 13vh 20px 20px;
    background: rgba(8, 10, 14, 0.46);
    backdrop-filter: blur(5px);
    animation: container-fade 120ms var(--ease);
  }

  .container-dialog {
    width: min(560px, 100%);
    max-height: min(620px, 76vh);
    display: flex;
    flex-direction: column;
    border-radius: 15px;
    border: 1px solid var(--border-strong);
    background: var(--surface-strong);
    box-shadow: var(--shadow-lg);
    overflow: hidden;
    animation: container-in 160ms var(--ease-out);
  }

  .container-dialog__head {
    flex: none;
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 14px 16px;
    border-bottom: 1px solid var(--border);
  }

  .container-dialog__icon {
    width: 32px;
    height: 32px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 9px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .container-dialog__heading {
    flex: 1;
    min-width: 0;
  }

  .container-dialog__title {
    font-size: var(--text-md);
    font-weight: var(--weight-semibold);
  }

  .container-dialog__url {
    margin-top: 2px;
    font-family: var(--font-mono);
    font-size: var(--text-2xs);
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .container-dialog__body {
    flex: 1;
    min-height: 0;
    overflow: auto;
    padding: 14px 16px 16px;
  }

  .container-options {
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .container-option {
    display: flex;
    align-items: center;
    gap: 9px;
    height: 38px;
    padding: 0 9px;
    border-radius: var(--radius-sm);
    color: var(--text-secondary);
    text-align: left;
  }

  .container-option:hover {
    background: var(--bg-hover);
    color: var(--text);
  }

  .container-option__dot {
    width: 10px;
    height: 10px;
    flex: none;
    border-radius: 3px;
  }

  .container-option__dot--default {
    background: var(--border-strong);
  }

  .container-option__name {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-size: var(--text-sm);
    font-weight: var(--weight-medium);
  }

  .container-option__hint {
    flex: none;
    font-size: var(--text-2xs);
    color: var(--text-muted);
  }

  .container-create {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin-top: 14px;
    padding-top: 13px;
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
    padding-top: 2px;
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

  .container-warning {
    margin-top: 12px;
  }

  .container-note {
    margin: 13px 0 0;
    font-size: var(--text-2xs);
    line-height: 1.5;
    color: var(--text-muted);
  }

  @keyframes container-fade {
    from {
      opacity: 0;
    }
    to {
      opacity: 1;
    }
  }

  @keyframes container-in {
    from {
      opacity: 0;
      transform: translateY(-5px) scale(0.985);
    }
    to {
      opacity: 1;
      transform: none;
    }
  }
</style>
