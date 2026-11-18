<script lang="ts">
  import { onMount } from 'svelte';
  import Icon from './Icon.svelte';
  import { call, notify } from './bridge';
  import { toast } from './state.svelte';
  import { t } from './i18n.svelte';
  import { trapFocus } from './focus-trap';
  import { dismissWith } from './dismiss';

  interface Props {
    url: string;
    title: string;
    onclose: () => void;
    oncreated?: () => void;
  }

  let { url, title, onclose, oncreated }: Props = $props();

  let keyword = $state('');
  let interval = $state(60);
  let busy = $state(false);
  let error = $state('');
  let keywordEl = $state<HTMLInputElement | null>(null);
  let dialogEl = $state<HTMLDivElement | null>(null);
  let backdropEl = $state<HTMLDivElement | null>(null);
  let closing = $state(false);

  function close(): void {
    if (closing) return;
    closing = true;
    dismissWith(backdropEl, 'overlay-out', onclose);
  }

  $effect(() => {
    if (!dialogEl) return;
    return trapFocus(dialogEl);
  });

  const intervals = $derived([
    { value: 15, label: t('watch.interval.15') },
    { value: 60, label: t('watch.interval.60') },
    { value: 360, label: t('watch.interval.360') },
    { value: 1440, label: t('watch.interval.1440') },
  ]);

  onMount(() => keywordEl?.focus());

  function onKeydown(event: KeyboardEvent): void {
    if (event.key === 'Escape') close();
  }

  async function create(): Promise<void> {
    if (busy) return;
    busy = true;
    error = '';
    try {
      const record = await call<{ id?: string }>('watch.add', {
        url,
        title: title || url,
        keyword,
        intervalMinutes: Number(interval),
      });
      if (record?.id) notify('ui.checkWatch', { id: record.id });
      toast(t('watch.created', { title: title || url }));
      oncreated?.();
      onclose();
    } catch (reason: any) {
      error = String(reason?.message ?? reason);
    } finally {
      busy = false;
    }
  }
</script>

<svelte:window onkeydown={onKeydown} />

<!-- svelte-ignore a11y_click_events_have_key_events -->
<div class="watch-backdrop" bind:this={backdropEl} role="presentation" onclick={close}>
  <div
    class="watch-dialog"
    bind:this={dialogEl}
    role="dialog"
    aria-modal="true"
    aria-label={t('watch.dialogTitle')}
    tabindex="-1"
    onclick={(event) => event.stopPropagation()}
  >
    <header class="watch-dialog__head">
      <span class="watch-dialog__icon"><Icon name="eye" size={17} /></span>
      <div class="watch-dialog__heading">
        <div class="watch-dialog__title">{t('watch.dialogTitle')}</div>
        <div class="watch-dialog__url">{url}</div>
      </div>
      <button class="icon-button" title={t('palette.close')} aria-label={t('palette.close')} onclick={close}>
        <Icon name="close" size={13} />
      </button>
    </header>

    <div class="watch-dialog__body">
      <label class="field">
        <span class="field__label">{t('watch.keyword')}</span>
        <input
          bind:this={keywordEl}
          class="input"
          bind:value={keyword}
          placeholder={t('watch.keywordPlaceholder')}
          spellcheck="false"
        />
        <span class="field__hint">{t('watch.keywordHint')}</span>
      </label>

      <label class="field">
        <span class="field__label">{t('watch.interval')}</span>
        <select class="select" bind:value={interval}>
          {#each intervals as item (item.value)}
            <option value={item.value}>{item.label}</option>
          {/each}
        </select>
      </label>

      <div class="watch-note">
        <Icon name="shield" size={12} />
        <span>{t('watch.privacy')}</span>
      </div>

      {#if error}
        <div class="callout callout--danger">
          <span class="callout__icon"><Icon name="warning" size={14} /></span>
          <span>{error}</span>
        </div>
      {/if}
    </div>

    <footer class="watch-dialog__foot">
      <button class="button button--ghost button--sm" onclick={close}>{t('rules.cancel')}</button>
      <button class="button button--primary button--sm" disabled={busy} onclick={create}>
        <Icon name="eye" size={13} />
        {busy ? t('watch.creating') : t('watch.create')}
      </button>
    </footer>
  </div>
</div>

<style>
  .watch-backdrop {
    position: fixed;
    inset: 0;
    z-index: 85;
    display: flex;
    justify-content: center;
    align-items: flex-start;
    padding: 16vh 20px 20px;
    background: var(--scrim);
    backdrop-filter: blur(5px);
    animation: watch-fade 120ms var(--ease);
  }

  .watch-backdrop:global(.overlay-out) {
    animation: overlay-out var(--fast) var(--ease) forwards;
  }

  .watch-backdrop:global(.overlay-out) .watch-dialog {
    animation: dialog-out var(--fast) var(--ease) forwards;
  }

  .watch-dialog {
    width: min(520px, 100%);
    border-radius: 15px;
    border: 1px solid var(--border-strong);
    background: var(--surface-strong);
    box-shadow: var(--shadow-lg);
    overflow: hidden;
    animation: watch-in 160ms var(--ease-out);
  }

  .watch-dialog__head {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 14px 16px;
    border-bottom: 1px solid var(--border);
  }

  .watch-dialog__icon {
    width: 32px;
    height: 32px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 9px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .watch-dialog__heading {
    flex: 1;
    min-width: 0;
  }

  .watch-dialog__title {
    font-size: var(--text-md);
    font-weight: var(--weight-semibold);
  }

  .watch-dialog__url {
    margin-top: 2px;
    font-family: var(--font-mono);
    font-size: var(--text-2xs);
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .watch-dialog__body {
    display: flex;
    flex-direction: column;
    gap: 14px;
    padding: 16px;
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: 5px;
  }

  .field__label {
    font-size: var(--text-xs);
    font-weight: var(--weight-medium);
    color: var(--text-secondary);
  }

  .field__hint {
    font-size: var(--text-2xs);
    line-height: 1.45;
    color: var(--text-muted);
  }

  .watch-note {
    display: flex;
    align-items: flex-start;
    gap: 6px;
    padding: 9px 10px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
    font-size: var(--text-2xs);
    line-height: 1.5;
    color: var(--text-muted);
  }

  .watch-dialog__foot {
    display: flex;
    justify-content: flex-end;
    gap: 6px;
    padding: 11px 16px;
    border-top: 1px solid var(--border);
    background: var(--bg-sunken);
  }

  @keyframes watch-fade {
    from {
      opacity: 0;
    }
    to {
      opacity: 1;
    }
  }

  @keyframes watch-in {
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
