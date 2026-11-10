<script lang="ts">
  import Icon from './Icon.svelte';
  import { humanSize, percentOf, revealDownload, type DownloadItem } from './downloads.svelte';
  import { t } from './i18n.svelte';

  interface Props {
    item: DownloadItem;
  }

  let { item }: Props = $props();
</script>

<div class="downloadpage">
  <div class="downloadpage__icon">
    <Icon name="download" size={24} stroke={1.4} />
  </div>
  <h1 class="downloadpage__title">{item.filename}</h1>

  {#if item.state === 'failed'}
    <p class="downloadpage__status error">{item.error || t('downloads.failed')}</p>
  {:else if item.state === 'finished'}
    <p class="downloadpage__status">{t('downloads.done')} · {humanSize(item.received)}</p>
    <button class="button button--ghost" onclick={() => revealDownload(item)}>
      <Icon name="folder" size={13} />
      {t('downloads.reveal')}
    </button>
  {:else}
    <div class="downloadpage__bar"><span style:width="{percentOf(item)}%"></span></div>
    <p class="downloadpage__status">
      {percentOf(item)}%{#if item.total > 0} · {humanSize(item.received)} / {humanSize(item.total)}{/if}
    </p>
  {/if}

  <p class="downloadpage__hint">{t('downloads.hint')}</p>
</div>

<style>
  .downloadpage {
    height: 100%;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 10px;
    padding: 40px 24px;
    background: var(--bg);
  }

  .downloadpage__icon {
    width: 56px;
    height: 56px;
    border-radius: 16px;
    display: grid;
    place-items: center;
    background: var(--accent-soft);
    color: var(--accent);
    margin-bottom: 4px;
  }

  .downloadpage__title {
    margin: 0;
    font-size: 16px;
    font-weight: 650;
    max-width: 60ch;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .downloadpage__status {
    margin: 0;
    font-size: 12.5px;
    color: var(--text-muted);
  }

  .downloadpage__status.error {
    color: var(--danger);
  }

  .downloadpage__bar {
    width: min(320px, 80%);
    height: 4px;
    border-radius: 2px;
    background: var(--bg-sunken);
    overflow: hidden;
  }

  .downloadpage__bar span {
    display: block;
    height: 100%;
    background: var(--accent);
  }

  .downloadpage__hint {
    margin-top: 6px;
    font-size: 11.5px;
    color: var(--text-muted);
  }
</style>
