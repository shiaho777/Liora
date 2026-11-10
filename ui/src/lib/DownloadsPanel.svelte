<script lang="ts">
  import Icon from './Icon.svelte';
  import {
    downloads,
    activeDownloadCount,
    clearFinishedDownloads,
    humanSize,
    percentOf,
    revealDownload,
  } from './downloads.svelte';
  import { t } from './i18n.svelte';

  const hasFinished = $derived(
    downloads.items.some((item) => item.state === 'finished' || item.state === 'failed'),
  );
</script>

<div class="downloads" role="dialog" aria-label={t('downloads.title')}>
  <header class="downloads__header">
    <span class="downloads__title">{t('downloads.title')}</span>
    {#if activeDownloadCount() > 0}
      <span class="badge badge--accent">{activeDownloadCount()}</span>
    {/if}
    <span style:flex="1"></span>
    {#if hasFinished}
      <button class="link" onclick={clearFinishedDownloads}>{t('downloads.clear')}</button>
    {/if}
  </header>

  <div class="downloads__list">
    {#if downloads.items.length === 0}
      <div class="empty">{t('downloads.empty')}</div>
    {:else}
      {#each downloads.items as item (item.id)}
        <div class="download">
          <span class="download__icon">
            <Icon name={item.state === 'failed' ? 'warning' : 'download'} size={13} />
          </span>
          <div class="download__main">
            <div class="download__name" title={item.path}>{item.filename}</div>
            {#if item.state === 'failed'}
              <div class="download__status download__status--error">{item.error || t('downloads.failed')}</div>
            {:else if item.state === 'finished'}
              <div class="download__status">{t('downloads.done')} · {humanSize(item.received)}</div>
            {:else}
              <div class="bar"><span style:width="{percentOf(item)}%"></span></div>
              <div class="download__status">
                {percentOf(item)}%
                {#if item.total > 0}· {humanSize(item.received)} / {humanSize(item.total)}{/if}
              </div>
            {/if}
          </div>
          {#if item.state === 'finished'}
            <button class="icon-button" title={t('downloads.reveal')} onclick={() => revealDownload(item)}>
              <Icon name="folder" size={14} />
            </button>
          {/if}
        </div>
      {/each}
    {/if}
  </div>
</div>

<style>
  .downloads {
    position: absolute;
    top: calc(var(--toolbar-height) - 4px);
    right: 10px;
    z-index: 30;
    width: 340px;
    max-height: 420px;
    display: flex;
    flex-direction: column;
    border-radius: var(--radius-md);
    border: 1px solid var(--border);
    background: var(--surface-strong);
    box-shadow: var(--shadow-lg);
    overflow: hidden;
  }

  .downloads__header {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 12px;
    border-bottom: 1px solid var(--border);
  }

  .downloads__title {
    font-size: 12.5px;
    font-weight: 600;
  }

  .downloads__list {
    overflow: auto;
    padding: 6px;
  }

  .download {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 8px 6px;
    border-radius: var(--radius-sm);
  }

  .download:hover {
    background: var(--bg-hover);
  }

  .download__icon {
    width: 26px;
    height: 26px;
    flex: none;
    border-radius: 8px;
    display: grid;
    place-items: center;
    background: var(--bg-sunken);
    color: var(--text-muted);
  }

  .download__main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .download__name {
    font-size: 12.5px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .download__status {
    font-size: 11px;
    color: var(--text-muted);
  }

  .download__status--error {
    color: var(--danger);
  }

  .bar {
    height: 3px;
    border-radius: 2px;
    background: var(--bg-sunken);
    overflow: hidden;
  }

  .bar span {
    display: block;
    height: 100%;
    background: var(--accent);
    transition: width var(--normal) var(--ease);
  }

  .link {
    font-size: 11.5px;
    color: var(--accent);
  }
</style>
